# Data, Feeds and Protocols

You cannot build a queue-position optimiser without **L3 / market-by-order** data. That
constraint drives every choice below.

## 1. Data granularity — and why L2 is not enough

| Level | Also called | What you get | Can you model queue position? |
|---|---|---|---|
| L1 | MBP-1, TOB, BBO | Best bid/ask price and size | No |
| L2 | MBP-10, depth | Aggregated size at each of the top *N* prices | **No** — you see the queue length, not your place in it |
| L3 | **MBO**, full order book | Every individual order, keyed by order ID, with add/modify/cancel/execute events | **Yes** |

With L2 you can compute imbalance, OFI and microprice. You *cannot* know whether the 500
lots ahead of you are one order about to be cancelled or fifty orders that will sit there.
Since your P&L is dominated by fill rate and adverse selection, and both depend on queue
position, **L3 is a hard requirement** for the core of this project.

A partial workaround exists: with L2 plus trade prints you can *infer* queue consumption
(trades take from the front, cancels are ambiguous). It is a real technique for venues
that don't publish MBO, and it is strictly worse. Treat it as a fallback, and measure how
much worse it is on a venue where you have both.

## 2. Market data protocols

### ITCH (Nasdaq and ITCH-derived feeds)
- Binary, application-level; **market-by-order**, keyed by order reference number.
- Almost always over **MoldUDP64** (sequencing/session layer) over **UDP multicast**.
  MoldUDP64 gives you sequence numbers, retransmission via a rewind server, and
  heartbeats — your gap-fill logic lives here.
- Message types you must handle: Add Order (with/without MPID), Order Executed
  (with/without price), Order Cancel (partial), Order Delete (full), Order Replace,
  Trade (non-displayable), Cross Trade, Stock Directory, Trading Action, NOII/imbalance.
- **Variants are not compatible.** Nasdaq TotalView-ITCH, BX, PSX, SGX ITCH, ASX ITCH,
  JPX Next J-GATE ITCH, Cboe PITCH, LSE MITCH all share conventions and differ in detail.
  Write the decoder against one spec, keep the book layer protocol-agnostic.
- Some ITCH-derived feeds key by *price level ID* rather than order ID — those are L2
  wearing an L3 costume. Check before you commit.
- Reference: [Databento's ITCH primer](https://databento.com/microstructure/itch);
  Nasdaq publishes the TotalView-ITCH 5.0 spec publicly.

### CME MDP 3.0 (futures/options)
- **FIX/SBE** (Simple Binary Encoding), event-driven. The sole feed for all CME Globex
  instruments. Since 2017 it carries **MBO ("market by order") alongside MBP** for a
  growing set of instruments.
- SBE has a published XML schema and **generates a zero-copy C++ decoder** — use
  [real-logic/simple-binary-encoding](https://github.com/real-logic/simple-binary-encoding)
  codegen rather than hand-rolling. Same encoding is used for **iLink 3** order entry, so
  one codegen toolchain covers both directions.
- CME's specs: [cmegroup.com — Market Data Platform](https://www.cmegroup.com/market-data/distributor/market-data-platform.html).
- Note the CME quirk that matters for a market maker: many CME products historically
  matched **pro-rata or with allocation algorithms other than pure FIFO**
  (see [arXiv:1205.3051](https://arxiv.org/abs/1205.3051)). Check the matching algorithm
  per product — it changes the optimiser completely.

### Order entry
| Venue family | Protocol |
|---|---|
| Nasdaq | **OUCH** (binary, order entry), SoupBinTCP session layer |
| CME | **iLink 3** (SBE) |
| Most others | **FIX 4.2/4.4/5.0 SP2** — verbose, slower to encode; use a binary alternative where offered |
| Crypto | REST + WebSocket, increasingly FIX (Binance, OKX, Coinbase, Deribit all offer FIX now) |

## 3. Where to get the data — free sources only

**This project uses free data only.** The paid rows are listed for completeness and
because the literature uses them, but nothing in the roadmap requires buying data.

| Source | Cost | What | Notes |
|---|---|---|---|
| **[Bitstamp](https://www.bitstamp.net/websocket/v2/) `live_orders` + `live_trades`** | **Free** | Genuine L3: `order_created` / `order_changed` / `order_deleted`, each with an order id, for the **whole book** | **What this project uses.** No account, no API key, no card — verified by capture, not by reading the docs. Recorded with `tools/record_bitstamp.py`, decoded by `include/lob/feed/bitstamp.hpp`. See §3.1 for what the feed actually carries; it is better than expected. |
| **Crypto exchange public feeds (L2)** | **Free** | Binance/OKX/Bybit public WebSocket L2 + trades; Binance publishes free historical dumps | Fine for a live event loop and for volatility work. **Cannot** give queue position: an L2 diff says a level shrank, never which order left or why. |
| **[Bitfinex](https://docs.bitfinex.com/reference/ws-public-books) raw books (`prec: R0`)** | **Free** | Per-order updates with ids | Real L3, but **windowed to the top 250 per side**. An order leaving the window is indistinguishable from a cancel, which silently corrupts the cancel/fill split. `tools/record_bitfinex.py` exists; prefer Bitstamp. |
| **[LOBSTER](https://lobsterdata.com/) sample data** | **Free** | Message + orderbook CSVs for a few Nasdaq tickers and days | Nanosecond timestamps, and the format the literature reports against — useful for checking a reconstruction against published results. ([LOBSTER paper, SSRN](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=1977207)) |
| **FI-2010** | **Free** | Benchmark ML dataset, 10 days, 5 Nasdaq Nordic stocks | Only for comparing against published ML numbers. No order-level data. |
| ~~Coinbase `level3` / `full`~~ | — | — | **Checked and ruled out.** The retail Advanced Trade WebSocket has no `level3` channel at all, and `level2` now requires authentication. The `full` channel lives on Coinbase Exchange, which is application-gated and institutional. Listed because it is the first thing everyone tries. |
| ~~[Databento](https://databento.com/) samples~~ | — | Sample MBO for Nasdaq TotalView-ITCH and CME MDP 3.0 | Excellent data — NY4 capture, FPGA hardware timestamps, PTP-synced. Dropped here because sign-up wanted card details, and this project spends nothing. Revisit if equities/futures MBO becomes necessary; the decoder interface is designed so that is a new file, not a change. |
| ~~Tardis.dev, LOBSTER full, exchange direct~~ | Paid | Normalised historical crypto L2/L3; academic pricing; entitlements + colocation | Out of scope. |

On FI-2010 specifically: it is free, but too small and too old for anything except
reproducing published ML numbers — and see [arXiv:2308.01915](https://arxiv.org/abs/2308.01915)
on how badly models trained on it generalise.

### 3.1 What the Bitstamp L3 feed actually carries

Written after decoding a real capture, and it differs from what the documentation
alone suggested. Every `live_orders` message carries:

| Field | Meaning |
|---|---|
| `amount_at_create` | size when the order was first booked |
| `amount` | size **remaining** right now |
| `amount_traded` | cumulative size **filled** so far |
| `event_id` / `pre_event_id` | a chain: each message names its predecessor |
| `microtimestamp` | exchange time, µs — but see the caveat below |

Three consequences, each of which changed the design:

**The cancel/fill split is exact and needs no join.** The original plan was to match
`order_deleted` against the trade channel on order id — the approach
[ob-analytics#28](https://github.com/phil8192/ob-analytics/issues/28) warns is unreliable.
`amount_traded` makes it a subtraction instead. Since §5 of
[03-metrics-and-estimators.md](03-metrics-and-estimators.md) makes this split
non-negotiable — volume *cancelled* ahead of you is free progress up the queue, volume
*traded* ahead of you is progress **plus** the information that someone is buying — this
is the single most valuable thing the feed provides. In a 10-minute btcusd capture
roughly **0.5% of removals were fills**; the other 99.5% were cancels. Aggregating them
would hide almost the entire mechanism.

**Gaps are detectable rather than suspected.** `pre_event_id` of each message equals
`event_id` of the last. A capture that chains end to end is *provably* whole, which is
what makes "a full session with zero invariant violations" a meaningful claim. The
decoder counts breaks; `replay` reports them before anything derived from the data.

**The clocks do not agree, so never join on time.** In one capture the order channel
arrived ~570 ms *after* its own exchange timestamp while the trade channel arrived ~88 ms
*before* its own — the two channels are not on a common clock, and neither is on the
local one. Worse, order-channel `microtimestamp` values are always whole milliseconds:
the resolution is 1 ms wearing a microsecond coat, so many events share a timestamp and
intra-millisecond ordering must come from the chain. Any duration derived from these
numbers is quantised to 1 ms and must be reported that way. A constant offset does cancel
out of a *spread*, so per-channel delay **variation** is still a real measurement — that
is what `replay` reports, per channel, never pooled.

**One structural caveat.** Orders rest very far from the touch — a bid at \$71,743 against
a \$79,715 touch is 800,000 ticks away at Bitstamp's \$0.01 tick. The book is a flat array
over a tick window (see [00-scope-and-architecture.md](00-scope-and-architecture.md)), so
it is banded around the touch and everything outside is **excluded and counted**, never
silently absorbed. `replay` prints what fraction of adds fell outside. For market making
this loses nothing that matters; for a depth study it would, and the number is printed so
that judgement is the reader's.

**Recommended path:** record Bitstamp L3 with `tools/record_bitstamp.py`, replay it with
`replay --bitstamp`, and keep a short slice in `data/samples/` as a committed regression
fixture. Synthetic flow only ever proves the decoder agrees with the generator that
produced it.

## 4. PCAP and the timestamp hierarchy

For latency research, raw **PCAP with hardware timestamps** is the gold standard, because
it is the only place you see the packet as it arrived on the wire, before any of your
software touched it.

Four clocks, in increasing distance from truth:

| Timestamp | Source | What it tells you |
|---|---|---|
| `ts_exchange` | In the message payload, set by the matching engine | When the exchange thought the event happened |
| `ts_wire` | NIC hardware timestamp (`SO_TIMESTAMPING`, or the capture card) | When the packet hit your rack |
| `ts_kernel` | Kernel software timestamp | Adds network-stack queueing |
| `ts_app` | `rdtscp` in your feed handler | Adds your own scheduling/wakeup latency |

The **differences between adjacent rows are the diagnosis**:
- `ts_wire − ts_exchange` = network + exchange publication delay (and clock skew — you
  need PTP for this to mean anything).
- `ts_kernel − ts_wire` = network stack queueing. Large ⇒ you need kernel bypass.
- `ts_app − ts_kernel` = your scheduling latency. Large ⇒ core isolation / busy-poll problem.

**Clock discipline, scaled to what you're doing.** The rule is: *any latency measured
across two machines needs PTP, or it is measuring clock drift rather than latency.*

For this project that mostly doesn't arise. Everything runs on one machine, so
`CLOCK_MONOTONIC_RAW` and the TSC are internally consistent and cost nothing — no PTP
daemon, no grandmaster, no GPS card. The one case where it does arise is comparing
*your* receive timestamp against the *exchange's* timestamp inside the message: those
clocks are unrelated, so that difference is not a latency measurement and should not be
reported as one. Measure it if you like, label it as an offset of unknown sign.

If this ever became a colocated system, PTP (`linuxptp`: `ptp4l` + `phc2sys`) against a
grandmaster or a GPS/PPS-disciplined card is the correct answer. Recorded here so the
gap is a known one, not an oversight.

## 5. Storage

| Format | Use |
|---|---|
| **Raw PCAP** | Archive of truth. Big. Keep the days that matter. |
| **DBN** (Databento) | Compact, streaming-friendly, has official C++/Rust/Python readers. Good default for captured market data. |
| **Custom flat binary** | Fixed-size POD records, `mmap`-able, hugepage-friendly. This is what your replay tool should read — you want the parse cost to be zero so replay measures the book, not the parser. |
| **Parquet / Arrow** | For the *derived* research tables (per-fill records, feature matrices, calibration inputs). Use **polars** over pandas; the row counts are large. |

Order of magnitude for planning: one liquid US equity, one day, MBO ≈ **10⁶–10⁷ messages**.
All of Nasdaq for one day ≈ **10⁹ messages, tens of GB compressed**. A year of one CME
front-month contract in MBO is a few hundred GB. Budget disk accordingly, and do your
calibration on sampled days before you commit to a full-history run.

## 6. Reference data and the boring things that break you

- **Tick size regimes** (US Reg NMS sub-penny rules, MiFID II tick tables, venue-specific
  tables). The tick size is an input to every model in `01-literature.md` §J.
- **Trading calendars, halts, auctions, LULD bands.** Opening and closing auctions have
  completely different dynamics and will contaminate every distribution you fit if you
  don't exclude or model them separately.
- **Symbol mapping and corporate actions.** Splits will silently destroy a calibration.
- **Fee schedules.** Maker rebates vs taker fees decide whether a strategy is viable at
  all — a maker-rebate venue changes the optimal spread. Model fees per-venue, per-tier,
  explicitly, from day one.
- **Self-match prevention rules** — these differ by venue and can cancel your resting
  order when you don't expect it.
