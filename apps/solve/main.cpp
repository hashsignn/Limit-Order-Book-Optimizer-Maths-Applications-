// Solves the market-making MDP offline and writes the policy table.
//
//   ./build/stats --capture data/samples/<pair>_bitstamp.jsonl.gz --outdir csv
//   py tools/mdp_params.py --csv csv --out policy
//   ./build/solve --params policy/mdp.json --pair xrpusd --out policy/xrpusd.bin
//
// Nothing here runs in a trading loop. The output is an array of one byte per
// state, and reading it is a bounds check and an index.
//
// This program REFUSES to guess. Where ten minutes of data cannot identify a
// parameter it says which one and stops, rather than substituting a plausible
// default and producing a table that looks exactly like a calibrated one. The
// two that come up in practice: btcusd's mid dynamics, where the reconstructed
// touch teleports instead of moving, and the fill rate one tick behind the
// touch on instruments with too few fills there to measure it.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "lob/feed/json.hpp"
#include "lob/feed/line_reader.hpp"
#include "lob/policy/mdp.hpp"
#include "lob/policy/table.hpp"

using namespace lob;
using namespace lob::policy;

namespace {

double to_double(json::View v, double fallback = 0.0) {
  if (v.empty()) return fallback;
  char buf[64];
  const std::size_t n = v.size() < sizeof(buf) - 1 ? v.size() : sizeof(buf) - 1;
  std::memcpy(buf, v.data(), n);
  buf[n] = '\0';
  char* end = nullptr;
  const double d = std::strtod(buf, &end);
  return (end == buf) ? fallback : d;
}

bool slurp(const char* path, std::string& out) {
  LineReader r;
  if (!r.open(path)) { std::fprintf(stderr, "%s\n", r.error().c_str()); return false; }
  std::string_view l;
  while (r.next(&l)) out.append(l.data(), l.size());
  return !out.empty();
}

const char* side_name(int side_state) {
  static char buf[32];
  if (!quoting(side_state)) { std::snprintf(buf, sizeof buf, "no quote"); return buf; }
  static const char* lvl[] = {"touch", "behind"};
  static const char* q[]   = {"front", "2nd", "3rd", "back"};
  std::snprintf(buf, sizeof buf, "%s/%s", lvl[level_of(side_state)], q[queue_of(side_state)]);
  return buf;
}

const char* act_name(int a) {
  switch (a) { case 0: return "pull"; case 1: return "touch"; default: return "behind"; }
}

}  // namespace

int main(int argc, char** argv) {
  const char* params_path = "policy/mdp.json";
  const char* pair = nullptr;
  const char* out_path = nullptr;
  double phi = 0.01, discount = 0.999, level_ratio = -1.0;
  bool force = false;

  for (int i = 1; i < argc; ++i) {
    const bool nx = (i + 1 < argc);
    if      (std::strcmp(argv[i], "--params") == 0 && nx) params_path = argv[++i];
    else if (std::strcmp(argv[i], "--pair")   == 0 && nx) pair = argv[++i];
    else if (std::strcmp(argv[i], "--out")    == 0 && nx) out_path = argv[++i];
    else if (std::strcmp(argv[i], "--penalty") == 0 && nx) phi = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--discount") == 0 && nx) discount = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--level-ratio") == 0 && nx) level_ratio = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--force") == 0) force = true;
    else {
      std::fprintf(stderr,
        "solve --pair <name> [--params policy/mdp.json] [--out <file.bin>]\n"
        "  --penalty <phi>       inventory penalty, ticks per lot squared per epoch (0.01)\n"
        "  --discount <beta>     per-epoch discount (0.999)\n"
        "  --level-ratio <r>     fill rate one tick behind the touch, relative to at it.\n"
        "                        Required when the data could not measure it.\n"
        "  --force               solve anyway on an instrument marked unusable\n");
      return 2;
    }
  }
  if (pair == nullptr) { std::fprintf(stderr, "--pair is required\n"); return 2; }

  std::string text;
  if (!slurp(params_path, text)) return 2;
  const json::View P = json::find(text, pair);
  if (P.empty()) { std::fprintf(stderr, "no parameters for %s in %s\n", pair, params_path); return 2; }

  // ---- refuse to solve a process the data could not identify ----
  const json::View usable = json::find_scalar(P, "usable_for_mdp");
  if (usable == "false") {
    std::fprintf(stderr, "\n%s is marked unusable for the MDP:\n", pair);
    const json::View why = json::find(P, "unusable_because");
    std::size_t i = 0; json::View r;
    while (json::array_next(why, i, &r)) std::fprintf(stderr, "  - %.*s\n", static_cast<int>(r.size()), r.data());
    if (!force) {
      std::fprintf(stderr, "\nRefusing to solve. A table built on a process this poorly measured is\n"
                           "indistinguishable from a calibrated one once it is a file on disk.\n"
                           "Pass --force if you want it anyway, for a test.\n");
      return 3;
    }
    std::fprintf(stderr, "  (--force given: solving anyway)\n");
  }

  MdpParams p;
  p.discount = discount;
  p.inventory_penalty = phi;
  p.dt_s = to_double(json::find_scalar(P, "dt_ms"), 100.0) / 1000.0;

  const json::View mid = json::find(P, "mid");
  p.move_ticks = to_double(json::find_scalar(mid, "median_abs_move_ticks"), 0.5);
  if (!(p.move_ticks > 0.0)) p.move_ticks = 0.5;

  // Mid direction conditional on imbalance: the reason imbalance is in the
  // state at all. Falls back to the unconditional rates for a bucket the data
  // never visited often enough to condition on.
  const json::View imb = json::find(P, "imbalance");
  const double pu0 = to_double(json::find_scalar(mid, "p_up"), 0.0);
  const double pd0 = to_double(json::find_scalar(mid, "p_down"), 0.0);
  for (int k = 0; k < kImbBuckets; ++k) { p.p_up[k] = pu0; p.p_down[k] = pd0; }
  for (const char* key : {"p_next_move_up", "p_next_move_down"}) {
    const json::View arr = json::find(imb, key);
    std::size_t i = 0; json::View e; int k = 0;
    while (json::array_next(arr, i, &e) && k < kImbBuckets) {
      if (e != "null") {
        const double v = to_double(e, -1.0);
        if (v >= 0.0) (std::strcmp(key, "p_next_move_up") == 0 ? p.p_up : p.p_down)[k] = v;
      }
      ++k;
    }
  }
  {
    const json::View T = json::find(imb, "transition");
    std::size_t i = 0; json::View row; int r = 0;
    while (json::array_next(T, i, &row) && r < kImbBuckets) {
      std::size_t j = 0; json::View e; int c = 0;
      while (json::array_next(row, j, &e) && c < kImbBuckets) p.imb_transition[r][c++] = to_double(e);
      ++r;
    }
  }

  // Fill probability by queue quartile, at the touch. The measured buckets are
  // "empty" plus quartiles of the positive queue; empty and q1 are both the
  // front of the queue and are pooled into bucket 0.
  {
    double at_touch[kQueueBuckets] = {};
    double wsum[kQueueBuckets] = {};
    const json::View H = json::find(P, "fill_hazard");
    std::size_t i = 0; json::View e;
    while (json::array_next(H, i, &e)) {
      const json::View b = json::find_scalar(e, "bucket");
      const double pf = to_double(json::find_scalar(e, "p_fill_per_step"));
      const double n  = to_double(json::find_scalar(e, "n"), 1.0);
      int slot = -1;
      if (b == "empty" || b == "q1") slot = 0;
      else if (b == "q2") slot = 1;
      else if (b == "q3") slot = 2;
      else if (b == "q4") slot = 3;
      if (slot >= 0) { at_touch[slot] += pf * n; wsum[slot] += n; }
    }
    for (int q = 0; q < kQueueBuckets; ++q)
      p.p_fill[0][q] = wsum[q] > 0 ? at_touch[q] / wsum[q] : 0.0;

    const json::View LR = json::find(P, "level_ratio");
    const bool measured = json::find_scalar(LR, "measured") == "true";
    double ratio = level_ratio;
    if (ratio < 0.0) {
      if (!measured) {
        std::fprintf(stderr,
          "\nThe fill rate one tick behind the touch is not measured for %s:\n"
          "  %.*s fills observed there, ratio %.*s\n"
          "A quote further from the touch cannot fill MORE often than one at it —\n"
          "every market order reaching the second level passed through the first —\n"
          "so a ratio at or above 1 is noise, not a result.\n"
          "Pass --level-ratio <r> with a value you are willing to defend.\n",
          pair,
          static_cast<int>(json::find_scalar(LR, "fills_one_behind").size()),
          json::find_scalar(LR, "fills_one_behind").data(),
          static_cast<int>(json::find_scalar(LR, "ratio").size()),
          json::find_scalar(LR, "ratio").data());
        return 3;
      }
      ratio = to_double(json::find_scalar(LR, "ratio"), 0.5);
    }
    for (int q = 0; q < kQueueBuckets; ++q) p.p_fill[1][q] = p.p_fill[0][q] * ratio;
    std::printf("  level ratio  %.3f %s\n", ratio, measured && level_ratio < 0 ? "(measured)" : "(given)");
  }

  // What a fill is worth, from the measured spread rather than an assumption.
  //
  // MdpParams defaults these to {0.5, 1.5}, which is right only for a one-tick
  // book. On a two-tick book it halves the edge at the touch while leaving the
  // edge one tick behind nearly correct, and the solver duly concludes that
  // quoting behind is optimal — which it then did, taking a thirty-eighth of
  // the fills the naive touch-joiner took.
  {
    const json::View SP = json::find(P, "spread");
    const double med = to_double(json::find_scalar(SP, "median_ticks"), 1.0);
    const double half = (med > 0.0 ? med : 1.0) / 2.0;
    p.edge_ticks[0] = half;          // at the touch
    p.edge_ticks[1] = half + 1.0;    // one tick behind it
    std::printf("  spread       %.0f ticks median -> edge %.2f at touch, %.2f one behind\n",
                med, p.edge_ticks[0], p.edge_ticks[1]);
  }

  // Advancing one quartile means a quarter of the queue in front leaving.
  {
    const json::View Q = json::find(P, "queue");
    const double drain = to_double(json::find_scalar(Q, "drain_fraction_per_step"));
    p.p_advance = drain <= 0.0 ? 0.0 : (drain * kQueueBuckets > 1.0 ? 1.0 : drain * kQueueBuckets);
  }

  std::string why;
  if (!p.validate(&why)) { std::fprintf(stderr, "the process is not a valid MDP: %s\n", why.c_str()); return 3; }

  std::printf("\nsolving %s   %u states x %d actions   discount %.4f   penalty %.4g ticks/lot^2\n",
              pair, kNumStates, kNumActions, p.discount, p.inventory_penalty);
  std::printf("  mid move %.2f ticks   P(up) %.3f..%.3f across imbalance   queue advance %.3f/epoch\n",
              p.move_ticks, p.p_up[0], p.p_up[kImbBuckets - 1], p.p_advance);
  std::printf("  P(fill per epoch) at touch, front to back: ");
  for (int q = 0; q < kQueueBuckets; ++q) std::printf("%.2e ", p.p_fill[0][q]);
  std::putchar('\n');

  const SolveResult r = solve(p);
  std::printf("  %s after %d sweeps, residual %.3g\n",
              r.converged ? "converged" : "STOPPED WITHOUT CONVERGING", r.sweeps, r.residual);
  if (!r.converged) return 4;

  // ---- what did it decide? ----
  std::uint64_t by_action[kNumActions] = {};
  for (std::uint8_t a : r.policy) ++by_action[a];
  std::printf("\naction mix over all states\n");
  for (int a = 0; a < kNumActions; ++a) {
    if (by_action[a] == 0) continue;
    const Action ac = decode_action(static_cast<std::uint8_t>(a));
    std::printf("  bid %-6s ask %-6s  %6llu states  %5.1f%%\n", act_name(ac.bid), act_name(ac.ask),
                static_cast<unsigned long long>(by_action[a]),
                100.0 * static_cast<double>(by_action[a]) / kNumStates);
  }

  std::printf("\na few states, and why\n");
  const State probes[] = {
    {0,  kNoQuote, kNoQuote, 2},
    {0,  make_side(0, 0), make_side(0, 0), 2},
    {0,  make_side(0, kBackOfQueue), make_side(0, kBackOfQueue), 2},
    {+kMaxInventory, make_side(0, 0), make_side(0, 0), 2},
    {-kMaxInventory, make_side(0, 0), make_side(0, 0), 2},
    {0,  make_side(0, 0), make_side(0, 0), 0},
    {0,  make_side(0, 0), make_side(0, 0), kImbBuckets - 1},
  };
  for (const State& s : probes) {
    const Action a = decode_action(r.policy[encode(s)]);
    std::printf("  inv %+d  bid %-12s ask %-12s imb %d  ->  bid %-6s ask %-6s   V=%.4f\n",
                s.inventory, side_name(s.bid), side_name(s.ask), s.imb,
                act_name(a.bid), act_name(a.ask), r.value[encode(s)]);
  }

  if (out_path != nullptr) {
    if (!PolicyTable::save(out_path, r.policy, r.value, p.hash(), p.discount, r.residual,
                           static_cast<std::uint64_t>(r.sweeps), &why)) {
      std::fprintf(stderr, "cannot write %s: %s\n", out_path, why.c_str());
      return 5;
    }
    std::printf("\nwrote %s  (%u states, param hash %016llx)\n", out_path, kNumStates,
                static_cast<unsigned long long>(p.hash()));
  }
  return 0;
}
