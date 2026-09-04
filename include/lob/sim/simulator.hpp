// Discrete-event simulator.
//
// The point of this file is one thing: THE AGENT SEES A STALE BOOK. Market data
// takes time to reach it, and its orders take time to reach the exchange. In
// between, the world carries on. A simulator without that gap optimises a
// strategy that cannot exist.
//
// Two books are maintained deliberately:
//
//   true_book   what the exchange actually holds. Matching happens here.
//   view_book   what the agent knows, lagging by the inbound latency.
//
// The gap between them is the cost of being slow, and it is the reason a cancel
// can be too late and a quote can join a queue you did not intend.
//
// The loop is strictly ordered so a run is reproducible: same seed, same
// events, same fills, byte for byte. Without that the journal cannot replay and
// the whole determinism argument in docs/00 collapses.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "lob/book/order_book.hpp"
#include "lob/feat/features.hpp"
#include "lob/sim/flow.hpp"
#include "lob/sim/latency.hpp"
#include "lob/sim/matching.hpp"

namespace lob {

struct SimConfig {
  Ticks         window_base = 5'000;
  std::uint32_t window      = 10'240;
  std::size_t   max_orders  = 1 << 18;
  bool          use_latency = true;    // false collapses both books into one
  FlowConfig    flow{};
  LatencyConfig latency{};
};

struct SimStats {
  std::uint64_t market_events   = 0;
  std::uint64_t agent_actions   = 0;
  std::uint64_t actions_applied = 0;
  std::uint64_t our_fills       = 0;
  Qty           our_filled_qty  = 0;
  // Passive: our resting quote was hit — we earned the spread.
  // Aggressive: our order crossed on arrival — we paid it. With latency a quote
  // decided on a stale book can land crossing, which is how being slow turns
  // liquidity provision into liquidity taking without the strategy asking.
  std::uint64_t passive_fills   = 0;
  Qty           passive_qty     = 0;
  std::uint64_t aggressive_fills = 0;
  Qty           aggressive_qty   = 0;
  std::uint64_t late_cancels    = 0;   // cancel arrived after the order was gone
  double        realised_pnl    = 0.0; // in ticks * shares
  std::int64_t  inventory       = 0;
};

// The agent's read-only view at decision time. Deliberately not the true book:
// handing an agent the real state is the bug this class exists to prevent.
struct AgentView {
  const OrderBook& book;
  const Features&  features;
  Nanos            now;
  std::int64_t     inventory;
};

class Simulator {
 public:
  explicit Simulator(SimConfig cfg = {})
      : cfg_(cfg),
        true_book_(cfg.window_base, cfg.window, cfg.max_orders),
        view_book_(cfg.window_base, cfg.window, cfg.max_orders),
        match_(true_book_),
        gen_(cfg.flow),
        lat_(cfg.latency) {}

  // Agent is any callable: void(const AgentView&, Simulator&).
  template <typename Agent>
  void run(Agent&& agent, int n_events) {
    for (int i = 0; i < n_events; ++i) {
      const BookEvent e = gen_.next();
      now_ = e.ts;

      // 1. Our in-flight orders land first. They were decided before this
      //    event, so they reach the exchange before it does.
      drain_actions(now_);

      // 2. Market data catches up to what the agent is allowed to know.
      drain_market_data(now_);

      // 3. The exchange handles the event. An aggressive order goes through the
      //    matcher, where it consumes the book front-first — including our
      //    resting quotes. That path is the fill model, and without it a
      //    market-making simulator never fills anything passively.
      if (e.type == EventType::Aggress) {
        const std::size_t before = match_.fills().size();
        (void)match_.submit_market(now_, e.order_id, e.side, e.qty, /*mine=*/false);
        for (std::size_t k = before; k < match_.fills().size(); ++k) {
          const Fill& f = match_.fills()[k];
          book_fill(f);
          // Resync the generator: it still believes any fully consumed order is
          // resting, and would go on referencing it.
          if (true_book_.qty_of(f.resting_id) == 0) gen_.forget_order(f.resting_id);
        }
      } else {
        (void)true_book_.apply(e);
      }
      ++stats_.market_events;
      gen_.on_applied(e, true_book_.qty_of(e.order_id));
      gen_.observe(true_book_.has_bid(), true_book_.best_bid(),
                   true_book_.has_ask(), true_book_.best_ask());

      // 4. Schedule the agent's copy. An aggressive order reaches the agent as
      //    the trades it caused, which is exactly what a real feed publishes —
      //    the exchange sends executions, never the incoming order itself.
      if (e.type == EventType::Aggress) {
        for (std::size_t k = fills_seen_; k < match_.fills().size(); ++k) {
          const Fill& f = match_.fills()[k];
          BookEvent ex{};
          ex.ts = e.ts; ex.seq = e.seq; ex.type = EventType::Execute;
          ex.order_id = f.resting_id; ex.side = f.resting_side; ex.qty = f.qty;
          deliver(ex, now_);
        }
        fills_seen_ = match_.fills().size();
      } else {
        deliver(e, now_);
      }

      // 5. The agent decides, on what it currently knows.
      agent(AgentView{view_book_, view_fe_.get(), now_, stats_.inventory}, *this);
    }
    // Let anything still in flight land, so the run ends in a defined state.
    drain_actions(now_ + 10'000'000'000LL);
  }

  // ---- the agent's only way to act. Everything here is delayed. ----
  void send_limit(OrderId id, Side s, Ticks px, Qty q) {
    queue(Action{now_, arrival(), ActionType::Limit, id, s, px, q});
  }
  void send_market(OrderId id, Side s, Qty q) {
    queue(Action{now_, arrival(), ActionType::Market, id, s, 0, q});
  }
  void send_cancel(OrderId id) {
    queue(Action{now_, arrival(), ActionType::Cancel, id, Side::Bid, 0, 0});
  }

  [[nodiscard]] const SimStats&  stats() const noexcept { return stats_; }
  [[nodiscard]] const OrderBook& true_book() const noexcept { return true_book_; }
  [[nodiscard]] const std::vector<Fill>& fills() const noexcept { return match_.fills(); }
  [[nodiscard]] Nanos now() const noexcept { return now_; }
  // How stale the agent's picture is right now, in ticks of mid difference.
  [[nodiscard]] double view_staleness_ticks() const noexcept {
    if (!true_book_.has_bid() || !true_book_.has_ask()) return 0.0;
    if (!view_book_.has_bid() || !view_book_.has_ask()) return 0.0;
    const double t = 0.5 * static_cast<double>(true_book_.best_bid() + true_book_.best_ask());
    const double v = 0.5 * static_cast<double>(view_book_.best_bid() + view_book_.best_ask());
    return t - v;
  }

 private:
  struct Delayed { Nanos arrive; BookEvent e; };

  [[nodiscard]] Nanos arrival() { return cfg_.use_latency ? now_ + lat_.outbound() : now_; }

  void queue(const Action& a) {
    ++stats_.agent_actions;
    if (cfg_.use_latency) inflight_.push(a);
    else                  apply_action(a);
  }

  void drain_actions(Nanos t) {
    while (inflight_.has_arrived_by(t)) {
      const Action a = inflight_.next();
      inflight_.pop();
      apply_action(a);
    }
  }

  void apply_action(const Action& a) {
    ++stats_.actions_applied;
    const std::size_t before = match_.fills().size();
    switch (a.type) {
      case ActionType::Limit:
        (void)match_.submit_limit(now_, a.id, a.side, a.price, a.qty, /*mine=*/true);
        break;
      case ActionType::Market:
        (void)match_.submit_market(now_, a.id, a.side, a.qty, /*mine=*/true);
        break;
      case ActionType::Cancel:
        // The order may already have traded while the cancel was in flight.
        // That is the whole point of modelling latency, so it is counted.
        if (match_.cancel(a.id) != BookError::Ok) ++stats_.late_cancels;
        break;
    }
    for (std::size_t i = before; i < match_.fills().size(); ++i) book_fill(match_.fills()[i]);
  }

  void book_fill(const Fill& f) {
    const bool ours = f.resting_mine || f.aggressor_mine;
    if (!ours) return;
    ++stats_.our_fills;
    stats_.our_filled_qty += f.qty;
    if (f.resting_mine) { ++stats_.passive_fills;    stats_.passive_qty    += f.qty; }
    else                { ++stats_.aggressive_fills; stats_.aggressive_qty += f.qty; }
    // Our side of the trade: if our resting order was hit, we traded on the
    // resting side; if we were the aggressor, on the opposite side.
    const Side our_side = f.resting_mine ? f.resting_side : opposite(f.resting_side);
    const std::int64_t signed_qty = (our_side == Side::Bid ? 1 : -1) * f.qty;
    stats_.inventory   += signed_qty;
    stats_.realised_pnl -= static_cast<double>(signed_qty) * static_cast<double>(f.price);
  }

  void deliver(const BookEvent& e, Nanos t) {
    if (cfg_.use_latency) {
      pending_md_.push_back(Delayed{t + lat_.inbound(), e});
    } else {
      (void)view_book_.apply(e);
      view_fe_.update(view_book_, t);
    }
  }

  void drain_market_data(Nanos t) {
    while (!pending_md_.empty() && pending_md_.front().arrive <= t) {
      (void)view_book_.apply(pending_md_.front().e);
      view_fe_.update(view_book_, pending_md_.front().arrive);
      pending_md_.pop_front();
    }
  }

  SimConfig          cfg_;
  OrderBook          true_book_;
  OrderBook          view_book_;
  MatchingEngine     match_;
  FlowGenerator      gen_;
  LatencyModel       lat_;
  FeatureEngine      view_fe_;
  InFlight           inflight_;
  std::deque<Delayed> pending_md_;
  std::size_t        fills_seen_ = 0;
  Nanos              now_ = 0;
  SimStats           stats_{};
};

}  // namespace lob
