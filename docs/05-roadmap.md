# Build Roadmap

Ordered so that **every phase produces something measurable**, and so that the expensive,
uncertain work (the optimiser) happens only after the cheap, certain work (the book, the
measurement plane, the simulator) is trustworthy.

The recurring failure mode in this domain is building the strategy first and the
measurement last. Then you can't tell whether a change helped.

---

## Phase 0 — Foundations *(scaffolding, no market logic)*

**Build**
- CMake project, C++20, GCC + Clang, Release/Debug/ASan/TSan presets, CI.
- `core/`: fixed-point `Price` (int64 ticks), `Qty`, `Timestamp`, `Side`, POD event structs.
- `measure/`: TSC clock with startup calibration, `Stopwatch`, HdrHistogram wrapper,
  per-stage `LatencyRecorder`, append-only binary journal.
- `bench/` harness with Google Benchmark + nanobench.
- Arena allocator + object pool.

**Done when**
- You can measure a function at nanosecond resolution and print an HDR percentile curve.
- `cyclictest` baseline for your machine is recorded in the repo — you know your OS jitter
  floor before you attribute anything to your own code.

> Deliberately first. You cannot claim an optimisation you cannot measure, and the
> measurement plane is the product feature the user actually asked for.

---

## Phase 1 — The book

**Build**
- L3 order book: flat price-level array indexed by tick offset + occupancy bitset,
  intrusive FIFO per level, open-addressed `order_id → order*` map, own-order tracking
  in the same FIFO.
- One feed decoder. Bitstamp `live_orders` + `live_trades` — free, no account, genuine
  L3 over the whole book. Add ITCH/SBE later behind the same `BookEvent` interface.
- `apps/replay`: stream a capture through the book, emit statistics.
- Property tests vs a naive reference book; decoder fuzzing; golden-replay hash.

**Done when**
- ✅ Replay of a full session of a liquid instrument with zero invariant violations, over a
  capture whose `event_id` chain is unbroken — so "no violations" means the book handled
  every message, not that messages went missing.
  *Met 2026-09-04: three 10-minute Bitstamp L3 sessions (btcusd, ethusd, xrpusd), 155k
  messages, 124k book events, zero rejects, zero invariant violations, all three chains
  unbroken. They are committed in `data/samples/` and replayed by `ctest -R capture` on
  every build.*
- Published percentile curve for per-event book update latency, on real data, warm and cold.
- Throughput number with the caveats stated (which instrument, which day, which mix of
  add/cancel/execute).

---

## Phase 2 — The measurement plane

**Build**
- Feature engine: microprice, multi-level OFI, imbalance, spread state, queue-ahead for
  own orders, short-horizon realised vol — all incremental, O(1) per event.
- nanobind bindings so Python computes features with the same C++ code.
- Python analysis package producing everything in `03-metrics-and-estimators.md` §4–§9:
  duration distributions, TTF/TTC, queue decay decomposition, fill-probability surfaces,
  markout curves, tail-exponent estimates, volatility signature plots.
- The **stylized-fact scorecard** as an automated report.

**Done when**
- You can produce, for any instrument-day: the full set of distributions, with fits and
  goodness-of-fit diagnostics, from one command.
- You've found the **exchange round-trip mode** in the inter-arrival distribution
  ([arXiv:2603.24137](https://arxiv.org/abs/2603.24137)) for at least one venue.
- ✅ You can state the tick-size regime (large / medium / small) for each instrument you care
  about, with the metric that justifies it.
  *The spread sits at one tick 99% / 92% / 70% of the time on btcusd / ethusd / xrpusd
  (time-weighted, 100 ms grid). Large-tick. Confirmed independently by the A/k fit, which is
  not identified on two of the three because δ has nothing to vary over — see
  `tools/calibrate.py`.*

> This phase is the deliverable the project is named after. Everything after it is
> strategy; this is the instrument.

---

## Phase 3 — The simulator

**Build**
- Matching engine with correct **price-time priority** (and pro-rata as a separate policy
  if you touch STIR futures).
- Order flow generators, in increasing fidelity:
  1. Poisson / zero-intelligence (the baseline you must beat),
  2. **Queue-reactive** ([arXiv:1312.0563](https://arxiv.org/abs/1312.0563)) — state-dependent intensities,
  3. **Queue-reactive + Hawkes** ([arXiv:1901.08938](https://arxiv.org/abs/1901.08938)) — adds clustering,
  4. Optional: size-aware QR ([arXiv:2405.18594](https://arxiv.org/abs/2405.18594)) or MDQR ([arXiv:2501.08822](https://arxiv.org/abs/2501.08822)).
- **A latency model**: decision→arrival delay drawn from your measured distribution,
  applied to every message, in both directions. Without this the simulator is optimising
  a strategy that cannot exist.
- **Flow feedback**: your orders change subsequent flow, at least crudely
  ([arXiv:2603.24137](https://arxiv.org/abs/2603.24137)).
- Calibration in Python (Hawkes MLE with time-rescaling GOF; QR transition estimation).

**Done when**
- The stylized-fact scorecard is green against real data for the target instrument.
- Turning the latency model on measurably changes backtest P&L — and you can explain the
  magnitude.

---

## Phase 4 — Baseline strategies

**Build**, in order, each backtested against Phase 3:
1. **Constant spread**, symmetric. The null hypothesis.
2. **Inventory-skewed** — Ho–Stoll reservation price.
3. **Avellaneda–Stoikov** closed form.
4. **GLFT** ([arXiv:1105.3115](https://arxiv.org/abs/1105.3115)) — the practical A–S, with inventory bounds.
5. **Imbalance/microprice-skewed** quoting on top of (4).
6. Requote hysteresis and queue-position-aware requoting.

**Done when**
- Five-line P&L attribution for each, with bootstrap CIs.
- A written answer to: *does each added component actually pay for itself, and by how much,
  and is the difference statistically distinguishable given the number of fills?*
- Ideally, reproduction of the fill-probability / post-fill-return trade-off from
  [arXiv:2502.18625](https://arxiv.org/abs/2502.18625) in your own data. If you can't
  reproduce it, your fill model is wrong.

---

## Phase 5 — The optimiser

**Build**
- Offline solver on the discretised MDP (large-tick, following
  [arXiv:1806.05849](https://arxiv.org/abs/1806.05849)) or an HJB grid solve for
  small-tick, **with latency in the state/transition model**.
  *Built as modified policy iteration (Puterman §6.5) rather than plain value
  iteration: a one-second horizon on a half-millisecond epoch is a per-epoch
  discount of 0.9995 and needs ~35,000 backups, which value iteration could not
  reach inside any sane sweep budget. Latency is still NOT in the model.*
- Emit a **policy table** as a binary artefact with a schema version and a hash.
- Hot path: bounds-checked lookup + guards. No solving, no branching on model internals.
- Optional RL track as an alternative policy producer — same table interface, trained in
  the Phase 3 simulator, never on replayed data alone
  ([arXiv:2511.15262](https://arxiv.org/abs/2511.15262) on why).

**Done when**
- ✅ Policy evaluation is measured in nanoseconds and is deterministic.
  *0.6 ns per lookup, random access across the whole table; the solve is
  bit-identical on a re-run and the artefact carries a hash of the parameters
  that produced it.*
- ❌ The tabulated policy beats the best Phase 4 baseline in the simulator, out of sample,
  with a bootstrap CI that excludes zero.
  *Run and NOT met — but the failure changed shape completely. It now **ties** the best
  baseline: **−30.3 against JoinTouch, CI [−118.9, +52.2], 11 of 24 seeds positive**,
  where it previously lost by **−1,055.9, CI [−1,212.8, −897.1], 0 of 24**. It takes 121
  passive fills to JoinTouch's 128; it used to take 7 to its 381. The criterion asks for
  an interval excluding zero and this one spans it, so the answer is still no.*

  *What was wrong was four separate things, none of them the solver:*

  *1. **Queue position was bucketed by rank, not volume.** The state said which quartile
  of the market's own resting orders our queue position fell in. Fill hazard depends on
  how much size must trade before the queue reaches you — an absolute quantity — and a
  market maker re-quotes the moment a level clears, so its orders sit at small absolute
  queues far more often than the book's own do. `evaluate --probe` measured our own
  placements against the model: **1,019 fills/s at the front where the model said 52**.
  Buckets are now fractions of a reference depth carried in the table header; realised and
  modelled hazard agree within 3× across the range.*

  *2. **The level ratio double-counted promotion.** The fill rate one tick behind the touch
  was measured from where orders were PLACED, so an order placed behind, promoted when the
  touch came to it, and filled at the front counted as a fill one tick behind. The MDP
  already models promotion as its own transition, so it paid for the same event twice.
  Measured instead from each print's distance past the touch at the moment it happened,
  the share of volume reaching one tick past the touch is **0.2%, not 42%** — and quoting
  behind the touch, which the old policy did at flat inventory, stops being attractive.*

  *3. **The two sides' fills were mutually exclusive.** "Neither side filled" was computed
  as 1 − p_bid − p_ask. Calibrated on real numbers those are 0.92 each, the remainder goes
  negative, the impossible branch was dropped, and the surviving probabilities summed to
  **1.8**. A transition function that creates probability is not a contraction: value
  iteration diverged to a residual of **2e+57** and reported it as "did not converge",
  which reads like it needed more sweeps. The sides are independent now, so both filling
  in one epoch — the market maker's whole business — is a state the model can represent.
  `stochastic()` checks the successors sum to one before any iteration happens.*

  *4. **Three preferences were expressed per epoch.** The inventory penalty, the discount
  and the executor's message budget each meant something different depending on a decision
  grid set in another file. Matching the model's epoch to the executor's actual cadence
  (0.5 ms, measured, not assumed) silently doubled the risk aversion and made the policy
  worse. The penalty is now per second and the discount is a horizon in seconds; both
  travel with the table, and `evaluate` warns when the executor's cadence and the table's
  epoch disagree.*

  *What remains is not a bug. On this generator the mid is a martingale, so inventory
  carries variance but no expected cost, and the criterion is measured on the MEAN. Every
  amount of risk aversion is therefore a pure drag: sweeping it on a held-out seed family
  gives −128 at 40 ticks/lot²/s, −68 at 2.5, and +1.4 at zero, monotone throughout. At
  zero the solved policy quotes both sides at the touch in 73% of states — it has largely
  rediscovered JoinTouch — and the 9% where it quotes behind for adverse-selection reasons
  is where the remaining gap lives. Beating a touch-joiner needs a process where the
  optimiser has something to optimise: more informed flow, a real spread distribution, or
  a criterion that prices the risk it actually avoids. The policy holds **half the peak
  inventory** for the same P&L at a moderate penalty, and nothing in the criterion sees
  that.*

  *The solver is also 14× faster — modified policy iteration rather than plain value
  iteration, because a one-second horizon on a half-millisecond epoch is a per-epoch
  discount of 0.9995 and needs 35,000 backups, where the old 20,000-sweep cap stopped at
  2e-6 and shipped nothing.*

- You can point at a state and explain why the policy quotes what it quotes. A policy you
  can't interrogate is a policy you can't safely deploy.

---

### Why Phase 5's criterion currently returns no signal

Not merely unmet — uninformative, and the reason is worth stating where the
criterion is.

Dumping `policy/ethusd.bin` over all 14,080 states: **97.8% of the solved policy
is `JoinTouch`'s rule exactly** — quote both sides at the touch, pull one side at
the position limit. The two 1,280-state blocks that pull a side are precisely one
whole inventory level each. Only 304 states (2.2%) differ, all of them "quote one
side a tick behind", and reaching one needs `|inventory| >= 3` **while alone at
the touch**. A 60,000-event run never gets there, which is why the comparison
against `JoinTouch` returned a mean of exactly +0.0 with a confidence interval of
[+0.0, +0.0]. At 250,000 events over 6 seeds it does break, by 3.4 ticks on one
extra requote out of 4,491.

So value iteration on this process converges to join-the-touch, and the criterion
compares a policy against a baseline it reproduced. The open question is whether
that is the truth about large-tick market making — where the decision is queue
position rather than price, which is the premise the whole state design rests on
— or an artefact of a state space too coarse to express anything else. The 2.2%
of states that do differ are where to look. See `docs/KNOWN-ISSUES.md` 6.

---

## Phase 6 — Shadow mode

**No orders are sent to any venue in this phase, or in this project.** The point is to run
the full pipeline against a live public feed and record what it *would* have done, so the
simulator can be validated against real-time conditions rather than replayed history.

**Build**
- Connect the feed handler to a free live public feed (a crypto venue's public WebSocket
  is the obvious choice — no account, no entitlement, no credentials).
- Run book → features → policy end to end in real time. **Emit intended orders to the
  journal only.** There is deliberately no order gateway and no venue credentials
  anywhere in the codebase.
- Shadow fill estimation: when the public tape shows a trade at or through your intended
  quote, record a *candidate* fill and mark it with the uncertainty (see the caveat below).
- Full journaling; live→replay determinism test.
- The risk logic from the design docs is still worth building — position limits, message
  budgets, kill switch — because it is part of what a correct system looks like and it
  constrains the simulator. It simply has nothing live to gate.

**Done when**
- Replaying a shadow session reproduces the decisions bit-for-bit.
- Measured end-to-end processing latency under live event rates matches the simulator's
  latency model within its CI — and the system keeps up during bursts without the ring
  buffers backing up.
- Shadow-estimated fill rates match the simulator's predicted fill rates within the
  bootstrap CI. **If they don't, the simulator is wrong and Phase 3 isn't finished.**

**The honest caveat.** Shadow mode cannot measure queue position, because you never joined
the queue. You can bound it — assume you are last in the queue for a pessimistic estimate,
first for an optimistic one — and the gap between those two bounds is itself a useful
measurement of how much queue position matters for that instrument. What shadow mode
*cannot* tell you is your true fill rate or your true adverse selection. Treat those as
modelled, never as measured, and say so in any result you write up.

---

## Ordering principles

1. **Measurement before optimisation**, always.
2. **The same code in backtest and shadow mode**, or the comparison in Phase 6 is meaningless.
3. **Beat the dumb baseline before adding a model.** Constant-spread quoting is
   surprisingly hard to beat once fees and adverse selection are honest.
4. **One instrument, one venue, end to end** before any breadth. Two half-finished venues
   teach you nothing.
5. **The fill model is where every backtest lies.** Spend the effort there, not on the
   twentieth feature.

## Permanently out of scope

**Sending real orders, in any form, on any venue.** No order gateway, no API keys, no
credentials, no paper-trading account that could be switched to live by changing a flag.
The deliverable is a model and its measurements.

Also out of scope, because each costs money: paid market data subscriptions and exchange
entitlements; colocation; kernel-bypass NICs and their licences; FPGA hardware; PTP
grandmaster clocks. These appear in the design docs because the literature and the
engineering practice assume them, and you should understand what the models describe — but
nothing in the build plan requires buying any of it.

Out of scope for now on complexity grounds: multi-venue routing and cross-impact; options
market making (a different problem — greeks, vol surface, quoting a whole chain).
