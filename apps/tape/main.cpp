// Emits a replay tape: real book state, frame by frame, as JSON.
//
// This is the bridge between the C++ pipeline and anything that draws. The
// rule it exists to enforce is that a picture of a book must be a picture of
// THIS book — every level, every size, every queue position on screen came out
// of the same OrderBook the tests run against, replaying a real Bitstamp
// capture. A dashboard drawing invented numbers is worse than no dashboard,
// because the first person to look closely stops believing the rest.
//
// The frame is emitted only when something visible changed, so a quiet market
// costs bytes proportional to what actually happened rather than to wall time.
//
// OUR OWN ORDER
//
// A passive order is placed at the touch and left there, and its queue position
// is read from the book on every frame. It rests in the SAME intrusive FIFO as
// every other order at that level — that is the decision the whole project
// turns on, and it is why queue_ahead() is a field lookup rather than a
// bookkeeping system that has to be kept in sync with reality.
//
// Two honest caveats, both printed into the tape's own metadata:
//
//   Our order adds size the real market never saw. Nobody reacted to it, so
//   this is a shadow order, not a counterfactual.
//
//   A real feed's executes name specific order ids, and never ours. So a fill
//   is not observed, it is DERIVED: volume that traded at our price while we
//   were queued is accumulated, and the moment it exceeds the size that was
//   ahead of us at placement, we would have been filled. That is the standard
//   construction and it is stated rather than presented as an observation.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "lob/book/order_book.hpp"
#include "lob/feat/features.hpp"
#include "lob/feed/bitstamp.hpp"
#include "lob/feed/line_reader.hpp"

using namespace lob;

namespace {

constexpr std::uint32_t kMaxDepth = 24;   // ceiling on levels per side

struct Trade { Ticks px; Qty qty; int side; };

bool slurp(const char* path, std::string& out) {
  LineReader r;
  if (!r.open(path)) { std::fprintf(stderr, "%s\n", r.error().c_str()); return false; }
  std::string_view line;
  while (r.next(&line)) out.append(line.data(), line.size());
  return !out.empty();
}

std::string snapshot_beside(const std::string& capture) {
  std::string base = capture;
  if (base.size() > 3 && base.compare(base.size() - 3, 3, ".gz") == 0) base.resize(base.size() - 3);
  const std::size_t at = base.rfind("_bitstamp.jsonl");
  if (at == std::string::npos) return {};
  return base.substr(0, at) + "_snapshot.json";
}

std::string pair_from(const std::string& path) {
  std::size_t slash = path.find_last_of("/\\");
  std::string f = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const std::size_t us = f.find('_');
  return us == std::string::npos ? f : f.substr(0, us);
}

}  // namespace

int main(int argc, char** argv) {
  const char* capture = nullptr;
  const char* out_path = nullptr;
  double start_sec = 120.0, span_sec = 90.0, warmup_sec = 60.0;
  double fps = 10.0;            // frames per second of market time
  Ticks requote_ticks = 6;      // pull and re-place once the touch is this far away
  std::uint32_t depth = 10;     // levels per side written to the tape
  Qty own_qty = 0;              // 0 = derive from the touch

  for (int i = 1; i < argc; ++i) {
    const bool has_next = (i + 1 < argc);
    if      (std::strcmp(argv[i], "--capture") == 0 && has_next) capture  = argv[++i];
    else if (std::strcmp(argv[i], "--out")     == 0 && has_next) out_path = argv[++i];
    else if (std::strcmp(argv[i], "--start")   == 0 && has_next) start_sec = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--seconds") == 0 && has_next) span_sec  = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--own-qty") == 0 && has_next) own_qty   = std::atoll(argv[++i]);
    else if (std::strcmp(argv[i], "--warmup")  == 0 && has_next) warmup_sec = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--fps")     == 0 && has_next) fps = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--requote-ticks") == 0 && has_next)
      requote_ticks = std::atoll(argv[++i]);
    else if (std::strcmp(argv[i], "--depth") == 0 && has_next)
      depth = std::min(kMaxDepth, static_cast<std::uint32_t>(std::atoi(argv[++i])));
    else {
      std::fprintf(stderr,
        "tape --capture <file> [options]\n"
        "\n"
        "Replays a Bitstamp capture and writes the book, frame by frame, as JSON.\n"
        "The book warms up from the start of the capture; frames begin at --start.\n"
        "\n"
        "  --out <file>          default stdout\n"
        "  --start <s>           first second of market time to record (120)\n"
        "  --seconds <s>         how long to record (90)\n"
        "  --warmup <s>          book fills in from the stream for this long (60)\n"
        "  --fps <n>             frames per second of market time (10). A trade always\n"
        "                        forces a frame, so prints are never dropped between them.\n"
        "  --requote-ticks <n>   pull and re-place our own order once the touch is this\n"
        "                        far from it (6). Smaller means more re-quoting and a\n"
        "                        shorter queue-position series.\n"
        "  --own-qty <n>         our order size, in 10^-qty_decimals units\n"
        "  --depth <n>           levels per side written per frame (10, max 24)\n");
      return 2;
    }
  }
  if (capture == nullptr) { std::fprintf(stderr, "--capture is required\n"); return 2; }

  const std::string snap_path = snapshot_beside(capture);
  std::string snap;
  if (!slurp(snap_path.c_str(), snap)) { std::fprintf(stderr, "no snapshot beside capture\n"); return 2; }

  BitstampConfig cfg;
  if (!BitstampDecoder::detect_decimals(snap, &cfg.price_decimals, &cfg.qty_decimals)) {
    std::fprintf(stderr, "cannot read quoting precision from the snapshot\n");
    return 2;
  }
  Ticks bb0 = 0, ba0 = 0;
  if (!BitstampDecoder::snapshot_touch(snap, cfg, &bb0, &ba0)) {
    std::fprintf(stderr, "snapshot has no two-sided book\n");
    return 2;
  }
  const Ticks mid0 = (bb0 + ba0) / 2;
  Ticks band = (mid0 / 25) & ~Ticks{63};
  if (band < 4096) band = 4096;
  cfg.window_ticks = band;
  cfg.window_base  = mid0 - band / 2;
  // Deep snapshot levels are seeded; everything near the touch comes from the
  // stream. See BitstampDecoder::load_snapshot for the measurement behind 0.3%.
  cfg.seed_guard   = static_cast<Ticks>(static_cast<double>(mid0) * 0.003);

  BitstampDecoder dec{cfg};
  OrderBook book{cfg.window_base, static_cast<std::uint32_t>(band), 1 << 21};
  FeatureEngine fe;

  std::vector<BookEvent> seed;
  dec.load_snapshot(snap, seed);
  for (const BookEvent& e : seed) book.apply(e);

  LineReader reader;
  if (!reader.open(capture)) { std::fprintf(stderr, "%s\n", reader.error().c_str()); return 2; }

  std::FILE* out = (out_path == nullptr) ? stdout : std::fopen(out_path, "wb");
  if (out == nullptr) { std::fprintf(stderr, "cannot write %s\n", out_path); return 2; }

  // ---- own order state ----
  // Far above any exchange id in these captures, so it can never collide.
  constexpr OrderId kOwnId = 0xFFFF'FFFF'FFFF'0001ULL;
  bool  own_live = false;
  Ticks own_px = 0;
  Qty   own_size = 0;
  Qty   own_ahead0 = 0;        // size queued ahead of us at placement
  Qty   own_traded_ahead = 0;  // volume of prints that reached past the queue to us
  bool  own_hit = false;       // a print got through to our order
  int   own_fills = 0, own_placements = 0;
  Nanos own_placed_ts = 0;
  // How long each derived fill took from placement. Reported below; it used to
  // be accumulated and never read.
  std::vector<Nanos> fill_latencies;

  std::string_view line;
  Nanos first_ts = 0;
  const Nanos start_ns = static_cast<Nanos>(start_sec * 1e9);
  const Nanos span_ns  = static_cast<Nanos>(span_sec * 1e9);
  const Nanos warmup   = static_cast<Nanos>(warmup_sec * 1e9);
  // Guarded against negative as well as zero: a negative fps gave a negative
  // frame_gap, so next_frame_at was always in the past and every event emitted
  // a frame.
  const Nanos frame_gap = static_cast<Nanos>(1e9 / (fps > 0.0 ? fps : 10.0));
  Nanos next_frame_at = 0;

  std::vector<Trade> pending;
  std::uint64_t frames = 0;

  std::fprintf(out, "{\n");
  std::fprintf(out, "  \"pair\": \"%s\",\n", pair_from(capture).c_str());
  std::fprintf(out, "  \"price_decimals\": %u,\n  \"qty_decimals\": %u,\n",
               cfg.price_decimals, cfg.qty_decimals);
  std::fprintf(out, "  \"depth\": %u,\n", depth);
  std::fprintf(out, "  \"source\": \"Bitstamp live_orders + live_trades, replayed through lob::OrderBook\",\n");
  std::fprintf(out, "  \"own_order_note\": \"Shadow order: it adds size the real market never saw and nobody reacted to it. Fills are DERIVED from volume traded at our price while queued, not observed — a real feed's executes never name our id.\",\n");
  std::fprintf(out, "  \"frames\": [\n");

  Ticks px_buf[kMaxDepth];
  Qty   qty_buf[kMaxDepth];

  while (reader.next(&line)) {
    const BookTouch touch{book.has_bid(), book.has_ask(),
                          book.has_bid() ? book.best_bid() : 0,
                          book.has_ask() ? book.best_ask() : 0};
    Decoded d;
    if (!dec.decode_line(line, touch, d)) continue;
    if (d.ts != 0 && first_ts == 0) first_ts = d.ts;
    const Nanos rel = (first_ts == 0) ? 0 : d.ts - first_ts;

    if (d.is_trade && d.trade_qty > 0) {
      pending.push_back(Trade{d.trade_price, d.trade_qty, d.taker == Side::Bid ? 0 : 1});
      // A print at our price fills us when it is larger than what is still
      // queued in front of us AT THAT MOMENT.
      //
      // The earlier version accumulated traded volume and compared it to the
      // queue as it stood at placement. That can never fire once the queue
      // drains by CANCELLATION, which is how it almost always drains here —
      // 99% of removals are cancels. Reading queue_ahead() at the moment of the
      // print is what makes cancels count as the progress they are, and it is
      // the whole reason our order rests in the same FIFO as everyone else's.
      if (own_live && d.trade_price == own_px) {
        const Qty ahead_now = book.queue_ahead(kOwnId);
        if (d.trade_qty > ahead_now) {
          own_traded_ahead += d.trade_qty - ahead_now;
          own_hit = true;
        }
      }
    }

    for (int i = 0; i < d.n; ++i) {
      book.apply(d.ev[i]);
      fe.update(book, d.ev[i].ts);
    }

    if (rel < warmup || !book.has_bid() || !book.has_ask()) continue;

    // ---- place / refresh our own order ----
    if (!own_live) {
      own_px    = book.best_bid();
      own_ahead0 = book.best_bid_qty();
      own_size  = (own_qty > 0) ? own_qty : std::max<Qty>(1, own_ahead0 / 20);
      if (book.add(kOwnId, Side::Bid, own_px, own_size, true) == BookError::Ok) {
        own_live = true;
        ++own_placements;
        own_traded_ahead = 0;
        own_hit = false;
        own_placed_ts = rel;
      }
    } else {
      const bool filled = own_hit && own_traded_ahead >= own_size;
      // A quote left far behind the touch is not a quote. But re-placing on
      // every one-tick improvement resets the queue on almost every event and
      // destroys the series this tape exists to show, so the order is held
      // until the touch has genuinely walked away from it.
      const bool stale = (book.best_bid() - own_px) >= requote_ticks;
      if (filled || stale) {
        if (filled) { ++own_fills; fill_latencies.push_back(rel - own_placed_ts); }
        book.remove(kOwnId);
        own_live = false;
      }
    }

    // Clear the trade buffer on the pre-start path too. `pending` was only
    // cleared when a frame was written, and no frame can be written before
    // --start -- so every print between the end of warm-up and --start was held
    // in memory and then dumped into frame zero. With the defaults that is 60
    // seconds of trades in the first frame, and a large --start grows it
    // without bound.
    if (rel < start_ns) { pending.clear(); continue; }
    if (rel > start_ns + span_ns) break;

    // A frame on a fixed grid of market time, so playback runs at a constant
    // rate rather than speeding up whenever the touch happens to be busy. A
    // trade always forces one: a print between grid points must not vanish.
    const Qty ahead = own_live ? book.queue_ahead(kOwnId) : -1;
    if (next_frame_at == 0) next_frame_at = rel;
    if (rel < next_frame_at && pending.empty()) continue;
    next_frame_at = rel + frame_gap;

    if (frames) std::fprintf(out, ",\n");
    std::fprintf(out, "  {\"t\":%lld", static_cast<long long>((rel - start_ns) / 1'000'000));

    for (int s = 0; s < 2; ++s) {
      const Side side = (s == 0) ? Side::Bid : Side::Ask;
      const std::uint32_t n = book.depth(side, depth, px_buf, qty_buf);
      std::fprintf(out, ",\"%c\":[", s == 0 ? 'b' : 'a');
      for (std::uint32_t k = 0; k < n; ++k) {
        std::fprintf(out, "%s[%lld,%lld,%u]", k ? "," : "",
                     static_cast<long long>(px_buf[k]), static_cast<long long>(qty_buf[k]),
                     book.orders_at(side, px_buf[k]));
      }
      std::fprintf(out, "]");
    }

    if (own_live) {
      std::fprintf(out, ",\"m\":[%lld,%lld,%lld,%lld,%lld]",
                   static_cast<long long>(own_px), static_cast<long long>(own_size),
                   static_cast<long long>(ahead), static_cast<long long>(own_ahead0),
                   static_cast<long long>(own_traded_ahead));
    }
    if (!pending.empty()) {
      std::fprintf(out, ",\"x\":[");
      for (std::size_t k = 0; k < pending.size(); ++k)
        std::fprintf(out, "%s[%lld,%lld,%d]", k ? "," : "",
                     static_cast<long long>(pending[k].px),
                     static_cast<long long>(pending[k].qty), pending[k].side);
      std::fprintf(out, "]");
      pending.clear();
    }
    const Features& f = fe.get();
    std::fprintf(out, ",\"i\":%.4f,\"w\":%.2f}", f.imbalance, f.weighted_mid);
    ++frames;
  }

  const auto& ds = dec.stats();
  std::fprintf(out, "\n  ],\n");
  std::fprintf(out, "  \"stats\": {\"frames\":%llu,\"lines\":%llu,\"fills\":%llu,"
                    "\"fill_events\":%llu,\"cancel_events\":%llu,\"filled_qty\":%lld,"
                    "\"cancelled_qty\":%lld,\"marketable\":%llu,\"placements\":%d,\"chain_gaps\":%llu,"
                    "\"size_violations\":%llu}\n",
               static_cast<unsigned long long>(frames),
               static_cast<unsigned long long>(ds.lines),
               static_cast<unsigned long long>(own_fills),
               static_cast<unsigned long long>(ds.fill_events),
               static_cast<unsigned long long>(ds.cancel_events),
               static_cast<long long>(ds.filled_qty),
               static_cast<long long>(ds.cancelled_qty),
               static_cast<unsigned long long>(ds.marketable), own_placements,
               static_cast<unsigned long long>(ds.chain_gaps),
               static_cast<unsigned long long>(ds.size_violations));
  std::fprintf(out, "}\n");
  // Closing is where deferred write errors surface, so a full disk produces a
  // truncated tape reported as a failure rather than as success.
  if (out != stdout && std::fclose(out) != 0) {
    std::fprintf(stderr, "tape: writing %s failed\n", out_path);
    return 1;
  }

  double median_fill_ms = 0.0;
  if (!fill_latencies.empty()) {
    std::sort(fill_latencies.begin(), fill_latencies.end());
    median_fill_ms = static_cast<double>(fill_latencies[fill_latencies.size() / 2]) / 1e6;
  }
  std::fprintf(stderr,
               "tape: %llu frames | own order placed %d times, %d derived fills"
               " | median time to fill %.0f ms\n",
               static_cast<unsigned long long>(frames), own_placements, own_fills,
               median_fill_ms);
  return 0;
}
