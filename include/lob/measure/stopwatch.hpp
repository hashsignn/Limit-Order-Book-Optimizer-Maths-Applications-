// Scoped timers.
//
// Both types record TSC ticks, not nanoseconds — conversion is deferred to
// report time so the measured path never pays for a divide.
#pragma once

#include <cstdint>

#include "lob/measure/histogram.hpp"
#include "lob/measure/tsc.hpp"

namespace lob {

// Manual start/stop. Serialised reads, so it is honest about short spans.
class Stopwatch {
 public:
  Stopwatch() noexcept : start_(tsc::now_serialized()) {}

  void restart() noexcept { start_ = tsc::now_serialized(); }

  [[nodiscard]] std::uint64_t elapsed_ticks() const noexcept {
    return tsc::now_serialized() - start_;
  }
  [[nodiscard]] Nanos elapsed_nanos() const noexcept {
    return tsc::to_nanos(elapsed_ticks());
  }

 private:
  std::uint64_t start_;
};

// RAII: records into a histogram when the scope exits.
//
//   { ScopedTimer t{hist}; decode(msg); }
//
// Note the measurement itself costs ~40-60 cycles of rdtscp. For a stage that
// takes hundreds of cycles that is acceptable overhead; for one that takes
// tens, measure a loop of them and divide instead.
class ScopedTimer {
 public:
  explicit ScopedTimer(Histogram& h) noexcept
      : hist_(&h), start_(tsc::now_serialized()) {}

  ~ScopedTimer() {
    const std::uint64_t ticks = tsc::now_serialized() - start_;
    hist_->record(static_cast<std::int64_t>(tsc::to_nanos(ticks)));
  }

  ScopedTimer(const ScopedTimer&)            = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;
  ScopedTimer(ScopedTimer&&)                 = delete;
  ScopedTimer& operator=(ScopedTimer&&)      = delete;

 private:
  Histogram*    hist_;
  std::uint64_t start_;
};

}  // namespace lob
