// Unit tests for the L3 book: the specific behaviours a market maker depends
// on, stated as assertions. The differential test is separate.
#include "lob/book/order_book.hpp"
#include "test_util.hpp"

#include <string>

using namespace lob;

namespace {
// A window wide enough for the tests, small enough to construct quickly.
OrderBook make_book() { return OrderBook{9'000, 2048, 4096}; }

void expect_ok(BookError e, const char* what) {
  ::lobtest::report(e == BookError::Ok, what, __FILE__, __LINE__, std::string(error_name(e)));
}
void expect_valid(const OrderBook& b) {
  std::string why;
  ::lobtest::report(b.check_invariants(&why), "invariants hold", __FILE__, __LINE__, why);
}
}  // namespace

int main() {
  // ---- empty book ----
  {
    OrderBook b = make_book();
    CHECK(!b.has_bid());
    CHECK(!b.has_ask());
    CHECK_EQ(b.spread(), 0);
    CHECK_EQ(b.live_orders(), 0U);
    expect_valid(b);
  }

  // ---- top of book tracking ----
  {
    OrderBook b = make_book();
    expect_ok(b.add(1, Side::Bid, 9'990, 100), "add bid");
    expect_ok(b.add(2, Side::Ask, 10'010, 100), "add ask");
    CHECK_EQ(b.best_bid(), 9'990);
    CHECK_EQ(b.best_ask(), 10'010);
    CHECK_EQ(b.spread(), 20);

    // A better bid moves the touch.
    expect_ok(b.add(3, Side::Bid, 9'995, 50), "improve bid");
    CHECK_EQ(b.best_bid(), 9'995);
    CHECK_EQ(b.best_bid_qty(), 50);

    // A worse bid does not.
    expect_ok(b.add(4, Side::Bid, 9'980, 999), "worse bid");
    CHECK_EQ(b.best_bid(), 9'995);

    // Removing the touch falls back to the next live level, not to empty.
    expect_ok(b.remove(3), "remove touch");
    CHECK_EQ(b.best_bid(), 9'990);
    expect_ok(b.remove(1), "remove next");
    CHECK_EQ(b.best_bid(), 9'980);
    expect_ok(b.remove(4), "remove last bid");
    CHECK(!b.has_bid());
    expect_valid(b);
  }

  // ---- FIFO priority: orders fill in arrival order, not size order ----
  {
    OrderBook b = make_book();
    expect_ok(b.add(10, Side::Bid, 9'990, 100), "first");
    expect_ok(b.add(11, Side::Bid, 9'990, 200), "second");
    expect_ok(b.add(12, Side::Bid, 9'990, 300), "third");
    CHECK_EQ(b.qty_at(Side::Bid, 9'990), 600);
    CHECK_EQ(b.orders_at(Side::Bid, 9'990), 3U);
    expect_valid(b);
  }

  // ---- queue position: the number the whole project turns on ----
  {
    OrderBook b = make_book();
    expect_ok(b.add(20, Side::Bid, 9'990, 100), "ahead A");
    expect_ok(b.add(21, Side::Bid, 9'990, 250), "ahead B");
    expect_ok(b.add(99, Side::Bid, 9'990, 10, /*mine=*/true), "ours");
    expect_ok(b.add(22, Side::Bid, 9'990, 400), "behind");

    // 350 rests ahead of us; the order behind is irrelevant.
    CHECK_EQ(b.queue_ahead(99), 350);
    CHECK(b.is_mine(99));
    CHECK(!b.is_mine(20));

    // A fill in front moves us up.
    expect_ok(b.execute(20, 60), "partial fill ahead");
    CHECK_EQ(b.queue_ahead(99), 290);

    // So does a cancel in front.
    expect_ok(b.remove(21), "cancel ahead");
    CHECK_EQ(b.queue_ahead(99), 40);

    // Activity BEHIND us must not change our position.
    expect_ok(b.execute(22, 100), "fill behind");
    CHECK_EQ(b.queue_ahead(99), 40);
    expect_ok(b.remove(22), "cancel behind");
    CHECK_EQ(b.queue_ahead(99), 40);

    // Front order fully consumed: we are now at the front.
    expect_ok(b.execute(20, 40), "consume rest ahead");
    CHECK_EQ(b.queue_ahead(99), 0);
    expect_valid(b);
  }

  // ---- a partial cancel KEEPS priority; a replace LOSES it ----
  {
    OrderBook b = make_book();
    expect_ok(b.add(30, Side::Ask, 10'010, 500), "ahead");
    expect_ok(b.add(31, Side::Ask, 10'010, 100, /*mine=*/true), "ours");
    CHECK_EQ(b.queue_ahead(31), 500);

    // Reducing the order ahead shrinks what is in front of us.
    expect_ok(b.reduce(30, 200), "reduce ahead");
    CHECK_EQ(b.queue_ahead(31), 300);

    // Reducing OUR order keeps our place — this is why shrinking beats requoting.
    expect_ok(b.reduce(31, 50), "reduce ours");
    CHECK_EQ(b.queue_ahead(31), 300);

    // Replacing ours sends it to the back, behind everything at the new level.
    expect_ok(b.add(32, Side::Ask, 10'010, 700), "someone behind");
    expect_ok(b.replace(31, 33, 10'010, 100), "replace ours");
    CHECK_EQ(b.queue_ahead(31), -1);          // old id is gone
    CHECK_EQ(b.queue_ahead(33), 300 + 700);   // now behind both
    expect_valid(b);
  }

  // ---- executes and deletes are accounted separately ----
  {
    OrderBook b = make_book();
    expect_ok(b.add(40, Side::Bid, 9'990, 100), "add");
    expect_ok(b.add(41, Side::Bid, 9'990, 100), "add");
    expect_ok(b.execute(40, 100), "full fill");
    expect_ok(b.remove(41), "cancel");
    CHECK_EQ(b.stats().executes, 1U);
    CHECK_EQ(b.stats().deletes, 1U);
    CHECK(!b.has_bid());
    expect_valid(b);
  }

  // ---- errors are returned and counted, never thrown or ignored ----
  {
    OrderBook b = make_book();
    CHECK(b.add(50, Side::Bid, 9'990, 0)      == BookError::BadQuantity);
    CHECK(b.add(50, Side::Bid, 9'990, -5)     == BookError::BadQuantity);
    CHECK(b.add(50, Side::Bid, 100, 10)       == BookError::PriceOutOfWindow);
    CHECK(b.add(50, Side::Bid, 999'999, 10)   == BookError::PriceOutOfWindow);
    expect_ok(b.add(50, Side::Bid, 9'990, 10), "valid add");
    CHECK(b.add(50, Side::Bid, 9'991, 10)     == BookError::DuplicateOrder);
    CHECK(b.remove(12345)                     == BookError::UnknownOrder);
    CHECK(b.reduce(12345, 1)                  == BookError::UnknownOrder);
    CHECK(b.execute(12345, 1)                 == BookError::UnknownOrder);
    CHECK(b.reduce(50, 999)                   == BookError::BadQuantity);
    CHECK(b.execute(50, 999)                  == BookError::BadQuantity);
    CHECK(b.stats().errors[static_cast<std::size_t>(BookError::UnknownOrder)] == 3U);
    // A rejected duplicate must not have corrupted the resting order.
    CHECK_EQ(b.qty_at(Side::Bid, 9'990), 10);
    expect_valid(b);
  }

  // ---- depth ladder walks outward from the touch, skipping empty levels ----
  {
    OrderBook b = make_book();
    for (int i = 0; i < 5; ++i)
      expect_ok(b.add(static_cast<OrderId>(60 + i), Side::Bid,
                      9'990 - i * 2, 10 * (i + 1)), "ladder");
    Ticks p[8]; Qty q[8];
    const std::uint32_t n = b.depth(Side::Bid, 8, p, q);
    CHECK_EQ(n, 5U);
    CHECK_EQ(p[0], 9'990); CHECK_EQ(q[0], 10);
    CHECK_EQ(p[1], 9'988); CHECK_EQ(q[1], 20);
    CHECK_EQ(p[4], 9'982); CHECK_EQ(q[4], 50);
    expect_valid(b);
  }

  // ---- clear resets everything, including the free list ----
  {
    OrderBook b = make_book();
    for (int i = 0; i < 100; ++i)
      expect_ok(b.add(static_cast<OrderId>(1000 + i), Side::Bid, 9'990 - (i % 10), 5), "fill");
    expect_ok(b.add(2000, Side::Bid, 9'985, 5, true), "ours");
    b.clear();
    CHECK(!b.has_bid());
    CHECK_EQ(b.live_orders(), 0U);
    CHECK_EQ(b.own_orders().size(), 0U);
    expect_valid(b);
    // Reusable after clear.
    expect_ok(b.add(3000, Side::Bid, 9'990, 42), "add after clear");
    CHECK_EQ(b.best_bid(), 9'990);
    expect_valid(b);
  }

  // ---- pool exhaustion is reported, not a crash or a heap fallback ----
  {
    OrderBook b{9'000, 2048, 8};
    for (int i = 0; i < 8; ++i)
      expect_ok(b.add(static_cast<OrderId>(i + 1), Side::Bid, 9'990, 1), "fill pool");
    CHECK(b.add(999, Side::Bid, 9'990, 1) == BookError::PoolExhausted);
    expect_valid(b);
    // Freeing a slot makes room again.
    expect_ok(b.remove(1), "free one");
    expect_ok(b.add(999, Side::Bid, 9'990, 1), "reuse slot");
    expect_valid(b);
  }

  return lobtest::summary("book");
}
