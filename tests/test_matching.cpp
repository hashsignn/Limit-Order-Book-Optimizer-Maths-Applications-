// Matching engine tests.
//
// The behaviour under test is price-time priority, and specifically that TIME
// is respected: an aggressive order eats the front of the queue first. A
// simulator that gets this wrong fills you as if you were always at the front,
// which is the single largest source of backtest overstatement.
#include "lob/book/order_book.hpp"
#include "lob/sim/matching.hpp"
#include "test_util.hpp"

#include <string>

using namespace lob;

namespace {
OrderBook make_book() { return OrderBook{9'000, 2048, 8192}; }
void put(OrderBook& b, OrderId id, Side s, Ticks px, Qty q, bool mine = false) {
  const BookError e = b.add(id, s, px, q, mine);
  ::lobtest::report(e == BookError::Ok, "add", __FILE__, __LINE__, std::string(error_name(e)));
}
}  // namespace

int main() {
  // ---- a limit order that does not cross simply rests ----
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    put(b, 1, Side::Ask, 10'010, 100);
    const auto r = m.submit_limit(1000, 2, Side::Bid, 10'000, 50);
    CHECK_EQ(r.filled, 0);
    CHECK_EQ(r.resting, 50);
    CHECK_EQ(m.fills().size(), 0U);
    CHECK_EQ(b.best_bid(), 10'000);
  }

  // ---- FIFO: the FRONT of the queue fills first, not the biggest ----
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    put(b, 1, Side::Ask, 10'010, 100);   // first in
    put(b, 2, Side::Ask, 10'010, 100);   // second
    put(b, 3, Side::Ask, 10'010, 100);   // third

    const auto r = m.submit_limit(1000, 9, Side::Bid, 10'010, 150);
    CHECK_EQ(r.filled, 150);
    CHECK_EQ(r.resting, 0);
    CHECK_EQ(r.n_fills, 2U);
    // Order 1 filled in full, order 2 partially. Order 3 untouched.
    CHECK_EQ(m.fills()[0].resting_id, 1U);
    CHECK_EQ(m.fills()[0].qty, 100);
    CHECK_EQ(m.fills()[1].resting_id, 2U);
    CHECK_EQ(m.fills()[1].qty, 50);
    CHECK_EQ(b.qty_of(1), 0);
    CHECK_EQ(b.qty_of(2), 50);
    CHECK_EQ(b.qty_of(3), 100);
  }

  // ---- the passive side sets the price ----
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    put(b, 1, Side::Ask, 10'005, 100);
    // A buyer willing to pay 10'010 trades at the resting 10'005, not at its
    // own limit. Price improvement belongs to the aggressor.
    const auto r = m.submit_limit(1000, 2, Side::Bid, 10'010, 100);
    CHECK_EQ(r.filled, 100);
    CHECK_EQ(m.fills()[0].price, 10'005);
  }

  // ---- sweeping several levels, best price first ----
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    put(b, 1, Side::Ask, 10'005, 50);
    put(b, 2, Side::Ask, 10'006, 50);
    put(b, 3, Side::Ask, 10'007, 50);

    const auto r = m.submit_limit(1000, 9, Side::Bid, 10'006, 200);
    CHECK_EQ(r.filled, 100);        // only the two levels at or below the limit
    CHECK_EQ(r.resting, 100);       // the rest rests at 10'006
    CHECK_EQ(m.fills().size(), 2U);
    CHECK_EQ(m.fills()[0].price, 10'005);
    CHECK_EQ(m.fills()[1].price, 10'006);
    CHECK_EQ(b.qty_of(3), 50);      // 10'007 was beyond the limit
    CHECK_EQ(b.best_bid(), 10'006); // the unfilled remainder is now the bid
  }

  // ---- a market order ignores price but not depth ----
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    put(b, 1, Side::Ask, 10'005, 30);
    put(b, 2, Side::Ask, 10'050, 30);
    const auto r = m.submit_market(1000, 9, Side::Bid, 100);
    CHECK_EQ(r.filled, 60);         // took everything available
    CHECK_EQ(r.resting, 0);         // and did NOT rest the remainder
    CHECK(!b.has_ask());
    CHECK(!b.has_bid());
  }

  // ---- our own passive order gets filled when the queue ahead is consumed ----
  // This is the fill model. It is the reason queue position is worth tracking.
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    put(b, 1, Side::Bid, 10'000, 100);              // ahead of us
    put(b, 99, Side::Bid, 10'000, 50, /*mine=*/true);
    put(b, 2, Side::Bid, 10'000, 100);              // behind us
    CHECK_EQ(b.queue_ahead(99), 100);

    // A seller takes 120: order 1 in full, then 20 of ours.
    const auto r = m.submit_market(2000, 500, Side::Ask, 120);
    CHECK_EQ(r.filled, 120);
    CHECK_EQ(m.fills().size(), 2U);
    CHECK(!m.fills()[0].resting_mine);
    CHECK(m.fills()[1].resting_mine);               // we got filled
    CHECK_EQ(m.fills()[1].qty, 20);
    CHECK_EQ(b.qty_of(99), 30);                     // 30 of ours still resting
    CHECK_EQ(b.queue_ahead(99), 0);                 // and now at the front
  }

  // ---- a seller that stops short of us does NOT fill us ----
  // The complement of the case above, and the one a naive backtest gets wrong.
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    put(b, 1, Side::Bid, 10'000, 100);
    put(b, 99, Side::Bid, 10'000, 50, /*mine=*/true);

    const auto r = m.submit_market(2000, 500, Side::Ask, 60);
    CHECK_EQ(r.filled, 60);
    CHECK_EQ(m.fills().size(), 1U);
    CHECK(!m.fills()[0].resting_mine);              // only the order ahead traded
    CHECK_EQ(b.qty_of(99), 50);                     // ours is untouched
    CHECK_EQ(b.queue_ahead(99), 40);                // but closer to the front
  }

  // ---- self-match prevention ----
  {
    OrderBook b = make_book(); MatchingEngine m{b, SelfMatch::CancelResting};
    put(b, 99, Side::Ask, 10'005, 100, /*mine=*/true);
    put(b, 1,  Side::Ask, 10'005, 100);

    // Our own aggressive buy must not trade with our own resting sell.
    const auto r = m.submit_limit(1000, 98, Side::Bid, 10'005, 150, /*mine=*/true);
    CHECK_EQ(m.self_matches_prevented(), 1U);
    CHECK_EQ(b.qty_of(99), 0);                      // ours was cancelled, not traded
    CHECK_EQ(r.filled, 100);                        // we traded with the stranger
    CHECK_EQ(m.fills().size(), 1U);
    CHECK_EQ(m.fills()[0].resting_id, 1U);
    CHECK(!m.fills()[0].resting_mine);
  }

  // ---- crossing an empty book just rests ----
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    const auto r = m.submit_limit(1000, 1, Side::Bid, 10'000, 100);
    CHECK_EQ(r.filled, 0);
    CHECK_EQ(r.resting, 100);
    const auto r2 = m.submit_market(1000, 2, Side::Ask, 500);
    CHECK_EQ(r2.filled, 100);                       // ate the resting bid
    const auto r3 = m.submit_market(1000, 3, Side::Ask, 500);
    CHECK_EQ(r3.filled, 0);                         // nothing left; no spin, no crash
  }

  // ---- the fill log is a sliding window, not an unbounded vector ----
  // It used to grow for the life of the engine, so memory scaled with run
  // length. fuzz/fuzz_matching.cpp calls clear_fills() every 32 operations
  // purely to work around that. Nothing is dropped until a consumer says it has
  // read it, and an index handed out earlier stays valid across the trim.
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    for (int i = 0; i < 200; ++i) {
      put(b, static_cast<OrderId>(i + 1), Side::Ask, 10'005, 10);
      (void)m.submit_market(1000 + i, static_cast<OrderId>(9000 + i), Side::Bid, 10);
    }
    CHECK_EQ(m.fills_begin(), 0U);
    CHECK_EQ(m.fills_end(), 200U);
    const Fill f150 = m.fill_at(150);          // note the value before trimming

    m.consume_through(150);
    CHECK_EQ(m.fills_begin(), 150U);
    CHECK_EQ(m.fills_end(), 200U);             // the end index does not move
    CHECK_EQ(m.fills().size(), 50U);           // but the window really shrank
    // The same global index still names the same fill.
    CHECK_EQ(m.fill_at(150).resting_id, f150.resting_id);
    CHECK_EQ(m.fill_at(150).ts, f150.ts);

    m.consume_through(150);                    // idempotent
    CHECK_EQ(m.fills_begin(), 150U);
    m.consume_through(10);                     // already behind the window
    CHECK_EQ(m.fills_begin(), 150U);
    m.consume_through(10'000);                 // past the end: drains it
    CHECK_EQ(m.fills_begin(), 200U);
    CHECK_EQ(m.fills().size(), 0U);
    CHECK_EQ(m.fills_end(), 200U);
  }

  // ---- the book is never left crossed after matching ----
  {
    OrderBook b = make_book(); MatchingEngine m{b};
    put(b, 1, Side::Ask, 10'005, 100);
    (void)m.submit_limit(1000, 2, Side::Bid, 10'010, 300);
    std::string why;
    ::lobtest::report(b.check_invariants(&why), "invariants after cross", __FILE__, __LINE__, why);
    CHECK(!b.has_ask());
    CHECK_EQ(b.best_bid(), 10'010);
    CHECK_EQ(b.spread(), 0);   // one-sided, so no spread to speak of
  }

  return lobtest::summary("matching");
}
