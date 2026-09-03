# The Measurement Plane: Delays, Distributions, Latency

This is the specification for "measure everything". It is organised as: **clocks →
your latency → the market's delays → queues → quotes → adverse selection →
distributions → statistical validity → P&L attribution**.

Rule for the whole document: **report distributions, not means.** A mean latency, a mean
time-to-fill, a mean markout — each of these is at best uninformative and at worst
actively misleading, because every distribution here is heavy-tailed and the tail is
where the money is.

---

## 1. Clocks — pick the right one for each job

| Clock | Cost | Resolution | Use for |
|---|---|---|---|
| `rdtscp` / `__rdtsc` | ~20–30 cycles | ~1 cycle | **Hot-path stage timing.** Requires `constant_tsc` + `nonstop_tsc` (check `/proc/cpuinfo`); calibrate the TSC→ns ratio once at startup. `rdtscp` serialises against later loads; `__rdtsc` + `lfence` if you need ordering. |
| `clock_gettime(CLOCK_MONOTONIC_RAW)` | ~20–25 ns (vDSO) | ns | Off-hot-path durations. `_RAW` is unadjusted by NTP so it can't jump. |
| `clock_gettime(CLOCK_REALTIME)` | ~20–25 ns (vDSO) | ns | Wall-clock stamps for journals and cross-machine correlation. PTP-disciplined. |
| `SO_TIMESTAMPING` (NIC hardware) | free (out of band) | ~ns | **Wire arrival time.** The only trustworthy view of when a packet actually reached you. |
| PTP hardware clock (`/dev/ptpN`) | — | ~ns | The reference all of the above are disciplined to. |

**Practical rules**
- Time the hot path with **TSC deltas only**; convert to nanoseconds offline.
- Never take a timestamp inside a loop you're measuring — the measurement dominates.
  Measure the loop, divide.
- On multi-socket boxes, TSCs are synchronised on modern Intel/AMD but **verify** — a
  negative duration is the symptom.
- Cross-machine ⇒ PTP or the number is meaningless.

## 2. Latency taxonomy — what to instrument

Instrument every arrow in the architecture diagram. Minimum set:

**Inbound (your stack)**
| Metric | Definition |
|---|---|
| `wire_to_userspace` | NIC hardware ts → first byte visible to your feed handler |
| `decode` | Feed handler entry → decoded event emitted |
| `book_update` | Event popped → book state consistent |
| `feature_compute` | Book updated → feature vector ready |
| `policy_eval` | Features ready → quote decision made |
| `gateway_encode` | Decision → bytes handed to the NIC |
| **`tick_to_trade`** | **NIC hardware ts (in) → NIC hardware ts (out).** The headline number. Everything else is attribution for this one. |

**Outbound / round-trip (the venue)**
| Metric | Definition |
|---|---|
| `order_ack_rtt` | Send → exchange ack |
| `cancel_ack_rtt` | Cancel send → cancel ack. **Usually worse than order ack, and it's the one that determines whether you get sniped.** |
| `md_echo` | Your own order sent → seeing it appear in the public market data feed. Measures the exchange's own internal latency and its market-data publication delay. |
| `fill_notification_lag` | Execution report vs the trade appearing on the public feed |

**Health**
| Metric | Definition |
|---|---|
| `queue_depth` | Occupancy of every SPSC ring. Rising occupancy = you are falling behind; this precedes every latency blowup. |
| `gap_count`, `gap_recovery_time` | MoldUDP64 sequence gaps and how long recovery took |
| `slow_path_hits` | Count of times the hot path took a branch it shouldn't have (allocation, map growth, retransmit) |

## 3. Measuring latency correctly

Four things people get wrong, all of which will make your numbers optimistic:

1. **Coordinated omission.** If your measurement loop stalls, the events that would have
   been slow are never measured — so the stall makes your p99 look *better*. If you are
   measuring against a fixed expected arrival rate, use HdrHistogram's
   `record_corrected_value(v, expected_interval)`. Watch Gil Tene's *How NOT to Measure
   Latency*. This is the single most common latency-measurement bug.
2. **Averages.** Report **p50, p90, p99, p99.9, p99.99, max** and the full HDR percentile
   distribution (`.hgrm`). Publish the plot, not the number. Two systems with the same
   mean can differ by 100× at p99.9, and p99.9 is where your P&L lives — that's the tick
   with the news on it.
3. **Warm vs cold.** Your first message after an idle period is 10–50× slower: cold
   i-cache, cold d-cache, cold branch predictors, cold TLB, and the CPU may have dropped
   frequency. This is *the* problem in HFT because market bursts follow quiet periods.
   Mitigation: **cache warming** — run the hot path on synthetic input continuously and
   discard the output at the last branch. Measure both distributions and report them
   separately (`warm_p99` and `cold_p99` are different products).
4. **Benchmark hygiene.** Pin the benchmark thread, disable turbo and frequency scaling
   (or measure in cycles), use `benchmark::DoNotOptimize`, run enough iterations for the
   tail, and interleave A/B runs (`--swap-order`) so thermal drift doesn't masquerade as
   a result.

**Tooling:** HdrHistogram (C) for recording; Google Benchmark or
[nanobench](https://github.com/martinus/nanobench) for microbenchmarks; `perf stat` for
IPC/cache-miss/branch-miss counters; `perf c2c` for false sharing; VTune or `perf record`
for attribution; `bpftrace` for kernel-side stalls.

## 4. The market's delays — time-based distributions

This is the "time delays and distributions" half of the problem, and it's the half that
feeds the model. All of these are **empirical distributions to be estimated per
instrument, per time-of-day bucket, and monitored for drift.**

| Quantity | Definition | Known shape | Reference |
|---|---|---|---|
| **Inter-arrival time** per event type (limit add / cancel / market order / trade), per side, per level | Δt between successive events | Heavily clustered, **not exponential**; power-law kernels; conditional intensity is Hawkes | [arXiv:2401.10722](https://arxiv.org/abs/2401.10722), Bacry et al. |
| **Inter-trade duration** | Δt between trades | Scaling collapse across stocks; long memory | [arXiv:0804.3431](https://arxiv.org/abs/0804.3431) |
| **Time-to-fill (TTF)** | Order submission → (first / full) execution | **Power law**; ≠ price first-passage time because cancellations intervene | [arXiv:physics/0701335](https://arxiv.org/abs/physics/0701335) |
| **Time-to-cancel (TTC)** | Submission → cancellation | Power law; strongly dependent on distance from mid | same |
| **Order lifetime** by level and size | Submission → removal by any cause | Power-law tails | [arXiv:2106.11691](https://arxiv.org/abs/2106.11691) |
| **Queue lifetime** | How long a whole price level survives | | |
| **First-passage time** of mid to ±Δ | | Feeds the adverse-selection horizon choice | [arXiv:physics/0701335](https://arxiv.org/abs/physics/0701335) |
| **Cross-event durations** | submission→fill, submission→cancel, order→order, trade→trade, cancel→cancel | **Long-range autocorrelated** — measure with DFA, not ACF alone | [arXiv:1711.03534](https://arxiv.org/abs/1711.03534) |
| **Exchange round-trip mode** | The observed inter-event time distribution has a **spike at the exchange round-trip latency** — the signature of latency races and simultaneous reactions | Look for it; its position tells you your competitors' latency | [arXiv:2603.24137](https://arxiv.org/abs/2603.24137) |

That last row is worth dwelling on: **the inter-arrival distribution of the public feed
contains a direct measurement of the market's collective reaction latency.** Find the
mode. It is one of the more useful numbers you will extract, and it costs nothing but the
histogram.

## 5. Queue metrics — the highest-value measurements in the system

For a large-tick instrument, this section *is* the alpha.

| Metric | Why |
|---|---|
| **Queue position at insertion** — total volume ahead of you when your order rests | The single best predictor of whether you get filled and of your markout |
| **Queue ahead / queue behind**, tracked continuously | The state variable of the optimiser |
| **Queue decay decomposition**: of the volume that left the queue ahead of you, what fraction was **traded** vs **cancelled**? | Cancels ahead of you are free progress. Trades ahead of you are progress *plus* information. Two completely different signals — never aggregate them. |
| **Fill probability** `P(fill | queue_pos, imbalance, spread, time_of_day, vol)` | The core lookup. Estimate empirically *and* compare with the semi-analytic form in [arXiv:2403.02572](https://arxiv.org/abs/2403.02572) |
| **Queue value** — expected P&L contribution of holding position *k* in a queue of length *L* | Makes the requote decision quantitative: requote iff (value of new position) > (value of current position). See the "value of an order" characterisation in [arXiv:1806.05849](https://arxiv.org/abs/1806.05849) |
| **Cancel-ahead rate** as a function of distance-from-touch | [arXiv:1112.6085](https://arxiv.org/abs/1112.6085) |
| **Queue jump / level-clear rate** | How often the whole level disappears before you fill — the tail risk of passive quoting |
| **Own-order priority loss events** | Every requote, size increase, or modify that resets your priority. Count them and price them. |

**Implementation note:** all of this comes free if your book stores your own orders in the
same intrusive FIFO as everyone else's. If you bolt on a separate "my orders" structure,
you will spend the rest of the project reconciling the two. Don't.

## 6. Quote and execution quality metrics

| Metric | Definition |
|---|---|
| **Quoted spread** | `ask − bid`, time-weighted (not event-weighted — they differ a lot) |
| **Effective spread** | `2 × side × (trade_price − mid_at_trade)` — what the taker actually paid |
| **Realised spread** | `2 × side × (trade_price − mid_{t+τ})` — what the *maker* actually kept, after the price moved. **Effective − realised = adverse selection.** |
| **Implicit spread** | The meaningful spread measure for large-tick assets where effective spread is always 1 tick — [arXiv:1207.6325](https://arxiv.org/abs/1207.6325) |
| **Quote lifetime** | How long each of your quotes rests before fill/cancel/replace |
| **Time at BBO / uptime** | Fraction of the session you are at the touch, per side |
| **Top-of-book share** | Your size as a fraction of the touch queue — your market share of the passive side |
| **Quote-to-trade ratio** | Messages per fill. Venues police this; exceeding it costs money or access |
| **Message rate** vs venue throttle | Distribution, and how close p99.9 is to the limit |
| **Requote frequency and cause** | Attribution: price move, inventory, competitor, timer |

## 7. Adverse selection — markouts

The most important economic measurement. For **every fill**, record the mid at a ladder of
horizons and store the whole vector:

```
markout(τ) = side × (mid_{t+τ} − fill_price)     # positive = the fill was good
τ ∈ {0, 100µs, 1ms, 10ms, 100ms, 1s, 10s, 60s, 300s}
```

Then:
- Plot **mean markout vs τ** — the *shape* tells you the story. A curve that starts
  positive (you captured spread) and decays negative is normal; where it crosses zero is
  your effective holding-time budget.
- Condition markouts on everything: queue position at fill, imbalance at fill, whether the
  fill came from a sweep, time of day, order size, whether you were alone at the touch.
- The core empirical finding you must reproduce and respect: **fill probability and
  post-fill returns are negatively correlated** ([arXiv:2502.18625](https://arxiv.org/abs/2502.18625)).
  The easy fills are the bad fills. Any strategy that optimises fill rate without pricing
  this in is optimising its way into a loss.
- Also measure **flow toxicity** on the aggregate: signed-volume imbalance over volume
  buckets (VPIN-style), and the fraction of your fills that arrive in the same
  microsecond as a broad multi-venue move (those are snipes).

## 8. Distributions to fit — and how to know the fit is wrong

| Quantity | Candidate families | Diagnostic |
|---|---|---|
| Event inter-arrivals | Hawkes (exp / sum-of-exp / power-law kernel), Weibull, Hawkes×state | **Time-rescaling theorem**: under the fitted intensity, `Λ(tᵢ₋₁,tᵢ)` must be i.i.d. Exp(1). Test with KS + QQ + Ljung–Box on the residuals. If it fails, the model is wrong — no amount of parameter tuning fixes it. |
| Order sizes | Power law (tail exponent ≈ 2 for limit, ≈ 2.4 for market), lognormal body, **plus a discrete atom structure at round lots** | Hill estimator with a plotted stability range; never a single-point Hill estimate. Don't smooth away the round-number atoms — they're real and they matter for queue modelling. |
| Relative limit price (distance from touch) | Power law, exponent ≈ 1.5 | [arXiv:cond-mat/0206280](https://arxiv.org/abs/cond-mat/0206280) |
| Time-to-fill / time-to-cancel / lifetimes | Power law | Log-log survival plot; Hill |
| Price gaps between occupied levels | See [arXiv:1405.1247](https://arxiv.org/abs/1405.1247) | |
| Spread (small-tick) | Discrete distribution over ticks; regime-dependent | |
| Queue imbalance | Bounded, bimodal near ±1 for large tick | |
| Markouts | Fat-tailed, asymmetric | Report quantiles, never just the mean |
| Latency (yours) | Multi-modal (warm/cold/interrupted) | **Never** fit a single distribution. Report the HDR curve. |
| Returns at the event scale | Non-Gaussian, autocorrelated at short lags (microstructure noise) | Volatility signature plot |

**Estimation checklist for every fit:** hold-out period; sensitivity to the estimation
window; parameter stability over time (plot the parameter path, not the pooled estimate);
time-of-day conditioning (open/close/lunch are different markets); and exclusion of
auctions, halts and the first/last minutes.

## 9. Volatility and microstructure noise

- **Volatility signature plot**: realised variance vs sampling frequency. The upward hook
  at high frequency is microstructure noise; where it flattens tells you the sampling
  scale at which the price is roughly a martingale. Do this before choosing any horizon.
- **Two-scale / multi-scale realised variance**, pre-averaging estimators for `σ` under noise.
- **Uncertainty zones** (Robert & Rosenbaum) for large-tick instruments where the observed
  price is a coarse discretisation of a latent efficient price.
- **Roll estimator** as the cheap effective-spread cross-check.
- [arXiv:2202.12137](https://arxiv.org/abs/2202.12137) — use a calibrated LOB model as a
  data-generating process to test *your own volatility estimator implementation* against
  a known truth. Do this; estimator bugs are silent.

## 10. P&L attribution — the report that matters

Per fill, and aggregated per hour / day / regime:

```
Total P&L = Spread capture            (side × (mid_at_fill − fill_price) × qty)
          − Adverse selection         (side × (mid_at_fill − mid_{t+τ}) × qty)
          − Inventory / hedging cost  (mark-to-market of held inventory + hedge slippage)
          − Fees                      (+ maker rebates, per venue tier)
          − Impact of unwinds         (cost of crossing to flatten)
```

These five lines are controlled by five different mechanisms. Reporting only the total
tells you nothing about which to fix. Additional cuts to always produce:
- P&L per fill, per share, per unit of inventory risk borne.
- P&L conditional on queue position at fill.
- P&L conditional on whether you were sniped (fill in the same microsecond as a broad move).
- Inventory path: distribution of `|q|`, time spent at limits, max drawdown of inventory.
- **Sharpe computed on a defensible time unit**, with the caveats in §12.

## 11. Simulator fidelity — the stylized-fact scorecard

Before you trust a single backtest number, run this scorecard on (a) real data and
(b) simulator output, and diff:

| # | Fact | Pass criterion |
|---|---|---|
| 1 | Inter-arrival clustering | Hawkes branching ratio within CI of the empirical value; ACF of durations matches |
| 2 | Order size distribution | Tail exponent + round-lot atoms match |
| 3 | Limit-price placement | Power law with exponent ≈ 1.5 |
| 4 | Book shape (average depth by level) | Matches within CI |
| 5 | Spread distribution | Matches |
| 6 | Volatility signature plot | Same shape and same flattening scale |
| 7 | Return autocorrelation at short lags | Matches sign and decay |
| 8 | OFI → Δmid slope `β` and its `1/depth` scaling | Matches |
| 9 | Time-to-fill distribution | Matches, **conditional on queue position** |
| 10 | Queue decay: cancel vs trade split | Matches |
| 11 | Inter-event time mode at exchange RTT | Present, in the right place |
| 12 | Long-range memory (DFA exponent) of durations and signed flow | Matches |

Any red row invalidates backtest conclusions that depend on it. Write this as an
automated test that runs on every simulator change.

## 12. Statistical validity — don't fool yourself

- **Fills are clustered and autocorrelated.** Per-trade t-statistics assume independence
  you do not have. Use a **block bootstrap by day** (or by session), and report the
  bootstrap CI, not a p-value.
- **Multiple testing.** If you tried 200 parameter combinations, your best Sharpe is a
  selection artefact. Use a deflated Sharpe ratio / probability of backtest overfitting,
  and hold out data you genuinely never look at.
- **Sample size.** Ask up front: how many fills do I need to distinguish a 0.1 bp/fill
  improvement from noise, given the observed markout variance? Usually far more than you
  expect. Compute it before running the experiment.
- **Non-stationarity.** Parameters drift. Every calibration needs a stability plot and a
  re-fit cadence. See [arXiv:2308.01915](https://arxiv.org/abs/2308.01915) for how brutally
  this hits ML models on LOB data.
- **Your own impact.** Once you are a meaningful share of the touch queue, the historical
  book is no longer the counterfactual. A replay-based backtest silently assumes you have
  no effect. This is why you need an *interactive* simulator with flow feedback
  ([arXiv:2603.24137](https://arxiv.org/abs/2603.24137), [arXiv:2511.15262](https://arxiv.org/abs/2511.15262)).

## 13. Minimum viable dashboard

If you build only one report, build this one, per instrument per session:

1. Tick-to-trade HDR percentile curve, warm and cold, with per-stage attribution bars.
2. Ring-buffer occupancy over time (the early-warning signal).
3. Mean markout vs τ curve, plus the same curve split by queue position at fill.
4. Fill probability heatmap over (queue position, imbalance).
5. The five-line P&L attribution, per hour.
6. Inventory path with the risk limits drawn on.
7. Message rate vs venue throttle, p99.9.
8. The stylized-fact scorecard, red/green.
