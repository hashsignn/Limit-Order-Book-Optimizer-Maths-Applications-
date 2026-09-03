# Machine Baseline

The Phase 0 acceptance criterion: **record this machine's jitter floor before
attributing any latency improvement to your own code.** If the OS interrupts you
for 700 µs, no amount of cache-line tuning matters.

Re-run after any tuning change and append a new section. Do not delete old
sections — the point is the comparison.

Regenerate with:

```bash
cmake --preset release && cmake --build build/release
./tools/jitter_baseline.sh 10
```

---

## 2026-09-03 — cloud VM (development container)

Intel Xeon @ 2.10 GHz, 4 cores, Linux 6.18, GCC 13.3 / Clang 18.1.
No core isolation, no IRQ affinity, shared virtualised host.

### TSC

| | |
|---|---|
| Calibrated frequency | 2.1000 GHz |
| `constant_tsc` | yes |
| `nonstop_tsc` | yes |
| Trustworthy | yes |

### Primitive costs

| Operation | ns/op | cycles/op |
|---|---|---|
| `tsc::now` (rdtsc, unserialised) | 16.72 | 35.1 |
| `tsc::now_serialized` (rdtscp+lfence) | 28.03 | 58.9 |
| `Histogram::record` | 3.65 | 7.7 |
| `Arena::allocate(64)` | 1.14 | 2.4 |
| `Pool` acquire+release | 0.72 | 1.5 |

**Read this before trusting any stage timing.** A `ScopedTimer` brackets its
scope with two serialised reads, so it adds roughly **56 ns** to whatever it
measures. Timing anything faster than a few hundred nanoseconds this way
measures the timer. For those, time a loop and divide.

### Jitter floor — `jitter_probe`, 3 s, 71.8 M samples

| Percentile | Gap between consecutive clock reads |
|---|---|
| min | 28 ns |
| p50 | 41 ns |
| p90 | 41 ns |
| p99 | 48 ns |
| p99.9 | 53 ns |
| **p99.99** | **2 977 ns** |
| **max** | **723 803 ns** |

The median is the cost of the read itself. Everything above p99.9 is the
machine interrupting us: scheduler preemption, interrupts, and — on a shared
virtualised host — steal time from other tenants.

**The 723 µs maximum is the number that matters.** It is the floor under every
latency figure this machine can produce. Any tail measurement here at or below
about a millisecond is measuring the hypervisor, not the code.

### Consequences for this project

- **Tail numbers from this container are not meaningful.** Medians and cycle
  counts are; p99.9 and above are dominated by the host.
- That is acceptable, because this is a **model, not a trading system** (see
  `docs/05-roadmap.md`). Correctness, throughput and algorithmic cost are what
  the measurement plane needs to establish here.
- Anyone reproducing this on a tuned bare-metal box should expect a jitter floor
  two to three orders of magnitude lower, and should record their own section
  below rather than trusting these numbers.

### Not applied here

Core isolation (`isolcpus`, `nohz_full`, `rcu_nocbs`), IRQ affinity, C-state and
frequency pinning, hugepages, THP disabling. All are documented in
`docs/04-toolchain.md` §5 and all are free — they simply cannot be applied
inside this container. On a machine where they can be, apply them and re-run:
the difference between the two sections is the value of the tuning.
