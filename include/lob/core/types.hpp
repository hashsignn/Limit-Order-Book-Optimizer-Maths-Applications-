// Core value types. Everything price-related is integer ticks — never floating
// point. A price that depends on rounding mode is a price that makes the
// backtest irreproducible.
#pragma once

#include <cstdint>
#include <compare>
#include <limits>

namespace lob {

using Ticks   = std::int64_t;   // signed: differences are first-class
using Qty     = std::int64_t;
using OrderId = std::uint64_t;
using SeqNum  = std::uint64_t;
using Nanos   = std::int64_t;   // duration or epoch offset, nanoseconds

inline constexpr Ticks kNoPrice = std::numeric_limits<Ticks>::min();

enum class Side : std::uint8_t { Bid = 0, Ask = 1 };

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
  return s == Side::Bid ? Side::Ask : Side::Bid;
}

// +1 for a bid, -1 for an ask. Lets you write side-agnostic arithmetic:
// a bid improves by moving up, an ask by moving down.
[[nodiscard]] constexpr int sign_of(Side s) noexcept {
  return s == Side::Bid ? 1 : -1;
}

// A price, held as an integer number of ticks from the instrument's price
// origin. Conversion to a human-facing decimal happens at the edges only
// (feed decode in, display out) and needs the instrument's tick size.
class Price {
 public:
  Price() = default;
  explicit constexpr Price(Ticks t) noexcept : ticks_(t) {}

  [[nodiscard]] constexpr Ticks ticks() const noexcept { return ticks_; }
  [[nodiscard]] constexpr bool valid() const noexcept { return ticks_ != kNoPrice; }

  [[nodiscard]] static constexpr Price none() noexcept { return Price{kNoPrice}; }

  constexpr auto operator<=>(const Price&) const noexcept = default;

  constexpr Price& operator+=(Ticks d) noexcept { ticks_ += d; return *this; }
  constexpr Price& operator-=(Ticks d) noexcept { ticks_ -= d; return *this; }

  friend constexpr Price operator+(Price p, Ticks d) noexcept { return Price{p.ticks_ + d}; }
  friend constexpr Price operator-(Price p, Ticks d) noexcept { return Price{p.ticks_ - d}; }
  // Distance in ticks. Signed, and deliberately not a Price.
  friend constexpr Ticks operator-(Price a, Price b) noexcept { return a.ticks_ - b.ticks_; }

  // "Better" means higher for a bid, lower for an ask.
  [[nodiscard]] constexpr bool better_than(Price other, Side s) const noexcept {
    return s == Side::Bid ? ticks_ > other.ticks_ : ticks_ < other.ticks_;
  }

 private:
  Ticks ticks_ = kNoPrice;
};

static_assert(sizeof(Price) == 8);

// Tick size lives with the instrument, not the price. Kept as a rational so
// venues with tick sizes like 1/32 (US Treasuries) are exact.
struct TickSize {
  std::int64_t numerator   = 1;
  std::int64_t denominator = 100;   // default: 1 cent

  [[nodiscard]] constexpr double to_double(Price p) const noexcept {
    return static_cast<double>(p.ticks()) * static_cast<double>(numerator)
         / static_cast<double>(denominator);
  }
};

}  // namespace lob
