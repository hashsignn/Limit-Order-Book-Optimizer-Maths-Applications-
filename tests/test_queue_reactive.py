#!/usr/bin/env python3
"""The queue-reactive intensity table must account for all of the time, and
must resolve the queue axis finely enough to see a shape on it.

An intensity is events over exposure. If a queue's exposure does not add up to
the capture's duration, some of the time it spent at some size went unrecorded —
and the rates for those sizes come out too high, by exactly the fraction lost.

The second check exists because the axis used to be log2 of raw quantity, which
put the entire range Huang et al. measure — zero to forty average event sizes —
into six buckets. The table still had plausible numbers in it; there was simply
no curve left to fit. Neither failure is visible in the output, so both are
asserted here rather than eyeballed.
"""
import pathlib
import subprocess
import sys
import tempfile

import csv


def main() -> int:
    stats, samples = sys.argv[1], pathlib.Path(sys.argv[2])
    caps = sorted(samples.glob("*_bitstamp.jsonl.gz"))
    if not caps:
        print("no captures; nothing to check")
        return 0

    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp)
        for cap in caps:
            pair = cap.name.split("_")[0]
            r = subprocess.run([stats, "--capture", str(cap), "--outdir", str(out),
                                "--grid-ms", "100"], capture_output=True, text=True)
            if r.returncode != 0:
                print(f"FAIL {pair}: stats exited {r.returncode}\n{r.stderr}")
                return 1

            mids = list(csv.DictReader((out / f"{pair}_mid.csv").open()))
            if len(mids) < 2:
                print(f"  skip {pair}: too few mid samples")
                continue
            span = (float(mids[-1]["t_ms"]) - float(mids[0]["t_ms"])) / 1000.0

            expo = {}
            aes = {}
            touch_buckets = set()
            for row in csv.DictReader((out / f"{pair}_qr.csv").open()):
                k = (row["side"], row["level"])
                expo[k] = expo.get(k, 0.0) + float(row["exposure_s"])
                aes[row["level"]] = float(row["aes"])
                if row["level"] == "0" and float(row["exposure_s"]) > 0.0:
                    touch_buckets.add(int(row["q_aes"]))
            if not expo:
                print(f"FAIL {pair}: no queue-reactive rows at all")
                fails += 1
                continue

            worst = max(abs(v - span) / span for v in expo.values())
            ok = worst < 0.02
            print(f"  {'ok  ' if ok else 'FAIL'} {pair}: {len(expo)} queues, span {span:.0f}s, "
                  f"worst exposure error {100*worst:.2f}%")
            fails += not ok

            # The scale froze, and to something positive. An average event size
            # of zero means the axis silently became raw quantity.
            bad_aes = [lv for lv, v in aes.items() if not v > 0.0]
            if bad_aes:
                print(f"  FAIL {pair}: average event size is zero at level(s) {sorted(bad_aes)}")
                fails += 1

            # And the touch spreads over enough of the axis to fit a slope on.
            # Six is what the old log2 grid managed across the whole range; a
            # regression to that shape should fail rather than quietly return
            # three points and a confident number.
            occupied = len([q for q in touch_buckets if q > 0])
            spread_ok = occupied >= 6
            print(f"  {'ok  ' if spread_ok else 'FAIL'} {pair}: touch occupies {occupied} "
                  f"non-empty queue-size buckets, AES {aes.get('0', 0):,.0f}")
            fails += not spread_ok

    print(f"\n{'PASS' if not fails else 'FAIL'}: exposure accounts for the capture and the queue axis resolves")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
