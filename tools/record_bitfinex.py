#!/usr/bin/env python3
"""Record Bitfinex raw order books (R0) — order-level market data, no account.

Bitfinex's `book` channel at precision R0 publishes individual orders with
their order ids, on a public endpoint that needs no API key. That makes it the
one L3 source available with zero friction.

    py -m pip install websockets
    py tools/record_bitfinex.py --symbol tBTCUSD --minutes 10

Output, per run, in data/:
    <symbol>_<utc>_bitfinex.jsonl.gz    every frame, one per line, raw

WIRE FORMAT
    subscribe   {"event":"subscribe","channel":"book","prec":"R0",
                 "symbol":"tBTCUSD","len":250}
    snapshot    [CHAN_ID, [[ORDER_ID, PRICE, AMOUNT], ...]]
    update      [CHAN_ID, [ORDER_ID, PRICE, AMOUNT]]
    heartbeat   [CHAN_ID, "hb"]

    AMOUNT > 0  bid          AMOUNT < 0  ask
    PRICE == 0  the order is gone (cancelled, filled, or fell out of the window)

THE LIMITATION, STATED UP FRONT
    R0 is a WINDOW of the top `len` orders per side, not the whole book. An
    order that drops out of the window is reported exactly like a cancellation,
    because the feed cannot distinguish them.

    So cancel rates measured deep in the book are wrong, and any statistic that
    depends on an order's full lifetime is only trustworthy for orders that
    stayed inside the window. At the touch — which is where queue position
    lives, and the reason we wanted L3 at all — this does not bite. Use len=250
    and treat deep-book lifetime statistics as unavailable rather than as
    measured.

    Coinbase's full channel has no such window, but now requires an API key.

Raw frames are written unmodified. The decoder is C++ and can change without
forcing a re-capture; you cannot recover what you discarded at record time.
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
    import websockets
except ImportError:
    sys.exit("missing dependency:  py -m pip install websockets")

WS_URL = "wss://api-pub.bitfinex.com/ws/2"


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


class Recorder:
    def __init__(self, symbol: str, length: int, outdir: pathlib.Path, rotate_minutes: int):
        self.symbol = symbol
        self.length = length
        self.outdir = outdir
        self.rotate_seconds = rotate_minutes * 60
        self.stop = False

        self.frames = 0          # websocket frames written
        self.orders = 0          # individual order updates inside them
        self.inserts = 0
        self.deletes = 0
        self.heartbeats = 0
        self.snapshot_orders = 0
        self.bytes_written = 0
        self.file = None
        self.file_opened_at = 0.0
        self.file_index = 0

    # ---- output ---------------------------------------------------------
    def _open_file(self) -> None:
        self._close_file()
        path = self.outdir / f"{self.symbol}_{utc_stamp()}_bitfinex.jsonl.gz"
        self.file = gzip.open(path, "wt", encoding="utf-8")
        self.file_opened_at = time.time()
        self.file_index += 1
        print(f"  -> writing {path.name}", flush=True)

    def _close_file(self) -> None:
        if self.file is not None:
            self.file.close()
            self.file = None

    def _write(self, obj) -> None:
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
                "event": "subscribe",
                "channel": "book",
                "prec": "R0",          # raw: individual orders, with ids
                "freq": "F0",          # realtime, not throttled
                "symbol": self.symbol,
                "len": str(self.length),
            }))

            # Wait for the subscription to be confirmed or refused, so a bad
            # symbol fails immediately rather than after ten silent minutes.
            while True:
                msg = json.loads(await ws.recv())
                if isinstance(msg, dict):
                    ev = msg.get("event")
                    if ev == "subscribed":
                        print(f"subscribed: {self.symbol} prec=R0 len={self.length} "
                              f"chanId={msg.get('chanId')}", flush=True)
                        self._write({"_meta": "subscribed", **msg, "_recv_ns": time.time_ns()})
                        break
                    if ev == "error":
                        sys.exit(f"subscribe failed: {msg.get('msg')} (code {msg.get('code')})")
                    if ev == "info":
                        continue

            print(f"recording {self.symbol} for {duration_seconds/60:.1f} min "
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

    def _record(self, msg) -> None:
        # Everything after subscription is [CHAN_ID, payload].
        if isinstance(msg, list) and len(msg) >= 2:
            payload = msg[1]
            if payload == "hb":
                self.heartbeats += 1
                return                      # heartbeats carry no book information
            if isinstance(payload, list):
                if payload and isinstance(payload[0], list):
                    # snapshot: a list of [id, price, amount]
                    self.snapshot_orders = len(payload)
                    print(f"snapshot: {len(payload)} orders", flush=True)
                    for o in payload:
                        self.orders += 1
                        self.inserts += 1
                else:
                    # single update
                    self.orders += 1
                    price = payload[1] if len(payload) > 1 else None
                    if price == 0:
                        self.deletes += 1
                    else:
                        self.inserts += 1

        self.frames += 1
        self._write({"m": msg, "_recv_ns": time.time_ns()})

    def _report(self, elapsed: float) -> None:
        rate = self.orders / elapsed if elapsed > 0 else 0.0
        print(f"  {elapsed/60:5.1f} min | {self.orders:8d} order updates | "
              f"{rate:6.0f}/s | {self.bytes_written/1e6:7.1f} MB raw", flush=True)

    def _summary(self, elapsed: float) -> None:
        print("\n" + "=" * 62)
        print(f"symbol            {self.symbol}   (prec R0, len {self.length})")
        print(f"duration          {elapsed/60:.1f} min")
        print(f"websocket frames  {self.frames}")
        print(f"order updates     {self.orders}")
        print(f"  inserts/changes {self.inserts}")
        print(f"  removals        {self.deletes}   (cancel, fill, OR fell out of the window)")
        print(f"snapshot size     {self.snapshot_orders} orders")
        print(f"heartbeats        {self.heartbeats}")
        print(f"rate              {self.orders/elapsed if elapsed else 0:.0f} order updates/s")
        print(f"raw size          {self.bytes_written/1e6:.1f} MB (gzipped on disk)")
        print(f"files             {self.file_index}")
        print("=" * 62)
        if self.orders == 0:
            print("\nNo order updates. Check the symbol — Bitfinex uses a 't' prefix,"
                  "\ne.g. tBTCUSD, tETHUSD, tXRPUSD.")
        else:
            print("\nNOTE: removals cannot be separated from orders leaving the top-"
                  f"{self.length}\nwindow. Treat deep-book lifetime statistics as unavailable."
                  "\nAt the touch, where queue position lives, this does not bite.")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--symbol", default="tBTCUSD",
                    help="Bitfinex symbol, 't' prefixed: tBTCUSD, tETHUSD, tXRPUSD, tDOGE:USD")
    ap.add_argument("--minutes", type=float, default=10.0)
    ap.add_argument("--len", dest="length", type=int, default=250,
                    choices=[1, 25, 100, 250], help="orders per side in the window")
    ap.add_argument("--outdir", default="data")
    ap.add_argument("--rotate-minutes", type=int, default=60)
    args = ap.parse_args()

    rec = Recorder(args.symbol, args.length, pathlib.Path(args.outdir), args.rotate_minutes)

    def on_sigint(_sig, _frame):
        print("\nstopping...", flush=True)
        rec.stop = True
    signal.signal(signal.SIGINT, on_sigint)

    asyncio.run(rec.run(args.minutes * 60.0))


if __name__ == "__main__":
    main()
