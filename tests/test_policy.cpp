// Phase 5 tests: the discretisation, the process, the solve, and the artefact.
//
// The load-bearing one is that every action's successor probabilities sum to
// one. A transition function that leaks probability does not crash and does not
// look wrong — it quietly discounts the future by however much it lost, and the
// policy that comes out is optimal for a process nobody wrote down.
#include "lob/policy/mdp.hpp"
#include "lob/policy/state.hpp"
#include "lob/policy/table.hpp"
#include "lob/strat/tabulated.hpp"
#include "lob/measure/tsc.hpp"
#include "test_util.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace lob;
using namespace lob::policy;

namespace {

// A process with everything in range, so the structural tests exercise real
// branches rather than a degenerate all-zero model.
MdpParams demo() {
  MdpParams p;
  p.dt_s = 0.1;
  p.move_ticks = 0.5;
  p.discount = 0.995;
  p.inventory_penalty = 0.02;
  for (int q = 1; q < kQueueBuckets; ++q) p.p_advance[q] = 0.4 / static_cast<double>(q);
  for (int i = 0; i < kImbBuckets; ++i) {
    p.p_up[i]   = 0.01 + 0.008 * i;      // bid-heavy leans up, as measured
    p.p_down[i] = 0.05 - 0.008 * i;
    for (int j = 0; j < kImbBuckets; ++j)
      p.imb_transition[i][j] = (i == j) ? 0.6 : 0.1;
  }
  for (int q = 0; q < kQueueBuckets; ++q) {
    p.p_fill[0][q] = 0.004 / static_cast<double>(q + 1);
    for (int l = 1; l < kQuoteLevels; ++l) p.p_fill[l][q] = p.p_fill[l - 1][q] * 0.4;
  }
  for (int l = 0; l < kQuoteLevels; ++l) p.edge_ticks[l] = 0.5 + l;
  return p;
}

// The scale queue buckets are fractions of. Any positive number does; the tests
// that care fix it so the bucket boundaries land where they expect.
constexpr std::int64_t kTestScale = 1000;

}  // namespace

int main() {
  // ---- the discretisation round-trips, for every state ----
  {
    for (std::uint32_t c = 0; c < kNumStates; ++c) {
      const State s = decode(c);
      CHECK(s.inventory >= -kMaxInventory && s.inventory <= kMaxInventory);
      CHECK(s.bid >= 0 && s.bid < kSideStates);
      CHECK(s.ask >= 0 && s.ask < kSideStates);
      CHECK(s.imb >= 0 && s.imb < kImbBuckets);
      if (encode(s) != c) { CHECK_EQ(encode(s), c); break; }
    }
    for (int a = 0; a < kNumActions; ++a)
      CHECK_EQ(encode_action(decode_action(static_cast<std::uint8_t>(a))), a);

    // Side-state helpers agree with each other over the whole range.
    CHECK(!quoting(kNoQuote));
    for (int l = 0; l < kQuoteLevels; ++l)
      for (int q = 0; q < kQueueBuckets; ++q) {
        const int ss = make_side(l, q);
        CHECK(quoting(ss));
        CHECK_EQ(level_of(ss), l);
        CHECK_EQ(queue_of(ss), q);
        CHECK(ss > 0 && ss < kSideStates);
      }
  }

  // ---- run-time bucketing ----
  {
    CHECK_EQ(imb_bucket(-1.0), 0);
    CHECK_EQ(imb_bucket(0.0), 2);
    CHECK_EQ(imb_bucket(1.0), kImbBuckets - 1);
    for (double x = -1.0; x <= 1.0; x += 0.05) {
      const int b = imb_bucket(x);
      CHECK(b >= 0 && b < kImbBuckets);
    }
    // Volume ahead, as a fraction of the reference depth — not a rank. Alone
    // gets its own bucket: nothing has to trade before we do, which is a
    // different state from "a little has to", not a small amount of it.
    CHECK_EQ(queue_bucket(0, 1000), 0);
    CHECK_EQ(queue_bucket(0, 0), 0);
    CHECK_EQ(queue_bucket(1, 1000), 1);        // 0.1% of a queue
    CHECK_EQ(queue_bucket(20, 1000), 1);       // 2.0%, on the edge, still bucket 1
    CHECK_EQ(queue_bucket(21, 1000), 2);       // just past it
    CHECK_EQ(queue_bucket(100, 1000), 2);      // 10%, on the edge
    CHECK_EQ(queue_bucket(101, 1000), 3);
    CHECK_EQ(queue_bucket(350, 1000), 3);      // 35%, on the edge
    CHECK_EQ(queue_bucket(351, 1000), kQueueBuckets - 1);
    CHECK_EQ(queue_bucket(999, 1000), kQueueBuckets - 1);
    CHECK_EQ(queue_bucket(5000, 1000), kQueueBuckets - 1);   // never past the end
    // No scale is not an excuse to guess a good bucket.
    CHECK_EQ(queue_bucket(1, 0), kQueueBuckets - 1);
    // Monotone in volume ahead. If this ever fails the buckets are not ordered
    // and every comparison the solver makes between them is meaningless.
    int prev = 0;
    for (long long a = 0; a <= 2000; a += 7) {
      const int b = queue_bucket(a, 1000);
      CHECK(b >= 0 && b < kQueueBuckets);
      CHECK(b >= prev);
      prev = b;
    }
    // The bucket geometry the solver turns a drain rate into transitions with.
    for (int b = 1; b < kQueueBuckets; ++b) {
      CHECK(queue_typical(b) > queue_floor(b));
      CHECK(queue_floor(b) >= queue_floor(b - 1));
      // A bucket's representative volume must actually land in that bucket.
      if (b < kQueueBuckets - 1)
        CHECK_EQ(queue_bucket(static_cast<long long>(queue_typical(b) * 1e6), 1'000'000), b);
    }
    CHECK_EQ(queue_typical(0), 0.0);
  }

  // ---- the process is a process ----
  {
    const MdpParams p = demo();
    std::string why;
    CHECK(p.validate(&why));

    std::vector<Transition> tr;
    double worst = 0.0;
    std::uint32_t checked = 0;
    for (std::uint32_t s = 0; s < kNumStates; ++s) {
      for (std::uint8_t a = 0; a < kNumActions; ++a) {
        if (!admissible(s, a)) continue;
        tr.clear();
        expand(p, s, a, tr);
        double sum = 0.0;
        for (const Transition& t : tr) {
          CHECK(t.prob >= 0.0);
          CHECK(t.next < kNumStates);
          CHECK(std::isfinite(t.reward));
          sum += t.prob;
        }
        worst = std::max(worst, std::fabs(sum - 1.0));
        ++checked;
      }
    }
    ::lobtest::report(worst < 1e-9, "successor probabilities sum to 1", __FILE__, __LINE__,
                      "worst deviation " + std::to_string(worst));
    CHECK(checked > kNumStates);   // every state had at least one legal action
    CHECK(stochastic(p, &why));    // and the shipped check agrees
  }

  // ---- the same, with fill probabilities a real calibration produces ----
  //
  // The demo process above fills at 0.4% an epoch, and at those numbers almost
  // any transition function sums to one by accident. Calibrated on a 1 ms epoch
  // this process fills at 92% at the front of the queue, and there the earlier
  // model — which treated the two sides as mutually exclusive and called the
  // remainder (1 - p_bid - p_ask) — produced a NEGATIVE remainder, dropped it,
  // and summed to 1.8. Value iteration on that diverged to 2e+57.
  //
  // A market maker at the touch on a fast book gets both sides filled in the
  // same epoch all the time. That is the business. The test is that the model
  // can represent it.
  {
    MdpParams p = demo();
    for (int l = 0; l < kQuoteLevels; ++l)
      for (int q = 0; q < kQueueBuckets; ++q)
        p.p_fill[l][q] = 0.92;                 // both sides, near-certain, every epoch
    for (int q = 1; q < kQueueBuckets; ++q) p.p_advance[q] = 1.0;
    std::string why;
    CHECK(p.validate(&why));
    ::lobtest::report(stochastic(p, &why), "a fast process is still a probability distribution",
                      __FILE__, __LINE__, why);

    // Both sides filling in one epoch is a REACHABLE outcome, not a rounding
    // error. Quoting both at the touch with nothing ahead, it is the single most
    // likely thing that happens.
    std::vector<Transition> tr;
    const State s0{0, make_side(0, 0), make_side(0, 0), 2};
    expand(p, encode(s0), encode_action(policy::Action{1, 1}), tr);
    double both = 0.0;
    for (const Transition& t : tr) {
      const State n = decode(t.next);
      if (n.inventory == 0 && !quoting(n.bid) && !quoting(n.ask)) both += t.prob;
    }
    ::lobtest::report(both > 0.5, "both sides can fill in one epoch", __FILE__, __LINE__,
                      "P(both filled) = " + std::to_string(both));

    // And it converges, which the mutually-exclusive form did not.
    const SolveResult fast = solve(p, 1e-6, 60000);
    ::lobtest::report(fast.converged && !fast.diverged, "a fast process still converges",
                      __FILE__, __LINE__,
                      "residual " + std::to_string(fast.residual) + " after "
                      + std::to_string(fast.sweeps) + " sweeps");
  }

  // ---- a malformed process is refused, not solved ----
  {
    std::string why;
    MdpParams p = demo(); p.imb_transition[0][0] += 0.5;
    CHECK(!p.validate(&why));
    p = demo(); p.p_fill[0][0] = 1.4;                  CHECK(!p.validate(&why));
    p = demo(); p.p_advance[2] = 1.4;                  CHECK(!p.validate(&why));
    p = demo(); p.discount = 1.0;                      CHECK(!p.validate(&why));
    p = demo(); p.p_up[1] = 0.7; p.p_down[1] = 0.7;    CHECK(!p.validate(&why));
    p = demo(); p.inventory_penalty = -1.0;            CHECK(!p.validate(&why));
  }

  // ---- the position limit is a constraint on the ACTION ----
  // Clamping the resulting inventory instead lets a fill at the boundary pay
  // its edge and change nothing, and the solver finds that immediately.
  {
    for (int b = 0; b < kSideStates; ++b)
      for (int i = 0; i < kImbBuckets; ++i) {
        const std::uint32_t long_max = encode(State{kMaxInventory, b, b, i});
        const std::uint32_t short_max = encode(State{-kMaxInventory, b, b, i});
        for (std::uint8_t a = 0; a < kNumActions; ++a) {
          const policy::Action ac = decode_action(a);
          if (ac.bid != 0) CHECK(!admissible(long_max, a));
          if (ac.ask != 0) CHECK(!admissible(short_max, a));
        }
      }
  }

  // ---- the solve ----
  SolveResult r;
  {
    const MdpParams p = demo();
    r = solve(p, 1e-8, 40000);
    CHECK(r.converged);
    CHECK_EQ(r.policy.size(), static_cast<std::size_t>(kNumStates));
    CHECK_EQ(r.value.size(), static_cast<std::size_t>(kNumStates));
    for (std::uint8_t a : r.policy) CHECK(a < kNumActions);
    for (double v : r.value) CHECK(std::isfinite(v));

    // The policy must respect the limit it was solved under.
    for (int b = 0; b < kSideStates; ++b)
      for (int i = 0; i < kImbBuckets; ++i) {
        CHECK_EQ(decode_action(r.policy[encode(State{kMaxInventory, b, b, i})]).bid, 0);
        CHECK_EQ(decode_action(r.policy[encode(State{-kMaxInventory, b, b, i})]).ask, 0);
      }

    // Holding inventory is worse than being flat, at equal book state. If this
    // fails the penalty is not reaching the value function.
    for (int b = 0; b < kSideStates; ++b) {
      const double flat = r.value[encode(State{0, b, b, 2})];
      CHECK(flat > r.value[encode(State{kMaxInventory, b, b, 2})]);
      CHECK(flat > r.value[encode(State{-kMaxInventory, b, b, 2})]);
    }

    // Deterministic: the hash written into the artefact claims exactly this.
    const SolveResult again = solve(p, 1e-8, 40000);
    CHECK(again.policy == r.policy);
    CHECK_EQ(again.sweeps, r.sweeps);
    CHECK_EQ(p.hash(), demo().hash());
    MdpParams other = demo(); other.inventory_penalty += 1e-9;
    CHECK(other.hash() != p.hash());
  }

  // ---- the artefact ----
  {
    const std::string path = "test_policy_table.bin";
    std::string why;
    CHECK(PolicyTable::save(path, r.policy, r.value, 0xABCDEF0123456789ULL, 0.995,
                            r.residual, static_cast<std::uint64_t>(r.sweeps), kTestScale, 0.1, &why));
    PolicyTable t;
    CHECK(t.load(path, &why));
    CHECK(t.loaded());
    CHECK_EQ(t.header().param_hash, 0xABCDEF0123456789ULL);
    CHECK_EQ(t.header().num_states, kNumStates);
    CHECK_EQ(t.header().max_inventory, static_cast<std::uint32_t>(kMaxInventory));
    // The scale the buckets were solved against travels with the table. An
    // executor that divides by a different one reads a different row for every
    // state, and nothing about that looks like a failure.
    CHECK_EQ(t.header().queue_scale, kTestScale);
    CHECK_NEAR(t.header().dt_s, 0.1, 1e-12);
    for (std::uint32_t s = 0; s < kNumStates; ++s) {
      CHECK_EQ(t.action_for(s), r.policy[s]);
      CHECK_NEAR(t.value_of(s), r.value[s], 1e-12);
    }
    // A lookup it cannot answer declines rather than reading past the end.
    CHECK_EQ(t.action_for(kNumStates), 0);
    CHECK_EQ(t.action_for(0xFFFFFFFFU), 0);
    CHECK_NEAR(t.value_of(kNumStates), 0.0, 0.0);

    // Corrupt the magic, the schema, and the discretisation in turn.
    for (const auto& [offset, byte] : std::vector<std::pair<long, unsigned char>>{
             {0, 'X'}, {8, 99}, {20, 1}}) {
      std::FILE* f = std::fopen(path.c_str(), "r+b");
      CHECK(f != nullptr);
      if (f) {
        std::fseek(f, offset, SEEK_SET);
        std::fwrite(&byte, 1, 1, f);
        std::fclose(f);
      }
      PolicyTable bad;
      ::lobtest::report(!bad.load(path, &why), "corrupt header is rejected", __FILE__, __LINE__, why);
      // Put it back for the next case.
      CHECK(PolicyTable::save(path, r.policy, r.value, 0xABCDEF0123456789ULL, 0.995,
                              r.residual, static_cast<std::uint64_t>(r.sweeps), kTestScale, 0.1, &why));
    }
    CHECK(!PolicyTable{}.load("no_such_policy_table.bin", &why));
    std::remove(path.c_str());
  }

  // ---- the hot path is a lookup, and is measured as one ----
  {
    const std::string path = "test_policy_latency.bin";
    std::string why;
    CHECK(PolicyTable::save(path, r.policy, r.value, 1, 0.995, 0.0, 1, kTestScale, 0.1, &why));
    PolicyTable t;
    CHECK(t.load(path, &why));

    tsc::init();
    std::mt19937 rng{12345};
    std::uniform_int_distribution<std::uint32_t> pick{0, kNumStates - 1};
    std::vector<std::uint32_t> probes(1 << 16);
    for (auto& v : probes) v = pick(rng);

    std::uint64_t sink = 0;
    const std::uint64_t t0 = tsc::now_serialized();
    for (std::uint32_t s : probes) sink += t.action_for(s);
    const std::uint64_t t1 = tsc::now_serialized();
    const double ns = static_cast<double>(tsc::to_nanos(t1 - t0)) / static_cast<double>(probes.size());
    ::lobtest::report(sink > 0, "lookups were not optimised away", __FILE__, __LINE__, "");
    // Random states across the whole table, so this is a cache-miss-inclusive
    // number rather than a hot-loop one. The claim is nanoseconds, not that it
    // is free.
    ::lobtest::report(ns < 100.0, "policy lookup is nanosecond scale", __FILE__, __LINE__,
                      std::to_string(ns) + " ns/lookup over " + std::to_string(kNumStates) + " states");
    std::printf("  policy lookup: %.1f ns each, random access over %u states\n", ns, kNumStates);
    std::remove(path.c_str());
  }

  // ---- the table as a strategy ----
  {
    OrderBook book{9'000, 2048, 8192};
    FeatureEngine fe;
    // A one-tick book, which is the regime this policy exists for.
    CHECK(book.add(1, Side::Bid, 10'000, 500) == BookError::Ok);
    CHECK(book.add(2, Side::Ask, 10'001, 500) == BookError::Ok);
    fe.update(book, 1000);
    const AgentView v{book, fe.get(), 1000, 0};

    // No table is not a licence to invent a quote.
    {
      TabulatedPolicy t;
      const Quote q = t.quote(v);
      CHECK(!q.bid_on); CHECK(!q.ask_on);
    }

    const std::string path = "test_tabulated.bin";
    std::string why;

    // A table that says "quote both at the touch" everywhere.
    {
      std::vector<std::uint8_t> pol(kNumStates, encode_action(policy::Action{1, 1}));
      std::vector<double> val(kNumStates, 0.0);
      CHECK(PolicyTable::save(path, pol, val, 7, 0.99, 0.0, 1, kTestScale, 0.1, &why));
      PolicyTable t; CHECK(t.load(path, &why));
      TabulatedPolicy s2; s2.table = &t; s2.p.size = 10;
      s2.p.max_inventory = kMaxInventory * s2.p.size;
      const Quote q = s2.quote(v);
      CHECK(q.bid_on); CHECK(q.ask_on);
      CHECK_EQ(q.bid, 10'000);      // at the touch, not behind it
      CHECK_EQ(q.ask, 10'001);

      // The position limit is enforced here as well as in the solve. A table is
      // not a place to discover that a constraint was dropped.
      const AgentView long_max{book, fe.get(), 1000, kMaxInventory * s2.p.size};
      const Quote ql = s2.quote(long_max);
      CHECK(!ql.bid_on);
      const AgentView short_max{book, fe.get(), 1000, -kMaxInventory * s2.p.size};
      const Quote qs = s2.quote(short_max);
      CHECK(!qs.ask_on);
    }

    // "One tick behind" means behind, on both sides.
    {
      std::vector<std::uint8_t> pol(kNumStates, encode_action(policy::Action{2, 2}));
      std::vector<double> val(kNumStates, 0.0);
      CHECK(PolicyTable::save(path, pol, val, 7, 0.99, 0.0, 1, kTestScale, 0.1, &why));
      PolicyTable t; CHECK(t.load(path, &why));
      TabulatedPolicy s2; s2.table = &t; s2.p.size = 10;
      const Quote q = s2.quote(v);
      CHECK_EQ(q.bid, 9'999);
      CHECK_EQ(q.ask, 10'002);
      CHECK(q.bid < q.ask);
    }

    // Queue tracking only ever shrinks: a level growing behind us is not
    // progress up the queue, and treating it as such would flatter every fill
    // probability the state feeds on.
    {
      std::vector<std::uint8_t> pol(kNumStates, encode_action(policy::Action{1, 1}));
      std::vector<double> val(kNumStates, 0.0);
      CHECK(PolicyTable::save(path, pol, val, 7, 0.99, 0.0, 1, kTestScale, 0.1, &why));
      PolicyTable t; CHECK(t.load(path, &why));
      TabulatedPolicy s2; s2.table = &t; s2.p.size = 10;

      s2.set_resting(v, true, 10'000, false, 0);
      const Qty joined = s2.bid_ahead;
      CHECK_EQ(joined, 500);

      CHECK(book.add(3, Side::Bid, 10'000, 400) == BookError::Ok);   // arrives BEHIND us
      fe.update(book, 1100);
      const AgentView v2{book, fe.get(), 1100, 0};
      s2.set_resting(v2, true, 10'000, false, 0);
      CHECK_EQ(s2.bid_ahead, joined);          // not 900

      CHECK(book.remove(1) == BookError::Ok);  // someone ahead of us cancels
      fe.update(book, 1200);
      const AgentView v3{book, fe.get(), 1200, 0};
      s2.set_resting(v3, true, 10'000, false, 0);
      CHECK_EQ(s2.bid_ahead, 400);             // and that IS progress

      // Moving the quote surrenders the position and rejoins at the back.
      s2.set_resting(v3, true, 9'999, false, 0);
      CHECK_EQ(s2.bid_ahead, book.qty_at(Side::Bid, 9'999));
    }
    std::remove(path.c_str());
  }

  // ---- JoinTouch, the baseline the policy has to beat ----
  {
    OrderBook book{9'000, 2048, 8192};
    FeatureEngine fe;
    CHECK(book.add(1, Side::Bid, 10'000, 500) == BookError::Ok);
    CHECK(book.add(2, Side::Ask, 10'001, 500) == BookError::Ok);
    fe.update(book, 1000);
    QuoteParams qp; qp.size = 10; qp.max_inventory = 50;
    JoinTouch jt{qp};

    const Quote q = jt.quote(AgentView{book, fe.get(), 1000, 0});
    CHECK(q.bid_on); CHECK(q.ask_on);
    CHECK_EQ(q.bid, 10'000);
    CHECK_EQ(q.ask, 10'001);
    CHECK(!jt.quote(AgentView{book, fe.get(), 1000, 50}).bid_on);
    CHECK(!jt.quote(AgentView{book, fe.get(), 1000, -50}).ask_on);
  }

  return lobtest::summary("policy");
}
