#include "lob/core/types.hpp"
#include "test_util.hpp"

using namespace lob;

int main() {
  // Default-constructed prices are explicitly invalid, not zero — zero is a
  // real price on some instruments.
  CHECK(!Price{}.valid());
  CHECK(!Price::none().valid());
  CHECK(Price{0}.valid());

  const Price a{100};
  const Price b{105};
  CHECK_EQ(a.ticks(), 100);
  CHECK_EQ(b - a, 5);          // distance is signed ticks, not a Price
  CHECK_EQ(a - b, -5);
  CHECK_EQ((a + 5).ticks(), b.ticks());
  CHECK_EQ((b - 5).ticks(), a.ticks());
  CHECK(a < b);
  CHECK(a != b);

  // "Better" is side-dependent: higher bid, lower ask.
  CHECK(b.better_than(a, Side::Bid));
  CHECK(!a.better_than(b, Side::Bid));
  CHECK(a.better_than(b, Side::Ask));
  CHECK(!b.better_than(a, Side::Ask));
  CHECK(!a.better_than(a, Side::Bid));   // strict

  CHECK(opposite(Side::Bid) == Side::Ask);
  CHECK(opposite(Side::Ask) == Side::Bid);
  CHECK_EQ(sign_of(Side::Bid), 1);
  CHECK_EQ(sign_of(Side::Ask), -1);

  // Rational tick size so venues like US Treasuries (1/32) stay exact.
  constexpr TickSize cents{1, 100};
  CHECK_NEAR(cents.to_double(Price{12345}), 123.45, 1e-9);
  constexpr TickSize thirty_seconds{1, 32};
  CHECK_NEAR(thirty_seconds.to_double(Price{3}), 0.09375, 1e-12);

  // Negative ticks are legal: spreads and price offsets are signed.
  CHECK_EQ(Price{-5}.ticks(), -5);
  CHECK(Price{-5} < Price{0});

  static_assert(sizeof(Price) == 8, "Price must stay register-sized");
  return lobtest::summary("types");
}
