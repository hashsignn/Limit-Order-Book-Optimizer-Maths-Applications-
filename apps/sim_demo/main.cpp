// Phase 3 demonstration: what latency costs.
//
// The same naive market-making agent, the same order flow, the same seed — run
// once with zero latency and once with a realistic delay. The difference is the
// thing every zero-latency backtest is hiding.
//
// The agent is deliberately naive (fixed spread around the mid, no skew, no
// signal) because the point here is the simulator, not the strategy. Phase 4 is
// where strategies get compared.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lob/measure/histogram.hpp"
#include "lob/measure/tsc.hpp"
#include "lob/sim/simulator.hpp"

using namespace lob;

namespace {

void banner(const char* t) {
  std::printf("\n\033[1m%s\033[0m\n", t);
  for (std::size_t i = 0; i < std::strlen(t); ++i) std::putchar('-');
  std::putchar('\n');
}

// Quotes a fixed spread either side of the mid it can see, requoting when its
// target moves. Nothing clever — a baseline that exists to be measured.
class NaiveMaker {
 public:
  explicit NaiveMaker(Ticks half_spread, Qty size, std::int64_t max_inv)
      : half_(half_spread), size_(size), max_inv_(max_inv) {}

  void operator()(const AgentView& v, Simulator& sim) {
    if (!v.book.has_bid() || !v.book.has_ask()) return;
    ++seen_;
    // Requote on a cadence rather than every event: message budgets are real,
    // and requoting resets queue position.
    if (seen_ - last_quote_ < 200) return;
    last_quote_ = seen_;

    const auto mid = static_cast<Ticks>(
        std::llround(0.5 * static_cast<double>(v.book.best_bid() + v.book.best_ask())));
    const Ticks want_bid = mid - half_;
    const Ticks want_ask = mid + half_;

    if (bid_id_ != 0) { sim.send_cancel(bid_id_); bid_id_ = 0; }
    if (ask_id_ != 0) { sim.send_cancel(ask_id_); ask_id_ = 0; }

    // Stop quoting the side that would take us further past the limit.
    if (v.inventory < max_inv_)  { bid_id_ = next_id_++; sim.send_limit(bid_id_, Side::Bid, want_bid, size_); }
    if (v.inventory > -max_inv_) { ask_id_ = next_id_++; sim.send_limit(ask_id_, Side::Ask, want_ask, size_); }
  }

 private:
  Ticks        half_;
  Qty          size_;
  std::int64_t max_inv_;
  OrderId      next_id_    = 900'000'000ULL;
  OrderId      bid_id_     = 0;
  OrderId      ask_id_     = 0;
  std::uint64_t seen_       = 0;
  std::uint64_t last_quote_ = 0;
};

struct Outcome {
  SimStats stats;
  double   mark_to_market = 0.0;
  double   total          = 0.0;
};

Outcome run_once(bool with_latency, int n) {
  SimConfig cfg;
  cfg.use_latency        = with_latency;
  cfg.flow               = FlowConfig::ethusd();
  cfg.flow.seed          = 20260904;   // identical flow in both runs
  cfg.flow.mid           = 10'000;
  cfg.latency.median_ns  = 1'000'000;  // 1 ms, the crypto-over-WebSocket row
  cfg.latency.seed       = 7;

  Simulator sim{cfg};
  NaiveMaker agent{1, 10, 200};
  sim.run(agent, n);

  Outcome o;
  o.stats = sim.stats();
  // Mark the leftover inventory at the final mid, so the two runs are compared
  // on the same footing rather than on whoever happened to end flatter.
  if (sim.true_book().has_bid() && sim.true_book().has_ask()) {
    const double mid = 0.5 * static_cast<double>(sim.true_book().best_bid() +
                                                 sim.true_book().best_ask());
    o.mark_to_market = static_cast<double>(o.stats.inventory) * mid;
  }
  o.total = o.stats.realised_pnl + o.mark_to_market;
  return o;
}

void report(const char* label, const Outcome& o) {
  std::printf("  %-14s  passive %5llu (%6lld sh)   aggressive %5llu (%6lld sh)   "
              "inv %5lld   late-cx %5llu   P&L %+11.1f\n",
              label,
              static_cast<unsigned long long>(o.stats.passive_fills),
              static_cast<long long>(o.stats.passive_qty),
              static_cast<unsigned long long>(o.stats.aggressive_fills),
              static_cast<long long>(o.stats.aggressive_qty),
              static_cast<long long>(o.stats.inventory),
              static_cast<unsigned long long>(o.stats.late_cancels),
              o.total);
}

}  // namespace

int main(int argc, char** argv) {
  const int n = argc > 1 ? std::atoi(argv[1]) : 500'000;
  tsc::init();

  banner("simulator: what latency costs");
  std::printf("  agent      quotes at the touch (1-tick half-spread), size 10, inventory limit 200\n");
  std::printf("  flow       identical seed in both runs\n");
  std::printf("  latency    lognormal, 1 ms median, outbound 1.3x inbound\n");
  std::printf("  events     %d\n\n", n);

  const Outcome zero = run_once(false, n);
  const Outcome real = run_once(true, n);

  report("zero latency", zero);
  report("1 ms latency", real);

  banner("what changed, and why");
  std::printf("  P&L difference     %+.1f ticks*shares\n", real.total - zero.total);
  std::printf("  passive fills      %lld -> %lld\n",
              static_cast<long long>(zero.stats.passive_fills),
              static_cast<long long>(real.stats.passive_fills));
  std::printf("  aggressive fills   %lld -> %lld   <- the mechanism\n",
              static_cast<long long>(zero.stats.aggressive_fills),
              static_cast<long long>(real.stats.aggressive_fills));
  std::printf("  late cancels       %llu -> %llu   (arrived after the order had traded)\n",
              static_cast<unsigned long long>(zero.stats.late_cancels),
              static_cast<unsigned long long>(real.stats.late_cancels));

  std::printf(
      "\nRead the aggressive-fill row, not the P&L. The strategy never asks to\n"
      "cross the spread: every order it sends is a passive quote. Under latency\n"
      "those quotes are decided on a book that has already moved, so some of\n"
      "them land crossing and take liquidity instead of providing it. Being slow\n"
      "silently converts a market maker into a liquidity taker, and it pays the\n"
      "spread every time it happens.\n"
      "\nThat is the effect, not a strategy result. The agent is a deliberately\n"
      "naive baseline and the flow is zero-intelligence, so the levels mean\n"
      "nothing. Phase 4 compares strategies; this only shows the gap is real.\n");
  return 0;
}
