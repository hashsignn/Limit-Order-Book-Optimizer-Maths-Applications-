// Fuzz target: the matching engine.
//
// The matcher walks the book while mutating it, which is the shape of code that
// spins forever or reads freed memory on a malformed sequence. It also runs the
// self-match path, where an aggressor and a resting order both belong to us.
#include <cstdint>
#include <cstdio>
#include <string>

#include "fuzz/decode.hpp"
#include "lob/book/order_book.hpp"
#include "lob/sim/matching.hpp"

namespace {
constexpr lob::Ticks    kBase   = 20'000;
constexpr std::uint32_t kWindow = 4'096;
constexpr std::size_t   kOrders = 512;
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 4) return 0;

  lob::OrderBook book{kBase, kWindow, kOrders};
  // All three modes, chosen from the input. Only CancelResting was ever
  // entered, so Allow and CancelIncoming -- which differ in what happens to the
  // aggressor's remainder -- were unreachable code as far as the fuzzer knew.
  const auto mode = static_cast<lob::SelfMatch>(data[0] % 3);
  lob::MatchingEngine match{book, mode};
  lobfuzz::ByteReader r{data, size};

  int applied = 0;
  while (!r.done() && applied < 2048) {
    const lob::BookEvent e = lobfuzz::decode_event(r);
    // Alternate ownership so the self-match branch is reached, rather than
    // being dead code the fuzzer never enters.
    const bool mine = (applied & 3) == 0;

    switch (e.type) {
      case lob::EventType::Aggress:
        (void)match.submit_market(e.ts, e.order_id, e.side, e.qty, mine);
        break;
      case lob::EventType::Add:
        (void)match.submit_limit(e.ts, e.order_id, e.side, e.price, e.qty, mine);
        break;
      case lob::EventType::Delete:
        (void)match.cancel(e.order_id);
        break;
      default:
        (void)book.apply(e);
        break;
    }
    ++applied;

    if ((applied & 0x1F) == 0) {
      std::string why;
      if (!book.check_invariants(&why)) {
        std::fprintf(stderr, "INVARIANT VIOLATED after %d ops: %s\n", applied, why.c_str());
        __builtin_trap();
      }
      // A matcher that leaves the book crossed has mismatched something.
      if (book.has_bid() && book.has_ask() && book.best_bid() >= book.best_ask()) {
        std::fprintf(stderr, "CROSSED BOOK after matching\n");
        __builtin_trap();
      }
      match.clear_fills();   // bound memory across a long input
    }
  }

  std::string why;
  if (!book.check_invariants(&why)) {
    std::fprintf(stderr, "INVARIANT VIOLATED at end: %s\n", why.c_str());
    __builtin_trap();
  }
  return 0;
}
