// TSC-based clock.
//
// rdtsc is the only clock cheap enough to bracket a hot-path stage: ~20-30
// cycles, versus ~20-25 ns for clock_gettime even through the vDSO. The cost of
// that is that it counts *cycles*, not time, so it has to be calibrated against
// a real clock once at startup.
//
// Requires an invariant TSC (constant_tsc + nonstop_tsc). Without it the
// counter changes rate with the CPU's frequency and every measurement is
// garbage; init() reports whether the flags are present so a caller can refuse
// to trust its own numbers.
#pragma once

#include <cstdint>

#include "lob/core/compiler.hpp"
#include "lob/core/types.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#define LOB_TSC_X86 1
#else
#define LOB_TSC_X86 0
#include <ctime>
#endif

namespace lob::tsc {

// Unserialised read. Cheapest, but the CPU may reorder it against surrounding
// work, so it can drift by a few cycles either way. Right for timing a stage
// that takes hundreds of cycles or more.
[[nodiscard]] inline std::uint64_t now() noexcept {
#if LOB_TSC_X86
  return __rdtsc();
#else
  timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL
       + static_cast<std::uint64_t>(ts.tv_nsec);
#endif
}

// Waits for all prior instructions to retire before reading, and fences after
// so later loads cannot float above it. Costs a few more cycles than now().
// Use it when the thing being measured is short enough that reordering matters.
[[nodiscard]] inline std::uint64_t now_serialized() noexcept {
#if LOB_TSC_X86
  unsigned aux = 0;
  const std::uint64_t t = __rdtscp(&aux);
  _mm_lfence();
  return t;
#else
  return now();
#endif
}

struct Calibration {
  double        ticks_per_ns = 1.0;
  double        ghz          = 1.0;
  // Fixed-point ns-per-tick as mult >> kShift, so conversion is integer-exact
  // and reproducible rather than dependent on FP rounding.
  std::uint64_t nanos_mult   = 0;
  bool          invariant    = false;  // constant_tsc
  bool          nonstop      = false;  // nonstop_tsc
  bool          trustworthy  = false;  // both of the above, on x86

  [[nodiscard]] const char* warning() const noexcept;
};

inline constexpr unsigned kShift = 32;

// Calibrates on first call (~90 ms of busy-waiting). Call once at startup,
// off the hot path.
[[nodiscard]] const Calibration& calibration();

// Explicitly (re)calibrate. Returns the new calibration.
const Calibration& init();

[[nodiscard]] inline Nanos to_nanos(std::uint64_t ticks) noexcept {
#if LOB_HAS_INT128
  // 128-bit intermediate: ticks * mult overflows 64 bits after a few minutes
  // of uptime, and a silently wrapped duration is worse than a slow one.
  // Round to nearest, not truncate: a systematic downward bias of up to 1 ns
  // per conversion accumulates badly when you are summing stage times.
  const auto m = static_cast<u128>(ticks) * calibration().nanos_mult
               + (static_cast<u128>(1) << (kShift - 1));
  return static_cast<Nanos>(static_cast<std::uint64_t>(m >> kShift));
#else
  return static_cast<Nanos>(static_cast<double>(ticks) / calibration().ticks_per_ns);
#endif
}

[[nodiscard]] inline double to_nanos_f(std::uint64_t ticks) noexcept {
  return static_cast<double>(ticks) / calibration().ticks_per_ns;
}

[[nodiscard]] inline std::uint64_t from_nanos(Nanos ns) noexcept {
  return static_cast<std::uint64_t>(static_cast<double>(ns) * calibration().ticks_per_ns);
}

}  // namespace lob::tsc
