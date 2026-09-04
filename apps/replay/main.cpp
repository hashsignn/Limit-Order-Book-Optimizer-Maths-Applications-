// Replay harness.
//
// Streams events through the book and reports what the book saw: throughput,
// per-event latency distribution, event mix, rejects, and top-of-book
// statistics. Today the source is the synthetic generator; when real MBO data
// arrives the only new code is a decoder that produces BookEvent, and this
// file does not change.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "lob/book/order_book.hpp"
#include "lob/core/compiler.hpp"
#include "lob/measure/histogram.hpp"
#include "lob/measure/recorder.hpp"
#include "lob/measure/tsc.hpp"
#include "lob/sim/flow.hpp"

using namespace lob;

namespace {
void banner(const char* t) {
  std::printf("\n\033[1m%s\033[0m\n", t);
  for (std::size_t i = 0; i < std::strlen(t); ++i) std::putchar('-');
  std::putchar('\n');
}
}  // namespace

int main(int argc, char** argv) {
  const int  n      = argc > 1 ? std::atoi(argv[1]) : 1'000'000;
  const bool verify = argc > 2 && std::strcmp(argv[2], "--verify") == 0;

  tsc::init();

  FlowConfig cfg;
  cfg.mid = 10'000; cfg.levels = 12; cfg.seed = 20260904;
  FlowGenerator gen{cfg};
  OrderBook book{5'000, 10'240, 1 << 18};

  Histogram per_event{1'000'000, 3};
  Histogram spread_hist{1'000, 3};
  Histogram depth_hist{1'000'000'000, 3};
  std::uint64_t by_type[static_cast<std::size_t>(EventType::Count)] = {};
  std::uint64_t invariant_checks = 0;

  banner("replay");
  std::printf("  source     synthetic flow (seed %llu)\n",
              static_cast<unsigned long long>(cfg.seed));
  std::printf("  events     %d\n", n);
  std::printf("  verify     %s\n", verify ? "yes (invariants every 10k events)" : "no");

  const std::uint64_t t0 = tsc::now_serialized();
  for (int i = 0; i < n; ++i) {
    const BookEvent e = gen.next();
    ++by_type[static_cast<std::size_t>(e.type)];

    const std::uint64_t s = tsc::now();
    const BookError err = book.apply(e);
    per_event.record(static_cast<std::int64_t>(tsc::to_nanos(tsc::now() - s)));
    do_not_optimize(err);

    // Keep the generator's belief in step with reality: after a partial the
    // order is still resting with less size, after a full one it is gone.
    gen.on_applied(e, book.qty_of(e.order_id));
    gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());

    if (book.has_bid() && book.has_ask()) {
      spread_hist.record(book.spread());
      depth_hist.record(book.best_bid_qty() + book.best_ask_qty());
    }

    if (verify && (i % 10'000 == 0)) {
      std::string why;
      ++invariant_checks;
      if (!book.check_invariants(&why)) {
        std::fprintf(stderr, "\n\033[31mINVARIANT VIOLATION at event %d: %s\033[0m\n", i, why.c_str());
        return 1;
      }
    }
  }
  const std::uint64_t t1 = tsc::now_serialized();
  const double total_ns = static_cast<double>(tsc::to_nanos(t1 - t0));

  banner("throughput");
  std::printf("  %.1f ms wall, %.2f M events/s  (book + generator + measurement)\n",
              total_ns / 1e6, static_cast<double>(n) * 1e3 / total_ns);
  std::printf("  per-event latency: %s\n", per_event.summary().c_str());
  std::printf("  (each event pays ~%.0f ns of rdtsc overhead to be measured at all)\n",
              2.0 * static_cast<double>(tsc::to_nanos(35)));

  banner("event mix");
  for (std::size_t t = 0; t < static_cast<std::size_t>(EventType::Count); ++t) {
    if (by_type[t] == 0) continue;
    std::printf("  %-9s %10llu  %5.1f%%\n", std::string(event_name(static_cast<EventType>(t))).c_str(),
                static_cast<unsigned long long>(by_type[t]),
                100.0 * static_cast<double>(by_type[t]) / static_cast<double>(n));
  }

  banner("book outcome");
  const auto& st = book.stats();
  std::printf("  applied    adds %llu  deletes %llu  reduces %llu  executes %llu  replaces %llu\n",
              static_cast<unsigned long long>(st.adds), static_cast<unsigned long long>(st.deletes),
              static_cast<unsigned long long>(st.reduces), static_cast<unsigned long long>(st.executes),
              static_cast<unsigned long long>(st.replaces));
  std::printf("  rejected  ");
  bool any = false;
  for (std::size_t i = 1; i < static_cast<std::size_t>(BookError::Count); ++i) {
    if (st.errors[i] == 0) continue;
    std::printf(" %s=%llu", std::string(error_name(static_cast<BookError>(i))).c_str(),
                static_cast<unsigned long long>(st.errors[i]));
    any = true;
  }
  std::printf("%s\n", any ? "" : " none");
  std::printf("  resting orders at end: %zu\n", book.live_orders());
  if (book.has_bid() && book.has_ask())
    std::printf("  final touch: %lld x %lld  |  %lld x %lld\n",
                static_cast<long long>(book.best_bid_qty()), static_cast<long long>(book.best_bid()),
                static_cast<long long>(book.best_ask()),     static_cast<long long>(book.best_ask_qty()));

  banner("book state distributions");
  std::printf("  spread (ticks)  %s\n", spread_hist.summary("ticks").c_str());
  std::printf("  touch depth     %s\n", depth_hist.summary("shares").c_str());

  if (verify) {
    std::string why;
    const bool ok = book.check_invariants(&why);
    std::printf("\n  invariants: %s after %llu checks%s\n",
                ok ? "\033[32mHOLD\033[0m" : "\033[31mVIOLATED\033[0m",
                static_cast<unsigned long long>(invariant_checks),
                ok ? "" : ("  " + why).c_str());
    if (!ok) return 1;
  }

  std::printf("\nSynthetic flow: use this to measure the book, never to calibrate a model.\n");
  return 0;
}
