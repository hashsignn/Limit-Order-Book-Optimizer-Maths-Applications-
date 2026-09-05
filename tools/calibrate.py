#!/usr/bin/env python3
"""Calibrates the Avellaneda-Stoikov fill intensity from real level-3 data.

    ./build/stats --capture data/samples/<pair>_bitstamp.jsonl.gz --outdir csv
    py tools/calibrate.py --csv csv --out figures

THE MODEL

    lambda(delta) = A * exp(-k * delta)

lambda is the rate at which a passive order resting delta ticks from the mid
gets hit. A is the intensity at the mid and k sets how fast that decays with
distance. Phase 4's strategies currently run on invented values for both, which
is why none of their P&L comparisons mean anything yet.

THE ESTIMATOR

Every resting order is an exposure and a fill is an event, so this is a Poisson
likelihood and nothing more:

    y_i ~ Poisson( T_i * A * exp(-k * delta_i) )

with T_i the order's lifetime. Fitted per order rather than on binned rates —
binning throws away information and makes the answer depend on bin edges — by
Newton-Raphson on (log A, k), with standard errors from the observed
information. Orders cancelled before filling are not missing data: they are
exposure without an event, which is exactly what a Poisson likelihood wants.

WHY THE RANGE MATTERS

delta runs to 160,000 ticks in these captures. Half of all btcusd orders rest
more than 1,191 ticks from the mid, contributing 71,000 order-seconds against 15
fills. Fitting an exponential across that range does not measure the decay a
market maker experiences; it measures the fact that nobody trades $12 away from
the touch. A-S is a model of the quotable region, so the fit is restricted to
one, the range is printed with the answer, and the fit is repeated at several
cutoffs so instability shows up instead of hiding.
"""
import argparse
import json
import pathlib

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

BLUE, ORANGE, MAGENTA = "#1F6FA8", "#B4571C", "#A8256E"
PAIR_C = {"btcusd": BLUE, "ethusd": ORANGE, "xrpusd": MAGENTA}
INK, INK2, INK3, GRID, SURF = "#151A20", "#4A5764", "#8A94A0", "#DFE4E9", "#FCFCFB"

plt.rcParams.update({
    "figure.facecolor": SURF, "axes.facecolor": SURF, "savefig.facecolor": SURF,
    "font.family": "sans-serif", "font.sans-serif": ["DejaVu Sans"],
    "font.size": 9, "axes.labelsize": 9, "axes.edgecolor": GRID, "axes.labelcolor": INK2,
    "text.color": INK, "xtick.color": INK3, "ytick.color": INK3,
    "xtick.labelsize": 8, "ytick.labelsize": 8,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.7, "axes.axisbelow": True,
    "legend.frameon": False, "legend.fontsize": 8.5,
    "figure.dpi": 130, "savefig.dpi": 130, "savefig.bbox": "tight",
})


def poisson_fit(delta, expo, events, iters=100):
    """MLE for lambda_i = exp(a - k*delta_i) with exposure expo.

    Returns (A, k, se_A, se_k, loglik). Newton-Raphson: the log-likelihood is
    concave in (a, k), so this converges from almost anywhere and there is no
    optimiser dependency to explain away.
    """
    a, k = np.log(max(events.sum(), 1) / max(expo.sum(), 1e-9)), 0.0
    for _ in range(iters):
        mu = expo * np.exp(a - k * delta)
        ga = (events - mu).sum()
        gk = (-events * delta + delta * mu).sum()
        haa = -mu.sum()
        hak = (delta * mu).sum()
        hkk = -(delta ** 2 * mu).sum()
        det = haa * hkk - hak * hak
        if abs(det) < 1e-300:
            break
        da = -(hkk * ga - hak * gk) / det
        dk = -(-hak * ga + haa * gk) / det
        step = 1.0
        while step > 1e-4 and (abs(dk * step) > 2.0 or abs(da * step) > 5.0):
            step *= 0.5
        a += da * step
        k += dk * step
        if abs(da * step) < 1e-10 and abs(dk * step) < 1e-10:
            break
    mu = expo * np.exp(a - k * delta)
    haa, hak, hkk = -mu.sum(), (delta * mu).sum(), -(delta ** 2 * mu).sum()
    det = haa * hkk - hak * hak

    # Covariance is the inverse of the NEGATIVE Hessian — the observed
    # information — not the inverse of the Hessian. Getting that sign wrong
    # makes every variance negative, and clamping the negatives to zero (which
    # the first version of this function did) reports a standard error of
    # exactly 0.0000 for every parameter. A zero standard error on 15 events is
    # not a tight fit, it is a broken one, and it should have been obvious.
    if abs(det) < 1e-300:
        var_a = var_k = float("nan")
    else:
        inv = np.array([[hkk, -hak], [-hak, haa]]) / det
        cov = -inv
        var_a, var_k = float(cov[0, 0]), float(cov[1, 1])
        if var_a < 0 or var_k < 0:          # not a maximum: say so, do not hide it
            var_a = var_k = float("nan")
    ll = float((events * np.log(np.maximum(mu, 1e-300)) - mu).sum())
    A = float(np.exp(a))
    return A, float(k), A * np.sqrt(var_a), float(np.sqrt(var_k)), ll


def prepare(csvdir, pair):
    o = pd.read_csv(pathlib.Path(csvdir) / f"{pair}_orders.csv")
    # Distance from the MID, which is what A-S measures against. dist_ticks is
    # measured from the same-side touch, so half the spread closes the gap.
    o["delta"] = o.dist_ticks + o.spread_ticks / 2.0
    o["T"] = o.lifetime_ms / 1000.0
    o["y"] = (o.filled > 0).astype(float)
    # A negative delta is an order posted INSIDE the spread. Legitimate, but A-S
    # is defined for a quote placed away from the mid and has nothing to say
    # about one placed through it, so those are excluded and counted.
    return o


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="csv")
    ap.add_argument("--out", default="figures")
    ap.add_argument("--dmax", type=float, default=32.0, help="fit range, ticks from mid")
    a = ap.parse_args()
    out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True)

    pairs = [p for p in ("btcusd", "ethusd", "xrpusd")
             if (pathlib.Path(a.csv) / f"{p}_orders.csv").exists()]
    results, data = {}, {}

    print(f"\nAvellaneda-Stoikov fill intensity   lambda(d) = A exp(-k d),  d in ticks from mid")
    print(f"Poisson MLE over {len(pairs)} instruments, fit range 0 < d <= {a.dmax:g} ticks\n")
    print(f"{'pair':8s} {'A (/s)':>16s} {'k (/tick)':>18s} {'1/k':>9s} {'fills':>6s} {'exposure':>10s}")
    print("-" * 74)

    for p in pairs:
        o = prepare(a.csv, p)
        data[p] = o
        m = (o.delta > 0) & (o.delta <= a.dmax) & (o["T"] > 0)
        d, T, y = o.delta[m].values, o["T"][m].values, o["y"][m].values
        A, k, seA, sek, ll = poisson_fit(d, T, y)
        results[p] = dict(A=A, k=k, se_A=seA, se_k=sek, loglik=ll, fills=int(y.sum()),
                          exposure_s=float(T.sum()), n_orders=int(m.sum()), dmax=a.dmax,
                          inside_spread_excluded=int(((o.delta <= 0)).sum()))
        inv_k = f"{1/k:8.1f}" if k > 1e-9 else "       —"
        print(f"{p:8s} {A:9.4f} ±{seA:<6.4f} {k:10.5f} ±{sek:<7.5f} "
              f"{inv_k} {int(y.sum()):6d} {T.sum():9.0f}s")

    # ---- is k stable, or an artefact of where the range was cut? ----
    print("\nk against fit range — a stable k is a k worth quoting")
    cuts = [4, 8, 16, 32, 64, 128]
    print(f"{'pair':8s}" + "".join(f"{f'd<={c}':>12s}" for c in cuts))
    stability = {}
    for p in pairs:
        o = data[p]
        row, ks = [], []
        for c in cuts:
            m = (o.delta > 0) & (o.delta <= c) & (o["T"] > 0)
            if (o["y"][m] > 0).sum() < 3:
                row.append("     too few"); ks.append(np.nan); continue
            _, k, _, sek, _ = poisson_fit(o.delta[m].values, o["T"][m].values, o["y"][m].values)
            row.append(f"{k:12.4f}"); ks.append(k)
        ks = np.array(ks, dtype=float)
        good = np.isfinite(ks)
        # A k that changes SIGN across cutoffs is not a wobbly estimate, it is a
        # different model — intensity rising with distance. A max/min ratio
        # silently turns that into a small positive number, which is how the
        # first version printed "stable" for a k that ran from 48 to -0.019.
        neg = bool((ks[good] <= 0).any())
        rng = (float(np.nanmax(ks[good]) / np.nanmin(ks[good]))
               if good.sum() > 1 and not neg else float("nan"))
        stability[p] = dict(ratio=rng, sign_change=neg,
                            k_by_cutoff={int(c): (None if not np.isfinite(v) else float(v))
                                         for c, v in zip(cuts, ks)})
        print(f"{p:8s}" + "".join(row))
    print("\nverdict:")
    for p in pairs:
        st = stability[p]
        if st["sign_change"]:
            v = "NOT IDENTIFIED — k changes sign, so the model inverts"
        elif not np.isfinite(st["ratio"]):
            v = "NOT IDENTIFIED — too few fills to fit"
        elif st["ratio"] < 3:
            v = f"{st['ratio']:.1f}x — usable"
        elif st["ratio"] < 10:
            v = f"{st['ratio']:.1f}x — SHAKY, quote it with the range"
        else:
            v = f"{st['ratio']:.1f}x — NOT IDENTIFIED"
        print(f"  {p:8s} {v}")

    # ---- does the exponential shape actually hold? ----
    print("\nempirical lambda by delta bucket, against the fit")
    edges = np.array([0, 1, 2, 4, 8, 16, 32])
    for p in pairs:
        o, r = data[p], results[p]
        print(f"  {p}")
        prev = None
        mono = True
        for lo, hi in zip(edges[:-1], edges[1:]):
            m = (o.delta > lo) & (o.delta <= hi) & (o["T"] > 0)
            ev, ex = (o["y"][m] > 0).sum(), o["T"][m].sum()
            if ex <= 0:
                continue
            emp = ev / ex
            mid = (lo + hi) / 2
            pred = r["A"] * np.exp(-r["k"] * mid)
            if prev is not None and emp > prev * 1.25:
                mono = False
            prev = emp
            print(f"    d {lo:>2}-{hi:<3g}  n={m.sum():>6,}  fills={ev:>3}  "
                  f"empirical {emp:.2e}/s   fitted {pred:.2e}/s")
        results[p]["monotone"] = bool(mono)
        if not mono:
            print(f"    -> NOT monotone in delta. An exponential cannot describe this shape,")
            print(f"       so A and k above are a curve fit rather than a measurement.")

    # ---- the alternative that actually applies to a one-tick book ----
    print("\nFill probability at the touch, by the volume queued ahead")
    print("(A-S has no queue in it. In a book whose spread is one tick almost always,")
    print(" delta barely varies and this is what decides whether you trade.)")
    qres = {}
    for p in pairs:
        o = data[p]
        touch = o[(o.delta > 0) & (o.delta <= 1.0)]
        if len(touch) < 50:
            continue
        qs = touch.q_ahead.values.astype(float)
        yy = touch["y"].values
        TT = touch["T"].values
        # Arriving at an EMPTY level is not "a small queue", it is first in
        # line — a different state, and the one a maker wants. Percentile bins
        # lump it in with everything else: on xrpusd a mass of zeros pushed one
        # bucket to span 21 through 178 billion, which is not a bucket.
        groups = [(0, "first in queue (empty level)", qs == 0)]
        pos = qs[qs > 0]
        if len(pos) >= 40:
            e = np.unique(np.percentile(pos, [0, 25, 50, 75, 100]))
            for q, (lo, hi) in enumerate(zip(e[:-1], e[1:]), start=1):
                last = hi == e[-1]
                groups.append((q, f"{lo/1e8:,.4g} – {hi/1e8:,.4g} ahead",
                               (qs > 0) & (qs >= lo) & ((qs <= hi) if last else (qs < hi))))
        rows = []
        print(f"  {p}  ({len(touch):,} orders at the touch, sizes in base currency)")
        for slot, label, m in groups:
            n = int(m.sum())
            if n < 10:
                continue
            f = int((yy[m] > 0).sum())
            ex = float(TT[m].sum())
            ph = f / n
            z = 1.96
            den = 1 + z * z / n
            ctr = (ph + z * z / (2 * n)) / den
            hw = z * np.sqrt(ph * (1 - ph) / n + z * z / (4 * n * n)) / den
            qmid = float(np.median(qs[m])) if n else 0.0
            rows.append(dict(slot=slot, label=label, qmid=qmid, n=n, fills=f, p=ph,
                             lo95=max(0.0, ctr - hw), hi95=min(1.0, ctr + hw),
                             hazard=f / ex if ex > 0 else 0.0))
            print(f"    {label:<34s} n={n:>4}  fills={f:>3}  "
                  f"P(fill)={100*ph:5.2f}%  [{100*max(0,ctr-hw):4.2f}, {100*min(1,ctr+hw):5.2f}]")
        qres[p] = rows

    figure(pairs, data, results, qres, a.dmax, out)

    payload = {"model": "lambda(delta) = A * exp(-k * delta), delta in ticks from mid",
               "estimator": "per-order Poisson MLE, Newton-Raphson, observed-information SEs",
               "fit_range_ticks": a.dmax, "k_stability_ratio": stability,
               "instruments": results,
               "caveat": "Ten minutes per instrument. Fill counts are 24-65 per instrument, "
                         "so these are order-of-magnitude estimates. A-S has no queue term and "
                         "these books sit at a one-tick spread almost always, where queue "
                         "position rather than distance decides fills."}
    (out / "calibration.json").write_text(json.dumps(payload, indent=2))
    print(f"\nwrote {out}/calibration.json and {out}/07_ak_calibration.png")


def figure(pairs, data, results, qres, dmax, out):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.4, 4.3))
    edges = np.array([0, 1, 2, 4, 8, 16, 32])
    for p in pairs:
        o, r = data[p], results[p]
        xs, ys, los, his = [], [], [], []
        for lo, hi in zip(edges[:-1], edges[1:]):
            m = (o.delta > lo) & (o.delta <= hi) & (o["T"] > 0)
            ev, ex = int((o["y"][m] > 0).sum()), o["T"][m].sum()
            if ex <= 0 or ev == 0:
                continue
            xs.append((lo + hi) / 2); ys.append(ev / ex)
            # Poisson count interval, from the counts themselves.
            los.append(max(1e-6, (ev - 1.96 * np.sqrt(ev)) / ex))
            his.append((ev + 1.96 * np.sqrt(ev)) / ex)
        if not xs:
            continue
        xs = np.array(xs); ys = np.array(ys)
        ax1.errorbar(xs, ys, yerr=[ys - np.array(los), np.array(his) - ys], fmt="o", ms=5,
                     color=PAIR_C[p], lw=1.2, capsize=3, label=f"{p}  {r['fills']} fills")
        g = np.linspace(0.5, dmax, 120)
        ax1.plot(g, r["A"] * np.exp(-r["k"] * g), color=PAIR_C[p], lw=1.6,
                 ls="-" if r["monotone"] else (0, (4, 3)))
    ax1.set_yscale("log"); ax1.set_xscale("log")
    ax1.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))
    for s in ("top", "right"):
        ax1.spines[s].set_visible(False)
    ax1.set_xlabel("delta — ticks from the mid (log)")
    ax1.set_ylabel("fill intensity (per second, log)")
    ax1.legend(loc="lower left")
    ax1.text(0.98, 0.96, "solid = exponential holds\ndashed = shape is not monotone,\nso the fit is decorative",
             transform=ax1.transAxes, ha="right", va="top", fontsize=7.6, color=INK3, linespacing=1.4)

    # Rank on the x-axis, not raw size: the three instruments quote in
    # different base currencies, so their queue volumes are not comparable
    # numbers. What IS comparable is position — empty level, then quartiles.
    xt = ["first\nin queue", "Q1\nahead", "Q2", "Q3", "Q4\n(deepest)"]
    for p in pairs:
        rows = qres.get(p)
        if not rows:
            continue
        # Plot at the row's OWN slot. btcusd has too few empty-level arrivals to
        # report one, so it has no slot 0 — indexing by position instead would
        # draw its Q1 on the "first in queue" tick and silently compare
        # different states across instruments.
        x = np.array([r["slot"] for r in rows], dtype=float)
        y = np.array([100 * r["p"] for r in rows])
        lo = y - np.array([100 * r["lo95"] for r in rows])
        hi = np.array([100 * r["hi95"] for r in rows]) - y
        jit = (list(PAIR_C).index(p) - 1) * 0.06
        ax2.errorbar(x + jit, y, yerr=[lo, hi], fmt="o-", ms=5, color=PAIR_C[p], lw=1.6,
                     capsize=3, label=p)
        for xi, yi, hh, r in zip(x, y, hi, rows):
            ax2.annotate(f"{r['fills']}/{r['n']}", (xi + jit, yi + hh), textcoords="offset points",
                         xytext=(0, 4 + 7 * (list(PAIR_C).index(p) % 2)), ha="center",
                         fontsize=6.6, color=PAIR_C[p])
    ax2.set_xticks(np.arange(len(xt))); ax2.set_xticklabels(xt, fontsize=7.6)
    ax2.set_ylim(bottom=0)
    for sp in ("top", "right"):
        ax2.spines[sp].set_visible(False)
    ax2.set_xlabel("volume queued ahead on arrival, by quartile")
    ax2.set_ylabel("P(this order ever fills)  %")
    ax2.legend(loc="upper right")

    fig.subplots_adjust(top=0.74)
    fig.text(0.008, 0.985, "Calibrating A and k — and the reason they are the wrong parameters here",
             ha="left", va="top", fontsize=12.5, fontweight="bold", color=INK)
    fig.text(0.008, 0.90,
             "Left: fill intensity against distance from the mid, with the fitted exponential. Bars are Poisson intervals from the fill counts.\n"
             "Right: the same orders, at the touch, against the volume queued in front of them. A-S has no queue term — but these books sit at a\n"
             "one-tick spread 70-99% of the time, so delta has almost nothing to vary over and the queue is what decides who trades.",
             ha="left", va="top", fontsize=8.4, color=INK3)
    fig.savefig(out / "07_ak_calibration.png"); plt.close(fig)


if __name__ == "__main__":
    main()
