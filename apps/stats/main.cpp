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
#include <filesystem>
#include <system_error>
#include <string>
#include <unordered_map>
#include <vector>

#include "lob/book/order_book.hpp"
#include "lob/feat/queue_reactive.hpp"
#include "lob/feed/bitstamp.hpp"
#include "lob/feed/json.hpp"
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

  // The touch is passed in rather than read off the book, and it must be the
  // touch BEFORE the aggressive order started consuming.
  //
  // `dist` is how far past the resting side's touch this print happened: 0 means
  // the top of the book traded, 1 means the sweep went a tick deeper. It is the
  // only honest way to measure how often a quote one tick behind the touch
  // actually trades THERE. Measuring it from where an order was placed counts
  // every order that was placed behind, was later promoted when the touch came
  // to it, and filled at the front — which is not a fill one tick behind the
  // touch at all, and the model already accounts for promotion separately. That
  // double count is worth about a factor of two, and it points the wrong way:
  // it makes quoting behind the touch look better than being at it.
  void on_trade(Ticks pre_bid, Ticks pre_ask, Nanos rel, Ticks px, int taker_side, Qty qty) {
    if (pre_bid <= 0 || pre_ask <= 0) return;
    // Taker buy (0) lifts the ask, so the resting side is the ask.
    const Ticks dist = (taker_side == 0) ? (px - pre_ask) : (pre_bid - px);
    // A print cannot happen in front of the touch it consumed. If it does, the
    // aggressor's side is mislabelled or the touch is stale, and every level
    // measurement built on this column is wrong. Counted, and reported at the
    // end, rather than left to be discovered by a number that looks plausible.
    if (dist < 0) {
      ++n_neg_dist_;
      // HOW FAR in front, which says WHICH fault this is. One tick means the
      // level was there and we had already removed it: the delete for it
      // arrived before the print that caused it, so the touch we compared
      // against had moved on. Many ticks means the level was never in our book
      // at all -- a genuinely missing level.
      //
      // Guessing between those two cost two wrong diagnoses. It is one counter.
      const Ticks d = -dist;
      neg_hist_[d <= 1 ? 0 : d <= 4 ? 1 : d <= 16 ? 2 : 3]++;
    }
    std::fprintf(f_trd_, "%.3f,%lld,%d,%lld,%lld,%lld,%lld\n",
                 static_cast<double>(rel) / 1e6, static_cast<long long>(px),
                 taker_side, static_cast<long long>(qty),
                 static_cast<long long>(pre_bid), static_cast<long long>(pre_ask),
                 static_cast<long long>(dist));
    ++n_trd_;
  }

  void on_gap(Nanos gap_ns) { std::fprintf(f_arr_, "%lld\n", static_cast<long long>(gap_ns / 1000)); }

  void on_grid(const OrderBook& book, Nanos rel) {
    if (!book.has_bid() || !book.has_ask()) return;
    std::fprintf(f_mid_, "%.3f,%lld,%lld,%lld,%lld\n", static_cast<double>(rel) / 1e6,
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

  // A reconnect means we missed messages, so every order still resting is of
  // unknown fate: it may have filled, cancelled, or still be there. It cannot
  // contribute an exposure-and-event pair to a hazard estimate, so it is
  // dropped rather than closed with a guessed outcome. Returns how many, since
  // silently discarding orders is exactly the kind of thing that should be
  // visible in the output. Without this they would also accumulate forever
  // across sessions, which on a long capture is a slow leak.
  std::uint64_t on_session_reset() {
    const std::uint64_t n = live_.size();
    live_.clear();
    n_abandoned_ += n;
    return n;
  }

  [[nodiscard]] std::uint64_t orders() const noexcept { return n_ord_; }
  [[nodiscard]] std::uint64_t abandoned() const noexcept { return n_abandoned_; }
  [[nodiscard]] std::uint64_t prints_before_touch() const noexcept { return n_neg_dist_; }
  [[nodiscard]] const std::uint64_t* before_touch_depth() const noexcept { return neg_hist_; }
  [[nodiscard]] std::uint64_t trades() const noexcept { return n_trd_; }
  [[nodiscard]] std::uint64_t mids()   const noexcept { return n_mid_; }

 private:
  // Times are milliseconds to microsecond precision, NOT integer milliseconds.
  // An order's lifetime is its exposure in a hazard estimate, and 4.3% of these
  // orders live less than a millisecond: rounded down they contributed a fill
  // event with no time at risk, which is a division by zero wearing the shape of
  // an infinite hazard. It also makes any epoch below a millisecond impossible
  // to calibrate — the mid series would have two samples at the same timestamp
  // and half the steps would be discarded as zero-length.
  void close(const Rec& r, Nanos rel) {
    const Qty cancelled = std::max<Qty>(0, r.size - r.filled);
    std::fprintf(f_ord_, "%.3f,%.3f,%d,%lld,%lld,%lld,%lld,%lld,%lld\n",
                 static_cast<double>(r.ts) / 1e6,
                 static_cast<double>(rel - r.ts) / 1e6,
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
  std::uint64_t n_ord_ = 0, n_trd_ = 0, n_mid_ = 0, n_neg_dist_ = 0, n_abandoned_ = 0;
  std::uint64_t neg_hist_[4] = {};   // 1 tick, 2-4, 5-16, 17+
};

bool slurp(const char* path, std::string& out) {
  LineReader r;
  if (!r.open(path)) { std::fprintf(stderr, "%s\n", r.error().c_str()); return false; }
  std::string_view l;
  while (r.next(&l)) out.append(l.data(), l.size());
  return !out.empty();
}

std::string dir_of(const std::string& path) {
  const std::size_t s = path.find_last_of("/\\");
  return s == std::string::npos ? std::string{} : path.substr(0, s + 1);
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
  std::vector<std::string> captures;
  std::string dir = ".", label;
  double warmup_sec = 60.0, grid_ms = 100.0;
  // Half-width of the book's price window, as a fraction of the opening mid.
  //
  // The window is set once from the first snapshot and never recentred, so it
  // has to be wide enough for everything the price does during the capture. The
  // old fixed 2% was sized for a ten-minute sample; over eight hours ETH will
  // routinely leave it, and the events outside are REJECTED -- counted, so it is
  // not silent, but the book then describes a market that stopped existing.
  double band_pct = 0.02;
  int synthetic = 0;
  bool calibrated = false;
  // Negative leaves the FlowConfig default in place; see apps/evaluate for why
  // a tool holding its own copy of a default is a way to measure a process
  // nobody configured.
  double drift = -1.0, informed = -1.0;
  std::uint64_t seed = 20260904;

  for (int i = 1; i < argc; ++i) {
    const bool nx = (i + 1 < argc);
    if      (std::strcmp(argv[i], "--capture") == 0 && nx) captures.emplace_back(argv[++i]);
    else if (std::strcmp(argv[i], "--capture-dir") == 0 && nx) {
      // A long recording rotates hourly, so the natural unit is a directory.
      // Sorted by name, which is sorted by the UTC stamp the recorder embeds.
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator(argv[i + 1], ec)) {
        const std::string n = e.path().filename().string();
        if (n.size() > 18 && n.find("_bitstamp.jsonl") != std::string::npos)
          captures.push_back(e.path().string());
      }
      if (ec) { std::fprintf(stderr, "cannot read %s: %s\n", argv[i + 1], ec.message().c_str()); return 2; }
      std::sort(captures.begin(), captures.end());
      ++i;
    }
    else if (std::strcmp(argv[i], "--band-pct") == 0 && nx) band_pct = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--outdir")  == 0 && nx) dir = argv[++i];
    else if (std::strcmp(argv[i], "--warmup")  == 0 && nx) warmup_sec = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--grid-ms") == 0 && nx) grid_ms = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--synthetic") == 0 && nx) synthetic = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--calibrated") == 0) calibrated = true;
    else if (std::strcmp(argv[i], "--seed")    == 0 && nx) seed = std::strtoull(argv[++i], nullptr, 10);
    else if (std::strcmp(argv[i], "--drift")   == 0 && nx) drift = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--informed") == 0 && nx) informed = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--label")   == 0 && nx) label = argv[++i];
    else {
      std::fprintf(stderr,
        "stats --capture <file> [--capture <file> ...] | --capture-dir <dir>\n"
        "      | --synthetic <n_events>\n"
        "      [--outdir .] [--warmup 60] [--grid-ms 100] [--seed N] [--label NAME]\n"
        "      [--calibrated]   --synthetic only: run the process fitted to the\n"
        "                       captures (FlowConfig::ethusd()) rather than the\n"
        "                       dense, fast one the other apps still use.\n"
        "      [--band-pct 0.02]\n"
        "\n"
        "  --capture-dir   every *_bitstamp.jsonl.gz in a directory, in name order.\n"
        "                  An hours-long recording rotates hourly and is seeded by a\n"
        "                  single snapshot beside the FIRST file.\n"
        "  --band-pct      half-width of the price window, as a fraction of the\n"
        "                  opening mid. Prices outside it are rejected. 2%% is fine\n"
        "                  for ten minutes and much too narrow for eight hours.\n"
        "\n"
        "Writes orders/trades/mid/depth/arrivals CSVs. --synthetic measures the\n"
        "simulator's own flow instead of a capture, which is what Phase 5's\n"
        "evaluation calibrates against.\n");
      return 2;
    }
  }
  if (captures.empty() && synthetic <= 0) {
    std::fprintf(stderr, "one of --capture, --capture-dir or --synthetic is required\n");
    return 2;
  }
  const char* capture = captures.empty() ? nullptr : captures.front().c_str();

  // Create the output directory rather than failing five times over. Every
  // invocation writes five CSVs into it, so a missing directory produced five
  // identical errors and no output -- which reads like a permissions problem
  // rather than the one mkdir it actually is.
  {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
      std::fprintf(stderr, "cannot create %s: %s\n", dir.c_str(), ec.message().c_str());
      return 2;
    }
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
  std::fprintf(f_trd, "t_ms,px,side,qty,bid,ask,dist_ticks\n");
  std::fprintf(f_mid, "t_ms,bid,ask,bid_qty,ask_qty\n");
  std::fprintf(f_arr, "gap_us\n");

  Accumulator acc{f_ord, f_trd, f_mid, f_arr};
  // Queue-reactive intensities (Huang, Lehalle & Rosenbaum). Measured on both
  // sources deliberately: the whole claim about the synthetic generator is that
  // its rates do NOT depend on queue size, and the only way to say that is to
  // measure the same thing on both and put the two curves side by side.
  QueueReactive qr;
  const Nanos warm = static_cast<Nanos>(warmup_sec * 1e9);
  const Nanos grid = static_cast<Nanos>(grid_ms * 1e6);

  if (synthetic > 0) {
    // ---- the simulator's own process ----
    // Driven exactly as Simulator drives it, including routing aggressive flow
    // through the matcher: without that path nothing ever fills passively, and
    // a fill rate of zero is not a measurement of this process.
    // --calibrated selects the process fitted to the captures; without it,
    // the stress process every other app still runs on. Both are worth
    // measuring and the whole point of this tool is to tell them apart.
    FlowConfig fc = calibrated ? FlowConfig::ethusd() : FlowConfig{};
    fc.seed = seed; fc.mid = 10'000;
    if (!calibrated) { fc.levels = 8; fc.target_live = 4'000; }
    if (drift >= 0.0)    fc.drift_prob    = drift;
    if (informed >= 0.0) fc.informed_frac = informed;
    FlowGenerator gen{fc};
    OrderBook book{5'000, 10'240, 1 << 18};
    MatchingEngine match{book};

    Nanos first_ts = 0, prev_ts = 0, next_grid = 0;
    std::size_t fills_seen = 0;
    // Fills arrive by two paths and only one of them is a trade. An Aggress
    // event goes through the matching engine and consumes the front of the
    // queue; an Execute event is fabricated on a uniformly random resting
    // order. Both end up as a fill in every statistic downstream, and the split
    // decides how much of the queue-position signal the second one dilutes.
    std::uint64_t n_matched = 0, n_fabricated = 0;
    Qty q_matched = 0, q_fabricated = 0;
    for (int i = 0; i < synthetic; ++i) {
      const BookEvent e = gen.next();
      if (first_ts == 0) { first_ts = e.ts; next_grid = e.ts; }
      const Nanos rel = e.ts - first_ts;
      const bool warmed = rel >= warm;
      if (warmed && prev_ts != 0 && e.ts > prev_ts) acc.on_gap(e.ts - prev_ts);
      prev_ts = e.ts;

      if (e.type == EventType::Aggress) {
        // Before the sweep. Every print in it is measured against the book the
        // aggressor arrived at, not the one it left behind — the capture path
        // already did this, and reading the touch back out of the consumed book
        // made the two sources disagree about what the columns meant.
        const Ticks pre_bid = book.has_bid() ? book.best_bid() : 0;
        const Ticks pre_ask = book.has_ask() ? book.best_ask() : 0;
        // The queue-reactive table has to see fills from the MATCHING ENGINE,
        // not only the ones the generator fabricates. Hooked to the else-branch
        // alone it saw nothing but fabricated executes -- so with those turned
        // off it reported a book in which no trade ever happens, while stats
        // counted 78,142 of them three lines away.
        //
        // A market order consumes the front of the touch, so the queue state
        // the trade found is the one BEFORE the sweep, exactly as for any other
        // event: read it here, once, rather than after each partial fill has
        // already changed it.
        if (warmed) {
          BookEvent tr{};
          tr.type  = EventType::Execute;
          tr.side  = opposite(e.side);
          tr.price = (tr.side == Side::Bid) ? pre_bid : pre_ask;
          tr.qty   = e.qty;
          if (tr.price > 0) qr.on_event(book, tr);
        }
        (void)match.submit_market(e.ts, e.order_id, e.side, e.qty, /*mine=*/false);
        for (std::size_t k = fills_seen; k < match.fills().size(); ++k) {
          const Fill& f = match.fills()[k];
          const bool gone = book.qty_of(f.resting_id) == 0;
          if (warmed) {
            acc.on_consumed(f.resting_id, f.qty, rel, gone);
            // The taker's side is the OPPOSITE of the resting order's: a
            // resting bid is hit by a seller. This said Bid -> 0 (buy), which
            // labelled every synthetic print with the wrong aggressor and so
            // flipped the sign of the adverse-selection markout computed from
            // them. It surfaced the moment prints were measured against the
            // pre-trade touch: a "taker buy" printing a tick BELOW the ask is
            // not a thing that can happen.
            acc.on_trade(pre_bid, pre_ask, rel, f.price,
                         f.resting_side == Side::Bid ? 1 : 0, f.qty);
          }
          if (gone) gen.forget_order(f.resting_id);
          if (warmed) { ++n_matched; q_matched += f.qty; }
        }
        fills_seen = match.fills().size();
        if (warmed) qr.on_state(book, rel);
      } else {
        if (warmed && e.type == EventType::Execute) {
          ++n_fabricated;
          q_fabricated += e.qty;
        }
        acc.before_apply(book, e, rel, warmed);
        qr.on_event(book, e, warmed);
        (void)book.apply(e);
        if (warmed) { acc.after_apply(book, e, rel); qr.on_state(book, rel); }
      }
      gen.on_applied(e, book.qty_of(e.order_id));
      gen.observe(book.has_bid(), book.best_bid(), book.has_ask(), book.best_ask());

      if (e.ts >= next_grid) {
        next_grid = e.ts + grid;
        if (warmed) acc.on_grid(book, rel);
      }
    }
    {
      const double n = static_cast<double>(n_matched + n_fabricated);
      const double q = static_cast<double>(q_matched + q_fabricated);
      std::fprintf(stderr,
          "  fills by path: %llu matched through the engine (%.1f%% of count, %.1f%% of "
          "volume),\n                 %llu fabricated on a random resting order "
          "(%.1f%%, %.1f%%)\n",
          static_cast<unsigned long long>(n_matched),
          n > 0 ? 100.0 * static_cast<double>(n_matched) / n : 0.0,
          q > 0 ? 100.0 * static_cast<double>(q_matched) / q : 0.0,
          static_cast<unsigned long long>(n_fabricated),
          n > 0 ? 100.0 * static_cast<double>(n_fabricated) / n : 0.0,
          q > 0 ? 100.0 * static_cast<double>(q_fabricated) / q : 0.0);
      std::fprintf(stderr,
          "  Only the matched path consumes the front of a queue, so only it carries any\n"
          "  information about queue position. The fabricated share is how much of the\n"
          "  signal the Phase 5 policy is asked to exploit has been averaged away.\n");
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
    Ticks band = static_cast<Ticks>(static_cast<double>(mid0) * 2.0 * band_pct) & ~Ticks{63};
    if (band < 4096) band = 4096;
    cfg.window_ticks = band;
    cfg.window_base  = mid0 - band / 2;
    cfg.seed_guard   = static_cast<Ticks>(static_cast<double>(mid0) * 0.003);

    BitstampDecoder dec{cfg};
    OrderBook book{cfg.window_base, static_cast<std::uint32_t>(band), 1 << 21};
    std::vector<BookEvent> seed_ev;
    dec.load_snapshot(snap, seed_ev);
    for (const BookEvent& e : seed_ev) book.apply(e);

    std::printf("  window %.1f%% of the opening mid (%lld ticks, base %lld)\n",
                100.0 * band_pct, static_cast<long long>(band),
                static_cast<long long>(cfg.window_base));

    Nanos first_ts = 0, prev_ts = 0, next_grid = 0;
    // The recorder writes a session_start marker at the top of EVERY session,
    // the first included -- and that first one names the very snapshot the book
    // was just seeded from. It is a header, not a boundary. Acting on it
    // reseeded the book from the file it had just been built out of and counted
    // a session that had not ended.
    //
    // So a marker is a boundary only once events have been decoded. That reads
    // correctly for all three shapes: a capture with no markers at all is one
    // session, a single-session capture with its header marker is one, and each
    // genuine reconnect adds one.
    std::uint64_t sessions = 1, boundary_lines = 0;
    bool seen_events = false;
    // How far the TOUCH travelled, which is the only thing that decides whether
    // the window was wide enough. Counting dropped adds does not: a book has
    // orders resting percent away from the mid at all times, they are outside
    // the window by design, and 13.6% of adds land there on a ten-minute sample
    // where nothing is wrong at all.
    Ticks lo_touch = 0, hi_touch = 0;
    for (const std::string& path : captures) {
    LineReader reader;
    if (!reader.open(path.c_str())) { std::fprintf(stderr, "%s\n", reader.error().c_str()); return 2; }
    if (captures.size() > 1)
      std::fprintf(stderr, "  reading %s\n", path.c_str());
    std::string_view line;
    while (reader.next(&line)) {
      // A session boundary. The recorder writes one whenever it had to
      // reconnect, because the gap means messages were missed and the book can
      // no longer be repaired from the stream. Everything resting is stale, so
      // the book is cleared and reseeded from the snapshot named on the line.
      // Ignoring these would splice two different books together and read the
      // join as a price move that never happened.
      if (json::find_scalar(line, "_meta") == "session_start") {
        ++boundary_lines;
        if (!seen_events) continue;   // the first session's own header
        const json::View sname = json::find_scalar(line, "_snapshot");
        std::string snap2;
        if (!sname.empty() && slurp((dir_of(path) + std::string(sname)).c_str(), snap2)) {
          book.clear();
          dec.reset_session();
          std::vector<BookEvent> ev2;
          dec.load_snapshot(snap2, ev2);
          for (const BookEvent& e : ev2) book.apply(e);
          const std::uint64_t dropped = acc.on_session_reset();
          ++sessions;
          std::fprintf(stderr, "  session %llu: reseeded from %.*s"
                               "  (%llu resting orders of unknown fate, dropped)\n",
                       static_cast<unsigned long long>(sessions),
                       static_cast<int>(sname.size()), sname.data(),
                       static_cast<unsigned long long>(dropped));
        } else {
          std::fprintf(stderr, "  \033[31msession boundary with no readable snapshot -- "
                               "the book from here on is not trustworthy\033[0m\n");
        }
        // The gap is a discontinuity in time as well as in state.
        prev_ts = 0;
        continue;
      }
      const BookTouch touch{book.has_bid(), book.has_ask(),
                            book.has_bid() ? book.best_bid() : 0,
                            book.has_ask() ? book.best_ask() : 0};
      Decoded d;
      if (!dec.decode_line(line, touch, d)) continue;
      seen_events = true;
      if (d.ts != 0 && first_ts == 0) { first_ts = d.ts; next_grid = d.ts; }
      const Nanos rel = (first_ts == 0) ? 0 : d.ts - first_ts;
      const bool warmed = rel >= warm;

      if (warmed && d.ts != 0 && prev_ts != 0 && d.ts > prev_ts) acc.on_gap(d.ts - prev_ts);
      if (d.ts != 0) prev_ts = d.ts;

      if (d.is_trade && d.trade_qty > 0 && warmed)
        acc.on_trade(book.has_bid() ? book.best_bid() : 0,
                     book.has_ask() ? book.best_ask() : 0,
                     rel, d.trade_price, d.taker == Side::Bid ? 0 : 1, d.trade_qty);

      for (int i = 0; i < d.n; ++i) {
        acc.before_apply(book, d.ev[i], rel, warmed);
        qr.on_event(book, d.ev[i], warmed);
        book.apply(d.ev[i]);
        if (warmed) { acc.after_apply(book, d.ev[i], rel); qr.on_state(book, rel); }
      }
      if (book.has_bid() && book.has_ask()) {
        if (lo_touch == 0 || book.best_bid() < lo_touch) lo_touch = book.best_bid();
        if (book.best_ask() > hi_touch) hi_touch = book.best_ask();
      }

      if (d.ts != 0 && d.ts >= next_grid) {
        next_grid = d.ts + grid;
        if (warmed) acc.on_grid(book, rel);
      }
    }
    }  // for each capture file
    const auto& ds = dec.stats();
    if (acc.abandoned() > 0)
      std::fprintf(stderr, "  %llu orders spanned a reconnect and were dropped: their fate is "
                           "not in the capture\n",
                   static_cast<unsigned long long>(acc.abandoned()));
    std::fprintf(stderr, "  %zu file(s), %llu session(s), chain gaps %llu, size violations %llu\n",
                 captures.size(), static_cast<unsigned long long>(sessions),
                 static_cast<unsigned long long>(ds.chain_gaps),
                 static_cast<unsigned long long>(ds.size_violations));
    (void)boundary_lines;
    // Orders outside the price window are dropped by the DECODER, before the
    // book ever sees them, so BookError::PriceOutOfWindow stays at zero however
    // badly the window is chosen -- which is why this counter has to come from
    // the decoder's own tally. Checking the book's would have reported a clean
    // run on a capture that lost a fifth of its orders.
    //
    // The window is set once from the opening mid and never recentred. Over ten
    // minutes that is fine; over eight hours the price can simply walk out of
    // it, and everything after that point describes a market that moved on
    // without us.
    if (ds.out_of_window > 0)
      std::fprintf(stderr, "  %llu adds (%.1f%%) rested outside the price window and were "
                           "dropped, plus %llu follow-ups\n",
                   static_cast<unsigned long long>(ds.out_of_window),
                   100.0 * static_cast<double>(ds.out_of_window)
                         / static_cast<double>(ds.created > 0 ? ds.created : 1),
                   static_cast<unsigned long long>(ds.suppressed));
    if (lo_touch > 0 && hi_touch > 0) {
      const Ticks top = cfg.window_base + static_cast<Ticks>(cfg.window_ticks);
      const double head_lo = 100.0 * static_cast<double>(lo_touch - cfg.window_base)
                           / static_cast<double>(cfg.window_ticks);
      const double head_hi = 100.0 * static_cast<double>(top - hi_touch)
                           / static_cast<double>(cfg.window_ticks);
      const double margin = std::min(head_lo, head_hi);
      std::fprintf(stderr, "  touch ranged %lld..%lld, window %lld..%lld "
                           "(%.0f%% headroom below, %.0f%% above)\n",
                   static_cast<long long>(lo_touch), static_cast<long long>(hi_touch),
                   static_cast<long long>(cfg.window_base), static_cast<long long>(top),
                   head_lo, head_hi);
      // Below this the price was close enough to the edge that a slightly
      // different day would have walked out of it, and a touch that leaves the
      // window does not fail loudly -- the book simply stops being told where
      // the market is.
      if (margin < 10.0)
        std::fprintf(stderr, "  \033[31mthe touch came within %.0f%% of the window edge. "
                             "Rerun with a larger --band-pct.\033[0m\n", margin);
    }
  }

  acc.write_depth(f_dep);
  if (std::FILE* f_qr = open_out(dir, name, "qr")) { qr.write(f_qr); std::fclose(f_qr); }
  for (std::FILE* f : {f_ord, f_trd, f_mid, f_dep, f_arr}) std::fclose(f);
  std::fprintf(stderr, "%s: %llu orders, %llu trades, %llu mid samples\n", name.c_str(),
               static_cast<unsigned long long>(acc.orders()),
               static_cast<unsigned long long>(acc.trades()),
               static_cast<unsigned long long>(acc.mids()));
  if (acc.prints_before_touch() > 0) {
    const double share = 100.0 * static_cast<double>(acc.prints_before_touch())
                       / static_cast<double>(acc.trades() + acc.prints_before_touch());
    // A print in front of the touch means the touch we know about is not the
    // real one, and the usual reason is a level we never learned: the book holds
    // only what has churned since recording started, or what the price window
    // let in. On a ten-minute sample that is a few per cent.
    //
    // A large share is the same fault, larger. An eight-hour ethusd capture
    // reported 27.6% with a 2% window whose lower edge the price came within 6%
    // of -- adds near that edge were dropped, so our best ask sat above the real
    // one and every print at the real touch looked like it was in front of ours.
    // An earlier version of this message blamed the aggressor's side at that
    // share; that was a guess, and it was wrong. Widen the window first.
    std::fprintf(stderr, "  \033[%sm%llu prints (%.1f%%) landed in front of the touch they "
                         "consumed: the reconstructed touch was inside the real one.\033[0m\n"
                         "%s",
                 share > 10.0 ? "31" : "33",
                 static_cast<unsigned long long>(acc.prints_before_touch()), share,
                 share > 10.0
                     ? "  At this share the level measurement is not usable.\n"
                     : "  Dropped from the level measurement.\n");
    const std::uint64_t* h = acc.before_touch_depth();
    std::fprintf(stderr, "    how far in front:  1 tick %llu   2-4 %llu   5-16 %llu   17+ %llu\n",
                 static_cast<unsigned long long>(h[0]), static_cast<unsigned long long>(h[1]),
                 static_cast<unsigned long long>(h[2]), static_cast<unsigned long long>(h[3]));
    std::fprintf(stderr, "    Mostly 1 tick means the delete for that level reached us before the\n"
                         "    print did, so the touch had already moved -- a message-ordering race,\n"
                         "    and the prints are simply unusable for the level split. Mostly deep\n"
                         "    means levels genuinely missing from the reconstruction.\n");
  }
  return 0;
}
