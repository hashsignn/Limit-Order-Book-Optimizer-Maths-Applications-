#!/usr/bin/env python3
"""One reconnect loop, shared by every recorder.

A capture is hours of wall time against a market that will not repeat, so the
single most expensive failure a recorder can have is ending early and telling
nobody until afterwards. `record_bitstamp.py` grew a retry loop for that reason;
`record_bitfinex.py` and `record_coinbase.py` did not, and a dropped connection
three minutes into an eight-hour run ended them. The loop lives here now so the
next fix to it lands everywhere at once.

A recorder supplies:
    rec.stop                     set by the SIGINT handler
    await rec._session(seconds)  one connection, returning True if it ended
                                 because the clock ran out rather than because
                                 the connection died
"""
import asyncio
import time


async def run_with_retry(rec, duration_seconds: float, *, max_backoff: float = 60.0) -> None:
    """Records for `duration_seconds` of WALL time, across as many sessions as
    it takes. Returns when the clock runs out or ctrl-c is pressed."""
    started = time.time()
    backoff = 1.0
    sessions = 0

    while not rec.stop and (time.time() - started) < duration_seconds:
        remaining = duration_seconds - (time.time() - started)
        sessions += 1
        try:
            clean = await rec._session(remaining)
            backoff = 1.0 if clean else min(backoff * 2.0, max_backoff)
        except Exception as exc:                              # noqa: BLE001
            print(f"  \033[33msession ended: {type(exc).__name__}: {exc}\033[0m", flush=True)
            backoff = min(backoff * 2.0, max_backoff)

        if rec.stop or (time.time() - started) >= duration_seconds:
            break

        # Every reconnect costs a REST snapshot on the venues that need one, and
        # those endpoints are rate limited. Backing off is not politeness, it is
        # the difference between a capture and a throttled IP.
        #
        # Never sleep past the deadline: a capture asked for 8 hours should end
        # at 8 hours, not at 8 hours plus whatever the backoff happened to be.
        nap = min(backoff, duration_seconds - (time.time() - started))
        if nap <= 0:
            break
        left = (duration_seconds - (time.time() - started)) / 60.0
        print(f"  reconnecting in {nap:.0f}s ({left:.0f} min left)", flush=True)
        await asyncio.sleep(nap)

    if sessions > 1:
        print(f"  capture spanned {sessions} sessions; every reconnect is a gap "
              f"in the sequence chain and the decoder will report it", flush=True)
