// Bitstamp decoder tests.
//
// The fixtures at the top are VERBATIM lines from a real capture, not lines
// invented to match my reading of the documentation. That distinction has
// already earned its keep once on this project: the documented plan was to
// recover the cancel/fill split by joining order ids against the trade channel,
// and the real bytes turned out to carry amount_traded on every message, which
// is both exact and clock-free. Testing against documentation would have
// enshrined the worse design.
#include "lob/book/order_book.hpp"
#include "lob/feed/bitstamp.hpp"
#include "lob/feed/json.hpp"
#include "lob/feed/line_reader.hpp"
#include "test_util.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace lob;

namespace {

// ---- real capture lines, btcusd, 2026-09-04 -------------------------------
constexpr const char* kCreateBid = R"J({"data":{"id":2046839978799111,"id_str":"2046839978799111","order_type":0,"order_subtype":5,"datetime":"1788551766","microtimestamp":"1788551766357000","amount":0.00137276,"amount_str":"0.00137276","amount_traded":"0","amount_at_create":"0.00137276","price":71743.85,"price_str":"71743.85","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_created","event_id":"00065aad-a8b2-3408-0000-000102000020","pre_event_id":"00065aad-a8b2-14c8-0000-000101000020","order_source":"orderbook","_recv_ns":1788551766927713300})J";

constexpr const char* kCreateAsk = R"J({"data":{"id":2046839978831872,"id_str":"2046839978831872","order_type":1,"order_subtype":5,"datetime":"1788551766","microtimestamp":"1788551766364000","amount":0.18816587,"amount_str":"0.18816587","amount_traded":"0","amount_at_create":"0.18816587","price":79728.11,"price_str":"79728.11","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_created","event_id":"00065aad-a8b2-4f60-0000-000103000020","pre_event_id":"00065aad-a8b2-3408-0000-000102000020","order_source":"orderbook","_recv_ns":1788551766928989900})J";

constexpr const char* kDeleteUnknown = R"J({"data":{"id":2046839978139649,"id_str":"2046839978139649","order_type":0,"order_subtype":5,"datetime":"1788551766","microtimestamp":"1788551766365000","amount":0.00137276,"amount_str":"0.00137276","amount_traded":"0","amount_at_create":"0.00137276","price":71743.85,"price_str":"71743.85","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_deleted","event_id":"00065aad-a8b2-5348-0000-000100000020","pre_event_id":"00065aad-a8b2-4f60-0000-000103000020","order_source":"orderbook","_recv_ns":1788551766929024400})J";

constexpr const char* kTrade = R"J({"data":{"id":629876235,"timestamp":"1788551770","amount":0.00062132,"amount_str":"0.00062132","price":79715.39,"price_str":"79715.39","type":0,"microtimestamp":"1788551770815000","buy_order_id":2046839997059073,"sell_order_id":2046839970762754},"channel":"live_trades_btcusd","event":"trade","_recv_ns":1788551770727077500})J";

constexpr const char* kSnapshot = R"J({"timestamp": "1788551766", "microtimestamp": "1788551766754717", "bids": [["79715.38", "0.06272315", "2046839968690178"], ["79715.38", "0.39542753", "2046839975448576"], ["79715.24", "0.37633686", "2046839971758081"], ["71743.85", "0.12500000", "2046839966826496"]], "asks": [["79728.11", "0.18816587", "2046839978831872"], ["79730.00", "0.50000000", "2046839978831999"]]})J";

// Bitstamp quotes BTC/USD to the cent and sizes to 1e-8 BTC. A window of 80,000
// ticks is $800 wide, which at a $79,715 mid is +-0.5% — far past anything a
// market maker quotes into, and far short of the $8,000-away resting orders the
// capture actually contains. Excluding those is the point; counting them is
// what keeps the exclusion honest.
// An empty book: nothing can be marketable against it, so the tests that are
// not about marketable orders are unaffected by the touch.
constexpr BookTouch kNoTouch{};

BitstampConfig make_cfg() {
  BitstampConfig c;
  c.price_decimals = 2;
  c.qty_decimals   = 8;
  c.window_base    = 7'930'000;   // $79,300.00
  c.window_ticks   = 80'000;      // .. $80,100.00
  return c;
}

// Builds an order message. Keeps the sequence tests readable without pasting a
// 500-character line for every step.
std::string order_msg(const char* event, OrderId id, int order_type, const char* price,
                      const char* amount, const char* traded, const char* at_create,
                      std::uint64_t micros, const char* eid, const char* peid) {
  char buf[1400];
  std::snprintf(buf, sizeof(buf),
      R"J({"data":{"id":%llu,"id_str":"%llu","order_type":%d,"microtimestamp":"%llu",)J"
      R"J("amount":0,"amount_str":"%s","amount_traded":"%s","amount_at_create":"%s",)J"
      R"J("price":0,"price_str":"%s"},"channel":"live_orders_btcusd","event":"%s",)J"
      R"J("event_id":"%s","pre_event_id":"%s","_recv_ns":1788551766900000000})J",
      static_cast<unsigned long long>(id), static_cast<unsigned long long>(id), order_type,
      static_cast<unsigned long long>(micros), amount, traded, at_create, price, event, eid, peid);
  return std::string(buf);
}

// Replays a real venue capture and asserts the book survived it.
//
// This is Phase 1's actual acceptance criterion: a full session of real market
// data with zero invariant violations. Everything else in this file proves the
// decoder agrees with fixtures I wrote; only this proves it agrees with an
// exchange. data/samples holds the captures — see .gitignore, which excludes
// bulk recordings and admits these.
void replay_real_capture(const char* capture_path, const char* snapshot_path) {
  std::string snap;
  {
    LineReader r;
    if (!r.open(snapshot_path)) {
      ::lobtest::report(false, "open snapshot", __FILE__, __LINE__, r.error());
      return;
    }
    std::string_view l;
    while (r.next(&l)) snap.append(l.data(), l.size());
  }
  CHECK(!snap.empty());

  BitstampConfig c = make_cfg();
  // Measured, not assumed: these three captures are btcusd (cents), ethusd
  // (cents) and xrpusd (five decimals), and one hardcoded precision cannot be
  // right for all of them.
  unsigned px_dp = 0, qt_dp = 0;
  CHECK(BitstampDecoder::detect_decimals(snap, &px_dp, &qt_dp));
  c.price_decimals = px_dp;
  c.qty_decimals   = qt_dp;

  Ticks bb = 0, ba = 0;
  CHECK(BitstampDecoder::snapshot_touch(snap, c, &bb, &ba));
  CHECK(bb < ba);                       // a real snapshot is never crossed

  // Centre the window on the touch. Sized here rather than in make_cfg because
  // these instruments differ by orders of magnitude: at a $0.01 tick, 80,000
  // ticks is $800 around a $79,715 BTC mid, and the same 80,000 ticks around a
  // $2 XRP mid is a band the whole book fits inside many times over.
  // The band is +-2% of the mid, expressed in ticks and rounded up to the
  // multiple of 64 the occupancy bitset needs. A fixed tick count cannot serve
  // both a $79,715 book at a $0.01 tick and a $2 book at a $0.00001 one.
  const Ticks mid = (bb + ba) / 2;
  Ticks band = (mid / 25) & ~Ticks{63};
  if (band < 4096) band = 4096;
  c.window_ticks = band;
  c.window_base  = mid - band / 2;
  c.seed_guard   = static_cast<Ticks>(static_cast<double>(mid) * 0.003);

  BitstampDecoder d{c};
  OrderBook book{c.window_base, static_cast<std::uint32_t>(c.window_ticks), 1 << 21};

  // Only the DEEP half of the snapshot seeds the book. Near the touch the
  // stream is authoritative — those orders churn in seconds, so the snapshot's
  // stale ones are contamination and cost 39 points of accuracy on xrpusd.
  std::vector<BookEvent> seed;
  d.load_snapshot(snap, seed);
  CHECK(seed.size() > 100);
  for (const BookEvent& e : seed) CHECK(book.apply(e) == BookError::Ok);

  LineReader reader;
  if (!reader.open(capture_path)) {
    ::lobtest::report(false, "open capture", __FILE__, __LINE__, reader.error());
    return;
  }
  std::string_view line;
  std::uint64_t n = 0, crossed = 0;
  std::string why;
  while (reader.next(&line)) {
    const BookTouch touch{book.has_bid(), book.has_ask(),
                          book.has_bid() ? book.best_bid() : 0,
                          book.has_ask() ? book.best_ask() : 0};
    Decoded out;
    if (!d.decode_line(line, touch, out)) continue;
    for (int i = 0; i < out.n; ++i) {
      // A real MBO feed can never book an order that crosses: the exchange
      // matched it instead of resting it. One here means the decoder built an
      // event the exchange never sent.
      if (book.apply(out.ev[i]) == BookError::CrossedBook) ++crossed;
      if ((++n % 20'000) == 0) CHECK(book.check_invariants(&why));
    }
  }
  CHECK(book.check_invariants(&why));
  CHECK_EQ(crossed, 0u);

  const BitstampStats& st = d.stats();
  CHECK(st.lines > 1000);
  CHECK_EQ(st.chain_gaps, 0u);            // the capture is provably whole
  CHECK_EQ(st.size_violations, 0u);       // no order lost more size than it held
  CHECK_EQ(st.missing_amount_traded, 0u); // the fill/cancel split is exact
  // A recording stopped with Ctrl-C ends mid-frame, so one unusable line is
  // expected; more than that means the decoder is misreading the format.
  CHECK(st.parse_errors <= 1);
  CHECK(st.fill_events > 0);              // a capture with no fills proves nothing
  CHECK(st.cancel_events > 0);

  std::printf("  %s\n    %llu lines -> %llu book events | %llu fills / %llu cancels"
              " | %llu marketable held back | %llu outside band | %zu resting\n",
              capture_path, static_cast<unsigned long long>(st.lines),
              static_cast<unsigned long long>(n),
              static_cast<unsigned long long>(st.fill_events),
              static_cast<unsigned long long>(st.cancel_events),
              static_cast<unsigned long long>(st.marketable),
              static_cast<unsigned long long>(st.out_of_window), book.live_orders());
}

}  // namespace

int main(int argc, char** argv) {
  // ======================= json scanner =======================
  {
    constexpr const char* obj = R"({"a":1,"b":"two","c":{"a":9},"d":[1,2],"e":null})";

    CHECK(json::find_scalar(obj, "a") == "1");
    CHECK(json::find_scalar(obj, "b") == "two");
    CHECK(json::find_scalar(obj, "e") == "null");
    CHECK(json::find(obj, "c") == R"({"a":9})");
    CHECK(json::find(obj, "d") == "[1,2]");
    CHECK(json::find(obj, "missing").empty());

    // A nested key must not satisfy an outer lookup. This is the failure mode
    // that matters on this feed: `price` lives inside `data` and `channel`
    // outside it, and a scanner that flattened them would read whichever came
    // first in the bytes.
    const json::View inner = json::find(obj, "c");
    CHECK(json::find_scalar(inner, "a") == "9");
    CHECK(json::find_scalar(obj, "a") == "1");

    // Structure inside a string value must not be treated as structure.
    constexpr const char* tricky = R"({"k":"},\"x\":1,{","x":7})";
    CHECK(json::find_scalar(tricky, "x") == "7");

    // Malformed input is answered, not crashed on.
    CHECK(json::find("", "a").empty());
    CHECK(json::find("{", "a").empty());
    CHECK(json::find(R"({"a")", "a").empty());
    CHECK(json::find(R"({"a":)", "a").empty());
    CHECK(json::find(R"({"a":"unterminated)", "a").empty());
    CHECK(json::find(R"({"a":[1,2)", "a").empty());
    CHECK(json::find(R"({"a":"trailing backslash\)", "a").empty());
  }

  // ---- arrays ----
  {
    constexpr const char* arr = R"([["a","b"],["c","d"]])";
    std::size_t i = 0;
    json::View row;
    CHECK(json::array_next(arr, i, &row));
    CHECK(row == R"(["a","b"])");
    std::size_t j = 0;
    json::View f;
    CHECK(json::array_next(row, j, &f)); CHECK(f == "a");
    CHECK(json::array_next(row, j, &f)); CHECK(f == "b");
    CHECK(!json::array_next(row, j, &f));
    CHECK(json::array_next(arr, i, &row));
    CHECK(row == R"(["c","d"])");
    CHECK(!json::array_next(arr, i, &row));

    std::size_t k = 0;
    CHECK(!json::array_next("[]", k, &row));
  }

  // ---- exact decimals ----
  {
    std::int64_t v = 0;
    CHECK(json::parse_decimal("79715.38", 2, &v));   CHECK_EQ(v, 7'971'538);
    CHECK(json::parse_decimal("0.00137276", 8, &v)); CHECK_EQ(v, 137'276);
    CHECK(json::parse_decimal("0", 8, &v));          CHECK_EQ(v, 0);
    CHECK(json::parse_decimal("1", 8, &v));          CHECK_EQ(v, 100'000'000);
    CHECK(json::parse_decimal("-1.5", 2, &v));       CHECK_EQ(v, -150);

    // Zeros past the scale are not significant, so they are accepted.
    CHECK(json::parse_decimal("1.000000000", 8, &v)); CHECK_EQ(v, 100'000'000);

    // Digits past the scale ARE significant. Rounding them away would make the
    // book disagree with the exchange by an amount nothing downstream could see.
    CHECK(!json::parse_decimal("0.123456789", 8, &v));

    // Exponent form is refused rather than mis-scaled.
    CHECK(!json::parse_decimal("1e-8", 8, &v));
    CHECK(!json::parse_decimal("", 8, &v));
    CHECK(!json::parse_decimal(".", 8, &v));
    CHECK(!json::parse_decimal("12x", 2, &v));
    CHECK(!json::parse_decimal("99999999999999999999", 2, &v));   // overflow
  }

  // ======================= real capture lines =======================
  {
    BitstampDecoder d{make_cfg()};
    Decoded out;

    // A bid $8,000 below the touch: outside the window, so no book event, but
    // it is counted rather than dropped in silence.
    CHECK(d.decode_line(kCreateBid, kNoTouch, out));
    CHECK_EQ(out.n, 0);
    CHECK_EQ(d.stats().out_of_window, 1u);
    CHECK_EQ(out.ts, 1788551766357000LL * 1000);
    CHECK_EQ(out.recv_ns, 1788551766927713300LL);

    // An ask inside the window: one Add, fields exact.
    CHECK(d.decode_line(kCreateAsk, kNoTouch, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Add);
    CHECK(out.ev[0].side == Side::Ask);
    CHECK_EQ(out.ev[0].order_id, 2046839978831872ULL);
    CHECK_EQ(out.ev[0].price, 7'972'811);
    CHECK_EQ(out.ev[0].qty, 18'816'587);

    // A delete for an id this stream never created. Never guessed at.
    CHECK(d.decode_line(kDeleteUnknown, kNoTouch, out));
    CHECK_EQ(out.n, 0);
    CHECK_EQ(d.stats().unknown_order, 1u);

    // A trade produces NO book event: the exchange already applied its effect
    // through the order channel, and replaying it would remove size twice.
    CHECK(d.decode_line(kTrade, kNoTouch, out));
    CHECK_EQ(out.n, 0);
    CHECK(out.is_trade);
    CHECK_EQ(out.trade_price, 7'971'539);
    CHECK_EQ(out.trade_qty, 62'132);
    CHECK(out.taker == Side::Bid);

    // Those three order lines chain end to end, so no gap is reported.
    CHECK_EQ(d.stats().chain_gaps, 0u);
    CHECK_EQ(d.stats().size_violations, 0u);
    CHECK_EQ(d.stats().missing_amount_traded, 0u);
  }

  // ======================= the cancel / fill split =======================
  // The measurement this decoder exists for. One order is partly filled, then
  // the remainder is cancelled: that must come out as an Execute and a Delete
  // with distinct quantities, never as one removal of the total.
  {
    BitstampDecoder d{make_cfg()};
    Decoded out;

    CHECK(d.decode_line(order_msg("order_created", 1, 0, "79715.00", "1.00000000", "0",
                                  "1.00000000", 1788551766000000ULL, "e1", "e0"), kNoTouch, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Add);
    CHECK_EQ(out.ev[0].qty, 100'000'000);

    // 0.6 traded away: an Execute for exactly the traded delta.
    CHECK(d.decode_line(order_msg("order_changed", 1, 0, "79715.00", "0.40000000", "0.60000000",
                                  "1.00000000", 1788551766100000ULL, "e2", "e1"), kNoTouch, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Execute);
    CHECK_EQ(out.ev[0].qty, 60'000'000);

    // The remaining 0.4 is pulled. amount_traded is PER EVENT, so this message
    // reports 0 traded — nothing traded in THIS event — and it must come out as
    // a Delete, never as a fill.
    CHECK(d.decode_line(order_msg("order_deleted", 1, 0, "79715.00", "0.40000000", "0",
                                  "1.00000000", 1788551766200000ULL, "e3", "e2"), kNoTouch, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Delete);
    CHECK_EQ(out.ev[0].qty, 40'000'000);

    CHECK_EQ(d.stats().filled_qty, 60'000'000);
    CHECK_EQ(d.stats().cancelled_qty, 40'000'000);
    CHECK_EQ(d.stats().fill_events, 1u);
    CHECK_EQ(d.stats().cancel_events, 1u);
    CHECK_EQ(d.stats().size_violations, 0u);
    CHECK_EQ(d.tracked_orders(), 0u);   // erased on delete: no unbounded growth
  }

  // A fill that consumes the whole order is one Execute and no Delete — the
  // order leaves the book because it traded, not because anyone cancelled.
  {
    BitstampDecoder d{make_cfg()};
    Decoded out;
    CHECK(d.decode_line(order_msg("order_created", 7, 1, "79720.00", "2.00000000", "0",
                                  "2.00000000", 1788551766000000ULL, "e1", "e0"), kNoTouch, out));
    CHECK(d.decode_line(order_msg("order_deleted", 7, 1, "79720.00", "0", "2.00000000",
                                  "2.00000000", 1788551766050000ULL, "e2", "e1"), kNoTouch, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Execute);
    CHECK_EQ(out.ev[0].qty, 200'000'000);
    CHECK_EQ(d.stats().cancel_events, 0u);
    CHECK_EQ(d.stats().cancelled_qty, 0);
  }

  // A pure cancel of an untouched order: one Delete, zero filled.
  {
    BitstampDecoder d{make_cfg()};
    Decoded out;
    CHECK(d.decode_line(order_msg("order_created", 8, 0, "79700.00", "0.50000000", "0",
                                  "0.50000000", 1788551766000000ULL, "e1", "e0"), kNoTouch, out));
    CHECK(d.decode_line(order_msg("order_deleted", 8, 0, "79700.00", "0.50000000", "0",
                                  "0.50000000", 1788551766050000ULL, "e2", "e1"), kNoTouch, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Delete);
    CHECK_EQ(d.stats().filled_qty, 0);
    CHECK_EQ(d.stats().cancelled_qty, 50'000'000);
  }

  // ======================= capture quality =======================
  {
    BitstampDecoder d{make_cfg()};
    Decoded out;
    CHECK(d.decode_line(order_msg("order_created", 1, 0, "79715.00", "1.00000000", "0",
                                  "1.00000000", 1788551766000000ULL, "e1", "e0"), kNoTouch, out));
    // pre_event_id names a message we never saw: a hole in the capture.
    CHECK(d.decode_line(order_msg("order_created", 2, 0, "79714.00", "1.00000000", "0",
                                  "1.00000000", 1788551766001000ULL, "e5", "e4"), kNoTouch, out));
    CHECK_EQ(d.stats().chain_gaps, 1u);
    // .. and the chain resumes from the message that was actually received,
    // so one gap is reported once rather than every message thereafter.
    CHECK(d.decode_line(order_msg("order_created", 3, 0, "79713.00", "1.00000000", "0",
                                  "1.00000000", 1788551766002000ULL, "e6", "e5"), kNoTouch, out));
    CHECK_EQ(d.stats().chain_gaps, 1u);
  }

  // amount_traded is PER EVENT, not cumulative. This is the sequence from real
  // order 2046841350975488: six partial fills, then the remainder cancelled.
  // Under a cumulative reading the last value is 0 and the order looks like a
  // pure cancel, losing every one of those fills.
  {
    BitstampDecoder d{make_cfg()};
    Decoded out;
    const char* fills[] = {"0.00572747", "0.00405924", "0.05969432",
                           "0.00784165", "0.02010227", "0.02334723"};
    const char* rem[]   = {"0.11927253", "0.11521329", "0.05551897",
                           "0.04767732", "0.02757505", "0.00422782"};
    CHECK(d.decode_line(order_msg("order_created", 9, 0, "79715.00", "0.12500000", "0",
                                  "0.12500000", 1788551766000000ULL, "e0", "x"), kNoTouch, out));
    for (int i = 0; i < 6; ++i) {
      char e[8], pe[8];
      std::snprintf(e, sizeof(e), "e%d", i + 1);
      std::snprintf(pe, sizeof(pe), "e%d", i);
      CHECK(d.decode_line(order_msg("order_changed", 9, 0, "79715.00", rem[i], fills[i],
                                    "0.12500000", 1788551766000000ULL + static_cast<std::uint64_t>(i + 1) * 1000,
                                    e, pe), kNoTouch, out));
      CHECK_EQ(out.n, 1);
      CHECK(out.ev[0].type == EventType::Execute);
    }
    // The six fills sum to the 0.12077218 that left the book.
    CHECK_EQ(d.stats().filled_qty, 12'077'218);
    CHECK_EQ(d.stats().fill_events, 6u);

    // Then the remainder is cancelled, with amount_traded 0 on that message.
    CHECK(d.decode_line(order_msg("order_deleted", 9, 0, "79715.00", "0.00422782", "0",
                                  "0.12500000", 1788551766100000ULL, "e7", "e6"), kNoTouch, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Delete);
    CHECK_EQ(d.stats().cancelled_qty, 422'782);
    CHECK_EQ(d.stats().filled_qty + d.stats().cancelled_qty, 12'500'000);  // == at_create
    CHECK_EQ(d.stats().size_violations, 0u);
  }

  // ======================= marketable creates are never rested =======================
  // Bitstamp publishes an aggressive order as order_created at its limit price
  // before publishing the fills it causes. Resting it would cross the book.
  {
    BitstampDecoder d{make_cfg()};
    Decoded out;
    const BookTouch touch{true, true, 7'971'500, 7'971'501};   // 1-tick spread

    // A bid at the ask: marketable. No book event at all.
    CHECK(d.decode_line(order_msg("order_created", 20, 0, "79715.01", "1.00000000", "0",
                                  "1.00000000", 1788551766000000ULL, "e1", "e0"), touch, out));
    CHECK_EQ(out.n, 0);
    CHECK_EQ(d.stats().marketable, 1u);

    // It is gone in the same millisecond having traded nothing — the signature
    // of an order the exchange refuses to rest. Still no book event: it was
    // never in the book.
    CHECK(d.decode_line(order_msg("order_deleted", 20, 0, "79715.01", "1.00000000", "0",
                                  "1.00000000", 1788551766000000ULL, "e2", "e1"), touch, out));
    CHECK_EQ(out.n, 0);
    CHECK_EQ(d.stats().marketable_cancelled, 1u);
    CHECK_EQ(d.stats().cancel_events, 0u);   // not a book cancel: it never rested

    // A bid one tick inside the spread is passive and rests normally.
    CHECK(d.decode_line(order_msg("order_created", 21, 0, "79715.00", "1.00000000", "0",
                                  "1.00000000", 1788551766001000ULL, "e3", "e2"), touch, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Add);
  }

  // A marketable order can become passive when the touch moves away from it,
  // and then it must join the book — at the back of the queue, on the event
  // that made it passive, not at the price it originally crossed.
  {
    BitstampDecoder d{make_cfg()};
    Decoded out;
    const BookTouch tight{true, true, 7'971'500, 7'971'501};
    CHECK(d.decode_line(order_msg("order_created", 30, 0, "79715.01", "1.00000000", "0",
                                  "1.00000000", 1788551766000000ULL, "e1", "e0"), tight, out));
    CHECK_EQ(out.n, 0);

    const BookTouch wide{true, true, 7'971'400, 7'971'600};   // ask moved up
    CHECK(d.decode_line(order_msg("order_changed", 30, 0, "79715.01", "1.00000000", "0",
                                  "1.00000000", 1788551766002000ULL, "e2", "e1"), wide, out));
    CHECK_EQ(out.n, 1);
    CHECK(out.ev[0].type == EventType::Add);
    CHECK_EQ(out.ev[0].price, 7'971'501);
    CHECK_EQ(d.stats().marketable_rested, 1u);
  }

  // ======================= snapshot =======================
  {
    BitstampDecoder d{make_cfg()};
    Ticks bb = 0, ba = 0;
    CHECK(BitstampDecoder::snapshot_touch(kSnapshot, make_cfg(), &bb, &ba));
    CHECK_EQ(bb, 7'971'538);
    CHECK_EQ(ba, 7'972'811);

    std::vector<BookEvent> seed;
    const Nanos ts = d.load_snapshot(kSnapshot, seed);
    CHECK_EQ(ts, 1788551766754717LL * 1000);
    // With no guard the whole snapshot is read — the measurably wrong setting,
    // kept so the comparison behind the guard can be reproduced.
    // Four bids, but one is the $71,743 outlier and falls outside the window.
    CHECK_EQ(seed.size(), 5u);
    CHECK_EQ(d.stats().out_of_window, 1u);
    CHECK_EQ(d.stats().parse_errors, 0u);
    if (seed.size() >= 2) {
      CHECK(seed[0].type == EventType::Add);
      CHECK(seed[0].side == Side::Bid);
      CHECK_EQ(seed[0].price, 7'971'538);
      CHECK_EQ(seed[0].qty, 6'272'315);

      // Two orders at the same price stay two orders. Aggregating them here
      // would destroy queue position before the book ever sees it, which is
      // the one thing L3 data is for.
      CHECK_EQ(seed[1].price, 7'971'538);
      CHECK_EQ(seed[1].order_id, 2046839975448576ULL);
    }
  }

  // ---- the seed guard ----
  // Only the DEEP half of a snapshot may seed the book. Orders near the touch
  // churn in seconds, so the stream reconstructs them exactly and the
  // snapshot's stale ones are contamination — worth 39 points of accuracy on
  // xrpusd, measured as the share of trades printing inside the touch.
  {
    BitstampConfig c = make_cfg();
    c.seed_guard = 1;                    // exclude only orders AT the touch
    BitstampDecoder d{c};
    std::vector<BookEvent> seed;
    d.load_snapshot(kSnapshot, seed);

    // Both bids at 7'971'538 and the ask at 7'972'811 are the touch: excluded.
    // 7'971'524 and 7'973'000 are behind it: kept. The $71,743 bid is far
    // enough out to qualify but falls outside the price window.
    CHECK_EQ(seed.size(), 2u);
    if (seed.size() == 2) {
      CHECK_EQ(seed[0].price, 7'971'524);
      CHECK_EQ(seed[1].price, 7'973'000);
    }
    // Seeded orders ARE registered, unlike the near-touch ones: they go into
    // the book, so the decoder must know about them or it will emit deletes
    // the book rejects as unknown.
    CHECK_EQ(d.tracked_orders(), 2u);
  }

  // A guard wider than the book seeds nothing at all.
  {
    BitstampConfig c = make_cfg();
    c.seed_guard = 1'000'000;
    BitstampDecoder d{c};
    std::vector<BookEvent> seed;
    d.load_snapshot(kSnapshot, seed);
    CHECK_EQ(seed.size(), 0u);
  }

  // A two-column snapshot means the endpoint aggregated by price and there are
  // no order ids: L2 wearing L3's clothes. It must fail loudly.
  {
    BitstampDecoder d{make_cfg()};
    std::vector<BookEvent> seed;
    constexpr const char* l2 =
        R"({"microtimestamp":"1788551766754717","bids":[["79715.38","0.5"]],"asks":[["79716.00","0.5"]]})";
    d.load_snapshot(l2, seed);
    CHECK_EQ(seed.size(), 0u);
    CHECK_EQ(d.stats().parse_errors, 2u);
  }

  // ======================= end to end, into the real book =======================
  {
    BitstampDecoder d{make_cfg()};
    OrderBook book{make_cfg().window_base, 80'000, 1 << 16};

    std::vector<BookEvent> seed;
    d.load_snapshot(kSnapshot, seed);
    for (const BookEvent& e : seed) CHECK(book.apply(e) == BookError::Ok);

    CHECK(book.has_bid());
    CHECK(book.has_ask());
    CHECK_EQ(book.best_bid(), 7'971'538);
    CHECK_EQ(book.best_ask(), 7'972'811);
    // Both snapshot orders at the touch, summed by the book but stored apart.
    CHECK_EQ(book.best_bid_qty(), 6'272'315 + 39'542'753);
    CHECK_EQ(book.orders_at(Side::Bid, 7'971'538), 2u);
    CHECK_EQ(book.front_order_at(Side::Bid, 7'971'538), 2046839968690178ULL);

    std::string why;
    CHECK(book.check_invariants(&why));

    // Now stream the capture lines over that seeded book. kCreateAsk creates an
    // order the snapshot already holds, which is the expected startup transient
    // where the websocket buffer overlaps the REST snapshot.
    Decoded out;
    d.decode_line(kCreateAsk, kNoTouch, out);
    for (int i = 0; i < out.n; ++i) {
      const BookError e = book.apply(out.ev[i]);
      CHECK(e == BookError::DuplicateOrder);
    }
    CHECK(book.check_invariants(&why));
    CHECK_EQ(d.stats().unknown_order, 0u);
  }

  // ======================= a real capture, if one was named =======================
  if (argc >= 3) replay_real_capture(argv[1], argv[2]);
  else std::printf("  (no capture given: pass <capture> <snapshot> for the real-data test)\n");

  return lobtest::summary("bitstamp");
}
