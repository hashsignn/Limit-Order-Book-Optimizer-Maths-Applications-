# The queue-reactive model: what the paper specifies, and what we have

Huang, Lehalle & Rosenbaum, *Simulating and analyzing order book data: the
queue-reactive model*, arXiv:1312.0563 (JASA 110:509). Read from the paper's
full text, not from a summary of it. This file is the implementation spec and
the gap list; it is not a summary of the results.

Why it is the next thing. The acceptance test now fails for a reason that is in
the process rather than the solver: the generator's imbalance signal runs
6.1 / 4.9 / 4.8 / 6.0 / 9.1 per cent across the five buckets where real ethusd
runs 0.7 to 2.7 monotonically, and its queue-position fill gradient is 2.8x
against 60x on a real book. A policy has nothing to exploit. Both of those are
consequences of order flow whose rates do not depend on the state of the book,
which is exactly what this model fixes.

---

## The state, and the units

The book is a `2K`-dimensional vector of queue sizes `q_i` around a reference
price `p_ref`, where `Q_i` is the limit `i - 0.5` ticks from `p_ref` on the ask
side and `Q_-i` on the bid. **K = 3** is enough: the paper reports `Q_4` and
`Q_5` behaving like `Q_3`.

`p_ref` is the midprice when the spread is an odd number of ticks. When it is
even the midprice is itself a legal price, so `p_ref` is `p_mid` plus or minus
half a tick, whichever is closer to the previous `p_ref`.

**Queue size is measured in units of AES**, the average event size at that
queue: `q_i = ceil(volume / AES_i)`. Not raw shares. This is the single most
consequential detail for us and we do not do it — see the gap list.

---

## The estimator

Define an event `w` as any change to the queue size. For queue `Q_i` record the
waiting time `dt_i(w)` since the previous event at `Q_i`, the event type
`T_i(w)`, and the queue size `q_i(w)` **before** the event. Then

```
  Lambda_i(n) = 1 / mean( dt_i(w) | q_i(w) = n )

  lambda^L_i(n) = Lambda_i(n) * #{T in adds,    q_i = n} / #{q_i = n}
  lambda^C_i(n) = Lambda_i(n) * #{T in cancels, q_i = n} / #{q_i = n}
  lambda^M_i(n) = Lambda_i(n) * #{T in trades,  q_i = n} / #{q_i = n}
```

A total rate times a type share. Confidence intervals come from a normal
approximation on `Lambda` and a binomial one on each share, multiplied.

Bid and ask are pooled by symmetry: `lambda_i(n) = lambda_{-i}(n)`.

When `p_ref` changes, the recording restarts.

---

## The shapes it finds (France Telecom, a large-tick stock)

These are the qualitative facts to reproduce, and each one is a place our
generator is currently wrong.

| flow | at the touch (`Q_1`) | behind it (`Q_2`, `Q_3`) |
|---|---|---|
| limit insertion | roughly **constant** in `q`, and markedly **smaller at q = 0** | **decreasing** in `q` |
| cancellation | **increasing and concave** to about 25 AES, then flat or slightly falling | rises near-linearly past 3 AES, plus a **spike at q = 1** |
| market orders | **exponentially decreasing** in `q` | same shape, much smaller |

Three readings the paper gives, worth keeping because they say what mechanism
each shape is:

- Inserting into an empty queue creates a new best limit and leaves you alone
  there, which is risky, so the rate drops at zero.
- Cancellation is **not** linear in queue size, contrary to Cont, Stoikov &
  Talreja. Priority value rises with queue length and orders with priority are
  not thrown away. The spike at `q = 1` is people cancelling when they find
  themselves alone.
- Market order intensity falls with available volume: participants rush for
  liquidity when it is scarce and wait for a better price when it is abundant.

---

## The three models

**Model I.** Queues independent; intensities depend only on the queue's own
size. The book is `2K` independent birth-and-death processes. Its invariant
distribution is closed-form, which makes it a **test rather than an
assumption**:

```
  rho_i(n) = lambda^L_i(n) / ( lambda^C_i(n+1) + lambda^M_i(n+1) )
  pi_i(n)  = pi_i(0) * prod_{j=1..n} rho_i(j-1)
  pi_i(0)  = ( 1 + sum_{n>=1} prod_{j=1..n} rho_i(j-1) )^-1
```

Simulate, sample the depth every 30 s, and compare against this. The paper's
match is close, and a Poisson model with a linear cancellation rate visibly
overestimates execution probabilities against the same data.

**Model II-a.** `Q_2`'s limit and cancel intensities also depend on
`1{q_1 > 0}` — whether the queue in front is empty. Market orders are sent only
to `Q_1` and `Q_2`: when `q_1 > 0` the intensity is a function of `q_1`, and
when `q_1 = 0` a function of `q_2`, because `Q_2` is then the best offer. The
paper is explicit that "the market order arrival rate when Q2 is the best limit
is not very different from that at Q1", and that a market order sweeping several
limits is treated as several orders arriving in quick succession at each.

Its findings at `Q_2`: the **cancellation rate is higher when `q_1 = 0`**, which
it relates to trading activity concentrating at best limits; and when `q_1 > 0`
the cancel rate is large at `q_2 = 1`, as at `Q_3`. Limit insertion at `Q_2` is
a decreasing function of queue size, read as posting at a non-best limit while
it is small to seize priority, then waiting for it to become best.

**Model II-b.** `Q_1`'s intensities depend on the **opposite** queue, bucketed
as `S_{m,l}(q_-1)` with `m = 4 AES_1` and `l = 9 AES_1`. Findings: limit
insertion falls as the opposite queue grows and is much larger when it is
empty; market orders rise when the opposite side is abundant. The paper notes
that the first-level intensities can equivalently be written as functions of
the **imbalance** `(q_1 - q_-1)/(q_1 + q_-1)`.

**This is the imbalance signal we are missing**, and it is the same variable
already in our MDP state.

**Model III — the queue-reactive model.** `p_ref` moves by one tick with
probability `theta` when `p_mid` moves and `q_{±1} = 0`, triggered by any of:
a limit order inserted inside the spread against an empty best queue, a
cancellation of the last order at a best queue, or a market order consuming it.

On a move, queues shift toward the new centre: each `q_i` takes its neighbour's
value, `q_1` becomes zero, and the outermost `q_3` is drawn from its invariant
measure. **Renormalise by the AES ratio when `q_i` becomes `q_j`** — the
average event size differs by level and the paper flags this as easy to get
wrong.

With probability `theta_reinit` the whole book is instead redrawn from its
invariant distribution: "the percentage of price changes due to exogenous
information."

`theta` and `theta_reinit` are calibrated against the **10-minute volatility**
and the **mean-reversion ratio** `eta = N_c / (2 N_a)`, continuations over twice
the alternations of `p_ref` (Robert & Rosenbaum 2011).

---

## Maximal mechanical volatility

At `theta_reinit = 0` and `theta = 1` the model is purely order-book driven, and
the highest volatility it can reach is **5 bps against an empirical 14 bps**.
The paper's conclusion: endogenous book dynamics alone cannot reproduce market
volatility, and an exogenous component is required.

That is a direct check on our own exogenous channels rather than a licence for
them. `drift_prob` and the informed-flow impact exist for this reason and
should end up carrying roughly the share this implies, not more.

---

## Gap list, against what is in the repository now

1. ~~**`QueueReactive` bins on `log2` of raw quantity.**~~ **Done.** The axis is
   `ceil(volume / AES)` in linear steps now, with AES measured per level during
   the warmup the caller already discards and pooled across sides the way the
   paper pools the intensities. The touch went from about six occupied buckets
   to seventeen (ethusd), twenty-one (btcusd) and eighteen (xrpusd), and
   `tests/test_queue_reactive.py` fails if that collapses again.

   With the axis fixed, the slopes at the touch are:

   | slope vs queue size | btcusd | generator | paper |
   |---|---|---|---|
   | adds | +0.10 ± 0.24 | +0.10 ± 0.01 | flat — **agrees** |
   | cancels | −1.03 ± 0.39 | +0.71 ± 0.04 | rising, concave |
   | trades | −0.83 ± 0.11 | +0.07 ± 0.01 | falling, near-exponential |

   Adds already match. Trades have the **wrong sign** and that is the paper's
   central shape. Cancels have the wrong sign too, and note our own captures
   also disagree with the paper's rising cancel rate — on a ten-minute sample,
   so this needs the eight-hour captures before anything is fitted to it.

2. ~~**`FlowGenerator` intensities do not depend on the book at all.**~~
   **Done, as an opt-in path.** `FlowConfig::Qr` holds the three intensity
   functions and `FlowConfig::ethusd_queue_reactive()` turns them on; every
   `(side, level)` is now a birth-and-death queue whose rates are functions of
   its own size in AES units, one event is drawn in proportion to its own rate,
   and the clock advances by an exponential draw at the total rate — so the
   interevent distribution is a consequence of the model rather than a setting.
   Cancellation picks uniformly **within** the named queue, the paper's
   Assumption 3, not uniformly over the whole book.

   The scales are fitted, the shapes are the paper's. A queue's stationary law
   depends only on the arrival/departure **ratio**, so the ratios were fitted to
   the depth targets and one division set the event rate; no target was traded
   against another. Against the ethusd capture, per side:

   | | L0 | L1 | L2 | L3 |
   |---|---|---|---|---|
   | mean q, target | 4.29 | 0.49 | 0.43 | 0.39 |
   | P(q=0), target | — | 0.79 | 0.80 | 0.85 |
   | P(q=0), model | — | 0.793 | 0.803 | 0.847 |
   | events/s, per side | 1.17 | 0.237 | 0.179 | 0.186 |

   Verified against Model I's **closed-form invariant distribution**, with the
   reference price pinned because that is the regime the closed form describes:
   total variation **0.013**. `tests/test_calibration.cpp` asserts it stays
   under 0.10, which is loose enough to survive a re-fit and tight enough to
   catch an intensity wired to the wrong rate.

3. ~~**Market order intensity has the wrong sign in `q`.**~~ **Done.** At the
   touch, against queue size:

   | | ethusd | btcusd | xrpusd | old generator | Model I |
   |---|---|---|---|---|---|
   | adds | +0.38 ± 0.35 | +0.10 ± 0.24 | +0.85 ± 0.25 | +0.10 ± 0.01 | +0.02 ± 0.00 |
   | cancels | −0.26 ± 0.17 | −1.03 ± 0.39 | +0.18 ± 0.36 | +0.71 ± 0.04 | +0.45 ± 0.02 |
   | trades | too few | −0.83 ± 0.11 | too few | +0.07 ± 0.01 | **−0.42 ± 0.06** |

   Trades fall with queue size now, where the generator had the sign backwards.
   That is the fix.

   **What this table does not support**, and an earlier version of this file
   claimed it did: the three instruments do not agree with each other on any
   row. Two cancel slopes are indistinguishable from flat and the third is
   negative at 2.6 standard errors, so ten minutes per instrument cannot say
   whether the paper's rising cancel rate is right — the earlier claim that "our
   captures say falling" was btcusd alone. The trade row rests on 179 events on
   one instrument; the other two produced too few trades to fit. The shapes are
   the paper's on the paper's authority and the eight-hour captures are what
   would test them.

4. ~~**No dependence on the opposite queue.**~~ **Done.** `QueueReactive` now
   carries the paper's four-regime axis `S_{m,l}(q_-1)` — empty, small, usual,
   large, with `m` and `l` the 33% and 67% quantiles of the touch queue
   conditional on positive, measured at 2 and 6 AES on ethusd, 2 and 4 on
   btcusd, 1 and 3 on xrpusd. Both of the paper's findings hold, per side:

   | opposite queue | | small | usual | large |
   |---|---|---|---|---|
   | ethusd | adds/s | 0.664 | 0.391 | 0.256 |
   | | trades/s | 0.039 | 0.067 | 0.055 |
   | btcusd | adds/s | 0.868 | 0.424 | 0.371 |
   | | trades/s | 0.189 | 0.175 | 0.379 |
   | xrpusd | adds/s | 0.428 | 0.335 | **0.760** |
   | | trades/s | 0.066 | 0.166 | 0.308 |

   Limit insertion falls with the opposite queue on the two large-tick
   instruments, for the paper's reason: a thick opposite side puts the efficient
   price nearer it, so quoting this side is profitable. Market orders rise with
   it on all three, which is the same reasoning from the taker's end, and it is
   the mechanism that puts a signal in imbalance — more selling into the bid
   exactly when the ask is thick.

   xrpusd inverts the add row, and it is the small-tick instrument. Same split
   Model II-a showed, and the constants here are the large-tick ones.

   Applied at the touch only, as multipliers normalised to the exposure-weighted
   mean, so they leave the marginal alone provided the simulator's regime
   occupancy matches the capture's. Measured after the fact it barely moves:
   touch adds go 0.546 to 0.562 per second with the multipliers off and on.

   P(next mid move is up) across the five imbalance buckets:

   | | 1 | 2 | 3 | 4 | 5 | range |
   |---|---|---|---|---|---|---|
   | fixed weights | 6.1 | 4.9 | 4.8 | 6.0 | 9.1 | U-shaped |
   | Model I | 1.9 | 2.6 | 3.9 | 5.6 | 9.5 | 4.2× |
   | + Model II-b | 0.7 | 1.5 | 3.0 | 4.0 | 5.1 | **7.3×** |
   | ethusd | 0.3 | 0.4 | 0.8 | 0.6 | 3.0 | 10× |

5. **Reference price: partly done, and it was not optional.** Levels are
   anchored to `p_ref` now, not to the moving touch, and `theta` moves `p_ref`
   onto an emptied best queue. Anchoring to the touch meant no order ever
   arrived *inside* the spread, so the spread could only ever widen — median 4
   ticks against ethusd's 1, at one tick 1% of the time against 91%, and
   `mdp_params` refused the process outright. With the anchor fixed: median
   spread 1 tick, 54% at one tick, and the process is usable.

   **Now done.** `theta_reinit` is implemented at 0.3 and the redraw from the
   invariant measure is `reinitialise()`, sampling Model I's closed-form
   stationary law per level by inverse CDF. `theta` was swept against the
   volatility and the mean-reversion ratio and stays at 1.0 for a measured
   reason: it governs how *often* the price moves, not whether successive moves
   are correlated, and lowering it makes eta monotonically worse. See the theta
   section below.

6. ~~**Order sizes are uniform on a range.**~~ **Done.** `FlowConfig::Qr` carries
   a seventeen-bin `order_size_aes` table normalised to a mean of one AES, drawn
   by `draw_size` for resting orders and for the market orders that consume
   them, and `reinitialise()` draws from it too. `qr_add` used to give every
   order exactly one AES, which capped every trade at one resting order: 12.4%
   of trades took exactly one order and 0.0% took more. See
   `docs/KNOWN-ISSUES.md` issue 7.

7. **The modelled queues sit at consecutive ticks, so the price can only move
   one tick at a time.** `queue_price(side, level) = p_ref -+ level` and
   `reference_price_step` moves `p_ref` by exactly one tick when a best queue
   empties. The real book is not shaped like that: of the ten prices nearest the
   touch, between one and two hold anything on any of the three captures, and
   when the touch changes the mid jumps a mean of 6.15 ticks (ethusd), 174.87
   (btcusd) or 3.15 (xrpusd).

   The consequence is quantified in `docs/KNOWN-ISSUES.md` issue 9: run this
   process on ethusd's own tick grid and it produces one twenty-fourth of
   ethusd's volatility, which the simulator's 1.0 bp tick has been concealing.
   Raising `kLevels` is not the fix and would make it worse -- the model already
   keeps more prices occupied near the touch than any instrument does. What is
   needed is a **spacing** distribution: the modelled queues have to be allowed
   to sit apart, so that clearing one moves the price by the gap.

   This is the largest remaining gap and it is a recalibration rather than a
   patch, because every rate in `FlowConfig::Qr` is fitted against the current
   one-tick spacing.

## Order of work

Estimator first (1), Model I next (2, 3) against the closed form. **Both done.**

Model II-a's market order routing and the measured market order size followed,
and between them they unblocked `level_ratio`. **All done.**

Model II-b followed, and the estimator now carries the opposite-queue regime.

Model II-b followed, then Model III's `theta`/`theta_reinit` (5) and the order
size distribution (6). **All done.**

Next, in order: the **spacing distribution** (7), which is what the volatility
scale turns on and what everything downstream of it is currently mis-stated by;
then the `Q_2` regime switch from Model II-a, which the estimator can be
extended to measure the same way.

## Refitting the informed parameters, and what that turned up

The Glosten-Milgrom overlay could not be refitted, because measuring first
showed the parameterisation is what fails. The quantity that decides it is the
**lift**: how much likelier the mid is to move in the next second after a print
than at a random moment.

| | P(move \| print) | P(move \| random) | lift |
|---|---|---|---|
| ethusd | 57.4% | 17.7% | 3.24 |
| btcusd | 68.6% | 19.6% | 3.50 |
| xrpusd | 81.0% | 41.0% | 1.98 |
| fixed-weight process | 62.5% | 56.2% | 1.11 |
| queue-reactive process | 28.9% | 29.3% | 0.99 |

A trade in the queue-reactive process carried no information at all. More
importantly, the fixed-weight process reads 1.11 — so `informed_impact_prob =
0.85`, fitted earlier against a measured 33% adverse selection and written up
as a success, **never produced information either**. It hit the target by
making the price move constantly, 56.2% unconditionally against ethusd's 17.7%.
Adverse selection can be bought with volatility, and that fit bought it.

The mechanical cause on this process is measured: the impact moves `p_ref`
whatever the book holds, so the price walks through resting liquidity. With it
on, 8.2% of states have orders on the wrong side of `p_ref`, against 0.02% with
it off. The observed touch stays put until those orders cancel away.

**What replaced it.** Real market orders are as small against the queue as ours
— a median of 0.067 AES against a touch of 4.29 — so the trade cannot be moving
the price by consuming it. What links the two is that a maker who has just been
hit *pulls*. So a print now excites cancellation on the side that was hit,
decaying over a time constant: the Hawkes component of arXiv:1901.08938 in its
simplest single-kernel form, and Glosten-Milgrom written in the queue-reactive
language rather than bolted beside it.

It only works with the base cancel rate compensated so the **mean** stays where
Model I put it. Without that the book equilibrates thinner and the process
simply becomes more volatile — the same bargain again, differently spelled.

**Fitted to the unconditional move rate, not to the lift:**

| gain | P(move \| print) | P(move \| random) | lift | adverse |
|---|---|---|---|---|
| 0 | 28.5% | 28.6% | 1.00 | 18.5% |
| 3 | 30.0% | 21.1% | 1.43 | 22.4% |
| **5** | **30.2%** | **15.7%** | **1.92** | **23.7%** |
| 8 | 27.2% | 7.0% | 3.90 | 22.2% |
| 12 | 39.7% | 0.0% | 1984 | 36.5% |
| ethusd | 57.4% | 17.7% | 3.24 | 48.1% |

Gain 8 lands the lift almost exactly and is still wrong: the numerator never
moves, and the lift rises only because the denominator collapses. By gain 12
the price does not move at all except after a trade. Matching one statistic by
breaking another is the mistake the old fit made facing the other way.

Five, because its unconditional move rate matches the instrument. The lift it
buys is 1.92 of a measured 3.24 — most of the way from "a fill says nothing" to
"a fill says something", and not all the way.

What the process looks like after it:

| | before | after | ethusd |
|---|---|---|---|
| spread at one tick | 72% | **84%** | 91% |
| mid moves per 100 ms | 5.5% | **3.4%** | 2.0% |
| volume one tick past the touch | 12.8% | **9.8%** | 7.2% |
| adverse selection | 19% | **24%** | 33% (8-hour) |
| imbalance, five buckets | 0.7–5.1 | **0.4–3.1** | 0.3–3.0 |

The imbalance profile now runs 0.4 / 0.9 / 1.8 / 2.4 / 3.1 against ethusd's
0.3 / 0.4 / 0.8 / 0.6 / 3.0 — the same level and nearly the same range.

## The Hawkes kernel, measured — and what it turned out not to fix

The kernel is directly observable: replay a capture, bin the lag since the most
recent print on that side, and accumulate both the cancels and the **time** spent
at each lag. The ratio to the unconditional rate is the kernel.

| lag, ms | 0–25 | 25–50 | 50–100 | 100–200 | 200–400 | 400–800 | 800+ |
|---|---|---|---|---|---|---|---|
| ethusd | **26.8** | 13.1 | 1.6\* | 10.1 | 2.0 | 1.4 | 0.9 |
| btcusd | **12.9** | 5.4 | 3.1 | 2.4 | 1.7 | 1.1 | ~0.9 |

\* two events; noise.

An order of magnitude above baseline inside 25 ms, back to baseline within a few
hundred. The first kernel assumed gain 5 with a one-second decay — twenty times
too weak and ten times too slow.

**A single exponential is the wrong family.** Weighted by event count the fit
lands on gain 19.3 / τ 145 ms for ethusd and gain 2.9 / τ 448 ms for btcusd, and
neither reproduces its own first bin. `rate − 1` falls by about half per doubling
of lag, so α ≈ 1: a power law, the shape Bacry and Muzy find for financial Hawkes
kernels. Reproducing it properly needs a sum of exponentials; one term is what is
implemented.

Trades excite trades far harder — 247× baseline in the first 25 ms on ethusd, 91×
on btcusd — but most of that is mechanical, not informational: one market order
sweeping several limits appears as several prints in quick succession, which the
paper says explicitly. Not modelled, for that reason.

**And the kernel is not what limits this process.** Sweeping it end to end:

| gain / τ | P(move \| print) | P(move \| random) | lift | adverse | **η** |
|---|---|---|---|---|---|
| 5 / 1.0 s | 30.2% | 15.7% | 1.92 | 23.7% | 0.39 |
| **19.3 / 0.145 s** | **29.2%** | **18.2%** | **1.61** | **22.1%** | **0.41** |
| 30 / 0.09 s | 26.4% | 17.4% | 1.51 | 19.8% | 0.40 |
| 60 / 0.05 s | 23.1% | 14.1% | 1.65 | 16.7% | 0.40 |
| ethusd | 57.4% | 17.7% | 3.24 | 48.1% | **0.84** |
| btcusd | 68.6% | 19.6% | 3.50 | — | **0.48** |

The lift barely moves across a twelvefold range of gain and a twentyfold range of
decay. What does not move **at all** is η, the Robert–Rosenbaum mean-reversion
ratio — continuations over twice the alternations — pinned at 0.40 in every
configuration against ethusd's 0.84. Below 0.5 the price alternates more than a
random walk: it moves and comes straight back, so a move caused by a print is
gone again before the one-second horizon the lift is measured over.

That is `theta`'s parameter, not the kernel's. The paper calibrates `theta` and
`theta_reinit` against the ten-minute volatility **and** η for exactly this
reason, and `theta` is still 1.0 here, unfitted.

Shipped at the ethusd fit — the kernel fitted to the mechanism it models rather
than to a downstream statistic. It also lands the process volatility closer than
the ad-hoc pair did, 18.2% against 17.7% where gain 5 gave 15.7%, and its lower
lift is bounded by mean reversion rather than by anything in the kernel.

## θ and θ_reinit, calibrated — and η is not θ's to fix

**θ does not control η.** Sweeping it moves η the *wrong* way:

| θ | η | sd/100 ms | lift | adverse | 1-tick spread |
|---|---|---|---|---|---|
| 1.0 | 0.41 | 0.185 | 1.61 | 22.1% | 81.5% |
| 0.5 | 0.38 | 0.179 | 1.51 | 20.2% | 83.1% |
| 0.25 | 0.37 | 0.167 | 1.61 | 19.3% | 85.4% |
| 0.10 | 0.30 | 0.148 | 1.53 | 16.2% | 88.0% |
| 0.03 | 0.21 | 0.134 | 1.58 | 12.9% | 90.2% |

Lower θ improves volatility and the spread and makes η monotonically worse. θ
governs how *often* the price moves, not whether successive moves are correlated,
so it was the wrong parameter for this and stays at 1.0.

**The η gap is real, not a sampling artefact.** Recomputing on the mid sampled
every k grid rows:

| | k=1 | k=2 | k=5 | k=10 | k=20 |
|---|---|---|---|---|---|
| ethusd | 0.84 | 0.88 | 0.97 | 0.83 | 0.62 |
| btcusd | 0.48 | 0.70 | 0.98 | 1.02 | 1.13 |
| this process | 0.41 | 0.42 | 0.44 | 0.46 | 0.49 |

Ours converges on 0.5 — a pure random walk — at every coarsening. The
instruments trend at every scale.

**θ_reinit is the half that was missing.** On a price move it redraws the book
from its invariant distribution rather than shifting it: "market participants
readjust very quickly their order flows around the new reference price". That
severs the queue that would have pulled the price back.

Written down before running it: this should move η from 0.40 toward 0.5 and
**not** to 0.84, because independent redraws make successive moves independent
and independence *is* η = 0.5. It held.

| θ_reinit | η | sd/100 ms | P(mv \| print) | P(mv \| random) | lift | adverse | 1-tick |
|---|---|---|---|---|---|---|---|
| 0.0 | 0.40 | 0.186 | 29.4% | 18.5% | 1.59 | 22.3% | 81.5% |
| 0.1 | 0.45 | 0.168 | 23.4% | 12.6% | 1.87 | 18.4% | 91.9% |
| **0.3** | **0.49** | **0.156** | **21.2%** | **9.9%** | **2.15** | **17.2%** | **95.7%** |
| 0.6 | 0.52 | 0.139 | 18.2% | 6.8% | 2.67 | 15.1% | 97.8% |
| 1.0 | 0.57 | 0.117 | 16.9% | 4.6% | 3.65 | 14.3% | 98.9% |
| ethusd | 0.84 | 0.110 | 57.4% | 17.7% | 3.24 | 48.1% | 91.0% |

η climbs to 0.57 and stops, a shade past the predicted 0.5 and nowhere near 0.84.

**The statistics disagree about where to set it**, so the rule matters. Volatility
wants 1.0, the spread wants 0.1, adverse selection wants 0, and the lift wants
1.0 — but the lift there is 3.65 against a measured 3.24 with P(move | random) at
4.6% against 17.7%, which is the denominator collapsing again.

Three tenths, on the rule that this parameter exists to remove mean reversion and
0.3 is where it has: η reaches the random walk. Below it the price flip-flops,
which no instrument does. Above it, η is bought by erasing the book's memory of
what just traded, and memory is what a market maker is paid for understanding.

**What reaching η = 0.84 actually needs.** Trade-sign autocorrelation:

| | n | k=1 | k=2 | k=5 | k=10 | k=20 |
|---|---|---|---|---|---|---|
| ethusd | 54 | 0.35 | 0.39 | 0.10 | 0.03 | −0.28 |
| btcusd | 220 | 0.56 | 0.48 | 0.26 | 0.18 | −0.03 |
| xrpusd | 153 | 0.59 | 0.36 | 0.16 | 0.12 | 0.06 |
| this process | 24,162 | 0.22 | 0.14 | 0.01 | 0.01 | −0.01 |

Real flow stays correlated for tens of trades; ours dies within five. That is
Lillo and Farmer's long memory, it is what makes a price trend, and it is a
mechanism this model does not have. Model II-b supplies the 0.22 at lag one —
trades favour a thick opposite queue and thickness persists — and nothing
supplies the tail.

## Long memory in the order flow — reproduced, and it does not fix η

Lillo, Mike and Farmer: a large trader does not take a position in one trade,
they work it over many child orders in the same direction. The sign sequence is
then a renewal process whose runs have the metaorder length distribution, and if
those are Pareto with exponent α the sign autocorrelation decays as a power law
with exponent α − 1.

So α is measurable from the decay rather than chosen. The sign autocorrelation
falls by 3.1× (btcusd) and 4.9× (xrpusd) between lag 1 and lag 10 — decay
exponents 0.49 and 0.69, so α ≈ 1.5 to 1.7. ethusd has 54 prints and cannot say.

| lag | 1 | 2 | 5 | 10 | 20 |
|---|---|---|---|---|---|
| off | 0.27 | 0.16 | 0.03 | −0.01 | −0.01 |
| α = 2.5 | 0.35 | 0.19 | 0.04 | 0.01 | −0.01 |
| **α = 1.6** | **0.62** | **0.47** | **0.28** | **0.16** | 0.08 |
| α = 1.2 | 0.81 | 0.72 | 0.59 | 0.50 | 0.43 |
| btcusd | 0.56 | 0.48 | 0.26 | 0.18 | −0.03 |
| ethusd | 0.35 | 0.39 | 0.10 | 0.03 | −0.28 |

α = 1.6 tracks btcusd at every lag, and it came from the decay rate rather than
from these numbers, so the whole autocorrelation function is a prediction the
mechanism got right.

**And the price still does not trend.** η is 0.49 with this off and 0.50 with it
on at either exponent — no movement at all, against 0.84 and 0.48 on the
instruments.

The reason is the rest of the chain. Persistent flow can only produce a
persistent price if trades move the price, and here they barely do: a trade is a
median 0.067 average events against a touch of 4.29, the queue empties by
cancellation far more often than by trading, and P(mid moves within a second |
print) is 21% against ethusd's 57%. **The gap is the price impact of a trade,
not the memory of the flow.** That is the next thing, and it was not visible
until the flow memory was in place and ruled out.

It costs a little adverse selection — 17.2% off against 13.4% on — because a
metaorder overrides which side is hit, and that was the side Model II-b chose
from the queue state. Kept anyway: the flow property is real, measured and
reproduced, and the statistic it costs is one already broken by the impact gap.

## Phase 5 on the calibrated process: the loop closes, and the test loses power

`backtest` and `evaluate` run `FlowConfig::ethusd_queue_reactive()` now. The
pipeline goes end to end — measure the process, estimate the MDP parameters,
solve, evaluate — so Phase 3 and Phase 5 are one loop rather than two phases.

The policy is healthy: it quotes both sides at the touch in 29.7% of states,
skews to touch/+1 in 51.4%, and pulls a side only at the inventory limit.

| strategy | session P&L | spread-cap | adv-select | passive | aggressive |
|---|---|---|---|---|---|
| JoinTouch | −698 | 6,216 | −404 | 2,211 | 0 |
| TabulatedMDP | −1,834 | 668 | −410 | 261 | 0 |
| ConstantSpread | −1,792 | 797 | −716 | 290 | 3 |
| GLFT | −1,605 | 794 | −743 | 286 | 3 |
| InventorySkew | −787,757 | −758,996 | 161,463 | 20 | **151,514** |
| AvellanedaStoikov | −818,188 | −690,193 | 192,311 | 24 | **136,294** |

```
TabulatedMDP minus JoinTouch, paired by seed:
  mean -1136.5   95% CI [-3871.9, +1515.7]   over 16 seeds, 6 of them positive
```

**A tie, where the fixed-weight process gave a clear loss** (−1,703, CI
[−2,118, −1,321], 1 of 24 positive). But read the interval before reading the
mean: ±2,700 around −1,136. The test has lost most of its power.

**Why.** JoinTouch captures 6,216 of spread and pays 404 of adverse selection —
+5,812 of trading edge — and still ends at −698. The difference is the closing
inventory marked at the closing mid, against a peak position of 59. With η ≈ 0.5
the price random-walks, so that mark is noise, and it is now large enough to
drown the comparison the test exists to make. **Every strategy loses money on
this process**, which is what a random walk plus a real spread should do to a
maker who carries inventory.

Two baselines are pathological and were not before: `InventorySkew` and
`AvellanedaStoikov` send 151,514 and 136,294 **aggressive** fills in 250,000
events. Their quotes cross a book whose spread is one tick 96% of the time. That
is a strategy-parameter problem exposed by a tighter process, not a solver
problem, and it is unfixed.

**Still open:** the price impact of a trade, which blocks η, adverse selection
and the lift together; the acceptance test's power, which needs either more
seeds or an inventory-neutral comparison; those two baselines; and the kernel's
power-law shape.

## Model II-a: what was implemented, and what level_ratio actually needed

**Market order routing — done.** A market order takes the best offer, which is
the first non-empty queue and is not always `Q_1`. The rate is the same function
of that queue's own size wherever the best offer is, which is the paper's
statement verbatim. Figure 2's much smaller value at `Q_2` is the
*unconditional* rate, averaged over all the time `Q_1` is occupied and no market
order can reach `Q_2` at all; reading it as a conditional rate would
double-count the emptiness.

**`level_ratio` was never a Model II-a problem.** The stated reason for doing
Model II-a was that no trade ever reached past the touch, so `level_ratio` came
out zero. Model II-a does not fix that and could not: a market order arriving at
`Q_2` when `Q_1` is empty is at the *best price*, so it is at the touch, not past
it. Only **size** gets past a queue, and every market order was exactly one AES
against a touch holding 4.29 of them.

Pooled over the three captures, 427 prints, market order size in AES:

| quantile | 0.25 | 0.50 | 0.75 | 0.90 | 0.99 | max |
|---|---|---|---|---|---|---|
| AES | 0.009 | 0.067 | 0.196 | 0.790 | 3.118 | 5.23 |

A typical market order is a fifteenth of an average event, and 8.4% are larger
than one. A constant AES is both too big most of the time and incapable of ever
being big. With the measured distribution sampled by inverse CDF — not a
lognormal, which matched the middle and then put the 99th percentile at 25 AES
against an observed 3.1 — volume reaching one tick past the touch went from
**0.0% to 13.1%**, against 7.2% on ethusd, 20.9% on xrpusd and 32.4% on btcusd.

The MDP now solves on this process: both sides quoted at the touch in 71.6% of
states, positive value at flat inventory, skewing at the limit, pulling both in
no state.

**The `Q_2` regime switch — measured, not yet implemented.** `q_1 = 0` is
observable without a reference price: it is the spread being wider than one
tick. Cancel and add rates at `Q_2` in the two regimes:

| | Q1 occupied | Q1 empty | ratio | time Q1 empty |
|---|---|---|---|---|
| ethusd cancels/s | 0.085 | 5.497 | 65× | 7.3% |
| btcusd cancels/s | 0.225 | 20.773 | 92× | 2.9% |
| xrpusd cancels/s | 0.069 | 0.211 | 3.1× | 61% |
| ethusd adds/s | 0.099 | 1.525 | 15× | |
| btcusd adds/s | 0.238 | 3.637 | 15× | |
| xrpusd adds/s | 0.039 | 0.065 | 1.7× | |

The paper's direction is confirmed on all three, and the split is the tick-size
regime this repository already works in: the effect is enormous on the two
large-tick instruments, where an empty `Q_1` is a rare transient the book is
repairing, and weak on xrpusd, where a wide spread is the normal state 61% of
the time.

It is **not implemented**, and the reason is a bookkeeping trap rather than
reluctance. The level-1 rates were fitted with no conditioning, so applying a
multiplier now would move the marginal away from what was fitted; it needs a
refit with the regime split. Worse, this probe and `QueueReactive` disagree by
2.7× on the same quantity — the probe's `Q_2` follows the reference price while
the estimator's level 1 is always touch-relative — so fitting from the probe
alone would be fitting two different definitions together. The estimator has to
carry the regime before this can be calibrated.

## A fourth defect, found while checking Model II-b's marginals

Every fitted rate was **twice** what it should have been, from the Model I fit
onward, and Model II-b only exposed it.

`QueueReactive::on_state` accrues exposure for *every* `(side, level)` on every
step, so exposure summed over both sides is twice the wall clock. The
aggregation that produced the targets divided that by two — which is the correct
wall clock, and the wrong denominator for a per-side rate. Count and exposure
have to be summed over the same set. Every target rate came out doubled and the
fit faithfully reproduced it.

The corrected ethusd targets, per side:

| level | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| adds/s | 0.449 | 0.121 | 0.095 | 0.095 |
| cancels/s | 0.669 | 0.116 | 0.084 | 0.091 |
| trades/s | 0.051 | — | — | — |

Nothing about the shapes moved. A birth-and-death queue's law depends only on
the arrival/departure ratios, so halving all four scales at a level halves its
event rate and leaves its distribution exactly where it was — mean q, P(q=0) and
the closed-form check are all unchanged. Only the clock was wrong.

Worth noting what did *not* catch it: the invariant-distribution test passes
either way, because it tests the shape. A rate is only wrong against something
outside the model, and the thing outside was the capture.

## Three defects found by auditing the Model I work, and what they cost

Worth keeping, because two of them looked like model findings.

**Orders the price walked away from never cancelled.** Levels are indexed from
`p_ref` and only `kLevels` exist, so a stray was invisible to every queue: never
counted, never eligible for cancellation, resting for ever. The book grew from
101 to 408 orders over two million events and was still climbing. The paper
never meets this because it simulates K queues with no book behind them. Fixed
by cancelling strays independently at `cancel_rate[K-1] / (1 + cancel_half[K-1])`
per order, derived from the fitted constants rather than added as a free one —
which is the paper's own K = 3 finding applied outward, that Q4 and Q5 behave
like Q3. The book now sits at 4 to 19 orders and is stable.

That leak was also producing a **non-monotone fill hazard** — 3.9e-4 alone
against 5.1e-4 with 240 ahead — which read like a model defect and was a wall of
stale orders that never traded. With it fixed the hazard is monotone and the
gradient is 7.0× (6.8e-3 / 3.2e-3 / 9.7e-4), against 2.8× for the fixed-weight
generator and 60× on a real book.

**`trade_rate` was never fitted.** It was the ratio of two numbers picked by
hand, carried through a fit that only constrained the total rate, and written up
as though it had been measured. Now fitted to the share of removals at the touch
that are trades: 7.08% measured, 7.09% reproduced. The correction was 2.8×.

## A property of Model I worth knowing before fitting to it

The three measured rates per level **cannot all be matched**. At stationarity a
birth-and-death queue's realised arrival rate equals its realised departure rate
— a theorem, not a modelling choice — while ethusd's touch measures 0.899 adds
per second against 1.338 cancels plus 0.102 trades. Trying to fit all three sent
the solver to a cancel scale of 37,652.

The gap is real and it is Model I's independence assumption failing: orders
arrive at the touch from other levels when the price moves, which the model
excludes by construction. What can be fitted is the stationary shape, the total
event rate, and how departures divide between cancels and trades, and that is
what the constants match.
