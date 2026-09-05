// Replay harness.
//
// Streams events through the book and reports what the book saw: throughput,
// per-event latency distribution, event mix, rejects, and top-of-book
// statistics.
//
// Two sources, one book. The synthetic generator measures the book's speed with
// flow whose parameters are known. A recorded venue capture measures whether the
// book survives a real market — which is a different question, and the one
// Phase 1's acceptance criterion actually asks.
//
//   replay [N] [--verify]
//   replay --bitstamp <capture.jsonl[.gz] | -> [--snapshot <file>] [--verify]
//
// The claim this file used to make, that adding a real feed would need only a
// decoder and no change here, held for the book but not for the report: real
// data raises questions synthetic data cannot, such as whether the capture has
// holes in it and how much of the book the price window excludes. Those are
// answered below.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "lob/book/order_book.hpp"
#include "lob/core/compiler.hpp"
#include "lob/feat/features.hpp"
#include "lob/measure/histogram.hpp"
#include "lob/measure/recorder.hpp"
#include "lob/measure/tsc.hpp"
#include "lob/feed/bitstamp.hpp"
#include "lob/feed/line_reader.hpp"
#include "lob/sim/flow.hpp"

using namespace lob;

namespace {
void banner(const char* t) {
  std::printf("\n\033[1m%s\033[0m\n", t);
  for (std::size_t i = 0; i < std::strlen(t); ++i) std::putchar('-');
  std::putchar('\n');
}
}  // namespace

namespace {
int replay_synthetic(int n, bool verify) {
  FlowConfig cfg;
  cfg.mid = 10'000; cfg.levels = 12; cfg.seed = 20260904;
  FlowGenerator gen{cfg};
  OrderBook book{5'000, 10'240, 1 << 18};

  FeatureEngine fe;
  Histogram per_event{1'000'000, 3};
  Histogram feat_cost{1'000'000, 3};
  Histogram imb_hist{2000, 3};        // imbalance mapped to [0, 2000]
  Histogram wmid_dev{10'000, 3};      // |weighted_mid - mid| in milli-ticks
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

    const std::uint64_t fs = tsc::now();
    fe.update(book, e.ts);
    feat_cost.record(static_cast<std::int64_t>(tsc::to_nanos(tsc::now() - fs)));

    if (book.has_bid() && book.has_ask()) {
      spread_hist.record(book.spread());
      depth_hist.record(book.best_bid_qty() + book.best_ask_qty());
      const Features& f = fe.get();
      imb_hist.record(static_cast<std::int64_t>((f.imbalance + 1.0) * 1000.0));
      wmid_dev.record(static_cast<std::int64_t>(std::fabs(f.weighted_mid - f.mid) * 1000.0));
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
  std::printf("  book  apply:   %s\n", per_event.summary().c_str());
  std::printf("  feature update: %s\n", feat_cost.summary().c_str());
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
  // Read the median, distrust the tail. The book is built from the stream, so a
  // level nobody has touched since the capture began is not in it; when the
  // touch empties, the next level we KNOW about can be far further out than the
  // real one. The median is corroborated by the snapshot's own touch, which
  // involves no reconstruction at all; the upper percentiles are a property of
  // the reconstruction, not of the market.
  std::printf("  touch depth     %s\n", depth_hist.summary("shares").c_str());
  std::printf("  imbalance*1000+1000 (so 1000 == balanced)\n                  %s\n",
              imb_hist.summary("").c_str());
  std::printf("  |wmid - mid|    %s\n", wmid_dev.summary("milli-ticks").c_str());
  {
    const Features& f = fe.get();
    std::printf("\n  final features: imb %+.3f  deep_imb %+.3f  wmid %.2f (mid %.2f)\n",
                f.imbalance, f.deep_imbalance, f.weighted_mid, f.mid);
    std::printf("                  ofi_ewma %+.1f  vol_ewma %.4f ticks^2  rate %.0f evt/s\n",
                f.ofi_ewma, f.vol_ewma, f.event_rate);
  }

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
}  // namespace

namespace {

// Percentiles of a sample held in memory. The feed-delay series is signed —
// the local clock and the exchange clock disagree by an unknown constant — so
// the bucketed Histogram, which starts at zero, is the wrong instrument here.
double pct(std::vector<Nanos>& v, double p) {
  if (v.empty()) return 0.0;
  const std::size_t k = static_cast<std::size_t>(p / 100.0 * static_cast<double>(v.size() - 1));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return static_cast<double>(v[k]);
}

// tools/record_bitstamp.py writes the snapshot beside the capture under the
// same stamp, so the second path can be derived instead of typed.
std::string snapshot_beside(const std::string& capture) {
  const std::string tail = "_bitstamp.jsonl";
  std::string base = capture;
  if (base.size() > 3 && base.compare(base.size() - 3, 3, ".gz") == 0)
    base.resize(base.size() - 3);
  const std::size_t at = base.rfind(tail);
  if (at == std::string::npos) return {};
  return base.substr(0, at) + "_snapshot.json";
}

bool slurp(const char* path, std::string& out) {
  LineReader r;
  if (!r.open(path)) { std::fprintf(stderr, "  %s\n", r.error().c_str()); return false; }
  std::string_view line;
  while (r.next(&line)) out.append(line.data(), line.size());
  return !out.empty();
}

int replay_bitstamp(const char* capture, const char* snapshot_arg, bool verify,
                    const BitstampConfig& base_cfg, Ticks band,
                    bool price_dp_set, bool qty_dp_set,
                    double seed_guard_pct, Nanos warmup_ns) {
  banner("replay: bitstamp capture");
  std::printf("  capture    %s\n", capture);

  // ---- snapshot: it sets the price window, so it is read first ----
  std::string snap_path = snapshot_arg != nullptr ? std::string(snapshot_arg)
                                                  : snapshot_beside(capture);
  if (snap_path.empty()) {
    std::fprintf(stderr,
        "\n  No snapshot. The book is a flat array over a tick window and cannot be\n"
        "  centred without knowing where the market is. Pass --snapshot <file>;\n"
        "  the recorder writes one beside every capture.\n");
    return 2;
  }
  std::string snap_text;
  if (!slurp(snap_path.c_str(), snap_text)) {
    std::fprintf(stderr, "  cannot read snapshot %s\n", snap_path.c_str());
    return 2;
  }
  std::printf("  snapshot   %s\n", snap_path.c_str());

  BitstampConfig cfg = base_cfg;

  // Measured from the snapshot unless the caller overrode it. btcusd is quoted
  // to the cent and xrpusd to five decimals; assuming either would silently
  // mis-scale the other by a factor of a thousand.
  unsigned px_dp = 0, qt_dp = 0;
  if (BitstampDecoder::detect_decimals(snap_text, &px_dp, &qt_dp)) {
    if (!price_dp_set) cfg.price_decimals = px_dp;
    if (!qty_dp_set)   cfg.qty_decimals   = qt_dp;
    std::printf("  precision  %u price decimals, %u size decimals%s\n",
                cfg.price_decimals, cfg.qty_decimals,
                (price_dp_set || qty_dp_set) ? " (overridden)" : " (measured from snapshot)");
  }

  Ticks bb = 0, ba = 0;
  if (!BitstampDecoder::snapshot_touch(snap_text, cfg, &bb, &ba)) {
    std::fprintf(stderr, "  snapshot has no two-sided book\n");
    return 2;
  }
  // Default band is +-2% of the mid, in ticks, rounded to the multiple of 64
  // the occupancy bitset needs. A fixed tick count cannot serve both a $79,715
  // book at a $0.01 tick and a $2 book at a $0.00001 one: the same 80,000 ticks
  // is +-0.5% of one and +-19% of the other.
  const Ticks mid = (bb + ba) / 2;
  if (band == 0) {
    band = (mid / 25) & ~Ticks{63};
    if (band < 4096) band = 4096;
  }
  cfg.window_ticks = band;
  cfg.window_base  = mid - band / 2;
  cfg.seed_guard   = static_cast<Ticks>(static_cast<double>(mid) * seed_guard_pct / 100.0);

  std::printf("  touch      %lld / %lld  (spread %lld ticks)\n",
              static_cast<long long>(bb), static_cast<long long>(ba),
              static_cast<long long>(ba - bb));
  std::printf("  window     [%lld, %lld)  %lld ticks\n",
              static_cast<long long>(cfg.window_base),
              static_cast<long long>(cfg.window_base + band), static_cast<long long>(band));

  BitstampDecoder dec{cfg};
  OrderBook book{cfg.window_base, static_cast<std::uint32_t>(band), 1 << 21};
  FeatureEngine fe;

  // The snapshot sets precision and centres the window. It does NOT seed the
  // book: it carries orders whose deletes happened before the capture began, so
  // nothing in the stream ever removes them. Measured against the trade channel
  // — a trade must print inside the touch — seeding scored 11.1% on xrpusd
  // against 90.2% for building from the stream alone, and BTC and ETH scored
  // the same either way. --seed-snapshot exists to reproduce that comparison.
  std::vector<BookEvent> seed;
  const Nanos snap_ts = dec.load_snapshot(snap_text, seed);
  std::uint64_t seed_rejects = 0;
  for (const BookEvent& e : seed) {
    if (book.apply(e) != BookError::Ok) ++seed_rejects;
  }
  std::printf("  seeded     %zu snapshot orders beyond +-%lld ticks (%.2f%%) of the touch"
              " (%llu rejected)\n",
              seed.size(), static_cast<long long>(cfg.seed_guard), seed_guard_pct,
              static_cast<unsigned long long>(seed_rejects));
  std::printf("             inside that guard the stream is authoritative: near-touch orders\n"
              "             churn in seconds, so the snapshot's stale ones are contamination\n");
  std::printf("  warm-up    %.0f s before market state is recorded (snapshot ts %lld)\n",
              static_cast<double>(warmup_ns) / 1e9, static_cast<long long>(snap_ts));

  // ---- stream ----
  LineReader reader;
  if (!reader.open(capture)) { std::fprintf(stderr, "\n  %s\n", reader.error().c_str()); return 2; }

  Histogram per_event{1'000'000, 3};
  Histogram spread_hist{100'000, 3};
  Histogram depth_hist{1'000'000'000'000LL, 3};
  Histogram trade_size{1'000'000'000'000LL, 3};
  std::vector<Nanos> order_delay, trade_delay;
  std::uint64_t by_type[static_cast<std::size_t>(EventType::Count)] = {};
  std::uint64_t applied = 0, rejected = 0, invariant_checks = 0;
  Nanos first_ts = 0, last_ts = 0;
  Ticks mid_lo = 0, mid_hi = 0;

  std::string_view line;
  const std::uint64_t t0 = tsc::now_serialized();
  while (reader.next(&line)) {
    const BookTouch touch{book.has_bid(), book.has_ask(),
                          book.has_bid() ? book.best_bid() : 0,
                          book.has_ask() ? book.best_ask() : 0};
    Decoded d;
    if (!dec.decode_line(line, touch, d)) continue;

    if (d.ts != 0) { if (first_ts == 0) first_ts = d.ts; last_ts = d.ts; }
    // Built from the stream, the book starts empty and fills in as orders
    // arrive. Anything measured before it is populated describes the warm-up,
    // not the market.
    const bool warm = (first_ts != 0) && (d.ts - first_ts) >= warmup_ns;
    // Kept apart on purpose. The order and trade channels are not on the same
    // clock — in one capture orders arrived 570ms "late" and trades 88ms
    // "early" — so pooling them would report that offset as jitter and bury the
    // real variation, which is two orders of magnitude smaller.
    if (d.recv_ns != 0 && d.ts != 0)
      (d.is_trade ? trade_delay : order_delay).push_back(d.recv_ns - d.ts);
    if (warm && d.is_trade && d.trade_qty > 0) trade_size.record(d.trade_qty);

    for (int i = 0; i < d.n; ++i) {
      const BookEvent& e = d.ev[i];
      ++by_type[static_cast<std::size_t>(e.type)];

      const std::uint64_t s = tsc::now();
      const BookError err = book.apply(e);
      per_event.record(static_cast<std::int64_t>(tsc::to_nanos(tsc::now() - s)));
      if (err == BookError::Ok) ++applied; else ++rejected;

      fe.update(book, e.ts);
      if (warm && book.has_bid() && book.has_ask()) {
        const Ticks m = (book.best_bid() + book.best_ask()) / 2;
        if (mid_lo == 0 || m < mid_lo) mid_lo = m;
        if (m > mid_hi) mid_hi = m;
        spread_hist.record(book.spread());
        depth_hist.record(book.best_bid_qty() + book.best_ask_qty());
      }

      if (verify && (applied % 10'000 == 0)) {
        std::string why;
        ++invariant_checks;
        if (!book.check_invariants(&why)) {
          std::fprintf(stderr, "\n\033[31mINVARIANT VIOLATION after %llu events: %s\033[0m\n",
                       static_cast<unsigned long long>(applied), why.c_str());
          return 1;
        }
      }
    }
  }
  const std::uint64_t t1 = tsc::now_serialized();
  const double total_ns = static_cast<double>(tsc::to_nanos(t1 - t0));
  const auto& ds = dec.stats();

  banner("capture");
  std::printf("  %.1f MB read, %llu lines, %.1f s of market time\n",
              static_cast<double>(reader.bytes_read()) / 1e6,
              static_cast<unsigned long long>(ds.lines),
              static_cast<double>(last_ts - first_ts) / 1e9);
  std::printf("  decode+book %.1f ms wall, %.2f M lines/s\n",
              total_ns / 1e6, static_cast<double>(ds.lines) * 1e3 / total_ns);
  std::printf("  book apply: %s\n", per_event.summary().c_str());

  banner("capture quality");
  // The chain is the only thing that can distinguish a quiet market from a
  // dropped message, so it is reported before anything derived from the data.
  std::printf("  sequence chain   %s",
              ds.chain_gaps == 0 ? "\033[32munbroken\033[0m — no message lost\n"
                                 : "\033[31mBROKEN\033[0m\n");
  if (ds.chain_gaps != 0)
    std::printf("                   %llu gap(s): everything below is measured over a capture\n"
                "                   with holes in it, and queue positions across a gap are wrong.\n",
                static_cast<unsigned long long>(ds.chain_gaps));
  std::printf("  size conservation %llu event(s) removed more size than the order held\n",
              static_cast<unsigned long long>(ds.size_violations));
  std::printf("  amount_traded    %s\n",
              ds.missing_amount_traded == 0
                  ? "present on every message — the fill/cancel split is exact"
                  : "MISSING on some messages — the split degrades to 'all cancels' there");
  std::printf("  parse errors     %llu   unknown events %llu   control frames %llu\n",
              static_cast<unsigned long long>(ds.parse_errors),
              static_cast<unsigned long long>(ds.unknown_event),
              static_cast<unsigned long long>(ds.control));
  std::printf("  unknown order    %llu  (change/delete for an id never seen: pre-snapshot\n"
              "                   orders, expected early and near zero after)\n",
              static_cast<unsigned long long>(ds.unknown_order));
  // The guard only works if the price stayed inside it; otherwise a stale deep
  // order becomes the touch. Reported rather than assumed.
  if (mid_hi > 0 && cfg.seed_guard > 0) {
    const Ticks moved = mid_hi - mid_lo;
    std::printf("  price range      %lld ticks over the session, guard is %lld  %s\n",
                static_cast<long long>(moved), static_cast<long long>(cfg.seed_guard),
                moved < cfg.seed_guard ? "\033[32mOK\033[0m"
                                       : "\033[31mTOO NARROW — widen --seed-guard-pct\033[0m");
  }
  std::printf("  outside window   %llu adds, %llu follow-ups suppressed\n",
              static_cast<unsigned long long>(ds.out_of_window),
              static_cast<unsigned long long>(ds.suppressed));
  if (ds.created != 0)
    std::printf("                   %.1f%% of adds sat outside the price band and are NOT in\n"
                "                   any depth number below.\n",
                100.0 * static_cast<double>(ds.out_of_window) / static_cast<double>(ds.created));
  if (ds.grew != 0 || ds.repriced != 0)
    std::printf("  amended in place %llu grew, %llu repriced (both modelled as cancel + new,\n"
                "                   because size added to a resting order loses priority)\n",
                static_cast<unsigned long long>(ds.grew),
                static_cast<unsigned long long>(ds.repriced));

  banner("marketable orders, published but never rested");
  // Bitstamp publishes an aggressive order as order_created at its limit price
  // before publishing the fills it causes. Resting those builds a crossed book.
  std::printf("  held back  %llu of %llu creates (%.1f%%)\n",
              static_cast<unsigned long long>(ds.marketable),
              static_cast<unsigned long long>(ds.created),
              ds.created ? 100.0 * static_cast<double>(ds.marketable) / static_cast<double>(ds.created) : 0.0);
  std::printf("    traded out %llu | cancelled without trading %llu | later rested passive %llu\n",
              static_cast<unsigned long long>(ds.marketable_traded),
              static_cast<unsigned long long>(ds.marketable_cancelled),
              static_cast<unsigned long long>(ds.marketable_rested));
  std::printf("  Their fills are counted on the RESTING side's own events, never here:\n"
              "  counting both would double the traded volume.\n");

  banner("why orders left the book");
  // docs/03 section 5: this split must never be aggregated. Volume cancelled
  // ahead of you is free progress up the queue; volume TRADED ahead of you is
  // progress plus the information that someone is buying.
  {
    const double fe_ = static_cast<double>(ds.fill_events);
    const double ce_ = static_cast<double>(ds.cancel_events + ds.partial_cancels);
    const double tot = fe_ + ce_;
    const double fq = static_cast<double>(ds.filled_qty);
    const double cq = static_cast<double>(ds.cancelled_qty);
    std::printf("  filled     %10llu events  %14lld size  %5.2f%% of events, %5.2f%% of size\n",
                static_cast<unsigned long long>(ds.fill_events),
                static_cast<long long>(ds.filled_qty),
                tot > 0 ? 100.0 * fe_ / tot : 0.0,
                (fq + cq) > 0 ? 100.0 * fq / (fq + cq) : 0.0);
    std::printf("  cancelled  %10llu events  %14lld size  %5.2f%% of events, %5.2f%% of size\n",
                static_cast<unsigned long long>(ds.cancel_events + ds.partial_cancels),
                static_cast<long long>(ds.cancelled_qty),
                tot > 0 ? 100.0 * ce_ / tot : 0.0,
                (fq + cq) > 0 ? 100.0 * cq / (fq + cq) : 0.0);
    std::printf("    of which %llu were partial cancels that KEPT queue position\n",
                static_cast<unsigned long long>(ds.partial_cancels));
    std::printf("  trades seen on the trade channel: %llu\n",
                static_cast<unsigned long long>(ds.trades));
  }

  banner("event mix into the book");
  std::uint64_t emitted = 0;
  for (std::size_t t = 0; t < static_cast<std::size_t>(EventType::Count); ++t) emitted += by_type[t];
  for (std::size_t t = 0; t < static_cast<std::size_t>(EventType::Count); ++t) {
    if (by_type[t] == 0) continue;
    std::printf("  %-9s %10llu  %5.1f%%\n",
                std::string(event_name(static_cast<EventType>(t))).c_str(),
                static_cast<unsigned long long>(by_type[t]),
                emitted > 0 ? 100.0 * static_cast<double>(by_type[t]) / static_cast<double>(emitted) : 0.0);
  }
  std::printf("  applied %llu, rejected %llu\n",
              static_cast<unsigned long long>(applied), static_cast<unsigned long long>(rejected));
  {
    const auto& st = book.stats();
    std::printf("  rejects   ");
    bool any = false;
    for (std::size_t i = 1; i < static_cast<std::size_t>(BookError::Count); ++i) {
      if (st.errors[i] == 0) continue;
      std::printf(" %s=%llu", std::string(error_name(static_cast<BookError>(i))).c_str(),
                  static_cast<unsigned long long>(st.errors[i]));
      any = true;
    }
    std::printf("%s\n", any ? "" : " none");
  }
  // These reconcile: the decoder also tracks the orders it excluded from the
  // window, so that their later changes and deletes are suppressed rather than
  // counted as feed gaps.
  std::printf("  resting orders at end: %zu in book, %zu tracked by the decoder\n",
              book.live_orders(), dec.tracked_orders());
  if (dec.tracked_orders() >= book.live_orders())
    std::printf("                         difference %zu = orders held outside the price band\n",
                dec.tracked_orders() - book.live_orders());

  banner("market state");
  std::printf("  spread (ticks)  %s\n", spread_hist.summary("ticks").c_str());
  // Read the median, distrust the tail. The book is built from the stream, so a
  // level nobody has touched since the capture began is not in it; when the
  // touch empties, the next level we KNOW about can be far further out than the
  // real one. The median is corroborated by the snapshot's own touch, which
  // involves no reconstruction at all; the upper percentiles are a property of
  // the reconstruction, not of the market.
  {
    // Sizes are integers in units of 10^-qty_decimals of the base currency, so
    // 6272315 is 0.06272315 BTC. Printed in those units rather than converted,
    // because everything downstream works in them and a decimal here would be
    // the only place in the pipeline that rounds.
    char unit[32];
    std::snprintf(unit, sizeof(unit), "x1e-%u", cfg.qty_decimals);
    std::printf("  touch depth     %s\n", depth_hist.summary(unit).c_str());
    if (ds.trades > 0) std::printf("  trade size      %s\n", trade_size.summary(unit).c_str());
  }
  if (book.has_bid() && book.has_ask())
    std::printf("  final touch     %lld x %lld  |  %lld x %lld\n",
                static_cast<long long>(book.best_bid_qty()), static_cast<long long>(book.best_bid()),
                static_cast<long long>(book.best_ask()),     static_cast<long long>(book.best_ask_qty()));

  if (!order_delay.empty() || !trade_delay.empty()) {
    banner("feed delay variation");
    // The absolute offset is not interpretable: the local clock is NTP-grade at
    // best, and there is no reason to think the exchange's clock agrees with it.
    // A CONSTANT error cancels out of the spread, though, so the spread is a
    // real measurement of feed jitter even when the offset is nonsense. Each
    // channel is measured against its own fastest message; comparing across the
    // two would measure the difference between two clocks, not the network.
    auto report_channel = [](const char* name, std::vector<Nanos>& v) {
      if (v.empty()) return;
      const double lo = pct(v, 0.0);
      std::printf("  %s (%zu samples), relative to its own fastest message:\n", name, v.size());
      std::printf("   ");
      for (const double p : {50.0, 90.0, 99.0, 99.9, 100.0})
        std::printf("  p%.4g %.1fms", p, (pct(v, p) - lo) / 1e6);
      std::putchar('\n');
    };
    report_channel("live_orders", order_delay);
    report_channel("live_trades", trade_delay);
    std::printf("  Exchange timestamps are whole milliseconds, so anything under 1 ms here is\n"
                "  quantisation rather than measurement. This is internet jitter to a retail\n"
                "  machine, not the microsecond budget docs/00 sets for the execution layer.\n");
  }

  if (verify) {
    std::string why;
    const bool ok = book.check_invariants(&why);
    std::printf("\n  invariants: %s after %llu checks%s\n",
                ok ? "\033[32mHOLD\033[0m" : "\033[31mVIOLATED\033[0m",
                static_cast<unsigned long long>(invariant_checks),
                ok ? "" : ("  " + why).c_str());
    if (!ok) return 1;
  }
  return 0;
}

void usage() {
  std::printf(
      "replay [N] [--verify]                       synthetic flow, N events\n"
      "replay --bitstamp <file|-> [options]        a recorded Bitstamp capture\n"
      "\n"
      "  --snapshot <file>     REST seed. Derived from the capture name if omitted.\n"
      "  --verify              check book invariants as it goes\n"
      "  --warmup-sec <s>      ignore market state for this long while the book\n"
      "                        fills in from the stream (default 60)\n"
      "  --seed-guard-pct <p>  seed snapshot orders more than p%% of mid away from\n"
      "                        the touch; build everything nearer from the stream.\n"
      "                        Default 0.30. Use 0 to seed nothing at all.\n"
      "  --price-decimals <n>  override; measured from the snapshot otherwise\n"
      "  --qty-decimals <n>    override; measured from the snapshot otherwise\n"
      "  --band-ticks <n>      price window width, multiple of 64. Default is +-2%%\n"
      "                        of the snapshot mid.\n"
      "\n"
      "  gzipped captures %s\n",
      LineReader::have_gzip() ? "are read directly."
                              : "need: gunzip -c f.gz | replay --bitstamp -");
}

}  // namespace

int main(int argc, char** argv) {
  tsc::init();

  const char* capture  = nullptr;
  const char* snapshot = nullptr;
  bool  verify = false;
  int   n      = 1'000'000;
  Ticks band   = 0;   // 0 = derive from the snapshot mid
  BitstampConfig cfg;
  bool price_dp_set = false, qty_dp_set = false;
  double seed_guard_pct = 0.30;
  Nanos warmup_ns = 60'000'000'000LL;

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    const bool has_next = (i + 1 < argc);
    if      (std::strcmp(a, "--verify")   == 0) verify = true;
    else if (std::strcmp(a, "--seed-guard-pct") == 0 && has_next)
      seed_guard_pct = std::atof(argv[++i]);
    else if (std::strcmp(a, "--warmup-sec") == 0 && has_next)
      warmup_ns = static_cast<Nanos>(std::atof(argv[++i]) * 1e9);
    else if (std::strcmp(a, "--help")     == 0) { usage(); return 0; }
    else if (std::strcmp(a, "--bitstamp") == 0 && has_next) capture  = argv[++i];
    else if (std::strcmp(a, "--snapshot") == 0 && has_next) snapshot = argv[++i];
    else if (std::strcmp(a, "--price-decimals") == 0 && has_next)
      { cfg.price_decimals = static_cast<unsigned>(std::atoi(argv[++i])); price_dp_set = true; }
    else if (std::strcmp(a, "--qty-decimals") == 0 && has_next)
      { cfg.qty_decimals = static_cast<unsigned>(std::atoi(argv[++i])); qty_dp_set = true; }
    else if (std::strcmp(a, "--band-ticks") == 0 && has_next)
      band = std::atoll(argv[++i]);
    else if (a[0] != '-') n = std::atoi(a);
    else { std::fprintf(stderr, "unknown option %s\n\n", a); usage(); return 2; }
  }

  if (capture != nullptr) {
    // The book indexes levels with a 64-bit occupancy word per 64 ticks.
    if (band < 0 || band % 64 != 0) {
      std::fprintf(stderr, "--band-ticks must be a positive multiple of 64\n");
      return 2;
    }
    return replay_bitstamp(capture, snapshot, verify, cfg, band, price_dp_set, qty_dp_set,
                           seed_guard_pct, warmup_ns);
  }
  return replay_synthetic(n, verify);
}
