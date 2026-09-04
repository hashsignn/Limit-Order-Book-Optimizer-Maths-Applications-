// Simulator tests.
//
// Two properties matter here. Determinism, because a run that cannot be
// reproduced cannot be replayed and the journal argument in docs/00 collapses.
// And that latency actually does something — a latency model that changes no
// outcome is decoration.
#include "lob/sim/simulator.hpp"
#include "test_util.hpp"

#include <string>

using namespace lob;

namespace {

// Quotes passively at the touch and never asks to cross. Any aggressive fill it
// gets is therefore caused by latency, not by the strategy.
struct PassiveMaker {
  OrderId next = 900'000'000ULL, bid = 0, ask = 0;
  std::uint64_t seen = 0, last = 0;

  void operator()(const AgentView& v, Simulator& sim) {
    if (!v.book.has_bid() || !v.book.has_ask()) return;
    if (++seen - last < 200) return;
    last = seen;
    if (bid) { sim.send_cancel(bid); bid = 0; }
    if (ask) { sim.send_cancel(ask); ask = 0; }
    bid = next++; sim.send_limit(bid, Side::Bid, v.book.best_bid(), 10);
    ask = next++; sim.send_limit(ask, Side::Ask, v.book.best_ask(), 10);
  }
};

SimConfig config(bool latency) {
  SimConfig c;
  c.use_latency       = latency;
  c.flow.seed         = 4242;
  c.flow.mid          = 10'000;
  c.flow.levels       = 8;
  c.flow.target_live  = 2'000;
  c.latency.seed      = 11;
  c.latency.median_ns = 1'000'000;
  return c;
}

SimStats run(bool latency, int n) {
  Simulator sim{config(latency)};
  PassiveMaker agent;
  sim.run(agent, n);
  std::string why;
  ::lobtest::report(sim.true_book().check_invariants(&why), "book invariants after sim",
                    __FILE__, __LINE__, why);
  return sim.stats();
}

}  // namespace

int main() {
  // ---- determinism: identical inputs must give byte-identical outcomes ----
  {
    const SimStats a = run(true, 40'000);
    const SimStats b = run(true, 40'000);
    CHECK_EQ(a.market_events,    b.market_events);
    CHECK_EQ(a.agent_actions,    b.agent_actions);
    CHECK_EQ(a.our_fills,        b.our_fills);
    CHECK_EQ(a.passive_fills,    b.passive_fills);
    CHECK_EQ(a.aggressive_fills, b.aggressive_fills);
    CHECK_EQ(a.our_filled_qty,   b.our_filled_qty);
    CHECK_EQ(a.late_cancels,     b.late_cancels);
    CHECK_EQ(a.inventory,        b.inventory);
    CHECK_NEAR(a.realised_pnl,   b.realised_pnl, 1e-9);
  }

  // ---- the flow actually trades against us ----
  // If this fails the fill model is not being exercised at all, which was true
  // of an earlier version of the generator and made the whole simulator vacuous.
  {
    const SimStats s = run(false, 200'000);
    CHECK(s.market_events == 200'000U);
    ::lobtest::report(s.passive_fills > 0, "passive fills happen", __FILE__, __LINE__,
                      std::to_string(s.passive_fills) + " passive fills");
  }

  // ---- latency changes outcomes, and in the direction the theory predicts ----
  {
    const SimStats zero = run(false, 200'000);
    const SimStats slow = run(true,  200'000);

    // A purely passive agent cannot cross by itself. With zero latency it never
    // does. Under latency its quotes are decided on a stale book, so some land
    // crossing — liquidity provision silently becomes liquidity taking.
    CHECK_EQ(zero.aggressive_fills, 0U);
    ::lobtest::report(slow.aggressive_fills > 0, "latency causes unintended aggression",
                      __FILE__, __LINE__,
                      std::to_string(slow.aggressive_fills) + " aggressive fills");

    // And cancels start arriving after the order has already traded.
    ::lobtest::report(slow.late_cancels > zero.late_cancels, "latency causes late cancels",
                      __FILE__, __LINE__,
                      std::to_string(zero.late_cancels) + " -> " + std::to_string(slow.late_cancels));
  }

  // ---- an agent that does nothing must leave no trace ----
  {
    Simulator sim{config(true)};
    auto noop = [](const AgentView&, Simulator&) {};
    sim.run(noop, 20'000);
    CHECK_EQ(sim.stats().agent_actions, 0U);
    CHECK_EQ(sim.stats().our_fills, 0U);
    CHECK_EQ(sim.stats().inventory, 0);
    std::string why;
    ::lobtest::report(sim.true_book().check_invariants(&why), "invariants with no agent",
                      __FILE__, __LINE__, why);
  }

  return lobtest::summary("simulator");
}
