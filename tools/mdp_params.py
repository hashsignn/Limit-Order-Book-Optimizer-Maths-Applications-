#!/usr/bin/env python3
"""Estimates the MDP's transition model from real level-3 data.

    ./build/stats --capture data/samples/<pair>_bitstamp.jsonl.gz --outdir csv
    py tools/mdp_params.py --csv csv --out policy

Phase 5 solves a discretised Markov decision process offline and ships the
answer as a lookup table. This file supplies the process. Everything it writes
is measured from the captures except where it says otherwise, and where a
quantity is not measurable from ten minutes of data it says so in the output
rather than quietly using a default.

The state the solver works over:

    inventory        lots held, bounded
    bid quote        none, or (at touch | one tick behind) x queue quartile
    ask quote        the same
    imbalance        touch imbalance, five buckets

Distance from the mid is deliberately NOT in the state. These books sit at a
one-tick spread 70-99% of the time, so there is nowhere to put a quote except
the touch or one tick behind it, and tools/calibrate.py could not identify an
Avellaneda-Stoikov k on two of the three instruments for exactly that reason.
Queue position is what varies and what decides who trades.
"""
import argparse
import json
import pathlib

import numpy as np
import pandas as pd

DT_MS = 100.0            # decision epoch; --dt-ms overrides, and must match the
                         # --grid-ms apps/stats sampled on
N_IMB = 5                # imbalance buckets
N_QUEUE = 4              # queue-position quartiles, front to back
IMB_EDGES = [-1.0, -0.6, -0.2, 0.2, 0.6, 1.0]


def imb_bucket(x):
    return np.clip(np.digitize(x, IMB_EDGES[1:-1]), 0, N_IMB - 1)


def estimate(csvdir, pair, max_spread, order_size):
    o = pd.read_csv(pathlib.Path(csvdir) / f"{pair}_orders.csv")
    m = pd.read_csv(pathlib.Path(csvdir) / f"{pair}_mid.csv")
    d = pd.read_csv(pathlib.Path(csvdir) / f"{pair}_depth.csv")
    tr = pd.read_csv(pathlib.Path(csvdir) / f"{pair}_trades.csv")

    o["delta"] = o.dist_ticks + o.spread_ticks / 2.0
    o["T"] = o.lifetime_ms / 1000.0
    o["y"] = (o.filled > 0).astype(float)
    out = {"pair": pair, "dt_ms": DT_MS}

    # ---- mid dynamics -----------------------------------------------------
    # The mid moves in half ticks because it sits between two integer prices.
    #
    # Two filters, both necessary and both stated in the output.
    #
    # A step is only trusted when the spread is narrow at BOTH ends. The book is
    # built from the stream, so when the touch empties the reconstructed mid
    # jumps to whatever level we happen to know about — hundreds of ticks away —
    # and back. Unfiltered, btcusd's per-epoch standard deviation is 79 ticks
    # and its one-second markout is +411 ticks, or $4.11, which is not a
    # microstructure effect but a hole in the reconstruction.
    #
    # And the location statistics are robust ones. Even filtered, btcusd moves
    # ZERO ticks in 99% of epochs and several hundred in a handful, so a
    # standard deviation describes the handful and nothing else. Signs and
    # medians survive that; a second moment does not.
    mid = (m.bid.values + m.ask.values) / 2.0
    spread = m.ask.values - m.bid.values
    gaps = np.diff(m.t_ms.values)
    trusted = spread <= max_spread
    ok = (gaps > 0) & (gaps <= DT_MS * 3) & trusted[:-1] & trusted[1:]
    dm = np.diff(mid)[ok]
    nz = np.abs(dm[dm != 0])
    hi = np.percentile(np.abs(dm), 99.5) if dm.size else 0.0
    out["mid"] = {
        "p_up": float((dm > 0).mean()), "p_down": float((dm < 0).mean()),
        "p_flat": float((dm == 0).mean()),
        "median_abs_move_ticks": float(np.median(nz)) if nz.size else 0.0,
        "winsorised_sd_ticks": float(np.clip(dm, -hi, hi).std()) if dm.size else 0.0,
        "raw_sd_ticks": float(dm.std()) if dm.size else 0.0,
        "max_abs_move_ticks": float(np.abs(dm).max()) if dm.size else 0.0,
        "samples": int(dm.size),
        "steps_kept_pct": float(100.0 * ok.sum() / max(1, ok.size)),
        "max_spread_ticks": max_spread,
    }

    # ---- touch depth and how fast the queue in front of you drains ---------
    touch_sz = d[(d.side == 0) & (d.dist_ticks == 0)].mean_qty
    touch_sz = float(touch_sz.iloc[0]) if len(touch_sz) else float("nan")
    at_touch = o[o.delta <= 1.0]
    span_s = (o.t_ms.max() - o.t_ms.min()) / 1000.0
    removed = float((at_touch.filled + at_touch.cancelled).sum())
    # Fraction of a full touch queue that leaves per epoch. This is the rate at
    # which an order climbs the queue without trading — the free progress that
    # makes cancels worth separating from fills in the first place.
    drain_per_s = removed / span_s / touch_sz if touch_sz > 0 else float("nan")
    # The half-spread a touch quote captures is not a constant: it is half of
    # whatever the spread actually is. Hardcoding 0.5 assumes a one-tick book
    # and silently misprices every other one — on a two-tick book it halves the
    # edge at the touch while leaving the edge one tick behind almost right,
    # which is exactly the bias that makes quoting behind look optimal.
    sp = (m.ask.values - m.bid.values)
    sp = sp[sp > 0]
    out["spread"] = {
        "median_ticks": float(np.median(sp)) if sp.size else 1.0,
        "p90_ticks": float(np.percentile(sp, 90)) if sp.size else 1.0,
        "share_at_one_tick": float((sp == 1).mean()) if sp.size else 0.0,
    }

    out["queue"] = {
        "mean_touch_size": touch_sz,
        "removed_per_s": removed / span_s,
        "drain_fraction_per_step": float(drain_per_s * DT_MS / 1000.0),
        "note": "fraction of a full touch queue that leaves per epoch, both sides pooled",
    }

    # ---- fill hazard by queue quartile ------------------------------------
    # Measured as events over exposure, so an order cancelled before filling
    # contributes time at risk without an event, which is what it is.
    # Fill probability depends on SIZE as much as on queue position: a small
    # order in front of a big one is consumed by a market order that barely
    # dents its neighbour. Calibrating over every order in the book — here they
    # run from 1 to 500 — estimates the hazard for an average-sized one, and a
    # market maker quoting ten lots then finds itself filling far faster than
    # the model expected. That bias is not neutral: it makes the low-fill-rate
    # quote one tick behind the touch look better than it is.
    #
    # Restricted to orders of roughly our own size when one is given.
    if order_size:
        band = at_touch[(at_touch["size"] >= order_size * 0.4) &
                        (at_touch["size"] <= order_size * 2.5)]
        if len(band) >= 200:
            at_touch = band
        out["hazard_size_band"] = {"target": order_size, "n": int(len(band)),
                                   "applied": bool(len(band) >= 200)}

    q = at_touch.q_ahead.values.astype(float)
    hz = []
    if len(at_touch) >= 60:
        pos = q[q > 0]
        edges = np.unique(np.percentile(pos, [0, 25, 50, 75, 100])) if len(pos) >= 40 else np.array([0.0])
        groups = [("empty", q == 0)] + [
            (f"q{i+1}", (q > 0) & (q >= lo) & ((q <= hi) if hi == edges[-1] else (q < hi)))
            for i, (lo, hi) in enumerate(zip(edges[:-1], edges[1:]))]
        for name, sel in groups:
            n = int(sel.sum())
            if n < 10:
                continue
            ev = float((at_touch["y"].values[sel] > 0).sum())
            ex = float(at_touch["T"].values[sel].sum())
            lam = ev / ex if ex > 0 else 0.0
            hz.append({"bucket": name, "n": n, "fills": int(ev), "exposure_s": ex,
                       "hazard_per_s": lam,
                       "p_fill_per_step": float(1.0 - np.exp(-lam * DT_MS / 1000.0))})
    out["fill_hazard"] = hz

    # How much less often a quote one tick BEHIND the touch trades than one at
    # it. Measured directly rather than borrowed from the A-S k, which
    # tools/calibrate.py could not identify on two of three instruments.
    def hazard(lo, hi):
        sel = o[(o.delta > lo) & (o.delta <= hi)]
        ex = float(sel["T"].sum())
        return (float((sel["y"] > 0).sum()) / ex) if ex > 0 else float("nan")
    h0, h1 = hazard(0.0, 1.0), hazard(1.0, 2.0)
    n_behind = int((o[(o.delta > 1.0) & (o.delta <= 2.0)]["y"] > 0).sum())
    ratio = (h1 / h0) if (h0 and np.isfinite(h0) and np.isfinite(h1) and h0 > 0) else float("nan")
    # A quote further from the touch cannot be filled MORE often than one at it:
    # every market order that reaches the second level passed through the first.
    # ethusd measures 1.125 off a single fill, which is noise wearing the shape
    # of a result. Marked unmeasured so the solver demands an explicit value
    # instead of quietly using it.
    measured = bool(np.isfinite(ratio) and 0.0 < ratio < 1.0 and n_behind >= 3)
    out["level_ratio"] = {
        "hazard_at_touch_per_s": h0, "hazard_one_behind_per_s": h1,
        "ratio": float(ratio) if np.isfinite(ratio) else None,
        "fills_one_behind": n_behind,
        "measured": measured,
        "note": "fill hazard one tick behind the touch over the hazard at it; "
                "unmeasured unless it lands in (0,1) on at least 3 fills",
    }

    # ---- imbalance, and how it moves --------------------------------------
    tot = m.bid_qty.values + m.ask_qty.values
    imb = np.where(tot > 0, (m.bid_qty.values - m.ask_qty.values) / np.maximum(tot, 1), 0.0)
    b = imb_bucket(imb)
    P = np.zeros((N_IMB, N_IMB))
    for i in np.flatnonzero(ok):
        P[b[i], b[i + 1]] += 1
    rows = P.sum(axis=1, keepdims=True)
    P = np.divide(P, rows, out=np.full_like(P, 1.0 / N_IMB), where=rows > 0)
    out["imbalance"] = {
        "edges": IMB_EDGES,
        "stationary": [float(x) for x in np.bincount(b, minlength=N_IMB) / max(1, len(b))],
        "transition": [[float(x) for x in row] for row in P],
    }

    # Does imbalance predict the next move? If it does not, it does not belong
    # in the state, and saying so is cheaper than carrying a dead dimension.
    drift, drift_p, drift_d = [], [], []
    for k in range(N_IMB):
        sel = (b[:-1] == k)[ok]
        if sel.sum() > 20:
            # Median, not mean: the same heavy tail that ruins the standard
            # deviation ruins a conditional mean. P(up) is the honest summary.
            drift.append(float(np.median(dm[sel])))
            drift_p.append(float((dm[sel] > 0).mean()))
            drift_d.append(float((dm[sel] < 0).mean()))
        else:
            drift.append(None); drift_p.append(None); drift_d.append(None)
    out["imbalance"]["median_next_move_ticks"] = drift
    out["imbalance"]["p_next_move_up"] = drift_p
    out["imbalance"]["p_next_move_down"] = drift_d

    # ---- adverse selection -------------------------------------------------
    # After a print, does the mid move against whoever was resting? The markout
    # figure says yes at every horizon; this is the number the reward uses.
    adv = {}
    if len(tr) >= 20:
        mt = m.t_ms.values.astype(float)
        t0 = tr.t_ms.values.astype(float)
        base = np.interp(t0, mt, mid)
        for h in (0.5, 1.0, 5.0):
            fwd = np.interp(t0 + h * 1000.0, mt, mid)
            sign = np.where(tr.side.values == 0, 1.0, -1.0)     # taker buy = +1
            mo = sign * (fwd - base)
            # Median in ticks, mean in bps, and the sign share. Ticks are not
            # comparable across instruments and a mean over this tail is not
            # comparable with anything.
            adv[f"markout_{h}s_ticks_median"] = float(np.median(mo))
            adv[f"markout_{h}s_bps"] = float(np.mean(1e4 * mo / base))
            adv[f"p_adverse_{h}s"] = float((mo > 0).mean())
        adv["n_prints"] = int(len(tr))
    out["adverse_selection"] = adv

    # ---- is this instrument's process usable at all? ----------------------
    # A book rebuilt from ten minutes of stream holds only what has churned
    # since recording started. Where that is not enough to know the level behind
    # the touch, the mid does not move by a tick when the touch clears — it
    # teleports to the nearest level we happen to have seen. The tell is a
    # median move far larger than a tick in a book whose spread IS a tick.
    md = out["mid"]
    reasons = []
    if md["median_abs_move_ticks"] > 4.0:
        reasons.append(f"median mid move is {md['median_abs_move_ticks']:.0f} ticks in a book whose "
                       f"spread is 1 tick — the touch is teleporting, not moving")
    if md["p_up"] + md["p_down"] < 0.005:
        reasons.append("the mid essentially never moves in the reconstruction")
    if not any(h["fills"] > 0 for h in hz):
        reasons.append("no fills observed at the touch")
    out["usable_for_mdp"] = not reasons
    out["unusable_because"] = reasons
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="csv")
    ap.add_argument("--out", default="policy")
    ap.add_argument("--dt-ms", type=float, default=None,
                    help="decision epoch in ms; must match the grid apps/stats used")
    ap.add_argument("--only", default=None, help="one instrument label")
    ap.add_argument("--order-size", type=float, default=None,
                    help="restrict the fill-hazard sample to orders near this size")
    ap.add_argument("--max-spread", type=float, default=3.0,
                    help="trust a mid step only if the spread is this narrow at both ends")
    a = ap.parse_args()
    global DT_MS
    if a.dt_ms:
        DT_MS = a.dt_ms
    outdir = pathlib.Path(a.out); outdir.mkdir(parents=True, exist_ok=True)

    if a.only:
        pairs = [a.only]
    else:
        pairs = sorted(q.name[: -len("_orders.csv")]
                       for q in pathlib.Path(a.csv).glob("*_orders.csv"))
    all_out = {}
    for p in pairs:
        r = estimate(a.csv, p, a.max_spread, a.order_size)
        all_out[p] = r
        print(f"\n=== {p} ===")
        md = r["mid"]
        print(f"  mid, per {DT_MS:.0f} ms (spread<={md['max_spread_ticks']} ticks, "
              f"{md['steps_kept_pct']:.0f}% of steps kept):")
        print(f"    up {100*md['p_up']:.1f}%  down {100*md['p_down']:.1f}%  flat {100*md['p_flat']:.1f}%   "
              f"median move {md['median_abs_move_ticks']:.1f}  winsorised sd {md['winsorised_sd_ticks']:.2f}  "
              f"raw sd {md['raw_sd_ticks']:.1f}  max {md['max_abs_move_ticks']:.0f} ticks")
        print(f"  spread: median {r['spread']['median_ticks']:.0f} ticks, "
              f"{100*r['spread']['share_at_one_tick']:.0f}% of the time at one tick")
        qq = r["queue"]
        print(f"  touch queue: mean size {qq['mean_touch_size']:,.0f}, "
              f"{100*qq['drain_fraction_per_step']:.2f}% of it leaves per epoch")
        print(f"  fill hazard by queue position:")
        for h in r["fill_hazard"]:
            print(f"    {h['bucket']:<6s} n={h['n']:>4}  fills={h['fills']:>3}  "
                  f"lambda={h['hazard_per_s']:.2e}/s  P(fill per epoch)={h['p_fill_per_step']:.2e}")
        pu = r["imbalance"]["p_next_move_up"]
        print(f"  imbalance -> P(next mid move is up), by bucket (bid-heavy on the right):")
        print("    " + "  ".join("  n/a" if x is None else f"{100*x:4.1f}%" for x in pu))
        if not r["usable_for_mdp"]:
            print(f"  \033[31mNOT USABLE for the MDP\033[0m: " + "; ".join(r["unusable_because"]))
        if r["adverse_selection"]:
            av = r["adverse_selection"]
            print(f"  adverse selection at 1 s: {av['markout_1.0s_bps']:+.2f} bps, "
                  f"median {av['markout_1.0s_ticks_median']:+.1f} ticks, mid moves against the "
                  f"passive side {100*av['p_adverse_1.0s']:.0f}% of the time (n={av['n_prints']})")

    (outdir / "mdp.json").write_text(json.dumps(all_out, indent=2))
    print(f"\nwrote {outdir}/mdp.json")


if __name__ == "__main__":
    main()
