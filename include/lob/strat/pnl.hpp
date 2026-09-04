// P&L attribution and markouts.
//
// A total P&L number tells you nothing about which mechanism to fix. The
// decomposition in docs/03-metrics-and-estimators.md §10 splits it into lines
// that are controlled by different things:
//
//   spread capture     what you earned at the moment of the fill
//   adverse selection  what the price then did to you
//   inventory cost     carrying risk you did not want
//   fees               maker rebates and taker fees, per venue tier
//
// The identity that makes this exact, for a fill of `qty` at price P where the
// mid was M0 at the fill and M1 at horizon tau, with s = +1 for a buy:
//
//   spread_capture    = s * (M0 - P) * qty
//   adverse_selection = s * (M0 - M1) * qty
//   markout_pnl       = s * (M1 - P) * qty = spread_capture - adverse_selection
//
// Adverse selection is POSITIVE when the price moved against you. The two lines
// are not independent estimates of the same thing — they are the two halves a
// fill decomposes into, and they must sum exactly. The tests assert that.
//
// Markouts are measured at a ladder of horizons because the shape is the story:
// a curve that starts positive and decays negative is normal, and where it
// crosses zero is your effective holding-time budget.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

#include "lob/core/types.hpp"
#include "lob/sim/matching.hpp"

namespace lob {

// Horizons in nanoseconds. Chosen to straddle the scales a maker cares about:
// the first two are microstructure, the last two are where information shows up.
inline constexpr std::array<Nanos, 5> kMarkoutHorizons = {
    100'000LL, 1'000'000LL, 10'000'000LL, 100'000'000LL, 1'000'000'000LL};
inline constexpr std::size_t kNumHorizons = kMarkoutHorizons.size();

// One of our fills, with the mid at the moment it happened and the mid at each
// horizon afterwards. `filled[i]` says whether horizon i has been reached — a
// fill near the end of a run never gets its later markouts, and counting an
// unfilled slot as zero would bias every long-horizon number toward zero.
struct MarkedFill {
  Nanos  ts        = 0;
  Ticks  price     = 0;
  Qty    qty       = 0;
  double mid_at    = 0.0;
  int    sign      = 0;      // +1 we bought, -1 we sold
  bool   passive   = false;  // our resting order was hit, vs we crossed
  std::array<double, kNumHorizons> mid_later{};
  std::array<bool,   kNumHorizons> filled{};

  [[nodiscard]] double spread_capture() const {
    return static_cast<double>(sign) * (mid_at - static_cast<double>(price)) * static_cast<double>(qty);
  }
  [[nodiscard]] double adverse_selection(std::size_t h) const {
    return filled[h] ? static_cast<double>(sign) * (mid_at - mid_later[h]) * static_cast<double>(qty) : 0.0;
  }
  [[nodiscard]] double markout(std::size_t h) const {
    return filled[h] ? static_cast<double>(sign) * (mid_later[h] - static_cast<double>(price)) * static_cast<double>(qty) : 0.0;
  }
};

// Records fills and back-fills their markouts as simulated time advances.
class MarkoutTracker {
 public:
  void on_fill(Nanos ts, Ticks price, Qty qty, int sign, bool passive, double mid_now) {
    MarkedFill f;
    f.ts = ts; f.price = price; f.qty = qty; f.sign = sign;
    f.passive = passive; f.mid_at = mid_now;
    fills_.push_back(f);
    open_.push_back(fills_.size() - 1);
  }

  // Call as time advances. Any open fill whose horizon has now elapsed gets its
  // later mid recorded.
  void advance(Nanos now, double mid_now) {
    if (mid_now <= 0.0) return;
    for (std::size_t i = 0; i < open_.size();) {
      MarkedFill& f = fills_[open_[i]];
      bool all_done = true;
      for (std::size_t h = 0; h < kNumHorizons; ++h) {
        if (f.filled[h]) continue;
        if (now >= f.ts + kMarkoutHorizons[h]) { f.mid_later[h] = mid_now; f.filled[h] = true; }
        else all_done = false;
      }
      if (all_done) { open_[i] = open_.back(); open_.pop_back(); }
      else ++i;
    }
  }

  [[nodiscard]] const std::vector<MarkedFill>& fills() const noexcept { return fills_; }

 private:
  std::vector<MarkedFill>  fills_;
  std::vector<std::size_t> open_;
};

struct Attribution {
  std::size_t n_fills        = 0;
  std::size_t n_passive      = 0;
  std::size_t n_aggressive   = 0;
  Qty         volume         = 0;
  double      spread_capture = 0.0;
  double      adverse_sel    = 0.0;   // positive means it cost us
  double      fees           = 0.0;   // negative is a rebate earned
  double      inventory_mtm  = 0.0;   // leftover position at the final mid
  double      total          = 0.0;
  std::array<double, kNumHorizons> markout_mean{};
  std::array<std::size_t, kNumHorizons> markout_n{};
};

struct FeeSchedule {
  double maker_per_share = -0.0;   // negative = rebate
  double taker_per_share =  0.0;
};

// `horizon` picks which markout defines "adverse selection" for the headline
// decomposition. There is no single right answer — it depends how long you
// expect to hold — so it is a parameter and the full curve is reported too.
[[nodiscard]] inline Attribution attribute(const std::vector<MarkedFill>& fills,
                                           std::size_t horizon,
                                           FeeSchedule fees,
                                           double final_mid,
                                           std::int64_t final_inventory) {
  Attribution a;
  for (const MarkedFill& f : fills) {
    ++a.n_fills;
    (f.passive ? a.n_passive : a.n_aggressive)++;
    a.volume += f.qty;
    a.spread_capture += f.spread_capture();
    a.fees += static_cast<double>(f.qty) * (f.passive ? fees.maker_per_share : fees.taker_per_share);
    if (f.filled[horizon]) a.adverse_sel += f.adverse_selection(horizon);
    for (std::size_t h = 0; h < kNumHorizons; ++h) {
      if (!f.filled[h]) continue;
      // Per share, so fills of different sizes are comparable.
      a.markout_mean[h] += f.markout(h) / static_cast<double>(f.qty);
      ++a.markout_n[h];
    }
  }
  for (std::size_t h = 0; h < kNumHorizons; ++h)
    if (a.markout_n[h] > 0) a.markout_mean[h] /= static_cast<double>(a.markout_n[h]);

  a.inventory_mtm = static_cast<double>(final_inventory) * final_mid;
  a.total = a.spread_capture - a.adverse_sel - a.fees;
  return a;
}

// Block bootstrap over fills.
//
// Fills are clustered and autocorrelated: they arrive in bursts, and the P&L of
// fills within a burst is driven by the same price move. A naive per-fill
// t-statistic assumes an independence that does not exist and will call noise
// significant. Resampling contiguous BLOCKS preserves that dependence.
struct BootstrapCI {
  // A block bootstrap with only a handful of blocks is not an interval, it is
  // an illusion of one. Below this many blocks the CI is reported as unusable
  // rather than printed as though it meant something.
  static constexpr std::size_t kMinBlocks = 10;

  double mean  = 0.0;
  double lo    = 0.0;   // 2.5th percentile
  double hi    = 0.0;   // 97.5th
  std::size_t n_blocks = 0;
  // Entirely above zero, or entirely below. An interval that touches or spans
  // zero does not exclude it — including the degenerate [0, 0], which the
  // previous formulation ((lo>0)==(hi>0)) wrongly reported as significant.
  [[nodiscard]] bool excludes_zero() const noexcept { return lo > 0.0 || hi < 0.0; }
  [[nodiscard]] bool usable() const noexcept { return n_blocks >= kMinBlocks; }
};

[[nodiscard]] inline BootstrapCI block_bootstrap(const std::vector<double>& x,
                                                 std::size_t block_size = 50,
                                                 int draws = 2000,
                                                 std::uint64_t seed = 20260904) {
  BootstrapCI ci;
  if (x.empty()) return ci;
  block_size = std::max<std::size_t>(1, std::min(block_size, x.size()));
  const std::size_t n_blocks = (x.size() + block_size - 1) / block_size;
  ci.n_blocks = n_blocks;

  for (const double v : x) ci.mean += v;
  ci.mean /= static_cast<double>(x.size());

  std::mt19937_64 rng{seed};
  std::vector<double> means;
  means.reserve(static_cast<std::size_t>(draws));
  for (int d = 0; d < draws; ++d) {
    double sum = 0.0; std::size_t cnt = 0;
    for (std::size_t b = 0; b < n_blocks; ++b) {
      const std::size_t start = (rng() % n_blocks) * block_size;
      for (std::size_t j = start; j < std::min(start + block_size, x.size()); ++j) {
        sum += x[j]; ++cnt;
      }
    }
    if (cnt > 0) means.push_back(sum / static_cast<double>(cnt));
  }
  if (means.empty()) return ci;
  std::sort(means.begin(), means.end());
  ci.lo = means[static_cast<std::size_t>(0.025 * static_cast<double>(means.size()))];
  ci.hi = means[static_cast<std::size_t>(0.975 * static_cast<double>(means.size()))];
  return ci;
}

}  // namespace lob
