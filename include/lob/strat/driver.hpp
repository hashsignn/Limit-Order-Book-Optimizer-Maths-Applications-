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
#include <cstdlib>
#include <string>
#include <vector>
#include <vector>

#include "lob/sim/simulator.hpp"
#include "lob/strat/pnl.hpp"
#include "lob/strat/quoting.hpp"

namespace lob {

struct DriverConfig {
  // The message budget: how long a quote must stand before it may be moved.
  //
  // This was 200 EVENTS, and an event count is not a budget — it is a budget
  // divided by whatever rate the generator happens to run at. At the old
  // generator's 2.5 us an event, 200 events was 0.5 ms; at the calibrated
  // 18.55 ms an event the same constant is 3.7 SECONDS, so the same code
  // described a maker requoting twice a millisecond and a maker requoting
  // twice a minute. Every constant downstream was chosen against the first
  // reading: the MDP's 100 ms epoch, the 100 ms markout, and the informed-flow
  // budget in flow.hpp, whose algebra is written in events at 2 us.
  //
  // A real exchange rate-limits in messages per SECOND, and a real maker's
  // reaction time is a time. So this is nanoseconds, and it defaults to the
  // MDP's decision epoch, which is the one cadence it must agree with: every
  // probability in the table is per epoch, so a policy consulted on a
  // different cadence is answering a question it was not solved for.
  Nanos       quote_every_ns = 100'000'000LL;   // 100 ms, the table's epoch
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
  // The run's wall span, so the cadences below can be derived rather than
  // assumed. The MDP's probabilities are all per decision epoch, and the epoch
  // the executor actually runs at was set in three different files — the
  // message budget here, the event rate in FlowConfig, and the grid
  // apps/stats sampled on. Nothing compared them until one of them was wrong.
  // Two of the three are times now and can be compared directly.
  Nanos               span_ns   = 0;

  // The shortest time in which the policy can change its action. The policy is
  // CONSULTED far more often than this — the budget only starts counting again
  // once a quote actually moves, so between requotes the strategy is asked on
  // every update — but it cannot act again until the budget clears, which makes
  // this the granularity the model's epoch should match. It is now the budget
  // itself, which is the point: it no longer depends on the generator's clock.
  [[nodiscard]] static double budget_floor_s(Nanos quote_every_ns) noexcept {
    return static_cast<double>(quote_every_ns) / 1e9;
  }
  // How many market events pass inside one budget period. Not a cadence -- the
  // budget is a time now and does not depend on this -- but the number that
  // says whether the process is being sampled at all. Five events an epoch and
  // five thousand are different simulations of the same market.
  [[nodiscard]] double events_per_budget(Nanos quote_every_ns) const noexcept {
    if (span_ns <= 0) return 0.0;
    return static_cast<double>(stats.market_events) * static_cast<double>(quote_every_ns)
         / static_cast<double>(span_ns);
  }
  // How long a quote actually stayed put, on average.
  [[nodiscard]] double mean_hold_s() const noexcept {
    return requotes == 0 ? 0.0 : static_cast<double>(span_ns) / static_cast<double>(requotes) / 1e9;
  }
  std::vector<double> per_fill_pnl;   // kept so runs can be differenced pairwise
  double              final_mid = 0.0;
  // Peak absolute position reached during the run. A limit that is only
  // checked when the strategy is consulted is not a limit, and a comparison
  // between strategies that breached it by different amounts is a comparison
  // between risk appetites.
  std::int64_t        peak_inventory = 0;

  // What OUR orders actually experienced: how much was queued ahead when each
  // one was placed, how long it rested, and whether it traded. The MDP is
  // calibrated on the hazard the market's own orders see, and that is only the
  // hazard OURS see if the two populations look alike. Recording it is how you
  // find out instead of assuming.
  struct Placement {
    Qty   ahead    = 0;   // resting at our price when we joined
    Qty   level    = 0;   // total at that level then
    Nanos rest_ns  = 0;   // how long it stayed
    Qty   filled   = 0;
    bool  at_touch = false;
  };
  std::vector<Placement> placements;

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

  std::uint64_t seen     = 0;
  std::size_t   requotes = 0;
  OrderId       next_id  = dc.first_id;
  OrderId       bid_id   = 0, ask_id = 0;
  Ticks         cur_bid  = 0, cur_ask = 0;
  std::uint64_t ev       = 0;
  Nanos         last_quote_ts = 0;
  bool          quoted_once   = false;
  double        last_mid = 0.0;
  std::int64_t  peak = 0;
  Nanos         first_ts = 0, last_ts = 0;
  Qty           bid_left = 0, ask_left = 0;
  RunResult::Placement bid_p{}, ask_p{};
  Nanos         bid_at = 0, ask_at = 0;
  std::vector<RunResult::Placement> places;

  auto agent = [&](const AgentView& v, Simulator& s) {
    if (first_ts == 0) first_ts = v.now;
    last_ts = v.now;
    // Markouts are an economic measurement of what actually happened, so they
    // use the TRUE mid, not the agent's lagged view.
    double true_mid = 0.0;
    if (s.true_book().has_bid() && s.true_book().has_ask()) {
      true_mid = 0.5 * (static_cast<double>(s.true_book().best_bid()) +
                        static_cast<double>(s.true_book().best_ask()));
      last_mid = true_mid;
      mk.advance(v.now, true_mid);
    }

    // Global fill indices, so the simulator can trim the log behind us.
    for (; seen < s.fills_end(); ++seen) {
      const Fill& f = s.fill_at(seen);
      if (!f.resting_mine && !f.aggressor_mine) continue;
      const Side our = f.resting_mine ? f.resting_side : opposite(f.resting_side);
      mk.on_fill(f.ts, f.price, f.qty, sign_of(our), f.resting_mine,
                 true_mid > 0.0 ? true_mid : static_cast<double>(f.price));
      if (f.resting_mine) {
        if (f.resting_id == bid_id) { bid_left -= f.qty; bid_p.filled += f.qty; }
        if (f.resting_id == ask_id) { ask_left -= f.qty; ask_p.filled += f.qty; }
      }
    }

    // An order that FILLED is gone, and the hysteresis below has to know.
    // Tracking cancellations only left the driver believing a filled quote was
    // still resting, so it declined to replace it until the target price moved.
    //
    // The obvious repair — clear the id when the order is no longer in the book
    // — is WRONG, and wrong in a way that looks like it works. An order still in
    // flight is not in the book either, so every quote was forgotten the moment
    // it was sent, a fresh one went out on the next decision, and the orphan
    // rested forever because nothing remembered its id to cancel it. Thousands
    // of stale own-orders accumulated, position limits stopped binding entirely
    // (peak inventory 3,740 against a limit of 50), and fill counts looked
    // wonderful.
    //
    // Executions are the only sound signal, and they are what a venue actually
    // reports. Size is counted down as fills arrive; the order is done when it
    // reaches zero, whether or not it has arrived anywhere yet.
    if (bid_id != 0 && bid_left <= 0) {
      bid_p.rest_ns = v.now - bid_at; places.push_back(bid_p); bid_id = 0;
    }
    if (ask_id != 0 && ask_left <= 0) {
      ask_p.rest_ns = v.now - ask_at; places.push_back(ask_p); ask_id = 0;
    }

    s.note_fills_read(seen);   // everything above is consumed; the log may slide

    if (std::llabs(s.stats().inventory) > peak) peak = std::llabs(s.stats().inventory);

    if (!v.book.has_bid() || !v.book.has_ask()) return;
    ++ev;

    if constexpr (requires { strat.set_resting(v, true, Ticks{}, true, Ticks{}); })
      strat.set_resting(v, bid_id != 0, cur_bid, ask_id != 0, cur_ask);

    // The budget, in time. The first decision is free -- otherwise the strategy
    // would sit out its first budget period doing nothing, which on a slow
    // clock is a real part of a short run.
    if (quoted_once && v.now - last_quote_ts < dc.quote_every_ns) return;

    const Quote q = strat.quote(v);

    // Hysteresis: a requote costs queue position, so only move when the target
    // has actually moved. Without this the strategy churns its own priority away.
    const bool bid_moved = q.bid_on != (bid_id != 0) || (q.bid_on && q.bid != cur_bid);
    const bool ask_moved = q.ask_on != (ask_id != 0) || (q.ask_on && q.ask != cur_ask);
    if (!bid_moved && !ask_moved) return;
    last_quote_ts = v.now;
    quoted_once   = true;
    ++requotes;

    if (bid_moved && bid_id) {
      bid_p.rest_ns = v.now - bid_at; places.push_back(bid_p);
      s.send_cancel(bid_id); bid_id = 0; bid_left = 0;
    }
    if (ask_moved && ask_id) {
      ask_p.rest_ns = v.now - ask_at; places.push_back(ask_p);
      s.send_cancel(ask_id); ask_id = 0; ask_left = 0;
    }
    if (bid_moved && q.bid_on) {
      bid_id = next_id++; cur_bid = q.bid; bid_left = q.bid_qty; bid_at = v.now;
      bid_p = RunResult::Placement{v.book.qty_at(Side::Bid, q.bid),
                                   v.book.qty_at(Side::Bid, q.bid), 0, 0,
                                   q.bid == v.book.best_bid()};
      s.send_limit(bid_id, Side::Bid, q.bid, q.bid_qty);
    }
    if (ask_moved && q.ask_on) {
      ask_id = next_id++; cur_ask = q.ask; ask_left = q.ask_qty; ask_at = v.now;
      ask_p = RunResult::Placement{v.book.qty_at(Side::Ask, q.ask),
                                   v.book.qty_at(Side::Ask, q.ask), 0, 0,
                                   q.ask == v.book.best_ask()};
      s.send_limit(ask_id, Side::Ask, q.ask, q.ask_qty);
    }
  };

  sim.run(agent, n_events);

  RunResult r;
  r.name     = Strat::name();
  r.stats    = sim.stats();
  r.requotes = requotes;
  r.span_ns   = last_ts > first_ts ? last_ts - first_ts : 0;
  r.final_mid = last_mid;
  r.peak_inventory = peak;
  r.placements = std::move(places);
  r.attr     = attribute(mk.fills(), dc.markout_idx, FeeSchedule{}, last_mid, sim.stats().inventory);
  for (const auto& f : mk.fills())
    if (f.filled[dc.markout_idx])
      r.per_fill_pnl.push_back(f.markout(dc.markout_idx) / static_cast<double>(f.qty));
  r.per_fill = block_bootstrap(r.per_fill_pnl);
  return r;
}

}  // namespace lob
