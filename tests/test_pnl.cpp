// P&L attribution and bootstrap tests.
//
// The identity that has to hold exactly:
//   markout = spread_capture - adverse_selection
// Those are not two estimates of one thing — they are the two halves a fill
// decomposes into. If they ever disagree, every attribution table is wrong.
#include "lob/strat/pnl.hpp"
#include "test_util.hpp"

#include <cmath>
#include <random>

using namespace lob;

int main() {
  // ---- the decomposition identity, both sides, both directions ----
  {
    // Bought 100 at 9'999 when the mid was 10'000: earned 1 tick of spread.
    MarkedFill f;
    f.ts = 0; f.price = 9'999; f.qty = 100; f.sign = +1; f.mid_at = 10'000.0;
    f.mid_later[0] = 9'998.0; f.filled[0] = true;   // mid then FELL: bad for a buyer

    CHECK_NEAR(f.spread_capture(),       100.0, 1e-9);   // (10000-9999)*100
    CHECK_NEAR(f.adverse_selection(0),   200.0, 1e-9);   // (10000-9998)*100, positive = cost
    CHECK_NEAR(f.markout(0),            -100.0, 1e-9);   // (9998-9999)*100
    CHECK_NEAR(f.markout(0), f.spread_capture() - f.adverse_selection(0), 1e-9);

    // Same buy, but the mid ROSE: adverse selection is negative, i.e. a gain.
    MarkedFill g = f;
    g.mid_later[0] = 10'003.0;
    CHECK_NEAR(g.adverse_selection(0), -300.0, 1e-9);
    CHECK_NEAR(g.markout(0),            400.0, 1e-9);
    CHECK_NEAR(g.markout(0), g.spread_capture() - g.adverse_selection(0), 1e-9);

    // Sold 100 at 10'001 with mid 10'000, and the mid then rose: bad for a seller.
    MarkedFill s;
    s.price = 10'001; s.qty = 100; s.sign = -1; s.mid_at = 10'000.0;
    s.mid_later[0] = 10'004.0; s.filled[0] = true;
    CHECK_NEAR(s.spread_capture(),      100.0, 1e-9);
    CHECK_NEAR(s.adverse_selection(0),  400.0, 1e-9);
    CHECK_NEAR(s.markout(0),           -300.0, 1e-9);
    CHECK_NEAR(s.markout(0), s.spread_capture() - s.adverse_selection(0), 1e-9);
  }

  // ---- an unreached horizon contributes nothing, rather than zero ----
  // A fill near the end of a run never gets its long markouts. Counting those
  // as zero would drag every long-horizon mean toward zero.
  {
    MarkedFill f;
    f.price = 100; f.qty = 10; f.sign = +1; f.mid_at = 101.0;
    CHECK_NEAR(f.markout(4), 0.0, 1e-12);
    CHECK(!f.filled[4]);

    std::vector<MarkedFill> v{f};
    const Attribution a = attribute(v, 4, FeeSchedule{}, 101.0, 0);
    CHECK_EQ(a.n_fills, 1U);
    CHECK_EQ(a.markout_n[4], 0U);          // not counted at all
    CHECK_NEAR(a.adverse_sel, 0.0, 1e-12);
  }

  // ---- markout tracker fills horizons as time passes, and only then ----
  {
    MarkoutTracker mk;
    mk.on_fill(0, 100, 10, +1, /*passive=*/true, 101.0);

    mk.advance(50'000, 102.0);                       // before the 100us horizon
    CHECK(!mk.fills()[0].filled[0]);

    mk.advance(150'000, 103.0);                      // past 100us, before 1ms
    CHECK(mk.fills()[0].filled[0]);
    CHECK_NEAR(mk.fills()[0].mid_later[0], 103.0, 1e-12);
    CHECK(!mk.fills()[0].filled[1]);

    mk.advance(2'000'000'000LL, 110.0);              // past everything
    for (std::size_t h = 0; h < kNumHorizons; ++h) CHECK(mk.fills()[0].filled[h]);
    // The 100us mid was recorded earlier and must NOT be overwritten later.
    CHECK_NEAR(mk.fills()[0].mid_later[0], 103.0, 1e-12);
    CHECK_NEAR(mk.fills()[0].mid_later[4], 110.0, 1e-12);
  }

  // ---- fees: makers earn the rebate, takers pay ----
  {
    MarkedFill passive; passive.price = 100; passive.qty = 100; passive.sign = +1;
    passive.mid_at = 100.0; passive.mid_later[3] = 100.0; passive.filled[3] = true;
    passive.passive = true;
    MarkedFill taker = passive; taker.passive = false;

    FeeSchedule fees; fees.maker_per_share = -0.02; fees.taker_per_share = 0.03;
    const Attribution a = attribute({passive}, 3, fees, 100.0, 0);
    const Attribution b = attribute({taker},   3, fees, 100.0, 0);
    CHECK_NEAR(a.fees, -2.0, 1e-9);        // rebate earned
    CHECK_NEAR(b.fees,  3.0, 1e-9);        // fee paid
    CHECK(a.total > b.total);              // same trade, different economics
    CHECK_EQ(a.n_passive, 1U);
    CHECK_EQ(b.n_aggressive, 1U);
  }

  // ---- bootstrap sanity ----
  {
    CHECK_EQ(block_bootstrap({}).n_blocks, 0U);

    // Constant data has no sampling variation, so the interval collapses.
    std::vector<double> flat(500, 3.0);
    const BootstrapCI c = block_bootstrap(flat, 50, 500);
    CHECK_NEAR(c.mean, 3.0, 1e-9);
    CHECK_NEAR(c.lo,   3.0, 1e-9);
    CHECK_NEAR(c.hi,   3.0, 1e-9);
    CHECK(c.excludes_zero());

    // Data centred on zero must NOT be called significant. Deliberately varied
    // ACROSS blocks: alternating +1/-1 would make every 50-element block sum to
    // exactly zero, leaving no sampling variation for the bootstrap to find.
    std::vector<double> noise;
    std::mt19937_64 rng{99};
    std::normal_distribution<double> nd{0.0, 1.0};
    for (int i = 0; i < 2000; ++i) noise.push_back(nd(rng));
    const BootstrapCI z = block_bootstrap(noise, 50, 1000);
    CHECK(std::fabs(z.mean) < 0.1);
    CHECK(z.lo < 0.0);
    CHECK(z.hi > 0.0);
    CHECK(!z.excludes_zero());

    // A degenerate interval sitting exactly on zero is not significant either.
    std::vector<double> zeros(200, 0.0);
    CHECK(!block_bootstrap(zeros, 50, 200).excludes_zero());

    // The interval must bracket the mean, and be ordered.
    std::vector<double> mixed;
    for (int i = 0; i < 1000; ++i) mixed.push_back(2.0 + ((i % 7) - 3) * 0.5);
    const BootstrapCI m = block_bootstrap(mixed, 50, 1000);
    CHECK(m.lo <= m.mean);
    CHECK(m.mean <= m.hi);
    CHECK(m.lo < m.hi);
  }

  return lobtest::summary("pnl");
}
