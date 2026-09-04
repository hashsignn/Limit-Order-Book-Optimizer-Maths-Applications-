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
//   amount_traded      cumulative size filled so far
//
// Those three are on every message, so the reason an order shrank or vanished
// is stated by the feed rather than inferred. That is the difference between
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

  // Capture quality.
  std::uint64_t chain_gaps            = 0;   // pre_event_id != previous event_id
  std::uint64_t out_of_window         = 0;   // adds outside the price band
  std::uint64_t suppressed            = 0;   // follow-ups for those adds
  std::uint64_t unknown_order         = 0;   // change/delete for an id never seen
  std::uint64_t identity_violations   = 0;   // amount + amount_traded != at_create
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
  bool decode_line(std::string_view line, Decoded& out) noexcept;

  // Seeds the book from a REST order_book?group=2 snapshot, appending one Add
  // per resting order. Returns the snapshot's microtimestamp in ns, or 0 on
  // failure. Orders outside the window are counted, not appended.
  Nanos load_snapshot(std::string_view text, std::vector<BookEvent>& out);

  [[nodiscard]] const BitstampStats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::size_t tracked_orders() const noexcept { return live_.size(); }

  // Mid price in ticks implied by a snapshot, for sizing the book's window
  // before any event is applied. Returns false if the snapshot has no
  // two-sided top.
  static bool snapshot_touch(std::string_view text, const BitstampConfig& cfg,
                             Ticks* best_bid, Ticks* best_ask) noexcept;

 private:
  struct OrderState {
    Qty   remaining = 0;    // our belief, kept in step with the book
    Qty   traded    = 0;    // cumulative, as last reported
    Ticks price     = 0;
    Side  side      = Side::Bid;
    bool  suppressed = false;   // outside the window: track it, never emit it
  };

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
