// Phase 4: compare the baseline strategies.
//
// Each strategy runs through the same simulator, on identical order flow with
// identical seeds, so the only thing that differs is the quoting rule.
//
// READ THE CAVEAT AT THE BOTTOM OF THE OUTPUT BEFORE READING THE TABLE.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "lob/measure/tsc.hpp"
#include "lob/sim/simulator.hpp"
#include "lob/strat/driver.hpp"
#include "lob/strat/pnl.hpp"
#include "lob/strat/quoting.hpp"

using namespace lob;

namespace {

void banner(const char* t) {
  std::printf("\n\033[1m%s\033[0m\n", t);
  for (std::size_t i = 0; i < std::strlen(t); ++i) std::putchar('-');
  std::putchar('\n');
}

SimConfig make_config(bool latency) {
  SimConfig c;
  c.use_latency       = latency;
  // NOT FlowConfig::ethusd(), though that is the calibrated process and this
  // one is 200x too thick. Switching it here alone is worse than leaving it:
  // the driver requotes every 200 EVENTS, which is 0.5 ms on this clock and 3.7
  // SECONDS on the calibrated one, so the quote life, the markout horizon and
  // the table's epoch all come apart. docs/KNOWN-ISSUES.md 5.
  c.flow.seed         = 20260904;
  c.flow.mid          = 10'000;
  c.flow.levels       = 8;
  c.flow.target_live  = 4'000;
  c.latency.seed      = 31;
  c.latency.median_ns = 1'000'000;
  return c;
}

using Result = RunResult;

// The driver moved to lob/strat/driver.hpp so Phase 5's evaluation could use
// the identical one. A comparison whose strategies are driven differently
// measures the drivers.
template <typename Strat>
Result run(Strat strat, int n, bool latency) {
  return run_strategy(strat, make_config(latency), n);
}

void print_table(const std::vector<Result>& rs) {
  std::printf("%-20s %7s %7s %7s %11s %11s %11s %9s\n",
              "strategy", "fills", "pasv", "aggr", "spread-cap", "adv-select", "net", "inv");
  std::printf("%s\n", std::string(96, '-').c_str());
  for (const auto& r : rs) {
    std::printf("%-20s %7zu %7zu %7zu %11.1f %11.1f %11.1f %9lld\n",
                r.name.c_str(), r.attr.n_fills, r.attr.n_passive, r.attr.n_aggressive,
                r.attr.spread_capture, r.attr.adverse_sel, r.attr.total,
                static_cast<long long>(r.stats.inventory));
  }
  std::printf("\n  spread-cap  what we earned at the moment of each fill\n");
  std::printf("  adv-select  what the price then did to us, over 100 ms (positive = it cost us)\n");
  std::printf("  net         spread-cap - adv-select - fees, in ticks*shares\n");
}

void print_markouts(const std::vector<Result>& rs) {
  std::printf("%-20s", "strategy");
  for (const Nanos h : kMarkoutHorizons) {
    if (h < 1'000'000) std::printf("%10lldus", static_cast<long long>(h / 1000));
    else               std::printf("%10lldms", static_cast<long long>(h / 1'000'000));
  }
  std::printf("\n%s\n", std::string(80, '-').c_str());
  for (const auto& r : rs) {
    std::printf("%-20s", r.name.c_str());
    for (std::size_t h = 0; h < kNumHorizons; ++h) {
      if (r.attr.markout_n[h] == 0) std::printf("%12s", "-");
      else std::printf("%+12.3f", r.attr.markout_mean[h]);
    }
    std::printf("\n");
  }
  std::printf("\n  Mean markout per share at each horizon. The SHAPE is the story:\n"
              "  positive then decaying toward negative is the normal signature, and\n"
              "  where it crosses zero is the effective holding-time budget.\n");
}

void print_significance(const std::vector<Result>& rs) {
  std::printf("%-20s %12s %12s %12s %10s %s\n",
              "strategy", "mean/share", "ci-lo", "ci-hi", "blocks", "excludes 0?");
  std::printf("%s\n", std::string(84, '-').c_str());
  for (const auto& r : rs) {
    std::printf("%-20s %12.4f %12.4f %12.4f %10zu %s\n",
                r.name.c_str(), r.per_fill.mean, r.per_fill.lo, r.per_fill.hi,
                r.per_fill.n_blocks,
                !r.per_fill.usable() ? "UNUSABLE" : (r.per_fill.excludes_zero() ? "yes" : "no"));
  }
  std::printf("\n  Block bootstrap over 100 ms markouts per share, 50-fill blocks, 2000 draws.\n"
              "  Fills arrive in bursts driven by the same price move, so a per-fill\n"
              "  t-statistic would assume an independence that does not exist.\n"
              "\n  UNUSABLE means fewer than %zu blocks. A bootstrap over two or three\n"
              "  blocks resamples almost the same data every draw: the interval it\n"
              "  produces is not a confidence interval and must not be read as one.\n",
              BootstrapCI::kMinBlocks);
}

}  // namespace

int main(int argc, char** argv) {
  const int  n       = argc > 1 ? std::atoi(argv[1]) : 500'000;
  const bool latency = !(argc > 2 && std::strcmp(argv[2], "--no-latency") == 0);
  tsc::init();

  QuoteParams p;
  p.size = 10; p.max_inventory = 200;
  p.gamma = 0.05; p.sigma = 0.5; p.horizon = 1.0;
  p.A = 1.0; p.k = 1.5; p.imbalance_gain = 1.0;

  banner("phase 4: baseline strategies");
  std::printf("  events   %d\n  latency  %s\n  params   gamma %.3f  sigma %.2f  A %.1f  k %.2f  size %lld  inv-limit %lld\n",
              n, latency ? "1 ms median" : "OFF (unreachable, for contrast only)",
              p.gamma, p.sigma, p.A, p.k,
              static_cast<long long>(p.size), static_cast<long long>(p.max_inventory));

  std::vector<Result> rs;
  rs.push_back(run(ConstantSpread{p, 1}, n, latency));
  rs.push_back(run(InventorySkew{p, 1},  n, latency));
  rs.push_back(run(AvellanedaStoikov{p}, n, latency));
  rs.push_back(run(GLFT{p},              n, latency));
  rs.push_back(run(ImbalanceSkew{p},     n, latency));

  banner("quoted half-spreads, before the tick grid");
  std::printf("  avellaneda-stoikov  %.3f ticks (optimal total spread %.3f)\n",
              0.5 * AvellanedaStoikov::optimal_spread(p),
              AvellanedaStoikov::optimal_spread(p));
  std::printf("  glft (flat inv)     %.3f ticks\n", GLFT::half_bid(0.0, p));
  std::printf("  tick floor          %lld tick\n", static_cast<long long>(p.min_half));
  if (0.5 * AvellanedaStoikov::optimal_spread(p) < static_cast<double>(p.min_half)) {
    std::printf("\n  \033[33mThe A-S optimum is BELOW one tick, so the grid clamps it to the same\n"
                "  quote the naive rule produces. That is not a bug and it is not\n"
                "  specific to these parameters: on a large-tick instrument the optimal\n"
                "  spread is routinely sub-tick, the price optimiser has nothing left to\n"
                "  say, and queue position becomes the entire game (Dayri & Rosenbaum,\n"
                "  arXiv:1207.6325). Expect identical rows below.\033[0m\n");
  }

  banner("P&L attribution");
  print_table(rs);
  banner("markout curve");
  print_markouts(rs);
  banner("is the difference real?");
  print_significance(rs);

  banner("what this does and does not show");
  std::printf(
      "  DOES     the five strategies are implemented, produce different quotes,\n"
      "           and are compared on identical flow with a shared attribution.\n"
      "           The harness is reusable and the statistics are honest.\n\n"
      "  DOES NOT say which would make money. Three reasons, each sufficient:\n\n"
      "   1. A and k are UNCALIBRATED. Avellaneda-Stoikov and GLFT are the\n"
      "      optimal response to a fill intensity lambda(delta)=A*exp(-k*delta).\n"
      "      Both parameters are hand-set here. Optimal quotes against the wrong\n"
      "      intensity are not optimal quotes.\n\n"
      "   2. The flow is ZERO-INTELLIGENCE. It does not produce an exponential\n"
      "      fill curve, so part of what this table measures is whether the\n"
      "      generator happens to match the models' assumptions. It does not.\n\n"
      "   3. No adverse selection in the flow. The synthetic aggressors are\n"
      "      uninformed, so the single largest cost a real market maker faces is\n"
      "      absent by construction. arXiv:2502.18625 finds fill probability and\n"
      "      post-fill returns are negatively correlated in live data; nothing\n"
      "      here can reproduce that.\n\n"
      "  Calibrating A and k, and replacing the flow with a queue-reactive or\n"
      "  Hawkes model fitted to real order flow, is Phase 2b and the rest of\n"
      "  Phase 3. Both need MBO data.\n");
  return 0;
}
