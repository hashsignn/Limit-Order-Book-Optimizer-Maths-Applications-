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

## 8. The acceptance test's "trading edge" was not a decomposition of its P&L

`docs/AUDIT.md` M18 recorded the Phase 5 acceptance test as noise-dominated and
proposed raising the seed count until the interval excluded zero. That remedy
would not have worked -- the test resolves its number precisely at 16 seeds --
and the first attempt to explain *why* got the explanation wrong.

### What this entry used to say, and why it was wrong

It said 98.7% of the per-seed variance was the closing position, and that on
trading edge `TabulatedMDP` beat `ConstantSpread` by +1,753 with an interval
excluding zero on the positive side. Both numbers came from splitting session
P&L as

```
    trading edge  =  Attribution::total
    closing mark  =  pnl() - Attribution::total
```

**That is not a split.** `Attribution::total` is spread capture minus adverse
selection measured on each fill at a **100 ms markout**. Session P&L is cash
exchanged plus the open position at the closing mid. The difference between them
is not the closing position -- it is everything the 100 ms window missed, which
on a run of hours is nearly all of it. The residual was labelled a closing mark
and it was a horizon error.

It shows in the growth law. A bounded position on a random walk has a spread
that grows as the square root of the run; the residual grew as `n^0.85`. Nothing
about a closing position can do that.

### The exact split, which is available and was not used

Session P&L is cash plus the open position at the closing mid, and only one term
in it touches the closing mid:

```
    pnl  =  cash + I x M_final
         =  (cash + I x M_open)  +  I x (M_final - M_open)
            \------ flat P&L ------/   \---- price-walk term ----/
```

The first term is what the run would have made had the price never moved: it is
spread capture, valuing whatever is left over at the price it started at. The
second is a position times a walk. They sum to session P&L identically.
`RunResult::flat_pnl()` and `RunResult::walk_exposure()` are those two terms.

### What the exact split says

The two halves are strongly **anti-correlated** -- a maker that ends short has
banked the cash for being short, so a run whose flat P&L is high has a walk term
that is low -- so comparing their standard deviations proves nothing, and each
can exceed the sd of their sum. The share of the spread that the closing
position accounts for is a regression, and it reads:

```
  16 seeds, TabulatedMDP minus the best baseline

  events/seed   opponent            total sd   walk sd   walk explains   flat-price mean
     120,000    ConstantSpread         3400      5407          3%        -2029  [-4655, +821]
     480,000    AvellanedaStoikov      7693     10544         42%       -11709  [-15427, -7878]
   1,815,598    InventorySkew         23533     13427          2%       -39751  [-51560, -27489]
```

**The closing position explains 3% of the acceptance interval, not 98.7%.** And
at a flat price -- with the price walk removed exactly, not approximately --
`TabulatedMDP` does not beat the best baseline at any run length. At the
acceptance run length the interval spans zero; at four times it and at fifteen
times it, the interval excludes zero on the *negative* side.

So the previous entry's conclusion is withdrawn. The policy is not a good trader
being punished by a coin flip. It loses on the trading.

### Where the +1,753 came from, and what it was telling us

It was real, and it was about a horizon. `TabulatedMDP`'s fills genuinely look
better than `ConstantSpread`'s 100 ms after they happen: 2,720.7 of spread
capture against 520.4, and adverse selection that *pays* the maker 476.3 rather
than 979.2. The money is lost afterwards.

The longest markout this repository measures is **1 second**. The simulator's
price changes its touch 0.0778 times a second -- once every 12.9 seconds. Every
markout horizon in `kMarkoutHorizons` is shorter than the process's own
price-move timescale, the longest by a factor of 13, so what they measure is the
queue refilling after a trade rather than any information in it. That is also
why adverse selection comes out as a gain: on this process, a fill at the touch
is usually followed by the price coming back.

Attribution stays as the diagnostic it is. It is not a decomposition of session
P&L and must not be differenced against it.

### Does a longer run fix the test?

Only if there is an edge to accumulate, and the flat-price column above says
there is not. The arithmetic is still worth stating, because it is what would
apply to a strategy that did have one: flat P&L accumulates linearly in the run
length and the walk term grows as its square root, so their ratio grows as the
square root of the run. `apps/evaluate` prints the run length at which the
challenger's own flat P&L would reach twice its own walk sd -- on the challenger
alone, deliberately, because the *paired* version is not stable: the best
baseline is chosen per run and it moves with run length (ConstantSpread at
120,000 events, AvellanedaStoikov at 480,000, InventorySkew at 1,815,598), and
the answer swung from 1.8M events to 13.7M as a result. When the challenger's
flat P&L is negative, as it is here, the tool says so rather than printing a
number.

That the opponent moves at all is issue 6's degenerate criterion surfacing in a
second place.

### Flattening the closing position does not fix it either

An earlier proposal was to charge the closing position at the price of
*flattening* it rather than marking it at the mid, "a real cost with a much
smaller variance than a coin flip on the mid".

Flattening does not remove the mark. It **realises** it. A strategy holding 37
shares sells them into the book at the prevailing price, which is the mid the
mark was taken at, less the cost of crossing:

```
  mean closing position          37 shares  (of a permitted +-50)
  crossing cost at 0.5 ticks     ~19 ticks x shares   -- a CONSTANT, given the book
  sd of the walk term it would realise   5407 ticks x shares
```

It moves the mean by about 19 and the variance by nothing. The variance was
never in the choice of exit price; it is in where the price had got to, and
closing at that price keeps all of it.

### What changed

`include/lob/strat/driver.hpp` gained `start_mid`, `flat_pnl()` and
`walk_exposure()` -- the exact split, with the algebra written beside it.

`apps/evaluate` uses them instead of differencing attribution against P&L, and
under every paired comparison now prints the per-seed sd with the **share the
price walk explains by regression**, the paired comparison recomputed at a flat
price, the run length that would be needed if there were an edge to accumulate,
and the seeds needed to resolve the observed effect.

Its results table gained an **`end inv`** column, which the prose above that
table had been promising since before the column existed: the mean *magnitude*
of the closing position, because its sign is a coin flip. It reads 10 to 37
shares of a permitted 50. `peak inv` stays beside it and says something
different -- every strategy reaches its cap during a run and overshoots it by
about a fifth (55 to 59 against 50), which is latency filling a quote decided
before the limit was hit.

---

## 9. The simulator's volatility was right only because its tick is 24x too fat — step size FIXED, rate still open

The standing complaint was that the simulated price barely moves: 0.06 ticks a
second against ethusd's 2.41, a factor of 40. Issue 7 answered it by converting
to basis points -- 0.060 bp/s against 0.098 -- and concluded the gap was tick
resolution rather than a missing mechanism.

**That conversion was measuring the wrong thing, and the conclusion it supported
was half right in a way that hid the real number.**

### ticks/s x tick_bp is not volatility

It is *total variation*: the length of the path the price traced. Variance per
unit time is rate times step **squared**. A coarse tick crossed rarely and a
fine tick crossed often can have the same path length and wildly different
variance, and that is exactly the case here.

The volatility is the standard deviation of the log return over a fixed clock.
On non-overlapping one-second windows:

```
                    tick_bp   ticks/s   path (bp/s)   sigma(1 s)
  ethusd             0.0408      2.41         0.098       0.2918
  btcusd             0.0013    103.03         0.134       0.3891
  simulator          0.9890      0.06         0.059       0.3007
```

The simulator lands between the two instruments. On the statistic that actually
governs an inventory's risk, its volatility is not 40x low, or 1.6x low. It is
right.

### And that is the problem

It is right because the simulator runs at a mid of 10,000 ticks, where one tick
is worth 1.0 basis points. Ethusd trades at 245,422 ticks, where one tick is
worth 0.041. Run **the identical process** at ethusd's price level -- the only
thing that changes is the denominator converting ticks to bp -- and:

```
  the same queue-reactive process, two price levels, same seed, 2M events

    mid  10,000 ticks   0.0778 touch changes/s   0.0680 ticks/s   sigma(1s) 0.2885 bp
    mid 245,422 ticks   0.0778 touch changes/s   0.0680 ticks/s   sigma(1s) 0.0121 bp
    ethusd, measured    0.3910 touch changes/s   2.4111 ticks/s   sigma(1s) 0.2918 bp
```

The dynamics are bit-identical, as they must be: this is a tick-lattice process
and `FlowConfig::mid` is only where it starts. **On the market's own tick grid
the model produces one twenty-fourth of the market's volatility.** The fat tick
is what has been hiding that, and it was never a modelling choice -- 10,000 is
the default from the first stress generator, written long before anything was
calibrated.

This is the paper's own finding, worse. Huang, Lehalle and Rosenbaum report
their purely order-book-driven Model III giving 5 bps against an empirical 14,
a factor of 2.8, and say plainly that mechanical volatility cannot be the whole
story. Ours is a factor of 24.

### The mechanism, in one line

`reference_price_step` moves the reference price by **exactly one tick** when a
best queue empties. `queue_price(side, level) = mid -+ level`, so the modelled
queues sit at consecutive ticks and the level behind the touch is always exactly
one tick away. The model has no way to express a gap.

The real book is nothing like that. `apps/stats` now writes a `gap.csv` -- how
far the touch is from the next price holding anything, read off the same walk
the depth profile uses -- and it is the number the reference price has never
had:

```
            gap to next occupied price          occupancy by ticks behind the touch
            mean  median   p90   P(=1)      1      2      3      4     ...    11
  ethusd    5.62       4    12   20.4%    20.4%  18.9%  14.7%  13.8%   ...  17.7%
  xrpusd    4.21       2    10   42.5%    42.5%  15.3%  17.6%  16.4%   ...  16.6%
  simulator 1.72       2     3   48.7%    24.7%  20.9%  17.1%   0.1%   ...   0.0%
```

The three levels the model has are right. There was nothing behind them, because
`kLevels` was 4 and the queues sit at consecutive ticks, so the modelled book was
exactly three ticks deep. A cross-check: the mid moves a mean of 6.15 ticks on
ethusd when the touch changes, against a measured gap of 5.62 -- the same
quantity reached two ways.

Neither number is an artefact of the reconstruction. `apps/stats` now takes
`--seed-guard-pct`, and sweeping it from 0 (seed the entire snapshot) to 1.0%
leaves the volatility, the touch rate, the jump size, the spread and the whole
occupancy profile **identical to four decimal places**. The snapshot's orders
never become the touch in these ten minutes; everything here is stream-built.

So the deficit factors cleanly, and neither factor is small:

```
                        ethusd    simulator   short by
  touch changes /s       0.391       0.0778      5.0x
  ticks per change       6.15        0.87        7.1x
  ------------------------------------------------------
  ticks /s               2.41        0.068      35x
  sigma at equal tick    0.2918 bp   0.0121 bp  24x
```

`FlowConfig::Qr::kLevels = 4` is half of it -- the modelled book stopped three
ticks behind the touch where the real one is still 14% occupied at eleven -- and
`reference_price_step` moving exactly one tick is the other half.

**An earlier version of this section said raising `kLevels` would make things
worse, on the grounds that the model already kept more prices occupied near the
touch than any instrument did.** That was measured with `apps/tape`, over a
different window and a shallower level cap than the depth profile uses, and it
disagreed with the depth profile by a factor of two. It was wrong: counting
occupied prices in the first ten ticks, the model had 1.63 and ethusd 2.38.
`gap.csv` exists so that this is read off the same book walk as everything else.

### The number this all comes down to

Spread and volatility are both in basis points, so their ratio survives any
choice of tick. It is what says whether making a market on a book is easy:

```
                mean spread   sigma(1 s)   spread / sigma
  ethusd           0.0764 bp     0.2918          0.26
  btcusd           0.0299 bp     0.3891          0.08
  simulator        1.0621 bp     0.3007          3.53
```

**A maker in this simulator is paid 13.6x more spread per unit of price risk
than a maker on ethusd, and 44x more than one on btcusd.** That is the single
most consequential distortion in the repository, and every Phase 4 and Phase 5
result sits on top of it. It is also why every strategy in `apps/evaluate` shows
a trading edge whose interval excludes zero on the positive side: on this book,
quoting is nearly free money.

It does **not** distort the comparison *between* strategies. Everything in the
model is denominated in ticks, so `FlowConfig::mid` scales edge and risk by the
same factor and cancels out of any paired difference. What it distorts is every
comparison against the real market -- which is what Phase 6 is for.

### What changed -- the step size, which was two thirds of it

**`kLevels` is 16.** Levels 0 to 3 stay as fitted; levels 4 and beyond repeat
level 3. That is not an assumption: the estimator records level 4 as well, and
on ethusd it measures the same as level 3 (0.191 adds/s against 0.191, 0.196
cancels against 0.182), which is the paper's own K = 3 finding -- Q_4 and Q_5
behave like Q_3 -- that this file already invokes for `far_cancel_per_order`.
Sixteen covers the measured gap distribution, whose p90 is 12.

**`reference_price_step` moves the price to the next resting order on the
emptied side**, not by one tick. Where it lands is read off the book rather than
drawn from a new distribution: the generator already tracks every order it
believes is live, so the jump distribution is a *consequence* of the modelled
depth profile, which is fitted. No new parameter.

**`apps/stats` writes `gap.csv`** and takes `--seed-guard-pct`, so the question
"how much of the book's shape is the guard's hole rather than the market's" can
be asked with the tool that is natural for it.

**`tools/impact.py`** reports `sig(1s)`, `spr_bp` and `spr/sig` and no longer
reports `bp/s`, which was labelled "scale-free volatility -- THIS is comparable"
and was neither.

What it bought, measured on 2,000,000 events:

```
                              before    after    ethusd
  gap to next occupied price    1.72     4.69      5.62   ticks
  occupied prices in 10 ticks   1.63     2.47      2.38
  mean touch move               1.76     5.16     11.91   ticks (grid-sampled)
  sigma(1 s), common tick grid  0.301    1.031     7.162  ticks
  price impact of a trade       0.084    0.629     0.676  bp
  P(mid moves | a print)        13.6%    25.1%     55.6%
  lift                          2.63     5.25      3.29
  eta                           0.48     0.55      0.84
  spread / volatility           3.53     1.17      0.26
```

**The price impact of a trade now matches the market**, which closes the gap
issue 7 left open: 0.629 bp against 0.676, from 0.084. Volatility on a common
tick grid is 3.4x what it was, and eta has crossed 0.5 -- the price trends now,
where every previous attempt to make it trend (Model II-b, the Hawkes kernel,
long memory in the flow, sweeping theta) moved it not at all. The reason those
failed is written up in `docs/06`: a persistent flow can only make a persistent
price if trades move the price, and they did not.

### What is left, and it is now one thing

**The touch clears 0.039 times a second against ethusd's 0.228** -- a factor of
5.8, and it is the whole of the remaining volatility gap. The step size is
fixed; the rate is not.

That rate is not a free parameter either. It is `P(the touch queue reaches
zero)`, which the fitted mean queue size of 4.29 AES and the fitted event rate
of 1.17/s imply between them. Matching it means the real touch queue is burstier
than a birth-and-death queue with that mean can be -- it empties far more often
and refills to larger sizes -- which is Model I's independence assumption
failing in a way the closed form cannot express. That is a new piece of work,
now gap 8 in `docs/06`.

It has a second consequence worth stating plainly: **the process still fails
`tools/mdp_params.py`'s usability gate**, and for this reason. The mid moves in
0.4% of 100 ms epochs against a 0.5% bar and ethusd's roughly 3.9%. Phase 5
cannot be re-solved end to end until the clearing rate is addressed -- the
shipped table in `simpolicy/` was measured on a different process again, at a
0.5 ms epoch the queue-reactive clock cannot support.

Raising `FlowConfig::mid` remains the wrong fix and is still not done: it would
make the tick honest and the volatility 24x too small.
