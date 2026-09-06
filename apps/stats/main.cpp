// Emits the measurement plane's raw records as CSV, for analysis and plotting.
//
// Two sources, one accumulator. A real Bitstamp capture, or the synthetic flow
// generator the simulator runs on. The second exists because Phase 5's
// acceptance test needs the SIMULATOR's process measured: a policy solved for
// xrpusd and run against synthetic flow tests neither the policy nor the flow,
// only the distance between them. Both sources feed the same accumulation code,
// because a second estimator is a second thing that can be wrong.
//
// Same rule as apps/tape: the numbers come out of the pipeline, and whatever
// draws them only draws. Nothing downstream recomputes book state, because a
// second implementation of the book is a second thing that can be wrong, and
// the one in include/lob/book is the one the tests cover.
//
// Five files, each answering a question docs/03 asks:
//
//   orders.csv   one row per order that left the book. Lifetime, distance from
//                the touch when it arrived, the spread and the volume already
//                queued at its level, and how much of it FILLED versus how much
//                was CANCELLED. Section 5 forbids aggregating those two; this
//                is where the split becomes a dataset. It is also the input to
//                the A/k calibration: lifetime is exposure, a fill is an event,
//                and the pair is a Poisson likelihood.
//   trades.csv   every print, with the touch at the moment it happened, so
//                markouts can be computed without re-deriving the book.
//   mid.csv      the touch on a fixed grid, prices and sizes — the series
//                markouts read forward into, the spread distribution, and the
//                imbalance the MDP's state is built from.
//   depth.csv    mean resting size by tick distance from the touch. The shape
//                that says whether a book is dense or sparse behind the touch.
//   arrivals.csv gaps between consecutive events, in microseconds. Section 4
//                looks here for the exchange round-trip mode.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "lob/book/order_book.hpp"
#include "lob/feed/bitstamp.hpp"
#include "lob/feed/line_reader.hpp"
#include "lob/sim/flow.hpp"
#include "lob/sim/matching.hpp"

using namespace lob;

namespace {

constexpr std::uint32_t kProfileDepth = 40;   // levels each side for depth.csv

struct Rec {
  Nanos ts     = 0;   // when it joined the book
  Ticks px     = 0;
  Ticks dist   = 0;   // ticks from the same-side touch on arrival, 0 = at it
  Ticks spread = 0;   // spread at arrival, so distance from the MID is recoverable
  Qty   ahead  = 0;   // volume already queued at this level when we arrived
  Qty   size   = 0;   // size on arrival
  Qty   filled = 0;
  int   side   = 0;
};

// Everything both sources share. The book is passed in rather than owned so the
// caller can drive it however its source requires — a decoder for a capture, a
// matching engine for synthetic aggressive flow.
class Accumulator {
 public:
  Accumulator(std::FILE* ord, std::FILE* trd, std::FILE* mid, std::FILE* arr)
      : f_ord_(ord), f_trd_(trd), f_mid_(mid), f_arr_(arr) {}

  // Call BEFORE applying the event to the book: an order's queue position on
  // arrival is the volume resting at its level at that moment, and once it has
  // been applied its own size is in there too.
  void before_apply(const OrderBook& book, const BookEvent& e, Nanos rel, bool warm) {
    if (e.type != EventType::Add || !warm || !book.has_bid() || !book.has_ask()) return;
    const Ticks same = (e.side == Side::Bid) ? book.best_bid() : book.best_ask();
    Rec r;
    r.ts = rel; r.px = e.price; r.size = e.qty; r.side = (e.side == Side::Bid) ? 0 : 1;
    r.dist   = (e.side == Side::Bid) ? (same - e.price) : (e.price - same);
    r.spread = book.best_ask() - book.best_bid();
    r.ahead  = book.qty_at(e.side, e.price);
    live_[e.order_id] = r;
  }

  void after_apply(const OrderBook& book, const BookEvent& e, Nanos rel) {
    if (e.type == EventType::Execute) {
      auto it = live_.find(e.order_id);
      if (it != live_.end()) it->second.filled += e.qty;
      return;
    }
    if (e.type != EventType::Delete && e.type != EventType::Reduce) return;
    auto it = live_.find(e.order_id);
    if (it == live_.end()) return;
    const bool gone = (e.type == EventType::Delete) || book.qty_of(e.order_id) == 0;
    if (!gone) return;
    close(it->second, rel);
    live_.erase(it);
  }

  // A resting order that was consumed by a match rather than by a Delete event.
  void on_consumed(OrderId id, Qty qty, Nanos rel, bool gone) {
    auto it = live_.find(id);
    if (it == live_.end()) return;
    it->second.filled += qty;
    if (gone) { close(it->second, rel); live_.erase(it); }
  }

  void on_trade(const OrderBook& book, Nanos rel, Ticks px, int taker_side, Qty qty) {
    if (!book.has_bid() || !book.has_ask()) return;
    std::fprintf(f_trd_, "%lld,%lld,%d,%lld,%lld,%lld\n",
                 static_cast<long long>(rel / 1'000'000), static_cast<long long>(px),
                 taker_side, static_cast<long long>(qty),
                 static_cast<long long>(book.best_bid()), static_cast<long long>(book.best_ask()));
    ++n_trd_;
  }

  void on_gap(Nanos gap_ns) { std::fprintf(f_arr_, "%lld\n", static_cast<long long>(gap_ns / 1000)); }

  void on_grid(const OrderBook& book, Nanos rel) {
    if (!book.has_bid() || !book.has_ask()) return;
    std::fprintf(f_mid_, "%lld,%lld,%lld,%lld,%lld\n", static_cast<long long>(rel / 1'000'000),
                 static_cast<long long>(book.best_bid()), static_cast<long long>(book.best_ask()),
                 static_cast<long long>(book.best_bid_qty()),
                 static_cast<long long>(book.best_ask_qty()));
    ++n_mid_;
    Ticks px_buf[kProfileDepth];
    Qty   qty_buf[kProfileDepth];
    for (int s = 0; s < 2; ++s) {
      const Side side = (s == 0) ? Side::Bid : Side::Ask;
      const Ticks same = (s == 0) ? book.best_bid() : book.best_ask();
      const std::uint32_t n = book.depth(side, kProfileDepth, px_buf, qty_buf);
      for (std::uint32_t k = 0; k < n; ++k) {
        const Ticks dist = (s == 0) ? (same - px_buf[k]) : (px_buf[k] - same);
        if (dist < 0 || dist >= static_cast<Ticks>(kProfileDepth)) continue;
        const std::size_t idx = static_cast<std::size_t>(s) * kProfileDepth
                              + static_cast<std::size_t>(dist);
        prof_sum_[idx] += static_cast<double>(qty_buf[k]);
        ++prof_n_[idx];
      }
    }
  }

  void write_depth(std::FILE* f) const {
    std::fprintf(f, "side,dist_ticks,mean_qty,samples\n");
    for (int s = 0; s < 2; ++s)
      for (std::uint32_t k = 0; k < kProfileDepth; ++k) {
        const std::size_t i = static_cast<std::size_t>(s) * kProfileDepth + k;
        if (prof_n_[i] == 0) continue;
        std::fprintf(f, "%d,%u,%.1f,%llu\n", s, k, prof_sum_[i] / static_cast<double>(prof_n_[i]),
                     static_cast<unsigned long long>(prof_n_[i]));
      }
  }

  [[nodiscard]] std::uint64_t orders() const noexcept { return n_ord_; }
  [[nodiscard]] std::uint64_t trades() const noexcept { return n_trd_; }
  [[nodiscard]] std::uint64_t mids()   const noexcept { return n_mid_; }

 private:
  void close(const Rec& r, Nanos rel) {
    const Qty cancelled = std::max<Qty>(0, r.size - r.filled);
    std::fprintf(f_ord_, "%lld,%lld,%d,%lld,%lld,%lld,%lld,%lld,%lld\n",
                 static_cast<long long>(r.ts / 1'000'000),
                 static_cast<long long>((rel - r.ts) / 1'000'000),
                 r.side, static_cast<long long>(r.dist),
                 static_cast<long long>(r.spread), static_cast<long long>(r.ahead),
                 static_cast<long long>(r.size), static_cast<long long>(r.filled),
                 static_cast<long long>(cancelled));
    ++n_ord_;
  }

  std::FILE *f_ord_, *f_trd_, *f_mid_, *f_arr_;
  std::unordered_map<OrderId, Rec> live_;
  std::vector<double>        prof_sum_ = std::vector<double>(2 * kProfileDepth, 0.0);
  std::vector<std::uint64_t> prof_n_   = std::vector<std::uint64_t>(2 * kProfileDepth, 0);
  std::uint64_t n_ord_ = 0, n_trd_ = 0, n_mid_ = 0;
};

bool slurp(const char* path, std::string& out) {
  LineReader r;
  if (!r.open(path)) { std::fprintf(stderr, "%s\n", r.error().c_str()); return false; }
  std::string_view l;
  while (r.next(&l)) out.append(l.data(), l.size());
  return !out.empty();
}

std::string snapshot_beside(const std::string& capture) {
  std::string b = capture;
  if (b.size() > 3 && b.compare(b.size() - 3, 3, ".gz") == 0) b.resize(b.size() - 3);
  const std::size_t at = b.rfind("_bitstamp.jsonl");
  return at == std::string::npos ? std::string{} : b.substr(0, at) + "_snapshot.json";
}

std::string pair_of(const std::string& p) {
  const std::size_t s = p.find_last_of("/\\");
  std::string f = (s == std::string::npos) ? p : p.substr(s + 1);
  const std::size_t u = f.find('_');
  return u == std::string::npos ? f : f.substr(0, u);
}

std::FILE* open_out(const std::string& dir, const std::string& pair, const char* name) {
  const std::string p = dir + "/" + pair + "_" + name + ".csv";
  std::FILE* f = std::fopen(p.c_str(), "wb");
  if (f == nullptr) std::fprintf(stderr, "cannot write %s\n", p.c_str());
  return f;
}

}  // namespace

int main(int argc, char** argv) {
  const char* capture = nullptr;
  std::string dir = ".", label;
  double warmup_sec = 60.0, grid_ms = 100.0;
  int synthetic = 0;
  // Negative leaves the FlowConfig default in place; see apps/evaluate for why
  // a tool holding its own copy of a default is a way to measure a process
  // nobody configured.
  double drift = -1.0, informed = -1.0;
  std::uint64_t seed = 20260904;

  for (int i = 1; i < argc; ++i) {
    const bool nx = (i + 1 < argc);
    if      (std::strcmp(argv[i], "--capture") == 0 && nx) capture = argv[++i];
    else if (std::strcmp(argv[i], "--outdir")  == 0 && nx) dir = argv[++i];
    else if (std::strcmp(argv[i], "--warmup")  == 0 && nx) warmup_sec = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--grid-ms") == 0 && nx) grid_ms = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--synthetic") == 0 && nx) synthetic = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--seed")    == 0 && nx) seed = std::strtoull(argv[++i], nullptr, 10);
    else if (std::strcmp(argv[i], "--drift")   == 0 && nx) drift = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--informed") == 0 && nx) informed = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--label")   == 0 && nx) label = argv[++i];
    else {
      std::fprintf(stderr,
        "stats --capture <file> | --synthetic <n_events>   [--outdir .] [--warmup 60]\n"
        "      [--grid-ms 100] [--seed N] [--label NAME]\n"
        "\n"
        "Writes orders/trades/mid/depth/arrivals CSVs. --synthetic measures the\n"
        "simulator's own flow instead of a capture, which is what Phase 5's\n"
        "evaluation calibrates against.\n");
      return 2;
    }
  }
  if (capture == nullptr && synthetic <= 0) {
    std::fprintf(stderr, "one of --capture or --synthetic is required\n");
    return 2;
  }

  const std::string name = !label.empty() ? label
                         : (capture ? pair_of(capture) : std::string("synthetic"));
  std::FILE* f_ord = open_out(dir, name, "orders");
  std::FILE* f_trd = open_out(dir, name, "trades");
  std::FILE* f_mid = open_out(dir, name, "mid");
  std::FILE* f_dep = open_out(dir, name, "depth");
  std::FILE* f_arr = open_out(dir, name, "arrivals");
  if (!f_ord || !f_trd || !f_mid || !f_dep || !f_arr) return 2;
  std::fprintf(f_ord, "t_ms,lifetime_ms,side,dist_ticks,spread_ticks,q_ahead,size,filled,cancelled\n");
  std::fprintf(f_trd, "t_ms,px,side,qty,bid,ask\n");
  std::fprintf(f_mid, "t_ms,bid,ask,bid_qty,ask_qty\n");
  std::fprintf(f_arr, "gap_us\n");

  Accumulator acc{f_ord, f_trd, f_mid, f_arr};
  const Nanos warm = static_cast<Nanos>(warmup_sec * 1e9);
  const Nanos grid = static_cast<Nanos>(grid_ms * 1e6);

  if (synthetic > 0) {
    // ---- the simulator's own process ----
    // Driven exactly as Simulator drives it, including routing aggressive flow
    // through the matcher: without that path nothing ever fills passively, and
    // a fill rate of zero is not a measurement of this process.
    FlowConfig fc;
    fc.seed = seed; fc.mid = 10'000; fc.levels = 8; fc.target_live = 4'000;
    if (drift >= 0.0)    fc.drift_prob    = drift;
    if (informed >= 0.0) fc.informed_frac = informed;
    FlowGenerator gen{fc};
    OrderBook book{5'000, 10'240, 1 << 18};
    MatchingEngine match{book};

    Nanos first_ts = 0, prev_ts = 0, next_grid = 0;
    std::size_t fills_seen = 0;
    for (int i = 0; i < synthetic; ++i) {
      const BookEvent e = gen.next();
      if (first_ts == 0) { first_ts = e.ts; next_grid = e.ts; }
      const Nanos rel = e.ts - first_ts;
      const bool warmed = rel >= warm;
      if (warmed && prev_ts != 0 && e.ts > prev_ts) acc.on_gap(e.ts - prev_ts);
      prev_ts = e.ts;

      if (e.type == EventType::Aggress) {
        (void)match.submit_market(e.ts, e.order_id, e.side, e.qty, /*mine=*/false);
        for (std::size_t k = fills_seen; k < match.fills().size(); ++k) {
          const Fill& f = match.fills()[k];
          const bool gone = book.qty_of(f.resting_id) == 0;
          if (warmed) {
            acc.on_consumed(f.resting_id, f.qty, rel, gone);
            // The taker's side is the opposite of the resting order's.
            acc.on_trade(book, rel, f.price, f.resting_side == Side::Bid ? 0 : 1, f.qty);
          }
          if (gone) gen.forget_order(f.resting_id);
        }
        fills_seen = match.fills().size();
      } else {
        acc.before_apply(book, e, rel, warmed);
        (void)book.apply(e);
        if (warmed) acc.after_apply(book, e, rel);
      }
      gen.on_applied(e, book.qty_of(e.order_id));
      gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());

      if (e.ts >= next_grid) {
        next_grid = e.ts + grid;
        if (warmed) acc.on_grid(book, rel);
      }
    }
  } else {
    // ---- a recorded venue capture ----
    std::string snap;
    if (!slurp(snapshot_beside(capture).c_str(), snap)) return 2;

    BitstampConfig cfg;
    if (!BitstampDecoder::detect_decimals(snap, &cfg.price_decimals, &cfg.qty_decimals)) return 2;
    Ticks bb = 0, ba = 0;
    if (!BitstampDecoder::snapshot_touch(snap, cfg, &bb, &ba)) return 2;
    const Ticks mid0 = (bb + ba) / 2;
    Ticks band = (mid0 / 25) & ~Ticks{63};
    if (band < 4096) band = 4096;
    cfg.window_ticks = band;
    cfg.window_base  = mid0 - band / 2;
    cfg.seed_guard   = static_cast<Ticks>(static_cast<double>(mid0) * 0.003);

    BitstampDecoder dec{cfg};
    OrderBook book{cfg.window_base, static_cast<std::uint32_t>(band), 1 << 21};
    std::vector<BookEvent> seed_ev;
    dec.load_snapshot(snap, seed_ev);
    for (const BookEvent& e : seed_ev) book.apply(e);

    LineReader reader;
    if (!reader.open(capture)) { std::fprintf(stderr, "%s\n", reader.error().c_str()); return 2; }
    std::string_view line;
    Nanos first_ts = 0, prev_ts = 0, next_grid = 0;
    while (reader.next(&line)) {
      const BookTouch touch{book.has_bid(), book.has_ask(),
                            book.has_bid() ? book.best_bid() : 0,
                            book.has_ask() ? book.best_ask() : 0};
      Decoded d;
      if (!dec.decode_line(line, touch, d)) continue;
      if (d.ts != 0 && first_ts == 0) { first_ts = d.ts; next_grid = d.ts; }
      const Nanos rel = (first_ts == 0) ? 0 : d.ts - first_ts;
      const bool warmed = rel >= warm;

      if (warmed && d.ts != 0 && prev_ts != 0 && d.ts > prev_ts) acc.on_gap(d.ts - prev_ts);
      if (d.ts != 0) prev_ts = d.ts;

      if (d.is_trade && d.trade_qty > 0 && warmed)
        acc.on_trade(book, rel, d.trade_price, d.taker == Side::Bid ? 0 : 1, d.trade_qty);

      for (int i = 0; i < d.n; ++i) {
        acc.before_apply(book, d.ev[i], rel, warmed);
        book.apply(d.ev[i]);
        if (warmed) acc.after_apply(book, d.ev[i], rel);
      }

      if (d.ts != 0 && d.ts >= next_grid) {
        next_grid = d.ts + grid;
        if (warmed) acc.on_grid(book, rel);
      }
    }
    const auto& ds = dec.stats();
    std::fprintf(stderr, "  chain gaps %llu, size violations %llu\n",
                 static_cast<unsigned long long>(ds.chain_gaps),
                 static_cast<unsigned long long>(ds.size_violations));
  }

  acc.write_depth(f_dep);
  for (std::FILE* f : {f_ord, f_trd, f_mid, f_dep, f_arr}) std::fclose(f);
  std::fprintf(stderr, "%s: %llu orders, %llu trades, %llu mid samples\n", name.c_str(),
               static_cast<unsigned long long>(acc.orders()),
               static_cast<unsigned long long>(acc.trades()),
               static_cast<unsigned long long>(acc.mids()));
  return 0;
}
