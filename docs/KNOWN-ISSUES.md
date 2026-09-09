# Known issues

Things measured, reproduced, and not yet fixed. An issue leaves this file when
it is fixed or when it is shown not to be real — not when it stops being
convenient.

---

## 1. ethusd's reconstructed book develops holes near the touch

**Found** 2026-09-09, on the 8-hour ethusd capture (1.62 M orders, 5,032 trades).

**Symptom.** 27.6% of prints landed *in front of* the touch we had
reconstructed: a taker buy printing below our best ask. That cannot happen
against a correct book — it means our best ask was worse than the real one, so
a level was missing.

**What it is not.** Two guesses, both disproved by measurement:

- *The aggressor's side is mislabelled.* No. The synthetic path did have that
  bug and it was fixed; the capture path takes the taker side straight from the
  feed, and btcusd through identical code reports 4.2%.
- *The price window is too narrow.* No. Widening `--band-pct` from 0.02 to 0.10
  left the count **identical at 1922**, and the touch then had 41%/49% headroom.

**What it is.** The distance histogram settles it:

| in front by | prints | share |
|---|---|---|
| 1 tick | 623 | 32% |
| 2–4 | 117 | 6% |
| 5–16 | 279 | 15% |
| **17+** | **903** | **47%** |

A one-tick gap is a message-ordering race between the `live_orders` and
`live_trades` channels — the delete reaches us before the print that caused it,
so the touch has already moved. That is a third of them and is not fixable from
the capture.

The other two thirds are levels genuinely absent from the book. At those moments
the reconstruction showed a spread of 17+ ticks on an instrument that sits at
one tick, so the touch was empty and the nearest level we knew about was far
away.

**Unverified hunch, recorded as a hunch.** ethusd moved 2.0% across the capture
against btcusd's 0.9%, so more of its range was price territory the book had
never seen. `BitstampConfig::seed_guard` also refuses to seed levels within 0.3%
of the snapshot touch, so that band starts empty and must fill from the stream.
Neither is measured. Do not repeat the mistake above and act on this before
testing it.

**What it costs.** One measurement, and only one: `level_ratio` in
`tools/mdp_params.py`, the share of traded volume that reaches one tick past the
touch. Nothing else reads `trades.dist_ticks`. The fill hazard by queue
position, mid dynamics, queue depth, imbalance and markouts all come from other
columns and are unaffected — ethusd's 1.62 M orders are usable.

**Workaround in force.** Take the level ratio from btcusd, which reports 4.2%
and carries 11,993 trades to ethusd's 5,032.

**How to check a fix.** `stats --capture-dir <dir>` prints the share and the
histogram. A fix moves the 5–16 and 17+ rows toward zero; the 1-tick row should
not move, because that one is the channel race and no book change touches it.
