# Limit Order Book Optimizer — Maths & Applications

An industry-grade market-making limit order book optimizer: quote placement, queue
modelling, and a first-class measurement plane for time delays, distributions and
latency.

**Status: research and design phase.** This repository currently contains the literature
survey, data/protocol analysis, measurement specification, and architecture that the
implementation will be built against. No code yet — by design; see
[`docs/05-roadmap.md`](docs/05-roadmap.md) for why the measurement plane is Phase 0.

> ### Scope: this is a model, not a trading operation
>
> The goal is a working, measurable simulation of market making — order book, queue
> dynamics, fill modelling, quote optimisation. **No live trading, no real capital, no
> paid data, no paid infrastructure.** Every phase below can be completed with free data
> and a normal machine.
>
> Where the literature or the engineering assumes a funded desk — colocation, kernel-bypass
> NICs, exchange entitlements, real fills — those are recorded as context for what the
> models are describing, and explicitly marked as out of scope. The final phase is
> **shadow mode only**: the strategy runs against a live public feed and logs what it
> *would* have done. Nothing connects to an order entry endpoint.

---

## The documents

| Doc | Contents |
|---|---|
| [`docs/00-scope-and-architecture.md`](docs/00-scope-and-architecture.md) | The three-layer split (calibration / policy solve / execution), **the language decision and why**, system architecture, threading and memory model, determinism, large-tick vs small-tick regimes, proposed repo layout |
| [`docs/01-literature.md`](docs/01-literature.md) | Annotated bibliography — ~90 papers and books, grouped by the role each plays in the system, with a suggested reading order |
| [`docs/02-data-and-protocols.md`](docs/02-data-and-protocols.md) | Why L3/MBO is a hard requirement, ITCH / MDP 3.0 / SBE / OUCH / iLink, where to get data, PCAP and the timestamp hierarchy, storage, reference data |
| [`docs/03-metrics-and-estimators.md`](docs/03-metrics-and-estimators.md) | **The measurement specification.** Clocks, latency taxonomy, how to measure latency without fooling yourself, the market's delay distributions, queue metrics, markouts, distribution fitting and goodness-of-fit, P&L attribution, statistical validity |
| [`docs/04-toolchain.md`](docs/04-toolchain.md) | C++ libraries, kernel bypass ladder, OS tuning, timing infrastructure, profiling, Python research stack, open-source prior art worth reading |
| [`docs/05-roadmap.md`](docs/05-roadmap.md) | Seven phases, each with explicit "done when" criteria |

## The short version

**Language.** C++20/23 for the execution layer — feed handler, order book, feature engine,
policy evaluation, order gateway, *and the simulator core*. Python for calibration, policy
solving and analysis. The C++ book and feature engine are exposed to Python via nanobind
so research and production compute the same numbers from the same code. Rust is a
defensible alternative for the hot path; C++ wins on vendor SDKs (`ef_vi`, DPDK, exchange
SBE codecs) and on the fact that all the reference material is C++.

**The key architectural decision.** The execution layer never solves an optimisation
problem. The HJB/MDP solution is computed offline and shipped as a lookup table; online,
quoting is an array index plus guards. Nanoseconds, deterministic, interrogable.

**What makes it "industry level"** rather than a weekend order book:

- queue-position-aware fills (a backtest that fills you whenever price touches your limit
  overstates P&L by a large factor),
- a latency model inside the simulator, in both directions,
- per-fill adverse-selection accounting via markouts at multiple horizons,
- bit-for-bit determinism between live and replay,
- distributions and goodness-of-fit tests, not point estimates,
- risk controls on the critical path, not in a monitoring script.

**Two regimes, two code paths.** Large-tick instruments (spread pinned at one tick, deep
queues) are a queue-position game. Small-tick instruments are a price-placement game. They
need different state variables and different optimisers. Start large-tick.

## Where to start reading

1. [`docs/00-scope-and-architecture.md`](docs/00-scope-and-architecture.md) §1 — the language question, answered.
2. [`docs/03-metrics-and-estimators.md`](docs/03-metrics-and-estimators.md) — what "measure everything" actually means.
3. [`docs/01-literature.md`](docs/01-literature.md) §O — the reading order.

---

## License

See [`All Rights Reserved`](./All%20Rights%20Reserved). Copyright (c) 2026 Harjot Singh.
