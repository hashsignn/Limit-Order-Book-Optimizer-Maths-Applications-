// Fuzz target: the Bitstamp JSON scanner and decoder.
//
// This is the only code in the project that parses bytes from a public network.
// Everything else consumes BookEvent, which is a POD the project built itself;
// this file consumes whatever a websocket handed to a Python script, and a
// capture file can be truncated mid-frame by a Ctrl-C, corrupted on disk, or
// simply describe a day the exchange changed its schema.
//
// The contract, asserted on every input: the scanner is TOTAL. Any byte string
// yields an answer — a value, an empty view, or false — and never a read past
// the end, never a crash, never unbounded recursion. Run under ASan and UBSan
// that is a real check rather than a hopeful one.
//
// The book is driven with whatever comes out, because a decoder that survives
// garbage but emits events that break the book has moved the bug rather than
// fixed it.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "lob/book/order_book.hpp"
#include "lob/feed/bitstamp.hpp"
#include "lob/feed/json.hpp"

namespace {

constexpr lob::Ticks    kBase   = 7'930'000;
constexpr std::uint32_t kWindow = 8'192;

lob::BitstampConfig cfg() {
  lob::BitstampConfig c;
  c.price_decimals = 2;
  c.qty_decimals   = 8;
  c.window_base    = kBase;
  c.window_ticks   = kWindow;
  return c;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) return 0;

  const std::string_view all{reinterpret_cast<const char*>(data), size};

  // ---- the scanner, on its own ----
  for (const char* key : {"data", "event", "id_str", "amount_traded", "bids", "asks", "", "a"}) {
    const lob::json::View v = lob::json::find(all, key);
    // A returned view must point INSIDE the input. A view that does not is the
    // signature of an out-of-bounds substr that happened to not fault.
    if (!v.empty() && (v.data() < all.data() || v.data() + v.size() > all.data() + all.size())) {
      std::fprintf(stderr, "find() returned a view outside its input\n");
      __builtin_trap();
    }
    (void)lob::json::find_scalar(all, key);

    std::int64_t d = 0;
    (void)lob::json::parse_decimal(v, 8, &d);
    std::uint64_t u = 0;
    (void)lob::json::parse_u64(v, &u);
  }

  // Array walking must terminate on any input, including one that is all '['.
  {
    std::size_t i = 0;
    lob::json::View elem;
    int guard = 0;
    while (lob::json::array_next(all, i, &elem)) {
      if (++guard > 100'000) {
        std::fprintf(stderr, "array_next did not terminate\n");
        __builtin_trap();
      }
    }
  }

  // ---- the decoder, and the book behind it ----
  lob::BitstampDecoder dec{cfg()};
  lob::OrderBook book{kBase, kWindow, 4096};

  std::vector<lob::BookEvent> seed;
  (void)dec.load_snapshot(all, seed);
  for (const lob::BookEvent& e : seed) (void)book.apply(e);

  // Split the input on newlines, exactly as the replay path does: a capture
  // file is JSONL, so a truncated final line is the normal end-of-file case
  // after an interrupted recording rather than an exotic one.
  std::size_t start = 0;
  int lines = 0;
  while (start < all.size() && lines < 2048) {
    std::size_t nl = all.find('\n', start);
    if (nl == std::string_view::npos) nl = all.size();
    lob::Decoded out;
    (void)dec.decode_line(all.substr(start, nl - start), out);
    for (int i = 0; i < out.n; ++i) (void)book.apply(out.ev[i]);
    start = nl + 1;
    ++lines;
  }

  // Whatever the decoder emitted, the book must still be internally consistent.
  std::string why;
  if (!book.check_invariants(&why)) {
    std::fprintf(stderr, "INVARIANT VIOLATED after decode: %s\n", why.c_str());
    __builtin_trap();
  }
  return 0;
}
