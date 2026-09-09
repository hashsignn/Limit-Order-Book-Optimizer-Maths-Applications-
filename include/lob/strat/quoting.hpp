// Baseline quoting strategies, in the order the roadmap builds them.
//
// Each adds one idea to the one before, so the comparison answers a specific
// question: does this component pay for itself?
//
//   1 ConstantSpread   the null hypothesis. Surprisingly hard to beat once
//                      adverse selection is accounted for honestly.
//   2 InventorySkew    Ho & Stoll (1981): shift the quote by inventory risk.
//   3 AvellanedaStoikov  the closed form (Quantitative Finance 8(3), 2008).
//   4 GLFT             Guéant, Lehalle & Fernandez-Tapia (arXiv:1105.3115) —
//                      the practical A-S, with inventory bounds.
//   5 ImbalanceSkew    GLFT plus a tilt on order book imbalance.
//   6 JoinTouch        quote AT the touch on both sides, pull a side at the
//                      position limit. No price optimisation at all.
//
// JoinTouch exists because models 1-5 cannot express it. They all quote a
// half-spread measured from the mid, and on a book whose spread is one tick the
// mid sits half a tick above the bid, so the smallest half-spread the tick grid
// admits — one tick — already puts the quote one tick BEHIND the touch. Run on
// a large-tick instrument they therefore take almost no fills, and a comparison
// against them is a comparison against abstention.
//
// This is the same fact tools/calibrate.py ran into from the other direction:
// distance from the mid has no room to vary on these books, so a family that
// optimises distance has nothing to optimise. On a large-tick book the decision
// is whether to be at the touch and where you stand in its queue, and JoinTouch
// is the naive version of that — the honest thing for a queue-aware policy to
// have to beat.
//
// ---------------------------------------------------------------------------
// A AND k ARE NOT KNOWN.
//
// Models 3-5 need the fill intensity lambda(delta) = A*exp(-k*delta): how often
// you get filled as a function of distance from the mid. Both parameters are
// CALIBRATED FROM DATA. They are hand-set here because there is no data yet.
//
// So a comparison run today measures whether these are implemented correctly,
// not which would make money. Worse, the synthetic flow does not produce an
// exponential fill curve at all, so part of what a comparison measures is
// whether the generator happens to match A-S's assumptions. Calibrating A and k
// is Phase 2b work and it needs real MBO data.
// ---------------------------------------------------------------------------
//
// No virtual functions: these are plain structs with a quote() method. Phase 5
// replaces them with a precomputed table read on the hot path, and nothing here
// should make a vtable look acceptable in that context.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "lob/sim/simulator.hpp"

namespace lob {

struct Quote {
  bool  bid_on  = false;
  bool  ask_on  = false;
  Ticks bid     = 0;
  Ticks ask     = 0;
  Qty   bid_qty = 0;
  Qty   ask_qty = 0;
};

struct QuoteParams {
  Qty          size          = 10;
  std::int64_t max_inventory = 200;
  // Zero, not one. min_half is a floor on the DISTANCE FROM THE CENTRE, and the
  // tick grid is the real floor: floor(mid - half) puts a quote at the touch for
  // any half below half a tick and one tick behind it above that. Setting this
  // to 1 threw that resolution away — GLFT's inventory term is 0.13 ticks at
  // flat inventory and 1.20 at the position limit, so the clamp erased the
  // entire signal and left it emitting the same quotes as ConstantSpread over
  // its whole working range. Crossing is prevented by assemble()'s clamp to the
  // opposite touch, which is where that rule belongs.
  Ticks        min_half      = 0;
  Ticks        max_half      = 50;     // never quote absurdly wide

  // --- risk ---
  //
  // gamma * sigma^2 * horizon is the reservation-price skew PER SHARE of
  // inventory, in ticks. That product is the number to reason about; the three
  // factors are only how Ho-Stoll and Avellaneda-Stoikov write it.
  //
  // It was 0.05 * 1.0 * 1e5 = 5,000 ticks per share. At the 50-share limit
  // apps/evaluate runs, that is a reservation price a quarter of a million ticks
  // from the market. apps/backtest and tests/test_strategies both overrode
  // horizon to 1.0 and were fine; apps/evaluate's base_params() sets only size
  // and max_inventory, so the acceptance test — the one place the number
  // mattered — got the raw default. These are now the values the working
  // callers already chose.
  //
  // At 0.05 ticks per share the skew is 2.5 ticks at a 50-share limit and 10 at
  // a 200-share one: enough to shift a quote across the grid, not enough to
  // leave the market.
  double gamma = 0.05;   // CARA risk aversion
  double sigma = 1.0;    // mid volatility, ticks per event (event clock)
  double horizon = 1.0;  // decision epochs remaining; A-S terminal-inventory penalty

  // Skew per share of inventory, in ticks. The product above, named — so a
  // configuration can be sanity-checked instead of multiplied out by hand.
  [[nodiscard]] double skew_per_share() const noexcept { return gamma * sigma * sigma * horizon; }
  // Skew at the position limit. A market maker whose quote leaves the book at
  // full inventory is not skewing, it is crossing.
  [[nodiscard]] double skew_at_limit() const noexcept {
    return static_cast<double>(max_inventory) * skew_per_share();
  }

  // --- fill intensity, lambda(delta) = A * exp(-k * delta). UNCALIBRATED. ---
  double A = 1.0;
  double k = 1.5;

  // --- imbalance tilt (strategy 5) ---
  double imbalance_gain = 1.0;   // ticks of skew at |imbalance| = 1
};

namespace detail {

// Turn a centre and two half-spreads into a quotable pair.
//
// THE BOOK IS AN ARGUMENT, AND THAT IS THE POINT.
//
// This used to clamp the half-spread into [min_half, max_half] and stop there.
// A half-spread is a distance from a CENTRE, and nothing clamped the centre —
// so a strategy quoting around a reservation price far from the market emitted
// orders straight through it. With the shipped defaults, InventorySkew at an
// inventory of one share centred on mid - 5000 and quoted its ask 4,999 ticks
// BELOW the best bid: a sell at any price. Measured over 60,000 events it took
// 2 passive fills and 37,224 aggressive ones, and Avellaneda-Stoikov did the
// same. Neither was quoting; both were crossing, and the comparison they
// appeared in was a comparison of parameterisation artefacts.
//
// A market maker's one inviolable rule is that a passive quote does not take
// liquidity. That is a statement about the book, so the book has to be here.
// The clamp is to the OPPOSITE touch, not to our own side: joining or improving
// the touch is legitimate market making and stays allowed; resting at or
// through the other side is not, and cannot happen now.
//
// A one-sided book returns no quote at all. best_bid() answers 0 for an empty
// side, so a mid computed from one is half the other side's price -- which is
// how a strategy ends up quoting 5,000 ticks from the market without any
// parameter being wrong.
inline Quote assemble(const OrderBook& book, double centre,
                      double half_bid, double half_ask,
                      const QuoteParams& p, std::int64_t inv) {
  Quote q;
  if (!book.has_bid() || !book.has_ask()) return q;   // no touch, no quote

  const auto lo = static_cast<double>(p.min_half);
  const auto hi = static_cast<double>(p.max_half);
  half_bid = std::clamp(half_bid, lo, hi);
  half_ask = std::clamp(half_ask, lo, hi);

  q.bid = static_cast<Ticks>(std::floor(centre - half_bid));
  q.ask = static_cast<Ticks>(std::ceil (centre + half_ask));

  // Never rest at or through the opposite touch.
  q.bid = std::min(q.bid, book.best_ask() - 1);
  q.ask = std::max(q.ask, book.best_bid() + 1);
  if (q.ask <= q.bid) q.ask = q.bid + 1;   // never quote a crossed or locked pair

  q.bid_qty = q.ask_qty = p.size;
  q.bid_on  = inv <  p.max_inventory;
  q.ask_on  = inv > -p.max_inventory;
  return q;
}

// Only meaningful on a two-sided book; every caller reaches it through
// assemble(), which refuses a one-sided one before this value is used.
[[nodiscard]] inline double mid_of(const OrderBook& b) noexcept {
  return 0.5 * (static_cast<double>(b.best_bid()) + static_cast<double>(b.best_ask()));
}

}  // namespace detail

// 1. The null hypothesis: a fixed spread around the mid, no skew at all.
struct ConstantSpread {
  QuoteParams p;
  Ticks       half = 1;

  static constexpr const char* name() { return "constant-spread"; }

  [[nodiscard]] Quote quote(const AgentView& v) const {
    const double mid = detail::mid_of(v.book);
    return detail::assemble(v.book, mid, static_cast<double>(half),
                            static_cast<double>(half), p, v.inventory);
  }
};

// 2. Ho & Stoll: quote around a RESERVATION PRICE rather than the mid. Long
//    inventory pulls both quotes down, so you are more likely to sell and less
//    likely to buy. The spread itself is unchanged — only the centre moves.
struct InventorySkew {
  QuoteParams p;
  Ticks       half = 1;

  static constexpr const char* name() { return "inventory-skew"; }

  [[nodiscard]] Quote quote(const AgentView& v) const {
    const double mid = detail::mid_of(v.book);
    const double r   = reservation_price(mid, v.inventory, p);
    // The half-spreads stay symmetric about r, which is not the same as being
    // symmetric about the mid.
    return detail::assemble(v.book, r, static_cast<double>(half),
                            static_cast<double>(half), p, v.inventory);
  }

  // r = s - q * gamma * sigma^2 * (T - t)
  [[nodiscard]] static double reservation_price(double mid, std::int64_t inv,
                                                const QuoteParams& p) {
    return mid - static_cast<double>(inv) * p.gamma * p.sigma * p.sigma * p.horizon;
  }
};

// 3. Avellaneda-Stoikov, closed form. Same reservation price as Ho-Stoll, plus
//    an optimal TOTAL spread that trades inventory risk against the fill
//    intensity: wider when volatile or risk-averse, tighter when fills are easy.
struct AvellanedaStoikov {
  QuoteParams p;

  static constexpr const char* name() { return "avellaneda-stoikov"; }

  [[nodiscard]] Quote quote(const AgentView& v) const {
    const double mid  = detail::mid_of(v.book);
    const double r    = InventorySkew::reservation_price(mid, v.inventory, p);
    const double half = 0.5 * optimal_spread(p);
    return detail::assemble(v.book, r, half, half, p, v.inventory);
  }

  // delta_total = gamma*sigma^2*(T-t) + (2/gamma)*ln(1 + gamma/k)
  [[nodiscard]] static double optimal_spread(const QuoteParams& p) {
    return p.gamma * p.sigma * p.sigma * p.horizon
         + (2.0 / p.gamma) * std::log1p(p.gamma / p.k);
  }
};

// 4. GLFT: the asymptotic (infinite-horizon) solution with inventory bounds.
//    Unlike A-S it gives the two sides DIFFERENT offsets directly, rather than
//    shifting a symmetric spread, and it does not blow up as the horizon grows.
//    That is what makes it the one people actually run.
struct GLFT {
  QuoteParams p;

  static constexpr const char* name() { return "glft"; }

  [[nodiscard]] Quote quote(const AgentView& v) const {
    const double mid = detail::mid_of(v.book);
    const auto   q   = static_cast<double>(v.inventory) / static_cast<double>(p.size);
    return detail::assemble(v.book, mid, half_bid(q, p), half_ask(q, p), p, v.inventory);
  }

  // delta_b = (1/k)ln(1+gamma/k) + ((2q+1)/2) * sqrt(...)
  // delta_a = (1/k)ln(1+gamma/k) - ((2q-1)/2) * sqrt(...)
  [[nodiscard]] static double base(const QuoteParams& p) {
    return (1.0 / p.k) * std::log1p(p.gamma / p.k);
  }
  [[nodiscard]] static double inventory_term(const QuoteParams& p) {
    const double c = (p.sigma * p.sigma * p.gamma) / (2.0 * p.k * p.A);
    return std::sqrt(c * std::pow(1.0 + p.gamma / p.k, 1.0 + p.k / p.gamma));
  }
  [[nodiscard]] static double half_bid(double q, const QuoteParams& p) {
    return base(p) + ((2.0 * q + 1.0) / 2.0) * inventory_term(p);
  }
  [[nodiscard]] static double half_ask(double q, const QuoteParams& p) {
    return base(p) - ((2.0 * q - 1.0) / 2.0) * inventory_term(p);
  }
};

// 5. GLFT plus an imbalance tilt. A bid-heavy book precedes an up move
//    (arXiv:1512.03492), so lean the quotes into it.
//
//    Note the sign, and note that it is contested. arXiv:2502.18625 finds fill
//    probability and post-fill returns are NEGATIVELY correlated: quoting on
//    the heavy side gets filled more and those fills are worse. Leaning WITH
//    imbalance may be exactly wrong, and only real data settles it. The gain is
//    a parameter so the sign can be flipped and tested.
struct ImbalanceSkew {
  QuoteParams p;

  static constexpr const char* name() { return "glft+imbalance"; }

  [[nodiscard]] Quote quote(const AgentView& v) const {
    const double mid  = detail::mid_of(v.book);
    const auto   q    = static_cast<double>(v.inventory) / static_cast<double>(p.size);
    const double tilt = p.imbalance_gain * v.features.imbalance;
    // Positive imbalance (bid-heavy, price likely up): tighten the bid to buy
    // before the move, widen the ask so as not to sell into it.
    return detail::assemble(v.book, mid, GLFT::half_bid(q, p) - tilt,
                                         GLFT::half_ask(q, p) + tilt, p, v.inventory);
  }
};

struct JoinTouch {
  static const char* name() { return "JoinTouch"; }
  QuoteParams p;

  [[nodiscard]] Quote quote(const AgentView& v) const {
    Quote q;
    if (!v.book.has_bid() || !v.book.has_ask()) return q;
    q.bid = v.book.best_bid();
    q.ask = v.book.best_ask();
    if (q.ask <= q.bid) return q;          // a stale view can arrive crossed
    q.bid_qty = q.ask_qty = p.size;
    q.bid_on = v.inventory <  p.max_inventory;
    q.ask_on = v.inventory > -p.max_inventory;
    return q;
  }
};

}  // namespace lob
