// Phase 5 tests: the discretisation, the process, the solve, and the artefact.
//
// The load-bearing one is that every action's successor probabilities sum to
// one. A transition function that leaks probability does not crash and does not
// look wrong — it quietly discounts the future by however much it lost, and the
// policy that comes out is optimal for a process nobody wrote down.
#include "lob/policy/mdp.hpp"
#include "lob/policy/state.hpp"
#include "lob/policy/table.hpp"
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
  p.p_advance = 0.4;
  for (int i = 0; i < kImbBuckets; ++i) {
    p.p_up[i]   = 0.01 + 0.008 * i;      // bid-heavy leans up, as measured
    p.p_down[i] = 0.05 - 0.008 * i;
    for (int j = 0; j < kImbBuckets; ++j)
      p.imb_transition[i][j] = (i == j) ? 0.6 : 0.1;
  }
  for (int q = 0; q < kQueueBuckets; ++q) {
    p.p_fill[0][q] = 0.004 / static_cast<double>(q + 1);
    p.p_fill[1][q] = p.p_fill[0][q] * 0.4;
  }
  p.edge_ticks[0] = 0.5;
  p.edge_ticks[1] = 1.5;
  return p;
}

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
    // Alone at a price is the FRONT of the queue, not a special case.
    CHECK_EQ(queue_bucket(0, 1000), 0);
    CHECK_EQ(queue_bucket(0, 0), 0);
    CHECK_EQ(queue_bucket(999, 1000), kQueueBuckets - 1);
    CHECK_EQ(queue_bucket(5000, 1000), kQueueBuckets - 1);   // never past the end
    for (long long a = 0; a <= 1000; a += 37) {
      const int b = queue_bucket(a, 1000);
      CHECK(b >= 0 && b < kQueueBuckets);
    }
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
  }

  // ---- a malformed process is refused, not solved ----
  {
    std::string why;
    MdpParams p = demo(); p.imb_transition[0][0] += 0.5;
    CHECK(!p.validate(&why));
    p = demo(); p.p_fill[0][0] = 1.4;                  CHECK(!p.validate(&why));
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
          const Action ac = decode_action(a);
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
                            r.residual, static_cast<std::uint64_t>(r.sweeps), &why));
    PolicyTable t;
    CHECK(t.load(path, &why));
    CHECK(t.loaded());
    CHECK_EQ(t.header().param_hash, 0xABCDEF0123456789ULL);
    CHECK_EQ(t.header().num_states, kNumStates);
    CHECK_EQ(t.header().max_inventory, static_cast<std::uint32_t>(kMaxInventory));
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
                              r.residual, static_cast<std::uint64_t>(r.sweeps), &why));
    }
    CHECK(!PolicyTable{}.load("no_such_policy_table.bin", &why));
    std::remove(path.c_str());
  }

  // ---- the hot path is a lookup, and is measured as one ----
  {
    const std::string path = "test_policy_latency.bin";
    std::string why;
    CHECK(PolicyTable::save(path, r.policy, r.value, 1, 0.995, 0.0, 1, &why));
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

  return lobtest::summary("policy");
}
