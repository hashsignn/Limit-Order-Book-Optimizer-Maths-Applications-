// Fuzz target: the order book's event path.
//
// Asserts, on every input: no crash, no invariant violation, and no silent
// state corruption. Errors must be RETURNED. A feed with a gap in it looks
// exactly like malicious input, so this is not a hypothetical threat model.
#include <cstdint>
#include <cstdio>
#include <string>

#include "fuzz/decode.hpp"
#include "lob/book/order_book.hpp"

namespace {
constexpr lob::Ticks         kBase   = 20'000;
constexpr std::uint32_t      kWindow = 4'096;
constexpr std::size_t        kOrders = 512;
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 4) return 0;

  lob::OrderBook book{kBase, kWindow, kOrders};
  lobfuzz::ByteReader r{data, size};

  int applied = 0;
  while (!r.done() && applied < 4096) {
    const lob::BookEvent e = lobfuzz::decode_event(r);
    // The return value is deliberately ignored: every error is a legitimate
    // outcome. What is NOT legitimate is the state afterwards being broken.
    (void)book.apply(e);
    ++applied;

    // Full structural check periodically. Every event would be O(window) per
    // event and would slow the fuzzer to uselessness.
    if ((applied & 0x3F) == 0) {
      std::string why;
      if (!book.check_invariants(&why)) {
        std::fprintf(stderr, "INVARIANT VIOLATED after %d events: %s\n", applied, why.c_str());
        __builtin_trap();
      }
    }
  }

  std::string why;
  if (!book.check_invariants(&why)) {
    std::fprintf(stderr, "INVARIANT VIOLATED at end: %s\n", why.c_str());
    __builtin_trap();
  }

  // Reads on a fuzzed book must also be safe, not just writes.
  lob::Ticks px[16]; lob::Qty qy[16];
  (void)book.depth(lob::Side::Bid, 16, px, qy);
  (void)book.depth(lob::Side::Ask, 16, px, qy);
  (void)book.best_bid(); (void)book.best_ask(); (void)book.spread();
  for (lob::OrderId id = 0; id < lobfuzz::kIdSpace; ++id) {
    (void)book.qty_of(id);
    (void)book.queue_ahead(id);
    (void)book.is_mine(id);
  }
  return 0;
}
