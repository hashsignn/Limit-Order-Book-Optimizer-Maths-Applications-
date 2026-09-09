// Does FlowConfig::ethusd() still produce the market it says it does?
//
// This test exists because of the way docs/KNOWN-ISSUES.md 4 went wrong. The
// generator's touch was measured only per SECOND, on a clock nobody had
// calibrated, and the resulting number was 6x off in the wrong DIRECTION while
// the per-event rate was a thousand times off in the right one. Two errors of
// opposite sign multiplied into one plausible-looking figure, and it was
// written down and believed.
//
// So every bound below is a rate per EVENT, per second, or a count, and all
// three are checked. A change to the clock moves one, a change to the book
// moves another, and a change that cancels the two moves the third.
//
// The bounds are wide -- roughly a factor of two either side of the fit -- on
// purpose. This is not a regression test on the exact numbers, which are one
// venue on one day and would fail for honest reasons. It is a guard against
// the book quietly draining or filling by orders of magnitude, which has
// happened here twice: once when target_live was a ceiling rather than a
// target and the book fell from 20,001 resting orders to twenty with every
// test still passing, and once as issue 4.
#include "lob/book/order_book.hpp"
#include "lob/sim/flow.hpp"
#include "lob/sim/matching.hpp"
#include "lob/strat/driver.hpp"
#include "lob/strat/quoting.hpp"
#include "test_util.hpp"

#include <cmath>
#include <string>

using namespace lob;

namespace {

struct Measured {
  double live = 0, at_touch = 0, spread = 0;
  double moves_per_event = 0, moves_per_second = 0, events_per_second = 0;
  double one_sided_pct = 0;
};

// Driven exactly as Simulator drives it: aggressive flow through the matching
// engine, so fills consume the front of a queue instead of being fabricated on
// a random resting order. Measuring the generator any other way measures a
// process nothing runs.
Measured measure(FlowConfig cfg, int n) {
  FlowGenerator gen{cfg};
  OrderBook book{5'000, 10'240, 1 << 21};
  MatchingEngine match{book};

  std::size_t fills_seen = 0;
  std::uint64_t moves = 0, pairs = 0, one_sided = 0;
  double samples = 0, live = 0, touch = 0, spread = 0;
  bool had_touch = false;
  Ticks prev_bid = 0, prev_ask = 0;
  Nanos first_ts = 0, last_ts = 0;

  for (int i = 0; i < n; ++i) {
    const BookEvent e = gen.next();
    if (first_ts == 0) first_ts = e.ts;
    last_ts = e.ts;

    if (e.type == EventType::Aggress) {
      (void)match.submit_market(e.ts, e.order_id, e.side, e.qty, false);
      for (std::size_t k = fills_seen; k < match.fills().size(); ++k)
        if (book.qty_of(match.fills()[k].resting_id) == 0)
          gen.forget_order(match.fills()[k].resting_id);
      fills_seen = match.fills().size();
    } else {
      (void)book.apply(e);
    }
    gen.on_applied(e, book.qty_of(e.order_id));
    gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());

    if (!book.has_bid() || !book.has_ask()) { had_touch = false; ++one_sided; continue; }
    const Ticks bid = book.best_bid(), ask = book.best_ask();
    // Only a transition between two states that BOTH had a touch is a touch
    // move. Counting one against an empty side counts the book coming apart.
    if (had_touch) {
      ++pairs;
      if (bid != prev_bid) ++moves;
      if (ask != prev_ask) ++moves;
    }
    prev_bid = bid; prev_ask = ask; had_touch = true;

    if (i > n / 5) {          // let the add controller reach its target first
      ++samples;
      live   += static_cast<double>(book.live_orders());
      spread += static_cast<double>(ask - bid);
      touch  += static_cast<double>(book.orders_at(Side::Bid, bid)
                                  + book.orders_at(Side::Ask, ask));
    }
  }

  Measured m;
  const double secs = static_cast<double>(last_ts - first_ts) / 1e9;
  if (samples > 0) { m.live = live / samples; m.at_touch = touch / samples; m.spread = spread / samples; }
  if (pairs > 0)   m.moves_per_event = static_cast<double>(moves) / static_cast<double>(pairs);
  if (secs > 0)  { m.moves_per_second = static_cast<double>(moves) / secs;
                   m.events_per_second = n / secs; }
  m.one_sided_pct = 100.0 * static_cast<double>(one_sided) / n;
  return m;
}

void in_range(double v, double lo, double hi, const char* what, int line) {
  ::lobtest::report(v >= lo && v <= hi, what, __FILE__, line,
                    std::to_string(v) + " outside [" + std::to_string(lo)
                    + ", " + std::to_string(hi) + "]");
}

}  // namespace

#define IN_RANGE(v, lo, hi, what) in_range((v), (lo), (hi), what, __LINE__)

int main() {
  // ---- the calibrated process still looks like the venue ------------------
  // Targets, pooled over both sides, from the three ten-minute captures in
  // data/samples:
  //
  //                       ethusd   btcusd   xrpusd
  //   events/second         53.9     96.0     57.4
  //   orders at the touch    3.8      4.5      3.1
  //   touch moves/event   3.3e-02  6.5e-02  6.9e-02
  //   touch moves/second    1.80     6.23     3.95
  {
    const Measured m = measure(FlowConfig::ethusd(), 400'000);

    // The clock. Fitted to ethusd at 53.9 events a second; the band spans all
    // three instruments and then some.
    IN_RANGE(m.events_per_second, 30.0, 120.0, "calibrated event rate, per second");

    // The book. A touch of three to five orders is what every capture shows,
    // and it is the whole reason the touch can be consumed at all. The upper
    // bound is what matters: at 24 orders the move rate is already ten times
    // too low, and at 857 -- the stress process -- it is a thousand.
    IN_RANGE(m.at_touch, 2.0, 9.0,  "calibrated orders at the touch");
    IN_RANGE(m.live,     6.0, 30.0, "calibrated resting orders");
    IN_RANGE(m.spread,   1.5, 3.5,  "calibrated spread, ticks");

    // The dynamics, per event and per second. Both, because either alone can
    // be made to look right by a compensating error in the other.
    IN_RANGE(m.moves_per_event,  1.5e-2, 1.2e-1, "calibrated touch moves per event");
    IN_RANGE(m.moves_per_second, 0.5,    8.0,    "calibrated touch moves per second");

    // A market maker cannot quote into a book with nothing on the other side.
    ::lobtest::report(m.one_sided_pct < 1.0, "calibrated book keeps both sides",
                      __FILE__, __LINE__, std::to_string(m.one_sided_pct) + "% one-sided");
  }

  // ---- the stress process is still the stress process ---------------------
  // It is NOT a market and must not be quietly calibrated into one: the
  // differential and property tests want a dense book and a fast clock, and
  // they lose coverage if it thins out. Asserting the gap keeps the two jobs
  // the file's header describes actually distinct.
  {
    FlowConfig stress;                 // the defaults, as tests use them
    stress.levels = 8; stress.target_live = 4'000;
    const Measured m = measure(stress, 400'000);
    ::lobtest::report(m.at_touch > 100.0, "stress book stays dense", __FILE__, __LINE__,
                      std::to_string(m.at_touch) + " orders at the touch");
    ::lobtest::report(m.events_per_second > 100'000.0, "stress clock stays fast",
                      __FILE__, __LINE__, std::to_string(m.events_per_second) + " events/s");
  }

  // ---- mean_gap_ns means what it says --------------------------------------
  // The parameter that did not exist until issue 4, and the one whose absence
  // let a per-second rate be quoted against a clock nobody had chosen.
  {
    FlowConfig c = FlowConfig::ethusd();
    c.mean_gap_ns = 1'000'000;                       // 1 ms -> 1,000 events/s
    const Measured m = measure(c, 200'000);
    CHECK_NEAR(m.events_per_second, 1'000.0, 60.0);

    // And the per-event dynamics must not care what the clock is set to. This
    // is the invariant the original measurement lacked.
    const Measured base = measure(FlowConfig::ethusd(), 200'000);
    CHECK_NEAR(m.moves_per_event, base.moves_per_event, 1e-9);
  }

  // ---- the message budget is a TIME, and survives a change of clock --------
  // The whole of docs/KNOWN-ISSUES.md 5. The budget was 200 EVENTS, which is
  // 0.5 ms at one generator setting and 3.7 seconds at another, so the same
  // constant described two completely different traders and every cadence
  // downstream -- the MDP epoch, the markout horizon -- was chosen against
  // whichever reading happened to be current.
  //
  // Run the same strategy on two clocks a factor of ten apart. The requote
  // cadence in SECONDS must not move; the events inside one budget period must.
  {
    QuoteParams qp; qp.max_inventory = 50;
    DriverConfig dc;                                   // 100 ms budget

    auto run_at = [&](Nanos gap) {
      SimConfig c;
      c.use_latency = false;
      c.flow        = FlowConfig::ethusd();
      c.flow.mean_gap_ns = gap;
      c.flow.seed   = 4242;
      c.flow.mid    = 10'000;
      return run_strategy(JoinTouch{qp}, c, 120'000, dc);
    };
    const RunResult slow = run_at(FlowConfig::ethusd().mean_gap_ns);
    const RunResult fast = run_at(FlowConfig::ethusd().mean_gap_ns / 10);

    // A quote's life is a multiple of the budget -- hysteresis holds it longer
    // when the target has not moved -- so it is bounded below by the budget and
    // must not scale with the clock.
    const double want = RunResult::budget_floor_s(dc.quote_every_ns);
    ::lobtest::report(slow.mean_hold_s() >= want * 0.9, "quote life respects the budget",
                      __FILE__, __LINE__, std::to_string(slow.mean_hold_s()) + " s");
    ::lobtest::report(fast.mean_hold_s() >= want * 0.9, "quote life respects the budget, fast clock",
                      __FILE__, __LINE__, std::to_string(fast.mean_hold_s()) + " s");

    // Ten times the events in the same wall-clock budget period. This is the
    // number that is ALLOWED to move, and the one that used to be fixed while
    // the cadence moved instead.
    const double ev_slow = slow.events_per_budget(dc.quote_every_ns);
    const double ev_fast = fast.events_per_budget(dc.quote_every_ns);
    ::lobtest::report(ev_fast > ev_slow * 5.0, "a faster clock puts more events in a budget",
                      __FILE__, __LINE__,
                      std::to_string(ev_slow) + " -> " + std::to_string(ev_fast));

    // And the budget itself is exactly what it was set to, on any clock.
    CHECK_NEAR(RunResult::budget_floor_s(dc.quote_every_ns), 0.1, 1e-12);
  }

  return lobtest::summary("calibration");
}
