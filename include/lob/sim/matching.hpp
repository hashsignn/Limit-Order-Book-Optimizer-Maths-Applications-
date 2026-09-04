// Matching engine.
//
// The book consumes an already-matched feed: by the time an exchange publishes
// an add, anything crossing has already traded. A simulator has to do that
// matching itself, and this is where the honesty of a backtest is decided.
//
// The rule this implements is price-time priority, and the part that matters is
// *time*: an aggressive order consumes the front of the queue first. That is
// why queue position is worth anything at all, and why a backtest that fills
// you whenever the price touches your limit overstates P&L by a large factor —
// it is quietly assuming you were always at the front.
//
// Pro-rata venues (STIR futures, see arXiv:1205.3051) allocate differently and
// need a separate policy. Not implemented; the roadmap has it out of scope.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "lob/book/order_book.hpp"
#include "lob/core/types.hpp"

namespace lob {

struct Fill {
  Nanos   ts             = 0;
  OrderId resting_id     = 0;   // the passive order that was filled
  OrderId aggressor_id   = 0;   // the order that crossed the spread
  Ticks   price          = 0;   // the RESTING order's price: passive side sets it
  Qty     qty            = 0;
  Side    resting_side   = Side::Bid;
  bool    resting_mine   = false;
  bool    aggressor_mine = false;
};

struct SubmitResult {
  Qty       filled     = 0;   // traded immediately against resting liquidity
  Qty       resting    = 0;   // what was left and joined the book
  std::size_t first_fill = 0; // index range into fills() for this submission
  std::size_t n_fills    = 0;
  BookError error      = BookError::Ok;
};

// How a venue handles an order that would trade against its own resting order.
enum class SelfMatch : std::uint8_t {
  Allow,          // trade with yourself (never what a real venue does)
  CancelResting,  // drop the resting order, aggressor continues — the common rule
  CancelIncoming  // drop the remainder of the aggressor
};

class MatchingEngine {
 public:
  explicit MatchingEngine(OrderBook& book, SelfMatch smp = SelfMatch::CancelResting)
      : book_(book), smp_(smp) { fills_.reserve(1024); }

  // A limit order: cross what it can, rest the remainder.
  SubmitResult submit_limit(Nanos ts, OrderId id, Side side, Ticks price, Qty qty,
                            bool mine = false) {
    SubmitResult r;
    r.first_fill = fills_.size();
    if (qty <= 0) { r.error = BookError::BadQuantity; return r; }

    Qty remaining = cross(ts, id, side, &price, qty, mine);
    r.filled  = qty - remaining;
    r.n_fills = fills_.size() - r.first_fill;

    if (remaining > 0) {
      r.error = book_.add(id, side, price, remaining, mine);
      if (r.error == BookError::Ok) r.resting = remaining;
    }
    return r;
  }

  // A market order: cross until filled or the book runs out. Never rests.
  // Unfilled size is simply lost, which is what happens on a venue that does
  // not convert the remainder into a resting order.
  SubmitResult submit_market(Nanos ts, OrderId id, Side side, Qty qty, bool mine = false) {
    SubmitResult r;
    r.first_fill = fills_.size();
    if (qty <= 0) { r.error = BookError::BadQuantity; return r; }

    const Qty remaining = cross(ts, id, side, nullptr, qty, mine);
    r.filled  = qty - remaining;
    r.n_fills = fills_.size() - r.first_fill;
    return r;
  }

  BookError cancel(OrderId id) { return book_.remove(id); }

  [[nodiscard]] const std::vector<Fill>& fills() const noexcept { return fills_; }
  void clear_fills() noexcept { fills_.clear(); }

  [[nodiscard]] std::uint64_t self_matches_prevented() const noexcept { return smp_hits_; }

 private:
  // Walks the opposing side from the touch, taking the FRONT of each level
  // first. `limit` is null for a market order. Returns unfilled quantity.
  Qty cross(Nanos ts, OrderId aggressor, Side side, const Ticks* limit, Qty qty, bool mine) {
    const Side other = opposite(side);
    Qty remaining = qty;

    while (remaining > 0) {
      const bool has = (other == Side::Bid) ? book_.has_bid() : book_.has_ask();
      if (!has) break;

      const Ticks lvl = (other == Side::Bid) ? book_.best_bid() : book_.best_ask();
      if (limit != nullptr) {
        // A buy crosses asks at or below its limit; a sell crosses bids at or above.
        const bool crosses = (side == Side::Bid) ? (lvl <= *limit) : (lvl >= *limit);
        if (!crosses) break;
      }

      // Drain this level front-first, which is the whole point.
      bool progressed = false;
      while (remaining > 0) {
        const OrderId front = book_.front_order_at(other, lvl);
        if (front == 0) break;

        const Qty avail = book_.qty_of(front);
        if (avail <= 0) break;

        if (mine && book_.is_mine(front)) {
          // Self-match. A venue will not let you trade with yourself, and a
          // simulator that does will invent P&L out of nothing.
          ++smp_hits_;
          if (smp_ == SelfMatch::CancelResting) {
            (void)book_.remove(front);
            progressed = true;
            continue;
          }
          if (smp_ == SelfMatch::CancelIncoming) return remaining;
        }

        const Qty take = std::min(remaining, avail);
        const bool resting_mine = book_.is_mine(front);
        if (book_.execute(front, take) != BookError::Ok) break;

        fills_.push_back(Fill{ts, front, aggressor, lvl, take, other, resting_mine, mine});
        remaining -= take;
        progressed = true;
      }
      if (!progressed) break;   // nothing consumable here: stop rather than spin
    }
    return remaining;
  }

  OrderBook&        book_;
  SelfMatch         smp_;
  std::vector<Fill> fills_;
  std::uint64_t     smp_hits_ = 0;
};

}  // namespace lob
