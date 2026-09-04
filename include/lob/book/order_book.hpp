// L3 (market-by-order) limit order book.
//
// Layout, and why:
//
//   price levels   Flat array indexed by (price_ticks - window_base), not a
//                  std::map. A map costs a pointer chase and a cache miss per
//                  level; an array index is arithmetic. Prices outside the
//                  window are rejected and counted, never silently dropped.
//
//   occupancy      One bitset per side. Finding the next live level is
//                  countr_zero / countl_zero over 64 levels at a time instead
//                  of walking empty slots. Best bid/ask are cached and updated
//                  incrementally, so the common case is O(1) and the scan only
//                  runs when a touch level empties.
//
//   orders         Intrusive doubly-linked FIFO per level, threaded through a
//                  preallocated pool. Cancel is an O(1) unlink with no search.
//
//   own orders     Live in the SAME FIFO as everyone else's. This is the
//                  decision the project turns on: queue position falls out of
//                  the structure instead of needing a parallel bookkeeping
//                  system that has to be kept in sync. See queue_ahead().
#pragma once

#include <bit>
#include <cstdint>
#include <vector>

#include "lob/book/events.hpp"
#include "lob/book/order_map.hpp"
#include "lob/core/types.hpp"

namespace lob {

inline constexpr std::uint32_t kNullOrder = 0xFFFFFFFFU;

struct Order {
  OrderId       id        = 0;
  Qty           qty       = 0;
  std::uint32_t level     = 0;         // index into the price window
  std::uint32_t prev      = kNullOrder;
  std::uint32_t next      = kNullOrder;
  std::uint64_t level_seq = 0;         // arrival rank within its level: lower is ahead
  Side          side      = Side::Bid;
  bool          mine      = false;
  std::uint8_t  _pad[6]   = {};
};

struct Level {
  Qty           total_qty = 0;
  std::uint32_t head      = kNullOrder;   // front of queue: fills first
  std::uint32_t tail      = kNullOrder;   // back of queue: newest arrival
  std::uint32_t count     = 0;
  std::uint64_t next_seq  = 0;            // monotonic arrival counter for this level
};

// What the book knows about one of our own resting orders. `qty_ahead` is
// maintained incrementally as the queue in front drains, so reading our queue
// position never walks the level.
struct OwnOrder {
  OrderId       id        = 0;
  std::uint32_t level     = 0;
  std::uint64_t level_seq = 0;
  Qty           qty_ahead = 0;
  Side          side      = Side::Bid;
};

struct BookStats {
  std::uint64_t events        = 0;
  std::uint64_t adds          = 0;
  std::uint64_t deletes       = 0;
  std::uint64_t reduces       = 0;
  std::uint64_t executes      = 0;
  std::uint64_t replaces      = 0;
  std::uint64_t errors[static_cast<std::size_t>(BookError::Count)] = {};
};

class OrderBook {
 public:
  // `window_ticks` must be a multiple of 64 (the bitset word size). 65536 ticks
  // at a 1-cent tick covers a $655 range, far beyond any intraday move, so the
  // window is set once and never recentred.
  OrderBook(Ticks window_base, std::uint32_t window_ticks, std::size_t max_orders);

  BookError apply(const BookEvent& e) noexcept;

  // Individual operations, also the API the synthetic generator drives.
  BookError add(OrderId id, Side side, Ticks price, Qty qty, bool mine = false) noexcept;
  BookError reduce(OrderId id, Qty by) noexcept;      // partial cancel, keeps priority
  BookError remove(OrderId id) noexcept;              // full cancel
  BookError execute(OrderId id, Qty qty) noexcept;    // fill from the front
  BookError replace(OrderId old_id, OrderId new_id, Ticks price, Qty qty) noexcept;
  void      clear() noexcept;

  // ---- top of book ----
  [[nodiscard]] bool  has_bid() const noexcept { return best_bid_ != kNoLevel; }
  [[nodiscard]] bool  has_ask() const noexcept { return best_ask_ != kNoLevel; }
  [[nodiscard]] Ticks best_bid() const noexcept { return to_price(best_bid_); }
  [[nodiscard]] Ticks best_ask() const noexcept { return to_price(best_ask_); }
  [[nodiscard]] Qty   best_bid_qty() const noexcept { return best_bid_ == kNoLevel ? 0 : bid_[best_bid_].total_qty; }
  [[nodiscard]] Qty   best_ask_qty() const noexcept { return best_ask_ == kNoLevel ? 0 : ask_[best_ask_].total_qty; }
  [[nodiscard]] Ticks spread() const noexcept {
    return (has_bid() && has_ask()) ? best_ask() - best_bid() : 0;
  }

  // ---- depth ----
  [[nodiscard]] Qty qty_at(Side s, Ticks price) const noexcept;
  // Remaining size of one resting order, or 0 if it is no longer in the book.
  [[nodiscard]] Qty qty_of(OrderId id) const noexcept;
  [[nodiscard]] std::uint32_t orders_at(Side s, Ticks price) const noexcept;
  // Walks up to `n` live levels outward from the touch. Returns how many filled.
  std::uint32_t depth(Side s, std::uint32_t n, Ticks* prices, Qty* qtys) const noexcept;

  // ---- own orders and queue position ----
  // Volume resting ahead of our order at its level. O(1): maintained as the
  // queue drains rather than recomputed.
  [[nodiscard]] Qty  queue_ahead(OrderId id) const noexcept;
  [[nodiscard]] bool is_mine(OrderId id) const noexcept;
  [[nodiscard]] const std::vector<OwnOrder>& own_orders() const noexcept { return own_; }

  // ---- introspection ----
  [[nodiscard]] std::size_t live_orders() const noexcept { return map_.size(); }
  [[nodiscard]] const BookStats& stats() const noexcept { return stats_; }
  [[nodiscard]] Ticks window_base() const noexcept { return window_base_; }
  [[nodiscard]] std::uint32_t window_ticks() const noexcept { return window_; }

  // Full O(n) consistency check. Debug/test only — never on the hot path.
  // Verifies: bid < ask; every level's total_qty and count match its FIFO;
  // every mapped order is reachable from its level; bitsets match emptiness;
  // cached best bid/ask match the bitsets; own-order qty_ahead is correct.
  [[nodiscard]] bool check_invariants(std::string* why = nullptr) const;

 private:
  static constexpr std::uint32_t kNoLevel = 0xFFFFFFFFU;

  [[nodiscard]] bool in_window(Ticks p) const noexcept {
    return p >= window_base_ && p < window_base_ + static_cast<Ticks>(window_);
  }
  [[nodiscard]] std::uint32_t to_index(Ticks p) const noexcept {
    return static_cast<std::uint32_t>(p - window_base_);
  }
  [[nodiscard]] Ticks to_price(std::uint32_t idx) const noexcept {
    return idx == kNoLevel ? 0 : window_base_ + static_cast<Ticks>(idx);
  }

  std::vector<Level>&       side_levels(Side s) noexcept { return s == Side::Bid ? bid_ : ask_; }
  const std::vector<Level>& side_levels(Side s) const noexcept { return s == Side::Bid ? bid_ : ask_; }
  std::vector<std::uint64_t>&       side_bits(Side s) noexcept { return s == Side::Bid ? bid_bits_ : ask_bits_; }
  const std::vector<std::uint64_t>& side_bits(Side s) const noexcept { return s == Side::Bid ? bid_bits_ : ask_bits_; }

  void set_bit(Side s, std::uint32_t i) noexcept {
    side_bits(s)[i >> 6] |= (std::uint64_t{1} << (i & 63));
  }
  void clear_bit(Side s, std::uint32_t i) noexcept {
    side_bits(s)[i >> 6] &= ~(std::uint64_t{1} << (i & 63));
  }
  [[nodiscard]] bool test_bit(Side s, std::uint32_t i) const noexcept {
    return (side_bits(s)[i >> 6] >> (i & 63)) & 1U;
  }

  // Highest set bit at or below `from`, and lowest at or above. These run only
  // when a touch level empties, so the scan cost is amortised across the many
  // events that do not move the touch.
  [[nodiscard]] std::uint32_t scan_down(Side s, std::uint32_t from) const noexcept;
  [[nodiscard]] std::uint32_t scan_up(Side s, std::uint32_t from) const noexcept;

  void unlink(std::uint32_t slot) noexcept;
  void on_qty_removed_at(std::uint32_t level, Side side, std::uint64_t seq, Qty qty) noexcept;
  void drop_own(OrderId id) noexcept;

  Ticks         window_base_;
  std::uint32_t window_;

  std::vector<Level>        bid_, ask_;
  std::vector<std::uint64_t> bid_bits_, ask_bits_;
  std::uint32_t             best_bid_ = kNoLevel;
  std::uint32_t             best_ask_ = kNoLevel;

  std::vector<Order>        pool_;
  std::vector<std::uint32_t> free_;      // free-list of pool slots
  OrderMap                  map_;
  std::vector<OwnOrder>     own_;
  BookStats                 stats_;
};

}  // namespace lob
