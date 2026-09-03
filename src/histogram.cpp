#include "lob/measure/histogram.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace lob {
namespace {

// ceil(log2(v)) for v >= 1.
[[nodiscard]] std::int32_t ceil_log2(std::int64_t v) noexcept {
  std::int32_t n = 0;
  std::int64_t x = 1;
  while (x < v) { x <<= 1; ++n; }
  return n;
}

[[nodiscard]] std::string human(double v) {
  char buf[64];
  const char* suffix = "";
  if (v >= 1e9)      { v /= 1e9; suffix = "G"; }
  else if (v >= 1e6) { v /= 1e6; suffix = "M"; }
  else if (v >= 1e3) { v /= 1e3; suffix = "k"; }
  if (suffix[0] == '\0') std::snprintf(buf, sizeof(buf), "%.0f", v);
  else                   std::snprintf(buf, sizeof(buf), "%.2f%s", v, suffix);
  return buf;
}

}  // namespace

Histogram::Histogram(std::int64_t highest, int significant_digits)
    : highest_(highest), significant_digits_(significant_digits) {
  if (significant_digits < 1 || significant_digits > 5)
    throw std::invalid_argument("significant_digits must be in [1,5]");
  if (highest < 2) throw std::invalid_argument("highest must be >= 2");

  // Enough linear slots that one unit of resolution covers the required number
  // of significant digits anywhere in a bucket.
  std::int64_t largest_single_unit = 2;
  for (int i = 0; i < significant_digits; ++i) largest_single_unit *= 10;

  sub_bucket_count_magnitude_      = ceil_log2(largest_single_unit);
  sub_bucket_count_                = std::int32_t{1} << sub_bucket_count_magnitude_;
  sub_bucket_half_count_           = sub_bucket_count_ / 2;
  sub_bucket_half_count_magnitude_ = sub_bucket_count_magnitude_ - 1;
  sub_bucket_mask_                 = static_cast<std::int64_t>(sub_bucket_count_) - 1;
  // unit_magnitude is fixed at 0: the smallest distinguishable value is 1,
  // which is what nanosecond latencies want.
  leading_zero_count_base_         = 64 - sub_bucket_count_magnitude_;

  std::int64_t smallest_untrackable = static_cast<std::int64_t>(sub_bucket_count_);
  bucket_count_ = 1;
  while (smallest_untrackable <= highest_) {
    if (smallest_untrackable > INT64_MAX / 2) { ++bucket_count_; break; }
    smallest_untrackable <<= 1;
    ++bucket_count_;
  }

  const auto len = static_cast<std::size_t>(bucket_count_ + 1)
                 * static_cast<std::size_t>(sub_bucket_half_count_);
  counts_.assign(len, 0);
}

std::int32_t Histogram::bucket_index(std::int64_t value) const noexcept {
  const auto v = static_cast<std::uint64_t>(value | sub_bucket_mask_);
  return leading_zero_count_base_ - static_cast<std::int32_t>(std::countl_zero(v));
}

std::int32_t Histogram::sub_bucket_index(std::int64_t value, std::int32_t bucket) const noexcept {
  return static_cast<std::int32_t>(static_cast<std::uint64_t>(value) >> bucket);
}

std::int32_t Histogram::counts_index(std::int32_t bucket, std::int32_t sub) const noexcept {
  const std::int32_t base   = (bucket + 1) << sub_bucket_half_count_magnitude_;
  const std::int32_t offset = sub - sub_bucket_half_count_;
  return base + offset;
}

std::int32_t Histogram::counts_index_for(std::int64_t value) const noexcept {
  const std::int32_t b = bucket_index(value);
  return counts_index(b, sub_bucket_index(value, b));
}

std::int64_t Histogram::value_from_index(std::int32_t index) const noexcept {
  std::int32_t bucket = (index >> sub_bucket_half_count_magnitude_) - 1;
  std::int32_t sub    = (index & (sub_bucket_half_count_ - 1)) + sub_bucket_half_count_;
  if (bucket < 0) { sub -= sub_bucket_half_count_; bucket = 0; }
  return static_cast<std::int64_t>(sub) << bucket;
}

std::int64_t Histogram::equivalent_range(std::int64_t value) const noexcept {
  const std::int32_t b   = bucket_index(value);
  const std::int32_t sub = sub_bucket_index(value, b);
  return std::int64_t{1} << (sub >= sub_bucket_count_ ? b + 1 : b);
}

std::int64_t Histogram::lowest_equivalent(std::int64_t value) const noexcept {
  const std::int32_t b = bucket_index(value);
  return static_cast<std::int64_t>(sub_bucket_index(value, b)) << b;
}

std::int64_t Histogram::highest_equivalent(std::int64_t value) const noexcept {
  return lowest_equivalent(value) + equivalent_range(value) - 1;
}

void Histogram::record_n(std::int64_t value, std::int64_t n) noexcept {
  if (value < 0) value = 0;
  if (value > highest_) { value = highest_; overflow_ += n; }
  counts_[static_cast<std::size_t>(counts_index_for(value))] += n;
  count_ += n;
  if (value < min_) min_ = value;
  if (value > max_) max_ = value;
}

void Histogram::record_corrected(std::int64_t value, std::int64_t expected_interval) noexcept {
  record(value);
  if (expected_interval <= 0 || value <= expected_interval) return;
  // Backfill the samples the stall swallowed.
  for (std::int64_t missing = value - expected_interval;
       missing >= expected_interval;
       missing -= expected_interval) {
    record(missing);
  }
}

std::int64_t Histogram::value_at_percentile(double percentile) const noexcept {
  if (count_ == 0) return 0;
  const double requested = std::min(std::max(percentile, 0.0), 100.0);
  auto target = static_cast<std::int64_t>((requested / 100.0) * static_cast<double>(count_) + 0.5);
  target = std::max(target, std::int64_t{1});

  std::int64_t running = 0;
  for (std::size_t i = 0; i < counts_.size(); ++i) {
    running += counts_[i];
    if (running >= target) {
      const std::int64_t v = value_from_index(static_cast<std::int32_t>(i));
      return requested == 0.0 ? lowest_equivalent(v) : highest_equivalent(v);
    }
  }
  return max_;
}

double Histogram::percentile_at_or_below(std::int64_t value) const noexcept {
  if (count_ == 0) return 0.0;
  const std::int32_t target = counts_index_for(value);
  std::int64_t running = 0;
  const auto last = std::min(static_cast<std::size_t>(target) + 1, counts_.size());
  for (std::size_t i = 0; i < last; ++i) running += counts_[i];
  return 100.0 * static_cast<double>(running) / static_cast<double>(count_);
}

double Histogram::mean() const noexcept {
  if (count_ == 0) return 0.0;
  double total = 0.0;
  for (std::size_t i = 0; i < counts_.size(); ++i) {
    if (counts_[i] == 0) continue;
    const std::int64_t v = value_from_index(static_cast<std::int32_t>(i));
    const std::int64_t mid = lowest_equivalent(v) + equivalent_range(v) / 2;
    total += static_cast<double>(counts_[i]) * static_cast<double>(mid);
  }
  return total / static_cast<double>(count_);
}

double Histogram::stddev() const noexcept {
  if (count_ == 0) return 0.0;
  const double m = mean();
  double acc = 0.0;
  for (std::size_t i = 0; i < counts_.size(); ++i) {
    if (counts_[i] == 0) continue;
    const std::int64_t v = value_from_index(static_cast<std::int32_t>(i));
    const std::int64_t mid = lowest_equivalent(v) + equivalent_range(v) / 2;
    const double d = static_cast<double>(mid) - m;
    acc += static_cast<double>(counts_[i]) * d * d;
  }
  return std::sqrt(acc / static_cast<double>(count_));
}

void Histogram::reset() noexcept {
  std::fill(counts_.begin(), counts_.end(), 0);
  count_ = 0; max_ = 0; overflow_ = 0; min_ = INT64_MAX;
}

void Histogram::merge(const Histogram& other) noexcept {
  if (other.count_ == 0) return;
  for (std::size_t i = 0; i < other.counts_.size(); ++i) {
    if (other.counts_[i] != 0)
      record_n(other.value_from_index(static_cast<std::int32_t>(i)), other.counts_[i]);
  }
  // The loop above re-records each slot's *lowest* value, which would drag the
  // merged min/max to bucket boundaries. Both are tracked exactly, so take the
  // true extremes from the source instead.
  if (other.max_ > max_) max_ = other.max_;
  if (other.min_ < min_) min_ = other.min_;
  overflow_ += other.overflow_;
}

std::string Histogram::summary(const char* unit) const {
  if (count_ == 0) return "n=0";
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "n=%s  min=%lld  p50=%lld  p90=%lld  p99=%lld  p99.9=%lld  p99.99=%lld  max=%lld %s",
                human(static_cast<double>(count_)).c_str(),
                static_cast<long long>(min()),
                static_cast<long long>(value_at_percentile(50.0)),
                static_cast<long long>(value_at_percentile(90.0)),
                static_cast<long long>(value_at_percentile(99.0)),
                static_cast<long long>(value_at_percentile(99.9)),
                static_cast<long long>(value_at_percentile(99.99)),
                static_cast<long long>(max()), unit);
  std::string s = buf;
  if (overflow_ > 0) s += "  [" + std::to_string(overflow_) + " clamped at ceiling]";
  return s;
}

std::string Histogram::percentile_table(double scale) const {
  std::string out = "       Value     Percentile TotalCount 1/(1-Percentile)\n\n";
  if (count_ == 0) return out;

  char line[256];
  // Halve the remaining tail each step, so resolution increases into the tail
  // where it matters: 50, 75, 87.5, ... This is the .hgrm convention.
  double percentile = 0.0;
  std::int64_t last_value = -1;
  for (int step = 0; step < 64; ++step) {
    const std::int64_t v = value_at_percentile(percentile);
    if (v != last_value) {
      const double one_over = percentile >= 100.0 ? 0.0 : 1.0 / (1.0 - percentile / 100.0);
      std::snprintf(line, sizeof(line), "%12.3f %14.6f %10lld %15.2f\n",
                    static_cast<double>(v) / scale, percentile / 100.0,
                    static_cast<long long>(
                        static_cast<double>(count_) * percentile / 100.0),
                    one_over);
      out += line;
      last_value = v;
    }
    if (percentile >= 100.0) break;
    percentile = 100.0 - (100.0 - percentile) / 2.0;
    if (100.0 - percentile < 1e-7) percentile = 100.0;
  }
  std::snprintf(line, sizeof(line),
                "#[Mean = %12.3f, StdDeviation = %12.3f]\n"
                "#[Max  = %12.3f, TotalCount   = %12lld]\n",
                mean() / scale, stddev() / scale,
                static_cast<double>(max()) / scale, static_cast<long long>(count_));
  out += line;
  return out;
}

}  // namespace lob
