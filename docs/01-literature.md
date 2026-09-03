# Annotated Bibliography

Everything here is grouped by the role it plays in the system, not by date. Each entry
says *what you take from it*. Papers marked **[core]** are the ones the design actually
depends on; the rest are context or alternatives.

Links are to arXiv/SSRN/publisher landing pages. arXiv IDs are given as `arXiv:ID` so you
can fetch them directly (`https://arxiv.org/abs/ID`).

---

## A. Foundations — the economics of quoting

The market maker's problem is not "predict the price". It is: *I must show a two-sided
price to a counterparty who may know more than I do, and I get paid the spread for
bearing that risk.* Everything downstream is an elaboration of that.

| Reference | What you take from it |
|---|---|
| Ho & Stoll (1981), *Optimal dealer pricing under transactions and return uncertainty*, JFE 9(1) | The original inventory-risk dealer problem. Reservation price = mid − (inventory × risk × variance × time). This is the skew term you will implement. |
| Glosten & Milgrom (1985), *Bid, ask and transaction prices in a specialist market...*, JFE 14(1) | Spread exists because of **adverse selection**, not inventory. Sets the floor on how tight you can quote against informed flow. |
| Kyle (1985), *Continuous auctions and insider trading*, Econometrica 53(6) | Lambda — price impact per unit signed volume. The linear-impact baseline every impact estimate is compared against. |
| Roll (1984), *A simple implicit measure of the effective bid-ask spread* | Estimating effective spread from trade prices alone (`−2√(−cov)`). Still the sanity check on your spread measurement. |
| [arXiv:1902.10743](https://arxiv.org/abs/1902.10743) — *From Glosten-Milgrom to the whole limit order book* | Derives the **entire book shape** (spread + depth at each level) from informed/noise/MM interaction. Bridges A→C. |

> **Design consequence:** your P&L decomposition must have separate lines for spread
> capture, adverse selection, and inventory cost — they are three different economic
> forces and they need three different controls. See `03-metrics-and-estimators.md` §10.

---

## B. Optimal market making (stochastic control) — **[core]**

This is the mathematical spine of "optimizer". You solve a Hamilton–Jacobi–Bellman
equation offline for the optimal bid/ask offsets as a function of state, then ship the
solution as a table.

| Reference | What you take from it |
|---|---|
| **Avellaneda & Stoikov (2008)**, *High-frequency trading in a limit order book*, Quantitative Finance 8(3) | **[core]** The canonical model. Mid follows BM; fill intensity `λ(δ) = A·exp(−k·δ)`; CARA utility. Gives reservation price `r = s − q·γ·σ²·(T−t)` and optimal total spread `γσ²(T−t) + (2/γ)ln(1+γ/k)`. Start here; it is ~40 lines of code. |
| [arXiv:1105.3115](https://arxiv.org/abs/1105.3115) — Guéant, Lehalle & Fernandez-Tapia, *Dealing with the inventory risk* | **[core]** Fixes A–S: inventory bounds, closed-form asymptotics, and a **quote-as-a-function-of-inventory table** that is genuinely implementable. This is the practical A–S. |
| [arXiv:1605.01862](https://arxiv.org/abs/1605.01862) — Guéant, *Optimal market making* | **[core]** The consolidated treatment: general intensities, multi-asset, closed-form approximations. The best single reference for the whole family. |
| [arXiv:1106.5040](https://arxiv.org/abs/1106.5040) — Guilbaud & Pham, *Optimal HFT with limit and market orders* | Mixes **limit orders (regular control) with market orders (impulse control)** — i.e. when to stop quoting and hedge by crossing. You need this for inventory unwinds. |
| [arXiv:1205.3051](https://arxiv.org/abs/1205.3051) — Guilbaud & Pham, *Optimal HFT in a pro-rata microstructure* | If you ever touch STIR futures (Eurodollar/SOFR, Euribor) the matching is **pro-rata, not FIFO** — completely different optimum (you over-quote to win allocation). |
| [arXiv:1303.7177](https://arxiv.org/abs/1303.7177) / [arXiv:1206.4810](https://arxiv.org/abs/1206.4810) — Fodra & Labadie | A–S extended to multi-dimensional Markov state (stochastic vol, stochastic intensities) and directional views. The template for adding a signal to the control. |
| [arXiv:1902.01157](https://arxiv.org/abs/1902.01157) — *Optimal market making under partial information* | Hidden-regime version: intensities depend on an unobserved Markov chain you must filter. Realistic — regimes exist and you do not observe them. |
| [arXiv:1903.07222](https://arxiv.org/abs/1903.07222) — *Market making under a weakly consistent LOB model* | Model that respects **price-time priority and mechanical consistency** (prices can't fall on buy market orders). Bridge between elegant control models and an actual book. |
| [arXiv:2405.11444](https://arxiv.org/abs/2405.11444) — *Adaptive optimal MM with inventory liquidation costs* | Discrete-time, closed-form, **random demand coefficients** — models partial fills properly. Good fit for a tabulated policy. |
| [arXiv:2101.03086](https://arxiv.org/abs/2101.03086) — *MM with stochastic liquidity demand* | Explicit optimal placement that **incorporates a price forecast** — the clean way to fold alpha into quoting rather than bolting it on. |
| [arXiv:2510.26438](https://arxiv.org/abs/2510.26438) — *An impulse control approach to MM in a Hawkes LOB market* | Realistic constraint: **you cannot requote on every event**. Impulse control with intervention costs is the honest formulation once you account for message limits. |
| [arXiv:2605.24878](https://arxiv.org/abs/2605.24878) — *Entropy-regularized certainty-equivalent Bellman policies* | Risk-sensitive discrete Bellman operator that is exact under CARA — a clean bridge between the HJB solution and an RL-shaped policy. |

> **Design consequence:** none of these get solved on the hot path. Solve offline, emit a
> `(inventory, imbalance, spread_state, vol_bucket) → (bid_offset, ask_offset)` table,
> and make the online step a bounds-checked array lookup. See `00-scope-and-architecture.md`.

---

## C. Order book dynamics — queueing and Markov models — **[core]**

You need a *generative* model of the book to (a) simulate fills counterfactually and
(b) compute fill probabilities analytically.

| Reference | What you take from it |
|---|---|
| Smith, Farmer, Gillemot & Krishnamurthy (2003), *Statistical theory of the continuous double auction* | The zero-intelligence baseline. Predicts spread/depth/impact scaling from arrival rates alone. Your simulator must beat it, and you should know by how much. |
| **Cont, Stoikov & Talreja (2010)**, *A stochastic model for order book dynamics*, Operations Research 58(3) | **[core]** Book as a continuous-time Markov chain of queues. Yields **analytical fill probabilities and first-passage times** via Laplace transforms. |
| Cont & de Larrard (2013), *Price dynamics in a Markovian limit order market*, SIAM J. Financial Math | Heavy-traffic limit: mid-price diffusion coefficient expressed in terms of *order flow* parameters. Connects microstructure to realised vol. |
| **[arXiv:1312.0563](https://arxiv.org/abs/1312.0563)** — Huang, Lehalle & Rosenbaum, *The queue-reactive model* | **[core]** Arrival intensities are functions of the current queue sizes. Simple, calibratable, and reproduces both book stylized facts *and* lower-frequency price behaviour. **This is the default simulator engine.** |
| [arXiv:2405.18594](https://arxiv.org/abs/2405.18594) — *A novel approach to queue-reactive models: the importance of order sizes* | QR extended so **size** is state-dependent too. Materially better realism; calibrated on Bund futures. |
| [arXiv:2501.08822](https://arxiv.org/abs/2501.08822) — *Multidimensional Deep Queue-Reactive (MDQR)* | Drops queue independence, adds market features and a size distribution via a neural net. The modern QR. |
| [arXiv:2506.11843](https://arxiv.org/abs/2506.11843) — *Multi-dimensional queue-reactive and signal-driven models: a unified framework* | Couples QR to a hidden efficient price — lets you correlate books across instruments. |
| [arXiv:1311.5661](https://arxiv.org/abs/1311.5661) — Toke, *The order book as a queueing system* | Birth–death process gives a **closed-form book shape** and execution probability. Excellent intuition, cheap to implement as a check. |
| [arXiv:1304.6819](https://arxiv.org/abs/1304.6819) — Gareche, Disdier, Kockelkoren & Bouchaud, *Fokker–Planck description for the queue dynamics of large tick stocks* | **2-D Fokker–Planck** for (bid queue, ask queue) with state-dependent drift/diffusion + jump terms. The right continuum model for large-tick assets, calibrated from best-quote data only. |
| [arXiv:1808.07107](https://arxiv.org/abs/1808.07107) / [arXiv:1405.5230](https://arxiv.org/abs/1405.5230) | Scaling limits: micro → meso → macro (SDE–SPDE systems). Read if you want to know *why* the diffusion approximations hold. |
| [arXiv:2403.02572](https://arxiv.org/abs/2403.02572) — *Fill probabilities in a LOB with state-dependent stochastic order flows* | **[core]** Semi-analytic fill probabilities in a state-dependent framework. Directly the quantity your quoting policy needs. |

---

## D. Hawkes processes and order-flow clustering — **[core]**

Order flow is not Poisson. It clusters, it self-excites, and inter-arrival times are the
thing you asked to measure. Hawkes is the standard tool.

| Reference | What you take from it |
|---|---|
| Bacry, Mastromatteo & Muzy (2015), *Hawkes processes in finance*, Market Microstructure & Liquidity 1(1) | **[core]** The survey. Read this before any of the below. |
| Bacry & Muzy (2014), *Hawkes model for price and trades high-frequency dynamics*, Quantitative Finance 14(7) | Non-parametric kernel estimation (Wiener–Hopf). Shows kernels are **power-law**, not exponential — which matters for long-memory. |
| [arXiv:1604.01824](https://arxiv.org/abs/1604.01824) — *The statistical significance of multivariate Hawkes processes fitted to LOB data* | How to tell whether your fitted excitation is real. Sum-of-exponentials kernel + piecewise-linear exogenous baseline. **Read before believing a calibration.** |
| [arXiv:1809.08060](https://arxiv.org/abs/1809.08060) — *State-dependent Hawkes processes and their application to LOB modelling* | Fully coupled counting process ↔ state process, with MLE methodology. The rigorous version of "Hawkes + book state". |
| [arXiv:1901.08938](https://arxiv.org/abs/1901.08938) — *Queue-reactive Hawkes models for the order flow* | **[core]** Explicitly combines the QR model (C) with Hawkes excitation (D). Best of both: state dependence *and* time clustering. |
| [arXiv:2107.12872](https://arxiv.org/abs/2107.12872) — *LOB modeling using Hawkes processes with a state-dependent factor* | Intensity = Hawkes × state factor (imbalance, spread). Full recipe for efficient MLE/EM estimation. Euronext empirics. |
| [arXiv:2107.09629](https://arxiv.org/abs/2107.09629) — *Order book queue Hawkes-Markovian modeling* | Event taxonomy (order type × subsequent price move) + Markovian baseline. Useful event-classification scheme. |
| [arXiv:2005.05730](https://arxiv.org/abs/2005.05730) — *Non-parametric estimation of quadratic Hawkes processes* | **Quadratic** Hawkes: past *price changes* (not just events) feed intensity. Captures the Zumbach effect / trend→volatility feedback. |
| [arXiv:2312.08927](https://arxiv.org/abs/2312.08927) — *LOB dynamics and order size modelling using compound Hawkes* | Marks the Hawkes events with sizes drawn from a calibrated distribution, keeping spread positive. Practical simulator ingredient. |
| [arXiv:2307.09077](https://arxiv.org/abs/2307.09077) — *Estimation of an order-book-dependent Hawkes process for large datasets* | Scales estimation to **billions of events**. Read when your calibration stops fitting in memory. |
| [arXiv:2502.17417](https://arxiv.org/abs/2502.17417) — *Event-based LOB simulation under a neural Hawkes process: application in market-making* | 12-event neural Hawkes LOB simulator built specifically for MM evaluation. |
| [arXiv:2510.08085](https://arxiv.org/abs/2510.08085) — *A deterministic LOB simulator with Hawkes-driven order flow* | **Directly relevant reference implementation:** deterministic **C++** LOB + marked multivariate Hawkes, with stability proofs, **time-rescaling goodness-of-fit**, and calibration on Binance BTCUSDT + LOBSTER AAPL. Note the finding that the *nearly-unstable subcritical* regime is what produces realistic clustering. |

> **Measurement consequence:** the **time-rescaling theorem** (Ogata residuals) is your
> goodness-of-fit test for every intensity model: if `Λ(tᵢ₋₁, tᵢ)` are not i.i.d. Exp(1),
> your model is wrong. See `03-metrics-and-estimators.md` §8.

---

## E. Queue position, fill probability, adverse selection — **[core]**

This is the part that separates a real market-making system from a backtest fantasy.
Your P&L is dominated by *where you are in the queue* and *what happens to the price
right after you get filled*.

| Reference | What you take from it |
|---|---|
| [arXiv:2502.18625](https://arxiv.org/abs/2502.18625) — *The market maker's dilemma: navigating the fill probability vs post-fill returns trade-off* | **[core] Read this one twice.** Live experiment on Binance BTC perps. Documents the fundamental negative correlation between **fill likelihood and post-fill returns**, and shows commonly-cited strategies are unprofitable once you account for it. Implies **contrarian** quoting against prevailing imbalance. |
| [arXiv:1610.00261](https://arxiv.org/abs/1610.00261) — Lehalle & Mounjid, *Limit order strategic placement with adverse selection risk and the role of latency* | **[core]** Empirical + control treatment of how imbalance drives fill *and* adverse selection, and where latency enters. |
| Cont & Kukanov (2017), *Optimal order placement in limit order markets*, Quantitative Finance 17(1) | Convex formulation for splitting between limit/market and across venues, with fill uncertainty. The routing layer. |
| Laruelle, Lehalle & Pagès (2013), *Optimal posting price of limit orders: learning by trading* | Stochastic-approximation scheme that **learns** the fill curve online rather than assuming `A·e^{−kδ}`. |
| [arXiv:physics/0701335](https://arxiv.org/abs/physics/0701335) — *Diffusive behavior and the modeling of characteristic times in limit order executions* | Empirics on **time-to-fill (TTF)** and **time-to-cancel (TTC)** distributions — both power-law, and TTF ≠ first-passage time because of cancellations. Directly the "time delays / distributions" you asked about. |
| [arXiv:1112.6085](https://arxiv.org/abs/1112.6085) — *The position profiles of order cancellations* | Where in the queue and at what price level cancellations actually happen. Needed to model **queue decay ahead of you**. |
| [arXiv:1511.04116](https://arxiv.org/abs/1511.04116) — *Latency and liquidity provision in a limit order book* | Nasdaq empirics on order flow **phases around a market order** — stimulated refill and the strategic response. Tells you when your quote is about to be run over. |
| [arXiv:2307.15599](https://arxiv.org/abs/2307.15599) — *Understanding the worst-kept secret of high-frequency trading* | Shows imbalance as an **optimal MM response** to price moves rather than a causal predictor. Important epistemic correction. |
| [arXiv:2607.28323](https://arxiv.org/abs/2607.28323) — *Optimal execution with passive market impact* | Builds on two measurables — exponential decay of fill probability with distance, and linear OFI response — to get a **passive impact rate**. Useful reduced form. |

---

## F. Signals: imbalance, microprice, order flow imbalance

| Reference | What you take from it |
|---|---|
| **[arXiv:1011.6402](https://arxiv.org/abs/1011.6402)** — Cont, Kukanov & Stoikov, *The price impact of order book events* | **[core]** `Δmid ≈ β · OFI`, with `β ∝ 1/depth`. Robust, linear, stable across stocks and time scales. The workhorse. |
| [arXiv:1907.06230](https://arxiv.org/abs/1907.06230) — *Multi-level order flow imbalance (MLOFI)* | OFI computed at each of the top *N* levels; explanatory power keeps improving with depth. Use the vector, not the scalar. |
| [arXiv:2112.13213](https://arxiv.org/abs/2112.13213) — Cont, Cucuringu & Zhang, *Cross-impact of order flow imbalance in equity markets* | Integrated (PCA-style) multi-level OFI; **cross-impact matters for prediction, not contemporaneous impact**. |
| [arXiv:2112.02947](https://arxiv.org/abs/2112.02947) — *The price impact of generalized order flow imbalance* | Handles non-minimum quote increments; log-OFI stationarisation. |
| Stoikov (2018), *The micro-price: a high-frequency estimator of future prices*, Quantitative Finance 18(12) | **[core]** Martingale-corrected mid: `E[future mid | imbalance, spread]`, computed by iterating a Markov transition. Cheap, and strictly better than weighted mid. |
| [arXiv:1512.03492](https://arxiv.org/abs/1512.03492) — Gould & Bonart, *Queue imbalance as a one-tick-ahead price predictor* | Careful logistic-regression study of imbalance predictive power on 10 Nasdaq names — including where it fails. |
| Lipton, Pesavento & Sotiropoulos (2013), *Trade arrival dynamics and quote imbalance in a limit order book* ([arXiv:1312.0514](https://arxiv.org/abs/1312.0514)) | Closed-form first-passage results for the imbalance→next-move problem. |
| [arXiv:1708.02715](https://arxiv.org/abs/1708.02715) — *Order flows and LOB resiliency on the meso-scale* | Nonlinear trade-imbalance→price relation that **linearises** once you weight market and limit order flow together. |
| [arXiv:2608.00885](https://arxiv.org/abs/2608.00885) — *Optimal trading of microstructure mean reversion* | The mid carries a stationary mean-reverting error around the efficient price; solves for the optimal rule net of spread in a large-tick asset. |

---

## G. Latency — theory, empirics, and market design — **[core]**

| Reference | What you take from it |
|---|---|
| **Moallemi & Sağlam (2013)**, *The cost of latency in high-frequency trading*, Operations Research 61(5) 1070–1086 — [SSRN](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=1571935) · [INFORMS](https://pubsonline.informs.org/doi/10.1287/opre.2013.1165) | **[core]** Gives latency a **dollar price**: the cost of delay in an execution problem, scaling roughly with `σ√latency`. This is how you justify infrastructure spend. |
| [arXiv:1806.05849](https://arxiv.org/abs/1806.05849) — *Optimal market making in the presence of latency* | **[core]** MDP formulation for **large-tick** assets with latency. Characterises the **value of an order** as the one-period reward, and gives explicit profitability criteria under latency. Closest paper to what you're building. |
| [arXiv:1908.03281](https://arxiv.org/abs/1908.03281) — Cartea, Jaimungal & Sánchez-Betancourt, *Latency and liquidity risk* | Latency-optimal marketable limit orders: how far through the book to reach given that the book moves while your order is in flight. The **taker-side** analogue. |
| [arXiv:2504.00846](https://arxiv.org/abs/2504.00846) — *The effect of latency on optimal order execution policy* | Two latency risks made explicit: non-execution, and a limit order becoming effectively marketable. |
| [arXiv:2505.12465](https://arxiv.org/abs/2505.12465) — *Resolving latency and inventory risk in market making with RL* | Notes that most RL MM papers **ignore latency entirely**, which produces unimplementable policies (cancels that fail, unintended inventory). |
| [arXiv:2006.08682](https://arxiv.org/abs/2006.08682) — *The importance of low latency to order book imbalance trading strategies* | Quantifies, in a controlled simulation, how much of an imbalance strategy's edge is pure speed. |
| Budish, Cramton & Shim (2015), *The HFT arms race: frequent batch auctions as a market design response*, QJE 130(4) | **[core]** Why continuous-time markets create a sniping race and a floor under the spread. Explains a structural share of your adverse selection. |
| [Aquilina, Budish & O'Neill (2022)](https://academic.oup.com/qje/article/137/1/493/6368348), *Quantifying the HFT arms race*, QJE 137(1) 493–564 | Message-level exchange data including **failed** snipes and cancels — the losers of a race, which normal LOB data never records. Races resolve on a microsecond timescale and impose a measurable latency-arbitrage tax. Sets your target latency scale honestly. |
| Hasbrouck & Saar (2013), *Low-latency trading*, Journal of Financial Markets 16(4) | "Strategic runs" — how to detect low-latency activity in public message data. |
| Menkveld (2013), *High frequency trading and the new market makers*, JFM 16(4) | Anatomy of a real HFT market maker's P&L: gross spread revenue vs positioning losses. The benchmark shape for your own attribution. |
| [arXiv:2603.24137](https://arxiv.org/abs/2603.24137) — *Bridging the reality gap in LOB simulation* | Calibrating event timing reveals **a pronounced mode at exchange round-trip latency** — direct empirical evidence of latency races in the inter-arrival distribution. Measure this on your own data. |
| [arXiv:2603.07752](https://arxiv.org/abs/2603.07752) — *Dynamic slippage control and rejection feedback in spot FX market making* | Latency-driven adverse selection modelled as a Gaussian mark over the delay window, plus last-look/rejection as a control. Relevant if you quote OTC/RFQ. |

---

## H. Machine learning and reinforcement learning

Useful, but note the recurring finding: **complexity buys less than better inputs and an
honest fill model.**

| Reference | What you take from it |
|---|---|
| [arXiv:1808.03668](https://arxiv.org/abs/1808.03668) — Zhang, Zohren & Roberts, *DeepLOB* | The reference CNN+LSTM LOB architecture. Baseline everything against it. |
| [arXiv:1601.01987](https://arxiv.org/abs/1601.01987) — Sirignano, *Deep learning for limit order books* | "Spatial neural network" exploiting the book's spatial structure; models the joint distribution deep in the book. |
| [arXiv:2003.00130](https://arxiv.org/abs/2003.00130) — *Transformers for limit order books* | Causal conv + masked self-attention; SOTA on FI-2010 at the time. |
| [arXiv:2502.15757](https://arxiv.org/abs/2502.15757) — *TLOB: dual attention for price trend prediction* | Notes a plain MLP beats much of the published SOTA — a healthy warning about the benchmark. |
| **[arXiv:2308.01915](https://arxiv.org/abs/2308.01915)** — *LOB-based DL models for stock price trend prediction: a benchmark study* (LOBCAST) | **[core] Read before doing any DL here.** Fifteen SOTA models, all of which **degrade sharply on new data**. The generalisation problem is the real problem. |
| [arXiv:2506.05764](https://arxiv.org/abs/2506.05764) — *Better inputs matter more than stacking another hidden layer* | Crypto LOB; same conclusion from the other direction. |
| [arXiv:2305.15821](https://arxiv.org/abs/2305.15821) — *Market making with deep RL from limit order books* | End-to-end RL on raw LOB rather than handcrafted features. |
| [arXiv:2207.09951](https://arxiv.org/abs/2207.09951) — *Deep RL for MM under a Hawkes-process-based LOB model* | RL trained on a **weakly consistent Hawkes simulator** — the right way round (learn in a good simulator, not on replayed data). |
| [arXiv:2608.18195](https://arxiv.org/abs/2608.18195) — *Multi-level market making with RL* | Quotes **multiple levels and sizes** simultaneously via logistic-normal allocations + deep-set encoder. Closest to a real quoting ladder. |
| [arXiv:2109.15110](https://arxiv.org/abs/2109.15110) — *Deep Hawkes process for high-frequency market making* | Agents choose price, order type **and execution time**. |
| [arXiv:2510.27334](https://arxiv.org/abs/2510.27334) — *When AI trading agents compete* | RL market makers adversely selecting meta-orders in a Hawkes LOB. Read for what happens when everyone runs this. |
| [arXiv:2411.13594](https://arxiv.org/abs/2411.13594) — *High-resolution microprice from LOB data using Tsetlin machines* | A genuinely fast (hot-path-viable) non-linear microprice estimator. |

---

## I. Simulation and backtesting — **[core]**

**Your backtest is a lie in exactly one place: the fill model.** These are how you make it
less of a lie.

| Reference | What you take from it |
|---|---|
| [arXiv:1904.12066](https://arxiv.org/abs/1904.12066) — **ABIDES** | Agent-based interactive discrete-event simulator; the de-facto open academic multi-agent market sim. [github.com/jpmorganchase/abides-jpmc-public](https://github.com/jpmorganchase/abides-jpmc-public) |
| [arXiv:2308.13289](https://arxiv.org/abs/2308.13289) — **JAX-LOB** | GPU-accelerated, vectorised LOB for training RL at scale — thousands of books in parallel. |
| [arXiv:2102.10925](https://arxiv.org/abs/2102.10925) — **CoinTossX** | Open-source low-latency, high-throughput **matching engine**. Useful as a realistic exchange to trade against. |
| [arXiv:2510.08085](https://arxiv.org/abs/2510.08085) — Deterministic **C++** LOB simulator with Hawkes order flow | The closest published architecture to this project. |
| [arXiv:2603.24137](https://arxiv.org/abs/2603.24137) — *Bridging the reality gap in LOB simulation* | Interactive large-tick simulator projecting book state onto (spread, imbalance), calibrated event timing, plus a **signed-flow feedback** mechanism so your own orders have impact. |
| [arXiv:2303.00080](https://arxiv.org/abs/2303.00080) — *Neural stochastic agent-based LOB simulation* | Hybrid ABM/stochastic: data-grounded *and* interactive. |
| [arXiv:2210.09897](https://arxiv.org/abs/2210.09897) — *Learning to simulate realistic LOB markets as a World Agent* | Skips agent calibration entirely — learns market response from history. |
| [arXiv:2202.12137](https://arxiv.org/abs/2202.12137) — *From zero-intelligence to queue-reactive* | Uses LOB models as a **data-generating process to test estimators** (volatility, execution). This is the methodology for validating your own measurement code. |
| [arXiv:1501.02447](https://arxiv.org/abs/1501.02447) — *Stochastic simulation framework for the LOB using liquidity-motivated agents* | Calibration by **indirect inference + multi-objective optimisation** — the practical way to fit an ABM. |
| [arXiv:2604.18046](https://arxiv.org/abs/2604.18046) — *EvoMarket* | Discrete-event multi-agent simulator aiming at mechanism + microstructure fidelity at market scale. |
| [arXiv:2511.15262](https://arxiv.org/abs/2511.15262) — *RL in queue-reactive models* | Why you need a simulator at all: historical data cannot give **counterfactual** feedback for policy optimisation. |

---

## J. Tick size and the large-tick / small-tick distinction — **[core]**

Get this wrong and every model above is misspecified. A large-tick asset (spread pinned
at 1 tick, deep queues) is a **queue-position game**. A small-tick asset (spread varies,
thin queues) is a **price-placement game**. Different code paths, different optimisers.

| Reference | What you take from it |
|---|---|
| [arXiv:1207.6325](https://arxiv.org/abs/1207.6325) — Dayri & Rosenbaum, *Large tick assets: implicit spread and optimal tick size* | **[core]** Defines the **implicit spread** — the meaningful spread measure when the effective spread is always 1 tick. Gives a quantitative "how large-tick is this asset". |
| [arXiv:2410.08744](https://arxiv.org/abs/2410.08744) — *No tick-size too small: a general method for modelling small-tick LOBs* | Stylized facts + explicit metrics for classifying large / medium / small tick, with cross-asset visualisations. |
| [arXiv:1304.6819](https://arxiv.org/abs/1304.6819) — Fokker–Planck queue dynamics (see §C) | The large-tick continuum model. |
| Robert & Rosenbaum (2011), *A new approach for the dynamics of ultra-high-frequency data: the model with uncertainty zones* | Explains why the observed price is a discretisation of an efficient price, and how to estimate the latter. Underpins microprice and vol estimation on tick grids. |

---

## K. Impact and execution (you will need this for inventory unwinds)

| Reference | What you take from it |
|---|---|
| Almgren & Chriss (2000), *Optimal execution of portfolio transactions*, J. Risk 3(2) | The baseline schedule for liquidating inventory. |
| Obizhaeva & Wang (2013), *Optimal trading strategy and supply/demand dynamics*, JFM 16(1) | Transient impact with book resilience. |
| Bouchaud, Gefen, Potters & Wyart (2004), *Fluctuations and response in financial markets* | The **propagator** model — impact decays as a power law; the reason naive impact estimates double-count. |
| Gatheral (2010), *No-dynamic-arbitrage and market impact*, Quantitative Finance 10(7) | Constraints linking impact shape and decay. |
| [arXiv:2112.04245](https://arxiv.org/abs/2112.04245) — *Do fundamentals shape the price response?* | Kyle vs propagator: identical at high frequency, differ in impact magnitude. Relevant to how you calibrate. |
| [arXiv:1409.2618](https://arxiv.org/abs/1409.2618) — *Optimal execution with dynamic order flow imbalance* | Leaning with vs against prevailing flow. |

---

## L. Stylized facts to reproduce and measure — **[core]**

These are the empirical targets for §7 of `03-metrics-and-estimators.md`. If your
simulator does not reproduce them, your backtest P&L is not informative.

| Reference | The fact |
|---|---|
| [arXiv:2401.10722](https://arxiv.org/abs/2401.10722) — *Stylized facts and market microstructure: German bond futures* | Broad, modern, LOB-level catalogue: order size distributions, order flow patterns, **inter-arrival times**. Best single "what should I be seeing" reference. |
| [arXiv:cond-mat/0102518](https://arxiv.org/abs/cond-mat/0102518) — Maslov & Mills | Market order sizes: power law, tail exponent ≈ 2.4. Limit order sizes: exponent ≈ 2. |
| [arXiv:cond-mat/0206280](https://arxiv.org/abs/cond-mat/0206280) — Zovko & Farmer, *The power of patience* | Relative limit price placement is a **power law with exponent ≈ 1.5**, over 2+ decades. |
| [arXiv:2106.11691](https://arxiv.org/abs/2106.11691) — *Two price regimes in limit order books* | Joint structure of price-distance, lifetime and volume: a "liquidity cushion" near the quotes and a fragmented distant field. |
| [arXiv:0804.3431](https://arxiv.org/abs/0804.3431) — *Scaling in the distribution of intertrade durations* | Inter-trade durations collapse onto a single scaled curve across stocks. |
| [arXiv:1711.03534](https://arxiv.org/abs/1711.03534) — *Long-range autocorrelations in LOB markets* | DFA on order-to-order, trade-to-trade, cancel-to-cancel **and** cross-event (submission→fill, submission→cancel) durations. Directly the "time delays" measurement programme. |
| [arXiv:1405.1247](https://arxiv.org/abs/1405.1247) — *Stylized facts of price gaps in limit order books* | Distribution of the gap between the first two occupied levels — a key depth/liquidity determinant. |
| [arXiv:physics/0701335](https://arxiv.org/abs/physics/0701335) | Power-law time-to-fill and time-to-cancel (see §E). |

---

## M. Books

| Book | Why |
|---|---|
| Cartea, Jaimungal & Peñalva, *Algorithmic and High-Frequency Trading*, CUP 2015 | **[core]** The textbook for this project. Chapter 10 is the market-making chapter; derivations are complete and implementable. |
| Bouchaud, Bonart, Donier & Gould, *Trades, Quotes and Prices: Financial Markets Under the Microscope*, CUP 2018 | **[core]** The empirical/statistical-physics complement. Best treatment of impact, order flow and stylized facts anywhere. |
| Guéant, *The Financial Mathematics of Market Liquidity*, CRC 2016 | The rigorous stochastic-control reference; ties together §B. |
| Lehalle & Laruelle (eds.), *Market Microstructure in Practice*, 2nd ed., World Scientific 2018 | Practitioner view of fragmentation, venue choice, and what actually happens on a trading desk. |
| Hasbrouck, *Empirical Market Microstructure*, OUP 2007 | The econometrics: VAR/VECM decompositions, information shares, Roll-type estimators. |
| Harris, *Trading and Exchanges*, OUP 2003 | Institutional detail — order types, priority rules, who is on the other side and why. |
| O'Hara, *Market Microstructure Theory*, Blackwell 1995 | The theory canon behind §A. |
| Donadio, Ghosh & Rossier, *Developing High-Frequency Trading Systems*, Packt 2022 | End-to-end HFT system engineering in C++/Java/Python. |
| Ghosh, *Building Low Latency Applications with C++*, Packt 2023 | Hands-on: feed handler → book → strategy → order gateway in C++ from scratch. |
| Silahian, *C++ High Performance for Financial Systems*, Packt 2024 | Architecture-level treatment of trading system components. |
| Williams, *C++ Concurrency in Action*, 2nd ed., Manning 2019 | The memory-model reference you will need for the lock-free ring buffers. |
| Fog, *Optimizing software in C++* / *Instruction tables* ([agner.org/optimize](https://www.agner.org/optimize/)) | **[core]** Free. The microarchitecture reference. |
| Drepper, *What Every Programmer Should Know About Memory* (2007) | **[core]** Free. Cache hierarchy, false sharing, prefetching, NUMA. |

---

## N. Talks worth watching

- **Carl Cook**, *When a Microsecond Is an Eternity: High Performance Trading Systems in C++* — CppCon 2017. The canonical talk; cache warming, avoiding branches on the hot path, "the code you never run is the fastest".
- **David Gross**, *Trading at Light Speed: Designing Low Latency Systems in C++* — CppCon 2024. Modern update: measurement methodology, kernel bypass, what actually matters in 2024+.
- **Gil Tene**, *How NOT to Measure Latency*. **[core]** Coordinated omission — why your p99 is probably wrong.
- **Martin Thompson / LMAX**, *Mechanical Sympathy* talks and the **Disruptor** pattern.
- **Nasdaq / CME exchange webinars** on their own matching-engine behaviour and self-match prevention.

---

## O. Reading order

If you are starting from zero, in this order:

1. Harris, *Trading and Exchanges* — institutional grounding (skim).
2. Cartea–Jaimungal–Peñalva ch. 1–6, then ch. 10.
3. Avellaneda–Stoikov → [arXiv:1105.3115](https://arxiv.org/abs/1105.3115) → [arXiv:1605.01862](https://arxiv.org/abs/1605.01862).
4. [arXiv:1312.0563](https://arxiv.org/abs/1312.0563) (queue-reactive) — then implement it.
5. [arXiv:1011.6402](https://arxiv.org/abs/1011.6402) (OFI) + Stoikov microprice — then implement both.
6. [arXiv:2502.18625](https://arxiv.org/abs/2502.18625) (fill probability vs post-fill returns) — the reality check.
7. Bacry–Mastromatteo–Muzy Hawkes survey → [arXiv:1901.08938](https://arxiv.org/abs/1901.08938).
8. Moallemi–Sağlam + [arXiv:1806.05849](https://arxiv.org/abs/1806.05849) — latency, priced.
9. [arXiv:2308.01915](https://arxiv.org/abs/2308.01915) (LOBCAST) — before you write any ML.
