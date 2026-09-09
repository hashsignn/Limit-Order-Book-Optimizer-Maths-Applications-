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
`1{q_1 > 0}` — whether the queue in front is empty.

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

2. **`FlowGenerator` intensities do not depend on the book at all.** Weights are
   fixed and a removal picks a uniformly random resting order, which is why
   cancel intensity comes out proportional to the order count for a reason that
   has nothing to do with reactivity (KNOWN-ISSUES 3).

3. **Market order intensity has the wrong sign in `q`.** Measured `+0.90` in the
   generator against `-0.28` on ethusd and exponentially decreasing in the
   paper. A thicker queue should trade *less*, not more.

4. **No dependence on the opposite queue**, so no imbalance signal. Model II-b
   is where it comes from.

5. **No reference price separate from the mid.** Our generator walks `mid_`
   directly on a coin flip and an informed-trade impact. Model III makes the
   price move a *consequence* of a queue emptying, which is the mechanism that
   ties adverse selection to order flow.

6. **Order sizes are uniform on a range.** The paper assumes one constant size
   per limit (the AES); arXiv:2405.18594 extends it to state-dependent size
   distributions and reports it matters.

## Order of work

Estimator first (1), because nothing below can be fitted without it. Then
Model I (2, 3) and check it against the closed-form invariant distribution.
Then Model II-b (4) for the imbalance signal. Then Model III (5), calibrating
`theta` and `theta_reinit` to our captures' 10-minute volatility and
mean-reversion ratio. Sizes (6) last.
