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
#include "lob/strat/pnl.hpp"
#include "lob/strat/quoting.hpp"

using namespace lob;

namespace {

void banner(const char* t) {
  std::printf("\n\033[1m%s\033[0m\n", t);
  for (std::size_t i = 0; i < std::strlen(t); ++i) std::putchar('-');
  std::putchar('\n');
}

struct Result {
  std::string  name;
  Attribution  attr;
  BootstrapCI  per_fill;
  SimStats     stats;
  std::size_t  requotes = 0;
};

SimConfig make_config(bool latency) {
  SimConfig c;
  c.use_latency       = latency;
  c.flow.seed         = 20260904;
  c.flow.mid          = 10'000;
  c.flow.levels       = 8;
  c.flow.target_live  = 4'000;
  c.latency.seed      = 31;
  c.latency.median_ns = 1'000'000;
  return c;
}

// Drives one strategy: quote, requote with hysteresis, track markouts, attribute.
template <typename Strat>
Result run(Strat strat, int n, bool latency) {
  Simulator      sim{make_config(latency)};
  MarkoutTracker mk;

  std::size_t   seen       = 0;
  std::size_t   requotes   = 0;
  OrderId       next_id    = 900'000'000ULL;
  OrderId       bid_id     = 0, ask_id = 0;
  Ticks         cur_bid    = 0, cur_ask = 0;
  std::uint64_t ev         = 0, last_quote = 0;
  double        last_mid   = 0.0;

  auto agent = [&](const AgentView& v, Simulator& s) {
    // Markouts are an economic measurement of what actually happened, so they
    // use the TRUE mid, not the agent's lagged view.
    double true_mid = 0.0;
    if (s.true_book().has_bid() && s.true_book().has_ask()) {
      true_mid = 0.5 * (static_cast<double>(s.true_book().best_bid()) +
                        static_cast<double>(s.true_book().best_ask()));
      last_mid = true_mid;
      mk.advance(v.now, true_mid);
    }

    for (; seen < s.fills().size(); ++seen) {
      const Fill& f = s.fills()[seen];
      if (!f.resting_mine && !f.aggressor_mine) continue;
      const Side our = f.resting_mine ? f.resting_side : opposite(f.resting_side);
      mk.on_fill(f.ts, f.price, f.qty, sign_of(our), f.resting_mine,
                 true_mid > 0.0 ? true_mid : static_cast<double>(f.price));
    }

    if (!v.book.has_bid() || !v.book.has_ask()) return;
    ++ev;
    if (ev - last_quote < 200) return;      // message budget: do not requote every event

    const Quote q = strat.quote(v);

    // Hysteresis: a requote costs queue position, so only move when the target
    // has actually moved. Without this the strategy churns its own priority away.
    const bool bid_moved = q.bid_on != (bid_id != 0) || (q.bid_on && q.bid != cur_bid);
    const bool ask_moved = q.ask_on != (ask_id != 0) || (q.ask_on && q.ask != cur_ask);
    if (!bid_moved && !ask_moved) return;
    last_quote = ev;
    ++requotes;

    if (bid_moved && bid_id) { s.send_cancel(bid_id); bid_id = 0; }
    if (ask_moved && ask_id) { s.send_cancel(ask_id); ask_id = 0; }
    if (bid_moved && q.bid_on) { bid_id = next_id++; cur_bid = q.bid; s.send_limit(bid_id, Side::Bid, q.bid, q.bid_qty); }
    if (ask_moved && q.ask_on) { ask_id = next_id++; cur_ask = q.ask; s.send_limit(ask_id, Side::Ask, q.ask, q.ask_qty); }
  };

  sim.run(agent, n);

  Result r;
  r.name  = Strat::name();
  r.stats = sim.stats();
  r.requotes = requotes;
  // Horizon index 3 = 100 ms. Long enough for information to show, short enough
  // that a maker plausibly still holds the position.
  r.attr = attribute(mk.fills(), 3, FeeSchedule{}, last_mid, sim.stats().inventory);

  std::vector<double> per_fill;
  per_fill.reserve(mk.fills().size());
  for (const auto& f : mk.fills())
    if (f.filled[3]) per_fill.push_back(f.markout(3) / static_cast<double>(f.qty));
  r.per_fill = block_bootstrap(per_fill);
  return r;
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
