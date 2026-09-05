#!/usr/bin/env python3
"""Draws the measurement-plane figures from the CSVs that apps/stats emits.

    ./build/stats --capture data/samples/<pair>_bitstamp.jsonl.gz --outdir csv
    py tools/figures.py --csv csv --out figures

Nothing here reconstructs a book. Every number plotted came out of the C++
pipeline replaying real Bitstamp level-3 data, and this file only draws it —
a second implementation of the book would be a second thing that can be wrong.

Palette is validated for colour-vision deficiency rather than chosen by eye:
blue / orange / magenta, worst all-pairs deutan dE 10.3, normal-vision 16.6.
Green was the obvious third hue and fails against orange at dE 4.9.
"""
import argparse
import pathlib
import sys

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

BLUE, ORANGE, MAGENTA = "#1F6FA8", "#B4571C", "#A8256E"
PAIR_C = {"btcusd": BLUE, "ethusd": ORANGE, "xrpusd": MAGENTA}
INK, INK2, INK3 = "#151A20", "#4A5764", "#8A94A0"
GRID, SURF = "#DFE4E9", "#FCFCFB"
CANCEL_F, CANCEL_E = "#E7EBEF", "#9AA6B2"      # the 99% mass: recessive
FILL_C = ORANGE                                 # the 1% that matters: loud

plt.rcParams.update({
    "figure.facecolor": SURF, "axes.facecolor": SURF, "savefig.facecolor": SURF,
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans"],
    "font.size": 9, "axes.titlesize": 10.5, "axes.labelsize": 9,
    "axes.edgecolor": GRID, "axes.labelcolor": INK2, "text.color": INK,
    "xtick.color": INK3, "ytick.color": INK3, "xtick.labelsize": 8, "ytick.labelsize": 8,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.7, "axes.axisbelow": True,
    "legend.frameon": False, "legend.fontsize": 8.5,
    "figure.dpi": 130, "savefig.dpi": 130, "savefig.bbox": "tight",
})


def header(fig, title, sub, top=0.80):
    """Title block above the axes, with the space actually reserved for it.

    suptitle plus a second text at overlapping y is the classic way to get a
    subtitle drawn through a title; the space has to be taken out of the axes.
    """
    fig.subplots_adjust(top=top)
    fig.text(0.008, 0.985, title, ha="left", va="top", fontsize=12.5,
             fontweight="bold", color=INK)
    fig.text(0.008, 0.90, sub, ha="left", va="top", fontsize=8.4, color=INK3)


def style(ax, title=None, sub=None, xlabel=None, ylabel=None):
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)
    if title:
        ax.set_title(title, loc="left", color=INK, fontweight="bold", pad=14 if sub else 8)
    if sub:
        ax.text(0, 1.02, sub, transform=ax.transAxes, color=INK3, fontsize=8.2, va="bottom")
    ax.set_xlabel(xlabel or "")
    ax.set_ylabel(ylabel or "")


def load(csvdir, pair, name):
    p = pathlib.Path(csvdir) / f"{pair}_{name}.csv"
    return pd.read_csv(p) if p.exists() else None


def survival(x):
    """Empirical P(T > t): the fraction still alive at each observed time."""
    x = np.sort(np.asarray(x, dtype=float))
    return x, 1.0 - np.arange(len(x)) / len(x)


# ---------------------------------------------------------------- figures
def fig_lifetime(pairs, data, out):
    fig, axes = plt.subplots(1, len(pairs), figsize=(4.1 * len(pairs), 3.5), sharey=True)
    axes = np.atleast_1d(axes)
    for ax, pair in zip(axes, pairs):
        o = data[pair]["orders"]
        o = o[o.lifetime_ms > 0]
        cancelled = o[o.filled == 0].lifetime_ms
        filled = o[o.filled > 0].lifetime_ms
        for series, colour, label in ((cancelled, CANCEL_E, "cancelled"), (filled, FILL_C, "filled")):
            if len(series) < 5:
                continue
            t, s = survival(series)
            ax.step(t / 1000.0, s, where="post", color=colour, lw=2.0,
                    label=f"{label}  n={len(series):,}")
        ax.set_xscale("log"); ax.set_yscale("log")
        ax.set_xlim(1e-3, 700)
        style(ax, pair.upper(), None, "order lifetime (s, log)",
              "fraction still resting" if pair == pairs[0] else None)
        ax.legend(loc="lower left")
    header(fig, "How long an order lives, and whether it ends in a fill or a cancel",
           "Survival curves, log-log. A filled order is one that traded at all, whole or in part.\n"
           "Real Bitstamp level-3, ten minutes per instrument.", top=0.76)
    fig.savefig(out / "01_order_lifetime.png"); plt.close(fig)


def fig_split(pairs, data, out):
    """The thesis figure: what fraction of removed volume actually traded.

    Drawn as a share on a log axis rather than a 100% stacked bar. The quantity
    is between 0.01% and 1%, and against a 0-100 axis it is a sliver one pixel
    wide — the chart would hide exactly the thing it exists to show.
    """
    edges = np.array([0, 1, 2, 4, 8, 16, 32, 64, 128, 100000])
    labels = ["0", "1", "2-3", "4-7", "8-15", "16-31", "32-63", "64-127", "128+"]
    fig, ax = plt.subplots(figsize=(8.4, 4.3))
    y = np.arange(len(labels))
    for j, pair in enumerate(pairs):
        o = data[pair]["orders"]
        idx = np.clip(np.digitize(o.dist_ticks.values, edges) - 1, 0, len(labels) - 1)
        f = np.zeros(len(labels)); c = np.zeros(len(labels))
        np.add.at(f, idx, o.filled.values.astype(float))
        np.add.at(c, idx, o.cancelled.values.astype(float))
        tot = f + c
        share = np.divide(f, tot, out=np.full_like(f, np.nan), where=tot > 0) * 100
        off = (j - (len(pairs) - 1) / 2) * 0.24
        live = np.isfinite(share) & (share > 0)
        ax.scatter(share[live], y[live] + off, s=42, color=PAIR_C[pair], zorder=3,
                   label=f"{pair}  {100*o.filled.sum()/(o.filled.sum()+o.cancelled.sum()):.2f}% overall")
        # A bucket where nothing traded at all is a real answer, not missing data.
        zero = np.isfinite(share) & (share == 0)
        ax.scatter(np.full(zero.sum(), 1.2e-3), y[zero] + off, s=26, facecolor="none",
                   edgecolor=PAIR_C[pair], lw=1.1, zorder=3)
    for i in y:
        ax.axhline(i, color=GRID, lw=0.7, zorder=0)
    ax.set_xscale("log")
    ax.set_xlim(1e-3, 30)
    ax.set_yticks(y); ax.set_yticklabels(labels)
    ax.invert_yaxis(); ax.xaxis.grid(False); ax.yaxis.grid(False)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}%"))
    ax.text(1.2e-3, -0.75, "hollow = nothing traded there at all", fontsize=7.6, color=INK3)
    style(ax, None, None, "share of removed volume that FILLED (log)",
          "ticks behind the touch on arrival")
    ax.legend(loc="lower right")
    header(fig, "Almost nothing that leaves the book leaves because it traded",
           "Volume cancelled ahead of you is free progress up the queue; volume TRADED ahead of you is progress "
           "plus the information\nthat someone is buying. They are different signals, and a model that aggregates "
           "them is modelling neither.", top=0.80)
    fig.savefig(out / "02_cancel_vs_fill.png"); plt.close(fig)


def fig_spread(pairs, data, out):
    fig, ax = plt.subplots(figsize=(7.6, 4.0))
    for pair in pairs:
        m = data[pair]["mid"]
        sp = (m.ask - m.bid).values
        sp = sp[(sp > 0) & (sp <= 60)]
        vals, counts = np.unique(sp, return_counts=True)
        at_one = 100 * (sp == 1).sum() / len(sp)
        ax.plot(vals, 100 * np.cumsum(counts) / len(sp), color=PAIR_C[pair], lw=2.0,
                marker="o", ms=3.5, label=f"{pair}  {at_one:.0f}% of the time at one tick")
    ax.set_xscale("log"); ax.set_xlim(0.9, 60); ax.set_ylim(0, 101)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))
    style(ax, None, None, "spread (ticks, log)", "cumulative % of time at or below")
    ax.legend(loc="lower right")
    header(fig, "Spread decides which optimiser Phase 5 has to build",
           "A spread pinned at one tick is the large-tick signature, and points at value iteration on a discretised MDP.\n"
           "A spread that floats is small-tick, and points at an HJB grid solve. These are different code paths.\n"
           "Weighted by TIME, on a 100 ms grid. replay weights by event instead, which is why its medians differ.",
           top=0.74)
    fig.savefig(out / "03_spread.png"); plt.close(fig)


def fig_depth(pairs, data, out):
    fig, axes = plt.subplots(1, len(pairs), figsize=(4.1 * len(pairs), 3.4))
    axes = np.atleast_1d(axes)
    for ax, pair in zip(axes, pairs):
        d = data[pair]["depth"]
        qd = 8
        for side, colour, label in ((0, BLUE, "bid"), (1, ORANGE, "ask")):
            s = d[d.side == side].sort_values("dist_ticks")
            ax.plot(s.dist_ticks, s.mean_qty / 10 ** qd, color=colour, lw=2.0, label=label)
        ax.set_yscale("log")
        style(ax, pair.upper(), None, "ticks from the touch",
              "mean resting size (base ccy)" if pair == pairs[0] else None)
        ax.legend(loc="upper right")
    header(fig, "Depth behind the touch",
           "Levels near the touch are reconstructed from the live stream; only snapshot levels beyond 0.3% of mid\n"
           "are seeded. Depth past 40 ticks is not drawn.", top=0.76)
    fig.savefig(out / "04_depth_profile.png"); plt.close(fig)


def fig_arrivals(pairs, data, out):
    fig, ax = plt.subplots(figsize=(7.6, 4.0))
    for pair in pairs:
        g = data[pair]["arrivals"].gap_us.values.astype(float)
        g = g[g > 0]
        t, s = survival(g / 1000.0)
        k = max(1, len(t) // 4000)
        ax.plot(t[::k], s[::k], color=PAIR_C[pair], lw=2.0,
                label=f"{pair}  n={len(g):,}  median {np.median(g)/1000:.0f} ms")
    ax.set_xscale("log"); ax.set_yscale("log"); ax.set_xlim(0.5, 3e4)
    style(ax, None, None, "gap between events (ms, log)", "fraction of gaps longer")
    ax.axvline(1.0, color=INK3, lw=0.8, ls=(0, (4, 3)))
    ax.text(1.06, 1.4e-4, "1 ms feed resolution", color=INK3, fontsize=7.6, rotation=90, va="bottom")
    ax.legend(loc="lower left")
    header(fig, "Time between consecutive events",
           "Exchange timestamps are whole milliseconds, so nothing below 1 ms is measured.\n"
           "The flat left edge is the clock, not the market.", top=0.80)
    fig.savefig(out / "05_interarrival.png"); plt.close(fig)


def fig_markout(pairs, data, out):
    """Mid drift after a print, signed by the aggressor.

    Three things this got wrong on the first pass, all of them the kind of error
    that produces a confident-looking chart:

      Ticks are not comparable across instruments. A tick is $0.01 on a $79,829
      book and $0.00001 on a $1.40 one, so plotting all three in ticks put btcusd
      three orders of magnitude above the others and hid them against the axis.
      That is a dual-scale chart wearing one axis. Basis points of mid fixes it.

      Long horizons on a short sample measure drift, not information. btcusd's
      mid moved +1634 ticks over the session; a 30-second markout of ~900 ticks
      was more than half the whole session's drift. Ten minutes supports a few
      seconds of horizon, not thirty.

      Overlapping windows are not independent, so an IID bootstrap CI is far too
      narrow. Markouts from prints seconds apart share almost all their path.
      Resampling contiguous BLOCKS keeps that autocorrelation, which is what
      block_bootstrap() in strat/pnl.hpp exists for on the P&L side.
    """
    HOR = np.array([0.1, 0.25, 0.5, 1, 2, 5])
    fig, ax = plt.subplots(figsize=(8.0, 4.2))
    notes = []
    for pair in pairs:
        tr, m = data[pair]["trades"], data[pair]["mid"]
        if tr is None or len(tr) < 20:
            continue
        mt = m.t_ms.values.astype(float)
        mid = (m.bid.values + m.ask.values) / 2.0
        t0 = tr.t_ms.values.astype(float)
        base = np.interp(t0, mt, mid)
        sign = np.where(tr.side.values == 0, 1.0, -1.0)      # taker buy = +1
        buy_share = 100 * (tr.side.values == 0).mean()
        drift_bps = 1e4 * (mid[-1] - mid[0]) / mid[0]
        notes.append(f"{pair}: {len(tr)} prints, {buy_share:.0f}% taker-buy, "
                     f"session drift {drift_bps:+.1f} bps")

        rng = np.random.default_rng(7)
        means, los, his = [], [], []
        for h in HOR:
            fwd = np.interp(t0 + h * 1000.0, mt, mid)
            mo = 1e4 * sign * (fwd - base) / base            # basis points of mid
            mo = mo[np.isfinite(mo)]
            means.append(mo.mean())
            blk = max(4, len(mo) // 20)
            nblk = max(1, len(mo) // blk)
            starts = rng.integers(0, max(1, len(mo) - blk), size=(500, nblk))
            draws = np.stack([np.concatenate([mo[s:s + blk] for s in row]) for row in starts])
            bs = draws.mean(axis=1)
            los.append(np.percentile(bs, 2.5)); his.append(np.percentile(bs, 97.5))
        ax.fill_between(HOR, los, his, color=PAIR_C[pair], alpha=0.14, lw=0)
        ax.plot(HOR, means, color=PAIR_C[pair], lw=2.0, marker="o", ms=4.5, label=pair)

    ax.axhline(0, color=INK2, lw=1.1)
    ax.set_xscale("log")
    ax.set_xticks(HOR)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))
    style(ax, None, None, "horizon after the print (s, log)",
          "mid move, bps, signed by taker direction")
    ax.legend(loc="upper left")
    ax.text(0.99, 0.03, "\n".join(notes), transform=ax.transAxes, ha="right", va="bottom",
            fontsize=7.4, color=INK3, linespacing=1.5)
    header(fig, "Markout: where the mid goes after a print",
           "Positive means the taker was right and whoever was resting got adversely selected. Bands are 95% BLOCK "
           "bootstrap —\noverlapping windows share most of their path, so an IID interval would be far too narrow. "
           "Ten minutes per instrument\nsupports a few seconds of horizon and no more; ethusd's flow is 76% one-sided, "
           "so its sign carries that, not information.", top=0.71)
    fig.savefig(out / "06_markout.png"); plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="csv")
    ap.add_argument("--out", default="figures")
    a = ap.parse_args()
    out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True)

    pairs = sorted({p.name.split("_")[0] for p in pathlib.Path(a.csv).glob("*_orders.csv")},
                   key=lambda x: ["btcusd", "ethusd", "xrpusd"].index(x)
                   if x in ("btcusd", "ethusd", "xrpusd") else 99)
    if not pairs:
        sys.exit(f"no *_orders.csv in {a.csv} — run ./build/stats first")

    data = {p: {n: load(a.csv, p, n)
                for n in ("orders", "trades", "mid", "depth", "arrivals")} for p in pairs}

    for fn in (fig_lifetime, fig_split, fig_spread, fig_depth, fig_arrivals, fig_markout):
        fn(pairs, data, out)
        print(f"  {fn.__name__}")

    print(f"\n{len(pairs)} instruments -> {out}/")
    for p in pairs:
        o = data[p]["orders"]
        tot = o.filled.sum() + o.cancelled.sum()
        print(f"  {p:8s} {len(o):>6,} orders  "
              f"{100*o.filled.sum()/tot:5.2f}% of removed volume filled  "
              f"median life {o.lifetime_ms.median():.0f} ms")


if __name__ == "__main__":
    main()
