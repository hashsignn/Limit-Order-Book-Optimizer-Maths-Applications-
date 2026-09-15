# Long-capture measurements

The derived CSVs from captures too long to commit whole. An eight-hour
recording is 60 to 140 MB per instrument as a raw stream, past what a git
repository should hold and past GitHub's per-file limit for one of them. The
raw stream stays with whoever recorded it; what travels is what `apps/stats`
measured from it, which is what every statistic downstream actually reads.

## What to put here

Run `apps/stats` over the capture and copy the CSVs it writes:

```bash
git pull                                  # gap.csv and --seed-guard-pct are recent
cmake --preset release && cmake --build build/release

./build/release/stats --capture-dir <dir-with-the-hourly-files> \
    --band-pct 0.10 --label ethusd8 --outdir data/capture8h \
    2> data/capture8h/ethusd8_stats.log
```

`--capture-dir`, not `--capture`: a long recording rotates hourly and is seeded
by the single snapshot beside the first file. `--band-pct 0.10`, because the
0.02 default is a 2% price window and eight hours of crypto walks out of it —
the tool warns if the touch reached the edge, and that warning is in the log.

## What each file is for, and which ones matter

| File | Size at 8 h | What reads it |
|---|---|---|
| `*_qr.csv` | tens of KB | The queue-reactive estimator: per-level intensities and the touch queue's size distribution. **The one that matters most.** |
| `*_depth.csv` | ~2 KB | Occupancy by tick distance behind the touch |
| `*_gap.csv` | ~2 KB | Distance from the touch to the next price holding anything |
| `*_trades.csv` | ~500 KB | Prints, for impact and markouts |
| `*_mid.csv` | ~6-10 MB | The mid path, for volatility and the spread |
| `*_orders.csv` | 30-60 MB | Order lifetimes and the fill hazard. Skip it if the push is slow |
| `*_arrivals.csv` | ~6-10 MB | Inter-arrival gaps. Optional — Bitstamp timestamps to the millisecond, so the sub-millisecond structure this would show is below the feed's resolution |
| `*_stats.log` | a few KB | The run's own diagnostics: session count, chain gaps, and the share of prints that landed in front of the reconstructed touch, which is `docs/KNOWN-ISSUES.md` issue 1 |

Ten minutes cannot settle the queue-reactive shapes — the slopes against queue
size disagree across instruments at one standard error, and two of the three
produced too few trades to fit at all. Eight hours is what settles them.
