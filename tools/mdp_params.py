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
    bid quote        none, or (one of three price levels) x (volume ahead)
    ask quote        the same
    imbalance        touch imbalance, five buckets

Distance from the mid is deliberately NOT in the state. These books sit at a
one-tick spread 70-99% of the time, so there is nowhere to put a quote except
the touch or a tick or two behind it, and tools/calibrate.py could not identify
an Avellaneda-Stoikov k on two of the three instruments for exactly that reason.
Queue position is what varies and what decides who trades.

Queue position is bucketed by ABSOLUTE volume ahead, as a fraction of the mean
touch depth — not by quartile among the market's own resting orders, which is
what this did first. Fill hazard depends on how much size has to trade before
the queue reaches you, and that is an absolute quantity; a quartile is a rank,
taken over a population that is not ours. A market maker re-quotes the moment a
level clears, so its orders sit at small absolute queues far more often than the
book's own do. Measured against a touch-joining strategy's real placements the
hazard at the front ran 1,019/s while the quartile-calibrated model said 52/s,
and the policy duly concluded that quoting behind the touch was the better idea.
"""
import argparse
import json
import pathlib

import numpy as np
import pandas as pd

DT_MS = 100.0            # decision epoch; --dt-ms overrides, and must match the
                         # --grid-ms apps/stats sampled on
N_IMB = 5                # imbalance buckets
IMB_EDGES = [-1.0, -0.6, -0.2, 0.2, 0.6, 1.0]

# Queue buckets, as fractions of the mean touch depth. These MUST match
# kQueueEdges and kQueueBuckets in include/lob/policy/state.hpp: the solver
# indexes the rows this file emits, and a table solved over one bucketing and
# looked up through another is not a degraded policy, it is a random one.
N_QUEUE = 5
QUEUE_EDGES = [0.02, 0.10, 0.35]
QUEUE_NAMES = ["alone", "<2%", "<10%", "<35%", "deep"]


def imb_bucket(x):
    return np.clip(np.digitize(x, IMB_EDGES[1:-1]), 0, N_IMB - 1)


def queue_bucket(ahead, scale):
    """Mirror of policy::queue_bucket. Bucket 0 is alone at the price."""
    ahead = np.asarray(ahead, dtype=float)
    b = np.ones(ahead.shape, dtype=int)
    if scale > 0:
        for i, e in enumerate(QUEUE_EDGES):
            b = np.where(ahead > e * scale, i + 2, b)
    else:
        b = np.full(ahead.shape, N_QUEUE - 1, dtype=int)
    return np.where(ahead <= 0, 0, b)


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

    # ---- fill hazard by volume ahead --------------------------------------
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
        bq = queue_bucket(q, touch_sz if np.isfinite(touch_sz) else 0.0)
        for i in range(N_QUEUE):
            sel = bq == i
            n = int(sel.sum())
            if n < 10:
                continue
            ev = float((at_touch["y"].values[sel] > 0).sum())
            ex = float(at_touch["T"].values[sel].sum())
            lam = ev / ex if ex > 0 else 0.0
            # `index` is what the solver reads. The name is for people; matching
            # on it meant an unrecognised name became a bucket of zeros, which
            # the solver could not tell from "a quote here never fills".
            hz.append({"bucket": QUEUE_NAMES[i], "index": i, "n": n, "fills": int(ev),
                       "exposure_s": ex, "hazard_per_s": lam,
                       "mean_ahead": float(q[sel].mean()),
                       "p_fill_per_step": float(1.0 - np.exp(-lam * DT_MS / 1000.0))})
    out["fill_hazard"] = hz
    out["queue"]["scale_note"] = (
        "mean_touch_size is the scale queue buckets are fractions of; it is written "
        "into the policy table header so the executor cannot bucket on another one")
    out["queue"]["bucket_edges"] = QUEUE_EDGES

    # How much less often a quote one tick BEHIND the touch trades than one at
    # it. Measured from the TRADES, not from where orders were placed.
    #
    # An order's recorded distance is its distance on arrival. Most orders that
    # were placed a tick behind and then filled were filled after the touch came
    # to them — they were AT the touch when they traded. Counting those as fills
    # one tick behind roughly doubles the level-one hazard, and the MDP already
    # models promotion to the touch as its own transition, so the model was
    # paying for the same event twice. That error has a direction: it makes
    # quoting behind the touch look better than quoting at it, which is exactly
    # the wrong conclusion the first solved policy reached.
    #
    # A print's dist_ticks is how far past the resting side's touch the sweep
    # went at the moment it happened, so this conditions on the thing that
    # matters. The ratio is the share of traded volume that reaches each level:
    # a resting order at level l is only reachable by the flow that gets there,
    # so the rate it is reached at scales with that share.
    lv = {}
    if len(tr) and "dist_ticks" in tr.columns:
        dist = tr.dist_ticks.values.astype(float)
        vol = tr.qty.values.astype(float)
        v0 = float(vol[dist >= 0].sum())
        for l in range(4):
            vl = float(vol[dist >= l].sum())
            lv[l] = {"volume": vl, "prints": int((dist >= l).sum()),
                     "share": (vl / v0) if v0 > 0 else float("nan")}
    ratio = lv.get(1, {}).get("share", float("nan"))
    n_behind = lv.get(1, {}).get("prints", 0)
    # A quote further from the touch cannot be filled MORE often than one at it:
    # every market order that reaches the second level passed through the first.
    # A ratio at or above 1 is noise wearing the shape of a result. Marked
    # unmeasured so the solver demands an explicit value instead of using it.
    # A quote one tick behind the touch on a book that sits AT one tick should
    # trade far less often than one at it -- the simulator measures 0.2%, and a
    # clean ten-minute ethusd sample measured 7%. A share up near a half means
    # the touch this was measured against is not the real touch, so "one tick
    # past it" is not a real distance either. That is what a teleporting touch
    # looks like from this side, and both eight-hour captures show it: btcusd
    # reports 30.8% / 29.2% / 29.1% across three levels and ethusd 62.1% /
    # 61.4% / 61.0%, flat, when penetration must fall with depth.
    #
    # Flat-and-high is therefore its own rejection, separate from the count.
    # Believing it would repeat the exact error that made the first solved
    # policy quote behind the touch and take a thirty-eighth of the fills.
    flat = bool(np.isfinite(lv.get(2, {}).get("share", float("nan")))
                and ratio > 0 and lv[2]["share"] / ratio > 0.8)
    measured = bool(np.isfinite(ratio) and 0.0 < ratio < 0.5 and n_behind >= 20
                    and not flat)
    out["level_ratio"] = {
        "volume_share_by_level": {str(k): v for k, v in lv.items()},
        "ratio": float(ratio) if np.isfinite(ratio) else None,
        "prints_one_behind": n_behind,
        "measured": measured,
        "flat_across_levels": flat,
        "note": "share of traded volume that reaches one tick past the touch, "
                "measured at the moment of each print. Unmeasured unless it is "
                "below 0.5 on at least 20 prints AND falls with depth: a share "
                "that does not fall means the touch it was measured against is "
                "not the real one. The solver compounds it geometrically",
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
    # Queue position is the whole reason this state variable exists, and the one
    # thing it must do is fall as the volume ahead rises: every market order that
    # reaches you passed through everything in front of you first. Where the
    # measured hazard does not fall, ten minutes has not identified it — the
    # buckets are being ordered by noise, and a policy solved on that is choosing
    # its queue position at random while looking like it optimised one.
    if len(hz) >= 2 and hz[0]["hazard_per_s"] <= hz[-1]["hazard_per_s"]:
        reasons.append(
            f"fill hazard does not fall with volume ahead ({hz[0]['bucket']} "
            f"{hz[0]['hazard_per_s']:.2e}/s on {hz[0]['fills']} fills vs "
            f"{hz[-1]['bucket']} {hz[-1]['hazard_per_s']:.2e}/s on {hz[-1]['fills']}) "
            "— queue position is not identified in this sample")
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
        print(f"  mid, per {DT_MS:g} ms (spread<={md['max_spread_ticks']} ticks, "
              f"{md['steps_kept_pct']:.0f}% of steps kept):")
        print(f"    up {100*md['p_up']:.1f}%  down {100*md['p_down']:.1f}%  flat {100*md['p_flat']:.1f}%   "
              f"median move {md['median_abs_move_ticks']:.1f}  winsorised sd {md['winsorised_sd_ticks']:.2f}  "
              f"raw sd {md['raw_sd_ticks']:.1f}  max {md['max_abs_move_ticks']:.0f} ticks")
        print(f"  spread: median {r['spread']['median_ticks']:.0f} ticks, "
              f"{100*r['spread']['share_at_one_tick']:.0f}% of the time at one tick")
        qq = r["queue"]
        print(f"  touch queue: mean size {qq['mean_touch_size']:,.0f}, "
              f"{100*qq['drain_fraction_per_step']:.2f}% of it leaves per epoch")
        print(f"  fill hazard by volume ahead (as a fraction of that mean size):")
        for h in r["fill_hazard"]:
            print(f"    {h['bucket']:<6s} n={h['n']:>4}  fills={h['fills']:>3}  "
                  f"mean ahead={h['mean_ahead']:>10,.0f}  "
                  f"lambda={h['hazard_per_s']:.2e}/s  P(fill per epoch)={h['p_fill_per_step']:.2e}")
        pu = r["imbalance"]["p_next_move_up"]
        print(f"  imbalance -> P(next mid move is up), by bucket (bid-heavy on the right):")
        print("    " + "  ".join("  n/a" if x is None else f"{100*x:4.1f}%" for x in pu))
        lr = r["level_ratio"]
        if lr["ratio"] is not None:
            print(f"  volume reaching each level past the touch: " +
                  "  ".join(f"L{k}={100*v['share']:.1f}%"
                            for k, v in lr["volume_share_by_level"].items()))
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
