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
- One feed decoder. Start with the simplest real data you have (crypto L2 diffs or a
  Databento MBO sample); add ITCH/SBE later behind the same event interface.
- `apps/replay`: stream a capture through the book, emit statistics.
- Property tests vs a naive reference book; decoder fuzzing; golden-replay hash.

**Done when**
- Replay of a full trading day of a liquid instrument with zero invariant violations.
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
- You can state the tick-size regime (large / medium / small) for each instrument you care
  about, with the metric that justifies it.

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
- Offline solver: value iteration on the discretised MDP (large-tick, following
  [arXiv:1806.05849](https://arxiv.org/abs/1806.05849)) or an HJB grid solve for
  small-tick, **with latency in the state/transition model**.
- Emit a **policy table** as a binary artefact with a schema version and a hash.
- Hot path: bounds-checked lookup + guards. No solving, no branching on model internals.
- Optional RL track as an alternative policy producer — same table interface, trained in
  the Phase 3 simulator, never on replayed data alone
  ([arXiv:2511.15262](https://arxiv.org/abs/2511.15262) on why).

**Done when**
- Policy evaluation is measured in nanoseconds and is deterministic.
- The tabulated policy beats the best Phase 4 baseline in the simulator, out of sample,
  with a bootstrap CI that excludes zero.
- You can point at a state and explain why the policy quotes what it quotes. A policy you
  can't interrogate is a policy you can't safely deploy.

---

## Phase 6 — The live path

**Build**
- Order gateway for one venue; pre-trade risk **on the critical path** (position limits,
  order value, message rate, price collar, self-match prevention, duplicate ClOrdID).
- Kill switch: automatic on inventory breach, message-rate breach, feed gap beyond
  threshold, or markout deterioration beyond a bound. Manual, one keystroke, always.
- Full journaling; live→replay determinism test.
- Paper trade, then trade minimum size on the cheapest venue you can get real fills on.

**Done when**
- Replaying a live session reproduces the live decisions bit-for-bit.
- Live measured tick-to-trade matches the simulated latency model within its CI.
- Live markouts match backtest markouts within the bootstrap CI. **If they don't, the
  simulator is wrong and Phase 3 isn't finished** — that comparison is the whole point of
  building the simulator and the live path against the same code.

---

## Ordering principles

1. **Measurement before optimisation**, always.
2. **The same code in backtest and live**, or the comparison in Phase 6 is meaningless.
3. **Beat the dumb baseline before adding a model.** Constant-spread quoting is
   surprisingly hard to beat once fees and adverse selection are honest.
4. **One instrument, one venue, end to end** before any breadth. Two half-finished venues
   teach you nothing.
5. **The fill model is where every backtest lies.** Spend the effort there, not on the
   twentieth feature.

## Deliberately out of scope for now

Multi-venue routing and cross-impact; options market making (different problem: greeks,
vol surface, quoting a whole chain); FPGA; colocation; anything requiring exchange
membership. Revisit once Phase 6 is genuinely working on one venue.
