// Deliberately naive order book.
//
// std::map of price -> std::list of orders. Slow, obvious, and easy to read as
// correct. Its only job is to be a second opinion: the fast book and this one
// are driven with the same event stream and their observable state compared
// after every event. That differential test finds essentially all book bugs,
// including the ones a hand-written unit test would never think to construct.
//
// If this file ever gets clever, it has stopped doing its job.
//
// It must also never crash or throw. find_entry() can return nullptr when the
// index and a price queue disagree, and every caller used to dereference it
// unchecked; the at() lookups would have thrown on the same disagreement. That
// cannot happen while the class is self-consistent -- but a segfault in the
// ORACLE looks exactly like a bug in the thing under test, which is the one
// failure mode a differential test cannot afford.
//
// TWO DELIBERATE ASYMMETRIES with the fast book, which bound what a comparison
// against it proves: this book has no price window and no order limit, so it
// accepts what OrderBook rejects as PriceOutOfWindow and PoolExhausted. A
// differential test staying inside the window and under capacity sees no
// difference; one that strays sees a disagreement that is correct rather than a
// defect. tests/test_book_differential.cpp compares the returned error codes
// first, so it reports such a case rather than hiding it.
#pragma once

#include <cstdint>
#include <list>
#include <utility>
#include <vector>
#include <map>
#include <unordered_map>

#include "lob/book/events.hpp"
#include "lob/core/types.hpp"

namespace lob {

class ReferenceBook {
 public:
  struct Entry { OrderId id; Qty qty; bool mine; };
  using Queue = std::list<Entry>;

  BookError add(OrderId id, Side side, Ticks price, Qty qty, bool mine = false) {
    if (qty <= 0) return BookError::BadQuantity;
    if (index_.count(id)) return BookError::DuplicateOrder;
    if (side == Side::Bid) { if (has_ask() && price >= best_ask()) return BookError::CrossedBook; }
    else                   { if (has_bid() && price <= best_bid()) return BookError::CrossedBook; }
    auto& book = (side == Side::Bid) ? bids_ : asks_;
    book[price].push_back(Entry{id, qty, mine});
    index_[id] = Loc{side, price};
    return BookError::Ok;
  }

  BookError reduce(OrderId id, Qty by) {
    auto it = index_.find(id);
    if (it == index_.end()) return BookError::UnknownOrder;
    Entry* e = find_entry(it->second, id);
    if (e == nullptr) return BookError::UnknownOrder;   // index and queue disagree
    if (by <= 0 || by > e->qty) return BookError::BadQuantity;
    if (by == e->qty) return remove(id);
    e->qty -= by;
    return BookError::Ok;
  }

  BookError remove(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return BookError::UnknownOrder;
    auto& book = (it->second.side == Side::Bid) ? bids_ : asks_;
    auto lit = book.find(it->second.price);
    if (lit == book.end()) { index_.erase(it); return BookError::UnknownOrder; }
    auto& q = lit->second;
    for (auto e = q.begin(); e != q.end(); ++e) {
      if (e->id == id) { q.erase(e); break; }
    }
    if (q.empty()) book.erase(lit);
    index_.erase(it);
    return BookError::Ok;
  }

  BookError execute(OrderId id, Qty qty) {
    auto it = index_.find(id);
    if (it == index_.end()) return BookError::UnknownOrder;
    Entry* e = find_entry(it->second, id);
    if (e == nullptr) return BookError::UnknownOrder;
    if (qty <= 0 || qty > e->qty) return BookError::BadQuantity;
    if (qty == e->qty) return remove(id);
    e->qty -= qty;
    return BookError::Ok;
  }

  BookError replace(OrderId old_id, OrderId new_id, Ticks price, Qty qty) {
    auto it = index_.find(old_id);
    if (it == index_.end()) return BookError::UnknownOrder;
    const Side side = it->second.side;
    const Entry* old = find_entry(it->second, old_id);
    if (old == nullptr) return BookError::UnknownOrder;
    const bool mine = old->mine;
    const BookError r = remove(old_id);
    if (r != BookError::Ok) return r;
    return add(new_id, side, price, qty, mine);
  }

  void clear() { bids_.clear(); asks_.clear(); index_.clear(); }

  BookError apply(const BookEvent& e) {
    switch (e.type) {
      case EventType::Add:     return add(e.order_id, e.side, e.price, e.qty);
      case EventType::Reduce:  return reduce(e.order_id, e.qty);
      case EventType::Delete:  return remove(e.order_id);
      case EventType::Execute: return execute(e.order_id, e.qty);
      case EventType::Replace: return replace(e.order_id, e.new_id, e.price, e.qty);
      case EventType::Clear:   clear(); return BookError::Ok;
      case EventType::Aggress: return BookError::Ok;
      case EventType::Count:   break;
    }
    return BookError::Ok;
  }

  [[nodiscard]] bool  has_bid() const { return !bids_.empty(); }
  [[nodiscard]] bool  has_ask() const { return !asks_.empty(); }
  [[nodiscard]] Ticks best_bid() const { return bids_.empty() ? 0 : bids_.rbegin()->first; }
  [[nodiscard]] Ticks best_ask() const { return asks_.empty() ? 0 : asks_.begin()->first; }

  [[nodiscard]] Qty qty_at(Side s, Ticks price) const {
    const auto& book = (s == Side::Bid) ? bids_ : asks_;
    auto it = book.find(price);
    if (it == book.end()) return 0;
    Qty sum = 0;
    for (const auto& e : it->second) sum += e.qty;
    return sum;
  }

  [[nodiscard]] std::size_t orders_at(Side s, Ticks price) const {
    const auto& book = (s == Side::Bid) ? bids_ : asks_;
    auto it = book.find(price);
    return it == book.end() ? 0 : it->second.size();
  }

  // Volume ahead of `id` in its own queue, computed the obvious way.
  [[nodiscard]] Qty queue_ahead(OrderId id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return -1;
    const auto& book = (it->second.side == Side::Bid) ? bids_ : asks_;
    const auto& q = book.at(it->second.price);
    Qty ahead = 0;
    for (const auto& e : q) {
      if (e.id == id) return ahead;
      ahead += e.qty;
    }
    return -1;
  }

  [[nodiscard]] std::size_t live_orders() const { return index_.size(); }

  // Remaining size of a resting order, or 0 if it is no longer in the book.
  [[nodiscard]] Qty qty_of(OrderId id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return 0;
    const auto& book = (it->second.side == Side::Bid) ? bids_ : asks_;
    for (const auto& e : book.at(it->second.price)) if (e.id == id) return e.qty;
    return 0;
  }

  // Levels from the touch outward, for comparing depth ladders.
  [[nodiscard]] std::vector<std::pair<Ticks, Qty>> ladder(Side s, std::size_t n) const {
    std::vector<std::pair<Ticks, Qty>> out;
    if (s == Side::Bid) {
      for (auto it = bids_.rbegin(); it != bids_.rend() && out.size() < n; ++it)
        out.emplace_back(it->first, sum_of(it->second));
    } else {
      for (auto it = asks_.begin(); it != asks_.end() && out.size() < n; ++it)
        out.emplace_back(it->first, sum_of(it->second));
    }
    return out;
  }

 private:
  struct Loc { Side side; Ticks price; };

  static Qty sum_of(const Queue& q) {
    Qty s = 0;
    for (const auto& e : q) s += e.qty;
    return s;
  }

  Entry* find_entry(const Loc& loc, OrderId id) {
    auto& book = (loc.side == Side::Bid) ? bids_ : asks_;
    for (auto& e : book.at(loc.price)) if (e.id == id) return &e;
    return nullptr;
  }

  std::map<Ticks, Queue>            bids_, asks_;
  std::unordered_map<OrderId, Loc>  index_;
};

}  // namespace lob
