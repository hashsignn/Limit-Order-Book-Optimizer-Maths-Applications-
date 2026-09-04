// Turns an arbitrary byte string into a sequence of book operations.
//
// The contract this exists to test: the book must survive ANY input. Not
// plausible input — any. A real feed hands you truncated packets, sequence
// gaps, references to orders you never saw, and sizes that do not fit. None of
// those may crash, corrupt state, or violate an invariant; every one must come
// back as a returned error.
//
// The decoder is deliberately dumb. It does not construct "valid" events — that
// is the generator's job, and a fuzzer that only produces valid input finds
// nothing. Bytes map straight onto fields, so most inputs are nonsense, which
// is the point.
#pragma once

#include <cstddef>
#include <cstdint>

#include "lob/book/events.hpp"
#include "lob/core/types.hpp"

namespace lobfuzz {

class ByteReader {
 public:
  ByteReader(const std::uint8_t* data, std::size_t size) : d_(data), n_(size) {}

  [[nodiscard]] bool done() const noexcept { return i_ >= n_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return n_ - i_; }

  [[nodiscard]] std::uint8_t u8() noexcept { return i_ < n_ ? d_[i_++] : 0; }

  [[nodiscard]] std::uint16_t u16() noexcept {
    const std::uint16_t a = u8();
    return static_cast<std::uint16_t>((a << 8) | u8());
  }

  [[nodiscard]] std::uint32_t u32() noexcept {
    const std::uint32_t a = u16();
    return (a << 16) | u16();
  }

 private:
  const std::uint8_t* d_;
  std::size_t         n_;
  std::size_t         i_ = 0;
};

// A small id space is deliberate: it forces collisions, so duplicate adds and
// references to just-deleted orders happen constantly rather than by luck.
inline constexpr lob::OrderId kIdSpace = 256;

[[nodiscard]] inline lob::BookEvent decode_event(ByteReader& r) {
  lob::BookEvent e{};
  const std::uint8_t op = r.u8();

  // Weighted so cancels and executes — the operations that touch existing
  // state, and therefore the ones that can corrupt it — dominate.
  const std::uint8_t kind = op % 7;
  switch (kind) {
    case 0: case 1: e.type = lob::EventType::Add;     break;
    case 2:         e.type = lob::EventType::Reduce;  break;
    case 3:         e.type = lob::EventType::Delete;  break;
    case 4:         e.type = lob::EventType::Execute; break;
    case 5:         e.type = lob::EventType::Replace; break;
    default:        e.type = lob::EventType::Aggress; break;
  }

  e.side     = (op & 0x80) ? lob::Side::Ask : lob::Side::Bid;
  e.order_id = r.u8() % kIdSpace;
  e.new_id   = r.u8() % kIdSpace;
  // Prices span well beyond the book's window, so out-of-window rejection is
  // exercised rather than assumed.
  e.price    = static_cast<lob::Ticks>(r.u16());
  // Sizes include 0 and huge values; the book must reject both.
  e.qty      = static_cast<lob::Qty>(r.u16()) - 8;
  e.ts       = static_cast<lob::Nanos>(r.u8()) * 1000;
  return e;
}

}  // namespace lobfuzz
