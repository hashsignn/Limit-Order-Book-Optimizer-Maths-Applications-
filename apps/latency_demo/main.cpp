// Phase 0 acceptance demo.
//
// Proves the measurement plane works end to end: calibrate the TSC, time work
// at nanosecond resolution, attribute it per stage, and print an HDR percentile
// curve. Also demonstrates why the warm/cold and coordinated-omission
// distinctions matter, since both make naive numbers look better than reality.
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "lob/core/arena.hpp"
#include "lob/core/compiler.hpp"
#include "lob/measure/histogram.hpp"
#include "lob/measure/recorder.hpp"
#include "lob/measure/stopwatch.hpp"
#include "lob/measure/tsc.hpp"

using namespace lob;

namespace {

// Stand-in for a real pipeline stage: enough work that the timer overhead is
// not the whole measurement. Marked noinline so the optimiser cannot hoist it.
[[gnu::noinline]] std::uint64_t fake_stage(std::uint64_t seed, int rounds) {
  std::uint64_t x = seed;
  for (int i = 0; i < rounds; ++i) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;   // xorshift64
  }
  return x;
}

void banner(const char* title) {
  std::printf("\n\033[1m%s\033[0m\n", title);
  for (std::size_t i = 0; i < std::string(title).size(); ++i) std::putchar('-');
  std::putchar('\n');
}

}  // namespace

int main(int argc, char** argv) {
  const int iterations = argc > 1 ? std::atoi(argv[1]) : 200'000;

  banner("1. TSC calibration");
  const auto& cal = tsc::init();
  std::printf("  frequency     %.4f GHz\n", cal.ghz);
  std::printf("  constant_tsc  %s\n", cal.invariant ? "yes" : "NO");
  std::printf("  nonstop_tsc   %s\n", cal.nonstop ? "yes" : "NO");
  std::printf("  trustworthy   %s\n", cal.trustworthy ? "yes" : "NO");
  if (const char* w = cal.warning()) std::printf("  \033[33mwarning: %s\033[0m\n", w);

  // ---------------------------------------------------------------------
  banner("2. Measurement overhead");
  {
    // What a ScopedTimer costs. Every number below is inflated by roughly this.
    constexpr int kN = 200'000;
    Histogram h{100'000, 3};
    const std::uint64_t s = tsc::now_serialized();
    for (int i = 0; i < kN; ++i) { volatile std::uint64_t t = tsc::now_serialized(); (void)t; }
    const std::uint64_t e = tsc::now_serialized();
    std::printf("  rdtscp+lfence  %.1f ns per read\n",
                static_cast<double>(tsc::to_nanos(e - s)) / kN);

    const std::uint64_t s2 = tsc::now_serialized();
    for (int i = 0; i < kN; ++i) h.record(i % 5000);
    const std::uint64_t e2 = tsc::now_serialized();
    std::printf("  histogram      %.1f ns per record\n",
                static_cast<double>(tsc::to_nanos(e2 - s2)) / kN);
  }

  // ---------------------------------------------------------------------
  banner("3. Cold vs warm");
  {
    // The first call after an idle period pays for cold i-cache, cold branch
    // predictors and a cold TLB. Market bursts follow quiet periods, so this
    // is the case that actually costs money — reporting only the warm number
    // is the most common way to publish a latency figure that is not real.
    Histogram cold{10'000'000, 3};
    Histogram warm{10'000'000, 3};

    for (int r = 0; r < 200; ++r) {
      // Evict: walk a buffer larger than L2 between measurements.
      std::vector<std::uint64_t> evict(1 << 20, 1);
      std::uint64_t junk = 0;
      for (std::size_t i = 0; i < evict.size(); i += 8) junk += evict[i];
      do_not_optimize(junk);   // the eviction must actually happen

      const std::uint64_t c0 = tsc::now_serialized();
      const std::uint64_t v  = fake_stage(static_cast<std::uint64_t>(r) + 1, 200);
      const std::uint64_t c1 = tsc::now_serialized();
      cold.record(static_cast<std::int64_t>(tsc::to_nanos(c1 - c0)));
      do_not_optimize(v);

      for (int k = 0; k < 50; ++k) {
        const std::uint64_t w0 = tsc::now_serialized();
        const std::uint64_t w  = fake_stage(static_cast<std::uint64_t>(k) + 1, 200);
        const std::uint64_t w1 = tsc::now_serialized();
        warm.record(static_cast<std::int64_t>(tsc::to_nanos(w1 - w0)));
        do_not_optimize(w);
      }
    }
    std::printf("  warm  %s\n", warm.summary().c_str());
    std::printf("  cold  %s\n", cold.summary().c_str());
    const double ratio = warm.value_at_percentile(50.0) > 0
        ? static_cast<double>(cold.value_at_percentile(50.0)) /
          static_cast<double>(warm.value_at_percentile(50.0)) : 0.0;
    std::printf("  cold/warm p50 ratio: %.2fx\n", ratio);
  }

  // ---------------------------------------------------------------------
  banner("4. Per-stage attribution");
  LatencyRecorder rec;
  {
    std::mt19937_64 rng{20260903};
    std::uniform_int_distribution<int> jitter{0, 40};
    for (int i = 0; i < iterations; ++i) {
      const std::uint64_t t0 = tsc::now_serialized();
      std::uint64_t x = fake_stage(static_cast<std::uint64_t>(i) + 1, 20 + jitter(rng) / 8);
      const std::uint64_t t1 = tsc::now_serialized();
      x ^= fake_stage(x, 60 + jitter(rng));
      const std::uint64_t t2 = tsc::now_serialized();
      x ^= fake_stage(x, 30 + jitter(rng) / 2);
      const std::uint64_t t3 = tsc::now_serialized();
      x ^= fake_stage(x, 10);
      const std::uint64_t t4 = tsc::now_serialized();
      do_not_optimize(x);   // keep the work

      rec.record_ticks(Stage::Decode,         t1 - t0);
      rec.record_ticks(Stage::BookUpdate,     t2 - t1);
      rec.record_ticks(Stage::FeatureCompute, t3 - t2);
      rec.record_ticks(Stage::PolicyEval,     t4 - t3);
      rec.record_ticks(Stage::TickToTrade,    t4 - t0);
    }
  }
  std::printf("%s", rec.report().c_str());

  // ---------------------------------------------------------------------
  banner("5. Coordinated omission");
  {
    // Same underlying reality, two ways of recording it. The uncorrected
    // histogram reports a better p99.9 precisely *because* it stalled.
    Histogram naive{10'000'000, 3};
    Histogram corrected{10'000'000, 3};
    constexpr std::int64_t kExpected = 1000;   // a sample every 1 µs
    for (int i = 0; i < 100'000; ++i) {
      const std::int64_t v = (i % 10'000 == 0) ? 250'000 : 950;   // rare 250 µs stall
      naive.record(v);
      corrected.record_corrected(v, kExpected);
    }
    std::printf("  uncorrected  %s\n", naive.summary().c_str());
    std::printf("  corrected    %s\n", corrected.summary().c_str());
    std::printf("  the stall is invisible in the uncorrected p99.9 and obvious in the corrected one\n");
  }

  // ---------------------------------------------------------------------
  banner("6. Full percentile distribution (tick_to_trade)");
  std::printf("%s", rec[Stage::TickToTrade].percentile_table().c_str());

  std::printf("\nAll values in nanoseconds. Overhead of ~%.0f ns per timed span is included.\n",
              2.0 * static_cast<double>(tsc::to_nanos(30)));
  return 0;
}
