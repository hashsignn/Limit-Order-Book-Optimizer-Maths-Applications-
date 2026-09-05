// The market-making MDP, and the value iteration that solves it.
//
// Solved OFFLINE. The execution path never sees this file — it loads the table
// this produces and does a bounds-checked array read. That split is the
// architectural decision the whole project is built around: an optimisation
// problem solved in the hot path is a latency budget nobody can hold.
//
// THE PROCESS
//
// One epoch is 100 ms, matching the grid apps/stats samples on. Within an
// epoch, in this order:
//
//   1. the action is applied — a quote is placed, held, or pulled
//   2. the mid either moves a tick or does not, with probabilities that depend
//      on the current imbalance bucket
//   3. if it moved, it moved THROUGH one of our quotes: that side is filled and
//      the other side is left one tick further from the touch
//   4. if it did not move, each side may be filled where it stands, and
//      otherwise the queue in front of it may drain by a quartile
//   5. imbalance transitions
//
// Adverse selection is not a parameter here. It falls out: a fill caused by the
// mid moving through us is booked before the move is applied to the resulting
// inventory, so selling into a rising market costs exactly what it should. A
// model that needed a hand-set adverse-selection term would be a model whose
// fills and price moves were independent, which is the one thing they are not.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lob/policy/state.hpp"

namespace lob::policy {

struct MdpParams {
  double dt_s = 0.1;

  // Mid dynamics, conditional on the imbalance bucket we are in.
  double p_up[kImbBuckets]   = {};
  double p_down[kImbBuckets] = {};
  // Size of a move when one happens, in ticks. Half a tick is one side of the
  // book moving by one tick, which is what these books actually do.
  double move_ticks = 0.5;

  double imb_transition[kImbBuckets][kImbBuckets] = {};

  // Probability our order is filled where it stands during one epoch, by price
  // level and queue quartile. Measured: front of the touch queue 3-10%, deepest
  // quartile 0.00%.
  double p_fill[kQuoteLevels][kQueueBuckets] = {};

  // Probability the queue ahead drains by a quartile in one epoch. This is the
  // free progress that cancels provide, and it is why the cancel/fill split had
  // to be measured rather than assumed.
  double p_advance = 0.0;

  // Half-spread captured by a fill at each level, in ticks.
  double edge_ticks[kQuoteLevels] = {0.5, 1.5};

  // Penalty on held inventory, per epoch, in ticks per lot squared. The one
  // parameter here that is a preference rather than a measurement.
  double inventory_penalty = 0.01;

  double discount = 0.999;

  [[nodiscard]] std::uint64_t hash() const noexcept;
  // Rejects anything that is not a probability, or a row that does not sum to
  // one. A silently malformed process yields a policy that looks fine.
  [[nodiscard]] bool validate(std::string* why) const;
};

// Whether an action may be taken in a state. Quoting a bid at maximum long
// inventory is not a bad decision to be discovered, it is a forbidden one: the
// position limit is a constraint, and expressing it as a clamp on the resulting
// inventory instead lets a fill at the boundary collect its edge for free.
[[nodiscard]] bool admissible(std::uint32_t state, std::uint8_t action) noexcept;

struct Transition {
  std::uint32_t next   = 0;
  double        prob   = 0.0;
  double        reward = 0.0;
};

// Enumerates the successors of (state, action). Appends to `out`; never clears
// it, so a caller can reuse one buffer across the whole sweep.
void expand(const MdpParams& p, std::uint32_t state, std::uint8_t action,
            std::vector<Transition>& out);

struct SolveResult {
  std::vector<std::uint8_t> policy;   // one action per state
  std::vector<double>       value;    // V(s), kept so a decision can be explained
  int    sweeps    = 0;
  double residual  = 0.0;
  bool   converged = false;
};

// Value iteration to a max-norm residual. Deterministic: same parameters in,
// same table out, which is what makes the hash in the artefact meaningful.
SolveResult solve(const MdpParams& p, double tol = 1e-9, int max_sweeps = 20000);

}  // namespace lob::policy
