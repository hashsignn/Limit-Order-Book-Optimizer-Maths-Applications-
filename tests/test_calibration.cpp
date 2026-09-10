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

  // ---- Model I reproduces its own invariant distribution ------------------
  // A birth-and-death queue's stationary law is closed form, so it is a TEST of
  // the implementation rather than an assumption inside it:
  //
  //   rho(n) = lambda^L(n) / ( lambda^C(n+1) + lambda^M(n+1) )
  //   pi(n)  = pi(0) * prod_{j=1..n} rho(j-1)
  //
  // Checked one level behind the touch, not at it. Level 0 here is the BEST
  // queue, so it is empty only when a whole side is, and the closed form has no
  // way to express that -- its q=0 mass is unreachable by construction and
  // comparing against it would fail for a reason that is not a defect.
  {
    // WITH THE REFERENCE PRICE PINNED. The paper's Model I holds during periods
    // when p_ref is CONSTANT; Model III is that plus price moves. Run against a
    // moving reference the closed form is simply not the right law -- queues
    // are relabelled underneath it every time the price steps, and the measured
    // total variation was 0.30. So the three things that move p_ref are turned
    // off here, which is the regime the closed form describes.
    FlowConfig cfg = FlowConfig::ethusd_queue_reactive();
    cfg.qr.theta             = 0.0;   // no endogenous move off an empty queue
    cfg.drift_prob           = 0.0;   // no exogenous news
    cfg.informed_impact_prob = 0.0;   // no impact behind an informed trade
    const FlowConfig::Qr& k = cfg.qr;
    constexpr int kQ = 16, kLvl = 1;

    // The model's own law, from the same constants the generator runs on.
    double pi[kQ] = {}, prod = 1.0, sum = 1.0;
    pi[0] = 1.0;
    for (int n = 1; n < kQ; ++n) {
      const double x = n;
      const double dep = k.cancel_rate[kLvl] * x / (x + k.cancel_half[kLvl]);
      if (!(dep > 0.0)) break;
      const double arr = (n - 1) == 0
          ? k.add_empty[kLvl]
          : k.add_rate[kLvl] * std::exp(-k.add_decay[kLvl] * (n - 2));
      prod *= arr / dep;
      pi[n] = prod;
      sum  += prod;
    }
    for (double& v : pi) v /= sum;

    // And what the generator actually does, sampled on a clock rather than per
    // event: an event-sampled histogram is biased toward whatever states have
    // the most events, which is the opposite of a stationary distribution.
    FlowGenerator gen{cfg};
    OrderBook book{5'000, 10'240, 1 << 20};
    MatchingEngine match{book};
    std::size_t fills_seen = 0;
    double hist[kQ] = {}, samples = 0;
    Nanos next_sample = 0;
    constexpr Nanos kEvery = 30'000'000'000LL;

    for (int i = 0; i < 1'500'000; ++i) {
      const BookEvent e = gen.next();
      if (next_sample == 0) next_sample = e.ts + kEvery;
      if (e.type == EventType::Aggress) {
        (void)match.submit_market(e.ts, e.order_id, e.side, e.qty, false);
        for (std::size_t j = fills_seen; j < match.fills().size(); ++j)
          if (book.qty_of(match.fills()[j].resting_id) == 0)
            gen.forget_order(match.fills()[j].resting_id);
        fills_seen = match.fills().size();
      } else {
        (void)book.apply(e);
      }
      gen.on_applied(e, book.qty_of(e.order_id));
      gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());

      if (e.ts < next_sample || !book.has_bid() || !book.has_ask()) continue;
      next_sample = e.ts + kEvery;
      ++samples;
      for (int s = 0; s < 2; ++s) {
        // Where the MODEL puts the queue, which is relative to the reference
        // price and not to the touch. Sampling `best_bid - 1` instead measures
        // a level the model never addresses, and reported a total variation of
        // 0.195 against 0.046 for the queue that actually exists.
        const Side side = (s == 0) ? Side::Bid : Side::Ask;
        const Ticks px  = (s == 0) ? gen.mid() - kLvl : gen.mid() + 1 + kLvl;
        const Qty  d    = book.qty_at(side, px);
        int q = d <= 0 ? 0 : static_cast<int>(std::ceil(static_cast<double>(d) / cfg.qr.aes));
        if (q >= kQ) q = kQ - 1;
        hist[q] += 0.5;                       // both sides pooled, as the paper pools them
      }
    }
    ::lobtest::report(samples > 100, "the invariant check got enough samples",
                      __FILE__, __LINE__, std::to_string(samples) + " samples");

    double tv = 0.0;
    for (int q = 0; q < kQ; ++q) tv += std::fabs(hist[q] / samples - pi[q]);
    tv *= 0.5;
    // A tenth in total variation. The two are not required to agree exactly:
    // the paper's Model I holds while the reference price is CONSTANT, and this
    // generator moves it, which relabels queues. The bound is there to catch an
    // intensity wired to the wrong rate, which moves this to a half.
    ::lobtest::report(tv < 0.10, "Model I matches its own invariant distribution",
                      __FILE__, __LINE__, "total variation " + std::to_string(tv));
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

  // ---- session P&L splits EXACTLY into a flat-price part and a walk part ----
  //
  // apps/evaluate used to split it as Attribution::total and the residual
  // against it, and call that residual the closing position's mark. It is not
  // one. Attribution is a per-fill markout at a 100 ms horizon; its residual
  // against session P&L is everything that window missed. Measured on this
  // process at 480,000 events, seed 1000: the true walk term was 115.0 ticks x
  // shares and the residual was -31,125.5, a factor of 270, and across eight
  // seeds the residual's spread grew as n^0.85 where a bounded position on a
  // random walk has to grow as the square root.
  //
  // The two identities below are what make the real split a split. They are
  // exact, not approximate: no tolerance is needed and none is given beyond
  // floating-point.
  {
    QuoteParams qp; qp.size = 10; qp.max_inventory = 50; qp.horizon = 1.0;
    SimConfig c;
    c.use_latency = false;
    c.flow        = FlowConfig::ethusd_queue_reactive();
    c.flow.seed   = 1000;
    c.flow.mid    = 10'000;
    const RunResult r = run_strategy(JoinTouch{qp}, c, 120'000);

    CHECK_NEAR(r.start_mid, static_cast<double>(c.flow.mid), 1e-12);
    // Only the walk term touches the closing mid, and it is exactly a position
    // times a price difference.
    CHECK_NEAR(r.walk_exposure(),
               static_cast<double>(r.stats.inventory) * (r.final_mid - r.start_mid), 1e-9);
    // The two parts sum to session P&L identically.
    CHECK_NEAR(r.flat_pnl() + r.walk_exposure(), r.pnl(), 1e-6);
    // A position times a price move cannot exceed the position reached times
    // the whole move. The residual this replaced routinely does.
    ::lobtest::report(std::fabs(r.walk_exposure()) <=
                          static_cast<double>(r.peak_inventory)
                              * std::fabs(r.final_mid - r.start_mid) + 1e-6,
                      "the walk term is bounded by position x price move",
                      __FILE__, __LINE__, std::to_string(r.walk_exposure()));
  }

  // ---- the modelled book is as deep as the real one, and the price falls ----
  // ---- into the gap rather than stepping one tick --------------------------
  //
  // Two properties, one cause. The queue-reactive levels sit at consecutive
  // ticks, so with four of them the modelled book was exactly three ticks deep
  // behind the touch and the distance from the touch to the next price holding
  // anything came out at 1.72 ticks. Measured, ethusd's is 5.62 and xrpusd's
  // 4.21, and their occupancy is still 13 to 18% eleven ticks out where this
  // model had nothing at all past three.
  //
  // That distance is exactly how far the price falls when a best queue clears,
  // and reference_price_step used to move one tick regardless. Volatility is
  // rate times step SQUARED, so the two together cost a factor of thirty in
  // variance. See docs/KNOWN-ISSUES.md issue 9.
  //
  // On the old code this block reports a mean gap of 1.7 ticks and occupancy of
  // zero at both depths.
  {
    FlowConfig fc = FlowConfig::ethusd_queue_reactive();
    fc.seed = 99;
    fc.mid  = 10'000;
    FlowGenerator gen{fc};
    OrderBook book{5'000, 10'240, 1 << 18};
    MatchingEngine match{book};

    std::size_t fills = 0;
    double gap_sum = 0.0;
    std::uint64_t samples = 0, occ8 = 0, occ12 = 0;
    Ticks px[40];
    Qty   qty[40];
    for (int i = 0; i < 300'000; ++i) {
      const BookEvent e = gen.next();
      if (e.type == EventType::Aggress) {
        (void)match.submit_market(e.ts, e.order_id, e.side, e.qty, /*mine=*/false);
        for (std::size_t k = fills; k < match.fills().size(); ++k)
          if (book.qty_of(match.fills()[k].resting_id) == 0)
            gen.forget_order(match.fills()[k].resting_id);
        fills = match.fills().size();
      } else {
        (void)book.apply(e);
      }
      gen.on_applied(e, book.qty_of(e.order_id));
      gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());
      // Every 97th event, which is coprime with nothing in the generator and so
      // does not sample the process in step with any of its cycles.
      if (i % 97 != 0 || !book.has_bid() || !book.has_ask()) continue;
      for (int s = 0; s < 2; ++s) {
        const Side side = (s == 0) ? Side::Bid : Side::Ask;
        const std::uint32_t n = book.depth(side, 40, px, qty);
        if (n < 2) continue;
        ++samples;
        gap_sum += static_cast<double>((s == 0) ? px[0] - px[1] : px[1] - px[0]);
        for (std::uint32_t k = 1; k < n; ++k) {
          const Ticks d = (s == 0) ? px[0] - px[k] : px[k] - px[0];
          if (d == 8)  ++occ8;
          if (d == 12) ++occ12;
        }
      }
    }
    CHECK(samples > 1000);
    const double gap = gap_sum / static_cast<double>(samples);
    const double o8  = 100.0 * static_cast<double>(occ8)  / static_cast<double>(samples);
    const double o12 = 100.0 * static_cast<double>(occ12) / static_cast<double>(samples);
    ::lobtest::report(gap > 3.0, "the touch is not one tick from the next occupied price",
                      __FILE__, __LINE__, std::to_string(gap) + " ticks");
    ::lobtest::report(o8 > 5.0, "the modelled book reaches eight ticks behind the touch",
                      __FILE__, __LINE__, std::to_string(o8) + "%");
    ::lobtest::report(o12 > 5.0, "the modelled book reaches twelve ticks behind the touch",
                      __FILE__, __LINE__, std::to_string(o12) + "%");
  }

  return lobtest::summary("calibration");
}
