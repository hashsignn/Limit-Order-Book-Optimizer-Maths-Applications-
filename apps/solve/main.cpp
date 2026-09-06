// Solves the market-making MDP offline and writes the policy table.
//
//   ./build/stats --capture data/samples/<pair>_bitstamp.jsonl.gz --outdir csv
//   py tools/mdp_params.py --csv csv --out policy
//   ./build/solve --params policy/mdp.json --pair ethusd --out policy/ethusd.bin
//
// Nothing here runs in a trading loop. The output is an array of one byte per
// state, and reading it is a bounds check and an index.
//
// This program REFUSES to guess. Where ten minutes of data cannot identify a
// parameter it says which one and stops, rather than substituting a plausible
// default and producing a table that looks exactly like a calibrated one. Three
// come up in practice on the captures in data/samples:
//
//   btcusd  the reconstructed touch teleports rather than moving, so the mid
//           dynamics are the reconstruction's, not the market's
//   xrpusd  the fill hazard does not fall with volume ahead — 19 fills at the
//           front against 10 in the deepest bucket orders them by noise, and
//           queue position is the one thing this state variable exists to carry
//   ethusd  the share of volume reaching one tick past the touch rests on six
//           prints, so --level-ratio must be given explicitly
#include <algorithm>
#include <cmath>
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

// Bucket names describe VOLUME AHEAD, not rank. "front" and "back" were the old
// quartile vocabulary and they hid the thing that mattered: a quartile is a
// position in a ranking, and fill hazard does not care where you rank, it cares
// how much has to trade before it reaches you.
const char* queue_name(int q) {
  static const char* n[kQueueBuckets] = {"alone", "<2%", "<10%", "<35%", "deep"};
  return (q >= 0 && q < kQueueBuckets) ? n[q] : "?";
}

const char* level_name(int l) {
  static const char* n[kQuoteLevels] = {"touch", "+1", "+2"};
  return (l >= 0 && l < kQuoteLevels) ? n[l] : "?";
}

const char* side_name(int side_state) {
  static char buf[32];
  if (!quoting(side_state)) { std::snprintf(buf, sizeof buf, "no quote"); return buf; }
  std::snprintf(buf, sizeof buf, "%s/%s", level_name(level_of(side_state)),
                queue_name(queue_of(side_state)));
  return buf;
}

const char* act_name(int a) {
  return a == 0 ? "pull" : level_name(a - 1);
}

}  // namespace

int main(int argc, char** argv) {
  const char* params_path = "policy/mdp.json";
  const char* pair = nullptr;
  const char* out_path = nullptr;
  double phi_per_s = 10.0, horizon_s = 1.0, level_ratio = -1.0;
  bool force = false;

  for (int i = 1; i < argc; ++i) {
    const bool nx = (i + 1 < argc);
    if      (std::strcmp(argv[i], "--params") == 0 && nx) params_path = argv[++i];
    else if (std::strcmp(argv[i], "--pair")   == 0 && nx) pair = argv[++i];
    else if (std::strcmp(argv[i], "--out")    == 0 && nx) out_path = argv[++i];
    else if (std::strcmp(argv[i], "--penalty") == 0 && nx) phi_per_s = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--horizon") == 0 && nx) horizon_s = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--level-ratio") == 0 && nx) level_ratio = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--force") == 0) force = true;
    else {
      std::fprintf(stderr,
        "solve --pair <name> [--params policy/mdp.json] [--out <file.bin>]\n"
        "  --penalty <phi>       inventory penalty, ticks per lot squared per SECOND (10).\n"
        "                        Per second, not per epoch: a preference should not\n"
        "                        change because the decision grid did.\n"
        "  --horizon <seconds>   how far ahead the policy cares, in SECONDS (1.0).\n"
        "                        Converted to a per-epoch discount here. Given as a\n"
        "                        per-epoch number it would mean a different horizon\n"
        "                        every time the decision grid changed.\n"
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
  p.dt_s = to_double(json::find_scalar(P, "dt_ms"), 100.0) / 1000.0;
  if (!(horizon_s > 0.0)) { std::fprintf(stderr, "--horizon must be positive\n"); return 2; }
  // A discount is a per-epoch number; a horizon is a preference. exp(-dt/H)
  // gives the epoch discount whose e-folding time is H seconds, so the same
  // --horizon means the same thing on any grid.
  p.discount = std::exp(-p.dt_s / horizon_s);
  // The preference is per second; the model charges it per epoch.
  p.inventory_penalty = phi_per_s * p.dt_s;

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

  // Fill probability by queue bucket, at the touch.
  //
  // The buckets are the ones in state.hpp — absolute volume ahead as a fraction
  // of the mean touch depth — and mdp_params.py stamps each measured row with
  // the index it belongs to, so the two files cannot drift into disagreeing
  // about which row is which. Reading the index beats string-matching names:
  // a name the solver does not recognise silently became a bucket of zeros,
  // which the solver then read as "a quote here never fills".
  {
    double at_touch[kQueueBuckets] = {};
    double wsum[kQueueBuckets] = {};
    const json::View H = json::find(P, "fill_hazard");
    std::size_t i = 0; json::View e;
    while (json::array_next(H, i, &e)) {
      const int slot = static_cast<int>(to_double(json::find_scalar(e, "index"), -1.0));
      const double pf = to_double(json::find_scalar(e, "p_fill_per_step"));
      const double n  = to_double(json::find_scalar(e, "n"), 1.0);
      if (slot >= 0 && slot < kQueueBuckets) { at_touch[slot] += pf * n; wsum[slot] += n; }
    }
    // A bucket the data never visited is not a bucket where nothing fills. It is
    // a bucket we know nothing about, and leaving it at zero tells the solver
    // quoting there is pointless — a claim, not the absence of one. Being alone
    // at a price is the bucket this bites: it is the best place a quote can be
    // and the rarest to observe, and a zero there says the opposite.
    //
    // Fill hazard falls monotonically in volume ahead, so an unmeasured bucket
    // is bounded by its neighbours: the deeper one from below, the shallower one
    // from above. Take the DEEPER neighbour wherever there is one. That is the
    // lower bound, and the direction that matters — an overstated hazard deep in
    // the queue is exactly the error that made quoting behind the touch look
    // like the better idea, and it is not an error worth making twice.
    bool have[kQueueBuckets] = {};
    for (int q = 0; q < kQueueBuckets; ++q) {
      have[q] = wsum[q] > 0;
      if (have[q]) p.p_fill[0][q] = at_touch[q] / wsum[q];
    }
    int filled_down = 0, filled_up = 0;
    for (int q = kQueueBuckets - 2; q >= 0; --q)
      if (!have[q] && have[q + 1]) { p.p_fill[0][q] = p.p_fill[0][q + 1]; have[q] = true; ++filled_down; }
    // Only if the DEEP end is what is missing, where no lower bound exists.
    for (int q = 1; q < kQueueBuckets; ++q)
      if (!have[q] && have[q - 1]) { p.p_fill[0][q] = p.p_fill[0][q - 1]; have[q] = true; ++filled_up; }
    if (filled_down + filled_up > 0)
      std::printf("  \033[33m%d queue bucket(s) unmeasured: %d took the deeper neighbour's "
                  "hazard (a lower bound), %d took the shallower one\033[0m\n",
                  filled_down + filled_up, filled_down, filled_up);

    const json::View LR = json::find(P, "level_ratio");
    const bool measured = json::find_scalar(LR, "measured") == "true";
    double ratio = level_ratio;
    if (ratio < 0.0) {
      if (!measured) {
        std::fprintf(stderr,
          "\nThe fill rate one tick behind the touch is not measured for %s:\n"
          "  %.*s prints reached that level, giving a share of %.*s\n"
          "A quote further from the touch cannot fill MORE often than one at it —\n"
          "every market order reaching the second level passed through the first —\n"
          "so a share at or above 1 is noise, not a result, and a share off a\n"
          "handful of prints is a point estimate with nothing behind it.\n"
          "Pass --level-ratio <r> with a value you are willing to defend.\n",
          pair,
          static_cast<int>(json::find_scalar(LR, "prints_one_behind").size()),
          json::find_scalar(LR, "prints_one_behind").data(),
          static_cast<int>(json::find_scalar(LR, "ratio").size()),
          json::find_scalar(LR, "ratio").data());
        return 3;
      }
      ratio = to_double(json::find_scalar(LR, "ratio"), 0.5);
    }
    // One measured ratio, applied geometrically. Every market order that reaches
    // level two passed through level one, so the decay compounds; assuming the
    // same ratio at each step is the least that can be said from one
    // measurement, and it is stated here rather than hidden in a table.
    for (int l = 1; l < kQuoteLevels; ++l) {
      double r = 1.0;
      for (int k = 0; k < l; ++k) r *= ratio;
      for (int q = 0; q < kQueueBuckets; ++q) p.p_fill[l][q] = p.p_fill[0][q] * r;
    }
    std::printf("  level ratio  %.3f %s, compounded across %d levels\n", ratio,
                measured && level_ratio < 0 ? "(measured)" : "(given)", kQuoteLevels);
  }

  // What a fill is worth, from the measured spread rather than an assumption.
  //
  // MdpParams defaults these to {0.5, 1.5, 2.5}, which is right only for a
  // one-tick book. On a two-tick book it halves the edge at the touch while
  // leaving the edge one tick behind nearly correct, and the solver duly
  // concludes that quoting behind is optimal — which it then did, taking a
  // thirty-eighth of the fills the naive touch-joiner took.
  {
    const json::View SP = json::find(P, "spread");
    const double med = to_double(json::find_scalar(SP, "median_ticks"), 1.0);
    const double half = (med > 0.0 ? med : 1.0) / 2.0;
    std::printf("  spread       %.0f ticks median -> edge", med);
    for (int l = 0; l < kQuoteLevels; ++l) {
      p.edge_ticks[l] = half + static_cast<double>(l);
      std::printf(" %.2f@%s", p.edge_ticks[l], level_name(l));
    }
    std::putchar('\n');
  }

  // How fast the queue in front drains, turned into one transition per bucket.
  //
  // The measurement is a single number: what fraction of a full touch queue
  // leaves per epoch. The buckets are not equally wide, so that one rate has to
  // be divided by how far each bucket is from the next one down — otherwise
  // reaching the front of the queue is modelled as exactly as likely as
  // escaping the back of it, and the front is where all the value is.
  double drain = 0.0;
  {
    const json::View Q = json::find(P, "queue");
    drain = to_double(json::find_scalar(Q, "drain_fraction_per_step"));
    if (!(drain > 0.0)) drain = 0.0;
    for (int q = 1; q < kQueueBuckets; ++q) {
      const double gap = queue_typical(q) - queue_floor(q);
      p.p_advance[q] = gap <= 0.0 ? 1.0 : std::min(1.0, drain / gap);
    }
  }

  // The reference depth the buckets are fractions of. It travels in the table
  // header, because an executor that divides by a different number is looking
  // up a different row for every state and nothing about that looks wrong.
  std::int64_t queue_scale = 0;
  {
    const json::View Q = json::find(P, "queue");
    const double sz = to_double(json::find_scalar(Q, "mean_touch_size"), 0.0);
    if (!(sz > 0.0)) {
      std::fprintf(stderr, "\nqueue.mean_touch_size is missing or non-positive for %s.\n"
                           "It is the scale every queue bucket is a fraction of; without it the\n"
                           "table cannot say what \"10%% of a queue ahead\" means and the executor\n"
                           "would bucket on a different scale than the solve did.\n", pair);
      return 3;
    }
    queue_scale = static_cast<std::int64_t>(sz + 0.5);
  }

  std::string why;
  if (!p.validate(&why)) { std::fprintf(stderr, "the process is not a valid MDP: %s\n", why.c_str()); return 3; }
  // Every parameter can be a legal probability and the process still not be one.
  // Checked here, once, because the alternative is finding out from a residual
  // of 2e+57 after twenty thousand sweeps and six minutes.
  if (!stochastic(p, &why)) {
    std::fprintf(stderr, "the transition function is not a probability distribution:\n  %s\n"
                         "Value iteration is a contraction only if it is; this would diverge.\n",
                 why.c_str());
    return 3;
  }

  std::printf("\nsolving %s   %u states x %d actions\n"
              "  epoch %.3f ms   horizon %.3g s -> discount %.6f per epoch\n"
              "  penalty %.4g ticks/lot^2/s = %.4g per epoch\n",
              pair, kNumStates, kNumActions,
              1e3 * p.dt_s, horizon_s, p.discount, phi_per_s, p.inventory_penalty);
  std::printf("  mid move %.2f ticks   P(up) %.3f..%.3f across imbalance\n",
              p.move_ticks, p.p_up[0], p.p_up[kImbBuckets - 1]);
  std::printf("  queue scale %lld shares, %.3f%% of it drains per epoch\n",
              static_cast<long long>(queue_scale), 100.0 * drain);
  std::printf("  %-14s", "queue bucket");
  for (int q = 0; q < kQueueBuckets; ++q) std::printf("%10s", queue_name(q));
  std::printf("\n  %-14s", "P(fill)@touch");
  for (int q = 0; q < kQueueBuckets; ++q) std::printf("%10.2e", p.p_fill[0][q]);
  std::printf("\n  %-14s", "P(advance)");
  for (int q = 0; q < kQueueBuckets; ++q) std::printf("%10.3f", p.p_advance[q]);
  std::putchar('\n');

  const SolveResult r = solve(p);
  std::printf("  %s after %d sweeps, residual %.3g\n",
              r.converged ? "converged"
                          : (r.diverged ? "DIVERGED" : "STOPPED WITHOUT CONVERGING"),
              r.sweeps, r.residual);
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
                           static_cast<std::uint64_t>(r.sweeps), queue_scale, p.dt_s, &why)) {
      std::fprintf(stderr, "cannot write %s: %s\n", out_path, why.c_str());
      return 5;
    }
    std::printf("\nwrote %s  (%u states, param hash %016llx)\n", out_path, kNumStates,
                static_cast<unsigned long long>(p.hash()));
  }
  return 0;
}
