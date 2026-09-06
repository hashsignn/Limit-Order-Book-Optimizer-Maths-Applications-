// The harness that turns a quoting rule into orders.
//
// Shared by every strategy in the comparison, including the tabulated policy,
// because a comparison in which strategies are driven differently measures the
// drivers. The message budget, the hysteresis rule, the markout bookkeeping and
// the order lifecycle are identical for all of them; the only thing that varies
// is which Quote comes back.
//
// Extracted from apps/backtest so Phase 5's evaluation could reuse it rather
// than grow a second copy that would quietly drift.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "lob/sim/simulator.hpp"
#include "lob/strat/pnl.hpp"
#include "lob/strat/quoting.hpp"

namespace lob {

struct DriverConfig {
  int         quote_every  = 200;    // events between requotes: a message budget
  std::size_t markout_idx  = 3;      // 100 ms — long enough for information, short
                                     // enough that a maker plausibly still holds
  OrderId     first_id     = 900'000'000ULL;
};

struct RunResult {
  std::string         name;
  Attribution         attr;
  BootstrapCI         per_fill;
  SimStats            stats;
  std::size_t         requotes = 0;
  std::vector<double> per_fill_pnl;   // kept so runs can be differenced pairwise
  double              final_mid = 0.0;

  // Session P&L: cash actually exchanged, plus whatever position is left over
  // marked at the closing mid.
  //
  // Attribution::total is NOT this. It is spread capture minus adverse
  // selection at one markout horizon — a decomposition of trading edge, which
  // deliberately says nothing about a position still open at the end. Comparing
  // strategies on it credits nothing to one that made its money by holding, and
  // a policy solved on a process with drift will do exactly that. The
  // decomposition stays, as the diagnostic it is; this is the number the
  // comparison is settled on.
  [[nodiscard]] double pnl() const noexcept {
    return stats.realised_pnl + static_cast<double>(stats.inventory) * final_mid;
  }
};

// Drives one strategy through one simulator run.
//
// A strategy that declares set_resting() is told what is currently resting
// before each decision. The tabulated policy needs it — its state includes
// where our own quotes are and how far up the queue they have climbed — and no
// baseline does, so the hook is detected rather than imposed.
template <typename Strat>
RunResult run_strategy(Strat strat, SimConfig cfg, int n_events, DriverConfig dc = {}) {
  Simulator      sim{cfg};
  MarkoutTracker mk;

  std::size_t   seen     = 0, requotes = 0;
  OrderId       next_id  = dc.first_id;
  OrderId       bid_id   = 0, ask_id = 0;
  Ticks         cur_bid  = 0, cur_ask = 0;
  std::uint64_t ev       = 0, last_quote = 0;
  double        last_mid = 0.0;

  auto agent = [&](const AgentView& v, Simulator& s) {
    // Markouts are an economic measurement of what actually happened, so they
    // use the TRUE mid, not the agent's lagged view.
    double true_mid = 0.0;
    if (s.true_book().has_bid() && s.true_book().has_ask()) {
      true_mid = 0.5 * (static_cast<double>(s.true_book().best_bid()) +
                        static_cast<double>(s.true_book().best_ask()));
      last_mid = true_mid;
      mk.advance(v.now, true_mid);
    }

    for (; seen < s.fills().size(); ++seen) {
      const Fill& f = s.fills()[seen];
      if (!f.resting_mine && !f.aggressor_mine) continue;
      const Side our = f.resting_mine ? f.resting_side : opposite(f.resting_side);
      mk.on_fill(f.ts, f.price, f.qty, sign_of(our), f.resting_mine,
                 true_mid > 0.0 ? true_mid : static_cast<double>(f.price));
    }

    // An order that FILLED is gone, and the hysteresis below has to know.
    //
    // This tracked cancellations only, so after a fill the driver went on
    // believing the quote was still resting and declined to replace it until
    // the target price happened to move. The cost was severe and uneven: a
    // strategy whose target follows the touch re-quoted anyway and barely
    // noticed, while one that holds a stable target simply stopped trading —
    // five fills against two hundred, for the same number of requotes, which is
    // what made it visible.
    //
    // Our own order's status is not market data. A venue reports our
    // executions to us directly, so consulting the true book for it is
    // legitimate in a way that reading anyone else's resting size would not be.
    if (bid_id != 0 && s.true_book().qty_of(bid_id) == 0) bid_id = 0;
    if (ask_id != 0 && s.true_book().qty_of(ask_id) == 0) ask_id = 0;

    if (!v.book.has_bid() || !v.book.has_ask()) return;
    ++ev;

    if constexpr (requires { strat.set_resting(v, true, Ticks{}, true, Ticks{}); })
      strat.set_resting(v, bid_id != 0, cur_bid, ask_id != 0, cur_ask);

    if (ev - last_quote < static_cast<std::uint64_t>(dc.quote_every)) return;

    const Quote q = strat.quote(v);

    // Hysteresis: a requote costs queue position, so only move when the target
    // has actually moved. Without this the strategy churns its own priority away.
    const bool bid_moved = q.bid_on != (bid_id != 0) || (q.bid_on && q.bid != cur_bid);
    const bool ask_moved = q.ask_on != (ask_id != 0) || (q.ask_on && q.ask != cur_ask);
    if (!bid_moved && !ask_moved) return;
    last_quote = ev;
    ++requotes;

    if (bid_moved && bid_id) { s.send_cancel(bid_id); bid_id = 0; }
    if (ask_moved && ask_id) { s.send_cancel(ask_id); ask_id = 0; }
    if (bid_moved && q.bid_on) { bid_id = next_id++; cur_bid = q.bid; s.send_limit(bid_id, Side::Bid, q.bid, q.bid_qty); }
    if (ask_moved && q.ask_on) { ask_id = next_id++; cur_ask = q.ask; s.send_limit(ask_id, Side::Ask, q.ask, q.ask_qty); }
  };

  sim.run(agent, n_events);

  RunResult r;
  r.name     = Strat::name();
  r.stats    = sim.stats();
  r.requotes = requotes;
  r.final_mid = last_mid;
  r.attr     = attribute(mk.fills(), dc.markout_idx, FeeSchedule{}, last_mid, sim.stats().inventory);
  for (const auto& f : mk.fills())
    if (f.filled[dc.markout_idx])
      r.per_fill_pnl.push_back(f.markout(dc.markout_idx) / static_cast<double>(f.qty));
  r.per_fill = block_bootstrap(r.per_fill_pnl);
  return r;
}

}  // namespace lob
