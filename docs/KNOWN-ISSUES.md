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

**What it does not affect.** The Phase 5 acceptance test, which runs on the
simulator, where the same solver produces a policy that quotes at the touch in
73% of states. This is the real-instrument table only, and that table is a
demonstration artefact.

**Do not fix by picking one.** Measure `move_ticks` on a book without issue 1's
holes, and separately measure P(mid move is adverse | our quote was taken), and
let the two numbers say which it is.

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

## 4. The generator's touch moves sixty times too often

**Found** 2026-09-09, measuring the corrected generator against the ethusd
capture.

| | touch moves |
|---|---|
| synthetic | **18.5 /s** |
| ethusd (8-hour capture) | **0.29 /s** |

Sixty-fold. The event mix is now fitted -- adds 45.5%, cancels 50.6%, trades
3.93% against a measured 43.5 / 53.9 / 2.60 -- so the generator produces roughly
the right KINDS of event in roughly the right proportions, and still moves its
price sixty times too fast. Mix and dynamics are separate calibrations and only
the first has been done.

**Caveat on the real number.** ethusd's 0.29/s comes from a capture where the
`max_spread` filter discards 39% of steps and the book has the holes of issue 1,
both of which suppress observed moves. The true figure is higher than 0.29 and
the gap is therefore smaller than sixty-fold, but the sign is not in doubt.

**Why it matters.** Adverse selection is the mid moving against a maker after a
fill, and its rate is set by how often the mid moves. A process that moves sixty
times too often prices a market maker's central risk sixty times too high, which
is exactly the direction that makes quoting look unattractive -- see issue 2,
where a touch quote came out worth −0.0138 ticks an epoch.

**It also cost a test.** `tests/test_simulator.cpp` asserted that latency turns
some passive quotes into aggressive fills. That needs the opposite touch to
collapse a spread onto the quote within its flight, and at 200,000 events it
happened zero times once the trade rate was fitted -- the assertion had been
resting on a process that traded five times too often. It passes at a million
events (151 such fills), and the count is now measured in the test rather than
assumed.

**Next.** Calibrate the touch dynamics, not just the mix: the rate at which the
best level is consumed or cancelled away is a queue-reactive quantity, and the
table `stats` already writes has it per level and per queue size.
