#include "lob/measure/tsc.hpp"
#include "test_util.hpp"

#include <cstdlib>
#include <cstdio>
#include <ctime>

using namespace lob;

namespace {
std::uint64_t mono_nanos() {
  timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL
       + static_cast<std::uint64_t>(ts.tv_nsec);
}
}  // namespace

int main() {
  const auto& cal = tsc::init();
  std::printf("  tsc: %.4f GHz  invariant=%d nonstop=%d trustworthy=%d\n",
              cal.ghz, cal.invariant, cal.nonstop, cal.trustworthy);
  if (const char* w = cal.warning()) std::printf("  warning: %s\n", w);

  // A plausible clock rate. Anything outside this and calibration is broken.
  CHECK(cal.ticks_per_ns > 0.05);   // > 50 MHz
  CHECK(cal.ticks_per_ns < 20.0);   // < 20 GHz
  CHECK(cal.nanos_mult > 0);

  // Monotonic within a thread.
  {
    std::uint64_t prev = tsc::now();
    bool monotonic = true;
    for (int i = 0; i < 100'000; ++i) {
      const std::uint64_t t = tsc::now();
      if (t < prev) { monotonic = false; break; }
      prev = t;
    }
    CHECK(monotonic);
  }

  // Serialised reads are ordered too, and strictly advance across a call.
  {
    const std::uint64_t a = tsc::now_serialized();
    const std::uint64_t b = tsc::now_serialized();
    CHECK(b >= a);
  }

  // The whole point: TSC deltas converted to ns must agree with a real clock.
  // Busy-wait 20 ms and compare. 2% tolerance covers calibration noise.
  {
    const std::uint64_t w0 = mono_nanos();
    const std::uint64_t c0 = tsc::now_serialized();
    std::uint64_t w1 = w0;
    while (w1 - w0 < 20'000'000ULL) w1 = mono_nanos();
    const std::uint64_t c1 = tsc::now_serialized();

    const auto wall_ns = static_cast<double>(w1 - w0);
    const auto tsc_ns  = static_cast<double>(tsc::to_nanos(c1 - c0));
    const double rel   = std::fabs(tsc_ns - wall_ns) / wall_ns;
    ::lobtest::report(rel < 0.02, "tsc ns within 2% of wall clock", __FILE__, __LINE__,
                      "wall=" + std::to_string(wall_ns) + " tsc=" + std::to_string(tsc_ns) +
                      " rel=" + std::to_string(rel));

    // Integer and floating-point conversions must not disagree meaningfully.
    CHECK_NEAR(tsc::to_nanos_f(c1 - c0), tsc_ns, wall_ns * 0.001);
  }

  // Round-trip ns -> ticks -> ns.
  for (const Nanos ns : {Nanos{1000}, Nanos{1'000'000}, Nanos{1'000'000'000}}) {
    const std::uint64_t ticks = tsc::from_nanos(ns);
    const Nanos back = tsc::to_nanos(ticks);
    const auto  abs_err = static_cast<double>(std::abs(back - ns));
    const double rel = abs_err / static_cast<double>(ns);
    // Either bound is fine: at 1000 ns the tick quantisation floor is ~1 ns,
    // so a relative-only test would be asserting sub-tick precision.
    ::lobtest::report(abs_err <= 1.0 || rel < 0.001, "ns round-trip", __FILE__, __LINE__,
                      std::to_string(ns) + " -> " + std::to_string(back));
  }

  // Cost of a read: this is the measurement overhead every ScopedTimer pays,
  // so it is worth asserting it stays in the tens of nanoseconds.
  {
    constexpr int kN = 100'000;
    const std::uint64_t s = tsc::now_serialized();
    std::uint64_t sink = 0;
    for (int i = 0; i < kN; ++i) sink += tsc::now();
    const std::uint64_t e = tsc::now_serialized();
    const double per = static_cast<double>(tsc::to_nanos(e - s)) / kN;
    std::printf("  rdtsc cost: %.1f ns/read (sink=%llu)\n", per,
                static_cast<unsigned long long>(sink & 1));
    CHECK(per < 100.0);
  }

  return lobtest::summary("tsc");
}
