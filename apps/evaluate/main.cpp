// Phase 5's acceptance test: does the tabulated policy beat the baselines?
//
//   ./build/stats --synthetic 2000000 --seed 20260904 --grid-ms 1 --label simcal --outdir simcsv
//   py tools/mdp_params.py --csv simcsv --out simpolicy --dt-ms 1 --only simcal
//   ./build/solve --params simpolicy/mdp.json --pair simcal --out simpolicy/simcal.bin
//   ./build/evaluate --table simpolicy/simcal.bin
//
// OUT OF SAMPLE means what it says. The process is measured on one seed and the
// policy is solved for it; every seed the comparison runs on is a different
// one. A policy evaluated on the flow it was fitted to is measuring its own
// memory.
//
// Every strategy goes through the identical driver in strat/driver.hpp, on
// identical flow per seed, with the same message budget, hysteresis, position
// limit and fee schedule. The only thing that varies is which Quote comes back.
//
// WHY MANY SEEDS
//
// One run's answer is one draw from a distribution, and market-making P&L on a
// short sample is dominated by whether the mid happened to drift. Each seed is
// an independent draw of the whole session, so the difference against the best
// baseline is measured pairwise per seed and the interval is taken over seeds.
// That controls for the drift; a single run cannot.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "lob/policy/state.hpp"
#include "lob/policy/table.hpp"
#include "lob/sim/simulator.hpp"
#include "lob/strat/driver.hpp"
#include "lob/strat/pnl.hpp"
#include "lob/strat/quoting.hpp"
#include "lob/strat/tabulated.hpp"

using namespace lob;

namespace {

void banner(const char* t) {
  std::printf("\n\033[1m%s\033[0m\n", t);
  for (std::size_t i = 0; i < std::strlen(t); ++i) std::putchar('-');
  std::putchar('\n');
}

// Negative means "leave whatever FlowConfig declares". A tool that hardcodes
// its own copy of a default silently overrides the header it was derived in —
// which is what happened here: the exogenous-drift ceiling was worked out and
// written into flow.hpp, and this file went on forcing the old value, so the
// sweep measured a process nobody had configured.
double g_drift = -1.0;
double g_informed = -1.0;

SimConfig make_config(std::uint64_t seed, bool latency, Nanos median_ns) {
  const double drift = g_drift;
  SimConfig c;
  c.use_latency      = latency;
  // The process fitted to the captures -- see apps/backtest and
  // docs/KNOWN-ISSUES.md 4 and 5. Every acceptance number recorded before this
  // was measured on a process whose touch took 35,000 events to clear, sampled
  // 40,000 events to a decision epoch.
  c.flow             = FlowConfig::ethusd_queue_reactive();
  c.flow.seed        = seed;
  c.flow.mid         = 10'000;
  if (drift >= 0.0)      c.flow.drift_prob    = drift;
  if (g_informed >= 0.0) c.flow.informed_frac = g_informed;
  c.latency.seed     = seed ^ std::uint64_t{0x9E3779B97F4A7C15};
  c.latency.median_ns = median_ns;
  return c;
}

// The position limit the policy was solved under. Every baseline gets the same
// one, or the comparison is between risk appetites rather than between rules.
QuoteParams base_params() {
  QuoteParams p;
  p.size = 10;
  p.max_inventory = policy::kMaxInventory * p.size;
  return p;
}

struct Row {
  std::string         name;
  std::vector<double> net;        // one per seed
  std::vector<double> spread;
  std::vector<double> adverse;
  std::vector<double> end_inv;
  double passive = 0, aggressive = 0, peak = 0;
  std::vector<double> per_fill;   // pooled across seeds
  double fills = 0, requotes = 0;
  [[nodiscard]] double mean_net() const {
    if (net.empty()) return 0.0;
    double s = 0.0; for (double v : net) s += v;
    return s / static_cast<double>(net.size());
  }
};

void record(Row& r, const RunResult& x) {
  r.net.push_back(x.pnl());
  r.spread.push_back(x.attr.spread_capture);
  r.adverse.push_back(x.attr.adverse_sel);
  r.end_inv.push_back(static_cast<double>(x.stats.inventory));
  r.per_fill.insert(r.per_fill.end(), x.per_fill_pnl.begin(), x.per_fill_pnl.end());
  r.fills      += static_cast<double>(x.attr.n_fills);
  r.passive    += static_cast<double>(x.attr.n_passive);
  r.aggressive += static_cast<double>(x.attr.n_aggressive);
  r.peak       += static_cast<double>(x.peak_inventory);
  r.requotes += static_cast<double>(x.requotes);
}

}  // namespace

int main(int argc, char** argv) {
  const char* table_path = nullptr;
  const char* xmodel_path = nullptr;
  int seeds = 24, events = 300'000;
  bool latency = true;
  std::uint64_t calib_seed = 20260904;
  // Which family of seeds the comparison runs on. The acceptance test uses
  // 1000; anything chosen by looking at results — the inventory penalty, the
  // horizon — has to be chosen on a different family, or the number it produces
  // is a fit to the test. --seed-base makes that separation visible in the
  // command line instead of implied by a comment.
  std::uint64_t seed_base = 1000;
  // The generator puts 2 us between events and moves the mid about a tick
  // every few ms, so a 1 ms round trip is 500 events of staleness — market
  // making is impossible by construction and abstaining wins. The default is
  // set proportionate to the process; --sweep-latency shows where the line is.
  Nanos median_ns = 10'000;
  bool sweep = false, sweep_drift = false, sweep_informed = false, probe = false;

  for (int i = 1; i < argc; ++i) {
    const bool nx = (i + 1 < argc);
    if      (std::strcmp(argv[i], "--table") == 0 && nx) table_path = argv[++i];
    else if (std::strcmp(argv[i], "--out-of-model") == 0 && nx) xmodel_path = argv[++i];
    else if (std::strcmp(argv[i], "--seeds")  == 0 && nx) seeds  = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--seed-base") == 0 && nx)
      seed_base = std::strtoull(argv[++i], nullptr, 10);
    else if (std::strcmp(argv[i], "--tune") == 0) seed_base = 700;
    else if (std::strcmp(argv[i], "--events") == 0 && nx) events = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--no-latency") == 0) latency = false;
    else if (std::strcmp(argv[i], "--latency-ns") == 0 && nx) median_ns = std::atoll(argv[++i]);
    else if (std::strcmp(argv[i], "--sweep-latency") == 0) sweep = true;
    // How volatile the generator is, per event. The default of 0.02 steps
    // the mid a tick every ~100 us while an order rests ~15 ms, so the
    // one-tick spread never compensates for the move and NO market maker
    // can profit. A real venue's spread sits near its short-horizon
    // volatility for exactly that reason. --sweep-drift finds the value
    // where naive touch-joining is marginally viable, which is the
    // weakest process on which the acceptance question is answerable.
    else if (std::strcmp(argv[i], "--drift") == 0 && nx) g_drift = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--sweep-drift") == 0) sweep_drift = true;
    // The share of aggressive orders that are informed. This is the parameter
    // that decides whether market making is a business at all: the maker keeps
    // the spread from the uninformed and pays impact to the informed.
    else if (std::strcmp(argv[i], "--informed") == 0 && nx) g_informed = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--sweep-informed") == 0) sweep_informed = true;
    else if (std::strcmp(argv[i], "--probe") == 0) probe = true;
    else {
      std::fprintf(stderr,
        "evaluate --table <file.bin> [--out-of-model <file.bin>] [--seeds 24]\n"
        "         [--events 300000] [--no-latency] [--latency-ns 10000]\n"
        "         [--sweep-latency] [--tune | --seed-base N]\n"
        "\n"
        "  --tune   run on a DIFFERENT family of seeds (700). Use it for anything\n"
        "           chosen by looking at the answer; the acceptance test is family\n"
        "           1000 and must stay untouched by that choice.\n");
      return 2;
    }
  }
  if (table_path == nullptr) { std::fprintf(stderr, "--table is required\n"); return 2; }

  policy::PolicyTable table, xtable;
  std::string why;
  if (!table.load(table_path, &why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 2; }
  const bool have_x = xmodel_path != nullptr && xtable.load(xmodel_path, &why);
  if (xmodel_path != nullptr && !have_x) std::fprintf(stderr, "out-of-model table: %s\n", why.c_str());

  const QuoteParams p = base_params();

  if (probe) {
    // What OUR orders actually experience, against what the MDP was told they
    // would. The model is calibrated on the market's own resting orders; if
    // those and ours are not the same population, every fill probability in the
    // table is for a different order than the one being quoted.
    banner("realised fill hazard for our own quotes, against the calibrated one");
    RunResult r = run_strategy(JoinTouch{p}, make_config(calib_seed + 77, latency, median_ns), events);
    std::printf("  JoinTouch placed %zu orders, %zu of them at the touch\n",
                r.placements.size(),
                static_cast<std::size_t>(std::count_if(r.placements.begin(), r.placements.end(),
                                                       [](const RunResult::Placement& q) { return q.at_touch; })));
    // Bucketed by the TABLE'S OWN discretisation, on the table's own scale, so
    // the two columns answer the same question. Bucketing the probe one way and
    // the model another is how the twenty-fold gap stayed invisible: both
    // numbers were called "the fill hazard by queue position" and neither was
    // measuring the position the other meant.
    const std::int64_t scale = table.header().queue_scale;
    std::printf("  queue scale %lld shares, from the table header\n\n", static_cast<long long>(scale));
    std::printf("  %-8s %10s %8s %8s %12s %14s %14s\n", "bucket", "mean ahead", "orders",
                "filled", "rest (ms)", "realised /s", "model P(fill)");
    std::printf("  %s\n", std::string(80, '-').c_str());
    for (int b = 0; b < policy::kQueueBuckets; ++b) {
      std::size_t n = 0, filled = 0;
      double rest = 0.0, ahead = 0.0;
      for (const RunResult::Placement& q : r.placements) {
        if (!q.at_touch || policy::queue_bucket(q.ahead, scale) != b) continue;
        ++n;
        if (q.filled > 0) ++filled;
        rest  += static_cast<double>(q.rest_ns) / 1e9;
        ahead += static_cast<double>(q.ahead);
      }
      if (n == 0) { std::printf("  %-8d %10s %8s\n", b, "-", "0"); continue; }
      std::printf("  %-8d %10.0f %8zu %8zu %12.2f %14.2f\n", b,
                  ahead / static_cast<double>(n), n, filled,
                  1e3 * rest / static_cast<double>(n),
                  rest > 0 ? static_cast<double>(filled) / rest : 0.0);
    }
    std::printf("\n  Compare 'realised' against fill_hazard in the mdp.json the table was\n"
                "  solved from, row for row. A large gap means the model is pricing a\n"
                "  different order than the one being quoted, and no amount of solving\n"
                "  fixes that: it was 20x at the front of the queue, because the model\n"
                "  bucketed by rank among the market's orders and a maker's orders are\n"
                "  not drawn from that population.\n");
    return 0;
  }

  if (sweep_informed) {
    // Glosten & Milgrom, measured rather than assumed. A maker earns the spread
    // from uninformed flow and pays impact to informed flow, so P&L should fall
    // as pi rises and cross zero somewhere below pi = 0.5 for a half-tick
    // spread against a one-tick impact. If this curve is flat, or negative
    // everywhere, the generator has no compensation structure in it and no
    // policy solved against it means anything.
    banner("does market making pay? session P&L against the informed share");
    std::printf("  %10s %14s %14s %9s %10s\n",
                "informed", "JoinTouch", "TabulatedMDP", "JT pasv", "JT per fill");
    std::printf("  %s\n", std::string(62, '-').c_str());
    const double saved = g_informed;
    for (const double pi : {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.7}) {
      g_informed = pi;
      double jt = 0.0, tb = 0.0, f = 0.0;
      const int n = std::max(3, seeds / 4);
      for (int s2 = 0; s2 < n; ++s2) {
        const SimConfig c = make_config(
            calib_seed + std::uint64_t{700} + static_cast<std::uint64_t>(s2) * std::uint64_t{104729},
            latency, median_ns);
        const RunResult a = run_strategy(JoinTouch{p}, c, events);
        jt += a.pnl(); f += static_cast<double>(a.attr.n_passive);
        TabulatedPolicy t2; t2.table = &table; t2.p = p;
        tb += run_strategy(t2, c, events).pnl();
      }
      std::printf("  %9.0f%% %14.1f %14.1f %9.0f %10.3f\n",
                  100.0 * pi, jt / n, tb / n, f / n, f > 0 ? (jt / n) / (f / n) : 0.0);
    }
    g_informed = saved;
    std::printf("\n  Falling with pi is the signature of a market that pays for liquidity.\n"
                "  Flat or negative everywhere means the generator has no compensation\n"
                "  structure and nothing solved against it can be interpreted.\n");
    return 0;
  }

  if (sweep_drift) {
    // Choosing the process by a stated rule rather than by which answer it
    // gives: the target is the volatility at which JOINTOUCH — the naive
    // large-tick heuristic, with no optimisation in it at all — is closest to
    // break-even. That is the weakest process on which "can an optimal policy
    // beat the heuristics" is a question with an answer. Below it market making
    // is free money and above it nothing can quote.
    banner("picking the process: session P&L against generator volatility");
    std::printf("  %10s %14s %14s %9s %9s\n",
                "drift/event", "JoinTouch", "TabulatedMDP", "JT pasv", "JT aggr");
    std::printf("  %s\n", std::string(62, '-').c_str());
    const double saved = g_drift;
    for (const double d : {0.02, 0.005, 0.001, 0.0005, 0.0002, 0.0001}) {
      g_drift = d;
      double jt = 0.0, tb = 0.0, f = 0.0, fa = 0.0;
      const int n = std::max(3, seeds / 4);
      for (int s2 = 0; s2 < n; ++s2) {
        const SimConfig c = make_config(
            calib_seed + std::uint64_t{700} + static_cast<std::uint64_t>(s2) * std::uint64_t{104729},
            latency, median_ns);
        const RunResult a = run_strategy(JoinTouch{p}, c, events);
        jt += a.pnl();
        f  += static_cast<double>(a.attr.n_passive);
        fa += static_cast<double>(a.attr.n_aggressive);
        TabulatedPolicy t2; t2.table = &table; t2.p = p;
        tb += run_strategy(t2, c, events).pnl();
      }
      std::printf("  %10.4f %14.1f %14.1f %9.0f %9.0f\n", d, jt / n, tb / n, f / n, fa / n);
    }
    g_drift = saved;
    std::printf("\n  The table the policy was solved from must be recalibrated at whichever\n"
                "  volatility is chosen — stats --synthetic --drift, then solve.\n");
    return 0;
  }

  if (sweep) {
    // What latency does to market making on this process. Printed before the
    // main test because it is what sets the default the test runs at, and
    // because it is the more interesting number: this project exists to
    // measure latency, and here is the price of it.
    banner("session P&L against round-trip latency");
    std::printf("  %10s %14s %14s %14s %10s\n",
                "median", "best baseline", "TabulatedMDP", "difference", "MM fills");
    std::printf("  %s\n", std::string(66, '-').c_str());
    for (const Nanos lat : {Nanos{0}, Nanos{1'000}, Nanos{5'000}, Nanos{10'000},
                            Nanos{50'000}, Nanos{200'000}, Nanos{1'000'000}}) {
      double bb = -1e300, tb = 0.0, fills = 0.0;
      const int n = std::max(4, seeds / 3);
      std::vector<double> base_pnl(6, 0.0);
      for (int s2 = 0; s2 < n; ++s2) {
        const std::uint64_t sd =
            calib_seed + std::uint64_t{500} + static_cast<std::uint64_t>(s2) * std::uint64_t{104729};
        const SimConfig c = make_config(sd, lat > 0, lat);
        base_pnl[0] += run_strategy(ConstantSpread{p, 1}, c, events).pnl();
        base_pnl[1] += run_strategy(InventorySkew{p, 1},  c, events).pnl();
        base_pnl[2] += run_strategy(AvellanedaStoikov{p}, c, events).pnl();
        base_pnl[3] += run_strategy(GLFT{p},              c, events).pnl();
        base_pnl[4] += run_strategy(ImbalanceSkew{p},     c, events).pnl();
        base_pnl[5] += run_strategy(JoinTouch{p},         c, events).pnl();
        TabulatedPolicy tp2; tp2.table = &table; tp2.p = p;
        const RunResult rr = run_strategy(tp2, c, events);
        tb += rr.pnl();
        fills += static_cast<double>(rr.attr.n_fills);
      }
      for (double& v : base_pnl) v /= n;
      tb /= n; fills /= n;
      for (double v : base_pnl) bb = std::max(bb, v);
      std::printf("  %8.0f us %14.1f %14.1f %14.1f %10.0f\n",
                  static_cast<double>(lat) / 1000.0, bb, tb, tb - bb, fills);
    }
    std::printf("\n  A round trip long relative to the process makes every quote stale on\n"
                "  arrival, and the best a market maker can then do is not quote. Where that\n"
                "  is true the comparison below is between two ways of abstaining.\n");
  }
  constexpr std::size_t kBaselines = 6;   // indices 0..5
  std::vector<Row> rows(have_x ? 8 : 7);
  const char* names[] = {"ConstantSpread", "InventorySkew", "AvellanedaStoikov",
                         "GLFT", "ImbalanceSkew", "JoinTouch",
                         "TabulatedMDP", "TabulatedMDP(out-of-model)"};
  for (std::size_t i = 0; i < rows.size(); ++i) rows[i].name = names[i];

  banner("Phase 5 acceptance test");
  std::printf("  table       %s  (hash %016llx, %llu sweeps, residual %.2g)\n", table_path,
              static_cast<unsigned long long>(table.header().param_hash),
              static_cast<unsigned long long>(table.header().sweeps),
              table.header().residual);
  std::printf("  calibrated  seed %llu - EXCLUDED from evaluation\n",
              static_cast<unsigned long long>(calib_seed));
  std::printf("  seeds       family %llu%s\n", static_cast<unsigned long long>(seed_base),
              seed_base == 1000 ? "  (the acceptance family: nothing may be tuned on it)"
                                : "  \033[33m(NOT the acceptance family - this is a tuning run)\033[0m");
  std::printf("  evaluating  %d seeds x %d events, latency %s (median %.1f us)\n", seeds, events,
              latency ? "on" : "off", static_cast<double>(median_ns) / 1000.0);
  {
    const FlowConfig fc = make_config(0, false, 0).flow;
    std::printf("  flow        %.0f%% of aggressive orders informed (impact %.0f%% x %lld tick),\n"
                "              exogenous drift %.2g/event\n",
                100.0 * fc.informed_frac, 100.0 * fc.informed_impact_prob,
                static_cast<long long>(fc.informed_impact), fc.drift_prob);
  }
  std::printf("  position    +-%lld shares for every strategy\n",
              static_cast<long long>(p.max_inventory));

  // The three layers each have an idea of how long a decision epoch is: the
  // grid apps/stats sampled on, the --dt-ms mdp_params measured over, and the
  // message budget in DriverConfig. Only the first two were ever compared. The
  // third is set in events, and how long an event takes is a property of the
  // generator — so the executor can quietly be asking a model calibrated for
  // one millisecond what to do about half of one.
  {
    const DriverConfig dc{};
    const RunResult warm = run_strategy(JoinTouch{p}, make_config(calib_seed + 91, latency, median_ns),
                                        std::min(events, 100'000));
    const double dt   = RunResult::budget_floor_s(dc.quote_every_ns);
    const double hold = warm.mean_hold_s();
    const double want = table.header().dt_s;
    const double evb  = warm.events_per_budget(dc.quote_every_ns);
    std::printf("  epoch       %.3f ms message budget (%.1f market events), %.3f ms mean\n"
                "              quote life, table solved for %.3f ms\n",
                1e3 * dt, evb, 1e3 * hold, 1e3 * want);
    // A budget that spans no events is a policy deciding against a book that
    // has not changed; one that spans thousands is a policy that cannot react.
    // The budget is a time now, so this is a statement about the PROCESS.
    if (evb > 0.0 && (evb < 1.0 || evb > 1000.0))
      std::printf("              \033[33m%.1f market events pass in one budget period. The "
                  "process is\n              being sampled at the wrong scale for this "
                  "cadence.\033[0m\n", evb);
    if (want > 0.0 && dt > 0.0 && (dt / want > 1.25 || want / dt > 1.25))
      std::printf("              \033[33mBudget and table differ by %.1fx. Every probability in the\n"
                  "              table is per epoch, so the policy is being asked about a different\n"
                  "              amount of elapsed time than it was solved for. Re-run\n"
                  "              stats --grid-ms and mdp_params --dt-ms at %.3f to match.\033[0m\n",
                  dt > want ? dt / want : want / dt, 1e3 * dt);
  }

  for (int s = 0; s < seeds; ++s) {
    // Stay in uint64_t throughout: mixing in ULL literals makes this an
    // unsigned long long expression, which is a different type from uint64_t
    // on this platform and trips -Wconversion on the way back.
    const std::uint64_t seed =
        calib_seed + seed_base + static_cast<std::uint64_t>(s) * std::uint64_t{7919};
    const SimConfig cfg = make_config(seed, latency, median_ns);

    record(rows[0], run_strategy(ConstantSpread{p, 1},    cfg, events));
    record(rows[1], run_strategy(InventorySkew{p, 1},     cfg, events));
    record(rows[2], run_strategy(AvellanedaStoikov{p},    cfg, events));
    record(rows[3], run_strategy(GLFT{p},                 cfg, events));
    record(rows[4], run_strategy(ImbalanceSkew{p},        cfg, events));
    record(rows[5], run_strategy(JoinTouch{p},            cfg, events));
    TabulatedPolicy tp; tp.table = &table; tp.p = p;
    record(rows[6], run_strategy(tp, cfg, events));
    if (have_x) {
      TabulatedPolicy xp; xp.table = &xtable; xp.p = p;
      record(rows[7], run_strategy(xp, cfg, events));
    }
    if ((s + 1) % 6 == 0) std::printf("  ... %d/%d seeds\n", s + 1, seeds);
  }

  banner("mean over seeds, in ticks x shares");
  std::printf("  session P&L is cash exchanged plus the closing position at the closing mid.\n"
              "  spread-cap and adv-select decompose TRADING edge at 100 ms and do not sum to\n"
              "  it: a position held to the end shows up in P&L and in 'end inv', not in them.\n"
              "  'aggr' is a fill where OUR order crossed on arrival — a quote decided on a\n"
              "  stale book, landing through the market. That is latency turning liquidity\n"
              "  provision into liquidity taking, and it is not market making.\n\n");
  std::printf("%-22s %12s %12s %12s %9s %7s %7s %8s\n",
              "strategy", "session P&L", "spread-cap", "adv-select", "peak inv",
              "pasv", "aggr", "requotes");
  std::printf("%s\n", std::string(92, '-').c_str());
  auto mean = [](const std::vector<double>& v) {
    double s = 0.0; for (double x : v) s += x;
    return v.empty() ? 0.0 : s / static_cast<double>(v.size());
  };
  for (const Row& r : rows)
    std::printf("%-22s %12.1f %12.1f %12.1f %9.1f %7.0f %7.0f %8.0f\n", r.name.c_str(),
                r.mean_net(), mean(r.spread), mean(r.adverse), r.peak / seeds,
                r.passive / seeds, r.aggressive / seeds, r.requotes / seeds);

  // ---- the test ----
  std::size_t best = 0;
  for (std::size_t i = 1; i < kBaselines; ++i)
    if (rows[i].mean_net() > rows[best].mean_net()) best = i;

  // A baseline that never trades is not a market-making strategy that wins, it
  // is a strategy that declines to play. On a process where quoting loses money
  // it scores exactly zero and nothing that quotes can beat it, so the
  // criterion as written is unanswerable there. The second comparison is
  // against the best baseline that actually participates, and it is the one
  // that says whether the optimiser is any good at the job.
  constexpr double kMinFills = 100.0;
  std::size_t best_active = kBaselines;   // sentinel: none participates
  for (std::size_t i = 0; i < kBaselines; ++i) {
    if (rows[i].passive / seeds < kMinFills) continue;
    if (best_active == kBaselines || rows[i].mean_net() > rows[best_active].mean_net())
      best_active = i;
  }

  banner("the acceptance criterion");
  std::printf("  best baseline overall:     %s at %.1f  (%.0f passive fills)\n",
              rows[best].name.c_str(), rows[best].mean_net(), rows[best].passive / seeds);
  if (best_active < kBaselines)
    std::printf("  best baseline that trades: %s at %.1f  (%.0f passive fills)\n",
                rows[best_active].name.c_str(), rows[best_active].mean_net(),
                rows[best_active].passive / seeds);
  if (rows[best].passive / seeds < kMinFills)
    std::printf("\n  \033[33mThe best baseline takes almost no fills. It quotes — %0.f times a run —\n"
                "  but always where nothing trades, so it scores zero, and on a process where\n"
                "  quoting loses money zero cannot be beaten by anything that quotes. The\n"
                "  criterion is degenerate here; read the second comparison.\033[0m\n",
                rows[best].requotes / seeds);

  auto test = [&](const Row& challenger, const Row& against) {
    std::vector<double> diff;
    diff.reserve(challenger.net.size());
    for (std::size_t i = 0; i < challenger.net.size() && i < against.net.size(); ++i)
      diff.push_back(challenger.net[i] - against.net[i]);
    // Seeds are independent draws of the whole session, so blocks of one. The
    // dependence a block bootstrap exists to preserve is WITHIN a run.
    const BootstrapCI ci = block_bootstrap(diff, 1, 8000);
    const int wins = static_cast<int>(std::count_if(diff.begin(), diff.end(),
                                                    [](double d) { return d > 0.0; }));
    std::printf("\n  %s minus %s, paired by seed:\n", challenger.name.c_str(), against.name.c_str());
    std::printf("    mean %+.1f   95%% CI [%+.1f, %+.1f]   over %zu seeds, %d of them positive\n",
                ci.mean, ci.lo, ci.hi, diff.size(), wins);
    if (!ci.usable()) {
      std::printf("    \033[33mtoo few seeds for an interval — %zu, need %zu\033[0m\n",
                  ci.n_blocks, BootstrapCI::kMinBlocks);
    } else if (ci.excludes_zero() && ci.mean > 0.0) {
      std::printf("    \033[32mPASSES: beats the best baseline, CI excludes zero\033[0m\n");
    } else if (ci.excludes_zero()) {
      std::printf("    \033[31mFAILS: significantly WORSE than the best baseline\033[0m\n");
    } else {
      std::printf("    \033[33mNOT PROVEN: the interval spans zero\033[0m\n");
    }
    return ci;
  };

  const BootstrapCI in_model = test(rows[6], rows[best]);
  if (best_active < kBaselines && best_active != best) {
    std::printf("\n  --- against the best baseline that actually trades ---\n");
    test(rows[6], rows[best_active]);
  }
  if (have_x) {
    test(rows[7], rows[best]);
    std::printf("\n  The second table was solved for a real venue's process and applied to\n"
                "  synthetic flow. The gap between the two rows is what model\n"
                "  mis-specification costs, and it is the reason the first one was\n"
                "  calibrated against the simulator at all.\n");
  }

  banner("read this before the table above");
  std::printf(
      "  The synthetic generator is zero-intelligence: it has no informed flow, so\n"
      "  adverse selection here is near zero and a market maker keeps almost all of\n"
      "  the spread it captures. Real data does not look like that — the same\n"
      "  measurement on Bitstamp put markouts at +0.3 to +0.6 bps against the\n"
      "  passive side at every horizon.\n\n"
      "  The generator can also drift, and value iteration will happily monetise a\n"
      "  drift: where one exists, part of any edge below is a directional bet the\n"
      "  policy found in the process rather than market making. The 'end inv' column\n"
      "  is where that shows up — a strategy carrying a position to the end of the\n"
      "  run is being paid for direction. tools/mdp_params.py prints the measured\n"
      "  up/down rates for whatever --drift this was run at; check them against this\n"
      "  table rather than trusting a number written here.\n\n"
      "  So this answers 'does an optimal policy beat these heuristics on THIS\n"
      "  process', which is the question the roadmap asks. It does not answer\n"
      "  whether it would make money.\n");

  return (in_model.usable() && in_model.excludes_zero() && in_model.mean > 0.0) ? 0 : 1;
}
