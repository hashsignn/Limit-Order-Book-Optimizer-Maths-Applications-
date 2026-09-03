# Toolchain, Libraries and Open-Source Prior Art

Curated. Every entry is here because it does a specific job in the architecture in
`00-scope-and-architecture.md`. Nothing here is a dependency yet — this is the shortlist
to choose from.

---

## 1. Language and compiler

- **C++20 minimum, C++23 where the compiler allows.** Wanted: `std::span`, concepts,
  `[[likely]]/[[unlikely]]`, designated initialisers, `std::bit_cast`, `constexpr`
  everything, `std::pmr`.
- **GCC 13+ and Clang 17+.** Build with both — each catches different bugs, and their
  codegen differs enough on hot loops that you should benchmark both.
- Flags: `-O3 -march=native -fno-exceptions -fno-rtti` (hot path libs only),
  `-fno-omit-frame-pointer` (keep it — you need profiles),
  `-ffp-contract=off` (determinism), **never `-ffast-math`**.
- **LTO**, then **PGO**, then **BOLT**. In that order, and measure each. PGO on a
  representative replay is usually worth 5–15% on the decode+book path and costs you
  nothing at runtime.
- Sanitizers (ASan/UBSan/TSan) in a separate debug build that runs in CI. Never in the
  build you benchmark.

## 2. Core hot-path libraries

| Need | Pick | Notes |
|---|---|---|
| SPSC ring buffer | **[rigtorp/SPSCQueue](https://github.com/rigtorp/SPSCQueue)** | Wait-free, cache-aligned, hugepage-friendly. Faster than `boost::lockfree::spsc` and folly's. Erik Rigtorp's [blog](https://rigtorp.se/) is also required reading. |
| MPMC / other topologies | **[max0x7ba/atomic_queue](https://github.com/max0x7ba/atomic_queue)**, `moodycamel::ConcurrentQueue` | Benchmark against your own workload; the published numbers are for their benchmark, not yours. |
| Hash map (order-id → order) | **[ankerl::unordered_dense](https://github.com/martinus/unordered_dense)**, `absl::flat_hash_map`, or hand-rolled Robin Hood | Open addressing, contiguous storage. **Never `std::unordered_map`** — node-per-element is a cache miss per lookup. |
| Intrusive containers | `boost::intrusive` (list, set) | Orders live in a pool; the FIFO links are members. Zero allocation, O(1) unlink. |
| Memory | `std::pmr::monotonic_buffer_resource` over a static arena; explicit object pools with free-lists | Plus hugepages (`madvise(MADV_HUGEPAGE)` or `mmap(MAP_HUGETLB)`). |
| Fixed-point / decimal | Hand-rolled `int64` ticks + scale | Never `double` for prices. |
| Formatting | **[fmt](https://github.com/fmtlib/fmt)** | Off hot path. |
| Logging | **[Quill](https://github.com/odygrd/quill)** (thread-local SPSC frontend, backend formatting, ~6–10 ns frontend cost) or [NanoLog](https://github.com/iyengar111/nanolog) | The design constraint: **no formatting and no I/O on the producing thread.** |
| Histograms | **[HdrHistogram_c](https://github.com/HdrHistogram/HdrHistogram_c)** | Nanosecond-range, 3 significant digits, `.hgrm` output, mergeable across threads. |
| SBE codec | **[real-logic/simple-binary-encoding](https://github.com/real-logic/simple-binary-encoding)** | Generates zero-copy C++ decoders from the venue's XML schema. Use for CME MDP 3.0 and iLink 3. |
| Messaging (inter-process) | **[Aeron](https://github.com/real-logic/aeron)** | If you split the system across processes/hosts. Also the reference implementation of the ideas you'll want. |
| Linear algebra | **[Eigen](https://eigen.tuxfamily.org/)** | Offline/calibration only. |
| SIMD | [xsimd](https://github.com/xtensor-stack/xsimd) or [Google Highway](https://github.com/google/highway) | Portable intrinsics for the feature engine. |
| Python bindings | **[nanobind](https://github.com/wjakob/nanobind)** (or pybind11) | So research and production share book + feature code. |

## 3. Benchmarking and profiling

| Tool | Use |
|---|---|
| **[Google Benchmark](https://github.com/google/benchmark)** | The community standard for microbenchmarks. `DoNotOptimize`, `ClobberMemory`. |
| **[nanobench](https://github.com/martinus/nanobench)** | Lighter, gives you cycles/op, IPC and branch misses directly. Great for A/B on a single function. |
| `perf stat` | Cycles, IPC, cache misses, branch misses, stalled-cycles. First stop, always. |
| `perf c2c` | **False sharing detection.** Run it once on the whole system; it will find something. |
| `perf record` / `perf report` | Attribution. |
| **Intel VTune** | Microarchitecture analysis (top-down: front-end bound vs back-end bound vs retiring). |
| `llvm-mca` | Static scheduling analysis of a hot loop. |
| **[Coz](https://github.com/plasma-umass/coz)** | Causal profiling — tells you what would *actually* speed things up, not just what's hot. |
| `bpftrace` / eBPF | Kernel-side stalls, scheduler latency, softirq time. |
| `cyclictest` (rt-tests) | Baseline OS jitter on your isolated cores. Run before optimising anything — if the OS gives you 50 µs spikes, your code is not the problem. |

## 4. Networking — the ladder

Climb only as far as you need. Each rung is a big jump in complexity.

**Rungs 0–2 are free and are all this project needs.** Rungs 3–5 need specific hardware
and, in places, licences; they are listed so the latency numbers in the design docs have
context, not as things to buy.

| Rung | Cost | Approach | Typical userspace-visible latency |
|---|---|---|---|
| 0 | free | Standard sockets, `epoll` | 20–100 µs, terrible tail |
| 1 | free | `SO_BUSY_POLL` + `SO_REUSEPORT` + tuned kernel | 10–30 µs |
| 2 | free | **`AF_XDP` / XDP** | 5–15 µs, stays in-tree |
| 3 | needs a supported NIC | [DPDK](https://www.dpdk.org/) | 2–10 µs, full kernel bypass, you now own the driver |
| 4 | NIC + licence | Solarflare/AMD OpenOnload (`LD_PRELOAD`) or `ef_vi` (raw API) | 1–5 µs |
| 5 | FPGA hardware | Exablaze/Cisco Nexus SmartNIC, Xilinx/AMD Alveo | 20–100 ns wire-to-wire, but now you're writing HDL |

Also relevant: NVIDIA/Mellanox **VMA** (their Onload equivalent), and hardware
timestamping via `SO_TIMESTAMPING` on any of the above.

## 5. OS and hardware tuning

```
# Kernel cmdline — isolate the cores the hot path runs on
isolcpus=2-7 nohz_full=2-7 rcu_nocbs=2-7 intel_pstate=disable idle=poll
processor.max_cstate=1 intel_idle.max_cstate=0 mitigations=off   # mitigations=off: dev boxes only
```

- **IRQ affinity**: move all IRQs off the isolated cores (`/proc/irq/*/smp_affinity`),
  stop `irqbalance`.
- **Frequency**: governor `performance`; decide deliberately about turbo (variable clocks
  make cycle-counting lie — many shops disable turbo on trading cores).
- **Hugepages**: pre-allocate 1 GB pages for the book and the pools. **Disable transparent
  hugepages** (`never`) — THP compaction causes multi-millisecond stalls.
- **NUMA**: NIC, feed thread and book on the same socket. Verify with `numactl -H` and
  `lstopo`. Cross-socket access is a silent 100 ns tax.
- **Scheduling**: `SCHED_FIFO` via `chrt` for hot threads, or just rely on isolation.
- **`tuned-adm profile latency-performance`** as a starting baseline, then tune past it.
- Turn off: swap, `ksmd`, `khugepaged`, unnecessary daemons on isolated cores, and any
  monitoring agent that walks `/proc`.
- **Verify with `cyclictest` before and after.** Report the jitter distribution.

## 6. Timing infrastructure

- **[linuxptp](https://linuxptp.nwtime.org/)**: `ptp4l` (PTP domain) + `phc2sys` (PHC →
  system clock). Sub-microsecond to the grandmaster.
- A **GPS/PPS-disciplined** grandmaster if you're serious, or the venue's PTP feed if
  colocated.
- `chrony` (not `ntpd`) for anything that isn't PTP.
- Verify with `phc_ctl` and by measuring a known-constant path both directions.

## 7. Research stack (Python)

| Need | Pick |
|---|---|
| Dataframes at scale | **polars** (not pandas — the row counts here are 10⁸+), pyarrow |
| Numerics | numpy, scipy, numba for the loops you can't vectorise |
| **Hawkes processes** | **[tick](https://github.com/X-DataInitiative/tick)** (parametric + non-parametric kernels, MLE, simulation), or hand-rolled EM for state-dependent variants |
| Point-process diagnostics | statsmodels (KS, Ljung–Box), custom time-rescaling residuals |
| Heavy tails | `powerlaw` package (Clauset–Shalizi–Newman method) — but plot the Hill estimator yourself |
| Long memory | DFA implementations (`nolds`, or 30 lines yourself) |
| ML/RL | PyTorch, gymnasium; [stable-baselines3](https://github.com/DLR-RM/stable-baselines3) for baselines |
| Plotting | matplotlib (publication), plotly (interactive latency curves) |

## 8. Simulators and reference implementations worth reading

| Project | Why look at it |
|---|---|
| **[ABIDES](https://github.com/jpmorganchase/abides-jpmc-public)** ([arXiv:1904.12066](https://arxiv.org/abs/1904.12066)) | The academic standard multi-agent discrete-event market simulator. Python, so slow, but the *design* (agent kernel, latency model, message passing) is the right shape. Its explicit **latency model between agents and exchange** is exactly what your simulator needs. |
| **JAX-LOB** ([arXiv:2308.13289](https://arxiv.org/abs/2308.13289)) | GPU-vectorised LOB for training RL across thousands of parallel books. Read for how they made the book branch-free enough to vectorise. |
| **CoinTossX** ([arXiv:2102.10925](https://arxiv.org/abs/2102.10925)) | Open-source low-latency **matching engine**. Gives you a realistic exchange to trade against over a real protocol. |
| **[nautilus_trader](https://github.com/nautechsystems/nautilus_trader)** | Production-grade Rust+Python trading platform. Its issue tracker on [order-fill simulation](https://github.com/nautechsystems/nautilus_trader/issues/2194) and [L1/L2/L3 book design](https://github.com/nautechsystems/nautilus_trader/issues/199) is an unusually candid discussion of exactly the problems in this project. |
| Deterministic C++ LOB + Hawkes ([arXiv:2510.08085](https://arxiv.org/abs/2510.08085)) | Closest published architecture to this project — including the time-rescaling GOF diagnostics. |
| **[0burak/imperial_hft](https://github.com/0burak/imperial_hft)** ([arXiv:2309.04259](https://arxiv.org/abs/2309.04259)) | Benchmarked low-latency C++ patterns: cache warming, `constexpr`, loop unrolling, lock-free, short-circuiting, plus a C++ **Disruptor**. Statistically benchmarked, which is rarer than it should be. |
| [rigtorp.se — *Designing a High Performance Market Data Feed Handler*](https://rigtorp.se/2012/11/22/feed-handler.html) | Concrete ITCH 4.1 feed handler design and benchmarks. |
| ["How to build a fast limit order book"](https://gist.github.com/halfelf/db1ae032dc34278968f8bf31ee999a25) | The widely-cited O(1) add/cancel/execute design. Start here for the data structure. |
| [*The World's Fastest Matching Engine Algorithm*](https://arxiv.org/abs/2606.01183) | Sharded matcher design; useful contrast with single-threaded designs. |
| [ovasylenko.com — Order book data structures for matching engines](https://www.ovasylenko.com/blog/order-book-data-structures-matching-engine) | Clear comparison of layouts with complexity and memory analysis. |
| [electronictradinghub.com — LOB architecture and sequencing](https://electronictradinghub.com/limit-order-book-architecture-the-real-cost-of-getting-the-sequencing-wrong/) | On why sequencing, not the book, is usually the real bottleneck. |

**A note on the throughput numbers you'll see in these READMEs** (1M/s, 3.2M/s, 160M/s):
they are not comparable to each other. They measure different operations, on different
book depths, with different order-arrival patterns, on different hardware, and some
measure a pre-filled synthetic workload with no cancels. Treat every published figure as
a claim about *that* benchmark. Build your own harness on **replayed real market data**,
report the full percentile curve, and compare against yourself.

## 9. Testing strategy

- **Property tests on the book**: run the real book and a naive reference
  (`std::map<Price, std::list<Order>>`) over the same event stream and assert identical
  observable state after every event. This finds essentially all book bugs.
- **Fuzz the decoder** (libFuzzer/AFL++) with the venue's message grammar. Feed handlers
  parse hostile-shaped bytes at line rate; a truncated packet must not corrupt the book.
- **Golden replay**: a fixed capture + a committed hash of the resulting book states and
  order actions. Any change to the hash must be explained in the commit message.
- **Determinism test**: replay the same journal twice, diff the outputs bit-for-bit.
- **Latency regression in CI**: run the benchmark suite, compare p50/p99 against a
  committed baseline, fail on regression beyond a threshold. Noisy CI machines make this
  hard — pin to a dedicated runner or compare *cycles* and instruction counts rather than
  wall time.
- **Invariant assertions** compiled in debug, compiled out in release, and a separate
  "paranoid release" build that keeps them for staging.
