// The MDP's state and action discretisation.
//
// Defined here ONCE and included by both the offline solver and whatever reads
// the table at run time. That is the whole point of the file: a policy solved
// over one discretisation and looked up through another is not a degraded
// policy, it is a random one, and nothing about the failure looks like a bug.
// The schema version in the table header exists to catch it if this file ever
// changes underneath a table that was already solved.
//
// WHAT IS IN THE STATE, AND WHY
//
//   inventory     what the position penalty acts on, and the reason a market
//                 maker skews quotes at all.
//   bid, ask      per side: not quoting, or (which of three price levels) x
//                 (how much volume is ahead of us, in five buckets). Queue
//                 position is in the state because measurement put it there —
//                 the realised fill hazard runs 1,019/s with nothing ahead and
//                 16/s with 50k ahead, a factor of sixty — while
//                 tools/calibrate.py could not identify an Avellaneda-Stoikov k
//                 on two of three instruments, because these books sit at a
//                 one-tick spread and delta has nowhere to vary. Distance is the
//                 small-tick state variable; this is the large-tick one.
//   imbalance     touch imbalance, five buckets. It earns its place: on ethusd
//                 P(next mid move is up) runs 0.3% at the ask-heavy end to 3.0%
//                 at the bid-heavy end.
//
// The action set is deliberately tiny: per side, pull or quote at one of the
// three modelled levels, so four choices a side and sixteen in total.
#pragma once

#include <cstdint>

namespace lob::policy {

inline constexpr int kMaxInventory   = 5;                        // lots, each way
inline constexpr int kInventoryStates = 2 * kMaxInventory + 1;   // 11
// Queue position is bucketed by the ABSOLUTE volume ahead, as a fraction of a
// reference depth — not by quartile among the market's own resting orders,
// which is what this did first and which was wrong by a factor of twenty.
//
// Fill hazard depends on how much size must trade before the queue reaches you.
// That is an absolute quantity. Quartiles are a ranking, and a ranking taken
// over the WRONG POPULATION at that: a market maker re-quotes the moment a
// level clears, so its orders sit at small absolute queues far more often than
// the book's own orders do. Measured against a touch-joining strategy's real
// placements, the hazard at the front ran 1,019/s while the quartile-calibrated
// model said 52/s, and the policy duly concluded that quoting behind the touch
// was better than being in front of it.
inline constexpr int kQueueBuckets   = 5;   // 0 = alone at the level
// Three levels, not two. With two, a quote pushed further out simply vanished
// from the state and the model treated that as free — so it never priced the
// cost of being left behind, only of never having quoted.
inline constexpr int kQuoteLevels    = 3;   // 0 = at touch, 1..2 = ticks behind
inline constexpr int kSideStates     = 1 + kQuoteLevels * kQueueBuckets;   // 16: none, plus 3x5
inline constexpr int kImbBuckets     = 5;

inline constexpr std::uint32_t kNumStates =
    static_cast<std::uint32_t>(kInventoryStates) * kSideStates * kSideStates * kImbBuckets;

inline constexpr int kSideActions = 1 + kQuoteLevels;  // 0 none, then one per level
inline constexpr int kNumActions  = kSideActions * kSideActions;   // 16

// Imbalance bucket edges, matching tools/mdp_params.py. If these two ever
// disagree the policy is solved against one book and applied to another.
inline constexpr double kImbEdges[kImbBuckets - 1] = {-0.6, -0.2, 0.2, 0.6};

struct State {
  int inventory = 0;    // [-kMaxInventory, kMaxInventory]
  int bid       = 0;    // [0, kSideStates)
  int ask       = 0;
  int imb       = 0;    // [0, kImbBuckets)
};

struct Action {
  int bid = 0;          // [0, kSideActions)
  int ask = 0;
};

// ---- side-state helpers ---------------------------------------------------
// A side state is an index into a table, so anything negative is not a quote —
// and saying `!= 0` meant the arithmetic below could be handed one and produce
// a negative array subscript. In range, or flat.
[[nodiscard]] constexpr bool quoting(int side_state) noexcept {
  return side_state > 0 && side_state < kSideStates;
}
[[nodiscard]] constexpr int  level_of(int side_state) noexcept {
  return side_state == 0 ? -1 : (side_state - 1) / kQueueBuckets;
}
[[nodiscard]] constexpr int  queue_of(int side_state) noexcept {
  return side_state == 0 ? -1 : (side_state - 1) % kQueueBuckets;
}
[[nodiscard]] constexpr int  make_side(int level, int queue) noexcept {
  return 1 + level * kQueueBuckets + queue;
}
inline constexpr int kNoQuote = 0;
// A new order joins the BACK of its level's queue. This is the cost of
// re-quoting, and in a book where 99% of removals are cancels it is the single
// most consequential line in the model: moving a quote one tick surrenders
// every second already spent waiting.
inline constexpr int kBackOfQueue = kQueueBuckets - 1;

// ---- encoding -------------------------------------------------------------
[[nodiscard]] constexpr std::uint32_t encode(const State& s) noexcept {
  const std::uint32_t q = static_cast<std::uint32_t>(s.inventory + kMaxInventory);
  return ((q * static_cast<std::uint32_t>(kSideStates)
           + static_cast<std::uint32_t>(s.bid)) * static_cast<std::uint32_t>(kSideStates)
          + static_cast<std::uint32_t>(s.ask)) * static_cast<std::uint32_t>(kImbBuckets)
         + static_cast<std::uint32_t>(s.imb);
}

[[nodiscard]] constexpr State decode(std::uint32_t code) noexcept {
  State s;
  s.imb = static_cast<int>(code % static_cast<std::uint32_t>(kImbBuckets));
  code /= static_cast<std::uint32_t>(kImbBuckets);
  s.ask = static_cast<int>(code % static_cast<std::uint32_t>(kSideStates));
  code /= static_cast<std::uint32_t>(kSideStates);
  s.bid = static_cast<int>(code % static_cast<std::uint32_t>(kSideStates));
  code /= static_cast<std::uint32_t>(kSideStates);
  s.inventory = static_cast<int>(code) - kMaxInventory;
  return s;
}

[[nodiscard]] constexpr std::uint8_t encode_action(const Action& a) noexcept {
  return static_cast<std::uint8_t>(a.bid * kSideActions + a.ask);
}
[[nodiscard]] constexpr Action decode_action(std::uint8_t a) noexcept {
  return Action{a / kSideActions, a % kSideActions};
}

// ---- run-time bucketing ---------------------------------------------------
// The execution path calls these on live book state to build the lookup key.
[[nodiscard]] constexpr int imb_bucket(double imbalance) noexcept {
  int b = 0;
  for (int i = 0; i < kImbBuckets - 1; ++i)
    if (imbalance > kImbEdges[i]) b = i + 1;
  return b;
}

// Volume ahead of us, as a fraction of a reference depth, in five buckets.
//
// `scale` is the mean touch depth the table was calibrated against, and it
// travels in the table header so the solver and the execution path cannot
// disagree about what "half a queue ahead" means. Being alone gets its own
// bucket because it is a different state, not a small number: nothing has to
// trade before you do.
inline constexpr double kQueueEdges[kQueueBuckets - 2] = {0.02, 0.10, 0.35};

[[nodiscard]] constexpr int queue_bucket(long long ahead, long long scale) noexcept {
  if (ahead <= 0) return 0;                       // alone at the level
  if (scale <= 0) return kQueueBuckets - 1;       // no scale: assume the worst
  const double f = static_cast<double>(ahead) / static_cast<double>(scale);
  int b = 1;
  for (int i = 0; i < kQueueBuckets - 2; ++i)
    if (f > kQueueEdges[i]) b = i + 2;
  return b;
}

// Where a bucket begins, and where a typical order inside it sits, both in
// fractions of `scale`. Derived from kQueueEdges rather than written out again,
// because two lists of the same numbers is one list and a bug waiting.
//
// The buckets are deliberately UNEQUAL — the front of a queue is where the
// hazard changes fastest — so a single "probability of advancing one bucket per
// epoch" cannot be right for all of them. These let the solver turn one
// measured drain rate into a per-bucket transition; see MdpParams::p_advance.
[[nodiscard]] constexpr double queue_floor(int b) noexcept {
  return b <= 1 ? 0.0 : kQueueEdges[b - 2];
}
[[nodiscard]] constexpr double queue_typical(int b) noexcept {
  if (b <= 0) return 0.0;
  // The deepest bucket is unbounded above. A full queue is the honest
  // representative: it is exactly what joining the back of a level puts in
  // front of you, which is the state kBackOfQueue names.
  if (b >= kQueueBuckets - 1) return 1.0;
  return 0.5 * (queue_floor(b) + kQueueEdges[b - 1]);
}

}  // namespace lob::policy
