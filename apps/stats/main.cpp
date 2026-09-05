// Emits the measurement plane's raw records as CSV, for analysis and plotting.
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
  std::string dir = ".";
  double warmup_sec = 60.0, grid_ms = 100.0;

  for (int i = 1; i < argc; ++i) {
    const bool nx = (i + 1 < argc);
    if      (std::strcmp(argv[i], "--capture") == 0 && nx) capture = argv[++i];
    else if (std::strcmp(argv[i], "--outdir")  == 0 && nx) dir = argv[++i];
    else if (std::strcmp(argv[i], "--warmup")  == 0 && nx) warmup_sec = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--grid-ms") == 0 && nx) grid_ms = std::atof(argv[++i]);
    else {
      std::fprintf(stderr, "stats --capture <file> [--outdir .] [--warmup 60] [--grid-ms 100]\n");
      return 2;
    }
  }
  if (capture == nullptr) { std::fprintf(stderr, "--capture is required\n"); return 2; }

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

  std::vector<BookEvent> seed;
  dec.load_snapshot(snap, seed);
  for (const BookEvent& e : seed) book.apply(e);

  const std::string pair = pair_of(capture);
  std::FILE* f_ord = open_out(dir, pair, "orders");
  std::FILE* f_trd = open_out(dir, pair, "trades");
  std::FILE* f_mid = open_out(dir, pair, "mid");
  std::FILE* f_dep = open_out(dir, pair, "depth");
  std::FILE* f_arr = open_out(dir, pair, "arrivals");
  if (!f_ord || !f_trd || !f_mid || !f_dep || !f_arr) return 2;

  std::fprintf(f_ord, "t_ms,lifetime_ms,side,dist_ticks,spread_ticks,q_ahead,size,filled,cancelled\n");
  std::fprintf(f_trd, "t_ms,px,side,qty,bid,ask\n");
  std::fprintf(f_mid, "t_ms,bid,ask,bid_qty,ask_qty\n");
  std::fprintf(f_arr, "gap_us\n");

  std::unordered_map<OrderId, Rec> live;
  // Mean resting size by tick distance from the touch, both sides.
  std::vector<double> prof_sum(2 * kProfileDepth, 0.0);
  std::vector<std::uint64_t> prof_n(2 * kProfileDepth, 0);
  // Only orders that arrived AFTER the warm-up are measurable: one that was
  // seeded or was already resting has no observable arrival time, and counting
  // it would bias every lifetime downward.
  std::unordered_map<OrderId, char> measurable;

  LineReader reader;
  if (!reader.open(capture)) { std::fprintf(stderr, "%s\n", reader.error().c_str()); return 2; }

  Ticks px_buf[kProfileDepth];
  Qty   qty_buf[kProfileDepth];
  std::string_view line;
  Nanos first_ts = 0, prev_ts = 0, next_grid = 0;
  const Nanos warm = static_cast<Nanos>(warmup_sec * 1e9);
  const Nanos grid = static_cast<Nanos>(grid_ms * 1e6);
  std::uint64_t n_ord = 0, n_trd = 0, n_mid = 0;

  while (reader.next(&line)) {
    const BookTouch touch{book.has_bid(), book.has_ask(),
                          book.has_bid() ? book.best_bid() : 0,
                          book.has_ask() ? book.best_ask() : 0};
    Decoded d;
    if (!dec.decode_line(line, touch, d)) continue;
    if (d.ts != 0 && first_ts == 0) { first_ts = d.ts; next_grid = d.ts; }
    const Nanos rel = (first_ts == 0) ? 0 : d.ts - first_ts;
    const bool warmed = rel >= warm;

    if (warmed && d.ts != 0 && prev_ts != 0 && d.ts > prev_ts)
      std::fprintf(f_arr, "%lld\n", static_cast<long long>((d.ts - prev_ts) / 1000));
    if (d.ts != 0) prev_ts = d.ts;

    if (d.is_trade && d.trade_qty > 0 && warmed && book.has_bid() && book.has_ask()) {
      std::fprintf(f_trd, "%lld,%lld,%d,%lld,%lld,%lld\n",
                   static_cast<long long>(rel / 1'000'000), static_cast<long long>(d.trade_price),
                   d.taker == Side::Bid ? 0 : 1, static_cast<long long>(d.trade_qty),
                   static_cast<long long>(book.best_bid()), static_cast<long long>(book.best_ask()));
      ++n_trd;
    }

    for (int i = 0; i < d.n; ++i) {
      const BookEvent& e = d.ev[i];
      const bool had_touch = book.has_bid() && book.has_ask();

      if (e.type == EventType::Add && warmed && had_touch) {
        const Ticks same = (e.side == Side::Bid) ? book.best_bid() : book.best_ask();
        Rec r;
        r.ts = rel; r.px = e.price; r.size = e.qty; r.side = (e.side == Side::Bid) ? 0 : 1;
        // Positive means behind the touch, which is where a passive order sits.
        r.dist   = (e.side == Side::Bid) ? (same - e.price) : (e.price - same);
        // Avellaneda-Stoikov measures the quote's distance from the REFERENCE
        // price, not from the same-side touch, so the spread has to travel with
        // the record or delta cannot be reconstructed later.
        r.spread = book.best_ask() - book.best_bid();
        // Read BEFORE the event is applied: this is the volume that has to be
        // consumed or cancelled before a print can reach this order. In a book
        // whose spread is one tick 99% of the time, this — not distance —
        // is what decides whether a passive order ever trades.
        r.ahead  = book.qty_at(e.side, e.price);
        live[e.order_id] = r;
        measurable[e.order_id] = 1;
      }

      book.apply(e);

      if (e.type == EventType::Execute) {
        auto it = live.find(e.order_id);
        if (it != live.end()) it->second.filled += e.qty;
      } else if (e.type == EventType::Delete || e.type == EventType::Reduce) {
        auto it = live.find(e.order_id);
        if (it != live.end()) {
          const bool gone = (e.type == EventType::Delete) || book.qty_of(e.order_id) == 0;
          if (gone) {
            const Rec& r = it->second;
            const Qty cancelled = std::max<Qty>(0, r.size - r.filled);
            std::fprintf(f_ord, "%lld,%lld,%d,%lld,%lld,%lld,%lld,%lld,%lld\n",
                         static_cast<long long>(r.ts / 1'000'000),
                         static_cast<long long>((rel - r.ts) / 1'000'000),
                         r.side, static_cast<long long>(r.dist),
                         static_cast<long long>(r.spread), static_cast<long long>(r.ahead),
                         static_cast<long long>(r.size), static_cast<long long>(r.filled),
                         static_cast<long long>(cancelled));
            ++n_ord;
            live.erase(it);
            measurable.erase(e.order_id);
          }
        }
      }
    }

    // ---- grid samples ----
    if (d.ts != 0 && d.ts >= next_grid) {
      next_grid = d.ts + grid;
      if (warmed && book.has_bid() && book.has_ask()) {
        // Touch sizes as well as prices: the MDP's state carries imbalance, and
        // it cannot be recovered from prices alone.
        std::fprintf(f_mid, "%lld,%lld,%lld,%lld,%lld\n", static_cast<long long>(rel / 1'000'000),
                     static_cast<long long>(book.best_bid()), static_cast<long long>(book.best_ask()),
                     static_cast<long long>(book.best_bid_qty()),
                     static_cast<long long>(book.best_ask_qty()));
        ++n_mid;
        for (int s = 0; s < 2; ++s) {
          const Side side = (s == 0) ? Side::Bid : Side::Ask;
          const Ticks same = (s == 0) ? book.best_bid() : book.best_ask();
          const std::uint32_t n = book.depth(side, kProfileDepth, px_buf, qty_buf);
          for (std::uint32_t k = 0; k < n; ++k) {
            const Ticks dist = (s == 0) ? (same - px_buf[k]) : (px_buf[k] - same);
            if (dist < 0 || dist >= static_cast<Ticks>(kProfileDepth)) continue;
            const std::size_t idx = static_cast<std::size_t>(s) * kProfileDepth
                                  + static_cast<std::size_t>(dist);
            prof_sum[idx] += static_cast<double>(qty_buf[k]);
            ++prof_n[idx];
          }
        }
      }
    }
  }

  std::fprintf(f_dep, "side,dist_ticks,mean_qty,samples\n");
  for (int s = 0; s < 2; ++s)
    for (std::uint32_t k = 0; k < kProfileDepth; ++k) {
      const std::size_t i = static_cast<std::size_t>(s) * kProfileDepth + k;
      if (prof_n[i] == 0) continue;
      std::fprintf(f_dep, "%d,%u,%.1f,%llu\n", s, k, prof_sum[i] / static_cast<double>(prof_n[i]),
                   static_cast<unsigned long long>(prof_n[i]));
    }

  for (std::FILE* f : {f_ord, f_trd, f_mid, f_dep, f_arr}) std::fclose(f);
  const auto& ds = dec.stats();
  std::fprintf(stderr, "%s: %llu orders, %llu trades, %llu mid samples"
                       " | chain gaps %llu, size violations %llu\n",
               pair.c_str(), static_cast<unsigned long long>(n_ord),
               static_cast<unsigned long long>(n_trd), static_cast<unsigned long long>(n_mid),
               static_cast<unsigned long long>(ds.chain_gaps),
               static_cast<unsigned long long>(ds.size_violations));
  return 0;
}
