// Bitstamp live_orders / live_trades decoder: raw capture line -> BookEvent.
//
// This is the first decoder for a real venue, and it is the only place in the
// project that knows Bitstamp exists. The book, the features, the matcher and
// the strategies see BookEvent and nothing else.
//
// WHAT THE FEED GIVES US, AND WHY IT MATTERS
//
//   amount_at_create   size when the order was first booked
//   amount             size REMAINING right now
//   amount_traded      size filled BY THIS EVENT (not cumulative — see below)
//
// Those three are on every message, so the reason an order shrank or vanished
// is stated by the feed rather than inferred.
//
//   amount_traded is PER EVENT. This was read as cumulative until a real
//   capture disproved it. Order 2046841350975488 took six partial fills of
//   0.00572747, 0.00405924, 0.05969432, 0.00784165, 0.02010227 and 0.02334723,
//   summing to exactly the 0.12077218 that left the book, and was then deleted
//   with the remaining 0.00422782 cancelled and amount_traded 0. Under a
//   cumulative reading that order's final traded size is zero. Across the
//   capture, the SUM of amount_traded matched the size consumed for 84.8% of
//   completed orders against 50.5% for the last value — and the residual is
//   amend-downs, which are cancels rather than fills.
//
//   Consequence: amount + amount_traded == amount_at_create holds only for an
//   order with exactly one trading event and no amend. It is not an invariant
//   and is not checked as one. What IS invariant is that no more size can leave
//   an order than it had: see size_violations. That is the difference between
// measuring the queue and guessing at it: volume cancelled ahead of you is free
// progress up the queue, volume TRADED ahead of you is progress plus the
// information that someone is buying, and a model that aggregates them is
// modelling neither. docs/03 §5 says this split must never be collapsed; here
// is where it is recovered.
//
//   This is better than the join this project originally planned. Matching
//   order_deleted against live_trades on order id is fragile, and on this feed
//   it would also be WRONG: the two channels are not on the same clock. In one
//   capture the order channel ran ~570ms behind local time while the trade
//   channel ran ~90ms ahead of it. amount_traded needs no clock at all.
//
//   event_id / pre_event_id   a chain: each message names its predecessor
//
// So a gap is detectable rather than merely suspected. A capture that chains
// end to end is provably whole, which is what Phase 1's acceptance criterion
// ("a full session with zero invariant violations") is worth stating about.
//
// TIMESTAMPS
//
// microtimestamp is microseconds, but the order channel's values are always
// whole milliseconds — the resolution is 1ms wearing a microsecond coat. Many
// events therefore share a timestamp, and intra-millisecond order comes from
// the chain, never from the clock. Any duration measured from these numbers is
// quantised to 1ms and must be reported that way.
//
// _recv_ns is added by tools/record_bitstamp.py: local CLOCK_REALTIME at the
// moment the frame was read. Against an unsynchronised local clock its absolute
// value is meaningless, but its variation is not — that is feed jitter, and it
// is the only latency in this pipeline that comes from the real world.
#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "lob/book/events.hpp"
#include "lob/core/types.hpp"

namespace lob {

struct BitstampConfig {
  // Ticks are 10^-price_decimals of the quote currency. Bitstamp quotes BTC/USD
  // to the cent, so 2 -> a $0.01 tick and a price of 79715.38 becomes tick
  // 7'971'538. Every venue/pair pair has its own; see feed_config_for().
  unsigned price_decimals = 2;
  unsigned qty_decimals   = 8;

  // The book is a flat array over a tick window, so it cannot span a book whose
  // orders sit $8,000 from the touch at a $0.01 tick — that is 800,000 ticks,
  // and most of them are permanently empty. Orders outside the band are
  // excluded rather than absorbed, and counted so the exclusion is visible in
  // the output instead of being a silent lie about market depth.
  //
  // window_ticks == 0 disables the check and lets the book do the rejecting.
  Ticks window_base  = 0;
  Ticks window_ticks = 0;

  // Minimum distance from the snapshot's own touch, in ticks, for a snapshot
  // order to be seeded — see load_snapshot. 0 reads the whole snapshot, which
  // is the measurably wrong setting and is kept only to reproduce that result.
  // Reading the snapshot and declining to apply the events is how you seed
  // nothing; that is the caller's decision, not this field's.
  Ticks seed_guard   = 0;
};

// The book's touch, passed in so the decoder can tell a passive order from a
// marketable one.
//
// Bitstamp publishes an aggressive order as order_created at its limit price
// BEFORE publishing the fills it causes, so a decoder that rests every create
// builds a crossed book. 13% of btcusd creates and 28% of xrpusd creates arrive
// marketable. Nearly all of them are gone within the same millisecond having
// traded nothing, which is the signature of a marketable order the exchange
// refuses to rest; the rest trade out or come back passive after the touch
// moves. None of them should ever join a queue.
struct BookTouch {
  bool  has_bid = false;
  bool  has_ask = false;
  Ticks bid     = 0;
  Ticks ask     = 0;
};

struct BitstampStats {
  std::uint64_t lines                 = 0;
  std::uint64_t parse_errors          = 0;   // line was not usable JSON
  std::uint64_t unknown_event         = 0;   // an event name we do not model
  std::uint64_t control               = 0;   // bts:* subscription plumbing

  std::uint64_t created               = 0;
  std::uint64_t changed               = 0;
  std::uint64_t deleted               = 0;
  std::uint64_t trades                = 0;

  // The split this whole file exists for.
  std::uint64_t fill_events           = 0;
  std::uint64_t cancel_events         = 0;
  std::uint64_t partial_cancels       = 0;
  Qty           filled_qty            = 0;
  Qty           cancelled_qty         = 0;

  // Marketable creates: published, never rested. See BookTouch.
  std::uint64_t marketable            = 0;
  std::uint64_t marketable_traded     = 0;   // left having traded
  std::uint64_t marketable_cancelled  = 0;   // left having traded nothing
  std::uint64_t marketable_rested     = 0;   // became passive and joined the book

  // Capture quality.
  std::uint64_t chain_gaps            = 0;   // pre_event_id != previous event_id
  std::uint64_t out_of_window         = 0;   // adds outside the price band
  std::uint64_t suppressed            = 0;   // follow-ups for those adds
  std::uint64_t unknown_order         = 0;   // change/delete for an id never seen
  // More size left an order than it had. Unlike the at_create identity this IS
  // an invariant, and a breach means the decoder is misreading a size field.
  std::uint64_t size_violations       = 0;
  std::uint64_t missing_amount_traded = 0;   // field absent: split degrades
  std::uint64_t grew                  = 0;   // amended UP, so priority was lost
  std::uint64_t repriced              = 0;   // changed price in place, likewise
};

// One line can produce more than one book event: an order that is partly filled
// and then cancelled is an Execute followed by a Delete, and collapsing that
// into one would lose exactly the distinction above.
struct Decoded {
  BookEvent ev[3]     = {};
  int       n         = 0;

  Nanos     recv_ns   = 0;      // local receive time, 0 if absent
  Nanos     ts        = 0;      // exchange microtimestamp, in ns

  // Trades produce no book event — the exchange already applied their effect
  // through order_changed/order_deleted, and applying it again would double
  // count. They are surfaced for markouts and for cross-checking the split.
  bool      is_trade    = false;
  Ticks     trade_price = 0;
  Qty       trade_qty   = 0;
  Side      taker       = Side::Bid;
};

class BitstampDecoder {
 public:
  explicit BitstampDecoder(BitstampConfig cfg) : cfg_(cfg) {}

  // Decodes one raw capture line. Returns false only when the line yields
  // nothing at all; `out.n` may legitimately be 0 for a control frame or a
  // trade. Never throws, never reads past the line.
  bool decode_line(std::string_view line, const BookTouch& touch, Decoded& out) noexcept;

  // Reads a REST order_book?group=2 snapshot into Add events, seeding only the
  // levels beyond cfg.seed_guard ticks from the snapshot's own touch.
  //
  // Seeding the WHOLE snapshot is wrong. It contains orders whose deletes
  // happened before the capture began, so nothing in the stream removes them
  // and they rest forever. Measured against the trade channel — a trade must
  // print inside the touch — full seeding scored 11.1% on xrpusd against 90.2%
  // for the stream alone.
  //
  // But seeding NOTHING is wasteful. A stream-built book holds only what has
  // churned since recording started, about 240 orders against the real book's
  // 8,000, so it understates depth badly and its spread tail is fiction.
  //
  // The split is the fix, and the reason it works is that the two populations
  // behave differently: orders near the touch turn over in seconds, so the
  // stream reconstructs them exactly and the snapshot's stale ones are pure
  // contamination; orders far out may rest for months — one in these captures
  // carries an id from years ago — so the snapshot is their only source and its
  // staleness there costs a little depth accuracy rather than a wrong touch.
  //
  // Measured, trades inside the touch against guard width:
  //
  //     guard      btcusd   ethusd   xrpusd    levels held (xrpusd)
  //     none        85.0%    88.9%    90.2%      152
  //     0.05%       84.5%    88.9%    51.6%     2578
  //     0.10%       85.0%    88.9%    90.2%     2579
  //     0.20%       85.0%    88.9%    90.2%     2574
  //
  // The cliff sits between 0.05% and 0.10% of mid, and density barely changes
  // across the range, so a wide guard costs nothing. 0.3% is the default.
  //
  // One limitation, and it is why replay reports the observed price range: the
  // guard must be wider than the price moves during the capture, or a stale
  // deep order becomes the touch. These ten-minute sessions moved 0.06%.
  Nanos load_snapshot(std::string_view text, std::vector<BookEvent>& out);

  [[nodiscard]] const BitstampStats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::size_t tracked_orders() const noexcept { return live_.size(); }

  // Infers quoting precision from a snapshot instead of carrying a per-pair
  // table. Bitstamp quotes btcusd to the cent and xrpusd to five decimals, and
  // a table of that sort is wrong the first time a venue changes a tick size
  // without telling anyone. Scanning thousands of real price strings and taking
  // the longest fraction is a measurement, and its failure mode is safe: under-
  // detecting makes parse_decimal REJECT the first price it cannot represent
  // exactly, which is loud, rather than rounding it, which is silent.
  static bool detect_decimals(std::string_view snapshot,
                              unsigned* price_dp, unsigned* qty_dp) noexcept;

  // Mid price in ticks implied by a snapshot, for sizing the book's window
  // before any event is applied. Returns false if the snapshot has no
  // two-sided top.
  static bool snapshot_touch(std::string_view text, const BitstampConfig& cfg,
                             Ticks* best_bid, Ticks* best_ask) noexcept;

 private:
  struct OrderState {
    Qty   remaining  = 0;   // our belief, kept in step with the book
    Ticks price      = 0;
    Side  side       = Side::Bid;
    bool  suppressed = false;   // outside the price band: tracked, never emitted
    bool  pending    = false;   // marketable on arrival: tracked, not yet rested
  };

  [[nodiscard]] static bool marketable(Side s, Ticks p, const BookTouch& t) noexcept {
    return s == Side::Bid ? (t.has_ask && p >= t.ask) : (t.has_bid && p <= t.bid);
  }

  [[nodiscard]] bool in_window(Ticks p) const noexcept {
    if (cfg_.window_ticks == 0) return true;
    return p >= cfg_.window_base && p < cfg_.window_base + cfg_.window_ticks;
  }

  BitstampConfig cfg_;
  BitstampStats  stats_;
  std::unordered_map<OrderId, OrderState> live_;
  SeqNum         seq_ = 0;
  char           last_event_id_[48] = {};
  std::size_t    last_event_id_len_ = 0;
};

}  // namespace lob
