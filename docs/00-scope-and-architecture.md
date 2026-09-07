# Scope, Language Choice, and Architecture

## 0. What "optimizer" means here

Three things get conflated under that word. Separate them, because they live in
different places and have latency budgets that differ by **nine orders of magnitude**:

| Layer | Question | Time budget | Where it runs |
|---|---|---|---|
| **Calibration** | What are the arrival intensities, fill curves, impact coefficients, and their distributions? | minutes–hours | Offline, batch |
| **Policy solve** | Given that model, what bid/ask offsets and sizes maximise risk-adjusted P&L? | seconds–minutes | Offline, batch |
| **Execution** | Given the current book and my inventory, what do I send *right now*? | **100 ns – 10 µs** | Online, hot path |

The single most important architectural decision in this project: **the execution layer
never solves an optimisation problem.** It evaluates a precomputed policy — an array
lookup plus a handful of guards. Everything expensive happens offline and is compiled or
loaded as a table.

If you take one thing from this document, take that.

---

## 1. So — C++?

**Yes for the execution layer. No for the rest, and forcing it there will cost you months.**

The honest breakdown:

| Component | Language | Why |
|---|---|---|
| Feed handler (ITCH/SBE decode) | **C++20/23** | Byte-level parsing, zero-copy, no allocation, must keep up with 5M+ msg/s bursts |
| Order book (L3, MBO) | **C++20/23** | Millions of updates/sec, pointer-chasing is the bottleneck, cache layout *is* the design |
| Feature/state engine | **C++20/23** | Runs on every book update, must be branch-predictable |
| Quoting policy evaluation | **C++20/23** | Table lookup + guards; must be deterministic and bounded |
| Order gateway + risk | **C++20/23** | Pre-trade checks are on the critical path; hard real-time |
| **Simulator / backtester core** | **C++** (shared with live) | Must be *the same code* as live or the backtest is measuring a different system |
| Model calibration (Hawkes MLE, QR transition matrices, fill curves) | **Python** (+ C++ kernels where needed) | Iteration speed dominates; runs offline; scipy/statsmodels/torch already have it |
| Policy solve (HJB grid / MDP policy iteration / RL) | **Python** or **C++** | Offline. Use whichever you'll finish. Output is a table either way |
| Analysis, plotting, research notebooks | **Python** (polars/numpy/matplotlib) | Obviously |
| Build/orchestration | CMake + Python | — |

**The binding is the interesting part.** Expose the C++ book and feature engine to Python
via **nanobind** (or pybind11) so that research and production compute features with
*literally the same code*. Divergence between your research feature and your production
feature is the most common and most expensive bug class in this domain — it produces
backtests that cannot be reproduced live and nobody can explain why.

### What about Rust?

Rust is a genuinely reasonable choice for the hot path and gets you memory safety for
free. The reasons this project should still be C++:

- **Vendor and kernel-bypass APIs are C/C++**: Solarflare `ef_vi`, OnLoad, DPDK, OnixS,
  exchange-supplied SBE codecs, FPGA vendor SDKs. Rust bindings exist but are a
  maintenance tax on the thing you least want to maintain.
- **The reference literature and code are C++.** Every talk, book and open-source LOB in
  §N of the bibliography is C++.
- **Deterministic destruction + placement new + `std::pmr`** give you the arena/pool
  patterns you need with less friction than Rust's ownership model in a graph-shaped
  order book (intrusive lists with back-pointers fight the borrow checker).

If you were starting a greenfield *venue* rather than a *participant*, or if you had no
vendor SDK dependencies, Rust would be the better default. Note the honest cost accounting
in [Engineering Low-Latency Trading Systems in Rust](https://mechanicalsnail.com/posts/low-latency-trading-rust/):
lock-free and kernel-bypass code takes 5–10× longer to write in any language.

**Java** (with LMAX Disruptor, Aeron, Agrona, off-heap buffers and a tuned GC) is used in
production at real firms and gets to single-digit microseconds. It is not competitive
below ~1 µs, and you would be fighting the JIT and GC for the tail. Not for this.

**Do not write the hot path in Python.** Do write the entire research stack in it.

### Target latency, stated honestly

Set expectations by what you're actually building on:

| Setup | Realistic tick-to-trade |
|---|---|
| FPGA, wire-to-wire, no software in the path | ~20–100 ns |
| C++ + kernel bypass (ef_vi/DPDK), pinned, busy-polled, colocated | ~1–5 µs |
| C++ + `AF_XDP`/busy-poll sockets, tuned Linux | ~5–20 µs |
| C++ over a normal kernel network stack | ~20–100 µs |
| Cloud VM, crypto exchange over WebSocket | ~1–50 **ms** (dominated by the network, not you) |

[Aquilina, Budish & O'Neill (2022)](https://academic.oup.com/qje/article/137/1/493/6368348)
measured real latency races in UK equities from message-level exchange data (including
the *failed* snipes and cancels that conventional LOB data never shows) and found they
resolve on a **microsecond timescale**. That is the bar in equities. If you are
targeting crypto, your competition is at ~1 ms and the software engineering matters far
less than the colocation and the model — which is a perfectly good place to start, and
and the one you can actually reach on a normal machine with free data.

**For this project the relevant rows are the bottom two.** The microsecond rows require
colocation and hardware this project is not buying; they are listed so you know what the
latency literature in `01-literature.md` §G is describing, and so the simulator's latency
model is parameterised against realistic numbers rather than invented ones.

**Corollary:** build the measurement plane before the optimisation. You cannot claim a
latency improvement you cannot measure, and most "optimisations" in this space are noise
against an unmeasured baseline. See `03-metrics-and-estimators.md` §1–3.

---

## 2. System architecture

```
                    ┌─────────────── OFFLINE (Python + C++) ───────────────┐
                    │                                                       │
  PCAP / DBN /      │  ┌──────────────┐   ┌──────────────┐   ┌───────────┐ │
  ITCH capture ────────▶│ Calibration  │──▶│ Policy solve │──▶│  Policy   │ │
                    │  │ Hawkes / QR  │   │ HJB / MDP /  │   │  table    │ │
                    │  │ fill curves  │   │ RL           │   │ (binary)  │ │
                    │  │ impact, vol  │   └──────────────┘   └─────┬─────┘ │
                    │  └──────▲───────┘                            │       │
                    └─────────┼─────────────────────────────────── │ ──────┘
                              │ same C++ book/feature code          │ loaded at start
                    ┌─────────┴─────────────────────────────────────▼──────┐
                    │                 ONLINE (C++, pinned cores)           │
                    │                                                      │
   UDP multicast    │  ┌────────┐  ┌───────┐  ┌─────────┐  ┌────────────┐ │
   A/B feeds ──────────▶│  Feed  │─▶│ Book  │─▶│ Feature │─▶│  Quoting   │─┼──▶ Order
                    │  │ handler│  │builder│  │ engine  │  │  policy    │ │    gateway
                    │  └───┬────┘  └───┬───┘  └────┬────┘  └──────┬─────┘ │      │
                    │      │           │           │              │       │      ▼
                    │      │  SPSC ring buffers, one per hop      │       │   Risk /
                    │      │           │           │              │       │  throttle
                    │      ▼           ▼           ▼              ▼       │      │
                    │  ┌──────────────────────────────────────────────┐   │      │
                    │  │  Measurement plane: TSC stamps, HdrHistogram │◀──┼──────┘
                    │  │  journal (seq, ts_in, ts_out, state, action) │   │
                    │  └──────────────────────────────────────────────┘   │
                    └──────────────────────────────────────────────────────┘
                                         │
                                         ▼  replay the journal → bit-identical
                                    ┌──────────┐
                                    │Simulator │  same book + feature + policy binaries,
                                    │ (C++)    │  synthetic order flow + latency model
                                    └──────────┘
```

### Component contracts

**1. Feed handler.** Decodes MoldUDP64/ITCH or SBE/MDP-3.0 straight out of the receive
buffer. Responsibilities: sequence-gap detection, A/B line arbitration (take the first
copy of each sequence number, drop the dup), snapshot recovery, and stamping every
message with a receive TSC. No allocation, no branches on error paths that matter, no
exceptions. Output: fixed-size POD events into an SPSC ring.

**2. Book builder.** Maintains L3 (market-by-order) state:
- `order_id → {price_level*, qty, side, queue_prev, queue_next}` in an open-addressing
  hash map (`ankerl::unordered_dense` or a hand-rolled Robin Hood map). Never `std::unordered_map`.
- Price levels in a **flat array indexed by tick offset** from a moving reference price,
  with a bitset of occupied levels scanned via `__builtin_ctzll` to hop to the next level.
  This beats `std::map` by a large margin and is the standard trick.
- Each level holds an **intrusive doubly-linked FIFO** of orders (so cancels are O(1)).
- **Your own orders are tracked in that same FIFO** — that is how you get queue position
  for free, and queue position is the single highest-value number in the whole system.

Invariants to assert in debug builds: best bid < best ask; level quantity == sum of order
quantities; every order in the id-map is reachable from its level; total order count
matches. Property-test these against a naive reference implementation.

**3. Feature engine.** Computes, incrementally, per update: microprice, multi-level OFI,
queue imbalance, spread state, queue-ahead for own orders, short-horizon realised vol,
event-rate estimates. Incremental means *O(1) per event*, not "recompute over the book".
Every feature must be expressible as an update rule.

**4. Quoting policy.** `(inventory_bucket, imbalance_bucket, spread_state, vol_bucket, time_bucket)
→ (bid_offset_ticks, ask_offset_ticks, bid_size, ask_size)`. A dense array. Plus online
guards: inventory hard limits, max order rate, spread floor, kill-switch state, and a
"don't requote unless the target differs from the resting quote by ≥ 1 tick" hysteresis
(message budgets are real and requoting costs you queue position).

**5. Order gateway.** OUCH / iLink 3 / FIX / venue REST-or-WS. Pre-trade risk *before* the
wire, always: max position, max order value, max messages/sec, price collar, self-match
prevention, duplicate-clordid check.

**6. Measurement plane.** Not optional and not an afterthought — see §3 below.

### Threading and memory model

- **One thread per pinned, isolated core.** `isolcpus` + `nohz_full` + `rcu_nocbs`, IRQs
  moved off those cores, C-states disabled, governor `performance`.
- **Busy-poll, never block.** No condition variables, no mutexes, no `epoll` on the hot path.
- **SPSC ring buffers between stages** (`rigtorp::SPSCQueue` or your own), cache-line
  aligned to **128 bytes** (not 64 — adjacent-line prefetch on x86 and 128B lines on Apple
  silicon both make 64 insufficient).
- **Zero allocation after startup.** Pre-allocate every pool; `std::pmr::monotonic_buffer_resource`
  over a static arena; hugepages for the book and the pools.
- **No syscalls, no logging I/O, no `std::string`, no exceptions thrown, no RTTI on the hot path.**
  Logging goes through an async, thread-local-SPSC logger (Quill/NanoLog) where the
  frontend cost is ~6–10 ns and all formatting happens on a backend thread.
- **NUMA:** feed thread, book thread and NIC on the same socket. Cross-socket is ~100 ns
  you cannot afford and will not notice until you measure.

### Determinism is the feature

Every event gets `(sequence_number, ts_hw_recv, ts_app_recv, ts_decision, ts_send)`. The
journal is append-only. **Replaying the journal through the same binaries must produce
byte-identical outputs.** This buys you three things that are otherwise unobtainable:

1. A backtest that measures the system you actually run.
2. Post-mortem debugging of a live incident.
3. Meaningful A/B tests — replay the same day through policy A and policy B.

No floating-point non-determinism on the hot path: fixed-point prices in ticks (`int64`),
integer quantities, `-ffp-contract=off`, no `-ffast-math`, no reliance on FMA
contraction, no unordered reductions.

---

## 3. The measurement plane

You asked to measure "everything from time delays, distributions, latency". That is not a
feature of this system; **it is the system**. Concretely, three separate instruments:

1. **Latency instrumentation** (yours): TSC deltas at every stage boundary, recorded into
   per-stage HdrHistograms, dumped periodically. Wire timestamps from the NIC
   (`SO_TIMESTAMPING`) for the parts you don't control.
2. **Market-time instrumentation** (theirs): inter-arrival times, queue lifetimes,
   time-to-fill, time-to-cancel, exchange round-trip — the *market's* delay
   distributions, which are what you actually optimise against.
3. **Economic instrumentation**: markouts, spread capture, adverse selection, per-fill
   P&L attribution. Latency only matters through its effect on these.

All three are specified in `03-metrics-and-estimators.md`.

---

## 4. Two regimes, two code paths

From [Dayri & Rosenbaum](https://arxiv.org/abs/1207.6325) and
[arXiv:2410.08744](https://arxiv.org/abs/2410.08744) — classify each instrument first:

| | **Large tick** (spread ≈ 1 tick, deep queues) | **Small tick** (spread varies, thin queues) |
|---|---|---|
| Examples | ES/ZN futures, BTC perps, bank stocks | AMZN, GOOG, most high-priced equities |
| The game is | **Queue position** — get in early, stay, don't lose priority | **Price placement** — where to post, when to step ahead |
| Optimiser state | queue-ahead, queue-behind, imbalance | spread, depth profile, distance-from-mid |
| Key model | Fokker–Planck queue dynamics ([arXiv:1304.6819](https://arxiv.org/abs/1304.6819)) | Avellaneda–Stoikov / GLFT with `λ(δ)` |
| Cost of a requote | **Enormous** (you go to the back of the queue) | Small |
| Key measurement | queue position at fill, queue decay rate | fill intensity vs distance `A·e^{−kδ}` |

Build for one first. **Recommendation: start large-tick** — the state space is small
enough to tabulate exactly, the queue dynamics are measurable, and the literature
([arXiv:1806.05849](https://arxiv.org/abs/1806.05849), [arXiv:2603.24137](https://arxiv.org/abs/2603.24137))
is specifically about that case.

---

## 5. Proposed repository layout

```
├── CMakeLists.txt
├── cmake/                       # toolchain, sanitizer, PGO/BOLT presets
├── include/lob/                 # public headers (header-only where it helps inlining)
│   ├── core/                    # types.hpp (fixed-point Price, Qty), clock.hpp, arena.hpp
│   ├── book/                    # order_book.hpp, price_level.hpp, own_orders.hpp
│   ├── feed/                    # itch.hpp, sbe.hpp, mold.hpp, arbiter.hpp
│   ├── features/                # microprice, ofi, imbalance, queue_position, vol
│   ├── policy/                  # policy_table.hpp, guards.hpp, avellaneda_stoikov.hpp
│   ├── sim/                     # matching engine, latency model, order flow generators
│   └── measure/                 # tsc.hpp, histogram.hpp, journal.hpp, stopwatch.hpp
├── src/                         # .cpp for anything not header-only
├── apps/
│   ├── replay/                  # replay a capture through the book, emit stats
│   ├── backtest/                # simulator + policy, emit P&L attribution
│   ├── calibrate/               # heavy estimation kernels callable from Python
│   └── live/                    # the real thing (last)
├── bench/                       # google-benchmark + nanobench microbenchmarks
├── tests/                       # gtest/catch2 + property tests + decoder fuzzers
├── python/
│   ├── lobopt/                  # nanobind bindings to the C++ book & features
│   ├── calib/                   # Hawkes, queue-reactive, fill curves, impact
│   ├── policy/                  # HJB solver / MDP policy iteration / RL training
│   └── analysis/                # notebooks, stylized-fact scorecard, latency reports
├── data/                        # (gitignored) captures, DBN files, calibration output
└── docs/                        # these documents
```

---

## 6. What separates this from a toy

A weekend order book gets you a `std::map<Price, deque<Order>>` and a matching loop.
Everything below is what makes it an actual system, and each has a home above:

1. **Queue-position-aware fills.** A backtest that fills you whenever the price touches
   your limit will overstate P&L by a factor of several. You need to model *where you
   were in the queue* and *how much of the queue ahead of you was cancelled vs traded*.
2. **A latency model in the simulator.** Your quote takes time to arrive; the book moves
   meanwhile. Without this, the backtest is optimising a strategy that cannot exist.
   ([arXiv:2505.12465](https://arxiv.org/abs/2505.12465) on why most RL MM papers are unimplementable.)
3. **Adverse selection accounting.** Markouts at multiple horizons per fill, not aggregate P&L.
4. **Determinism and journaling** — §2 above.
5. **Distribution monitoring, not point estimates.** Every parameter you calibrate has a
   distribution and a stability-over-time question attached.
6. **Risk controls that are on the critical path**, not in a monitoring script.
7. **Honest statistics.** Fills are autocorrelated and clustered; naive t-stats on
   per-trade P&L are meaningless. Block bootstrap by day, minimum.
