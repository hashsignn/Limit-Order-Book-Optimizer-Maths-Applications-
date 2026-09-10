# Known issues

Things measured, reproduced, and not yet fixed. An issue leaves this file when
it is fixed or when it is shown not to be real — not when it stops being
convenient.

---

## 1. ethusd's reconstructed book develops holes near the touch

**Found** 2026-09-09, on the 8-hour ethusd capture (1.62 M orders, 5,032 trades).

**Symptom.** 27.6% of prints landed *in front of* the touch we had
reconstructed: a taker buy printing below our best ask. That cannot happen
against a correct book — it means our best ask was worse than the real one, so
a level was missing.

**What it is not.** Two guesses, both disproved by measurement:

- *The aggressor's side is mislabelled.* No. The synthetic path did have that
  bug and it was fixed; the capture path takes the taker side straight from the
  feed, and btcusd through identical code reports 4.2%.
- *The price window is too narrow.* No. Widening `--band-pct` from 0.02 to 0.10
  left the count **identical at 1922**, and the touch then had 41%/49% headroom.

**What it is.** The distance histogram settles it:

| in front by | prints | share |
|---|---|---|
| 1 tick | 623 | 32% |
| 2–4 | 117 | 6% |
| 5–16 | 279 | 15% |
| **17+** | **903** | **47%** |

A one-tick gap is a message-ordering race between the `live_orders` and
`live_trades` channels — the delete reaches us before the print that caused it,
so the touch has already moved. That is a third of them and is not fixable from
the capture.

The other two thirds are levels genuinely absent from the book. At those moments
the reconstruction showed a spread of 17+ ticks on an instrument that sits at
one tick, so the touch was empty and the nearest level we knew about was far
away.

**Unverified hunch, recorded as a hunch.** ethusd moved 2.0% across the capture
against btcusd's 0.9%, so more of its range was price territory the book had
never seen. `BitstampConfig::seed_guard` also refuses to seed levels within 0.3%
of the snapshot touch, so that band starts empty and must fill from the stream.
Neither is measured. Do not repeat the mistake above and act on this before
testing it.

**What it costs.** More than the one measurement first claimed here. That
claim was made by reading which column `trades.dist_ticks` feeds, and it missed
that a book with holes reports a wider spread than the market has:

| | clean 10-min capture | 8-hour capture |
|---|---|---|
| spread at one tick | 92% | **60%** |
| mid steps kept by the `max_spread` filter | 85% | **61%** |

So the spread distribution is degraded and 39% of mid steps are discarded. What
survives is sound — the median mid move is 1 tick, P(up) 1.6%, and the imbalance
signal is the best measured on any instrument, running 0.7% to 2.7% monotonically
across the five buckets. ethusd still passes both usability checks. But it is
not true that only `level_ratio` was affected.

**No workaround for the level ratio.** Taking it from btcusd was the first plan
and it does not work: btcusd's own touch teleports, so its levels are just as
unreliable, and it reports 30.8% / 29.2% / 29.1% across three levels — flat,
when penetration must fall with depth. ethusd reports 62.1% / 61.4% / 61.0%.
`tools/mdp_params.py` now rejects a ratio that is flat across levels or above
0.5 for exactly this reason. Neither eight-hour capture measures it; it must be
given to `solve` explicitly with `--level-ratio` and defended.

**How to check a fix.** `stats --capture-dir <dir>` prints the share and the
histogram. A fix moves the 5–16 and 17+ rows toward zero; the 1-tick row should
not move, because that one is the channel race and no book change touches it.

---

## 2. The MDP prices a touch quote as a guaranteed loser on real data — FIXED

**Found** 2026-09-09, solving ethusd from the 8-hour capture. The policy quotes
one tick behind the touch in **81.8% of states and at the touch in none** — the
same pathology fixed earlier against the simulator, arrived at from a different
direction.

**The arithmetic, from the numbers the solve printed.** Averaged across
imbalance, P(mid moves) is 2.9% an epoch while P(fill at the touch, alone in the
queue) is 0.141% — **moves are 21x more likely than fills**. A touch quote taken
by a move earns `edge_ticks[0]` = 0.5 and then has `move_ticks` = 1.0 applied
against the resulting inventory, so each one is worth −0.5:

| | ticks/epoch |
|---|---|
| at the touch | 2.9% x (−0.50) + 0.141% x (+0.50) = **−0.0138** |
| one tick behind | 0.0141% x (+1.50) = **+0.0002** |

Nothing about the policy is wrong given those inputs. One of the inputs is.

**Two candidates. Both measurable. Neither yet measured.**

1. `move_ticks` is 1.0 here against 0.5 on the clean ten-minute capture, and it
   comes from the same degraded book as issue 1 — 60% of the time at one tick
   against 92%. If the true figure is 0.5 the move-fill is break-even, not −0.5,
   and the fill income decides it.
2. `expand()` charges the FULL move against every move-fill. The markout
   measured on this very capture says the mid moves against the passive side
   **33% of the time**, not always. Charging a certain loss where the data shows
   a one-in-three chance is a different error, in the same direction, and it
   does not need issue 1 to be true.

Note what is NOT a candidate: `taken_by_move()` firing for a quote at the touch.
That looks like an over-assumption in a book where 99.7% of removals are
cancels, and it is not one — a maker does not cancel its own quote, so for the
touch to move past our price our order must have been taken.

**Where it does and does not show up.** On the simulator the same solver
produces a policy that quotes at the touch in 73% of states, so the Phase 5
acceptance number does not move. That is not a defence of the acceptance number
and it is not a demotion of the ethusd table -- it was called a "demonstration
artefact" here, which was wrong. ethusd is the real instrument and the real
measurement; the simulator is the thing standing in for one. Issues 4 and 5 say
what the simulator was standing in for, and the disagreement between the two
tables is evidence about the simulator at least as much as about ethusd.

**Do not fix by picking one.** Measure `move_ticks` on a book without issue 1's
holes, and separately measure P(mid move is adverse | our quote was taken), and
let the two numbers say which it is.

**Fixed 2026-09-09. Both remaining causes were measured, and one of them was
not on the list above.**

P(the mid has moved against the resting side, one second after a print) is 33%
on the eight-hour ethusd capture (n=5,032) and 48-63% on the three ten-minute
samples. It is not 100%. So candidate 2 stands: `expand()` charges the full move
against every move-fill, where the data says it goes against the maker between a
third and two thirds of the time. That is still unfixed.

The third cause was the inventory penalty, and it was the larger one. At
`--penalty 10` on a 100 ms epoch the model charged 1.0 tick per lot per epoch
against a touch edge of 0.5 — so a fill was a loss before any mid move was
considered, and the arithmetic in this entry, which omits the penalty term
entirely, was incomplete. With the penalty derived from measured volatility
(issue 5) the ethusd table's value at flat inventory goes from negative to
**+0.0013** and it quotes in every state rather than pulling.

That left the preference for standing one tick behind, which is candidate 2's,
and it is fixed too. `expand()` now splits the move-fill branch in two: the move
is still against us at the holding horizon with probability `p_move_adverse`,
taken from the measurement above, and has reverted otherwise. Both outcomes
leave the same state and differ only in the reward, so the branch is a split of
the same probability mass rather than a change to it — which matters, because a
transition function that creates mass is not a contraction and value iteration
has diverged here once already.

Only the move-FILL is rescaled, not the mark on inventory we already hold. A
move marks an existing position in whichever direction it goes and both
directions are in the expansion, so that charge is symmetric and averages out. A
move-fill is one-sided by construction — we are only filled on the side the move
goes through — so over-charging it biases every decision about quoting at the
touch, always in the same direction.

**The result, on the real instrument this entry is about.**

| ethusd policy | before | after |
|---|---|---|
| quotes both sides at the touch | **0%** | **79.7%** |
| quotes one tick behind | 81.8% | 2.1% |
| skews one side at the inventory limit | — | 18.2% |
| value at flat inventory | negative | positive |

The charge applied is 48% of the move on ethusd and 37% on the simulator, both
measured rather than chosen.

**Deliberately conservative.** The split charges the move with probability
`p_move_adverse` and nothing otherwise, so it ignores the fills where the mid
came back the other way and the trade turned out profitable. The true expected
markout is smaller than what this charges. A proportion is a robust statistic on
54 prints and a mean over that tail is not, so the maker is under-credited on
purpose.

**What it did to the acceptance test.** The gap to JoinTouch halved, from
-3,418 to **-1,703** (95% CI [-2,118, -1,321], 1 of 24 seeds positive), with the
policy quoting both sides at the touch in 27.3% of simulator states against
10.7% before, and taking 3,108 passive fills against JoinTouch's 3,362. It still
fails. See issue 5 for why that is most likely the process rather than the
solver.

---

## 3. The generator fabricates fills that no aggressor caused

**Found** 2026-09-09, comparing queue-reactive intensities measured on the
8-hour ethusd capture against the same measurement on `stats --synthetic`.

**Where trades happen.** In the capture essentially every trade is at the touch.
In the generator most are behind it:

| trades at level | ethusd | synthetic |
|---|---|---|
| 0, the touch | 3,699 | 21,283 |
| 1 | none measurable | 41,238 |
| 2 | none measurable | 44,012 |
| 3 | none measurable | 44,547 |

**And the intensity slope has the wrong sign.** Against shares queued at the
touch, trade intensity runs **−0.28 ± 0.11** on ethusd — a thicker queue trades
slightly less — against **+0.90 ± 0.02** in the generator, where a thicker queue
trades much more. Ten standard errors apart, in opposite directions.

**Cause, read off the code rather than inferred.** `FlowGenerator::
make_on_existing` picks a uniformly random resting order from anywhere in the
book and emits `EventType::Execute` on it. `w_execute = 0.09`, so 9% of the
event mix is a fill that no aggressor caused, that consumed nothing from the
front of any queue, and whose victim was chosen without reference to queue
position. Uniform selection over resting orders is also why the generator's
cancel intensity is proportional to the order count — not queue-reactivity,
just sampling.

**Why it matters here specifically.** Queue position is the state variable the
entire Phase 5 MDP is built around. A fill drawn uniformly over resting orders
is independent of it. Every such fill dilutes the very signal the policy is
being asked to exploit, and it does so inside the process the acceptance test is
measured on.

**What this does not say.** It does not say the measured queue-position gradient
in the simulator is fake — the `w_aggress` path does go through the matching
engine and does consume front-first. It says the two paths are mixed, and the
mix is not something anyone chose on purpose.

**Measured, and it was dominant:** 56.2% of the simulator's fills by count and
**87.0% by volume** arrived by the fabricated path. The volume skew is because
a fabricated execute takes the whole resting order half the time, while an
aggressive order is usually one to forty lots.

**Fixed** the same day. `w_execute` now defaults to zero, so every fill goes
through the matching engine and consumes the front of a queue. It remains
available because the generator has a second job -- driving the book hard
enough to prove it correct, where exercising the Execute path is the point --
and the four places that need it now ask for it explicitly. The default is zero
because of which mistake is worse: a book test that loses Execute coverage
still passes and covers less, while a simulator with fabricated fills still
runs and answers a different question.

**A caution about the test that found this.** The comparison was originally set
up to check whether cancel intensity is proportional to the number of resting
orders, on the theory that a real book cancels independently and a
zero-intelligence one does not. That test does not discriminate: uniform
sampling over resting orders produces the same proportionality for a reason that
has nothing to do with the book being reactive. The finding above came from a
different column than the one the test was built to read.

---

## 4. The generator's touch could not be consumed, and the clock hid it

**Found** 2026-09-09. **Corrected 2026-09-09**, same day, in the opposite
direction: what this entry first said was wrong in sign and in magnitude, and
the way it was measured is why.

**What it said.** That the generator's touch moved 18.5 times a second against
ethusd's 0.29, sixty-fold too often.

**What is true.** Per EVENT -- which no choice of synthetic clock can affect --
the generator's touch moved 2.9e-05 times against ethusd's 3.3e-02. It was
**about a thousand times too STABLE**, not sixty times too volatile.

Both figures are correct as measured. They disagree because the per-second rate
is the per-event rate multiplied by the event rate, and the generator's clock
was `ts_ += 1 + rng() % 5000` -- a 2.5 us mean, so 400,000 events a second
against ethusd's 53.9. A book 200x too thick and a clock 7,400x too fast came
out looking 6x too volatile:

| | ethusd | btcusd | xrpusd | generator |
|---|---|---|---|---|
| events/second | 53.9 | 96.0 | 57.4 | **399,774** |
| orders at the touch | 3.8 | 4.5 | 3.1 | **857** |
| touch moves/event | 3.3e-02 | 6.5e-02 | 6.9e-02 | **8.0e-05** |
| touch moves/second | 1.80 | 6.23 | 3.95 | 32.0 |

Three instruments, ten minutes each, one venue. They agree closely: a touch is
three to five orders, a few dozen events arrive a second, and three to seven per
cent of them move the price.

**The cause is thickness, and two other explanations were tested and killed.**

- *The exogenous mid walk is doing the work.* No. Setting `drift_prob` to zero
  leaves 77-89% of the moves in place at every book size.
- *Orders at the touch die faster than orders behind it, and uniform
  cancellation cannot reproduce that.* No, and backwards. Within the top six
  ticks ethusd rests **69.9%** of its orders at the touch while only 47.8% of
  adds land there, so a touch order lives 1.46x the average -- longer, not
  shorter. Relative lifetime across those six levels spans 0.45 to 1.46, which
  is nearly flat.

What is left is the level of the book, and it accounts for essentially all of
it. Sweeping `target_live` with everything else fixed:

| target_live | orders at touch | spread | moves/event | one-sided |
|---|---|---|---|---|
| 20,000 | 3,314 | 1.0 | 3.0e-05 | 0.0% |
| 4,000 | 950 | 1.3 | 6.0e-05 | 0.0% |
| 800 | 253 | 1.7 | 1.1e-04 | 0.0% |
| 232 | 82 | 1.9 | 1.3e-04 | 0.0% |
| 64 | 24 | 2.0 | 1.3e-04 | 0.0% |
| 32 | 12 | 2.0 | 9.4e-04 | 0.0% |
| 16 | 6.3 | 2.1 | 1.9e-02 | 0.0% |
| 8 | 3.6 | 2.9 | 1.0e-01 | 1.8% |
| **ethusd** | **3.8** | **2.3** | **3.3e-02** | -- |

Note the shape of that column: flat from 232 down to 64 while the touch thins
threefold, then a factor of twenty in one step. Nothing about it is a smooth
response to thickness, and a fit sitting on the steep part is a fit that will
move under any other change to the generator.

At a touch of the real thickness the generator's dynamics are within a factor of
two. Above 64 they are three orders of magnitude out. The response is steep and
non-linear, which is worth saying plainly: 12 is a fitted number on a knife
edge, not a law.

**Why it matters.** A touch that takes 35,000 events to clear does not move
because anyone traded through it. It moves when the exogenous walk moves it. So
a fill carries no information about where the price is going, and adverse
selection -- the entire risk a market maker is paid to bear, and the thing the
Phase 5 state space exists to price -- becomes noise the policy can neither
anticipate nor be compensated for.

**Fixed, in part.** `FlowConfig::ethusd()` is the process fitted to the
captures: `target_live` 12 and `mean_gap_ns` 18.55 ms, chosen together as the
only pair within 30% of ethusd on touch thickness, spread, moves per event and
moves per second at once, and stable to within 3% across five seeds. The
inline clock is now `FlowConfig::mean_gap_ns`, so it is a parameter that can be
wrong rather than a literal that cannot.

`stats --synthetic --calibrated` runs it. Nothing else does yet, and issue 5 is
why.

**How this was caught, since the same mistake is easy to repeat.** By measuring
a rate per event as well as per second. Every quantity in this file that is
quoted per second on synthetic flow is a quantity multiplied by a clock nobody
had calibrated.

---

## 5. Every time constant downstream was fitted to the wrong clock — FIXED

**Found** 2026-09-09, on trying to point the acceptance test at the calibrated
process from issue 4. **Fixed** the same day.

**Symptom.** `evaluate` on the calibrated process reported an informed-share
sweep that was flat from 0% to 70%:

| informed share | 0% | 20% | 40% | 70% |
|---|---|---|---|---|
| JoinTouch session P&L | 15,029 | 15,076 | 14,906 | 14,773 |

By the criterion `evaluate` prints under that table, flat means the generator
has no compensation structure and nothing solved against it can be interpreted.
The MDP table, solved on the same process, quoted both sides in **no state at
all** and earned exactly zero.

*(An earlier version of this entry also called JoinTouch's 8.35 "per fill"
impossible against a two-tick spread. It is not. The column is ticks x shares
and a quote is ten shares, so it is 0.835 ticks a share — below the half-spread
and entirely ordinary. The claim was wrong and the units were on the table's own
header the whole time.)*

**Cause.** `evaluate` diagnosed the first part without being asked:

```
epoch  3722.861 ms message budget (200 events), 3760.465 ms mean quote life,
       table solved for 100.000 ms
       Budget and table differ by 37.2x.
```

`DriverConfig::quote_every` was 200 EVENTS. At 2.5 us an event that is 0.5 ms;
at the calibrated 18.55 ms it is 3.7 SECONDS. One constant, two completely
different traders, and every cadence downstream had been chosen against the
first reading.

**Three constants were wrong, in the same way, for the same reason.**

*The message budget.* Now `DriverConfig::quote_every_ns`, a time, defaulting to
the 100 ms MDP epoch — the one cadence it must agree with, since every
probability in the table is per epoch.

*The informed-flow impact.* `informed_impact_prob` was 0.01, derived from a
quotability bound written as `L = 7500 events at ~2 us apart`. Both inputs were
the bad clock, and a bound is the wrong shape for choosing a value anyway: it is
a ceiling, and every value below it satisfies it, including zero. It is fitted
now, to the one thing the captures state directly — P(the mid has moved against
the resting side one second after a print), which `tools/mdp_params.py` reports
as `p_adverse_1.0s`:

| | p_adverse |
|---|---|
| ethusd, 8-hour capture, n=5,032 | 33% |
| ethusd, 10-min sample, n=54 | 48% |
| xrpusd, 10-min sample, n=153 | 52% |
| btcusd, 10-min sample, n=220 | 63% |
| generator at 0.01 (the old value) | **13.6%** |
| generator at 0.85 (fitted) | **33.4%** |

*The inventory penalty.* `solve --penalty` defaulted to a bare 10 ticks/lot²/s.
That is 0.005 per epoch on a 0.5 ms grid and **1.0 per epoch on a 100 ms one** —
twenty-two times the entire per-epoch edge of a touch quote, so no policy quotes
at all whatever else is true of the market. It is now derived, at
Avellaneda-Stoikov's `gamma * sigma^2 * q^2`, from the mid volatility the params
measured: scale from the process, preference (`--risk-aversion`, default 1) from
the user. On these processes that gives 0.35 (simulator) and 0.12 (ethusd)
rather than 10.

**What the fix produced.** The sweep falls, which is what the diagnostic is
for:

| informed share | 0% | 20% | 50% | 70% |
|---|---|---|---|---|
| JoinTouch session P&L | 21,653 | 17,401 | 10,494 | 7,985 |

The budget and the table's epoch now agree exactly (100 ms, 5.4 market events)
and the warning no longer fires. The policy quotes both sides at the touch at
flat inventory, skews one side at the inventory limit, and pulls both in no
state. Its modelled fill hazard is within 1.6x of what its own orders realise,
against 20x historically.

**And the acceptance test now fails, interpretably.**

```
TabulatedMDP minus JoinTouch, paired by seed:
  mean -1703.4   95% CI [-2117.7, -1321.3]   over 24 seeds, 1 of them positive
```

(-3,418 when this was written; issue 2's move-fill fix, landed after, halved it.)

This is a real result rather than an abstention: the policy trades (3,108
passive fills against JoinTouch's 3,362) and earns less. It skews for inventory
— peak position 40.5 against JoinTouch's 59.0 — and pays for it in P&L on a
process where carrying inventory is not punished enough to be worth avoiding.
Sweeping risk aversion on the tune seed family (never the acceptance family)
says there is no value of it that wins:

| gamma | 0.1 | 0.25 | 1.0 | 4.0 |
|---|---|---|---|---|
| MDP minus JoinTouch | +0.0 | +5.6 | -1,645 | -1,854 |

At 0.1 the policy has become JoinTouch exactly and the comparison is degenerate;
at 0.25 it ties; above that it loses. The default stays at 1 because it is the
principled value, not the flattering one.

**What that leaves.** The acceptance test is answerable and the answer is no.
The likely reason is in the process rather than the solver: the calibrated
generator's imbalance signal runs 6.1 / 4.9 / 4.8 / 6.0 / 9.1 per cent across
the five buckets — barely monotone — where real ethusd runs 0.7 to 2.7 per cent
monotonically, and its queue-position gradient is 2.8x against the 60x measured
on a real book. A policy has little to exploit here. That is Phase 3 work, not
Phase 5's.

## 6. The Phase 5 acceptance table measured parameterisation, not strategy — FIXED, and the criterion is degenerate

**Symptom.** In `apps/evaluate`, `InventorySkew` took 2 passive fills and 37,224
aggressive ones over 60,000 events; `AvellanedaStoikov` took 6 and 31,748. Both
lost roughly 150,000 ticks x shares. `GLFT` scored 627.0 against
`ConstantSpread`'s 643.8 with 84 passive fills against 86 — the same strategy
with extra arithmetic. And `TabulatedMDP` was byte-identical to `JoinTouch` in
all seven columns, with the paired comparison reporting mean +0.0, CI
[+0.0, +0.0].

**Cause, part one: the centre was never clamped.** `detail::assemble` clamped the
half-spread into `[min_half, max_half]` and nothing clamped the centre. A
half-spread is a distance from a centre, so a strategy centred far from the
market emitted orders straight through it. With the shipped `QuoteParams`
defaults — `gamma 0.05`, `sigma 1.0`, `horizon 1e5` — the Ho-Stoll skew is
`gamma * sigma^2 * horizon` = **5,000 ticks per share**, so at an inventory of
one share the reservation price is 5,000 ticks below the mid and the ask is
quoted 4,999 ticks below the best bid.

Why it survived: `apps/backtest` (line 118) and `tests/test_strategies.cpp`
(line 24) both override `horizon` to 1.0. `apps/evaluate`'s `base_params()` sets
only `size` and `max_inventory` and leaves the risk parameters alone — so the
acceptance test was the one caller that got the raw default, and no test ever
constructed a default `QuoteParams` and asked a strategy for a quote.

**Cause, part two: `min_half = 1` erased GLFT's signal.** GLFT's inventory term
is 0.13 ticks at flat inventory and 1.20 at a 50-share limit. Clamping the
half-spread up to 1 collapsed the whole range onto one value, so on a one-tick
book GLFT quoted 9999/10002 at flat inventory and 9999/10002 at full.

**Fix.**
- `assemble` now takes the book and clamps each side to the **opposite** touch:
  `bid <= best_ask - 1`, `ask >= best_bid + 1`. Joining or improving the touch
  stays allowed; resting at or through the other side cannot happen. A one-sided
  book returns no quote at all, which also fixes `mid_of` reading half the other
  side's price when `best_bid()` answers 0 for an empty side.
- `horizon` defaults to 1.0 and `min_half` to 0 — the values every working
  caller already chose, and the tick grid is the real floor anyway.
- `QuoteParams::skew_per_share()` and `skew_at_limit()` name the product, so a
  configuration can be checked rather than multiplied out by hand.

**Result**, 2 seeds x 60,000 events:

| strategy | passive before → after | aggressive before → after | P&L before → after |
|---|---|---|---|
| InventorySkew | 2 → 117 | 37,224 → 0 | −171,431.8 → +122.0 |
| AvellanedaStoikov | 6 → 165 | 31,748 → 2 | −140,578.8 → +120.0 |
| GLFT | 84 → 464 | 0 → 15 | +627.0 → +241.8 |
| ImbalanceSkew | 82 → 378 | 1 → 22 | +178.5 → +1,026.0 |

**Tests added** to `tests/test_strategies.cpp`, each verified to fail on the old
code: every strategy's quote is asserted not to cross the book it was computed
from, the whole sweep is repeated on a **default-constructed** `QuoteParams`,
`skew_at_limit()` is asserted quotable, a one-sided book is asserted to produce
no quote, and GLFT's clamped quotes are asserted to vary with inventory **on a
one-tick book** — a wide book hides it, because at a 4-tick spread the mid is a
whole tick and even a clamped 1.0 against 1.2 lands on different ticks.

**The TabulatedMDP tie was not a bug, and the answer is worse than one.**
Dumping the action distribution of `policy/ethusd.bin` over all 14,080 states:

```
action (bid,ask)   states   share
   1 (0,1)          1280    9.1%     pull the bid   — exactly inventory +5
   4 (1,0)          1280    9.1%     pull the ask   — exactly inventory -5
   5 (1,1)         11216   79.7%     quote both at the touch
   6 (1,2)           160    1.1%     ask one tick behind
   9 (2,1)           144    1.0%     bid one tick behind
```

**97.8% of the state space is JoinTouch's rule exactly** — quote both sides at
the touch, pull one side at the position limit. The 1,280-state blocks are one
whole inventory level across every bid, ask and imbalance state. Only 304 states
(2.2%) differ, all of them "quote one side a tick behind", and they need
`|inventory| >= 3` **while alone at the touch**, which a 60,000-event run never
reaches. At 250,000 events over 6 seeds the tie does break: TabulatedMDP −84.4
against JoinTouch −87.8, on one extra requote out of 4,491.

So value iteration on this process converges to join-the-touch. That is a real
result, and it means the Phase 5 acceptance criterion is comparing a policy
against a baseline it has essentially reproduced. **Open**: whether that is the
truth about large-tick market making — where the decision is queue position
rather than price — or an artefact of a state space too coarse to express
anything else. The 2.2% of states that do differ are the place to look.

---

## 7. Trade sizes were capped at one resting order, and eta was never comparable

Two findings from building a scorecard for trade price impact
(`tools/impact.py`), one a real defect and one a correction to this project's
own reading of its numbers.

### The trade-size tail was drawn and discarded

`qr_add` gave every resting order exactly `aes` lots -- the queue-reactive
paper's constant-size assumption, and the reason its queue axis is in average
event sizes at all. A trade can consume at most one resting order per execution,
so with every order identical **no trade could ever exceed 1.0 AES**.

Measured over 6,708 simulated trades:

```
fraction at exactly one full order (240 lots): 12.4%
fraction above one full order:                  0.0%
```

`trade_size_aes` fits a tail out to 5.23 AES from the capture, `qr_trade` draws
from it faithfully, and the book then chopped every draw into 240-lot pieces.
The fitted tail never reached the output.

**Fix.** Resting order sizes are drawn from a distribution measured over 13,458
orders in the ethusd capture, at sixteenth quantiles, normalised so the mean
draw is exactly 1.0 AES. The normalisation is the load-bearing part: the raw
distribution has a mean of 2.86 AES, and using it directly would have tripled
every queue's volume and broken the depth calibration Model I's rates were
fitted against. The shape is measured; the scale is the one already in the
model. `reinitialise()` draws too, or a redraw would reset the book to constant
sizes.

Measured effect, 300,000 events against the ethusd capture:

| statistic | before | after | ethusd |
|---|---|---|---|
| P(mid moves within 1 s of a print) | 10.6% | 13.8% | 55.6% |
| of those moves, went WITH the trade | 74.9% | 80.9% | 86.7% |
| mean signed impact, bp | 0.055 | 0.084 | 0.676 |
| trade sizes above one order | 0.0% | present | present |

### eta was comparing two different statistics

`docs/06` records the simulator's Robert-Rosenbaum ratio at 0.49 against
ethusd's 0.84 and reads it as "the simulator's price is a random walk where the
market's trends". That reading does not survive measuring the tick grid:

| | mid level | one tick | ticks/s traversed | bp/s |
|---|---|---|---|---|
| ethusd | 245,340 | 0.041 bp | 2.41 | 0.098 |
| simulator | 10,048 | 0.995 bp | 0.06 | 0.059 |

eta is the sign of consecutive grid changes, so what it measures depends on how
many ticks the price crosses between samples. ethusd crosses 2.41 a second on a
tick worth 0.04 bp, and the sign tracks drift. The simulator crosses 0.06 a
second on a tick worth 1 bp, so it usually does not move at all and the sign
tracks the touch flickering by half a tick. **In basis points per second the two
volatilities are 0.098 and 0.059 -- within a factor of 1.7.**

So the gap eta reports is tick resolution, not a missing mechanism. I swept
`--reinit`, `--excite` and their combinations and eta never crossed 0.5 under
any of them, while `adv|mv` moved from 65.4% to 91.2% -- the excitation is
creating directional impact, and eta simply cannot see it at this tick size.

`tools/impact.py` now prints `tick_bp`, `ticks/s` and `bp/s` beside eta so the
comparison cannot be read the old way again.

**Still open.** Mean signed impact is 0.084 bp against 0.676, a factor of 8. That
is a volatility-scale question -- how far the reference price should walk per
unit of signed flow -- and not the "trades carry no information" question it was
being read as: conditional on the mid moving at all, 80.9% of moves already go
with the trade, against 86.7% on the capture.

---

## 8. The acceptance test was ranking on a coin flip — FIXED

`docs/AUDIT.md` M18 recorded the Phase 5 acceptance test as noise-dominated:
mean −1,136 with a 95% interval of [−3,872, +1,516], 6 of 16 seeds positive. The
proposed remedy was to raise the seed count until the interval excluded zero.

That remedy would not have worked, and the reason is worth recording.

### What it looks like now

With the baselines fixed (issue 6 above, which is what gave the test any power
at all), 16 seeds at 120,000 events:

```
  TabulatedMDP minus ConstantSpread, paired by seed:
    mean -3794.8   95% CI [-5224.5, -2391.2]   over 16 seeds, 2 of them positive
    FAILS: significantly WORSE than the best baseline
    per-seed sd 3043.8  =  trading edge sd 337.8  +  closing-mark sd 3003.3
    to resolve an effect this size at 95%/80%: 6 seeds (have 16)
    the closing position, not the trading, is what this interval is mostly measuring
    on TRADING EDGE alone (inventory-neutral): mean +1753.3  95% CI [+1591.8, +1907.2]  (excludes zero, positive)
```

### The two things that says

**The test is not underpowered.** Six seeds would resolve an effect of that size
and it has sixteen. It resolves its number precisely. The number is the wrong
one.

**98.7% of the variance is the closing position.** Of a per-seed standard
deviation of 3,043.8, the trading edge contributes 337.8 and the mark on the
leftover position contributes 3,003.3. Session P&L is trading edge plus that
mark, and the mark is a large zero-mean term: a strategy holding 59 shares at
the end of a 2,200-second run is exposed to a price walk whose sign is a coin
flip and whose scale dwarfs a run's worth of spread capture.

So on the statistic being ranked, `TabulatedMDP` loses decisively. On trading
edge, it beats `ConstantSpread` by +1,753 with an interval of [+1,592, +1,907]
that excludes zero on the *positive* side. Both are true. The first is dominated
by how much inventory a strategy happened to be carrying when the clock stopped;
the second is what it did while trading.

More seeds shrink the interval around a mean that is itself mostly the average
of coin flips. They do not separate skill from position risk, because that is not
a sampling problem.

### What changed

`apps/evaluate` now prints, under every paired comparison:

- the per-seed standard deviation, **split** into its trading-edge and
  closing-mark parts, so it is visible which one the interval is resolving;
- the number of seeds needed to resolve the observed effect at 95%/80%, or a
  note that the effect is indistinguishable from zero and more seeds will not
  help;
- a warning when the closing mark dominates;
- the same paired comparison computed on **trading edge alone**.

Session P&L stays the headline. It is the right number to report -- a position
carried to the end is a real cost and `include/lob/strat/driver.hpp` is right
that a decomposition credits nothing to a strategy that made its money by
holding. It is the wrong number to *rank* on at this seed count, and the tool now
says so rather than leaving a reader to conclude the policy is bad at trading.

**Still open.** Ranking on trading edge alone would credit a strategy that
accumulates inventory and never closes it. The honest fix is a comparison that
charges the closing position at the price of flattening it rather than marking it
at the mid -- that is a real cost with a much smaller variance than a coin flip
on the mid. That needs the impact model in issue 7, which is why the two are the
same piece of work.

---
