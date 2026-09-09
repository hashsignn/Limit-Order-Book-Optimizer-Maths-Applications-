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

## 2. The MDP prices a touch quote as a guaranteed loser on real data

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

**Update 2026-09-09: the second measurement exists now, and a THIRD cause was
found that is not on the list above.**

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

It still prefers one tick behind the touch at flat inventory, and quotes at the
touch only on the skewed side (18.2% of states, against the 0% this entry
recorded). So the penalty explains why the market looked unquotable; it does not
explain the preference for standing behind. That part is candidate 2's.

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
  mean -3418.3   95% CI [-3942.1, -2913.5]   over 24 seeds, 0 of them positive
```

This is a real result rather than an abstention: the policy trades (2,837
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
