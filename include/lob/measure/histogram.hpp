// High Dynamic Range histogram.
//
// Records values across a wide range at constant relative precision, in fixed
// memory, with an O(1) record path. This is what lets you report p99.9 honestly
// instead of a mean.
//
// The layout is the standard HdrHistogram one: buckets at successive powers of
// two, each split into `sub_bucket_count` linear slots, so the absolute error of
// a stored value stays a fixed fraction of the value itself. `significant_digits`
// picks that fraction (3 digits => within 0.1%).
//
// Implemented here rather than pulled in so the measurement layer has no
// build-time network dependency and no third-party code on the record path.
// The bucketing matches HdrHistogram, so .hgrm output is comparable.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lob {

class Histogram {
 public:
  // `highest` is the largest value that can be recorded without saturating.
  // Anything above it is clamped and counted in `overflow_count()` — a silently
  // dropped outlier would defeat the purpose of the histogram.
  explicit Histogram(std::int64_t highest = 3'600'000'000'000LL,  // 1 hour in ns
                     int significant_digits = 3);

  void record(std::int64_t value) noexcept { record_n(value, 1); }
  void record_n(std::int64_t value, std::int64_t count) noexcept;

  // Corrects for coordinated omission: if a measurement took longer than
  // `expected_interval`, the samples that *would* have been taken during the
  // stall never were, and dropping them makes the tail look better than it is.
  // This backfills them by linear interpolation.
  void record_corrected(std::int64_t value, std::int64_t expected_interval) noexcept;

  [[nodiscard]] std::int64_t value_at_percentile(double percentile) const noexcept;
  [[nodiscard]] double       percentile_at_or_below(std::int64_t value) const noexcept;
  [[nodiscard]] double       mean() const noexcept;
  [[nodiscard]] double       stddev() const noexcept;

  [[nodiscard]] std::int64_t min() const noexcept { return count_ ? min_ : 0; }
  [[nodiscard]] std::int64_t max() const noexcept { return max_; }
  [[nodiscard]] std::int64_t count() const noexcept { return count_; }
  [[nodiscard]] std::int64_t overflow_count() const noexcept { return overflow_; }
  [[nodiscard]] bool         empty() const noexcept { return count_ == 0; }

  void reset() noexcept;
  void merge(const Histogram& other) noexcept;

  // Smallest and largest values that land in the same slot as `value` — i.e.
  // the resolution actually achieved there.
  [[nodiscard]] std::int64_t lowest_equivalent(std::int64_t value) const noexcept;
  [[nodiscard]] std::int64_t highest_equivalent(std::int64_t value) const noexcept;

  // One-line summary: "n=1000 min=41 p50=48 p99=71 p99.9=204 max=1.2k".
  [[nodiscard]] std::string summary(const char* unit = "ns") const;
  // Full percentile distribution, HdrHistogram .hgrm format.
  [[nodiscard]] std::string percentile_table(double scale = 1.0) const;

 private:
  [[nodiscard]] std::int32_t bucket_index(std::int64_t value) const noexcept;
  [[nodiscard]] std::int32_t sub_bucket_index(std::int64_t value, std::int32_t bucket) const noexcept;
  [[nodiscard]] std::int32_t counts_index(std::int32_t bucket, std::int32_t sub) const noexcept;
  [[nodiscard]] std::int32_t counts_index_for(std::int64_t value) const noexcept;
  [[nodiscard]] std::int64_t value_from_index(std::int32_t index) const noexcept;
  [[nodiscard]] std::int64_t equivalent_range(std::int64_t value) const noexcept;

  std::int64_t highest_;
  std::int32_t significant_digits_;
  std::int32_t sub_bucket_count_magnitude_;
  std::int32_t sub_bucket_count_;
  std::int32_t sub_bucket_half_count_;
  std::int32_t sub_bucket_half_count_magnitude_;
  std::int64_t sub_bucket_mask_;
  std::int32_t leading_zero_count_base_;
  std::int32_t bucket_count_;

  std::vector<std::int64_t> counts_;
  std::int64_t count_    = 0;
  std::int64_t min_      = INT64_MAX;
  std::int64_t max_      = 0;
  std::int64_t overflow_ = 0;
};

}  // namespace lob
