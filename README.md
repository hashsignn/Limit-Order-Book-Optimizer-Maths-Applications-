# Limit Order Book Optimizer — Maths & Applications

An industry-grade market-making limit order book optimizer: quote placement, queue
modelling, and a first-class measurement plane for time delays, distributions and
latency.

**Status: Phase 1 complete — the L3 order book.** The design documents are in
`docs/`; the code is the measurement plane (Phase 0) and the market-by-order book it
measures (Phase 1). See [`docs/05-roadmap.md`](docs/05-roadmap.md).

## Build and run

```bash
cmake --preset release && cmake --build build/release
ctest --preset release            # 5 suites, ~90 assertions

./build/release/latency_demo         # per-stage attribution + HDR percentile curve
./build/release/jitter_probe 10      # this machine's jitter floor
./build/release/bench_measure        # cost of each measurement primitive
./build/release/replay 1000000 --verify   # stream events through the book, check invariants
./build/release/bench_book           # cost of each book operation
./tools/jitter_baseline.sh 10        # full machine baseline, for docs/BASELINE.md
```

Presets: `release`, `debug`, `asan` (ASan+UBSan), `tsan`. Tests pass under all four,
on GCC 13 and Clang 18.

## What Phase 0 built

| Component | Header | What it is for |
|---|---|---|
| Fixed-point price/qty types | `lob/core/types.hpp` | Prices are integer ticks. Never floating point — a price that depends on rounding mode makes the backtest irreproducible. |
| Arena + object pool | `lob/core/arena.hpp` | Allocate once at startup; the hot path bumps a pointer or pops a free list. Exhaustion returns `nullptr` rather than silently falling back to the heap. |
| TSC clock | `lob/measure/tsc.hpp` | ~17 ns per read versus ~25 ns for `clock_gettime`. Calibrated against `CLOCK_MONOTONIC_RAW` at startup, and refuses to claim trustworthiness without `constant_tsc` + `nonstop_tsc`. |
| HDR histogram | `lob/measure/histogram.hpp` | Full latency distributions at constant relative precision in fixed memory, ~3.7 ns per record. Includes coordinated-omission correction. No third-party dependency. |
| Per-stage recorder | `lob/measure/recorder.hpp` | One histogram per pipeline stage, so a tick-to-trade figure can be attributed rather than merely observed. |
| Append-only journal | `lob/measure/journal.hpp` | Every decision written with its sequence number and timestamps. Replay must reproduce it byte for byte — that property is what makes a backtest measure the system you actually run. |

## What Phase 1 built

| Component | Header | What it is for |
|---|---|---|
| L3 order book | `lob/book/order_book.hpp` | Market-by-order. Flat price-level array indexed by tick offset, occupancy bitset scanned with `countr_zero`/`countl_zero`, intrusive FIFO per level, open-addressed order-id map. |
| Reference book | `lob/book/reference_book.hpp` | Deliberately naive `std::map` + `std::list`. Its only job is to be a second opinion in the differential test. |
| Order map | `lob/book/order_map.hpp` | Open addressing with backward-shift deletion, so a day of balanced adds and cancels does not accumulate tombstones. |
| Synthetic flow | `lob/sim/flow.hpp` | Zero-intelligence generator. Drives the book hard enough to prove it correct; becomes the Phase 3 simulator's interface once a calibrated model sits behind it. |

**Own orders live in the same FIFO as everyone else's.** That is the decision the project
turns on: queue position falls out of the structure rather than needing a parallel
bookkeeping system to be kept in sync. `queue_ahead()` is O(1) — maintained as the queue
drains, not recomputed.

Measured on this machine (see [`docs/BASELINE.md`](docs/BASELINE.md) for the full record
and its caveats):

```
measurement                          book
tsc::now (rdtsc)     16.72 ns   35.1 cy   add (deep book)        29.66 ns   62.3 cy
tsc::now_serialized  28.03 ns   58.9 cy   cancel (mid-queue)     45.09 ns   94.7 cy
Histogram::record     3.65 ns    7.7 cy   execute (partial)      21.96 ns   46.1 cy
Arena::allocate(64)   1.14 ns    2.4 cy   best_bid + best_ask     0.65 ns    1.4 cy
Pool acquire+release  0.72 ns    1.5 cy   depth(10) both sides   47.04 ns   98.8 cy
```

Mixed synthetic stream: **6.1 M events/s**, p50 36 ns per event. Top-of-book at 1.4 cycles
is the cached-touch design paying off — the feature engine reads it on every event.

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
| [`docs/BASELINE.md`](docs/BASELINE.md) | This machine's measured jitter floor and primitive costs — the numbers every later claim is relative to |

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
