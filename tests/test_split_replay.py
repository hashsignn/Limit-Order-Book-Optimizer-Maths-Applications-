#!/usr/bin/env python3
"""A capture split across files must replay identically to the whole.

An hours-long recording rotates hourly, so what lands on disk is N files and a
single snapshot beside the FIRST of them. `stats --capture-dir` has to seed once
and stream through the lot in order.

Getting this wrong does not fail loudly. The book would simply be reseeded or
truncated at each file boundary, and every distribution measured afterwards
would be measured from a book that briefly described a different market.
"""
import gzip
import pathlib
import subprocess
import shutil
import sys
import tempfile

def main() -> int:
    stats, samples = sys.argv[1], pathlib.Path(sys.argv[2])
    caps = sorted(samples.glob("*_bitstamp.jsonl.gz"))
    if not caps:
        print("no captures; nothing to check")
        return 0
    src = caps[0]
    snap = pathlib.Path(str(src).replace("_bitstamp.jsonl.gz", "_snapshot.json"))
    if not snap.exists():
        print(f"no snapshot beside {src.name}; skipping")
        return 0

    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        whole, split, out_w, out_s = (tmp / d for d in ("whole", "split", "ow", "os"))
        for d in (whole, split, out_w, out_s):
            d.mkdir()
        shutil.copy(src, whole / src.name)
        shutil.copy(snap, whole / snap.name)

        # Three pieces, named so they sort in stream order the way the
        # recorder's UTC stamps do. One snapshot, beside the first only.
        lines = gzip.open(src, "rt", encoding="utf-8").readlines()
        stem = src.name.split("_")[0]
        n = len(lines) // 3
        pieces = [lines[:n], lines[n:2 * n], lines[2 * n:]]
        shutil.copy(snap, split / f"{stem}_20200101T000000Z_snapshot.json")
        for i, chunk in enumerate(pieces):
            with gzip.open(split / f"{stem}_20200101T0000{i:02d}Z_bitstamp.jsonl.gz",
                           "wt", encoding="utf-8") as f:
                f.writelines(chunk)

        for src_dir, out in ((whole, out_w), (split, out_s)):
            r = subprocess.run([stats, "--capture-dir", str(src_dir), "--outdir", str(out),
                                "--grid-ms", "100"], capture_output=True, text=True)
            if r.returncode != 0:
                print(f"FAIL stats exited {r.returncode}\n{r.stderr}")
                return 1

        for csv in sorted(out_w.glob("*.csv")):
            other = out_s / csv.name
            same = other.exists() and csv.read_bytes() == other.read_bytes()
            print(f"  {'ok  ' if same else 'FAIL'} {csv.name}")
            fails += not same

    print(f"\n{'PASS' if not fails else 'FAIL'}: split replay reproduces the whole "
          f"({len(pieces)} files, 1 snapshot)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
