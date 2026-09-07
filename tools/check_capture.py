#!/usr/bin/env python3
"""Inspects a capture directory and reports whether it is usable, without
building anything.

    py tools/check_capture.py data/eth8h

Answers the questions that decide whether hours of recording were worth it:
how many sessions, where the boundaries are, whether each names a snapshot that
exists, how far the price travelled, and whether any file is a stub from an
interrupted run. Pure Python and one pass over the bytes, so it runs wherever
the recorder ran -- which is the point, because a capture is worth checking
before the toolchain that consumes it is working.
"""
import argparse
import gzip
import json
import pathlib
import sys


def scan(path):
    """One pass. Full JSON parsing 10M lines is minutes; the fields that matter
    are found by substring first and parsed only when present."""
    n = created = deleted = changed = trades = 0
    t0 = t1 = None
    # TRADE prices, not order prices. Someone always has a sell resting at
    # 999,999,999 and a buy at 0.01; the min and max of the order book say
    # nothing about where the market was, and sizing the price window from them
    # would ask for a window a billion wide. A print is by definition a price
    # both sides agreed on.
    lo = hi = None
    markers = []
    with gzip.open(path, "rt", encoding="utf-8", errors="replace") as f:
        for line in f:
            n += 1
            if '"_meta"' in line:
                try:
                    markers.append((n, json.loads(line)))
                except ValueError:
                    pass
                continue
            i = line.find('"microtimestamp":"')
            if i >= 0:
                j = line.find('"', i + 18)
                try:
                    ts = int(line[i + 18:j])
                    if t0 is None:
                        t0 = ts
                    t1 = ts
                except ValueError:
                    pass
            if '"order_created"' in line:
                created += 1
            elif '"order_deleted"' in line:
                deleted += 1
            elif '"order_changed"' in line:
                changed += 1
            elif '"trade"' in line:
                trades += 1
                q = line.find('"price":')
                if q >= 0:
                    j = q + 8
                    k = j
                    while k < len(line) and (line[k].isdigit() or line[k] == "."):
                        k += 1
                    try:
                        v = float(line[j:k])
                        if v > 0:
                            lo = v if lo is None or v < lo else lo
                            hi = v if hi is None or v > hi else hi
                    except ValueError:
                        pass
    return dict(lines=n, created=created, deleted=deleted, changed=changed,
                trades=trades, t0=t0, t1=t1, lo=lo, hi=hi, markers=markers)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("directory")
    a = ap.parse_args()
    d = pathlib.Path(a.directory)
    caps = sorted(d.glob("*_bitstamp.jsonl.gz"))
    snaps = {p.name for p in d.glob("*_snapshot.json")}
    if not caps:
        print(f"no captures in {d}")
        return 1

    # One directory, one instrument. Pointed at a mixed directory every
    # aggregate below is pooled across instruments that share nothing but a
    # folder -- data/samples holds xrpusd at $1.40 and btcusd at $79,875, and
    # the "price range" computed over both asks for a window 200% wide.
    pairs = sorted({c.name.split("_")[0] for c in caps})
    if len(pairs) > 1:
        print(f"\033[31m{d} holds {len(pairs)} instruments: {', '.join(pairs)}\033[0m")
        print("A capture directory must hold one pair. Nothing below would mean anything.")
        return 1

    print(f"{d}: {pairs[0]}, {len(caps)} capture file(s), {len(snaps)} snapshot(s)\n")
    tot = dict(lines=0, created=0, deleted=0, trades=0)
    lo = hi = None
    prev_end = None
    sessions = 0
    problems = []

    for c in caps:
        r = scan(c)
        for k in tot:
            tot[k] += r[k]
        if r["lo"] is not None:
            lo = r["lo"] if lo is None else min(lo, r["lo"])
            hi = r["hi"] if hi is None else max(hi, r["hi"])
        span = (r["t1"] - r["t0"]) / 1e6 if r["t0"] and r["t1"] else 0.0
        print(f"  {c.name}")
        print(f"    {c.stat().st_size/1e6:8.1f} MB  {r['lines']:>9,} lines  "
              f"{span/60:6.1f} min  {r['created']:>8,} created  {r['trades']:>6,} trades")
        if span < 60 and r["lines"] > 0:
            problems.append(f"{c.name} covers only {span:.0f}s -- a stub from an "
                            f"interrupted run; consider deleting it")
        # A gap between consecutive files is a stopped-and-restarted recorder.
        if prev_end is not None and r["t0"] is not None:
            gap = (r["t0"] - prev_end) / 1e6
            if gap > 5.0:
                print(f"    \033[33mgap of {gap/60:.1f} min before this file\033[0m")
        if r["t1"] is not None:
            prev_end = r["t1"]
        for lineno, m in r["markers"]:
            kind = m.get("_meta")
            if kind == "session_start":
                sessions += 1
                sn = m.get("_snapshot", "")
                ok = sn in snaps
                print(f"    line {lineno:>9,}: session {m.get('_session')} starts, "
                      f"snapshot {sn} {'ok' if ok else 'MISSING'}")
                if not ok:
                    problems.append(f"session marker names {sn!r}, which is not in {d}")
            elif kind == "request_reconnect":
                print(f"    line {lineno:>9,}: server asked to reconnect")

    print(f"\n  TOTAL  {tot['lines']:,} lines, {tot['created']:,} created, "
          f"{tot['deleted']:,} deleted, {tot['trades']:,} trades")
    print(f"  sessions with a start marker: {sessions}")
    if lo is not None and hi is not None and lo > 0:
        # The book's price window is set once from the OPENING mid and never
        # recentred, so it has to reach from that opening price to the furthest
        # the market got in either direction -- which is not the same as half the
        # range, when the move is one-sided.
        mid0 = (hi + lo) / 2
        reach = max(abs(hi - lo), abs(lo - hi)) / mid0
        print(f"  traded {lo:,.2f} .. {hi:,.2f}  ({100*reach:.2f}% end to end)")
        need = max(0.02, reach * 1.5)          # 50% headroom over the observed move
        print(f"  -> run stats with  --band-pct {need:.2f}")
        print("     stats then prints the real touch excursion and the headroom left;")
        print("     that is the number to trust, this one just picks a safe start.")
    else:
        print("  no trades found, so the price range is unknown")
    if problems:
        print("\n  \033[33missues\033[0m")
        for p in problems:
            print(f"    - {p}")
    else:
        print("\n  no problems found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
