#include "lob/book/order_book.hpp"

#include <algorithm>
#include <string>

namespace lob {

OrderBook::OrderBook(Ticks window_base, std::uint32_t window_ticks, std::size_t max_orders)
    : window_base_(window_base),
      window_(window_ticks),
      map_(max_orders) {
  const std::size_t words = (window_ticks + 63) / 64;
  bid_.assign(window_ticks, Level{});
  ask_.assign(window_ticks, Level{});
  bid_bits_.assign(words, 0);
  ask_bits_.assign(words, 0);

  pool_.assign(max_orders, Order{});
  free_.resize(max_orders);
  // Hand out slots front-to-back so early allocations walk memory in order.
  for (std::size_t i = 0; i < max_orders; ++i)
    free_[i] = static_cast<std::uint32_t>(max_orders - 1 - i);

  own_.reserve(64);
}

// ---------------------------------------------------------------- bit scans

std::uint32_t OrderBook::scan_down(Side s, std::uint32_t from) const noexcept {
  if (from == kNoLevel) return kNoLevel;
  const auto& bits = side_bits(s);
  std::size_t w = from >> 6;
  // Mask off everything above `from` in the starting word.
  std::uint64_t word = bits[w] & (~std::uint64_t{0} >> (63 - (from & 63)));
  while (true) {
    if (word != 0)
      return static_cast<std::uint32_t>(w * 64 + (63 - static_cast<std::size_t>(std::countl_zero(word))));
    if (w == 0) return kNoLevel;
    --w;
    word = bits[w];
  }
}

std::uint32_t OrderBook::scan_up(Side s, std::uint32_t from) const noexcept {
  if (from == kNoLevel) return kNoLevel;
  const auto& bits = side_bits(s);
  std::size_t w = from >> 6;
  if (w >= bits.size()) return kNoLevel;
  // Mask off everything below `from` in the starting word.
  std::uint64_t word = bits[w] & (~std::uint64_t{0} << (from & 63));
  while (true) {
    if (word != 0)
      return static_cast<std::uint32_t>(w * 64 + static_cast<std::size_t>(std::countr_zero(word)));
    if (++w >= bits.size()) return kNoLevel;
    word = bits[w];
  }
}

// ------------------------------------------------------------ own-order upkeep

// Called whenever `qty` leaves a level at arrival rank `seq`. Any of our orders
// at that level that sit BEHIND the departing order (higher rank) just moved
// that much closer to the front.
void OrderBook::on_qty_removed_at(std::uint32_t level, Side side,
                                  std::uint64_t seq, Qty qty) noexcept {
  if (own_.empty()) return;
  for (auto& o : own_) {
    if (o.level == level && o.side == side && seq < o.level_seq) {
      o.qty_ahead -= qty;
      if (o.qty_ahead < 0) o.qty_ahead = 0;   // defensive: a feed gap can desync
    }
  }
}

void OrderBook::drop_own(OrderId id) noexcept {
  for (std::size_t i = 0; i < own_.size(); ++i) {
    if (own_[i].id == id) {
      own_[i] = own_.back();
      own_.pop_back();
      return;
    }
  }
}

// ------------------------------------------------------------------- linking

void OrderBook::unlink(std::uint32_t slot) noexcept {
  Order& o = pool_[slot];
  auto& lv = side_levels(o.side)[o.level];
  if (o.prev != kNullOrder) pool_[o.prev].next = o.next;
  else                      lv.head = o.next;
  if (o.next != kNullOrder) pool_[o.next].prev = o.prev;
  else                      lv.tail = o.prev;
  o.prev = o.next = kNullOrder;
  --lv.count;
}

// ----------------------------------------------------------------- operations

BookError OrderBook::add(OrderId id, Side side, Ticks price, Qty qty, bool mine) noexcept {
  if (qty <= 0)          { ++stats_.errors[static_cast<std::size_t>(BookError::BadQuantity)];      return BookError::BadQuantity; }
  if (!in_window(price)) { ++stats_.errors[static_cast<std::size_t>(BookError::PriceOutOfWindow)]; return BookError::PriceOutOfWindow; }
  if (free_.empty())     { ++stats_.errors[static_cast<std::size_t>(BookError::PoolExhausted)];    return BookError::PoolExhausted; }

  // Reject anything that would cross or lock. Checked before any mutation, so a
  // rejection leaves no trace. Two comparisons against cached values — the
  // branch predicts perfectly in the common case, and it is what makes
  // check_invariants()'s non-crossing assertion true by construction rather
  // than by hope.
  if (side == Side::Bid) {
    if (best_ask_ != kNoLevel && price >= to_price(best_ask_))
      { ++stats_.errors[static_cast<std::size_t>(BookError::CrossedBook)]; return BookError::CrossedBook; }
  } else {
    if (best_bid_ != kNoLevel && price <= to_price(best_bid_))
      { ++stats_.errors[static_cast<std::size_t>(BookError::CrossedBook)]; return BookError::CrossedBook; }
  }

  const std::uint32_t idx = to_index(price);
  const std::uint32_t slot = free_.back();

  if (!map_.insert(id, slot)) {
    ++stats_.errors[static_cast<std::size_t>(BookError::DuplicateOrder)];
    return BookError::DuplicateOrder;
  }
  free_.pop_back();

  auto& lv = side_levels(side)[idx];
  Order& o = pool_[slot];
  o.id = id; o.qty = qty; o.level = idx; o.side = side; o.mine = mine;
  o.prev = lv.tail; o.next = kNullOrder;
  o.level_seq = lv.next_seq++;

  // Joins the BACK of the queue. This is price-time priority, and it is why a
  // requote is expensive: the new order starts again behind everything.
  if (lv.tail != kNullOrder) pool_[lv.tail].next = slot;
  else                       lv.head = slot;
  lv.tail = slot;
  ++lv.count;

  const bool was_empty = (lv.total_qty == 0);
  lv.total_qty += qty;

  if (was_empty) {
    set_bit(side, idx);
    if (side == Side::Bid) { if (best_bid_ == kNoLevel || idx > best_bid_) best_bid_ = idx; }
    else                   { if (best_ask_ == kNoLevel || idx < best_ask_) best_ask_ = idx; }
  }

  if (mine) {
    // Everything already resting at this level is ahead of us, by definition.
    own_.push_back(OwnOrder{id, idx, o.level_seq, lv.total_qty - qty, side});
  }

  ++stats_.adds;
  return BookError::Ok;
}

BookError OrderBook::reduce(OrderId id, Qty by) noexcept {
  const std::uint32_t slot = map_.find(id);
  if (slot == OrderMap::kEmpty) { ++stats_.errors[static_cast<std::size_t>(BookError::UnknownOrder)]; return BookError::UnknownOrder; }
  Order& o = pool_[slot];
  if (by <= 0 || by > o.qty)    { ++stats_.errors[static_cast<std::size_t>(BookError::BadQuantity)];  return BookError::BadQuantity; }

  if (by == o.qty) return remove(id);   // a reduce to zero is a delete

  auto& lv = side_levels(o.side)[o.level];
  o.qty -= by;
  lv.total_qty -= by;
  // A partial cancel does NOT lose priority — the order keeps its rank, so
  // orders behind it gain nothing.
  on_qty_removed_at(o.level, o.side, o.level_seq, by);
  for (auto& own : own_) if (own.id == id) break;   // our own qty_ahead is unchanged

  ++stats_.reduces;
  return BookError::Ok;
}

BookError OrderBook::remove(OrderId id) noexcept {
  const std::uint32_t slot = map_.find(id);
  if (slot == OrderMap::kEmpty) { ++stats_.errors[static_cast<std::size_t>(BookError::UnknownOrder)]; return BookError::UnknownOrder; }

  Order& o = pool_[slot];
  const std::uint32_t idx  = o.level;
  const Side          side = o.side;
  const Qty           qty  = o.qty;
  const std::uint64_t seq  = o.level_seq;

  unlink(slot);
  auto& lv = side_levels(side)[idx];
  lv.total_qty -= qty;

  on_qty_removed_at(idx, side, seq, qty);
  if (o.mine) drop_own(id);

  if (lv.total_qty == 0) {
    lv.head = lv.tail = kNullOrder;
    lv.count = 0;
    clear_bit(side, idx);
    // Only rescan when the level that emptied was the touch.
    if (side == Side::Bid && idx == best_bid_) best_bid_ = idx == 0 ? kNoLevel : scan_down(side, idx - 1);
    if (side == Side::Ask && idx == best_ask_) best_ask_ = scan_up(side, idx + 1);
  }

  map_.erase(id);
  free_.push_back(slot);
  ++stats_.deletes;
  return BookError::Ok;
}

BookError OrderBook::execute(OrderId id, Qty qty) noexcept {
  const std::uint32_t slot = map_.find(id);
  if (slot == OrderMap::kEmpty) { ++stats_.errors[static_cast<std::size_t>(BookError::UnknownOrder)]; return BookError::UnknownOrder; }
  Order& o = pool_[slot];
  if (qty <= 0 || qty > o.qty)  { ++stats_.errors[static_cast<std::size_t>(BookError::BadQuantity)];  return BookError::BadQuantity; }

  ++stats_.executes;
  if (qty == o.qty) {
    const BookError r = remove(id);
    --stats_.deletes;   // counted as an execute, not a cancel
    return r;
  }

  auto& lv = side_levels(o.side)[o.level];
  o.qty -= qty;
  lv.total_qty -= qty;
  on_qty_removed_at(o.level, o.side, o.level_seq, qty);
  return BookError::Ok;
}

BookError OrderBook::replace(OrderId old_id, OrderId new_id, Ticks price, Qty qty) noexcept {
  const std::uint32_t slot = map_.find(old_id);
  if (slot == OrderMap::kEmpty) { ++stats_.errors[static_cast<std::size_t>(BookError::UnknownOrder)]; return BookError::UnknownOrder; }
  const Side side = pool_[slot].side;
  const bool mine = pool_[slot].mine;

  const BookError r = remove(old_id);
  if (r != BookError::Ok) return r;
  --stats_.deletes;

  // Deliberately a fresh add: the replaced order goes to the BACK of the queue.
  // Losing priority is the real cost of a requote, and the book must model it.
  // If the add is rejected the old order is already gone. That is the right
  // outcome for a feed consumer: a Replace means the exchange ALREADY did this,
  // so the order is genuinely no longer resting. The book stays consistent and
  // the caller gets the error.
  const BookError a = add(new_id, side, price, qty, mine);
  if (a != BookError::Ok) return a;
  --stats_.adds;

  ++stats_.replaces;
  return BookError::Ok;
}

void OrderBook::clear() noexcept {
  std::fill(bid_.begin(), bid_.end(), Level{});
  std::fill(ask_.begin(), ask_.end(), Level{});
  std::fill(bid_bits_.begin(), bid_bits_.end(), 0);
  std::fill(ask_bits_.begin(), ask_bits_.end(), 0);
  best_bid_ = best_ask_ = kNoLevel;
  map_.clear();
  own_.clear();
  free_.resize(pool_.size());
  for (std::size_t i = 0; i < pool_.size(); ++i)
    free_[i] = static_cast<std::uint32_t>(pool_.size() - 1 - i);
}

BookError OrderBook::apply(const BookEvent& e) noexcept {
  ++stats_.events;
  switch (e.type) {
    case EventType::Add:     return add(e.order_id, e.side, e.price, e.qty);
    case EventType::Reduce:  return reduce(e.order_id, e.qty);
    case EventType::Delete:  return remove(e.order_id);
    case EventType::Execute: return execute(e.order_id, e.qty);
    case EventType::Replace: return replace(e.order_id, e.new_id, e.price, e.qty);
    case EventType::Clear:   clear(); return BookError::Ok;
    // Routed through the matching engine by the simulator, not applied here.
    case EventType::Aggress: return BookError::Ok;
    case EventType::Count:   break;
  }
  return BookError::Ok;
}

// -------------------------------------------------------------------- queries

Qty OrderBook::qty_at(Side s, Ticks price) const noexcept {
  if (!in_window(price)) return 0;
  return side_levels(s)[to_index(price)].total_qty;
}

Qty OrderBook::qty_of(OrderId id) const noexcept {
  const std::uint32_t slot = map_.find(id);
  return slot == OrderMap::kEmpty ? 0 : pool_[slot].qty;
}

OrderId OrderBook::front_order_at(Side s, Ticks price) const noexcept {
  if (!in_window(price)) return 0;
  const Level& lv = side_levels(s)[to_index(price)];
  return lv.head == kNullOrder ? 0 : pool_[lv.head].id;
}

std::uint32_t OrderBook::orders_at(Side s, Ticks price) const noexcept {
  if (!in_window(price)) return 0;
  return side_levels(s)[to_index(price)].count;
}

std::uint32_t OrderBook::depth(Side s, std::uint32_t n, Ticks* prices, Qty* qtys) const noexcept {
  const auto& lv = side_levels(s);
  std::uint32_t idx = (s == Side::Bid) ? best_bid_ : best_ask_;
  std::uint32_t out = 0;
  while (out < n && idx != kNoLevel) {
    prices[out] = to_price(idx);
    qtys[out]   = lv[idx].total_qty;
    ++out;
    if (s == Side::Bid) idx = (idx == 0) ? kNoLevel : scan_down(s, idx - 1);
    else                idx = scan_up(s, idx + 1);
  }
  return out;
}

Qty OrderBook::queue_ahead(OrderId id) const noexcept {
  for (const auto& o : own_) if (o.id == id) return o.qty_ahead;
  return -1;   // not one of ours
}

bool OrderBook::is_mine(OrderId id) const noexcept {
  const std::uint32_t slot = map_.find(id);
  return slot != OrderMap::kEmpty && pool_[slot].mine;
}

// ----------------------------------------------------------------- invariants

bool OrderBook::check_invariants(std::string* why) const {
  auto fail = [&](const std::string& msg) { if (why) *why = msg; return false; };

  if (has_bid() && has_ask() && best_bid() >= best_ask())
    return fail("crossed book: bid " + std::to_string(best_bid()) + " >= ask " + std::to_string(best_ask()));

  std::size_t counted = 0;
  for (int si = 0; si < 2; ++si) {
    const Side s = static_cast<Side>(si);
    const auto& lv = side_levels(s);
    for (std::uint32_t i = 0; i < window_; ++i) {
      const Level& L = lv[i];

      // The bitset must agree with emptiness, or scans will find ghost levels.
      if ((L.total_qty != 0) != test_bit(s, i))
        return fail("bitset mismatch at level " + std::to_string(i) +
                    " qty=" + std::to_string(L.total_qty));

      if (L.total_qty == 0) {
        if (L.head != kNullOrder || L.tail != kNullOrder || L.count != 0)
          return fail("empty level " + std::to_string(i) + " still has a queue");
        continue;
      }

      // Walk the FIFO: totals, count, link symmetry and strictly increasing rank.
      Qty sum = 0; std::uint32_t n = 0; std::uint32_t prev = kNullOrder;
      std::uint64_t last_seq = 0; bool first = true;
      for (std::uint32_t cur = L.head; cur != kNullOrder; cur = pool_[cur].next) {
        const Order& o = pool_[cur];
        if (o.level != i || o.side != s) return fail("order on the wrong level");
        if (o.prev != prev)              return fail("broken prev link at level " + std::to_string(i));
        if (o.qty <= 0)                  return fail("non-positive resting qty");
        if (!first && o.level_seq <= last_seq)
          return fail("FIFO rank not increasing at level " + std::to_string(i));
        if (map_.find(o.id) == OrderMap::kEmpty)
          return fail("resting order " + std::to_string(o.id) + " missing from the map");
        last_seq = o.level_seq; first = false;
        sum += o.qty; ++n; prev = cur;
        if (n > L.count + 1) return fail("FIFO longer than count at level " + std::to_string(i));
      }
      if (prev != L.tail)   return fail("tail does not match last node at level " + std::to_string(i));
      if (sum != L.total_qty)
        return fail("level " + std::to_string(i) + " total_qty " + std::to_string(L.total_qty) +
                    " != sum " + std::to_string(sum));
      if (n != L.count)     return fail("level count mismatch at " + std::to_string(i));
      counted += n;
    }
  }

  if (counted != map_.size())
    return fail("reachable orders " + std::to_string(counted) +
                " != map size " + std::to_string(map_.size()));

  // Cached touch must equal what a full scan would find.
  const std::uint32_t true_bid = window_ == 0 ? kNoLevel : scan_down(Side::Bid, window_ - 1);
  const std::uint32_t true_ask = scan_up(Side::Ask, 0);
  if (best_bid_ != true_bid) return fail("stale best_bid cache");
  if (best_ask_ != true_ask) return fail("stale best_ask cache");

  // Own-order queue position must match a from-scratch walk of its level.
  for (const auto& own : own_) {
    const auto& L = side_levels(own.side)[own.level];
    Qty ahead = 0; bool found = false;
    for (std::uint32_t cur = L.head; cur != kNullOrder; cur = pool_[cur].next) {
      const Order& o = pool_[cur];
      if (o.id == own.id) { found = true; break; }
      if (o.level_seq < own.level_seq) ahead += o.qty;
    }
    if (!found) return fail("own order " + std::to_string(own.id) + " not in its level");
    if (ahead != own.qty_ahead)
      return fail("queue_ahead drift for " + std::to_string(own.id) + ": tracked " +
                  std::to_string(own.qty_ahead) + " actual " + std::to_string(ahead));
  }
  return true;
}

}  // namespace lob
