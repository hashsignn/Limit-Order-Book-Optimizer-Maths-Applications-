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
//   bid, ask      per side: not quoting, or (which of two price levels) x
//                 (which quartile of the queue). Queue position is in the state
//                 because measurement put it there — P(fill) runs 3-10% at the
//                 front of the touch queue and 0.00% in the deepest quartile,
//                 while tools/calibrate.py could not identify an
//                 Avellaneda-Stoikov k on two of three instruments, because
//                 these books sit at a one-tick spread and delta has nowhere to
//                 vary. Distance is the small-tick state variable; this is the
//                 large-tick one.
//   imbalance     touch imbalance, five buckets. It earns its place: on ethusd
//                 P(next mid move is up) runs 0.3% at the ask-heavy end to 3.0%
//                 at the bid-heavy end.
//
// The action set is deliberately tiny. A one-tick spread leaves nowhere to
// quote except the touch or one tick behind it, so per side there are three
// choices and nine in total.
#pragma once

#include <cstdint>

namespace lob::policy {

inline constexpr int kMaxInventory   = 5;                        // lots, each way
inline constexpr int kInventoryStates = 2 * kMaxInventory + 1;   // 11
inline constexpr int kQueueBuckets   = 4;                        // 0 = front of queue
inline constexpr int kQuoteLevels    = 2;                        // 0 = at touch, 1 = one behind
inline constexpr int kSideStates     = 1 + kQuoteLevels * kQueueBuckets;   // 9: none, plus 2x4
inline constexpr int kImbBuckets     = 5;

inline constexpr std::uint32_t kNumStates =
    static_cast<std::uint32_t>(kInventoryStates) * kSideStates * kSideStates * kImbBuckets;

inline constexpr int kSideActions = 3;                 // 0 none, 1 at touch, 2 one behind
inline constexpr int kNumActions  = kSideActions * kSideActions;   // 9

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
[[nodiscard]] constexpr bool quoting(int side_state) noexcept { return side_state != 0; }
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

// Which quartile of a level's queue our order sits in. 0 is the front, and an
// empty level is the front rather than a special case: being alone at a price
// IS being first in line.
[[nodiscard]] constexpr int queue_bucket(long long ahead, long long level_size) noexcept {
  if (level_size <= 0 || ahead <= 0) return 0;
  const long long b = (ahead * kQueueBuckets) / level_size;
  return b >= kQueueBuckets ? kQueueBuckets - 1 : static_cast<int>(b);
}

}  // namespace lob::policy
