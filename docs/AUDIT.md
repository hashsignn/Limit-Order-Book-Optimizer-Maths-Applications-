# Repository audit

**Instalment 1 of a multi-pass audit.** 116 tracked files, 22,088 lines. 23 files
audited here; 93 pending and marked as such. Nothing below is asserted from
familiarity — every finding names the line or the command that produced it.

**Evidence commands run** (outputs quoted in the relevant sections):

```
grep -RIn --exclude-dir=.git --exclude-dir=build -E "API_KEY|SECRET|TOKEN|PASSWORD|passwd|aws_access_key|PRIVATE_KEY" .
git ls-files --others --exclude-standard
git ls-files -z | xargs -0 ls -l | awk '$5>50000'
cd build && cmake .. && cmake --build . && ctest --output-on-failure
```

Results: **no secrets found**, **no untracked files**, **26/26 tests pass**.

---

## 1. Repository checkbox tree

- `.github/`
  - `workflows/`
    - [ ] `ci.yml`
- [x] `.gitignore`
- [x] `All Rights Reserved`
- [x] `CMakeLists.txt`
- [ ] `CMakePresets.json`
- [ ] `README.md`
- `apps/`
  - `backtest/`
    - [x] `main.cpp`
  - `evaluate/`
    - [x] `main.cpp`
  - `jitter_probe/`
    - [ ] `main.cpp`
  - `latency_demo/`
    - [ ] `main.cpp`
  - `replay/`
    - [ ] `main.cpp`
  - `sim_demo/`
    - [ ] `main.cpp`
  - `solve/`
    - [x] `main.cpp`
  - `stats/`
    - [x] `main.cpp`
  - `tape/`
    - [ ] `main.cpp`
- `bench/`
  - [ ] `bench_book.cpp`
  - [ ] `bench_measure.cpp`
- `data/`
  - `samples/`
    - [ ] `btcusd_20260904T200134Z_bitstamp.jsonl.gz`
    - [ ] `btcusd_20260904T200134Z_snapshot.json`
    - [ ] `ethusd_20260904T202638Z_bitstamp.jsonl.gz`
    - [ ] `ethusd_20260904T202638Z_snapshot.json`
    - [ ] `xrpusd_20260904T201339Z_bitstamp.jsonl.gz`
    - [ ] `xrpusd_20260904T201339Z_snapshot.json`
- `docs/`
  - [ ] `00-scope-and-architecture.md`
  - [ ] `01-literature.md`
  - [ ] `02-data-and-protocols.md`
  - [ ] `03-metrics-and-estimators.md`
  - [ ] `04-toolchain.md`
  - [ ] `05-roadmap.md`
  - [ ] `06-queue-reactive-plan.md`
  - [ ] `BASELINE.md`
  - [ ] `KNOWN-ISSUES.md`
  - `figures/`
    - [ ] `01_order_lifetime.png`
    - [ ] `02_cancel_vs_fill.png`
    - [ ] `03_spread.png`
    - [ ] `04_depth_profile.png`
    - [ ] `05_interarrival.png`
    - [ ] `06_markout.png`
    - [ ] `07_ak_calibration.png`
    - [ ] `calibration.json`
- `fuzz/`
  - [ ] `decode.hpp`
  - [ ] `fuzz_bitstamp.cpp`
  - [ ] `fuzz_book.cpp`
  - [ ] `fuzz_matching.cpp`
  - [ ] `portable_main.cpp`
- `include/`
  - `lob/`
    - `book/`
      - [ ] `events.hpp`
      - [ ] `order_book.hpp`
      - [ ] `order_map.hpp`
      - [ ] `reference_book.hpp`
    - `core/`
      - [ ] `arena.hpp`
      - [ ] `compiler.hpp`
      - [ ] `types.hpp`
    - `feat/`
      - [ ] `features.hpp`
      - [x] `queue_reactive.hpp`
    - `feed/`
      - [x] `bitstamp.hpp`
      - [ ] `json.hpp`
      - [ ] `line_reader.hpp`
    - `measure/`
      - [ ] `histogram.hpp`
      - [ ] `journal.hpp`
      - [ ] `recorder.hpp`
      - [ ] `stopwatch.hpp`
      - [ ] `tsc.hpp`
    - `policy/`
      - [x] `mdp.hpp`
      - [x] `state.hpp`
      - [x] `table.hpp`
    - `sim/`
      - [x] `flow.hpp`
      - [ ] `latency.hpp`
      - [x] `matching.hpp`
      - [ ] `simulator.hpp`
    - `strat/`
      - [x] `driver.hpp`
      - [ ] `pnl.hpp`
      - [ ] `quoting.hpp`
      - [ ] `tabulated.hpp`
- `policy/`
  - [ ] `ethusd.bin`
  - [ ] `mdp.json`
  - [ ] `queue_reactive.json`
  - [ ] `sim_mdp.json`
  - [ ] `simcal.bin`
- `src/`
  - [x] `bitstamp.cpp`
  - [ ] `histogram.cpp`
  - [ ] `journal.cpp`
  - [ ] `line_reader.cpp`
  - [x] `mdp.cpp`
  - [x] `order_book.cpp`
  - [ ] `policy_table.cpp`
  - [ ] `recorder.cpp`
  - [ ] `tsc.cpp`
- `tests/`
  - [ ] `test_arena.cpp`
  - [ ] `test_bitstamp.cpp`
  - [ ] `test_book.cpp`
  - [ ] `test_book_differential.cpp`
  - [x] `test_calibration.cpp`
  - [ ] `test_features.cpp`
  - [ ] `test_histogram.cpp`
  - [ ] `test_journal.cpp`
  - [ ] `test_matching.cpp`
  - [ ] `test_order_map.cpp`
  - [ ] `test_pnl.cpp`
  - [ ] `test_policy.cpp`
  - [ ] `test_properties.cpp`
  - [x] `test_queue_reactive.py`
  - [ ] `test_simulator.cpp`
  - [ ] `test_split_replay.py`
  - [ ] `test_strategies.cpp`
  - [ ] `test_tsc.cpp`
  - [ ] `test_types.cpp`
  - [ ] `test_util.hpp`
- `tools/`
  - [ ] `calibrate.py`
  - [ ] `check_capture.py`
  - [ ] `figures.py`
  - [x] `fit_queue_reactive.py`
  - [ ] `jitter_baseline.sh`
  - [x] `mdp_params.py`
  - [ ] `record_bitfinex.py`
  - [x] `record_bitstamp.py`
  - [ ] `record_coinbase.py`
---

## 2. File-by-file audit

### `src/order_book.cpp`

**Status:** [x] Reviewed (408 lines, read in full)

**Issues Found:** One dead statement, one asymmetric bounds guard, one stats
accounting hole on an error path. No memory-safety or invariant defects.

- **`reduce()` line 173 is a no-op loop.**
  ```cpp
  for (auto& own : own_) if (own.id == id) break;   // our own qty_ahead is unchanged
  ```
  It iterates, matches, breaks, and does nothing. The intent was documentation.
  It compiles to nothing under `-O2`, but it reads as though it maintains state
  and does not, which is how a future edit introduces a bug.
- **`scan_down()` lacks the `w >= bits.size()` guard that `scan_up()` has.**
  `scan_up` checks it at line 46; `scan_down` computes `w = from >> 6` and
  indexes `bits[w]` immediately. Safe for every current caller because `from` is
  always a validated level index, so this is defence in depth rather than a live
  bug — but the asymmetry invites a caller that is not.
- **`replace()` loses an event from the stats on the failure path.** It does
  `--stats_.deletes` after a successful `remove`, then returns early if `add`
  fails. On that path the delete is decremented, no add was counted, and
  `++stats_.replaces` never runs, so the event is counted nowhere.
- **`queue_ahead()` returns `-1` in a `Qty`** as "not one of ours". A signed
  sentinel in a quantity type; a caller that forgets the check gets a negative
  size.

**Severity:** Minor (all four)

**Why it matters:** None of these are live defects. The dead loop and the
sentinel are traps for the next edit; the stats hole makes `replace` error rates
unmeasurable, which matters because the decoder emits `Replace` for a large
share of real feed events.

**Fix Recommendation:**
```cpp
// reduce(): delete the loop. The comment alone carries the meaning.
// A partial cancel does NOT lose priority, so our own qty_ahead is unchanged.

// scan_down(): match scan_up's guard.
std::size_t w = from >> 6;
if (w >= bits.size()) return kNoLevel;

// replace(): count the event once, on every path.
const BookError a = add(new_id, side, price, qty, mine);
++stats_.replaces;                 // the event happened either way
if (a != BookError::Ok) return a;
--stats_.adds;
```

**Refactor Suggestion:** Give `queue_ahead` an `std::optional<Qty>` return, or a
`bool queue_ahead(OrderId, Qty*)` out-parameter, so "not ours" cannot be
silently arithmetic.

**Tests Missing:** A test that `replace()` into a full pool or an out-of-window
price leaves `stats_.replaces` incremented and the book consistent. There is no
current test for a failing `replace`.

**Performance Notes:** `on_qty_removed_at()` is a linear scan of `own_` on
**every** removal, and `own_` is reserved at 64. For a market maker holding two
quotes it is two comparisons; for a strategy holding hundreds it is O(n) per
book event on the hot path. If own-order counts ever grow, key `own_` by
`(level, side)` instead. `check_invariants()` is O(window x orders) — 9,792
levels on the ethusd window — and is called per test, not per event, so it is
fine where it is used.

**Security Notes:** None. All four mutators validate id, quantity and window
before touching state, and reject before any mutation.

**Market-Logic Notes:** Correct on the points that matter. `add()` rejects
crossing **before** any mutation, so non-crossing is true by construction rather
than by later repair. `add()` joins the tail, `replace()` is deliberately a fresh
add so a requote loses priority, and `reduce()` deliberately keeps it — that
tri-state is the real exchange behaviour and it is right. `check_invariants()`
verifies FIFO rank is strictly increasing, that the bitset agrees with
emptiness, that the cached touch equals a full rescan, and that tracked
`qty_ahead` equals a from-scratch walk. That last one is the check most
order-book implementations lack.

---

### `include/lob/sim/matching.hpp`

**Status:** [x] Reviewed (154 lines, read in full)

**Issues Found:** `fills_` grows without bound for the life of the engine.

- **Unbounded memory growth.** `cross()` does `fills_.push_back(...)` per fill
  and nothing ever trims it. `clear_fills()` exists but is opt-in, and the
  callers that matter do not call it: `apps/stats/main.cpp` tracks a
  `fills_seen` index into an ever-growing vector, as do the simulator and every
  probe. A 1.5M-event synthetic run produces ~40,000 fills; a long calibration
  run is linear in that. `SubmitResult::first_fill` is an index into this
  vector, so trimming naively would invalidate outstanding results.
- **A failed `book_.execute()` is swallowed.** Line 137 breaks out of the level
  loop on error and the aggressor's remainder is silently reported as unfilled.
  An execute cannot fail here given the preceding `qty_of` check, so this is a
  should-not-happen path with no counter on it.

**Severity:** Major (growth), Minor (swallowed error)

**Why it matters:** The growth is the one finding in this instalment that can
take a process down. It is invisible in the test suite because tests are short,
and it scales with exactly the runs the calibration work depends on.

**Fix Recommendation:** Make the fill log a consumable stream rather than a
transcript. Least invasive:
```cpp
// Callers already consume by index; give them a way to drop what they consumed.
void consume_through(std::size_t n) noexcept {
  fills_.erase(fills_.begin(), fills_.begin() + static_cast<std::ptrdiff_t>(n));
  consumed_ += n;                    // so first_fill stays monotone
}
[[nodiscard]] std::size_t consumed() const noexcept { return consumed_; }
```
and index as `first_fill - consumed()`. A ring buffer is the better end state.
Add a counter for the swallowed error so it is observable:
```cpp
if (book_.execute(front, take) != BookError::Ok) { ++execute_failures_; break; }
```

**Refactor Suggestion:** Hand `cross()` a callback instead of a vector, so the
caller decides whether to store fills at all. The simulator wants them; a
calibration sweep counting fills does not.

**Tests Missing:** A soak test asserting bounded memory across a long run —
e.g. 5M events with periodic `consume_through`, checking `fills().size()` stays
under a cap. There is currently no test that exercises the fill log's lifetime.

**Performance Notes:** `cross()` calls `front_order_at`, `qty_of` and `is_mine`
per fill, each an `OrderMap` lookup; that is three hash lookups where one walk
of the level would do. Not hot at current event rates.

**Security Notes:** None.

**Market-Logic Notes:** Correct and it is the file where a backtest's honesty is
decided. It drains each level **front-first**, so queue position is worth
something — the failure mode it avoids is filling whenever the price touches
your limit, which overstates P&L badly. `Fill::price` is the resting order's
price, so the passive side sets the price, which is right. Self-match prevention
defaults to `CancelResting`, the common venue rule. `SelfMatch::Allow` exists and
does trade with yourself; it is documented as never what a venue does, but
nothing prevents a config selecting it, and a simulator that self-matches
invents P&L from nothing.

---

### `All Rights Reserved`

**Status:** [x] Reviewed (322 bytes)

**Issues Found:** The licence file is named `All Rights Reserved` — spaces, no
extension.

**Severity:** Minor

**Why it matters:** GitHub's licence detection, `pip`/`cmake` packaging metadata
and most SCA tooling look for `LICENSE`, `LICENSE.txt` or `COPYING`. As named,
the repository reads as unlicensed to every automated consumer, which is the
opposite of the file's intent. Spaces in a tracked filename also break naive
shell loops (`for f in $(git ls-files)`).

**Fix Recommendation:**
```bash
git mv "All Rights Reserved" LICENSE
```
The content is fine as it stands — a clear all-rights-reserved grant with a
named copyright holder and an educational-use carve-out.

**Security Notes:** None.

---

### `CMakeLists.txt`

**Status:** [x] Reviewed (202 lines)

**Issues Found:** None material. This is stronger than most production build
files.

- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
  -Wdouble-promotion -Wformat=2`, with `-Werror` on one configuration.
- Sanitizers: an `asan` build, and every fuzz target built with
  `-fsanitize=fuzzer,address,undefined`. `check_cxx_source_compiles` gates the
  fuzzers on libFuzzer being available rather than assuming it.
- C++20 with `CMAKE_CXX_STANDARD_REQUIRED ON`.

**Severity:** —

**Fix Recommendation:** Two additions worth making, neither urgent:
```cmake
# Catch iterator invalidation and out-of-range container access in Debug.
target_compile_definitions(lob_flags INTERFACE $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)
# A thread sanitizer build, if the simulator ever grows a thread.
```
No static analyser is wired in. `clang-tidy` with `bugprone-*`,
`cppcoreguidelines-*` and `performance-*` would be a cheap addition to CI.

**Security Notes:** No unsafe flags. Nothing disables warnings or fortification.

---

### `.gitignore`

**Status:** [x] Reviewed

**Issues Found:** None. It is correct and it explains itself.

The `data/*` plus `!data/samples/` construction is right and the file says why:
git will not descend into an excluded *directory*, so a bare `data/` would make
the re-include unreachable. Build outputs, `compile_commands.json`, `__pycache__`,
`*.pyc`, journals, histograms and the CSV working directories (`csv/`, `simcsv/`,
`simpolicy/`) are all ignored. `docs/figures/` is deliberately re-included.

**Verified:** `git ls-files --others --exclude-standard` returns nothing, so
there is no untracked file the ignore rules are hiding by accident.

**See section 5** for the two additions recommended.

---

### Files audited during this session's development work

These carry findings already recorded in `docs/KNOWN-ISSUES.md` and
`docs/06-queue-reactive-plan.md`, found by measurement rather than by reading,
and each is fixed or explicitly open there. Listing them so the tree is honest
about *why* they are marked reviewed.

| File | Status | Principal finding |
|---|---|---|
| `include/lob/sim/flow.hpp` | [x] | Book 200x too thick and clock 7,400x too fast (fixed); trade sizes constant (fixed); orders stranded outside the modelled window never cancelled, book grew 101→408 unbounded (fixed) |
| `src/mdp.cpp` | [x] | Move-fill charged the full move with certainty against a measured 33% (fixed) |
| `include/lob/policy/state.hpp` | [x] | Queue bucketing by quartile rank, wrong by 20x (fixed) |
| `include/lob/strat/driver.hpp` | [x] | Message budget counted in events, not time (fixed) |
| `apps/solve/main.cpp` | [x] | Inventory penalty a flat constant, 22x the per-epoch edge at a 100 ms grid (fixed) |
| `include/lob/feat/queue_reactive.hpp` | [x] | Queue axis binned log2, collapsing the measured range into six buckets (fixed) |
| `apps/stats/main.cpp` | [x] | Per-side exposure summed over both sides then halved, doubling every fitted rate (fixed) |
| `apps/evaluate/main.cpp`, `apps/backtest/main.cpp` | [x] | Ran an uncalibrated process; now on the fitted one, and the acceptance test is noise-dominated (open) |
| `include/lob/policy/table.hpp`, `include/lob/policy/mdp.hpp` | [x] | Schema versioning correct; no findings |
| `tools/record_bitstamp.py` | [x] | No reconnect handling (fixed); `requests.get(...).json()` unvalidated (open, below) |
| `tools/mdp_params.py`, `tools/fit_queue_reactive.py` | [x] | Level ratio measured from placement not prints, 200x wrong (fixed) |
| `tests/test_calibration.cpp`, `tests/test_queue_reactive.py` | [x] | Written this session as regression guards |
| `include/lob/feed/bitstamp.hpp`, `src/bitstamp.cpp` | [x] | Seed guard is load-bearing and correct; book holes over multi-hour captures (open, KNOWN-ISSUES 1) |

**Open finding, `tools/record_bitstamp.py` line 207:**
```python
snap = requests.get(REST_BOOK.format(pair=self.pair), timeout=30).json()
```
A non-JSON error page raises inside the recording loop. There is a timeout and
no `verify=False`, so TLS is intact, but the response is neither status-checked
nor schema-checked before use.
```python
r = requests.get(REST_BOOK.format(pair=self.pair), timeout=30)
r.raise_for_status()
snap = r.json()
if not isinstance(snap.get("bids"), list) or not snap.get("asks"):
    raise RuntimeError(f"unexpected snapshot shape: {sorted(snap)[:6]}")
```

---

## 3. Consolidated summary

### Critical
None found in the 23 files audited.

### High
| # | Item | File | Owner |
|---|---|---|---|
| H1 | `fills_` grows without bound; scales with run length | `include/lob/sim/matching.hpp` | simulator |

### Medium
| # | Item | File | Owner |
|---|---|---|---|
| M1 | REST snapshot used without status or schema check | `tools/record_bitstamp.py` | data |
| M2 | Acceptance test noise-dominated: CI ±2,700 on a mean of −1,136 | `apps/evaluate/main.cpp` | research |
| M3 | Two baselines send 150k aggressive fills in 250k events | `include/lob/strat/quoting.hpp` | research |
| M4 | ethusd book develops holes over multi-hour captures | `src/bitstamp.cpp` | data |

### Low
| # | Item | File | Owner |
|---|---|---|---|
| L1 | Dead no-op loop in `reduce()` | `src/order_book.cpp` | book |
| L2 | `scan_down` missing the bounds guard `scan_up` has | `src/order_book.cpp` | book |
| L3 | `replace()` counts no event on the add-failure path | `src/order_book.cpp` | book |
| L4 | `queue_ahead()` returns −1 sentinel in a `Qty` | `src/order_book.cpp` | book |
| L5 | Failed `execute()` inside `cross()` is silent | `include/lob/sim/matching.hpp` | simulator |
| L6 | Licence file named `All Rights Reserved` | repo root | owner |
| L7 | No static analyser in CI | `CMakeLists.txt` / `ci.yml` | build |

---

## 4. Prioritized fix roadmap

### Immediate (this week, ~6 h)
- **PR: "Bound the matching engine's fill log"** — 3 h. `consume_through()` plus
  an offset so `first_fill` stays monotone; update the four call sites that index
  into `fills()`; add a soak test. Closes H1.
- **PR: "Order book: drop dead code, match the bounds guards, count replace once"**
  — 1 h. Closes L1–L4. Add the failing-`replace` test.
- **PR: "Rename the licence file"** — 5 min. `git mv "All Rights Reserved" LICENSE`.
  Closes L6.
- **PR: "Validate the REST snapshot before use"** — 1 h. Closes M1.

### Next two weeks (~20 h)
- **PR: "Periodic re-snapshot during recording"** — 8 h. Fire the existing
  session-snapshot machinery on a timer, reseed at the marker with the guard
  relative to the current touch. Needs a fresh 8-hour capture to validate.
  Closes M4.
- **PR: "Give the acceptance test power back"** — 6 h. Either raise the seed
  count until the CI excludes zero, or compare on an inventory-neutral statistic
  so the closing mark stops dominating. Closes M2.
- **PR: "Fix the two crossing baselines"** — 4 h. Their quotes cross a one-tick
  book; clamp to the touch. Closes M3.
- **PR: "clang-tidy in CI"** — 2 h. Closes L7.

### Next month
- Complete this audit: 93 files remain, roughly 15,000 lines. Estimated 25–30 h
  at the depth of the sections above.
- The price impact of a trade, which blocks η, adverse selection and the lift
  together (`docs/06`).

---

## 5. Safety checklist report

### Secrets

```
$ grep -RIn --exclude-dir=.git --exclude-dir=build \
    -E "API_KEY|SECRET|TOKEN|PASSWORD|passwd|aws_access_key|PRIVATE_KEY" .
(no matches)
```

**No secrets, tokens or credentials anywhere in the tree.** This is by design and
the design is stated in `docs/05-roadmap.md`: sending real orders is permanently
out of scope, so there is no order gateway, no API key, no paper-trading account
that a flag could flip to live. Every venue tool reads only public endpoints —
`requests.get` on a public REST book and a public WebSocket — with no
authentication path in the codebase. **This is the single strongest safety
property the repository has and it should be treated as an invariant**, not a
current state: a future PR adding an authenticated endpoint changes the risk
class of the whole project.

### `.gitignore` — recommended patch

The existing file is correct. Two additions, both defensive:

```diff
--- a/.gitignore
+++ b/.gitignore
@@
 csv/
 simcsv/
 simpolicy/
+out/
+*.bin.tmp
+
+# Credentials must never enter this repository. There is no code path that
+# reads them and there is not meant to be one; this is here so that an
+# accidental `.env` cannot be committed while someone is experimenting.
+.env
+.env.*
+!.env.example
+*.pem
+*.key
+credentials.json
 
 __pycache__/
 *.pyc
```

### `.env` handling

There is **no `.env` file and no code that reads environment configuration for
credentials**, which is correct for this project. The recommendation is not to
introduce one. Ship an `.env.example` only as a statement of what is *not*
needed:

```dotenv
# This project reads no credentials and sends no orders. Nothing here is
# required to build, test, record or backtest.
#
# Public endpoints only, and both already default correctly in the tools:
# BITSTAMP_WS=wss://ws.bitstamp.net
# BITSTAMP_REST=https://www.bitstamp.net/api/v2/order_book
#
# If a key ever appears in this file, the project has changed category.
# See docs/05-roadmap.md, "Permanently out of scope".
```

### Binary and large-file handling

| Path | Size | Verdict |
|---|---|---|
| `data/samples/*.jsonl.gz` (3 files) | 5.6 MB | **Keep tracked.** The real-data regression tests replay them; synthetic data only proves the decoder agrees with the generator that wrote it. |
| `data/samples/*_snapshot.json` (3) | 697 KB | **Keep tracked.** Required to seed those replays. |
| `policy/*.bin` (2) | 254 KB | **Keep tracked, with a caveat.** They are solver output and regenerable, but they are the artefact the executor loads and the schema version in the header exists to catch a mismatch. Re-solved on most changes, so they churn — see below. |
| `docs/figures/*.png` (7) | 691 KB | **Keep tracked.** Regenerated by `tools/figures.py`; the docs reference them. |

**Verified no secrets in the binaries:** `grep -RIn` above covers them (`-I`
skips binary content, so it was re-checked with `strings`-equivalent scanning of
the `.bin` headers — they contain a schema version, a parameter hash and a float
array, no text).

Total `.git` is 14 MB, so **git LFS is not warranted yet**. The one to watch is
`policy/*.bin`: 127 KB each, rewritten on every re-solve. If solves become
frequent, either move them to LFS or stop tracking them and regenerate in CI:

```diff
+# policy/*.bin  — uncomment if re-solves start dominating repository growth.
+#                 Regenerate with: solve --pair simqr --params policy/sim_mdp.json
```

### CMake and CI

Already strong; see the `CMakeLists.txt` section. Recommended additions:

```cmake
target_compile_definitions(lob_flags INTERFACE $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)
```

```yaml
# .github/workflows/ci.yml
- name: clang-tidy
  run: |
    cmake -B build-tidy -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    run-clang-tidy -p build-tidy -checks='bugprone-*,cppcoreguidelines-*,performance-*'
```

### Trading safety

- **No order gateway, no venue credentials, no submission path.** Confirmed by
  the secrets scan and by reading every network call in `tools/*.py`: three
  `requests.get` and three WebSocket subscribes, all public, all read-only.
- **Position limits are enforced as a constraint on the action**, not as a clamp
  on the outcome — `admissible()` in `src/mdp.cpp` forbids quoting a bid at
  maximum long inventory rather than clamping the resulting position. The
  comment records why: clamping let a fill at the boundary collect its edge for
  free and the first solve exploited exactly that.
- **Runaway optimisation:** `solve()` is bounded by a sweep cap and a divergence
  bail-out at `kDivergent = 1e12`, added after value iteration diverged to
  2.05e+57 on a non-stochastic transition function.
- **No unbounded risk path exists** because no order can be sent.

---

## 6. Final confirmation

**Not all files have been audited.** 23 of 116 are complete; 93 remain, marked
`[ ]` in the tree above. Claiming otherwise would make the checkbox tree — the
one artefact of this document meant to be trusted at a glance — worthless.

The acceptance criterion "every file marked `[x]`" is not met and should not be
recorded as met until the remaining 93 have had the same treatment. Estimated
25–30 hours at this depth.
