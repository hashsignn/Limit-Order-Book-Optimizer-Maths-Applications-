#include "lob/policy/mdp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lob::policy {
namespace {

// A max-norm residual no bounded-reward discounted MDP can reach. The rewards
// here are ticks per epoch and the discount is below one, so the value
// function is bounded by reward/(1-discount) — a few thousand at the very
// most. Anything past this is divergence, not slow progress.
constexpr double kDivergent = 1e12;

// FNV-1a. Not cryptographic and does not need to be: its job is to make a
// table that was solved from different parameters fail to match, not to resist
// an adversary.
std::uint64_t fnv(std::uint64_t h, const void* data, std::size_t n) noexcept {
  const auto* p = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 0x100000001B3ULL;
  }
  return h;
}

// The action's effect on one side, before any dynamics.
//
// Holding is implicit and is the point: asking for the level we are already
// quoting keeps the queue position we have spent time earning, while asking for
// the other level surrenders it and rejoins at the back. In a book where 99% of
// removals are cancels, that is the central trade-off of the whole problem.
[[nodiscard]] int apply_action(int side_state, int act) noexcept {
  if (act == 0) return kNoQuote;
  const int want_level = act - 1;                    // 1 -> touch, 2..3 -> ticks behind
  if (quoting(side_state) && level_of(side_state) == want_level) return side_state;
  return make_side(want_level, kBackOfQueue);
}

[[nodiscard]] int advance(int side_state) noexcept {
  if (!quoting(side_state)) return side_state;
  const int q = queue_of(side_state);
  return q == 0 ? side_state : make_side(level_of(side_state), q - 1);
}

// A mid move slides the whole book one level past our quotes.
//
// On the side the price moves INTO, only the quote AT THE TOUCH is filled. A
// quote one tick behind is not reachable by a move of this size — the market
// merely arrives at it, and it becomes the touch quote with its queue position
// intact. Filling it too was the first version of this model, and it made
// quoting behind look strictly better than quoting at the touch: three times
// the edge for the same certainty of being filled by any move. The policy duly
// quoted behind in a third of all states.
//
// Falling out of the modelled window is not the same as being cancelled, and
// with only two levels it happened after a single adverse move — so the model
// priced being left behind as free, and never learned that a stale quote is a
// liability. Three levels is still a window, but a move now has to go twice as
// far before the state stops describing the order.
[[nodiscard]] int step_away(int side_state) noexcept {
  if (!quoting(side_state)) return kNoQuote;
  const int lvl = level_of(side_state);
  if (lvl + 1 >= kQuoteLevels) return kNoQuote;      // fell out of the modelled window
  return make_side(lvl + 1, queue_of(side_state));
}

// The side the price moved into: a level-1 quote becomes a level-0 one, keeping
// its place in the queue, because nothing displaced it.
[[nodiscard]] int step_toward(int side_state) noexcept {
  if (!quoting(side_state)) return kNoQuote;
  const int lvl = level_of(side_state);
  return lvl == 0 ? kNoQuote : make_side(lvl - 1, queue_of(side_state));
}

// Indexing the parameter arrays by a side state.
//
// level_of() and queue_of() are -1 on a flat side, and they are plain
// arithmetic on the state code, so nothing in the type system stops a caller
// using one as a subscript — which is exactly what happened: edge_ticks[-1] was
// read in every state where one side was not quoting, which is most of them.
// These three are the only way the process touches those arrays, and each
// answers "flat" rather than reading whatever is next to the table.
[[nodiscard]] double fill_prob(const MdpParams& p, int ss) noexcept {
  return quoting(ss) ? p.p_fill[level_of(ss)][queue_of(ss)] : 0.0;
}
[[nodiscard]] double edge_of(const MdpParams& p, int ss) noexcept {
  return quoting(ss) ? p.edge_ticks[level_of(ss)] : 0.0;
}
// Bucket 0 is alone at the level: there is nothing in front to drain.
[[nodiscard]] double advance_prob(const MdpParams& p, int ss) noexcept {
  return (quoting(ss) && queue_of(ss) > 0) ? p.p_advance[queue_of(ss)] : 0.0;
}

// Only a quote at the touch is filled by a one-level move.
[[nodiscard]] bool taken_by_move(int side_state) noexcept {
  return quoting(side_state) && level_of(side_state) == 0;
}

[[nodiscard]] int clamp_inv(int q) noexcept {
  return std::clamp(q, -kMaxInventory, kMaxInventory);
}

}  // namespace

bool admissible(std::uint32_t state, std::uint8_t action) noexcept {
  const State s = decode(state);
  const Action a = decode_action(action);
  // A hard position limit, and the reason it must be a limit on the ACTION
  // rather than a clamp on the outcome: clamping means a fill at the boundary
  // changes no inventory while still paying the edge, so the model discovers
  // that quoting at the limit is free money. The first solve did exactly that —
  // it kept a bid live at maximum long inventory. A position limit is also what
  // a real system enforces, so this is the honest shape as well as the correct
  // one.
  if (s.inventory >= kMaxInventory && a.bid != 0) return false;
  if (s.inventory <= -kMaxInventory && a.ask != 0) return false;
  return true;
}

std::uint64_t MdpParams::hash() const noexcept {
  std::uint64_t h = 0xCBF29CE484222325ULL;
  h = fnv(h, &dt_s, sizeof dt_s);
  h = fnv(h, p_up, sizeof p_up);
  h = fnv(h, p_down, sizeof p_down);
  h = fnv(h, &move_ticks, sizeof move_ticks);
  h = fnv(h, imb_transition, sizeof imb_transition);
  h = fnv(h, p_fill, sizeof p_fill);
  h = fnv(h, p_advance, sizeof p_advance);
  h = fnv(h, edge_ticks, sizeof edge_ticks);
  h = fnv(h, &inventory_penalty, sizeof inventory_penalty);
  h = fnv(h, &discount, sizeof discount);
  return h;
}

bool MdpParams::validate(std::string* why) const {
  auto fail = [&](const char* m) { if (why) *why = m; return false; };
  auto prob = [](double x) { return std::isfinite(x) && x >= 0.0 && x <= 1.0; };

  if (!(dt_s > 0.0)) return fail("dt_s must be positive");
  if (!(discount > 0.0 && discount < 1.0)) return fail("discount must be in (0, 1)");
  if (!(move_ticks > 0.0)) return fail("move_ticks must be positive");
  if (!(inventory_penalty >= 0.0)) return fail("inventory_penalty must not be negative");

  for (int i = 0; i < kImbBuckets; ++i) {
    if (!prob(p_up[i]) || !prob(p_down[i])) return fail("p_up/p_down outside [0,1]");
    if (p_up[i] + p_down[i] > 1.0) return fail("p_up + p_down exceeds 1 for some imbalance bucket");
    double row = 0.0;
    for (int j = 0; j < kImbBuckets; ++j) {
      if (!prob(imb_transition[i][j])) return fail("imb_transition entry outside [0,1]");
      row += imb_transition[i][j];
    }
    if (std::fabs(row - 1.0) > 1e-6) return fail("an imb_transition row does not sum to 1");
  }
  for (int l = 0; l < kQuoteLevels; ++l) {
    if (!(std::isfinite(edge_ticks[l]))) return fail("edge_ticks not finite");
    for (int q = 0; q < kQueueBuckets; ++q)
      if (!prob(p_fill[l][q])) return fail("p_fill outside [0,1]");
  }
  for (int q = 0; q < kQueueBuckets; ++q)
    if (!prob(p_advance[q])) return fail("p_advance outside [0,1]");
  return true;
}

void expand(const MdpParams& p, std::uint32_t state, std::uint8_t action,
            std::vector<Transition>& out) {
  const State s = decode(state);
  const Action a = decode_action(action);

  const int b0 = apply_action(s.bid, a.bid);
  const int a0 = apply_action(s.ask, a.ask);

  const double pu = p.p_up[s.imb], pd = p.p_down[s.imb];
  const double pf = 1.0 - pu - pd;

  // Reward for landing in a post-fill inventory with a mid move of `dm` ticks,
  // having captured `edge` ticks of half-spread on the way. The move is applied
  // to the inventory AFTER the fill, which is what makes being filled into a
  // move cost what it costs.
  auto reward = [&p](int inv_after, double edge, double dm) {
    return edge + static_cast<double>(inv_after) * dm
           - p.inventory_penalty * static_cast<double>(inv_after) * inv_after;
  };

  auto emit = [&](double prob, int inv, int nb, int na, double edge, double dm) {
    if (prob <= 0.0) return;
    const int inv_c = clamp_inv(inv);
    const double r = reward(inv_c, edge, dm);
    for (int j = 0; j < kImbBuckets; ++j) {
      const double pj = p.imb_transition[s.imb][j];
      if (pj <= 0.0) continue;
      State n; n.inventory = inv_c; n.bid = nb; n.ask = na; n.imb = j;
      out.push_back(Transition{encode(n), prob * pj, r});
    }
  };

  // ---- the mid moved up: it went through our ask ----
  if (pu > 0.0) {
    const bool hit = taken_by_move(a0);
    emit(pu, s.inventory - (hit ? 1 : 0), step_away(b0), hit ? kNoQuote : step_toward(a0),
         hit ? p.edge_ticks[0] : 0.0, p.move_ticks);
  }
  // ---- the mid moved down: it went through our bid ----
  if (pd > 0.0) {
    const bool hit = taken_by_move(b0);
    emit(pd, s.inventory + (hit ? 1 : 0), hit ? kNoQuote : step_toward(b0), step_away(a0),
         hit ? p.edge_ticks[0] : 0.0, -p.move_ticks);
  }
  // ---- the mid held: we may be filled where we stand ----
  if (pf > 0.0) {
    const double pb = fill_prob(p, b0);
    const double pa = fill_prob(p, a0);
    const double eb = edge_of(p, b0), ea = edge_of(p, a0);
    const double qb = advance_prob(p, b0), qa = advance_prob(p, a0);

    // THE TWO SIDES FILL INDEPENDENTLY. Both can trade in the same epoch, and
    // on a fast book at the front of the queue they routinely do — the measured
    // per-epoch fill probability at the touch on this process is 0.92.
    //
    // The first version treated the sides as mutually exclusive and called the
    // remainder (1 - pb - pa). Against real numbers that remainder is NEGATIVE,
    // emit() discarded the negative branch as it discards any impossible one,
    // and the surviving probabilities summed to 1.8. A transition function that
    // creates probability is not a contraction: the effective discount exceeds
    // one and value iteration diverges. It did, silently, to a residual of
    // 2e+57 after twenty thousand sweeps, and the only symptom was a solve that
    // "did not converge" — which reads like it needed more sweeps.
    //
    // Four outcomes, and the best of them is the one the old form could not
    // express at all: both sides fill, the full spread is captured, and there is
    // no inventory left over. That is the market maker's whole business, and a
    // model that gives it zero probability cannot value being at the touch.
    emit(pf * pb * pa, s.inventory, kNoQuote, kNoQuote, eb + ea, 0.0);
    // Only the bid: we bought. The ask still rests and its queue may drain.
    emit(pf * pb * (1.0 - pa) * (1.0 - qa), s.inventory + 1, kNoQuote, a0,          eb, 0.0);
    emit(pf * pb * (1.0 - pa) * qa,         s.inventory + 1, kNoQuote, advance(a0), eb, 0.0);
    // Only the ask: we sold.
    emit(pf * (1.0 - pb) * pa * (1.0 - qb), s.inventory - 1, b0,          kNoQuote, ea, 0.0);
    emit(pf * (1.0 - pb) * pa * qb,         s.inventory - 1, advance(b0), kNoQuote, ea, 0.0);
    // Neither traded, so each side's queue may have drained into the next
    // bucket. The two sides advance independently, which is four outcomes.
    const double rest = pf * (1.0 - pb) * (1.0 - pa);
    emit(rest * (1.0 - qb) * (1.0 - qa), s.inventory, b0,          a0,          0.0, 0.0);
    emit(rest * qb         * (1.0 - qa), s.inventory, advance(b0), a0,          0.0, 0.0);
    emit(rest * (1.0 - qb) * qa,         s.inventory, b0,          advance(a0), 0.0, 0.0);
    emit(rest * qb         * qa,         s.inventory, advance(b0), advance(a0), 0.0, 0.0);
  }
}

bool stochastic(const MdpParams& p, std::string* why) {
  std::vector<Transition> tr;
  tr.reserve(64);
  double worst = 0.0;
  std::uint32_t worst_s = 0;
  std::uint8_t worst_a = 0;
  for (std::uint32_t s = 0; s < kNumStates; ++s) {
    for (std::uint8_t a = 0; a < kNumActions; ++a) {
      if (!admissible(s, a)) continue;
      tr.clear();
      expand(p, s, a, tr);
      double sum = 0.0;
      for (const Transition& t : tr) sum += t.prob;
      if (std::fabs(sum - 1.0) > worst) { worst = std::fabs(sum - 1.0); worst_s = s; worst_a = a; }
    }
  }
  if (worst <= 1e-9) return true;
  if (why != nullptr) {
    const State st = decode(worst_s);
    const Action ac = decode_action(worst_a);
    *why = "the successors of one state-action do not sum to 1 (off by "
         + std::to_string(worst) + "): inventory " + std::to_string(st.inventory)
         + ", bid state " + std::to_string(st.bid) + ", ask state " + std::to_string(st.ask)
         + ", imbalance " + std::to_string(st.imb)
         + ", action (bid " + std::to_string(ac.bid) + ", ask " + std::to_string(ac.ask) + ")";
  }
  return false;
}

SolveResult solve(const MdpParams& p, double tol, int max_sweeps) {
  SolveResult r;
  r.policy.assign(kNumStates, 0);
  r.value.assign(kNumStates, 0.0);
  std::vector<double> next(kNumStates, 0.0);
  std::vector<Transition> tr;
  tr.reserve(256);

  // Modified policy iteration (Puterman, Markov Decision Processes, section 6.5).
  //
  // One greedy improvement, then kEval evaluations of the policy it just chose.
  // An evaluation backup expands ONE action where an improvement expands all
  // sixteen, so the evaluations cost a sixteenth each while contracting the
  // error as far as a full sweep would.
  //
  // This is not a micro-optimisation, it is what makes the problem solvable on
  // the grid it has to be solved on. A one-second horizon on a half-millisecond
  // epoch is a per-epoch discount of 0.9995, and plain value iteration needs
  // upwards of forty thousand sweeps to reach a 1e-9 residual there — it hit the
  // twenty-thousand cap at 2e-6 and shipped nothing at all. The fixed point is
  // identical; only the route to it is shorter.
  constexpr int kEval = 32;

  auto backup = [&](std::uint32_t s, std::uint8_t a) {
    tr.clear();
    expand(p, s, a, tr);
    double q = 0.0;
    for (const Transition& t : tr) q += t.prob * (t.reward + p.discount * r.value[t.next]);
    return q;
  };

  while (r.sweeps < max_sweeps) {
    // ---- improvement: greedy in the current value ----
    double residual = 0.0;
    for (std::uint32_t s = 0; s < kNumStates; ++s) {
      double best = -1e300;
      std::uint8_t best_a = 0;
      for (std::uint8_t a = 0; a < kNumActions; ++a) {
        if (!admissible(s, a)) continue;
        const double q = backup(s, a);
        // Ties go to the lower-numbered action, which orders "do not quote"
        // first. A policy that flips between equally-valued actions on floating
        // point noise is not deterministic, and determinism is what the hash in
        // the table header claims.
        if (q > best + 1e-12) { best = q; best_a = a; }
      }
      next[s] = best;
      r.policy[s] = best_a;
      residual = std::max(residual, std::fabs(next[s] - r.value[s]));
    }
    r.value.swap(next);
    ++r.sweeps;
    r.residual = residual;
    if (residual < tol) { r.converged = true; break; }
    // Stop the moment the iteration is clearly not contracting, and say which it
    // was. "Did not converge in N sweeps" reads like it needed more sweeps; a
    // residual past anything a bounded reward and a discount below one can
    // produce means the process is not a probability distribution, and no number
    // of sweeps fixes that.
    if (!std::isfinite(residual) || residual > kDivergent) { r.diverged = true; break; }

    // ---- evaluation: hold that policy and let the values settle ----
    for (int k = 0; k < kEval && r.sweeps < max_sweeps; ++k) {
      for (std::uint32_t s = 0; s < kNumStates; ++s) next[s] = backup(s, r.policy[s]);
      r.value.swap(next);
      ++r.sweeps;
    }
  }
  return r;
}

}  // namespace lob::policy
