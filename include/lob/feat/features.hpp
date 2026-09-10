// Incremental feature engine.
//
// Everything here updates in O(1) per book event. That constraint is not an
// optimisation, it is the design: a feature that needs a pass over the book is
// a feature the quoting path cannot afford, and one that will quietly diverge
// between research and production because the two will compute it differently.
//
// So each feature is expressed as an update rule over (previous top of book,
// current top of book), and the engine holds only the state those rules need.
//
// What is here, and what is deliberately not:
//
//   HERE      quantities computable online from the book alone.
//   NOT HERE  anything requiring a fitted model. The true micro-price
//             (Stoikov 2018) is the fixed point of a transition matrix
//             estimated from data; what this computes is the imbalance-weighted
//             mid, its first-order approximation. Naming it honestly matters,
//             because the two differ most in exactly the states a market maker
//             cares about. The fitted version belongs in Phase 2b.
//
// References: docs/01-literature.md §F. OFI follows Cont, Kukanov & Stoikov
// (arXiv:1011.6402); the multi-level extension follows arXiv:1907.06230.
#pragma once

#include <cmath>
#include <cstdint>

#include "lob/book/order_book.hpp"
#include "lob/core/types.hpp"

namespace lob {

inline constexpr std::size_t kOfiLevels = 5;

// One snapshot of the top of book, per side, `kOfiLevels` deep. OFI is defined
// as a comparison against the previous snapshot, so the engine keeps one.
struct BookTop {
  Ticks bid_px[kOfiLevels] = {};
  Qty   bid_qty[kOfiLevels] = {};
  Ticks ask_px[kOfiLevels] = {};
  Qty   ask_qty[kOfiLevels] = {};
  std::uint32_t n_bid = 0;
  std::uint32_t n_ask = 0;
  bool  valid = false;
};

struct Features {
  // --- state ---
  // False when the last update saw a one-sided or empty book. Everything from
  // `bid` to `weighted_mid` below is then STALE -- it holds whatever the last
  // two-sided book said, because there is no meaningful value to replace it
  // with. There was no way to tell before, and `updates` still incremented, so
  // a consumer could not distinguish "the mid is 9,991.5" from "the mid was
  // 9,991.5 before the ask side emptied". A one-sided book is precisely the
  // state a market maker must not quote into.
  bool   two_sided    = false;
  Ticks  bid          = 0;
  Ticks  ask          = 0;
  Qty    bid_qty      = 0;
  Qty    ask_qty      = 0;
  Ticks  spread       = 0;
  double mid          = 0.0;   // in ticks, half-tick resolution

  // --- imbalance ---
  // (qb - qa) / (qb + qa) at the touch. In [-1, 1]; positive means bid-heavy,
  // which empirically precedes an up move (arXiv:1512.03492).
  double imbalance    = 0.0;
  // Imbalance over kOfiLevels, which carries information the touch does not
  // (arXiv:1907.06230).
  double deep_imbalance = 0.0;

  // --- weighted mid ---
  // mid + (spread/2) * imbalance. The first-order approximation to the
  // micro-price; NOT the fitted estimator. See the file header.
  double weighted_mid = 0.0;

  // --- order flow imbalance ---
  double ofi_touch    = 0.0;   // this event's contribution at the touch
  double ofi_deep     = 0.0;   // summed over kOfiLevels
  // A decayed running SUM, not an EWMA -- the update is (1-a)*prev + new, with
  // no `a` on the new term, unlike vol_ewma below. That is deliberate and the
  // comment at the update site explains why, but the two sat under the same
  // `_ewma` suffix on scales roughly 145x apart at the default half-life, which
  // invited them to be compared or normalised alike.
  double ofi_decayed  = 0.0;   // decayed running sum, the usable signal

  // --- dynamics ---
  double vol_ewma     = 0.0;   // EWMA of squared mid changes, in ticks
  double event_rate   = 0.0;   // EWMA of events per second
  Nanos  since_last   = 0;

  std::uint64_t updates = 0;
};

struct FeatureConfig {
  // Half-lives, expressed in events rather than seconds: order flow is driven
  // by activity, not by the clock, and an event clock is the standard fix for
  // the intraday seasonality that a wall clock drags in.
  double ofi_halflife_events = 100.0;
  double vol_halflife_events = 500.0;
  // A TIME CONSTANT, not a half-life -- and named as one now. The update below
  // is alpha = 1 - exp(-dt / tau), with no log(2), while the two above do
  // include it. So the actual half-life of event_rate is tau * ln 2, about
  // 0.693 s, and the old name promised 1 s.
  double rate_tau_ns         = 1e9;    // this one IS a clock: it measures the clock
};

class FeatureEngine {
 public:
  explicit FeatureEngine(FeatureConfig cfg = {}) : cfg_(cfg) {
    // A non-positive half-life gives alpha >= 1 and an EWMA that diverges
    // rather than decays. Nothing checked it.
    if (cfg_.ofi_halflife_events <= 0.0) cfg_.ofi_halflife_events = 1.0;
    if (cfg_.vol_halflife_events <= 0.0) cfg_.vol_halflife_events = 1.0;
    if (cfg_.rate_tau_ns          <= 0.0) cfg_.rate_tau_ns        = 1e9;
    ofi_alpha_  = 1.0 - std::exp(-std::log(2.0) / cfg_.ofi_halflife_events);
    vol_alpha_  = 1.0 - std::exp(-std::log(2.0) / cfg_.vol_halflife_events);
  }

  // Call after every applied book event. O(1) — the only work proportional to
  // anything is reading kOfiLevels levels, which is a fixed small constant.
  void update(const OrderBook& book, Nanos ts) noexcept {
    BookTop now;
    snapshot(book, now);

    f_.updates++;
    f_.two_sided = (now.n_bid > 0 && now.n_ask > 0);

    if (f_.two_sided) {
      f_.bid     = now.bid_px[0];
      f_.ask     = now.ask_px[0];
      f_.bid_qty = now.bid_qty[0];
      f_.ask_qty = now.ask_qty[0];
      f_.spread  = f_.ask - f_.bid;
      f_.mid     = 0.5 * (static_cast<double>(f_.bid) + static_cast<double>(f_.ask));

      const double denom = static_cast<double>(f_.bid_qty + f_.ask_qty);
      f_.imbalance = denom > 0.0
          ? (static_cast<double>(f_.bid_qty) - static_cast<double>(f_.ask_qty)) / denom
          : 0.0;

      Qty db = 0, da = 0;
      for (std::uint32_t i = 0; i < now.n_bid; ++i) db += now.bid_qty[i];
      for (std::uint32_t i = 0; i < now.n_ask; ++i) da += now.ask_qty[i];
      const double ddenom = static_cast<double>(db + da);
      f_.deep_imbalance = ddenom > 0.0
          ? (static_cast<double>(db) - static_cast<double>(da)) / ddenom
          : 0.0;

      // Pulls the mid toward the thin side, which is where the price is going.
      f_.weighted_mid = f_.mid + 0.5 * static_cast<double>(f_.spread) * f_.imbalance;
    }

    // ---- order flow imbalance ----
    if (prev_.valid && now.valid) {
      f_.ofi_touch = ofi_at(0, prev_, now);
      double deep = 0.0;
      for (std::size_t i = 0; i < kOfiLevels; ++i) deep += ofi_at(i, prev_, now);
      f_.ofi_deep = deep;
      // Decayed sum rather than a fixed window: no edge effects, one number of
      // state, and the half-life is the only parameter.
      f_.ofi_decayed = (1.0 - ofi_alpha_) * f_.ofi_decayed + f_.ofi_deep;
    } else {
      f_.ofi_touch = f_.ofi_deep = 0.0;
    }

    // ---- realised volatility, on the event clock ----
    if (prev_.valid && prev_mid_ > 0.0 && f_.mid > 0.0) {
      const double d = f_.mid - prev_mid_;
      f_.vol_ewma = (1.0 - vol_alpha_) * f_.vol_ewma + vol_alpha_ * d * d;
    }

    // ---- event rate ----
    if (last_ts_ != 0 && ts > last_ts_) {
      f_.since_last = ts - last_ts_;
      const double dt_s  = static_cast<double>(f_.since_last) * 1e-9;
      const double alpha = 1.0 - std::exp(-static_cast<double>(f_.since_last) / cfg_.rate_tau_ns);
      const double inst  = dt_s > 0.0 ? 1.0 / dt_s : 0.0;
      f_.event_rate = (1.0 - alpha) * f_.event_rate + alpha * inst;
    }

    prev_     = now;
    prev_mid_ = f_.mid;
    last_ts_  = ts;
  }

  void reset() noexcept {
    f_ = Features{};
    prev_ = BookTop{};
    prev_mid_ = 0.0;
    last_ts_ = 0;
  }

  [[nodiscard]] const Features& get() const noexcept { return f_; }
  [[nodiscard]] const BookTop&  previous() const noexcept { return prev_; }

  // Instantaneous OFI contribution at level `i`, exposed for testing against a
  // from-scratch recomputation.
  [[nodiscard]] static double ofi_at(std::size_t i, const BookTop& a, const BookTop& b) noexcept {
    // Cont, Kukanov & Stoikov: a bid that improves or holds its price ADDS its
    // new size; one that worsens or holds REMOVES the old size. Symmetrically,
    // with sign flipped, for the ask. The point is that a price move and a size
    // change are the same kind of event once you look at it this way.
    const bool a_has_b = i < a.n_bid, b_has_b = i < b.n_bid;
    const bool a_has_a = i < a.n_ask, b_has_a = i < b.n_ask;

    double e = 0.0;
    if (a_has_b || b_has_b) {
      const Ticks pb0 = a_has_b ? a.bid_px[i] : 0;
      const Ticks pb1 = b_has_b ? b.bid_px[i] : 0;
      const Qty   qb0 = a_has_b ? a.bid_qty[i] : 0;
      const Qty   qb1 = b_has_b ? b.bid_qty[i] : 0;
      if (!a_has_b)      e += static_cast<double>(qb1);
      else if (!b_has_b) e -= static_cast<double>(qb0);
      else {
        if (pb1 >= pb0) e += static_cast<double>(qb1);
        if (pb1 <= pb0) e -= static_cast<double>(qb0);
      }
    }
    if (a_has_a || b_has_a) {
      const Ticks pa0 = a_has_a ? a.ask_px[i] : 0;
      const Ticks pa1 = b_has_a ? b.ask_px[i] : 0;
      const Qty   qa0 = a_has_a ? a.ask_qty[i] : 0;
      const Qty   qa1 = b_has_a ? b.ask_qty[i] : 0;
      if (!a_has_a)      e -= static_cast<double>(qa1);
      else if (!b_has_a) e += static_cast<double>(qa0);
      else {
        if (pa1 <= pa0) e -= static_cast<double>(qa1);
        if (pa1 >= pa0) e += static_cast<double>(qa0);
      }
    }
    return e;
  }

  static void snapshot(const OrderBook& book, BookTop& out) noexcept {
    out.n_bid = book.depth(Side::Bid, kOfiLevels, out.bid_px, out.bid_qty);
    out.n_ask = book.depth(Side::Ask, kOfiLevels, out.ask_px, out.ask_qty);
    out.valid = true;
  }

 private:
  FeatureConfig cfg_;
  Features      f_{};
  BookTop       prev_{};
  double        prev_mid_  = 0.0;
  Nanos         last_ts_   = 0;
  double        ofi_alpha_ = 0.0;
  double        vol_alpha_ = 0.0;
};

}  // namespace lob
