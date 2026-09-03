#include "lob/measure/tsc.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace lob::tsc {
namespace {

[[nodiscard]] std::uint64_t mono_raw_nanos() noexcept {
  timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL
       + static_cast<std::uint64_t>(ts.tv_nsec);
}

// Reads /proc/cpuinfo once for the two flags that decide whether the TSC is
// usable as a clock at all.
void read_tsc_flags(bool& invariant, bool& nonstop) noexcept {
  invariant = nonstop = false;
  std::FILE* f = std::fopen("/proc/cpuinfo", "re");
  if (f == nullptr) return;
  std::array<char, 4096> line{};
  while (std::fgets(line.data(), static_cast<int>(line.size()), f) != nullptr) {
    if (std::strncmp(line.data(), "flags", 5) != 0) continue;
    invariant = std::strstr(line.data(), "constant_tsc") != nullptr;
    nonstop   = std::strstr(line.data(), "nonstop_tsc") != nullptr;
    break;
  }
  std::fclose(f);
}

// One calibration round: busy-wait for `window_ns` and count TSC ticks over the
// same interval. Busy-wait rather than sleep, so the thread is never descheduled
// mid-measurement.
[[nodiscard]] double measure_ticks_per_ns(std::uint64_t window_ns) noexcept {
  const std::uint64_t t0 = mono_raw_nanos();
  const std::uint64_t c0 = now_serialized();
  std::uint64_t t1 = t0;
  while (t1 - t0 < window_ns) t1 = mono_raw_nanos();
  const std::uint64_t c1 = now_serialized();
  const std::uint64_t dt = t1 - t0;
  if (dt == 0) return 1.0;
  return static_cast<double>(c1 - c0) / static_cast<double>(dt);
}

Calibration g_cal;
bool        g_done = false;

}  // namespace

const char* Calibration::warning() const noexcept {
#if LOB_TSC_X86
  if (!invariant && !nonstop) return "TSC is neither constant nor nonstop: timings are meaningless";
  if (!invariant)             return "no constant_tsc: TSC rate varies with CPU frequency";
  if (!nonstop)               return "no nonstop_tsc: TSC halts in deep C-states";
  return nullptr;
#else
  return "not x86: falling back to CLOCK_MONOTONIC_RAW, ~25 ns per read";
#endif
}

const Calibration& init() {
  read_tsc_flags(g_cal.invariant, g_cal.nonstop);

  // Three rounds; take the median. A round that gets interrupted reads low
  // (wall time advanced while the TSC did not accumulate our work), so the
  // median is more robust here than the mean.
  std::array<double, 3> rounds{};
  (void)measure_ticks_per_ns(5'000'000);  // discard: warms the loop and the vDSO page
  for (auto& r : rounds) r = measure_ticks_per_ns(30'000'000);
  std::sort(rounds.begin(), rounds.end());
  g_cal.ticks_per_ns = rounds[1];
  g_cal.ghz          = rounds[1];

  const double ns_per_tick = 1.0 / g_cal.ticks_per_ns;
  g_cal.nanos_mult = static_cast<std::uint64_t>(
      std::llround(ns_per_tick * static_cast<double>(1ULL << kShift)));

#if LOB_TSC_X86
  g_cal.trustworthy = g_cal.invariant && g_cal.nonstop;
#else
  // The fallback path already returns nanoseconds from a real clock.
  g_cal.ticks_per_ns = 1.0;
  g_cal.ghz          = 1.0;
  g_cal.nanos_mult   = 1ULL << kShift;
  g_cal.trustworthy  = true;
#endif

  g_done = true;
  return g_cal;
}

const Calibration& calibration() {
  if (!g_done) init();
  return g_cal;
}

}  // namespace lob::tsc
