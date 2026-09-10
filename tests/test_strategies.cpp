// Strategy tests: the formulas, and the invariants every quoting rule must obey.
//
// These check the MATH, not the profitability. A and k are uncalibrated, so
// profitability is not a thing this repository can currently assert.
#include "lob/strat/quoting.hpp"
#include "test_util.hpp"

#include <cmath>
#include <string>

using namespace lob;

namespace {

OrderBook book_at(Ticks bid, Ticks ask, Qty bq = 100, Qty aq = 100) {
  OrderBook b{9'000, 2048, 4096};
  (void)b.add(1, Side::Bid, bid, bq);
  (void)b.add(2, Side::Ask, ask, aq);
  return b;
}

QuoteParams params() {
  QuoteParams p;
  p.size = 10; p.max_inventory = 100;
  p.gamma = 0.05; p.sigma = 0.5; p.horizon = 1.0;
  p.A = 1.0; p.k = 1.5; p.min_half = 1; p.max_half = 50;
  return p;
}

}  // namespace

int main() {
  const QuoteParams p = params();
  Features f{};
  f.imbalance = 0.0;

  // ---- constant spread is symmetric about the mid, and never crossed ----
  {
    OrderBook b = book_at(9'998, 10'002);
    ConstantSpread s{p, 2};
    const Quote q = s.quote(AgentView{b, f, 0, 0});
    CHECK_EQ(q.ask - 10'000, 10'000 - q.bid);   // symmetric
    CHECK(q.bid < q.ask);
    CHECK(q.bid_on); CHECK(q.ask_on);
  }

  // ---- Ho-Stoll: long inventory shifts BOTH quotes down ----
  // The spread does not change; only its centre moves. That is the whole idea:
  // make selling more likely and buying less likely, without widening.
  {
    OrderBook b = book_at(9'998, 10'002);
    InventorySkew s{p, 2};
    const Quote flat  = s.quote(AgentView{b, f,  0,   0});
    const Quote lng   = s.quote(AgentView{b, f,  0,  50});
    const Quote shrt  = s.quote(AgentView{b, f,  0, -50});

    // Direction only. At these parameters the shift is 0.625 ticks, and on an
    // integer price grid a sub-tick shift can round away entirely — which is
    // exactly why a large-tick instrument is a queue-position game rather than
    // a price-placement one. The skew is real; the grid may swallow it.
    CHECK(lng.bid  <= flat.bid);
    CHECK(lng.ask  <= flat.ask);
    CHECK(shrt.bid >= flat.bid);
    CHECK(shrt.ask >= flat.ask);
    CHECK(std::abs((lng.ask - lng.bid) - (flat.ask - flat.bid)) <= 1);

    // With a shift well over a tick, the movement must actually appear.
    QuoteParams big = p; big.horizon = 20.0;   // 50 * 0.05 * 0.25 * 20 = 12.5 ticks
    InventorySkew s2{big, 2};
    const Quote f2 = s2.quote(AgentView{b, f, 0,   0});
    const Quote l2 = s2.quote(AgentView{b, f, 0,  50});
    const Quote s3 = s2.quote(AgentView{b, f, 0, -50});
    CHECK(l2.bid < f2.bid);
    CHECK(l2.ask < f2.ask);
    CHECK(s3.bid > f2.bid);
    CHECK(s3.ask > f2.ask);
  }

  // ---- reservation price matches the closed form ----
  {
    const double mid = 10'000.0;
    const double r   = InventorySkew::reservation_price(mid, 40, p);
    // r = s - q*gamma*sigma^2*(T-t)
    const double want = mid - 40.0 * p.gamma * p.sigma * p.sigma * p.horizon;
    CHECK_NEAR(r, want, 1e-12);
    CHECK(r < mid);                                     // long => below mid
    CHECK(InventorySkew::reservation_price(mid, -40, p) > mid);
    CHECK_NEAR(InventorySkew::reservation_price(mid, 0, p), mid, 1e-12);
  }

  // ---- Avellaneda-Stoikov optimal spread matches the closed form ----
  {
    const double got  = AvellanedaStoikov::optimal_spread(p);
    const double want = p.gamma * p.sigma * p.sigma * p.horizon
                      + (2.0 / p.gamma) * std::log(1.0 + p.gamma / p.k);
    CHECK_NEAR(got, want, 1e-12);
    CHECK(got > 0.0);

    // More volatility means a wider quote. This one IS monotone: sigma appears
    // only in the inventory-risk term.
    QuoteParams hi_sigma = p; hi_sigma.sigma = 2.0;
    CHECK(AvellanedaStoikov::optimal_spread(hi_sigma) > got);

    // Larger k means the fill intensity decays faster with distance, so quoting
    // wide costs you fills: the optimal quote tightens.
    QuoteParams hi_k = p; hi_k.k = 5.0;
    CHECK(AvellanedaStoikov::optimal_spread(hi_k) < got);

    // Risk aversion is NOT monotone, which is easy to assume and wrong. The two
    // terms move in opposite directions:
    //
    //   gamma*sigma^2*(T-t)      grows with gamma  — more inventory risk
    //   (2/gamma)*ln(1+gamma/k)  SHRINKS with gamma — a more risk-averse trader
    //                            demands less edge per fill, because it wants
    //                            the fill in order to offload risk.
    //
    // At these parameters the second term dominates, so raising gamma from 0.05
    // to 0.20 TIGHTENS the spread. Asserting "more risk aversion => wider" here
    // would encode a plausible-sounding falsehood.
    QuoteParams hi_gamma = p; hi_gamma.gamma = 0.20;
    const double wide_gamma = AvellanedaStoikov::optimal_spread(hi_gamma);
    CHECK(wide_gamma < got);
    // What IS reliable: the inventory-risk half always grows with gamma.
    CHECK(hi_gamma.gamma * hi_gamma.sigma * hi_gamma.sigma * hi_gamma.horizon >
          p.gamma * p.sigma * p.sigma * p.horizon);
  }

  // ---- GLFT: inventory moves the two sides in opposite directions ----
  {
    const double b0 = GLFT::half_bid(0.0, p), a0 = GLFT::half_ask(0.0, p);
    const double b5 = GLFT::half_bid(5.0, p), a5 = GLFT::half_ask(5.0, p);
    // Long: back away from buying (wider bid), lean into selling (tighter ask).
    CHECK(b5 > b0);
    CHECK(a5 < a0);
    // Flat inventory is symmetric.
    CHECK_NEAR(b0, a0, 1e-12);
    // Short is the mirror image.
    CHECK_NEAR(GLFT::half_bid(-5.0, p), a5, 1e-12);
    CHECK_NEAR(GLFT::half_ask(-5.0, p), b5, 1e-12);
    CHECK(GLFT::base(p) > 0.0);
    CHECK(GLFT::inventory_term(p) > 0.0);
  }

  // ---- imbalance tilt leans the quotes, with the documented sign ----
  {
    OrderBook b = book_at(9'998, 10'002);
    ImbalanceSkew s{p};
    Features bid_heavy{}; bid_heavy.imbalance =  0.8;
    Features ask_heavy{}; ask_heavy.imbalance = -0.8;
    const Quote nz = s.quote(AgentView{b, f,         0, 0});
    const Quote bh = s.quote(AgentView{b, bid_heavy, 0, 0});
    const Quote ah = s.quote(AgentView{b, ask_heavy, 0, 0});
    // Bid-heavy: tighten the bid (buy before the move up), widen the ask.
    CHECK(bh.bid >= nz.bid);
    CHECK(bh.ask >= nz.ask);
    CHECK(ah.bid <= nz.bid);
    CHECK(ah.ask <= nz.ask);
  }

  // ---- invariants every strategy must satisfy ----
  {
    OrderBook b = book_at(9'998, 10'002);
    for (const std::int64_t inv : {-150, -100, -50, 0, 50, 100, 150}) {
      const AgentView v{b, f, 0, inv};
      const Quote qs[] = {ConstantSpread{p, 1}.quote(v), InventorySkew{p, 1}.quote(v),
                          AvellanedaStoikov{p}.quote(v), GLFT{p}.quote(v),
                          ImbalanceSkew{p}.quote(v)};
      for (const Quote& q : qs) {
        // Never quote a crossed or locked pair.
        CHECK(q.bid < q.ask);
        // And never quote THROUGH THE MARKET. This is the assertion that was
        // missing: q.bid < q.ask forbids a self-crossed pair and says nothing
        // about the book, so a strategy centred far from the mid passed it
        // while emitting orders that took liquidity on arrival.
        if (q.bid_on) CHECK(q.bid < b.best_ask());
        if (q.ask_on) CHECK(q.ask > b.best_bid());
        // Respect the inventory limit: stop adding to a position at its bound.
        if (inv >=  p.max_inventory) CHECK(!q.bid_on);
        if (inv <= -p.max_inventory) CHECK(!q.ask_on);
        // Half-spreads stay inside the configured bounds.
        CHECK(q.ask - q.bid >= 2 * p.min_half - 1);
      }
    }
  }

  // ---- the same sweep, on the SHIPPED DEFAULTS ----
  // Every other block in this file builds its own QuoteParams, and apps/backtest
  // does too. apps/evaluate does not: base_params() sets size and
  // max_inventory and leaves the risk parameters alone. So the defaults were the
  // one configuration nothing exercised, and they were the broken one.
  {
    OrderBook b = book_at(9'998, 10'002);
    QuoteParams d;                       // defaults, deliberately untouched
    d.size = 10; d.max_inventory = 50;   // as apps/evaluate sets them

    // A market maker whose quote leaves the book at full inventory is crossing,
    // not skewing. Assert the scale directly, so a bad default fails here rather
    // than in a results table.
    ::lobtest::report(d.skew_at_limit() < 20.0, "skew at the position limit is quotable",
                      __FILE__, __LINE__,
                      std::to_string(d.skew_at_limit()) + " ticks at inventory " +
                      std::to_string(d.max_inventory));

    for (const std::int64_t inv : {-60, -50, -25, -1, 0, 1, 25, 50, 60}) {
      const AgentView v{b, f, 0, inv};
      const Quote qs[] = {ConstantSpread{d, 1}.quote(v), InventorySkew{d, 1}.quote(v),
                          AvellanedaStoikov{d}.quote(v), GLFT{d}.quote(v),
                          ImbalanceSkew{d}.quote(v), JoinTouch{d}.quote(v)};
      for (const Quote& q : qs) {
        if (q.bid_on) ::lobtest::report(q.bid < b.best_ask(), "default params: bid never crosses",
                                        __FILE__, __LINE__,
                                        "inv=" + std::to_string(inv) + " bid=" + std::to_string(q.bid));
        if (q.ask_on) ::lobtest::report(q.ask > b.best_bid(), "default params: ask never crosses",
                                        __FILE__, __LINE__,
                                        "inv=" + std::to_string(inv) + " ask=" + std::to_string(q.ask));
      }
    }
  }

  // ---- a one-sided book yields no quote at all ----
  // best_bid() answers 0 for an empty side, so a mid taken from a one-sided book
  // is half the other side's price. Every strategy must decline rather than
  // quote around it.
  {
    OrderBook b{9'000, 2048, 4096};
    (void)b.add(1, Side::Ask, 10'002, 100);       // asks only
    QuoteParams d; d.size = 10; d.max_inventory = 50;
    const AgentView v{b, f, 0, 0};
    const Quote qs[] = {ConstantSpread{d, 1}.quote(v), InventorySkew{d, 1}.quote(v),
                        AvellanedaStoikov{d}.quote(v), GLFT{d}.quote(v),
                        ImbalanceSkew{d}.quote(v), JoinTouch{d}.quote(v)};
    for (const Quote& q : qs) { CHECK(!q.bid_on); CHECK(!q.ask_on); }
  }

  // ---- GLFT's quotes must actually vary with inventory ----
  // half_bid/half_ask are tested above as raw doubles, before assemble()'s
  // clamp. That passed while min_half = 1 erased the entire inventory term and
  // left the clamped quotes constant, so GLFT scored within 3% of
  // ConstantSpread on 60,000 events. Assert the property that matters: the
  // quotes, after clamping, are not the same at flat and at full inventory.
  //
  // On a ONE-TICK book, which is the regime this project exists for. A wide
  // book hides the defect: at a 4-tick spread the mid is a whole tick, so even a
  // clamped 1.0 against 1.2 lands on different ticks. At a one-tick spread the
  // mid sits on a half tick and both clamp to the same quote.
  {
    OrderBook b = book_at(10'000, 10'001);
    QuoteParams d; d.size = 10; d.max_inventory = 50;
    const Quote flat = GLFT{d}.quote(AgentView{b, f, 0,  0});
    const Quote lng  = GLFT{d}.quote(AgentView{b, f, 0, 50});
    ::lobtest::report(flat.bid != lng.bid || flat.ask != lng.ask,
                      "GLFT quotes vary with inventory after clamping", __FILE__, __LINE__,
                      "flat " + std::to_string(flat.bid) + "/" + std::to_string(flat.ask) +
                      "  long " + std::to_string(lng.bid) + "/" + std::to_string(lng.ask));
  }

  return lobtest::summary("strategies");
}
