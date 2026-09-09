// Differential test: the fast book against a deliberately naive one.
//
// This is the test that actually establishes correctness. Hand-written cases
// check the behaviours you thought of; a million random events check the ones
// you did not — interleavings of partial fills, cancels either side of an own
// order, levels emptying and refilling under the touch, replaces that move
// across the spread.
//
// Any divergence in observable state is a failure, and the seed is printed so
// a failure is reproducible.
#include "lob/book/order_book.hpp"
#include "lob/book/reference_book.hpp"
#include "lob/sim/flow.hpp"
#include "test_util.hpp"

#include <cstdio>
#include <string>

using namespace lob;

namespace {

constexpr Ticks         kBase   = 5'000;
constexpr std::uint32_t kWindow = 10'240;
constexpr std::size_t   kOrders = 1 << 16;

// Every observable the two books share must agree.
bool same_state(const OrderBook& fast, const ReferenceBook& ref, std::string* why) {
  auto fail = [&](const std::string& m) { if (why) *why = m; return false; };

  if (fast.has_bid() != ref.has_bid()) return fail("has_bid differs");
  if (fast.has_ask() != ref.has_ask()) return fail("has_ask differs");
  if (fast.has_bid() && fast.best_bid() != ref.best_bid())
    return fail("best_bid " + std::to_string(fast.best_bid()) + " vs " + std::to_string(ref.best_bid()));
  if (fast.has_ask() && fast.best_ask() != ref.best_ask())
    return fail("best_ask " + std::to_string(fast.best_ask()) + " vs " + std::to_string(ref.best_ask()));
  if (fast.live_orders() != ref.live_orders())
    return fail("live orders " + std::to_string(fast.live_orders()) + " vs " + std::to_string(ref.live_orders()));

  // Compare the top 20 levels a side, which is where any real strategy looks.
  for (int si = 0; si < 2; ++si) {
    const Side s = static_cast<Side>(si);
    Ticks p[20]; Qty q[20];
    const std::uint32_t n = fast.depth(s, 20, p, q);
    const auto rl = ref.ladder(s, 20);
    if (n != rl.size())
      return fail(std::string(si ? "ask" : "bid") + " ladder depth " +
                  std::to_string(n) + " vs " + std::to_string(rl.size()));
    for (std::uint32_t i = 0; i < n; ++i) {
      if (p[i] != rl[i].first)
        return fail("ladder price at " + std::to_string(i) + ": " +
                    std::to_string(p[i]) + " vs " + std::to_string(rl[i].first));
      if (q[i] != rl[i].second)
        return fail("ladder qty at price " + std::to_string(p[i]) + ": " +
                    std::to_string(q[i]) + " vs " + std::to_string(rl[i].second));
    }
  }
  return true;
}

// One run over `n` events with a given seed. Returns false on first divergence.
bool run(std::uint64_t seed, int n, bool with_own, std::string* why) {
  FlowConfig cfg;
  // The book must be driven through its Execute path to be proved correct, and
  // the generator no longer fabricates one by default -- see FlowConfig::w_execute.
  cfg.w_execute = 0.09;
  cfg.seed   = seed;
  cfg.mid    = 10'000;
  cfg.levels = 12;
  FlowGenerator gen{cfg};

  OrderBook     fast{kBase, kWindow, kOrders};
  ReferenceBook ref;

  // A handful of our own orders, so the queue-position bookkeeping is exercised
  // under the same churn as everything else.
  OrderId our_next = 1'000'000'000ULL;
  std::vector<OrderId> ours;

  for (int i = 0; i < n; ++i) {
    const BookEvent e = gen.next();

    const BookError fe = fast.apply(e);
    const BookError re = ref.apply(e);
    if (fe != re) {
      *why = "event " + std::to_string(i) + " " + std::string(event_name(e.type)) +
             ": fast=" + std::string(error_name(fe)) + " ref=" + std::string(error_name(re));
      return false;
    }

    // Tell the generator what actually happened, so its next reference stays
    // valid: after a partial fill the order is still there with less size, and
    // after a full one it is gone.
    gen.on_applied(e, ref.qty_of(e.order_id));
    gen.observe(fast.has_bid(), fast.best_bid(), fast.has_ask(), fast.best_ask());

    if (with_own && (i % 500 == 0) && fast.has_bid()) {
      // Place one of ours at the touch, and retire an old one.
      const OrderId id = our_next++;
      if (fast.add(id, Side::Bid, fast.best_bid(), 7, /*mine=*/true) == BookError::Ok) {
        (void)ref.add(id, Side::Bid, fast.best_bid(), 7, true);
        ours.push_back(id);
      }
      if (ours.size() > 4) {
        const OrderId old = ours.front();
        ours.erase(ours.begin());
        if (fast.remove(old) == BookError::Ok) (void)ref.remove(old);
      }
    }

    // Our tracked queue position must match the reference's honest walk.
    if (with_own) {
      for (const OrderId id : ours) {
        const Qty a = fast.queue_ahead(id);
        const Qty b = ref.queue_ahead(id);
        if (a >= 0 && b >= 0 && a != b) {
          *why = "event " + std::to_string(i) + ": queue_ahead(" + std::to_string(id) +
                 ") fast=" + std::to_string(a) + " ref=" + std::to_string(b);
          return false;
        }
      }
    }

    // Full structural check periodically — it is O(window) so not every event.
    if (i % 2000 == 0) {
      std::string inv;
      if (!fast.check_invariants(&inv)) {
        *why = "event " + std::to_string(i) + ": " + inv;
        return false;
      }
      if (!same_state(fast, ref, why)) {
        *why = "event " + std::to_string(i) + ": " + *why;
        return false;
      }
    }
  }

  std::string inv;
  if (!fast.check_invariants(&inv)) { *why = "final: " + inv; return false; }
  if (!same_state(fast, ref, why))  { *why = "final: " + *why; return false; }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  // Default is sized so the sanitizer builds stay quick; --long is the thorough
  // pass the optimised CI job runs. Both use the same code and the same seeds,
  // so a failure at either size reproduces at the other.
  const bool longer = argc > 1 && std::string(argv[1]) == "--long";
  const int  short_n = longer ? 40'000  : 12'000;
  const int  deep_n  = longer ? 250'000 : 50'000;

  // Several seeds: one long run can miss a state a different seed reaches early.
  const std::uint64_t seeds[] = {1, 7, 20260904, 0xDEADBEEF, 999331};
  for (const std::uint64_t seed : seeds) {
    std::string why;
    const bool ok = run(seed, short_n, /*with_own=*/true, &why);
    ::lobtest::report(ok, "differential run", __FILE__, __LINE__,
                      "seed=" + std::to_string(seed) + (ok ? "" : "  " + why));
    if (!ok) std::fprintf(stderr, "  reproduce with seed %llu\n",
                          static_cast<unsigned long long>(seed));
  }

  // One long run, to reach states short runs do not.
  {
    std::string why;
    const bool ok = run(4242, deep_n, /*with_own=*/true, &why);
    ::lobtest::report(ok, "long differential run", __FILE__, __LINE__, ok ? "" : why);
  }

  return lobtest::summary("book_diff");
}
