#include "lob/policy/mdp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lob::policy {
namespace {

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
  const int want_level = act - 1;                    // 1 -> touch, 2 -> behind
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
  h = fnv(h, &p_advance, sizeof p_advance);
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
  if (!prob(p_advance)) return fail("p_advance outside [0,1]");
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
    const double pb = quoting(b0) ? p.p_fill[level_of(b0)][queue_of(b0)] : 0.0;
    const double pa = quoting(a0) ? p.p_fill[level_of(a0)][queue_of(a0)] : 0.0;

    emit(pf * pb, s.inventory + 1, kNoQuote, a0, p.edge_ticks[level_of(b0)], 0.0);
    emit(pf * pa, s.inventory - 1, b0, kNoQuote, p.edge_ticks[level_of(a0)], 0.0);

    // Neither side traded, so each side's queue may have drained a quartile.
    // Both sides are advanced independently, which is four outcomes.
    const double rest = pf * (1.0 - pb - pa);
    if (rest > 0.0) {
      const bool can_b = quoting(b0) && queue_of(b0) > 0;
      const bool can_a = quoting(a0) && queue_of(a0) > 0;
      const double qb = can_b ? p.p_advance : 0.0;
      const double qa = can_a ? p.p_advance : 0.0;
      emit(rest * (1 - qb) * (1 - qa), s.inventory, b0,          a0,          0.0, 0.0);
      emit(rest * qb       * (1 - qa), s.inventory, advance(b0), a0,          0.0, 0.0);
      emit(rest * (1 - qb) * qa,       s.inventory, b0,          advance(a0), 0.0, 0.0);
      emit(rest * qb       * qa,       s.inventory, advance(b0), advance(a0), 0.0, 0.0);
    }
  }
}

SolveResult solve(const MdpParams& p, double tol, int max_sweeps) {
  SolveResult r;
  r.policy.assign(kNumStates, 0);
  r.value.assign(kNumStates, 0.0);
  std::vector<double> next(kNumStates, 0.0);
  std::vector<Transition> tr;
  tr.reserve(256);

  for (int sweep = 0; sweep < max_sweeps; ++sweep) {
    double residual = 0.0;
    for (std::uint32_t s = 0; s < kNumStates; ++s) {
      double best = -1e300;
      std::uint8_t best_a = 0;
      for (std::uint8_t a = 0; a < kNumActions; ++a) {
        if (!admissible(s, a)) continue;
        tr.clear();
        expand(p, s, a, tr);
        double q = 0.0;
        for (const Transition& t : tr) q += t.prob * (t.reward + p.discount * r.value[t.next]);
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
    r.sweeps = sweep + 1;
    r.residual = residual;
    if (residual < tol) { r.converged = true; break; }
  }
  return r;
}

}  // namespace lob::policy
