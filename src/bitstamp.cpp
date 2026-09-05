#include "lob/feed/bitstamp.hpp"

#include <algorithm>
#include <cstring>

#include "lob/feed/json.hpp"

namespace lob {
namespace {

using json::View;

constexpr Nanos kMicrosToNanos = 1000;

// order_type / trade type: 0 is buy, 1 is sell. Confirmed against capture, not
// only documentation: an order_type 0 sat $8,000 BELOW the touch and an
// order_type 1 sat above it, which only holds if 0 is the bid side.
[[nodiscard]] bool parse_side(View v, Side* s) noexcept {
  if (v == "0") { *s = Side::Bid; return true; }
  if (v == "1") { *s = Side::Ask; return true; }
  return false;
}

[[nodiscard]] bool parse_id(View data, OrderId* id) noexcept {
  // id_str first: `id` is a JSON number, and an id of 2046839978799111 is
  // already past the 2^53 where a JSON parser using doubles starts rounding.
  // Ours does not, but preferring the string costs nothing and means the
  // decoder stays correct if the capture is ever rewritten by one that does.
  View v = json::find_scalar(data, "id_str");
  if (v.empty()) v = json::find_scalar(data, "id");
  return json::parse_u64(v, id);
}

}  // namespace

bool BitstampDecoder::decode_line(std::string_view line, const BookTouch& touch,
                                  Decoded& out) noexcept {
  out = Decoded{};
  ++stats_.lines;

  const View ev_name = json::find_scalar(line, "event");
  if (ev_name.empty()) { ++stats_.parse_errors; return false; }

  // Subscription plumbing the recorder wrote through verbatim.
  if (ev_name.size() >= 4 && ev_name.substr(0, 4) == "bts:") { ++stats_.control; return true; }

  const View data = json::find(line, "data");
  if (data.empty() || data.front() != '{') { ++stats_.parse_errors; return false; }

  std::uint64_t u = 0;
  if (json::parse_u64(json::find_scalar(line, "_recv_ns"), &u)) out.recv_ns = static_cast<Nanos>(u);
  if (json::parse_u64(json::find_scalar(data, "microtimestamp"), &u))
    out.ts = static_cast<Nanos>(u) * kMicrosToNanos;

  // ---- trades ----
  // Deliberately produce no book event. The exchange has already applied the
  // trade's effect through order_changed/order_deleted on both resting sides;
  // replaying it here would remove the same size twice.
  if (ev_name == "trade") {
    ++stats_.trades;
    out.is_trade = true;
    std::int64_t v = 0;
    if (json::parse_decimal(json::find_scalar(data, "price_str"), cfg_.price_decimals, &v))
      out.trade_price = v;
    if (json::parse_decimal(json::find_scalar(data, "amount_str"), cfg_.qty_decimals, &v))
      out.trade_qty = v;
    Side t = Side::Bid;
    if (parse_side(json::find_scalar(data, "type"), &t)) out.taker = t;
    return true;
  }

  const bool is_create  = (ev_name == "order_created");
  const bool is_change  = (ev_name == "order_changed");
  const bool is_delete  = (ev_name == "order_deleted");
  if (!is_create && !is_change && !is_delete) { ++stats_.unknown_event; return true; }

  // ---- sequence chain ----
  // Each order message names its predecessor. A mismatch is a dropped message,
  // and a capture with none is provably whole rather than presumed so.
  {
    const View eid  = json::find_scalar(line, "event_id");
    const View peid = json::find_scalar(line, "pre_event_id");
    if (!eid.empty()) {
      if (last_event_id_len_ != 0 &&
          (peid.size() != last_event_id_len_ ||
           std::memcmp(peid.data(), last_event_id_, last_event_id_len_) != 0)) {
        ++stats_.chain_gaps;
      }
      last_event_id_len_ = std::min(eid.size(), sizeof(last_event_id_));
      std::memcpy(last_event_id_, eid.data(), last_event_id_len_);
    }
  }

  OrderId id = 0;
  if (!parse_id(data, &id)) { ++stats_.parse_errors; return false; }

  Side side = Side::Bid;
  if (!parse_side(json::find_scalar(data, "order_type"), &side)) { ++stats_.parse_errors; return false; }

  Ticks price = 0;
  {
    std::int64_t v = 0;
    if (!json::parse_decimal(json::find_scalar(data, "price_str"), cfg_.price_decimals, &v)) {
      ++stats_.parse_errors;
      return false;
    }
    price = v;
  }

  Qty remaining = 0;
  {
    std::int64_t v = 0;
    if (!json::parse_decimal(json::find_scalar(data, "amount_str"), cfg_.qty_decimals, &v)) {
      ++stats_.parse_errors;
      return false;
    }
    remaining = v;
  }

  // The field the cancel/fill split rests on. If it is ever absent the split
  // degrades to "every removal is a cancel", which is the wrong answer roughly
  // 0.5% of the time — so it is counted rather than assumed away.
  Qty traded = 0;
  bool have_traded = false;
  {
    std::int64_t v = 0;
    if (json::parse_decimal(json::find_scalar(data, "amount_traded"), cfg_.qty_decimals, &v)) {
      traded = v;
      have_traded = true;
    } else {
      ++stats_.missing_amount_traded;
    }
  }

  auto emit = [&out](EventType t, Nanos ts, SeqNum seq, OrderId oid, Side s, Ticks px, Qty q) {
    if (out.n >= static_cast<int>(sizeof(out.ev) / sizeof(out.ev[0]))) return;
    BookEvent& e = out.ev[out.n++];
    e.ts = ts; e.seq = seq; e.order_id = oid; e.side = s; e.price = px; e.qty = q; e.type = t;
  };

  // ---- created ----
  if (is_create) {
    ++stats_.created;
    if (!in_window(price)) {
      ++stats_.out_of_window;
      live_[id] = OrderState{remaining, price, side, true, false};
      return true;
    }
    // Marketable on arrival: published by the exchange before the fills it
    // causes. Resting it would cross the book and hand it a queue position it
    // never had. Tracked so its later events resolve, never added.
    if (marketable(side, price, touch)) {
      ++stats_.marketable;
      live_[id] = OrderState{remaining, price, side, false, true};
      return true;
    }
    live_[id] = OrderState{remaining, price, side, false, false};
    emit(EventType::Add, out.ts, ++seq_, id, side, price, remaining);
    return true;
  }

  auto it = live_.find(id);
  if (it == live_.end()) {
    // An order created before the snapshot and absent from it, or the far side
    // of a gap. Counted, never guessed at: inventing an Add here would put size
    // in the book at a price and a queue position the exchange never had.
    ++stats_.unknown_order;
    return true;
  }
  OrderState& st = it->second;
  if (st.suppressed) {
    ++stats_.suppressed;
    if (is_delete) live_.erase(it);
    return true;
  }

  // ---- a marketable order that never rested ----
  // It is not in the book, so nothing here removes size from the book. Its
  // fills are already accounted for by the RESTING side's own events; counting
  // them again here would double the traded volume.
  if (st.pending) {
    if (is_delete) {
      if (traded > 0 || remaining == 0) ++stats_.marketable_traded;
      else                              ++stats_.marketable_cancelled;
      live_.erase(it);
      return true;
    }
    ++stats_.changed;
    st.remaining = remaining;
    st.price     = price;
    st.side      = side;
    // The touch moved and what was marketable is now a passive resting order.
    if (remaining > 0 && !marketable(side, price, touch)) {
      if (in_window(price)) {
        ++stats_.marketable_rested;
        st.pending = false;
        emit(EventType::Add, out.ts, ++seq_, id, side, price, remaining);
      } else {
        ++stats_.out_of_window;
        st.pending    = false;
        st.suppressed = true;
      }
    }
    return true;
  }

  // Size this event traded. PER EVENT, not a difference against a running
  // total: amount_traded is not cumulative, and differencing it silently
  // under-counts every order that fills more than once. Without the field we
  // cannot tell, and treating the removal as a cancel is the conservative
  // reading — it under-counts fills rather than inventing them.
  const Qty d_traded = have_traded ? std::max<Qty>(0, traded) : 0;

  // The invariant that replaced the at_create identity: no more size can leave
  // an order than it had. What traded plus what still remains cannot exceed
  // what we believed was resting. A breach means a size field is being misread.
  if (have_traded && d_traded + remaining > st.remaining) ++stats_.size_violations;

  const Qty fill = std::min(d_traded, st.remaining);
  if (fill > 0) {
    emit(EventType::Execute, out.ts, ++seq_, id, st.side, st.price, fill);
    st.remaining -= fill;
    ++stats_.fill_events;
    stats_.filled_qty += fill;
  }

  // ---- deleted ----
  if (is_delete) {
    ++stats_.deleted;
    if (st.remaining > 0) {
      emit(EventType::Delete, out.ts, ++seq_, id, st.side, st.price, st.remaining);
      ++stats_.cancel_events;
      stats_.cancelled_qty += st.remaining;
    }
    live_.erase(it);
    return true;
  }

  // ---- changed ----
  ++stats_.changed;

  // A price change in place is an amend that cannot keep priority. Bitstamp
  // normally sends delete+create for this; if it ever does not, modelling it as
  // a shrink would leave size resting at a price nobody is quoting.
  if (price != st.price) {
    ++stats_.repriced;
    emit(EventType::Delete, out.ts, ++seq_, id, st.side, st.price, st.remaining);
    if (!in_window(price)) {
      ++stats_.out_of_window;
      st = OrderState{remaining, price, side, true, false};
    } else if (marketable(side, price, touch)) {
      ++stats_.marketable;
      st = OrderState{remaining, price, side, false, true};
    } else {
      emit(EventType::Add, out.ts, ++seq_, id, side, price, remaining);
      st = OrderState{remaining, price, side, false, false};
    }
    return true;
  }

  if (remaining < st.remaining) {
    const Qty by = st.remaining - remaining;
    emit(EventType::Reduce, out.ts, ++seq_, id, st.side, st.price, by);
    ++stats_.partial_cancels;
    stats_.cancelled_qty += by;
    st.remaining = remaining;
  } else if (remaining > st.remaining) {
    // Amended up. Size added to a resting order goes to the BACK of the queue
    // everywhere that matters, so this is a cancel and a new order, not a
    // grow-in-place. Getting this wrong would hand our own orders a queue
    // position the exchange never gave them.
    ++stats_.grew;
    emit(EventType::Delete, out.ts, ++seq_, id, st.side, st.price, st.remaining);
    emit(EventType::Add, out.ts, ++seq_, id, side, price, remaining);
    st.remaining = remaining;
    st.side = side;
  }
  return true;
}

Nanos BitstampDecoder::load_snapshot(std::string_view text, std::vector<BookEvent>& out) {
  std::uint64_t micros = 0;
  if (!json::parse_u64(json::find_scalar(text, "microtimestamp"), &micros)) return 0;
  const Nanos ts = static_cast<Nanos>(micros) * kMicrosToNanos;

  // The guard is measured from the snapshot's OWN touch, not the book's: the
  // book may be empty when this runs, and the snapshot is internally consistent
  // even when it is stale relative to the stream.
  Ticks snap_bid = 0, snap_ask = 0;
  if (cfg_.seed_guard > 0 && !snapshot_touch(text, cfg_, &snap_bid, &snap_ask)) return 0;

  for (int s = 0; s < 2; ++s) {
    const Side side  = (s == 0) ? Side::Bid : Side::Ask;
    const View rows  = json::find(text, (s == 0) ? "bids" : "asks");
    std::size_t i = 0;
    View row;
    while (json::array_next(rows, i, &row)) {
      std::size_t j = 0;
      View px_s, amt_s, id_s;
      if (!json::array_next(row, j, &px_s))  { ++stats_.parse_errors; continue; }
      if (!json::array_next(row, j, &amt_s)) { ++stats_.parse_errors; continue; }
      // A two-element row means the endpoint aggregated by price and there are
      // no order ids: the feed is L2 and nothing downstream about queue
      // position is recoverable. Loud, not silent.
      if (!json::array_next(row, j, &id_s))  { ++stats_.parse_errors; continue; }

      std::int64_t px = 0, amt = 0;
      OrderId id = 0;
      if (!json::parse_decimal(px_s, cfg_.price_decimals, &px) ||
          !json::parse_decimal(amt_s, cfg_.qty_decimals, &amt) ||
          !json::parse_u64(id_s, &id)) {
        ++stats_.parse_errors;
        continue;
      }
      if (amt <= 0) continue;

      // Near the touch the stream is authoritative and the snapshot is
      // contamination. Skipped silently: these are not errors, they are the
      // half of the snapshot that is not trusted.
      if (cfg_.seed_guard > 0) {
        const Ticks out_by = (side == Side::Bid) ? snap_bid - px : px - snap_ask;
        if (out_by < cfg_.seed_guard) continue;
      }

      if (!in_window(px)) {
        ++stats_.out_of_window;
        continue;
      }
      // Registered, because unlike the near-touch orders these DO go into the
      // book, and the decoder must know about them or it will emit deletes the
      // book rejects as unknown.
      live_[id] = OrderState{amt, px, side, false, false};

      BookEvent e;
      e.ts = ts; e.seq = ++seq_; e.order_id = id;
      e.side = side; e.price = px; e.qty = amt; e.type = EventType::Add;
      out.push_back(e);
    }
  }
  return ts;
}

namespace {
// Digits after the decimal point, 0 if there is no point.
[[nodiscard]] unsigned fraction_digits(json::View v) noexcept {
  const std::size_t dot = v.find('.');
  if (dot == json::View::npos) return 0;
  unsigned n = 0;
  for (std::size_t i = dot + 1; i < v.size(); ++i) {
    if (v[i] < '0' || v[i] > '9') return n;
    ++n;
  }
  return n;
}
}  // namespace

bool BitstampDecoder::detect_decimals(std::string_view snapshot,
                                      unsigned* price_dp, unsigned* qty_dp) noexcept {
  if (price_dp == nullptr || qty_dp == nullptr) return false;
  unsigned px = 0, qt = 0;
  bool any = false;
  for (int s = 0; s < 2; ++s) {
    const View rows = json::find(snapshot, (s == 0) ? "bids" : "asks");
    std::size_t i = 0;
    View row;
    while (json::array_next(rows, i, &row)) {
      std::size_t j = 0;
      View f;
      if (!json::array_next(row, j, &f)) continue;
      px = std::max(px, fraction_digits(f));
      if (!json::array_next(row, j, &f)) continue;
      qt = std::max(qt, fraction_digits(f));
      any = true;
    }
  }
  if (!any) return false;
  *price_dp = px;
  *qty_dp   = qt;
  return true;
}

bool BitstampDecoder::snapshot_touch(std::string_view text, const BitstampConfig& cfg,
                                     Ticks* best_bid, Ticks* best_ask) noexcept {
  if (best_bid == nullptr || best_ask == nullptr) return false;
  bool have_bid = false, have_ask = false;
  Ticks bid = 0, ask = 0;

  for (int s = 0; s < 2; ++s) {
    const View rows = json::find(text, (s == 0) ? "bids" : "asks");
    std::size_t i = 0;
    View row;
    while (json::array_next(rows, i, &row)) {
      std::size_t j = 0;
      View px_s;
      if (!json::array_next(row, j, &px_s)) continue;
      std::int64_t px = 0;
      if (!json::parse_decimal(px_s, cfg.price_decimals, &px)) continue;
      // Scanned rather than assuming row 0 is the touch: a snapshot that is not
      // sorted the way we expect would otherwise silently mis-centre the window.
      if (s == 0) { if (!have_bid || px > bid) { bid = px; have_bid = true; } }
      else        { if (!have_ask || px < ask) { ask = px; have_ask = true; } }
    }
  }
  *best_bid = bid;
  *best_ask = ask;
  return have_bid && have_ask;
}

}  // namespace lob
