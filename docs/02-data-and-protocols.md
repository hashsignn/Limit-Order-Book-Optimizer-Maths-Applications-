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

## 3. Where to get the data

| Source | What | Notes |
|---|---|---|
| **[Databento](https://databento.com/)** | Nasdaq TotalView-ITCH (`XNAS.ITCH`), CME Globex MDP 3.0 (`GLBX.MDP3`), ICE, and ~15 US equity exchanges | **Best starting point.** MBO/MBP-10/MBP-1 schemas, usage-based pricing, official C++/Python/Rust clients, and their own binary **DBN** format. Captured at Equinix NY4 with **FPGA hardware timestamping, PTP-synced** — so their timestamps are trustworthy for latency work. Also sells **raw PCAPs** if you want to test your own decoder against the wire format. Free sample files per dataset. |
| **[LOBSTER](https://lobsterdata.com/)** | Reconstructed Nasdaq LOB from ITCH, 2007→present, all Nasdaq tickers | Two CSVs per ticker-day: a **message file** (event-by-event) and an **orderbook file** (level snapshots), nanosecond timestamps. Academic pricing. The standard dataset in the literature — use it so your results are comparable. ([LOBSTER paper, SSRN](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=1977207)) |
| **Exchange direct** | Nasdaq, CME, Cboe historical data products | Cheapest per byte at volume, most work. You need colocation or a vendor to get the real-time version anyway. |
| **Crypto exchanges (free)** | Binance/OKX/Bybit/Deribit WebSocket L2 + trades; Binance publishes historical dumps | **Start here if you want to iterate this week.** No entitlement cost, no colocation, real fills achievable with small capital. Crypto perps are large-tick and deep — a good fit for the queue-position problem. Caveat: most crypto venues publish **L2 diffs, not MBO**, so true queue position must be inferred. Deribit and a few others do better. |
| **[Tardis.dev](https://tardis.dev/)** | Historical crypto L2/L3 + tick data, normalised across venues | Commercial, saves a lot of scraping. |
| **FI-2010** | Benchmark LOB dataset (Nasdaq Nordic, 10 days, 5 stocks) | Only useful for comparing ML models to the literature. Too small and too old for anything else — and see [arXiv:2308.01915](https://arxiv.org/abs/2308.01915) on how badly models trained on it generalise. |

**Recommended path:** develop against **free crypto L2 + a Databento MBO sample** in
parallel. The crypto feed gives you a live loop and real fills; the Databento MBO sample
gives you a correct L3 book to build the queue logic against.

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

**Clock discipline is a prerequisite, not a detail.** Run PTP (`linuxptp`: `ptp4l` +
`phc2sys`) with a grandmaster, or a GPS/PPS-disciplined card. Cross-machine latency
measurements without PTP are measuring clock drift, not latency.

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
