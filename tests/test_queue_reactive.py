#!/usr/bin/env python3
"""The queue-reactive intensity table must account for all of the time.

An intensity is events over exposure. If a queue's exposure does not add up to
the capture's duration, some of the time it spent at some size went unrecorded —
and the rates for those sizes come out too high, by exactly the fraction lost.

Nothing about that failure is visible in the output: the table still has
plausible numbers in it. So it is asserted here rather than eyeballed.
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
            for row in csv.DictReader((out / f"{pair}_qr.csv").open()):
                k = (row["side"], row["level"])
                expo[k] = expo.get(k, 0.0) + float(row["exposure_s"])
            if not expo:
                print(f"FAIL {pair}: no queue-reactive rows at all")
                fails += 1
                continue

            worst = max(abs(v - span) / span for v in expo.values())
            ok = worst < 0.02
            print(f"  {'ok  ' if ok else 'FAIL'} {pair}: {len(expo)} queues, span {span:.0f}s, "
                  f"worst exposure error {100*worst:.2f}%")
            fails += not ok

    print(f"\n{'PASS' if not fails else 'FAIL'}: queue exposure accounts for the capture")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
