#!/usr/bin/env python3
"""Fits queue-reactive intensities to the table apps/stats measured.

    py tools/fit_queue_reactive.py --csv csv --out policy

Huang, Lehalle & Rosenbaum (arXiv:1312.0563) model a limit order book as queues
whose event rates depend on their own SIZE. This asks the data whether that is
true here, and by how much, before anything is fitted to it.

The test is a slope. If an intensity is proportional to x^beta then log lambda
is linear in log x with slope beta, and beta says which mechanism is running:

    beta ~ 1   proportional. Every resting order behaves independently, so a
               queue with twice as many ORDERS produces twice the events. This
               is what cancels should look like.
    beta ~ 0   flat. The rate ignores the queue -- which is what a Poisson
               generator with fixed rates produces, at every size, by
               construction.

It is fitted against TWO axes, and which one matters is the point. Independent
cancellation scales with the NUMBER OF ORDERS resting, not with the shares they
represent: a six-million-share queue may be three large orders or three hundred
small ones. Fitted on quantity alone this returned a cancel slope of -0.03 +-
0.08 for ethusd -- rejecting proportionality at twelve standard errors -- which
would have been reported as "real books do not cancel independently" when the
measurement was simply keyed on the wrong variable.

So the slope is not a curiosity: it is the number that says whether the
simulator this project tests policies against resembles the book it is meant to
model. Run it on both and put them side by side.
"""
import argparse
import json
import math
import pathlib
import sys

import numpy as np
import pandas as pd

EVENTS = ("adds", "cancels", "trades")
MIN_N = 20          # events in a bucket before its rate means anything


def slope(logq, loglam, n):
    """Weighted least squares of log(lambda) on log(q).

    Poisson counts give Var(log lambda_hat) ~ 1/n, so the weights are the counts
    themselves. Returns (beta, se, intercept) or None if under-determined -- two
    points define a line exactly and say nothing about whether it is one.
    """
    if len(logq) < 3:
        return None
    w = np.asarray(n, dtype=float)
    x, y = np.asarray(logq, dtype=float), np.asarray(loglam, dtype=float)
    sw = w.sum()
    xb, yb = (w * x).sum() / sw, (w * y).sum() / sw
    sxx = (w * (x - xb) ** 2).sum()
    if sxx <= 0:
        return None
    beta = (w * (x - xb) * (y - yb)).sum() / sxx
    resid = y - (yb + beta * (x - xb))
    dof = len(x) - 2
    s2 = (w * resid ** 2).sum() / (sw * dof) if dof > 0 else float("nan")
    return beta, math.sqrt(max(s2 / sxx * sw, 0.0)), yb - beta * xb


def fit(df, pair):
    out = {"pair": pair, "levels": {}}
    print(f"\n=== {pair} ===")
    for lvl in sorted(df.level.unique()):
        g = df[df.level == lvl]
        # Both sides pooled: the book is symmetric and splitting halves every
        # bucket's count for no hypothesis anyone is testing.
        lv = {"exposure_s": float(g.exposure_s.sum())}
        print(f"  level {lvl}   ({g.exposure_s.sum():,.0f} queue-seconds observed)")

        for axis, label in (("log2_orders", "orders resting"), ("log2_qty", "shares queued")):
            agg = g.groupby(axis).agg(
                adds=("adds", "sum"), cancels=("cancels", "sum"),
                trades=("trades", "sum"), expo=("exposure_s", "sum")).reset_index()
            agg = agg[agg.expo > 0]
            if agg.empty:
                continue
            # Bucket b covers [2^b, 2^(b+1)); its geometric centre is 2^(b+0.5).
            agg["x"] = 2.0 ** (agg[axis] + 0.5)

            print(f"    vs {label}:")
            for ev in EVENTS:
                sel = agg[agg[ev] >= MIN_N]
                key = f"{ev}_vs_{'orders' if axis == 'log2_orders' else 'qty'}"
                if len(sel) < 3:
                    lv[key] = {"beta": None,
                               "note": f"{MIN_N}+ events in only {len(sel)} bucket(s)"}
                    continue
                lam = sel[ev].values / sel.expo.values
                fitres = slope(np.log(sel.x.values), np.log(lam), sel[ev].values)
                if fitres is None:
                    lv[key] = {"beta": None, "note": "degenerate fit"}
                    continue
                beta, se, icpt = fitres
                # Which mechanism the slope is consistent with, at 2 SEs.
                flat = abs(beta) < 2 * se
                prop = abs(beta - 1.0) < 2 * se
                verdict = ("FLAT: rate ignores the queue" if flat and not prop
                           else "PROPORTIONAL: independent per-order behaviour" if prop and not flat
                           else "between flat and proportional" if not flat and not prop
                           else "too noisy to separate flat from proportional")
                lv[key] = {"beta": float(beta), "se": float(se), "log_intercept": float(icpt),
                           "buckets_used": int(len(sel)), "events": int(sel[ev].sum()),
                           "verdict": verdict}
                print(f"      {ev:<8} beta = {beta:+.2f} +- {se:.2f}  "
                      f"({int(sel[ev].sum()):,} events, {len(sel)} buckets)   {verdict}")
        out["levels"][str(lvl)] = lv
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="csv")
    ap.add_argument("--out", default="policy")
    ap.add_argument("--only", default=None)
    a = ap.parse_args()

    d = pathlib.Path(a.csv)
    files = sorted(d.glob("*_qr.csv"))
    if a.only:
        files = [f for f in files if f.name.startswith(a.only + "_")]
    if not files:
        print(f"no *_qr.csv in {d}. Run stats first -- it writes one per instrument.")
        return 1

    all_out = {}
    for f in files:
        pair = f.name[: -len("_qr.csv")]
        all_out[pair] = fit(pd.read_csv(f), pair)

    outdir = pathlib.Path(a.out)
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "queue_reactive.json").write_text(json.dumps(all_out, indent=2))
    print(f"\nwrote {outdir}/queue_reactive.json")
    print("\nRead the two axes together. A cancel slope near 1 against ORDERS RESTING is")
    print("independent per-order cancellation. Near 0 against SHARES QUEUED at the same")
    print("time says the shares were never the mechanism -- only how many orders hold")
    print("them. Flat on both axes is a process whose rates ignore the book entirely,")
    print("which is what --synthetic should show; run it there and compare.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
