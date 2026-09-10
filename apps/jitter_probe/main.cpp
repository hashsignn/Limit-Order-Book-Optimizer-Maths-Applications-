// OS jitter baseline — a free, portable stand-in for cyclictest.
//
// Measures the gap between consecutive clock reads in a tight loop. In a
// perfect world every gap is the cost of the read itself; in practice the
// distribution's tail is scheduler preemption, interrupts, SMIs and frequency
// transitions. That tail is the floor under every latency number this machine
// can produce, so it has to be measured before any optimisation is attributed
// to your own code.
//
// Run it before and after tuning (core isolation, IRQ affinity, C-states) to
// see whether the tuning did anything.
#include <cstdio>
#include <cstdlib>

#include "lob/measure/histogram.hpp"
#include "lob/measure/tsc.hpp"

using namespace lob;

int main(int argc, char** argv) {
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 5;
  // A negative value reached from_nanos() and was cast to std::uint64_t, which
  // is undefined and in practice produced a near-UINT64_MAX deadline: the probe
  // spun until it was killed. This is the first tool a new contributor is told
  // to run.
  if (seconds <= 0) {
    std::fprintf(stderr, "seconds must be a positive integer (got %s)\n",
                 argc > 1 ? argv[1] : "0");
    return 2;
  }

  const auto& cal = tsc::init();
  std::printf("Jitter baseline — %d s on this machine\n", seconds);
  std::printf("  tsc %.4f GHz, trustworthy=%s\n", cal.ghz, cal.trustworthy ? "yes" : "NO");
  if (const char* w = cal.warning()) std::printf("  \033[33mwarning: %s\033[0m\n", w);

  Histogram gaps{1'000'000'000LL, 3};   // up to 1 s
  const std::uint64_t deadline_ticks = tsc::from_nanos(static_cast<Nanos>(seconds) * 1'000'000'000LL);
  const std::uint64_t start = tsc::now_serialized();
  std::uint64_t prev = start;
  std::uint64_t samples = 0;

  while (true) {
    const std::uint64_t now = tsc::now_serialized();
    gaps.record(static_cast<std::int64_t>(tsc::to_nanos(now - prev)));
    prev = now;
    ++samples;
    if (now - start >= deadline_ticks) break;
  }

  std::printf("\n%s\n", gaps.summary().c_str());
  std::printf("samples: %llu\n\n", static_cast<unsigned long long>(samples));
  std::printf("%s", gaps.percentile_table().c_str());
  std::printf(
      "\nRead the tail, not the median. The median is the cost of the clock read;\n"
      "p99.9 and max are the machine interrupting you. Anything you build here has\n"
      "that as its floor.\n");
  return 0;
}
