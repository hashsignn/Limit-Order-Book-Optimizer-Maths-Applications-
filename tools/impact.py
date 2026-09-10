#!/usr/bin/env python3
"""The stylized-fact scorecard for trade price impact.

    ./build/stats --capture data/samples/<pair>_bitstamp.jsonl.gz --outdir csv
    py tools/impact.py --csv csv --label ethusd

Five statistics, all of them about the SAME question: does a trade carry
information? A zero-intelligence process answers no by construction, and every
number below then comes out near its null value.

    lift        P(the mid moves within tau | a print just happened)
                --------------------------------------------------
                P(the mid moves within tau | a random moment)
                1.0 means a print says nothing about what happens next.

    adverse     P(the mid has moved AGAINST the resting side at tau | a print).
                50% is a coin flip. A maker filled at the touch is on the
                resting side, so this is what its markout is made of.

    eta         Robert-Rosenbaum: continuations / (2 * alternations) over the
                sequence of mid moves. 0.5 is a random walk; above it the price
                trends, below it mean-reverts on the tick grid.

                READ IT NEXT TO tick_bp AND ticks/s, NEVER ALONE. eta is the
                sign of consecutive grid changes, so what it measures depends on
                how many ticks the price crosses between samples. ethusd
                traverses 2.41 ticks/s on a tick worth 0.041 bp; this simulator
                traverses 0.06 ticks/s on a tick worth 1.0 bp. On the first the
                sign tracks drift and comes out at 0.84; on the second it tracks
                the touch flickering by half a tick and comes out near 0.5.
                Those are different statistics wearing one name, and comparing
                them directly is what produced the standing claim that the
                simulator's price is a random walk where the market's trends.

    sig(1s)     Realised volatility: the standard deviation of the log mid
                return over non-overlapping one-second windows, in bp.

                THIS COLUMN REPLACED A WRONG ONE. It used to read `bp/s`,
                ticks/s times tick_bp, described here as "scale-free volatility
                -- THIS is comparable". It is not volatility. It is total
                variation, the length of the path, and variance per unit time is
                rate times step SQUARED. A coarse tick crossed rarely and a fine
                tick crossed often have the same path length and very different
                variance, which is precisely the case here: ethusd and this
                simulator agreed at 0.098 and 0.060 bp/s, and that agreement was
                an artefact. On sig(1s) they read 0.292 and 0.289 -- still
                agreeing, and still an artefact, because the simulator is
                running at a mid of 10,000 ticks where one tick is 1.0 bp.
                Run the identical process at ethusd's own price level and
                nothing about it changes except the denominator:

                    mid 10,000 ticks   0.0778 touch changes/s   sig 0.2885 bp
                    mid 245,422 ticks  0.0778 touch changes/s   sig 0.0121 bp
                    ethusd             0.3910 touch changes/s   sig 0.2918 bp

                So on the market's own tick grid the model produces one
                twenty-fourth of the market's volatility, and the fat tick is
                what hides it. See docs/KNOWN-ISSUES.md issue 9.

    spr/sig     Mean quoted spread in bp, over sig(1s). What a maker is paid for
                one round trip, per one second of price risk. Both halves are in
                basis points, so this is the one column no choice of tick size
                can flatter, and it is the number that says whether making a
                market on a book is easy or hard.

    ac1, ac10   Autocorrelation of the trade SIGN at lags 1 and 10. Order
                splitting (Lillo-Mike-Farmer) makes this positive and slowly
                decaying; independent takers make it zero.

    impact      Mean signed mid change over tau following a print, in BASIS
                POINTS of the mid. Not ticks: ethusd trades at 245,374 ticks and
                the simulator at 10,000, so the same tick is 0.04 bps on one
                book and 1.0 bp on the other. A raw-tick column made the two
                look 300x apart when in bps they are 12x apart, and the whole
                point of this file is a comparison.

    adv|mv      P(the move went WITH the trade sign | the mid moved at all).
                `adverse` mixes two things -- how often the mid moves, and
                whether the move is informative -- and the second is the one a
                model of information has to get right.

Run it on a capture and on the simulator and put them side by side. That
comparison is the point: a number is only interesting against the market it
claims to model.
"""
import argparse
import bisect
import csv
import math
import pathlib
import random


def load_mid(path):
    ts, mid, spr = [], [], []
    with open(path, newline="") as f:
        r = csv.reader(f)
        next(r, None)
        for row in r:
            if len(row) < 3:
                continue
            try:
                t = float(row[0]); b = int(row[1]); a = int(row[2])
            except ValueError:
                continue
            if b <= 0 or a <= 0:
                continue
            ts.append(t); mid.append(0.5 * (b + a)); spr.append(a - b)
    return ts, mid, spr


def realised_sigma(ts, mid, window_ms):
    """Standard deviation of the log mid return over NON-OVERLAPPING windows,
    in basis points.

    This, and not ticks/s, is the volatility. Variance per unit time is
    rate x step^2, so a coarse tick crossed rarely and a fine tick crossed often
    can traverse wildly different numbers of ticks at the same variance -- which
    is exactly the case between this simulator and the captures. Sampling on a
    fixed clock rather than on events also makes the number independent of how
    densely the book happened to be written out.
    """
    if len(ts) < 3:
        return float("nan")
    n = int((ts[-1] - ts[0]) // window_ms)
    if n < 8:
        return float("nan")
    px = []
    for k in range(n + 1):
        i = bisect.bisect_right(ts, ts[0] + k * window_ms) - 1
        px.append(mid[max(i, 0)])
    r = [math.log(px[k + 1] / px[k]) * 1e4 for k in range(n)]
    m = sum(r) / n
    return math.sqrt(sum((x - m) ** 2 for x in r) / (n - 1))


def load_trades(path):
    out = []
    with open(path, newline="") as f:
        r = csv.reader(f)
        next(r, None)
        for row in r:
            if len(row) < 4:
                continue
            try:
                t = float(row[0]); side = int(row[2]); qty = int(row[3])
            except ValueError:
                continue
            # side 0 = taker bought (lifted the ask) => signed +1
            out.append((t, +1 if side == 0 else -1, qty))
    return out


def mid_at(ts, mid, t):
    """The last grid sample at or before t. The grid is what the book was
    actually sampled on, so interpolating between samples would invent a price
    the book never showed."""
    i = bisect.bisect_right(ts, t) - 1
    return mid[i] if i >= 0 else None


def moved(ts, mid, t0, tau_ms):
    a = mid_at(ts, mid, t0)
    b = mid_at(ts, mid, t0 + tau_ms)
    if a is None or b is None:
        return None
    return b - a


def scorecard(csvdir, label, tau_ms, seed=20260904):
    d = pathlib.Path(csvdir)
    ts, mid, spr = load_mid(d / f"{label}_mid.csv")
    trades = load_trades(d / f"{label}_trades.csv")
    if len(ts) < 10 or not trades:
        return None

    span_s = (ts[-1] - ts[0]) / 1000.0
    # Scale-free volatility, and the resolution the tick grid gives it.
    level = sorted(mid)[len(mid) // 2]
    tick_bp = 1e4 / level if level else float("nan")
    traversed = sum(abs(mid[i] - mid[i - 1]) for i in range(1, len(mid)))
    ticks_per_s = traversed / span_s if span_s > 0 else float("nan")
    sigma_1s = realised_sigma(ts, mid, 1000.0)
    spread_bp = (sum(spr) / len(spr)) * tick_bp if spr else float("nan")
    # What a maker is paid for one round trip, over one second of price risk.
    # Both numerator and denominator are in basis points, so this survives any
    # choice of tick size -- which is the whole reason it is here.
    sp_sigma = spread_bp / sigma_1s if sigma_1s > 0 else float("nan")

    # ---- lift, adverse selection, and mean signed impact ----
    n_print = n_move = n_adv = n_adv_given_move = 0
    impact_bps_sum = 0.0
    for t, sgn, _q in trades:
        d_ = moved(ts, mid, t, tau_ms)
        if d_ is None:
            continue
        m0 = mid_at(ts, mid, t)
        n_print += 1
        if d_ != 0.0:
            n_move += 1
            if d_ * sgn > 0:
                n_adv_given_move += 1
        # Against the RESTING side: a taker buy lifts the ask, so the resting
        # side is short and an upward move is against it.
        if d_ * sgn > 0:
            n_adv += 1
        if m0:
            impact_bps_sum += 1e4 * (d_ * sgn) / m0

    # The null: the same question asked at moments chosen without reference to
    # any print. Sampled from the grid itself, so the two share a clock.
    rng = random.Random(seed)
    n_rand = n_rand_move = 0
    for _ in range(max(2000, n_print)):
        t = rng.uniform(ts[0], max(ts[0], ts[-1] - tau_ms))
        d_ = moved(ts, mid, t, tau_ms)
        if d_ is None:
            continue
        n_rand += 1
        if d_ != 0.0:
            n_rand_move += 1

    p_print = n_move / n_print if n_print else 0.0
    p_rand = n_rand_move / n_rand if n_rand else 0.0
    lift = (p_print / p_rand) if p_rand > 0 else float("nan")
    adverse = n_adv / n_print if n_print else float("nan")
    impact = impact_bps_sum / n_print if n_print else float("nan")
    adv_given_move = n_adv_given_move / n_move if n_move else float("nan")

    # ---- eta: continuations over twice the alternations ----
    moves = []
    for i in range(1, len(mid)):
        dm = mid[i] - mid[i - 1]
        if dm != 0.0:
            moves.append(1 if dm > 0 else -1)
    cont = alt = 0
    for i in range(1, len(moves)):
        if moves[i] == moves[i - 1]:
            cont += 1
        else:
            alt += 1
    eta = cont / (2.0 * alt) if alt else float("nan")

    # ---- trade sign autocorrelation ----
    signs = [s for _t, s, _q in trades]
    n = len(signs)
    mu = sum(signs) / n
    var = sum((s - mu) ** 2 for s in signs) / n

    def ac(lag):
        if n <= lag or var <= 0:
            return float("nan")
        c = sum((signs[i] - mu) * (signs[i + lag] - mu) for i in range(n - lag)) / (n - lag)
        return c / var

    return dict(label=label, span_s=span_s, trades=n_print, moves=len(moves),
                p_print=p_print, p_rand=p_rand, lift=lift, adverse=adverse,
                impact=impact, adv_given_move=adv_given_move,
                tick_bp=tick_bp, ticks_per_s=ticks_per_s,
                sigma_1s=sigma_1s, spread_bp=spread_bp, sp_sigma=sp_sigma,
                eta=eta, ac1=ac(1), ac10=ac(10),
                flat=1.0 - p_print)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", action="append", required=True,
                    help="a csv directory; repeat to compare")
    ap.add_argument("--label", action="append", required=True,
                    help="the label used for that directory, in the same order")
    ap.add_argument("--tau-ms", type=float, default=1000.0)
    a = ap.parse_args()
    if len(a.csv) != len(a.label):
        print("--csv and --label must be given the same number of times")
        return 2

    rows = []
    for c, l in zip(a.csv, a.label):
        r = scorecard(c, l, a.tau_ms)
        if r is None:
            print(f"  {l}: no usable data in {c}")
            continue
        rows.append(r)
    if not rows:
        return 1

    print(f"\ntrade price impact at tau = {a.tau_ms:.0f} ms\n")
    print(f"{'':<12}{'trades':>8}{'span_s':>9}{'P(mv|prt)':>11}{'P(mv|rnd)':>11}"
          f"{'lift':>7}{'adverse':>9}{'adv|mv':>9}{'impact_bp':>11}"
          f"{'tick_bp':>9}{'ticks/s':>9}{'sig(1s)':>9}{'spr_bp':>8}{'spr/sig':>9}"
          f"{'eta':>7}{'ac1':>7}{'ac10':>7}")
    print("-" * 160)
    for r in rows:
        print(f"{r['label']:<12}{r['trades']:>8}{r['span_s']:>9.0f}"
              f"{100*r['p_print']:>10.1f}%{100*r['p_rand']:>10.1f}%"
              f"{r['lift']:>7.2f}{100*r['adverse']:>8.1f}%{100*r['adv_given_move']:>8.1f}%"
              f"{r['impact']:>11.4f}"
              f"{r['tick_bp']:>9.4f}{r['ticks_per_s']:>9.2f}"
              f"{r['sigma_1s']:>9.4f}{r['spread_bp']:>8.4f}{r['sp_sigma']:>9.2f}"
              f"{r['eta']:>7.2f}{r['ac1']:>7.2f}{r['ac10']:>7.2f}")
    print("\n  lift       1.0 = a print says nothing about the next second")
    print("  adverse    50% = a coin flip; a maker's markout is made of this")
    print("  adv|mv     of the times the mid DID move, how often it went with the trade")
    print("  impact_bp  mean signed mid change after a print, in basis points")
    print("  tick_bp    what one tick is worth, in basis points")
    print("  sig(1s)    realised volatility: sd of the 1-second log return, in bp.")
    print("             THIS is the comparable number, not ticks/s")
    print("  spr_bp     mean quoted spread, in basis points")
    print("  spr/sig    what a maker is paid per round trip, over one second of")
    print("             price risk. Both in bp, so it survives any tick size --")
    print("             and it is the single number that says whether making a")
    print("             market on this book is easy or hard")
    print("  eta        0.5 = random walk -- but only comparable at equal tick_bp")
    print("  ac1/10     trade-sign autocorrelation; order splitting makes it positive")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
