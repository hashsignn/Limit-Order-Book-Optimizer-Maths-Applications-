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
//      otherwise the queue in front of it may drain into the next bucket
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

  // Probability that a move which took our quote is STILL against us at the
  // holding horizon, rather than having reverted.
  //
  // The model used to charge the full move on every move-fill, with certainty.
  // That is one of the two candidates docs/KNOWN-ISSUES.md 2 named, and the
  // measurement it asked for now exists: P(the mid has moved against the
  // resting side one second after a print) is 33% on the eight-hour ethusd
  // capture (n=5,032) and 48-63% on the three ten-minute samples. Not 100%.
  //
  // WHY THIS BRANCH AND NOT THE MARK ON EXISTING INVENTORY, which the same
  // move also applies. A move marks a position we already hold in whichever
  // direction it goes, and up and down are both in the expansion, so over the
  // two branches that charge is symmetric and averages out. A move-FILL is
  // one-sided by construction — we are only filled on the side the move goes
  // through — so an over-charge there does not cancel against anything. It
  // biases every decision about whether to quote at the touch, in the same
  // direction, always. That is the asymmetry worth fixing.
  //
  // CONSERVATIVE ON PURPOSE. This charges the move with probability
  // p_move_adverse and nothing otherwise, so it ignores the cases where the
  // mid came back the other way and the fill turned out to be profitable. The
  // true expected markout is therefore smaller than what this charges, and a
  // mean over that tail is not a robust statistic on n=54 prints while a
  // proportion is. Better to under-credit the maker than to fit an outlier.
  //
  // 1.0 reproduces the old behaviour exactly, and is the default so that a
  // params file with no measurement in it changes nothing.
  double p_move_adverse = 1.0;

  double imb_transition[kImbBuckets][kImbBuckets] = {};

  // Probability our order is filled where it stands during one epoch, by price
  // level and queue bucket. Bucketed by the ABSOLUTE volume ahead, because that
  // is what a market order has to consume before reaching us; the first version
  // of this bucketed by quartile among the market's own resting orders and was
  // wrong by a factor of twenty for our own.
  double p_fill[kQuoteLevels][kQueueBuckets] = {};

  // Probability the queue ahead drains into the next bucket down, in one epoch.
  // This is the free progress that cancels provide, and it is why the
  // cancel/fill split had to be measured rather than assumed.
  //
  // PER BUCKET, because the buckets are not equally wide: escaping the deepest
  // one means draining most of a full queue, escaping the shallowest means
  // draining two per cent of one. A single rate made the front of the queue as
  // hard to reach as the back. Index 0 is unused — nothing is ahead of you
  // there — and the solver derives the rest from one measured drain rate and
  // the bucket geometry in state.hpp.
  double p_advance[kQueueBuckets] = {};

  // Half-spread captured by a fill at each level, in ticks.
  double edge_ticks[kQuoteLevels] = {0.5, 1.5, 2.5};

  // Penalty on held inventory, PER EPOCH, in ticks per lot squared. The one
  // parameter here that is a preference rather than a measurement — and the one
  // that must not be set directly, because "per epoch" is not a unit anybody
  // holds a preference in. Halving the epoch halves the number of ticks a lot
  // costs per second while leaving this constant, so the same 0.01 meant 10
  // ticks/lot^2/s at a 1 ms epoch and 20 at 0.5 ms. It duly made the policy
  // twice as afraid of inventory, and it pulled a side in a third of all states.
  // apps/solve takes the preference per SECOND and multiplies by dt_s here.
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
  // The iteration ran away rather than merely running out of sweeps. It can
  // only happen if the process is not a probability distribution, so it is a
  // different diagnosis and gets a different flag.
  bool   diverged  = false;
};

// Checks that every admissible state-action's successors sum to one, before any
// value iteration happens. One sweep's worth of arithmetic, and it turns the
// failure mode above into a sentence naming the state instead of twenty
// thousand sweeps ending in a residual of 2e+57.
[[nodiscard]] bool stochastic(const MdpParams& p, std::string* why);

// Solves to a max-norm residual. Deterministic: same parameters in, same table
// out, which is what makes the hash in the artefact meaningful.
//
// `max_sweeps` counts BACKUPS, not outer iterations, and the number needed is
// set by the discount: reaching 1e-9 at 0.9995 per epoch takes about 35,000 of
// them. The old cap of 20,000 was not a safety limit, it was a silent ceiling
// the solve ran into and stopped at 2e-6 — which apps/solve then correctly
// refused to ship, having produced nothing after ten minutes.
SolveResult solve(const MdpParams& p, double tol = 1e-9, int max_sweeps = 200000);

}  // namespace lob::policy
