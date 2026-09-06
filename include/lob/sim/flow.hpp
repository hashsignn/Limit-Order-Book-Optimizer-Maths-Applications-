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

  // ---- informed and uninformed flow (Glosten & Milgrom 1985) --------------
  //
  // A market maker is paid by traders who trade for reasons unrelated to value
  // and taxed by traders who know something. Both populations have to exist or
  // quoting makes no sense: with no informed flow there is no adverse selection
  // to price, and with nothing but informed flow there is no reason to quote at
  // all.
  //
  // This generator had NEITHER. Its mid stepped on an independent coin flip,
  // uncorrelated with any trade, so a fill carried no information and the price
  // moved for reasons a maker could neither anticipate nor be compensated for.
  // Every strategy lost money, the optimal action was to stop quoting, and
  // Phase 5's acceptance test was therefore unanswerable on it — the best
  // baseline was whichever one traded least.
  //
  // Now a share of aggressive orders are informed: the value moved, and this
  // order is the first sign of it, so the mid FOLLOWS the trade. The rest are
  // uninformed and the mid does not move behind them. A maker keeps the spread
  // from the second group and pays impact to the first, which is the whole
  // economics of the business and the thing that was missing.
  //
  // Not every informed trade moves the price. Information arrives in pieces and
  // the price moves when enough of it accumulates, so an informed order moves
  // the mid a tick only with probability informed_impact_prob.
  //
  // That second parameter is not decoration, it is what makes the market
  // quotable. The volatility a resting quote is exposed to over its lifetime L
  // has to stay under the half-spread it earns, and every source counts:
  //
  //     L * (drift_prob + p_aggress * pi * informed_impact_prob)  <  half_spread^2
  //
  // L is the quote's EXPOSURE lifetime, not the requote interval. A driver that
  // requotes every 200 events still holds a quote whose target has not moved —
  // that hysteresis is the point, since requoting surrenders queue position —
  // so a touch-joining quote here rests about 15 ms, or L = 7500 events at ~2 us
  // apart. Using the requote interval instead put the bound at pi < 0.6 when
  // the measured crossover was below 0.1, which is how the error surfaced.
  //
  // With L = 7500, aggressive orders at ~5.7% of the stream and half_spread of
  // 1 tick, the bound is  pi * informed_impact_prob < 0.0021 once exogenous
  // drift is small. At informed_impact_prob = 0.01 that leaves pi up to ~0.2,
  // which is a realistic informed share and a quotable market.
  //
  // One informed order in a hundred moving the price a tick is also the right
  // order of magnitude for a real book: on the Bitstamp captures the touch
  // moves far more often than trades occur, because most of what moves it is
  // quotes being pulled rather than anything trading.
  //
  // sweep-informed measures the curve rather than trusting this algebra, and
  // it is the thing to re-run if any of these change.
  double        informed_frac        = 0.20;   // pi
  double        informed_impact_prob = 0.01;   // chance an informed order moves the mid
  Ticks         informed_impact      = 1;      // ticks it moves when it does

  // Exogenous news: the mid moving with no trade behind it at all. Real prices
  // do that, so it is kept — but it has a ceiling, and the ceiling is
  // derivable rather than a matter of taste.
  //
  // A resting quote does not move with the mid. Whenever the mid walks away
  // from it the quote is picked off, and CONDITIONAL ON BEING FILLED the walk
  // is adverse — that is a cost with no offsetting revenue, unlike informed
  // flow, which at least pays the spread on the way through. Over a quote's
  // lifetime L events the walk is sqrt(L * drift_prob) ticks, and for a maker
  // to survive it that has to stay under the half-spread it earns:
  //
  //     L * drift_prob  <  half_spread^2
  //
  // Quotes here live about 15 ms, which at ~2 us between events is L = 7500,
  // and half_spread is 1 tick. So L * drift_prob must stay well under 1, and
  // it has to leave room for the informed flow below rather than spending the
  // whole budget itself.
  //
  // It was 0.02 — a 12-tick walk against a 1-tick edge, a hundred and fifty
  // times over. That single number was why every strategy lost money in the
  // Phase 5 evaluation and why the optimal action was to stop quoting.
  double        drift_prob     = 1e-5;
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

    // Impact from the last informed trade lands before this event, so the
    // aggressor that carried the information traded at the OLD price and
    // whoever supplied it is now holding at the new one. That ordering is the
    // adverse selection; reversing it would pay the maker for being run over.
    if (pending_impact_ != 0) {
      mid_ += pending_impact_;
      pending_impact_ = 0;
    }
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
  // How the aggressive flow split, so a run can report the population it was
  // actually drawn against rather than the one that was configured.
  [[nodiscard]] std::uint64_t informed_trades() const noexcept { return informed_; }
  [[nodiscard]] std::uint64_t uninformed_trades() const noexcept { return uninformed_; }

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

    // Informed: the value moved and this order is acting on it, so the mid
    // follows. A Bid aggressor is buying, which lifts the ask and takes the
    // price up.
    if (uniform() < cfg_.informed_frac) {
      ++informed_;
      if (uniform() < cfg_.informed_impact_prob)
        pending_impact_ = (e.side == Side::Bid) ? cfg_.informed_impact : -cfg_.informed_impact;
    } else {
      ++uninformed_;
    }
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
  Ticks             pending_impact_ = 0;
  std::uint64_t     informed_ = 0, uninformed_ = 0;
  bool              has_bid_ = false;
  bool              has_ask_ = false;
  Ticks             best_bid_ = 0;
  Ticks             best_ask_ = 0;
  OrderId           next_id_ = 1;
  SeqNum            seq_     = 1;
  Nanos             ts_      = 0;
};

}  // namespace lob
