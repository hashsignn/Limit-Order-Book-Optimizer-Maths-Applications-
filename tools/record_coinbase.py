#!/usr/bin/env python3
"""Record Coinbase Exchange level-3 (full channel) market data.

Captures the order-by-order feed to disk so it can be replayed through the C++
book. Writes raw JSON, unmodified: the decoder lives in C++ and can change
without needing a re-capture. Never pre-process at the recording stage — you
cannot get back what you threw away.

    pip install websockets requests
    python tools/record_coinbase.py --product BTC-USD --minutes 10

Output, per run, in data/:
    <product>_<utc>_snapshot.json     the level-3 book at the sync point
    <product>_<utc>_events.jsonl.gz   every message after it, one per line

THE SYNC PROCEDURE, which is the part that matters:

    1. open the websocket and START BUFFERING
    2. THEN fetch the REST snapshot
    3. drop buffered messages with sequence <= snapshot sequence
    4. apply the rest

Done the other way round you lose every message between the snapshot and the
subscription. The book then drifts from reality with no error and no symptom
until the numbers are quietly wrong. Sequence gaps are logged loudly for the
same reason.

No API key: Coinbase Exchange market data channels are public.
"""

import argparse
import asyncio
import gzip
import json
import pathlib
import signal
import sys
import time

from recorder_retry import run_with_retry
from datetime import datetime, timezone

try:
    import requests
    import websockets
except ImportError:
    sys.exit("missing dependencies:  pip install websockets requests")

WS_URL = "wss://ws-feed.exchange.coinbase.com"
REST_URL = "https://api.exchange.coinbase.com/products/{product}/book?level=3"


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


class Recorder:
    def __init__(self, product: str, outdir: pathlib.Path, rotate_minutes: int):
        self.product = product
        self.outdir = outdir
        self.rotate_seconds = rotate_minutes * 60
        self.stop = False

        self.messages = 0
        self.gaps = 0
        self.bytes_written = 0
        self.last_sequence = None
        self.by_type: dict[str, int] = {}
        self.file = None
        self.file_opened_at = 0.0
        self.file_index = 0

    # ---- output ---------------------------------------------------------
    def _open_file(self) -> None:
        self._close_file()
        name = f"{self.product}_{utc_stamp()}_events.jsonl.gz"
        path = self.outdir / name
        self.file = gzip.open(path, "wt", encoding="utf-8")
        self.file_opened_at = time.time()
        self.file_index += 1
        print(f"  -> writing {path.name}", flush=True)

    def _close_file(self) -> None:
        if self.file is not None:
            self.file.close()
            self.file = None

    def _write(self, obj: dict) -> None:
        if self.file is None or (time.time() - self.file_opened_at) > self.rotate_seconds:
            self._open_file()
        line = json.dumps(obj, separators=(",", ":"))
        self.file.write(line + "\n")
        self.bytes_written += len(line) + 1

    # ---- capture --------------------------------------------------------
    # One connection. Returns True if it ended because the clock ran out
    # rather than because the connection died; tools/recorder_retry.py
    # decides whether to come back and how long to wait first.
    async def _session(self, duration_seconds: float) -> bool:
        self.outdir.mkdir(parents=True, exist_ok=True)
        started = time.time()

        print(f"connecting to {WS_URL}", flush=True)
        async with websockets.connect(WS_URL, max_size=None, ping_interval=20) as ws:
            await ws.send(json.dumps({
                "type": "subscribe",
                "product_ids": [self.product],
                "channels": ["full"],
            }))

            # ---- step 1: buffer BEFORE taking the snapshot ----
            buffered: list[dict] = []
            confirmed = False
            print("buffering while the snapshot is fetched...", flush=True)
            while not confirmed:
                msg = json.loads(await ws.recv())
                if msg.get("type") == "subscriptions":
                    confirmed = True
                elif msg.get("type") == "error":
                    sys.exit(f"subscribe failed: {msg}")
                else:
                    buffered.append(msg)

            # Keep buffering while the REST call is in flight.
            async def drain():
                try:
                    while True:
                        buffered.append(json.loads(
                            await asyncio.wait_for(ws.recv(), timeout=0.25)))
                except (asyncio.TimeoutError, Exception):
                    return

            drain_task = asyncio.create_task(drain())

            # ---- step 2: the snapshot ----
            # Status- and shape-checked before use; see record_bitstamp.py.
            resp = requests.get(REST_URL.format(product=self.product), timeout=30)
            resp.raise_for_status()
            snap = resp.json()
            if not isinstance(snap.get("bids"), list) or not snap.get("asks"):
                raise RuntimeError(f"unexpected snapshot shape: {sorted(snap)[:6]}")
            snap_seq = snap.get("sequence")
            if snap_seq is None:
                sys.exit(f"snapshot has no sequence field: {list(snap)[:5]}")

            drain_task.cancel()
            try:
                await drain_task
            except asyncio.CancelledError:
                pass

            snap_path = self.outdir / f"{self.product}_{utc_stamp()}_snapshot.json"
            snap_path.write_text(json.dumps(snap), encoding="utf-8")
            n_bids = len(snap.get("bids", []))
            n_asks = len(snap.get("asks", []))
            print(f"snapshot seq={snap_seq}  {n_bids} bid orders, {n_asks} ask orders", flush=True)
            print(f"  -> {snap_path.name}", flush=True)

            # ---- step 3: drop what the snapshot already contains ----
            kept = [m for m in buffered if (m.get("sequence") or 0) > snap_seq]
            print(f"buffered {len(buffered)}, kept {len(kept)} after the snapshot point", flush=True)
            for m in kept:
                self._record(m)

            # ---- step 4: stream ----
            print(f"recording {self.product} for {duration_seconds/60:.1f} min "
                  f"(ctrl-c to stop early)\n", flush=True)
            last_report = time.time()
            while not self.stop and (time.time() - started) < duration_seconds:
                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=5.0)
                except asyncio.TimeoutError:
                    continue
                self._record(json.loads(raw))

                if time.time() - last_report >= 15.0:
                    self._report(time.time() - started)
                    last_report = time.time()

        self._close_file()
        self._summary(time.time() - started)
        return (time.time() - started) >= duration_seconds or self.stop

    async def run(self, duration_seconds: float) -> None:
        self.outdir.mkdir(parents=True, exist_ok=True)
        await run_with_retry(self, duration_seconds)

    def _record(self, msg: dict) -> None:
        seq = msg.get("sequence")
        if seq is not None:
            if self.last_sequence is not None and seq != self.last_sequence + 1:
                # A gap means the book cannot be reconstructed exactly across
                # it. Recorded rather than hidden, so the replay can decide
                # whether to resync or refuse.
                missing = seq - self.last_sequence - 1
                self.gaps += 1
                print(f"  \033[33mSEQUENCE GAP: {missing} message(s) missing "
                      f"before seq {seq}\033[0m", flush=True)
            self.last_sequence = seq

        # Local receive time, in nanoseconds. The exchange stamps its own time
        # in the message; the difference between the two is not a latency
        # measurement (unrelated clocks) but it is worth having.
        msg["_recv_ns"] = time.time_ns()

        self.messages += 1
        t = msg.get("type", "?")
        self.by_type[t] = self.by_type.get(t, 0) + 1
        self._write(msg)

    def _report(self, elapsed: float) -> None:
        rate = self.messages / elapsed if elapsed > 0 else 0.0
        mb = self.bytes_written / 1e6
        print(f"  {elapsed/60:5.1f} min | {self.messages:8d} msgs | "
              f"{rate:6.0f}/s | {mb:7.1f} MB raw | {self.gaps} gaps", flush=True)

    def _summary(self, elapsed: float) -> None:
        print("\n" + "=" * 62)
        print(f"product        {self.product}")
        print(f"duration       {elapsed/60:.1f} min")
        print(f"messages       {self.messages}")
        print(f"rate           {self.messages/elapsed if elapsed else 0:.0f}/s")
        print(f"raw size       {self.bytes_written/1e6:.1f} MB (gzipped on disk)")
        print(f"files          {self.file_index}")
        print(f"sequence gaps  {self.gaps}" + ("  <-- book cannot be replayed exactly across these"
                                               if self.gaps else "  (clean)"))
        print("\nmessage mix")
        for t, n in sorted(self.by_type.items(), key=lambda kv: -kv[1]):
            share = 100.0 * n / self.messages if self.messages else 0.0
            print(f"  {t:<12} {n:8d}  {share:5.1f}%")
        print("=" * 62)
        if self.messages == 0:
            print("\nNo messages. Check the product id is valid and tradable.")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--product", default="BTC-USD", help="e.g. BTC-USD, ETH-USD, DOGE-USD")
    ap.add_argument("--minutes", type=float, default=10.0, help="how long to record")
    ap.add_argument("--outdir", default="data", help="output directory (gitignored)")
    ap.add_argument("--rotate-minutes", type=int, default=60,
                    help="start a new file this often, so a long run is not one huge file")
    args = ap.parse_args()

    rec = Recorder(args.product, pathlib.Path(args.outdir), args.rotate_minutes)

    def on_sigint(_sig, _frame):
        print("\nstopping...", flush=True)
        rec.stop = True
    signal.signal(signal.SIGINT, on_sigint)

    asyncio.run(rec.run(args.minutes * 60.0))


if __name__ == "__main__":
    main()
