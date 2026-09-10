// Feature engine tests.
//
// Two kinds. First, hand-constructed book states where the right answer can be
// worked out on paper — those pin down the definitions. Second, a differential
// run: drive the incremental engine over a long random stream and check it
// against a from-scratch recomputation at every step. The incremental update is
// where the bugs live, because it is the only part that carries state.
#include "lob/book/order_book.hpp"
#include "lob/feat/features.hpp"
#include "lob/sim/flow.hpp"
#include "test_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace lob;

namespace {

OrderBook make_book() { return OrderBook{9'000, 2048, 8192}; }

void put(OrderBook& b, OrderId id, Side s, Ticks px, Qty q) {
  const BookError e = b.add(id, s, px, q);
  ::lobtest::report(e == BookError::Ok, "add", __FILE__, __LINE__, std::string(error_name(e)));
}

}  // namespace

int main() {
  // ---- imbalance and weighted mid, worked out by hand ----
  {
    OrderBook b = make_book();
    FeatureEngine fe;

    put(b, 1, Side::Bid, 9'990, 300);
    put(b, 2, Side::Ask, 9'992, 100);
    fe.update(b, 1000);
    const Features& f = fe.get();

    CHECK_EQ(f.bid, 9'990);
    CHECK_EQ(f.ask, 9'992);
    CHECK_EQ(f.spread, 2);
    CHECK_NEAR(f.mid, 9'991.0, 1e-9);
    // (300 - 100) / 400 = 0.5
    CHECK_NEAR(f.imbalance, 0.5, 1e-12);
    // Bid-heavy, so the weighted mid sits above the mid: 9991 + 1*0.5
    CHECK_NEAR(f.weighted_mid, 9'991.5, 1e-12);
    // and strictly inside the spread, always.
    CHECK(f.weighted_mid > static_cast<double>(f.bid));
    CHECK(f.weighted_mid < static_cast<double>(f.ask));
  }

  // ---- imbalance is antisymmetric and bounded ----
  {
    OrderBook b = make_book();
    FeatureEngine fe;
    put(b, 1, Side::Bid, 9'990, 100);
    put(b, 2, Side::Ask, 9'992, 300);
    fe.update(b, 1000);
    CHECK_NEAR(fe.get().imbalance, -0.5, 1e-12);
    CHECK_NEAR(fe.get().weighted_mid, 9'990.5, 1e-12);

    // Balanced book: imbalance 0, weighted mid == mid.
    OrderBook c = make_book();
    FeatureEngine fe2;
    put(c, 3, Side::Bid, 9'990, 250);
    put(c, 4, Side::Ask, 9'992, 250);
    fe2.update(c, 1000);
    CHECK_NEAR(fe2.get().imbalance, 0.0, 1e-12);
    CHECK_NEAR(fe2.get().weighted_mid, fe2.get().mid, 1e-12);
  }

  // ---- OFI: the four cases from Cont, Kukanov & Stoikov ----
  {
    // A size increase at an unchanged best bid is positive flow.
    BookTop a{}, c{};
    a.n_bid = a.n_ask = 1; c.n_bid = c.n_ask = 1;
    a.bid_px[0] = 100; a.bid_qty[0] = 10; a.ask_px[0] = 102; a.ask_qty[0] = 10;
    c = a; c.bid_qty[0] = 15;
    // price held, so ADD the new size and REMOVE the old: 15 - 10 = +5
    CHECK_NEAR(FeatureEngine::ofi_at(0, a, c), 5.0, 1e-12);

    // A size decrease at an unchanged best bid is negative flow.
    c = a; c.bid_qty[0] = 4;
    CHECK_NEAR(FeatureEngine::ofi_at(0, a, c), -6.0, 1e-12);

    // A bid improving in price: the whole new queue is added, none removed.
    c = a; c.bid_px[0] = 101; c.bid_qty[0] = 7;
    CHECK_NEAR(FeatureEngine::ofi_at(0, a, c), 7.0, 1e-12);

    // A bid worsening: the old queue is removed, none added.
    c = a; c.bid_px[0] = 99; c.bid_qty[0] = 7;
    CHECK_NEAR(FeatureEngine::ofi_at(0, a, c), -10.0, 1e-12);

    // The ask side is the mirror image, with the sign flipped.
    c = a; c.ask_qty[0] = 15;                       // more ask = downward pressure
    CHECK_NEAR(FeatureEngine::ofi_at(0, a, c), -5.0, 1e-12);
    c = a; c.ask_px[0] = 101; c.ask_qty[0] = 7;     // ask improves down
    CHECK_NEAR(FeatureEngine::ofi_at(0, a, c), -7.0, 1e-12);
    c = a; c.ask_px[0] = 103; c.ask_qty[0] = 7;     // ask retreats up
    CHECK_NEAR(FeatureEngine::ofi_at(0, a, c), 10.0, 1e-12);

    // No change at all is no flow.
    CHECK_NEAR(FeatureEngine::ofi_at(0, a, a), 0.0, 1e-12);
  }

  // ---- OFI through the real book, not a synthetic snapshot ----
  {
    OrderBook b = make_book();
    FeatureEngine fe;
    put(b, 1, Side::Bid, 9'990, 100);
    put(b, 2, Side::Ask, 9'992, 100);
    fe.update(b, 1000);

    // Adding to the bid is positive order flow.
    put(b, 3, Side::Bid, 9'990, 50);
    fe.update(b, 2000);
    CHECK_NEAR(fe.get().ofi_touch, 50.0, 1e-9);

    // Cancelling from the bid is negative.
    CHECK(b.remove(3) == BookError::Ok);
    fe.update(b, 3000);
    CHECK_NEAR(fe.get().ofi_touch, -50.0, 1e-9);

    // Adding to the ask is negative order flow.
    put(b, 4, Side::Ask, 9'992, 70);
    fe.update(b, 4000);
    CHECK_NEAR(fe.get().ofi_touch, -70.0, 1e-9);
  }

  // ---- deep imbalance uses more than the touch ----
  {
    OrderBook b = make_book();
    FeatureEngine fe;
    // Touch balanced, but the book behind it is heavily bid-side.
    put(b, 1, Side::Bid, 9'990, 100);
    put(b, 2, Side::Ask, 9'992, 100);
    put(b, 3, Side::Bid, 9'989, 900);
    put(b, 4, Side::Ask, 9'993, 10);
    fe.update(b, 1000);
    CHECK_NEAR(fe.get().imbalance, 0.0, 1e-12);       // touch says nothing
    CHECK(fe.get().deep_imbalance > 0.7);             // depth says plenty
  }

  // ---- one-sided and empty books must not produce NaN ----
  {
    OrderBook b = make_book();
    FeatureEngine fe;
    fe.update(b, 1000);                     // empty
    CHECK(!std::isnan(fe.get().imbalance));
    CHECK(!std::isnan(fe.get().weighted_mid));
    CHECK_NEAR(fe.get().imbalance, 0.0, 1e-12);

    put(b, 1, Side::Bid, 9'990, 100);       // bid only
    fe.update(b, 2000);
    CHECK(!std::isnan(fe.get().imbalance));
    CHECK(!std::isnan(fe.get().ofi_decayed));
    CHECK(!std::isnan(fe.get().vol_ewma));
  }

  // ---- a one-sided book is FLAGGED, not silently stale ----
  // This block used to assert only the absence of NaN, which a stale value
  // satisfies perfectly. bid, ask, mid, spread, imbalance and weighted_mid all
  // hold their last two-sided values on a one-sided book -- there is nothing
  // meaningful to replace them with -- and `updates` still increments, so a
  // consumer had no way to tell. A one-sided book is exactly the state a market
  // maker must not quote into.
  {
    OrderBook b = make_book();
    FeatureEngine fe;
    fe.update(b, 1000);                                  // empty
    CHECK(!fe.get().two_sided);

    put(b, 1, Side::Bid, 9'990, 100);
    put(b, 2, Side::Ask, 9'992, 100);
    fe.update(b, 2000);                                  // two-sided
    CHECK(fe.get().two_sided);
    CHECK_NEAR(fe.get().mid, 9'991.0, 1e-9);
    const double good_mid = fe.get().mid;

    CHECK(b.remove(2) == BookError::Ok);                 // ask side empties
    fe.update(b, 3000);
    CHECK(!fe.get().two_sided);
    // The value is stale, and that is now visible rather than merely true.
    CHECK_NEAR(fe.get().mid, good_mid, 1e-12);
    CHECK(fe.get().updates == 3U);                       // still counting
  }

  // ---- differential: incremental engine vs recomputation from scratch ----
  // This is the test that matters. The engine carries state across events, and
  // carried state is where incremental code goes wrong.
  {
    FlowConfig cfg;
    // The book must be driven through its Execute path to be proved correct, and
    // the generator no longer fabricates one by default -- see FlowConfig::w_execute.
    cfg.w_execute = 0.09;
    cfg.seed = 20260904; cfg.mid = 10'000; cfg.levels = 8; cfg.target_live = 3'000;
    FlowGenerator gen{cfg};
    OrderBook  book{5'000, 10'240, 1 << 16};
    FeatureEngine fe;

    BookTop prev{};
    bool    have_prev = false;
    int     mismatches = 0;
    double  worst = 0.0;

    for (int i = 0; i < 200'000; ++i) {
      const BookEvent e = gen.next();
      (void)book.apply(e);
      gen.on_applied(e, book.qty_of(e.order_id));
      gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());

      fe.update(book, e.ts);

      // Recompute OFI independently from two snapshots.
      BookTop now{};
      FeatureEngine::snapshot(book, now);
      if (have_prev) {
        double expect_touch = FeatureEngine::ofi_at(0, prev, now);
        double expect_deep  = 0.0;
        for (std::size_t k = 0; k < kOfiLevels; ++k) expect_deep += FeatureEngine::ofi_at(k, prev, now);
        const double dt = std::fabs(fe.get().ofi_touch - expect_touch);
        const double dd = std::fabs(fe.get().ofi_deep  - expect_deep);
        worst = std::max({worst, dt, dd});
        if (dt > 1e-9 || dd > 1e-9) { ++mismatches; if (mismatches < 3)
          std::printf("  OFI mismatch at %d: touch %f vs %f, deep %f vs %f\n",
                      i, fe.get().ofi_touch, expect_touch, fe.get().ofi_deep, expect_deep); }
      }
      prev = now; have_prev = true;

      // Imbalance recomputed straight from the book.
      if (book.has_bid() && book.has_ask()) {
        const double qb = static_cast<double>(book.best_bid_qty());
        const double qa = static_cast<double>(book.best_ask_qty());
        const double expect = (qb - qa) / (qb + qa);
        if (std::fabs(fe.get().imbalance - expect) > 1e-12) ++mismatches;
      }

      // Nothing may ever become non-finite.
      const Features& f = fe.get();
      if (!std::isfinite(f.imbalance) || !std::isfinite(f.weighted_mid) ||
          !std::isfinite(f.ofi_decayed)  || !std::isfinite(f.vol_ewma)     ||
          !std::isfinite(f.event_rate)) {
        ::lobtest::report(false, "feature became non-finite", __FILE__, __LINE__,
                          "event " + std::to_string(i));
        break;
      }
    }
    ::lobtest::report(mismatches == 0, "incremental matches recomputation", __FILE__, __LINE__,
                      std::to_string(mismatches) + " mismatches, worst delta " + std::to_string(worst));
    CHECK(fe.get().updates == 200'000U);
  }

  // ---- reset clears carried state ----
  {
    OrderBook b = make_book();
    FeatureEngine fe;
    put(b, 1, Side::Bid, 9'990, 100);
    put(b, 2, Side::Ask, 9'992, 100);
    for (int i = 0; i < 50; ++i) fe.update(b, 1000 + i * 100);
    CHECK(fe.get().updates == 50U);
    fe.reset();
    CHECK_EQ(fe.get().updates, 0U);
    CHECK_NEAR(fe.get().ofi_decayed, 0.0, 1e-12);
    CHECK_NEAR(fe.get().vol_ewma, 0.0, 1e-12);
  }

  return lobtest::summary("features");
}
