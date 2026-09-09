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

**What it costs.** More than the one measurement first claimed here. That
claim was made by reading which column `trades.dist_ticks` feeds, and it missed
that a book with holes reports a wider spread than the market has:

| | clean 10-min capture | 8-hour capture |
|---|---|---|
| spread at one tick | 92% | **60%** |
| mid steps kept by the `max_spread` filter | 85% | **61%** |

So the spread distribution is degraded and 39% of mid steps are discarded. What
survives is sound — the median mid move is 1 tick, P(up) 1.6%, and the imbalance
signal is the best measured on any instrument, running 0.7% to 2.7% monotonically
across the five buckets. ethusd still passes both usability checks. But it is
not true that only `level_ratio` was affected.

**No workaround for the level ratio.** Taking it from btcusd was the first plan
and it does not work: btcusd's own touch teleports, so its levels are just as
unreliable, and it reports 30.8% / 29.2% / 29.1% across three levels — flat,
when penetration must fall with depth. ethusd reports 62.1% / 61.4% / 61.0%.
`tools/mdp_params.py` now rejects a ratio that is flat across levels or above
0.5 for exactly this reason. Neither eight-hour capture measures it; it must be
given to `solve` explicitly with `--level-ratio` and defended.

**How to check a fix.** `stats --capture-dir <dir>` prints the share and the
histogram. A fix moves the 5–16 and 17+ rows toward zero; the 1-tick row should
not move, because that one is the channel race and no book change touches it.
