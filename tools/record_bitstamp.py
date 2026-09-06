#!/usr/bin/env python3
"""Record Bitstamp level-3 order data — the full book, order by order, no account.

Bitstamp's live_orders channel is a genuine L3 event stream: order_created,
order_changed and order_deleted, each carrying an order id. Unlike Bitfinex's
raw books it is NOT windowed — this is the whole book, so order lifetimes and
cancel rates away from the touch are measurable rather than confounded.

    py -m pip install websockets requests
    py tools/record_bitstamp.py --pair btcusd --minutes 10

Output, per run, in data/:
    <pair>_<utc>_snapshot.json        REST book with individual order ids
    <pair>_<utc>_bitstamp.jsonl.gz    every websocket message, raw

WHY TWO CHANNELS

    live_orders_<pair>   order_created / order_changed / order_deleted
    live_trades_<pair>   every trade, with buy_order_id and sell_order_id

    order_deleted does not say WHY the order left. A cancel and a fill look
    identical. The only way to tell them apart is to join against live_trades
    on the order id.

    That distinction is not cosmetic. Volume cancelled ahead of you is free
    progress up the queue; volume TRADED ahead of you is progress plus the
    information that someone is buying. They are different signals and
    aggregating them destroys both. Recording only live_orders would make the
    split permanently unrecoverable, so both channels are captured from the
    start.

    Known caveat: the join is not perfectly reliable. See
    https://github.com/phil8192/ob-analytics/issues/28 — an order_changed with
    an unchanged amount can be reported where no trade occurred. Treat the
    cancel/fill split as high quality but not exact, and say so in any result
    that depends on it.

SNAPSHOT SYNC
    live_orders carries no snapshot, so the book is seeded from REST
    order_book/<pair>?group=2, which returns individual orders rather than
    aggregated price levels. As with any snapshot-plus-stream feed, the
    websocket is subscribed and buffered BEFORE the REST call, so nothing that
    happens in between is lost. Bitstamp gives a microtimestamp on both sides
    to reconcile against.

Raw messages are written unmodified. The decoder is C++ and can change without
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
from datetime import datetime, timezone

try:
    import requests
    import websockets
except ImportError:
    sys.exit("missing dependencies:  py -m pip install websockets requests")

WS_URL = "wss://ws.bitstamp.net"
REST_BOOK = "https://www.bitstamp.net/api/v2/order_book/{pair}/?group=2"


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


class Recorder:
    def __init__(self, pair: str, outdir: pathlib.Path, rotate_minutes: int):
        self.pair = pair
        self.outdir = outdir
        self.rotate_seconds = rotate_minutes * 60
        self.stop = False

        self.messages = 0
        self.created = 0
        self.changed = 0
        self.deleted = 0
        self.trades = 0
        self.bytes_written = 0
        self.snapshot_orders = 0
        self.file = None
        self.file_opened_at = 0.0
        self.file_index = 0
        # A long capture is not one stream. Bitstamp asks clients to reconnect
        # periodically (bts:request_reconnect), and any reconnect means messages
        # were missed -- so the book cannot be repaired from the stream and needs
        # a fresh REST snapshot. The capture is therefore a SEQUENCE of sessions,
        # each one continuous, with an explicit marker between them. Replay must
        # reset the book at each marker and must never difference the mid across
        # one: that gap would read as an enormous price move that never happened.
        self.session = 0
        self.session_msgs = 0
        self.snapshots = []

    # ---- output ---------------------------------------------------------
    def _open_file(self) -> None:
        self._close_file()
        path = self.outdir / f"{self.pair}_{utc_stamp()}_bitstamp.jsonl.gz"
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
    async def run(self, duration_seconds: float) -> None:
        """Records for `duration_seconds` of WALL time, across as many sessions
        as it takes. Returns when the clock runs out or ctrl-c is pressed."""
        self.outdir.mkdir(parents=True, exist_ok=True)
        started = time.time()
        backoff = 1.0
        while not self.stop and (time.time() - started) < duration_seconds:
            remaining = duration_seconds - (time.time() - started)
            try:
                clean = await self._session(remaining)
                backoff = 1.0 if clean else min(backoff * 2.0, 60.0)
            except Exception as exc:                      # noqa: BLE001
                print(f"  \033[33msession ended: {type(exc).__name__}: {exc}\033[0m", flush=True)
                backoff = min(backoff * 2.0, 60.0)
            if self.stop or (time.time() - started) >= duration_seconds:
                break
            # Every reconnect costs a REST snapshot, and that endpoint is rate
            # limited. Backing off is not politeness, it is the difference
            # between a capture and a throttled IP.
            # Never sleep past the deadline: a capture asked for 8 hours should
            # end at 8 hours, not at 8 hours plus whatever the backoff was.
            nap = min(backoff, duration_seconds - (time.time() - started))
            if nap <= 0:
                break
            print(f"  reconnecting in {nap:.0f}s "
                  f"({(duration_seconds - (time.time() - started))/60:.0f} min left)", flush=True)
            await asyncio.sleep(nap)

        self._close_file()
        self._summary(time.time() - started)

    async def _session(self, budget_seconds: float) -> bool:
        """One connect-snapshot-stream cycle. True if it ended for a reason that
        is not an error (server asked to reconnect, or the budget ran out)."""
        self.session += 1
        self.session_msgs = 0
        session_started = time.time()
        orders_ch = f"live_orders_{self.pair}"
        trades_ch = f"live_trades_{self.pair}"

        print(f"\nsession {self.session}: connecting to {WS_URL}", flush=True)
        async with websockets.connect(WS_URL, max_size=None, ping_interval=20) as ws:
            for ch in (orders_ch, trades_ch):
                await ws.send(json.dumps({"event": "bts:subscribe", "data": {"channel": ch}}))

            # ---- buffer BEFORE the snapshot ----
            # Anything arriving between subscribing and the REST call has to be
            # kept, or the book starts with a hole nothing downstream can see.
            buffered = []
            confirmed = 0
            deadline = time.time() + 15.0
            while confirmed < 2 and time.time() < deadline:
                try:
                    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=5.0))
                except asyncio.TimeoutError:
                    continue
                ev = msg.get("event")
                if ev == "bts:subscription_succeeded":
                    confirmed += 1
                elif ev == "bts:error":
                    sys.exit(f"subscribe failed: {msg}")
                else:
                    buffered.append(msg)

            if confirmed < 2:
                # On the FIRST session this is a bad pair and there is no point
                # retrying. Later, it is a transient failure worth a backoff.
                if self.session == 1:
                    sys.exit(f"only {confirmed}/2 channels subscribed -- check the pair name "
                             f"(lowercase, no separator: btcusd, ethusd, xrpusd)")
                raise RuntimeError(f"only {confirmed}/2 channels subscribed")

            # Keep buffering while the REST call is in flight.
            async def drain():
                try:
                    while True:
                        buffered.append(json.loads(
                            await asyncio.wait_for(ws.recv(), timeout=0.25)))
                except Exception:
                    return
            drain_task = asyncio.create_task(drain())

            # ---- snapshot: group=2 gives individual orders, not price levels ----
            snap = requests.get(REST_BOOK.format(pair=self.pair), timeout=30).json()
            drain_task.cancel()
            try:
                await drain_task
            except asyncio.CancelledError:
                pass

            bids, asks = snap.get("bids", []), snap.get("asks", [])
            if not bids and not asks:
                raise RuntimeError("empty snapshot")
            # group=2 rows are [price, amount, order_id]; a 2-element row means
            # the request was silently aggregated and queue position is lost.
            if bids and len(bids[0]) < 3:
                sys.exit("snapshot has no order ids -- group=2 was not honoured")
            self.snapshot_orders = len(bids) + len(asks)

            snap_path = self.outdir / f"{self.pair}_{utc_stamp()}_snapshot.json"
            snap_path.write_text(json.dumps(snap), encoding="utf-8")
            self.snapshots.append(snap_path.name)
            print(f"  snapshot: {len(bids)} bids, {len(asks)} asks "
                  f"(microtimestamp {snap.get('microtimestamp')}) -> {snap_path.name}", flush=True)

            # The boundary marker. Replay resets the book here and reseeds from
            # the named snapshot; without it a reader would splice two different
            # books together and see the join as a price move.
            self._write({"_meta": "session_start",
                         "_session": self.session,
                         "_snapshot": snap_path.name,
                         "_microtimestamp": snap.get("microtimestamp"),
                         "_recv_ns": time.time_ns()})

            for m in buffered:
                self._record(m)
            print(f"  buffered {len(buffered)} message(s) during the snapshot", flush=True)

            last_report = time.time()
            while not self.stop and (time.time() - session_started) < budget_seconds:
                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=5.0)
                except asyncio.TimeoutError:
                    continue
                msg = json.loads(raw)
                if msg.get("event") == "bts:request_reconnect":
                    print("  \033[33mserver asked to reconnect\033[0m", flush=True)
                    self._write({"_meta": "request_reconnect",
                                 "_session": self.session, "_recv_ns": time.time_ns()})
                    return True
                self._record(msg)

                if time.time() - last_report >= 30.0:
                    self._report(time.time() - session_started)
                    last_report = time.time()
        return True

    def _record(self, msg) -> None:
        ev = msg.get("event")
        if ev == "order_created":
            self.created += 1
        elif ev == "order_changed":
            self.changed += 1
        elif ev == "order_deleted":
            self.deleted += 1
        elif ev == "trade":
            self.trades += 1
        elif ev in ("bts:subscription_succeeded", "bts:heartbeat"):
            return

        self.messages += 1
        self.session_msgs += 1
        msg["_recv_ns"] = time.time_ns()
        self._write(msg)

    def _report(self, elapsed: float) -> None:
        rate = self.session_msgs / elapsed if elapsed > 0 else 0.0
        print(f"  s{self.session} {elapsed/60:5.1f} min | {self.messages:8d} msgs | {rate:6.0f}/s | "
              f"created {self.created:6d}  deleted {self.deleted:6d}  trades {self.trades:5d} | "
              f"{self.bytes_written/1e6:6.1f} MB", flush=True)

    def _summary(self, elapsed: float) -> None:
        print("\n" + "=" * 66)
        print(f"pair              {self.pair}")
        print(f"duration          {elapsed/60:.1f} min")
        print(f"sessions          {self.session}  ({len(self.snapshots)} snapshots)")
        print(f"messages          {self.messages}")
        print(f"  order_created   {self.created}")
        print(f"  order_changed   {self.changed}")
        print(f"  order_deleted   {self.deleted}")
        print(f"  trade           {self.trades}")
        print(f"rate              {self.messages/elapsed if elapsed else 0:.0f}/s")
        on_disk = sum(f.stat().st_size for f in self.outdir.glob(f"{self.pair}_*.jsonl.gz"))
        print(f"uncompressed      {self.bytes_written/1e6:.1f} MB of JSON")
        print(f"on disk           {on_disk/1e6:.1f} MB gzipped"
              f"  ({self.bytes_written/max(on_disk,1):.1f}x)")
        if elapsed > 0:
            print(f"projected 8 h     {on_disk/1e6 * (8*3600/elapsed):.0f} MB on disk")
        print(f"files             {self.file_index}")
        if self.session > 1:
            print("\nThis capture is a SEQUENCE of sessions, not one stream.")
            print('Each  {"_meta":"session_start"}  line names the snapshot that reseeds')
            print("the book at that point. Replay must reset there, and must never")
            print("difference the mid across a boundary.")
            for i, name in enumerate(self.snapshots, 1):
                print(f"  session {i}: {name}")
        if self.deleted:
            pct = 100.0 * self.trades / self.deleted
            print(f"\ntrades / deletions  {pct:.1f}%")
            print("  Roughly the share of removals that were fills rather than cancels.")
            print("  A real number needs the order-id join; this is only a sanity check,")
            print("  and a very low value is normal — most orders are cancelled, not filled.")
        print("=" * 66)
        if self.messages == 0:
            print("\nNo messages. Check the pair: lowercase, no separator (btcusd, ethusd).")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pair", default="btcusd",
                    help="lowercase, no separator: btcusd, ethusd, xrpusd, ltcusd")
    ap.add_argument("--minutes", type=float, default=10.0,
                    help="total WALL time to record, across as many reconnects as it takes")
    ap.add_argument("--outdir", default="data")
    ap.add_argument("--rotate-minutes", type=int, default=60)
    args = ap.parse_args()

    rec = Recorder(args.pair.lower(), pathlib.Path(args.outdir), args.rotate_minutes)

    def on_sigint(_sig, _frame):
        print("\nstopping...", flush=True)
        rec.stop = True
    signal.signal(signal.SIGINT, on_sigint)

    asyncio.run(rec.run(args.minutes * 60.0))


if __name__ == "__main__":
    main()
