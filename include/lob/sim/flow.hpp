// Synthetic order flow.
//
// Two jobs, and it is worth being clear which is which:
//
//  1. NOW (Phase 1): drive the book hard enough to prove it correct. For that,
//     realism is beside the point — what matters is coverage. The generator is
//     tuned to produce adds, partial cancels, full cancels, partial and full
//     fills, replaces, empty levels, level clears and touch moves in quick
//     succession, including the awkward orderings a real feed produces rarely.
//
//  2. LATER (Phase 3): the same interface, backed by a calibrated queue-reactive
//     or Hawkes model, becomes the simulator. This is a zero-intelligence
//     Poisson generator — the baseline the roadmap says a real simulator must
//     beat, and the one Smith, Farmer, Gillemot & Krishnamurthy (2003) analyse.
//
// It does NOT reproduce the stylized facts in docs/03-metrics-and-estimators.md
// §11, and nothing calibrated should be fitted to it.
#pragma once

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "lob/book/events.hpp"
#include "lob/core/types.hpp"

namespace lob {

struct FlowConfig {
  Ticks         mid            = 10'000;   // starting mid, in ticks
  Ticks         half_spread    = 1;
  std::uint32_t levels         = 10;       // how far from the touch orders land
  Qty           min_qty        = 1;
  Qty           max_qty        = 500;
  // Relative weights. Cancels dominate real order flow; executes are rarer.
  double        w_add          = 0.50;
  double        w_delete       = 0.28;
  double        w_reduce       = 0.09;
  double        w_execute      = 0.09;
  double        w_replace      = 0.04;
  // Aggressive orders that cross the spread. Without these nothing ever trades
  // against a resting quote, so the fill model is never exercised — which is
  // the one thing a market-making simulator has to get right.
  double        w_aggress      = 0.06;
  Qty           aggress_max    = 400;
  double        drift_prob     = 0.02;     // chance the mid steps a tick
  // Adds outnumber removals in these weights, so without a brake the book grows
  // without bound and every benchmark ends up measuring an absurdly deep queue.
  // Past this many resting orders, adds are suppressed and the book holds
  // roughly stationary — which is what a real venue's book does intraday.
  std::size_t   target_live    = 20'000;
  std::uint64_t seed           = 20260904;
};

// Emits a stream that is always *structurally* valid: it only cancels or fills
// orders it knows are resting, and never quotes a bid through an ask. A feed
// with genuine gaps is a separate test case, driven by corrupting this output.
class FlowGenerator {
 public:
  explicit FlowGenerator(FlowConfig cfg = {})
      : cfg_(cfg), rng_(cfg.seed), mid_(cfg.mid) {}

  [[nodiscard]] BookEvent next() noexcept {
    BookEvent e{};
    e.ts  = ts_;
    e.seq = seq_++;
    ts_  += 1 + static_cast<Nanos>(rng_() % 5000);   // ~µs-scale gaps

    if (uniform() < cfg_.drift_prob) drift();

    // Nothing resting yet: only Add is legal.
    const bool can_touch_existing = !live_.empty();
    // Suppress adds once the book is at its target size, so removals catch up.
    const double w_add = live_.size() >= cfg_.target_live ? cfg_.w_add * 0.15 : cfg_.w_add;
    const double total = w_add + cfg_.w_delete + cfg_.w_reduce + cfg_.w_execute
                       + cfg_.w_replace + cfg_.w_aggress;
    const double r = uniform() * total;

    double acc = w_add;
    if (r < acc || !can_touch_existing) return make_add(e);
    acc += cfg_.w_delete;
    if (r < acc) return make_on_existing(e, EventType::Delete);
    acc += cfg_.w_reduce;
    if (r < acc) return make_on_existing(e, EventType::Reduce);
    acc += cfg_.w_execute;
    if (r < acc) return make_on_existing(e, EventType::Execute);
    acc += cfg_.w_replace;
    if (r < acc) return make_on_existing(e, EventType::Replace);
    return make_aggress(e);
  }

  // The generator tracks what it believes is resting so it can emit valid
  // references. Call after applying, to keep belief and book in step.
  void on_applied(const BookEvent& e, Qty resting_after) noexcept {
    switch (e.type) {
      case EventType::Add:
        remember(Live{e.order_id, e.qty, e.price, e.side});
        break;
      case EventType::Delete:
        forget(e.order_id);
        break;
      case EventType::Reduce:
      case EventType::Execute: {
        if (resting_after <= 0) { forget(e.order_id); break; }
        auto it = at_.find(e.order_id);
        if (it != at_.end()) live_[it->second].qty = resting_after;
        break;
      }
      case EventType::Replace:
        forget(e.order_id);
        remember(Live{e.new_id, e.qty, e.price, e.side});
        break;
      case EventType::Clear:
        live_.clear();
        at_.clear();
        break;
      case EventType::Aggress:
        // The matcher decides what this consumed; the caller resyncs via
        // forget_order() for each order it removed.
        break;
      case EventType::Count: break;
    }
  }

  // The generator is told the current touch after each event. A real venue
  // never *books* a crossing order — it matches it — so an MBO feed contains no
  // crossing adds. Without this the drifting mid eventually quotes bids through
  // resting asks and produces a stream no exchange would ever emit.
  void observe(bool has_bid, Ticks bid, bool has_ask, Ticks ask) noexcept {
    has_bid_ = has_bid; best_bid_ = bid;
    has_ask_ = has_ask; best_ask_ = ask;
  }

  // Told by the caller when a match removed a resting order the generator still
  // believed was live. Without this the generator emits references to orders
  // that an aggressive order already consumed.
  void forget_order(OrderId id) { forget(id); }

  [[nodiscard]] std::size_t believed_live() const noexcept { return live_.size(); }
  [[nodiscard]] Ticks mid() const noexcept { return mid_; }

 private:
  struct Live { OrderId id; Qty qty; Ticks price; Side side; };

  [[nodiscard]] double uniform() noexcept {
    return static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0);
  }
  [[nodiscard]] Qty rand_qty() noexcept {
    return cfg_.min_qty + static_cast<Qty>(rng_() % static_cast<std::uint64_t>(cfg_.max_qty - cfg_.min_qty + 1));
  }

  void drift() noexcept { mid_ += (rng_() & 1) ? 1 : -1; }

  // Prices are drawn from the touch outward, which keeps the book shaped
  // roughly like a real one and, more importantly for testing, keeps levels
  // shallow enough that they empty and re-fill constantly.
  [[nodiscard]] Ticks price_for(Side s) noexcept {
    const auto away = static_cast<Ticks>(rng_() % cfg_.levels);
    Ticks p = (s == Side::Bid) ? mid_ - cfg_.half_spread - away
                               : mid_ + cfg_.half_spread + away;
    // Clamp to the near side of the touch, so the stream stays non-crossing
    // even as the mid drifts away from where the resting orders sit.
    if (s == Side::Bid && has_ask_ && p >= best_ask_) p = best_ask_ - 1;
    if (s == Side::Ask && has_bid_ && p <= best_bid_) p = best_bid_ + 1;
    return p;
  }

  BookEvent make_add(BookEvent e) noexcept {
    e.type     = EventType::Add;
    e.side     = (rng_() & 1) ? Side::Bid : Side::Ask;
    e.price    = price_for(e.side);
    e.qty      = rand_qty();
    e.order_id = next_id_++;
    return e;
  }

  // A market order sweeping the opposite side. Size is drawn small most of the
  // time and occasionally large, so both "nibbles the front of the queue" and
  // "clears the level" are exercised.
  BookEvent make_aggress(BookEvent e) noexcept {
    e.type     = EventType::Aggress;
    e.side     = (rng_() & 1) ? Side::Bid : Side::Ask;
    e.order_id = next_id_++;
    e.price    = 0;
    const bool big = (rng_() % 10) == 0;
    e.qty = big ? 1 + static_cast<Qty>(rng_() % static_cast<std::uint64_t>(cfg_.aggress_max))
                : 1 + static_cast<Qty>(rng_() % 40);
    return e;
  }

  BookEvent make_on_existing(BookEvent e, EventType t) noexcept {
    const auto& l = live_[rng_() % live_.size()];
    e.type     = t;
    e.order_id = l.id;
    e.side     = l.side;
    e.price    = l.price;
    switch (t) {
      case EventType::Delete:
        e.qty = 0;
        break;
      case EventType::Reduce:
      case EventType::Execute:
        // Half the time take the whole thing, so full-fill and reduce-to-zero
        // paths get exercised as often as the partial ones.
        e.qty = (rng_() & 1) ? l.qty : 1 + static_cast<Qty>(rng_() % static_cast<std::uint64_t>(l.qty));
        break;
      case EventType::Replace:
        e.new_id = next_id_++;
        e.price  = price_for(l.side);
        e.qty    = rand_qty();
        break;
      default: break;
    }
    return e;
  }

  void remember(const Live& l) {
    at_[l.id] = live_.size();
    live_.push_back(l);
  }

  // Swap-and-pop, with the index of the moved element repaired. O(1) — a linear
  // scan here costs more than the book operation it is bookkeeping for.
  void forget(OrderId id) {
    auto it = at_.find(id);
    if (it == at_.end()) return;
    const std::size_t i = it->second;
    live_[i] = live_.back();
    at_[live_[i].id] = i;
    live_.pop_back();
    at_.erase(id);
  }

  FlowConfig        cfg_;
  std::mt19937_64   rng_;
  std::vector<Live>                       live_;
  std::unordered_map<OrderId, std::size_t> at_;   // id -> index into live_
  Ticks             mid_     = 0;
  bool              has_bid_ = false;
  bool              has_ask_ = false;
  Ticks             best_bid_ = 0;
  Ticks             best_ask_ = 0;
  OrderId           next_id_ = 1;
  SeqNum            seq_     = 1;
  Nanos             ts_      = 0;
};

}  // namespace lob
