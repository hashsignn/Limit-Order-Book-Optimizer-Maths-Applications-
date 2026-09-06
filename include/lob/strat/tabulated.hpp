// The Phase 5 policy, as a strategy the Phase 4 harness can run.
//
// It solves nothing. Every decision is: build the discretised state from what
// the agent can see, read one byte out of the table, translate it into two
// prices. That is the whole point of the offline/online split — the expensive
// thinking happened once, in apps/solve, and what runs here is an array index.
//
// TRACKING OUR OWN QUEUE POSITION
//
// The state includes how far up each level's queue our order has climbed, and
// nothing hands that to us: the simulator's view book contains market orders
// only, never ours, exactly as a real venue's public feed does. So it is
// estimated the way a real system estimates it — the volume resting at our
// price when we arrived is what is ahead of us, and it can only shrink.
//
// The running MINIMUM is what makes that correct. A level's total size also
// grows as orders queue up BEHIND us, and those are not ahead of us; taking the
// smallest total seen since we joined ignores them. It is an upper bound on our
// true queue position, and it is the same estimator a production system uses,
// because the public feed does not say which orders are whose either.
//
// WHAT THE QUEUE IS MEASURED AGAINST
//
// The bucket is a fraction of a reference depth, and that depth comes from the
// TABLE HEADER, not from the book in front of us. Dividing by the current
// level's size looks equivalent and is not: it turns "a lot of size ahead of
// me" into "a large share of this particular level", so an order alone at a
// thin level and an order alone at a thick one land in different buckets, and
// every lookup is answered from a row the solver computed for a different
// question. The scale the solve used is the only scale that reproduces it.
#pragma once

#include <algorithm>
#include <cstdint>

#include "lob/policy/state.hpp"
#include "lob/policy/table.hpp"
#include "lob/sim/simulator.hpp"
#include "lob/strat/quoting.hpp"

namespace lob {

struct TabulatedPolicy {
  static const char* name() { return "TabulatedMDP"; }

  const policy::PolicyTable* table = nullptr;
  QuoteParams p{};

  // What the driver told us is resting, and our estimate of the queue in front.
  bool  bid_on = false, ask_on = false;
  Ticks bid_px = 0, ask_px = 0;
  Qty   bid_ahead = 0, ask_ahead = 0;
  // Counted so a run can report how often our own quote sat outside the levels
  // the model has a state for — the state is then "not quoting", which is not
  // what is happening, and the count is how that stays visible rather than
  // becoming an invisible approximation.
  mutable std::uint64_t off_grid = 0;

  void set_resting(const AgentView& v, bool bon, Ticks bpx, bool aon, Ticks apx) {
    track(v, Side::Bid, bon, bpx, bid_on, bid_px, bid_ahead);
    track(v, Side::Ask, aon, apx, ask_on, ask_px, ask_ahead);
  }

  Quote quote(const AgentView& v) {
    Quote q;
    if (table == nullptr || !table->loaded()) return q;

    const Ticks bb = v.book.best_bid(), ba = v.book.best_ask();
    const int inv = static_cast<int>(std::clamp<std::int64_t>(
        v.inventory / (p.size > 0 ? p.size : 1),
        -policy::kMaxInventory, policy::kMaxInventory));

    policy::State s;
    s.inventory = inv;
    s.imb = policy::imb_bucket(v.features.imbalance);
    s.bid = side_state(Side::Bid, bb);
    s.ask = side_state(Side::Ask, ba);

    const policy::Action a = policy::decode_action(table->action_for(policy::encode(s)));

    q.bid_qty = q.ask_qty = p.size;
    if (a.bid != 0) { q.bid_on = true; q.bid = bb - (a.bid - 1); }
    if (a.ask != 0) { q.ask_on = true; q.ask = ba + (a.ask - 1); }
    // The action set cannot produce a crossed pair on a book with a positive
    // spread, but the book is the agent's LAGGED copy and a stale one-tick
    // spread can arrive already crossed. Refusing to quote beats quoting
    // through the market by accident.
    if (q.bid_on && q.ask_on && q.ask <= q.bid) { q.bid_on = q.ask_on = false; }
    // The position limit the policy was solved under, enforced here as well.
    // A table is not a place to discover that a constraint was dropped.
    if (v.inventory >= policy::kMaxInventory * p.size) q.bid_on = false;
    if (v.inventory <= -policy::kMaxInventory * p.size) q.ask_on = false;
    return q;
  }

 private:
  void track(const AgentView& v, Side side, bool on, Ticks px,
             bool& cur_on, Ticks& cur_px, Qty& ahead) {
    if (!on) { cur_on = false; ahead = 0; return; }
    if (!cur_on || px != cur_px) {
      // A fresh order joins the back: everything resting here is ahead of us.
      cur_on = true;
      cur_px = px;
      ahead  = v.book.qty_at(side, px);
      return;
    }
    // Only ever shrinks. Growth is orders arriving behind us.
    ahead = std::min(ahead, v.book.qty_at(side, px));
  }

  [[nodiscard]] int side_state(Side side, Ticks touch) const {
    const bool  on    = (side == Side::Bid) ? bid_on : ask_on;
    const Ticks px    = (side == Side::Bid) ? bid_px : ask_px;
    const Qty   ahead = (side == Side::Bid) ? bid_ahead : ask_ahead;
    if (!on) return policy::kNoQuote;
    const Ticks away = (side == Side::Bid) ? (touch - px) : (px - touch);
    if (away < 0 || away >= policy::kQuoteLevels) { ++off_grid; return policy::kNoQuote; }
    return policy::make_side(static_cast<int>(away),
                             policy::queue_bucket(ahead, table->header().queue_scale));
  }
};

}  // namespace lob
