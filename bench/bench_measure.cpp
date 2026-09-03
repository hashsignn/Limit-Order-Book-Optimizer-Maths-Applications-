// Microbenchmarks for the Phase 0 primitives.
//
// Deliberately self-contained rather than pulling in Google Benchmark: the
// measurement layer should not need a network fetch to build, and the harness
// it uses to measure itself should be something you can read in one sitting.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "lob/core/arena.hpp"
#include "lob/core/compiler.hpp"
#include "lob/measure/histogram.hpp"
#include "lob/measure/tsc.hpp"

using namespace lob;

namespace {

struct Result {
  std::string name;
  double      ns_per_op;
  double      cycles_per_op;
};

// Runs `f` in batches, reports the median batch. Median rather than mean so a
// single preemption does not decide the answer.
template <typename F>
Result bench(const std::string& name, std::size_t ops_per_batch, int batches, F&& f) {
  std::vector<double> per_op;
  per_op.reserve(static_cast<std::size_t>(batches));
  for (int b = 0; b < batches + 2; ++b) {
    const std::uint64_t t0 = tsc::now_serialized();
    f(ops_per_batch);
    const std::uint64_t t1 = tsc::now_serialized();
    if (b < 2) continue;                   // discard warm-up batches
    per_op.push_back(static_cast<double>(t1 - t0) / static_cast<double>(ops_per_batch));
  }
  std::sort(per_op.begin(), per_op.end());
  const double cycles = per_op[per_op.size() / 2];
  return {name, cycles / tsc::calibration().ticks_per_ns, cycles};
}

}  // namespace

int main() {
  const auto& cal = tsc::init();
  std::printf("lob Phase 0 microbenchmarks — %.4f GHz%s\n\n", cal.ghz,
              cal.trustworthy ? "" : "  [TSC NOT TRUSTWORTHY]");

  std::vector<Result> r;

  r.push_back(bench("tsc::now (rdtsc)", 1'000'000, 9, [](std::size_t n) {
    std::uint64_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) acc += tsc::now();
    do_not_optimize(acc);
  }));

  r.push_back(bench("tsc::now_serialized", 1'000'000, 9, [](std::size_t n) {
    std::uint64_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) acc += tsc::now_serialized();
    do_not_optimize(acc);
  }));

  {
    Histogram h{1'000'000'000LL, 3};
    r.push_back(bench("Histogram::record", 1'000'000, 9, [&h](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) h.record(static_cast<std::int64_t>(i & 0xFFFF));
    }));
  }

  {
    Arena a{1 << 20};
    r.push_back(bench("Arena::allocate(64)", 8192, 9, [&a](std::size_t n) {
      a.reset();
      for (std::size_t i = 0; i < n; ++i) do_not_optimize(a.allocate(64, 8));
    }));
  }

  {
    struct Node { std::uint64_t a, b, c, d; };
    Pool<Node> p{4096};
    r.push_back(bench("Pool acquire+release", 4096, 9, [&p](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) { Node* x = p.acquire(); do_not_optimize(x); p.release(x); }
    }));
  }

  std::printf("%-26s %12s %14s\n", "operation", "ns/op", "cycles/op");
  std::printf("--------------------------------------------------------\n");
  for (const auto& x : r)
    std::printf("%-26s %12.2f %14.2f\n", x.name.c_str(), x.ns_per_op, x.cycles_per_op);

  std::printf(
      "\nMedian of 9 batches, 2 warm-up batches discarded. These are warm-cache\n"
      "numbers on an idle machine and are a floor, not a promise.\n");
  return 0;
}
