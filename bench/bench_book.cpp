// Per-operation cost of the L3 book, and end-to-end replay throughput.
//
// Measured on synthetic flow, so the mix is not a real venue's. What it does
// establish is the cost of each operation in isolation and that no operation
// hides an unexpected O(n).
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lob/book/order_book.hpp"
#include "lob/core/compiler.hpp"
#include "lob/feat/features.hpp"
#include "lob/measure/histogram.hpp"
#include "lob/measure/tsc.hpp"
#include "lob/sim/flow.hpp"

using namespace lob;

namespace {

struct Result { std::string name; double ns; double cycles; };

template <typename Setup, typename F>
Result bench(const std::string& name, std::size_t ops, int batches, Setup setup, F&& f) {
  std::vector<double> per_op;
  for (int b = 0; b < batches + 2; ++b) {
    auto state = setup();
    const std::uint64_t t0 = tsc::now_serialized();
    f(state, ops);
    const std::uint64_t t1 = tsc::now_serialized();
    if (b < 2) continue;                       // discard warm-up
    per_op.push_back(static_cast<double>(t1 - t0) / static_cast<double>(ops));
  }
  std::sort(per_op.begin(), per_op.end());
  const double cy = per_op[per_op.size() / 2];
  return {name, cy / tsc::calibration().ticks_per_ns, cy};
}

constexpr Ticks         kBase   = 5'000;
constexpr std::uint32_t kWindow = 10'240;
constexpr std::size_t   kOrders = 1 << 18;

}  // namespace

int main() {
  const auto& cal = tsc::init();
  std::printf("lob Phase 1 book benchmarks — %.4f GHz%s\n\n", cal.ghz,
              cal.trustworthy ? "" : "  [TSC NOT TRUSTWORTHY]");

  std::vector<Result> r;
  constexpr std::size_t kN = 200'000;

  // Add into a book that already has depth, so levels are populated and the
  // FIFO tail pointers are warm — the realistic case, not an empty book.
  r.push_back(bench("add (deep book)", kN, 7,
    [] {
      auto b = std::make_unique<OrderBook>(kBase, kWindow, kOrders);
      for (std::size_t i = 0; i < 50'000; ++i)
        (void)b->add(static_cast<OrderId>(i + 1), (i & 1) ? Side::Bid : Side::Ask,
                     10'000 + ((i & 1) ? -1 : 1) * static_cast<Ticks>(1 + (i % 20)), 100);
      return b;
    },
    [](auto& b, std::size_t n) {
      for (std::size_t i = 0; i < n; ++i)
        do_not_optimize(b->add(static_cast<OrderId>(1'000'000 + i),
                               (i & 1) ? Side::Bid : Side::Ask,
                               10'000 + ((i & 1) ? -1 : 1) * static_cast<Ticks>(1 + (i % 20)), 10));
    }));

  // Cancel an order in the middle of a queue: the O(1) unlink claim.
  r.push_back(bench("cancel (mid-queue)", kN, 7,
    [] {
      auto b = std::make_unique<OrderBook>(kBase, kWindow, kOrders);
      for (std::size_t i = 0; i < kN + 1000; ++i)
        (void)b->add(static_cast<OrderId>(i + 1), Side::Bid,
                     10'000 - static_cast<Ticks>(1 + (i % 20)), 100);
      return b;
    },
    [](auto& b, std::size_t n) {
      for (std::size_t i = 0; i < n; ++i)
        do_not_optimize(b->remove(static_cast<OrderId>(i + 1)));
    }));

  // Partial fill from the front of a level.
  r.push_back(bench("execute (partial)", kN, 7,
    [] {
      auto b = std::make_unique<OrderBook>(kBase, kWindow, kOrders);
      for (std::size_t i = 0; i < kN + 1000; ++i)
        (void)b->add(static_cast<OrderId>(i + 1), Side::Ask,
                     10'000 + static_cast<Ticks>(1 + (i % 20)), 1'000'000);
      return b;
    },
    [](auto& b, std::size_t n) {
      for (std::size_t i = 0; i < n; ++i)
        do_not_optimize(b->execute(static_cast<OrderId>(i + 1), 1));
    }));

  // Top-of-book read: what the feature engine does on every single event.
  r.push_back(bench("best_bid + best_ask", kN, 7,
    [] {
      auto b = std::make_unique<OrderBook>(kBase, kWindow, kOrders);
      for (std::size_t i = 0; i < 50'000; ++i)
        (void)b->add(static_cast<OrderId>(i + 1), (i & 1) ? Side::Bid : Side::Ask,
                     10'000 + ((i & 1) ? -1 : 1) * static_cast<Ticks>(1 + (i % 20)), 100);
      return b;
    },
    [](auto& b, std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        do_not_optimize(b->best_bid());
        do_not_optimize(b->best_ask());
      }
    }));

  // A 10-deep ladder, the usual feature-engine input.
  r.push_back(bench("depth(10) both sides", kN / 4, 7,
    [] {
      auto b = std::make_unique<OrderBook>(kBase, kWindow, kOrders);
      for (std::size_t i = 0; i < 50'000; ++i)
        (void)b->add(static_cast<OrderId>(i + 1), (i & 1) ? Side::Bid : Side::Ask,
                     10'000 + ((i & 1) ? -1 : 1) * static_cast<Ticks>(1 + (i % 40)), 100);
      return b;
    },
    [](auto& b, std::size_t n) {
      Ticks p[10]; Qty q[10];
      for (std::size_t i = 0; i < n; ++i) {
        do_not_optimize(b->depth(Side::Bid, 10, p, q));
        do_not_optimize(b->depth(Side::Ask, 10, p, q));
      }
    }));

  // The feature engine must be O(1) in book size. Run it against books an order
  // of magnitude apart: if the cost tracks depth, an update rule is walking the
  // book and the whole design premise is broken.
  for (const std::size_t depth_orders : {std::size_t{5'000}, std::size_t{50'000}, std::size_t{500'000}}) {
    r.push_back(bench("feature update @" + std::to_string(depth_orders / 1000) + "k orders", kN, 5,
      [depth_orders] {
        auto st = std::make_unique<std::pair<OrderBook, FeatureEngine>>(
            OrderBook{kBase, kWindow, std::size_t{1} << 20}, FeatureEngine{});
        for (std::size_t i = 0; i < depth_orders; ++i)
          (void)st->first.add(static_cast<OrderId>(i + 1), (i & 1) ? Side::Bid : Side::Ask,
                              10'000 + ((i & 1) ? -1 : 1) * static_cast<Ticks>(1 + (i % 30)), 100);
        return st;
      },
      [](auto& st, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
          st->second.update(st->first, static_cast<Nanos>(i) * 1000);
          do_not_optimize(st->second.get().imbalance);
        }
      }));
  }

  std::printf("%-26s %12s %14s\n", "operation", "ns/op", "cycles/op");
  std::printf("--------------------------------------------------------\n");
  for (const auto& x : r)
    std::printf("%-26s %12.2f %14.2f\n", x.name.c_str(), x.ns, x.cycles);

  // End-to-end: a realistic mixed stream, with the per-event latency
  // distribution rather than just a mean.
  {
    std::printf("\nmixed replay (synthetic flow)\n");
    FlowConfig cfg; cfg.mid = 10'000; cfg.levels = 12; cfg.seed = 99;
    FlowGenerator gen{cfg};
    OrderBook b{kBase, kWindow, kOrders};
    Histogram h{1'000'000, 3};

    // Build the stream against a scratch book so the generator's references
    // stay valid, then replay the recorded stream into a clean book for timing.
    std::vector<BookEvent> events;
    events.reserve(1'000'000);
    {
      OrderBook scratch{kBase, kWindow, kOrders};
      for (int i = 0; i < 1'000'000; ++i) {
        const BookEvent e = gen.next();
        (void)scratch.apply(e);
        gen.on_applied(e, scratch.qty_of(e.order_id));
        gen.observe(scratch.has_bid(), scratch.best_bid(), scratch.has_ask(), scratch.best_ask());
        events.push_back(e);
      }
    }

    const std::uint64_t t0 = tsc::now_serialized();
    for (const auto& e : events) {
      const std::uint64_t s = tsc::now();
      do_not_optimize(b.apply(e));
      h.record(static_cast<std::int64_t>(tsc::to_nanos(tsc::now() - s)));
    }
    const std::uint64_t t1 = tsc::now_serialized();

    const double total_ns = static_cast<double>(tsc::to_nanos(t1 - t0));
    std::printf("  %zu events in %.1f ms  =  %.2f M events/s\n",
                events.size(), total_ns / 1e6,
                static_cast<double>(events.size()) * 1e3 / total_ns);
    std::printf("  per event: %s\n", h.summary().c_str());
    std::printf("  (includes ~%.0f ns of rdtsc overhead per event)\n",
                2.0 * static_cast<double>(tsc::to_nanos(35)));
    std::printf("  accepted %llu, rejected %llu\n",
                static_cast<unsigned long long>(b.stats().adds + b.stats().deletes +
                                                b.stats().executes + b.stats().reduces +
                                                b.stats().replaces),
                static_cast<unsigned long long>(
                    b.stats().errors[1] + b.stats().errors[2] + b.stats().errors[3] +
                    b.stats().errors[4] + b.stats().errors[5]));
  }

  std::printf("\nMedian of 7 batches, 2 discarded. Warm cache, idle machine: a floor.\n");
  return 0;
}
