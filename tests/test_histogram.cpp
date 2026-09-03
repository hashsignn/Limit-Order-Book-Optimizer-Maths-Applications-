// The histogram is the foundation of every latency claim this project will
// make, so it gets the most rigorous test in Phase 0. The critical property is
// the precision guarantee: a value read back at a percentile must be within
// 10^-significant_digits of a value actually recorded.
#include "lob/measure/histogram.hpp"
#include "test_util.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace lob;

namespace {

// Exact percentile of a sorted sample, using the same rank convention as
// Histogram::value_at_percentile.
std::int64_t exact_percentile(std::vector<std::int64_t> v, double p) {
  std::sort(v.begin(), v.end());
  auto rank = static_cast<std::int64_t>((p / 100.0) * static_cast<double>(v.size()) + 0.5);
  rank = std::max(rank, std::int64_t{1});
  rank = std::min(rank, static_cast<std::int64_t>(v.size()));
  return v[static_cast<std::size_t>(rank - 1)];
}

}  // namespace

int main() {
  // ---- construction guards ----
  CHECK_THROWS(Histogram(1000, 0));
  CHECK_THROWS(Histogram(1000, 6));
  CHECK_THROWS(Histogram(1, 3));

  // ---- empty ----
  {
    Histogram h{1'000'000, 3};
    CHECK(h.empty());
    CHECK_EQ(h.count(), 0);
    CHECK_EQ(h.min(), 0);
    CHECK_EQ(h.max(), 0);
    CHECK_EQ(h.value_at_percentile(50.0), 0);
    CHECK_NEAR(h.mean(), 0.0, 1e-12);
  }

  // ---- exact recall in the linear region ----
  // Below sub_bucket_count every value has its own slot, so recall is exact.
  {
    Histogram h{1'000'000, 3};
    for (std::int64_t v = 1; v <= 1000; ++v) h.record(v);
    CHECK_EQ(h.count(), 1000);
    CHECK_EQ(h.min(), 1);
    CHECK_EQ(h.max(), 1000);
    CHECK_EQ(h.value_at_percentile(50.0), 500);
    CHECK_EQ(h.value_at_percentile(100.0), 1000);
    CHECK_NEAR(h.mean(), 500.5, 1.0);
  }

  // ---- the precision guarantee, across five orders of magnitude ----
  {
    constexpr int kDigits = 3;
    const double kTolerance = std::pow(10.0, -kDigits);  // 0.1%
    Histogram h{10'000'000'000LL, kDigits};
    std::vector<std::int64_t> raw;
    std::mt19937_64 rng{20260903};
    std::lognormal_distribution<double> dist{7.0, 1.6};
    for (int i = 0; i < 200'000; ++i) {
      auto v = static_cast<std::int64_t>(dist(rng));
      v = std::clamp<std::int64_t>(v, 1, 10'000'000'000LL);
      raw.push_back(v);
      h.record(v);
    }
    CHECK_EQ(h.count(), static_cast<std::int64_t>(raw.size()));

    for (const double p : {50.0, 90.0, 99.0, 99.9, 99.99}) {
      const std::int64_t got  = h.value_at_percentile(p);
      const std::int64_t want = exact_percentile(raw, p);
      const double rel = std::fabs(static_cast<double>(got - want)) / static_cast<double>(want);
      ::lobtest::report(rel <= kTolerance, "percentile within 0.1%", __FILE__, __LINE__,
                        "p=" + std::to_string(p) + " got=" + std::to_string(got) +
                        " want=" + std::to_string(want) + " rel=" + std::to_string(rel));
    }
    // min and max are tracked exactly, not bucketed.
    CHECK_EQ(h.min(), *std::min_element(raw.begin(), raw.end()));
    CHECK_EQ(h.max(), *std::max_element(raw.begin(), raw.end()));
  }

  // ---- equivalence ranges are consistent with recall ----
  {
    Histogram h{1'000'000'000LL, 3};
    for (const std::int64_t v : {1LL, 7LL, 999LL, 2047LL, 2048LL, 5000LL, 1'000'000LL, 999'999'999LL}) {
      const std::int64_t lo = h.lowest_equivalent(v);
      const std::int64_t hi = h.highest_equivalent(v);
      CHECK(lo <= v);
      CHECK(v <= hi);
      // Everything in [lo, hi] must map to the same slot.
      CHECK_EQ(h.lowest_equivalent(lo), lo);
      CHECK_EQ(h.lowest_equivalent(hi), lo);
      // And the range must be within the promised relative precision.
      CHECK(static_cast<double>(hi - lo + 1) <= static_cast<double>(v) * 1e-3 + 1.0);
    }
  }

  // ---- percentile_at_or_below is the inverse of value_at_percentile ----
  {
    Histogram h{1'000'000, 3};
    for (std::int64_t v = 1; v <= 10'000; ++v) h.record(v);
    CHECK_NEAR(h.percentile_at_or_below(5000), 50.0, 1.0);
    CHECK_NEAR(h.percentile_at_or_below(10'000), 100.0, 0.5);
    CHECK_NEAR(h.percentile_at_or_below(1), 0.01, 0.5);
  }

  // ---- clamping at the ceiling is counted, never silent ----
  {
    Histogram h{1000, 3};
    h.record(500);
    h.record(5000);          // above the ceiling
    h.record(1'000'000);     // way above
    CHECK_EQ(h.count(), 3);
    CHECK_EQ(h.overflow_count(), 2);
    CHECK_EQ(h.max(), 1000);   // clamped, and visibly so
  }

  // ---- coordinated-omission correction ----
  // A 1000-unit stall when a sample was expected every 100 units should
  // backfill the samples the stall swallowed, dragging the tail up.
  {
    Histogram plain{1'000'000, 3};
    Histogram corrected{1'000'000, 3};
    for (int i = 0; i < 999; ++i) { plain.record(100); corrected.record_corrected(100, 100); }
    // One 50 µs stall where a sample was due every 100 ns: 499 samples were
    // never taken, and their absence is what flatters the uncorrected tail.
    plain.record(50'000);
    corrected.record_corrected(50'000, 100);

    CHECK_EQ(plain.count(), 1000);
    CHECK(corrected.count() > plain.count());   // the omitted samples are back
    // The uncorrected p99.9 hides the stall; the corrected one cannot.
    CHECK(corrected.value_at_percentile(99.0) > plain.value_at_percentile(99.0));
    // A value at or below the expected interval must not backfill anything.
    Histogram noop{100'000, 3};
    noop.record_corrected(50, 100);
    CHECK_EQ(noop.count(), 1);
  }

  // ---- merge is equivalent to recording into one histogram ----
  {
    Histogram a{1'000'000, 3};
    Histogram b{1'000'000, 3};
    Histogram both{1'000'000, 3};
    std::mt19937_64 rng{7};
    std::uniform_int_distribution<std::int64_t> d{1, 100'000};
    for (int i = 0; i < 20'000; ++i) {
      const std::int64_t x = d(rng), y = d(rng);
      a.record(x); b.record(y);
      both.record(x); both.record(y);
    }
    a.merge(b);
    CHECK_EQ(a.count(), both.count());
    CHECK_EQ(a.max(), both.max());
    CHECK_EQ(a.min(), both.min());
    for (const double p : {50.0, 90.0, 99.0, 99.9})
      CHECK_EQ(a.value_at_percentile(p), both.value_at_percentile(p));
  }

  // ---- reset ----
  {
    Histogram h{1'000'000, 3};
    for (int i = 0; i < 100; ++i) h.record(i + 1);
    h.record(10'000'000);          // force an overflow too
    h.reset();
    CHECK(h.empty());
    CHECK_EQ(h.count(), 0);
    CHECK_EQ(h.max(), 0);
    CHECK_EQ(h.min(), 0);
    CHECK_EQ(h.overflow_count(), 0);
    CHECK_EQ(h.value_at_percentile(99.0), 0);
  }

  // ---- zero and negative inputs are clamped to zero, not UB ----
  {
    Histogram h{1000, 3};
    h.record(0);
    h.record(-5);
    CHECK_EQ(h.count(), 2);
    CHECK_EQ(h.min(), 0);
  }

  // ---- output renders without crashing and carries the headline numbers ----
  {
    Histogram h{1'000'000, 3};
    for (int i = 1; i <= 1000; ++i) h.record(i);
    const std::string s = h.summary();
    CHECK(s.find("p99") != std::string::npos);
    const std::string t = h.percentile_table();
    CHECK(t.find("Percentile") != std::string::npos);
    CHECK(t.find("TotalCount") != std::string::npos);
  }

  return lobtest::summary("histogram");
}
