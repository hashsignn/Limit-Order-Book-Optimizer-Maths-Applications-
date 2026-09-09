// Property tests.
//
// Not "does this case work" but "is this statement true for every input we can
// generate". Each property below is something the rest of the system silently
// relies on; if one is false, code elsewhere is wrong in a way no unit test
// would notice.
#include "lob/book/order_book.hpp"
#include "lob/feat/features.hpp"
#include "lob/measure/histogram.hpp"
#include "lob/sim/flow.hpp"
#include "lob/sim/matching.hpp"
#include "test_util.hpp"

#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace lob;

namespace {
constexpr Ticks         kBase   = 5'000;
constexpr std::uint32_t kWindow = 10'240;
constexpr std::size_t   kOrders = 1 << 16;
}  // namespace

int main() {
  // ==== PROPERTY: quantity is conserved =====================================
  // Everything that enters the book leaves it or is still resting. A leak here
  // would show up as depth that is not there, which is the sort of error a
  // strategy would happily trade against for a long time.
  {
    FlowConfig cfg; cfg.seed = 11; cfg.mid = 10'000; cfg.levels = 8; cfg.target_live = 3'000; cfg.w_execute = 0.09;
    FlowGenerator gen{cfg};
    OrderBook book{kBase, kWindow, kOrders};

    Qty added = 0, removed = 0;
    for (int i = 0; i < 150'000; ++i) {
      const BookEvent e = gen.next();
      const Qty before = book.qty_of(e.order_id);
      const BookError r = book.apply(e);
      const Qty after  = book.qty_of(e.order_id);

      if (r == BookError::Ok) {
        switch (e.type) {
          case EventType::Add:     added   += e.qty; break;
          case EventType::Delete:  removed += before; break;
          case EventType::Reduce:
          case EventType::Execute: removed += before - after; break;
          case EventType::Replace: removed += before; added += e.qty; break;
          default: break;
        }
      }
      gen.on_applied(e, after);
      gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());
    }

    // Walk every level and sum what is actually resting.
    Qty resting = 0;
    for (int si = 0; si < 2; ++si) {
      const Side s = static_cast<Side>(si);
      Ticks px[512]; Qty qy[512];
      const std::uint32_t n = book.depth(s, 512, px, qy);
      for (std::uint32_t j = 0; j < n; ++j) resting += qy[j];
    }
    ::lobtest::report(added - removed == resting, "quantity is conserved", __FILE__, __LINE__,
                      "added " + std::to_string(added) + " - removed " + std::to_string(removed) +
                      " = " + std::to_string(added - removed) + " but resting " + std::to_string(resting));
  }

  // ==== PROPERTY: matching conserves quantity and respects the limit ========
  {
    OrderBook book{kBase, kWindow, kOrders};
    MatchingEngine match{book};
    std::mt19937_64 rng{7};
    OrderId next = 1;

    for (int i = 0; i < 60'000; ++i) {
      // Keep a two-sided book to cross into.
      if ((i % 3) != 0) {
        const Side s = (rng() & 1) ? Side::Bid : Side::Ask;
        const Ticks px = (s == Side::Bid) ? 10'000 - 1 - static_cast<Ticks>(rng() % 10)
                                          : 10'000 + 1 + static_cast<Ticks>(rng() % 10);
        (void)book.add(next++, s, px, 1 + static_cast<Qty>(rng() % 100));
        continue;
      }

      const Side  s     = (rng() & 1) ? Side::Bid : Side::Ask;
      const Qty   qty   = 1 + static_cast<Qty>(rng() % 300);
      const Ticks limit = 10'000 + static_cast<Ticks>(rng() % 21) - 10;

      const std::size_t before_fills = match.fills().size();
      const auto r = match.submit_limit(static_cast<Nanos>(i), next++, s, limit, qty);
      if (r.error != BookError::Ok && r.filled == 0) continue;

      // Conservation: everything submitted either traded or rests.
      ::lobtest::report(r.filled + r.resting == qty || r.error != BookError::Ok,
                        "limit order quantity is conserved", __FILE__, __LINE__,
                        "filled " + std::to_string(r.filled) + " + resting " +
                        std::to_string(r.resting) + " != " + std::to_string(qty));

      Qty fill_sum = 0;
      Ticks prev_px = 0; bool first = true;
      for (std::size_t k = before_fills; k < match.fills().size(); ++k) {
        const Fill& f = match.fills()[k];
        fill_sum += f.qty;

        // Never trade through the limit.
        if (s == Side::Bid)
          ::lobtest::report(f.price <= limit, "buy never fills above its limit",
                            __FILE__, __LINE__, std::to_string(f.price) + " > " + std::to_string(limit));
        else
          ::lobtest::report(f.price >= limit, "sell never fills below its limit",
                            __FILE__, __LINE__, std::to_string(f.price) + " < " + std::to_string(limit));

        // Prices sweep best-first: monotonically worse for the aggressor.
        if (!first) {
          const bool ordered = (s == Side::Bid) ? (f.price >= prev_px) : (f.price <= prev_px);
          ::lobtest::report(ordered, "fills sweep best price first", __FILE__, __LINE__,
                            std::to_string(prev_px) + " then " + std::to_string(f.price));
        }
        prev_px = f.price; first = false;
      }
      ::lobtest::report(fill_sum == r.filled, "fill quantities sum to reported fill",
                        __FILE__, __LINE__,
                        std::to_string(fill_sum) + " != " + std::to_string(r.filled));

      if ((i % 5000) == 0) match.clear_fills();
    }

    std::string why;
    ::lobtest::report(book.check_invariants(&why), "book valid after matching", __FILE__, __LINE__, why);
  }

  // ==== PROPERTY: features stay in their mathematical range =================
  {
    FlowConfig cfg; cfg.seed = 99; cfg.mid = 10'000; cfg.levels = 8; cfg.target_live = 2'000; cfg.w_execute = 0.09;
    FlowGenerator gen{cfg};
    OrderBook book{kBase, kWindow, kOrders};
    FeatureEngine fe;

    int out_of_range = 0, wmid_outside = 0;
    for (int i = 0; i < 150'000; ++i) {
      const BookEvent e = gen.next();
      (void)book.apply(e);
      gen.on_applied(e, book.qty_of(e.order_id));
      gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());
      fe.update(book, e.ts);

      const Features& f = fe.get();
      if (f.imbalance < -1.0 - 1e-12 || f.imbalance > 1.0 + 1e-12) ++out_of_range;
      if (f.deep_imbalance < -1.0 - 1e-12 || f.deep_imbalance > 1.0 + 1e-12) ++out_of_range;

      // The weighted mid must lie inside the spread: it is a convex blend of
      // the two touch prices, so a value outside means the blend is wrong.
      if (book.has_bid() && book.has_ask() && f.mid > 0.0) {
        const auto lo = static_cast<double>(book.best_bid());
        const auto hi = static_cast<double>(book.best_ask());
        if (f.weighted_mid < lo - 1e-9 || f.weighted_mid > hi + 1e-9) ++wmid_outside;
      }
    }
    ::lobtest::report(out_of_range == 0, "imbalance stays in [-1, 1]", __FILE__, __LINE__,
                      std::to_string(out_of_range) + " violations");
    ::lobtest::report(wmid_outside == 0, "weighted mid stays inside the spread", __FILE__, __LINE__,
                      std::to_string(wmid_outside) + " violations");
  }

  // ==== PROPERTY: percentiles are monotone in p ============================
  {
    std::mt19937_64 rng{3};
    int violations = 0;
    for (int trial = 0; trial < 40; ++trial) {
      Histogram h{1'000'000'000LL, 3};
      const int n = 100 + static_cast<int>(rng() % 20'000);
      for (int i = 0; i < n; ++i) h.record(static_cast<std::int64_t>(rng() % 10'000'000));
      std::int64_t prev = -1;
      for (const double pct : {0.0, 1.0, 10.0, 25.0, 50.0, 75.0, 90.0, 99.0, 99.9, 100.0}) {
        const std::int64_t v = h.value_at_percentile(pct);
        if (v < prev) ++violations;
        prev = v;
      }
      // The boundary contract of a BUCKETED histogram, which is weaker than it
      // first looks and worth stating precisely: min and max are tracked
      // exactly, but a percentile is resolved to a bucket. So p0 is the FLOOR
      // of the bucket holding the minimum and p100 is the CEILING of the one
      // holding the maximum. Asserting p0 >= min or p100 <= max claims a
      // precision the structure does not provide, and both are false in
      // general — an easy mistake, since both look like the obvious sanity
      // check to write.
      if (h.value_at_percentile(0.0)   != h.lowest_equivalent(h.min()))  ++violations;
      if (h.value_at_percentile(100.0) != h.highest_equivalent(h.max())) ++violations;
      // What IS always true: the reported extremes bracket the real ones.
      if (h.value_at_percentile(0.0)   >  h.min()) ++violations;
      if (h.value_at_percentile(100.0) <  h.max()) ++violations;
    }
    ::lobtest::report(violations == 0, "percentiles are monotone and bracket the extremes",
                      __FILE__, __LINE__, std::to_string(violations) + " violations");
  }

  // ==== PROPERTY: a failed operation leaves the book untouched =============
  // Errors must be inert. If a rejected operation half-applies, the book is
  // corrupt in a way nothing downstream can detect.
  {
    OrderBook book{kBase, kWindow, 64};
    (void)book.add(1, Side::Bid, 9'990, 100);
    (void)book.add(2, Side::Ask, 10'010, 100);

    auto snapshot = [&] {
      return std::to_string(book.best_bid()) + "/" + std::to_string(book.best_ask()) + "/" +
             std::to_string(book.qty_at(Side::Bid, 9'990)) + "/" +
             std::to_string(book.qty_at(Side::Ask, 10'010)) + "/" +
             std::to_string(book.live_orders());
    };
    const std::string before = snapshot();

    // Every rejection path.
    CHECK(book.add(1, Side::Bid, 9'991, 10)      == BookError::DuplicateOrder);
    CHECK(book.add(9, Side::Bid, 9'990, 0)       == BookError::BadQuantity);
    CHECK(book.add(9, Side::Bid, 1, 10)          == BookError::PriceOutOfWindow);
    CHECK(book.add(9, Side::Bid, 10'010, 10)     == BookError::CrossedBook);
    CHECK(book.add(9, Side::Ask, 9'990, 10)      == BookError::CrossedBook);
    CHECK(book.remove(999)                       == BookError::UnknownOrder);
    CHECK(book.reduce(999, 1)                    == BookError::UnknownOrder);
    CHECK(book.execute(999, 1)                   == BookError::UnknownOrder);
    CHECK(book.reduce(1, 9999)                   == BookError::BadQuantity);
    CHECK(book.execute(1, 9999)                  == BookError::BadQuantity);

    ::lobtest::report(snapshot() == before, "rejections leave the book untouched",
                      __FILE__, __LINE__, before + " -> " + snapshot());
    std::string why;
    ::lobtest::report(book.check_invariants(&why), "book valid after rejections",
                      __FILE__, __LINE__, why);
  }

  // ==== PROPERTY: clear then replay == fresh replay ========================
  {
    FlowConfig cfg; cfg.seed = 555; cfg.mid = 10'000; cfg.levels = 6; cfg.target_live = 800; cfg.w_execute = 0.09;
    std::vector<BookEvent> events;
    {
      FlowGenerator gen{cfg};
      OrderBook scratch{kBase, kWindow, kOrders};
      for (int i = 0; i < 30'000; ++i) {
        const BookEvent e = gen.next();
        (void)scratch.apply(e);
        gen.on_applied(e, scratch.qty_of(e.order_id));
        gen.observe(scratch.has_bid(), scratch.best_bid(), scratch.has_ask(), scratch.best_ask());
        events.push_back(e);
      }
    }

    OrderBook fresh{kBase, kWindow, kOrders};
    for (const auto& e : events) (void)fresh.apply(e);

    OrderBook reused{kBase, kWindow, kOrders};
    for (const auto& e : events) (void)reused.apply(e);   // dirty it first
    reused.clear();
    for (const auto& e : events) (void)reused.apply(e);

    CHECK_EQ(fresh.best_bid(), reused.best_bid());
    CHECK_EQ(fresh.best_ask(), reused.best_ask());
    CHECK_EQ(fresh.live_orders(), reused.live_orders());
    CHECK_EQ(fresh.best_bid_qty(), reused.best_bid_qty());
    CHECK_EQ(fresh.best_ask_qty(), reused.best_ask_qty());
    Ticks p1[64], p2[64]; Qty q1[64], q2[64];
    const std::uint32_t n1 = fresh.depth(Side::Bid, 64, p1, q1);
    const std::uint32_t n2 = reused.depth(Side::Bid, 64, p2, q2);
    CHECK_EQ(n1, n2);
    bool same = true;
    for (std::uint32_t i = 0; i < n1 && i < n2; ++i)
      if (p1[i] != p2[i] || q1[i] != q2[i]) same = false;
    ::lobtest::report(same, "clear() fully resets state", __FILE__, __LINE__, "");
  }

  return lobtest::summary("properties");
}
