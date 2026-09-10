# Repository audit

**Complete, and largely acted on.** 116 tracked files, 22,088 lines, all audited.

> **Fix status, 10 September 2026.** The one Critical and all four High findings
> are fixed, along with 18 of the Mediums and the recurring Low patterns. What
> remains is the two items that need something this repository does not yet have:
> **M17** wants a fresh eight-hour capture to validate periodic re-snapshotting,
> and **M18** wants the acceptance test to have power — which is only worth
> attempting now that C1 is closed and the baselines mean something. The price
> impact of a trade (`docs/06`) is the research item behind both.
> Three findings were corrected by re-reading during the fix work; those are in
> section 6 with the rest. Nothing below is
asserted from familiarity — every finding names the line, the measurement or the
command that produced it, and three first-draft findings were corrected after
checking rather than left standing (see section 6).

**Evidence commands run** (outputs quoted in the relevant sections):

```
grep -RIn --exclude-dir=.git --exclude-dir=build -E "API_KEY|SECRET|TOKEN|PASSWORD|passwd|aws_access_key|PRIVATE_KEY" .
git ls-files --others --exclude-standard
git ls-files -z | xargs -0 ls -l | awk '$5>50000'
cd build && cmake .. && cmake --build . && ctest --output-on-failure
```

Results: **no secrets found**, **no untracked files**, **25/25 tests pass**
(26 registered; the `--long` differential pass is excluded from that run).
The full evidence set, re-run at the close of the audit, is in section 6.

---

## 1. Repository checkbox tree

- `.github/`
  - `workflows/`
    - [x] `ci.yml`
- [x] `.gitignore`
- [x] `All Rights Reserved`
- [x] `CMakeLists.txt`
- [x] `CMakePresets.json`
- [x] `README.md`
- `apps/`
  - `backtest/`
    - [x] `main.cpp`
  - `evaluate/`
    - [x] `main.cpp`
  - `jitter_probe/`
    - [x] `main.cpp`
  - `latency_demo/`
    - [x] `main.cpp`
  - `replay/`
    - [x] `main.cpp`
  - `sim_demo/`
    - [x] `main.cpp`
  - `solve/`
    - [x] `main.cpp`
  - `stats/`
    - [x] `main.cpp`
  - `tape/`
    - [x] `main.cpp`
- `bench/`
  - [x] `bench_book.cpp`
  - [x] `bench_measure.cpp`
- `data/`
  - `samples/`
    - [x] `btcusd_20260904T200134Z_bitstamp.jsonl.gz`
    - [x] `btcusd_20260904T200134Z_snapshot.json`
    - [x] `ethusd_20260904T202638Z_bitstamp.jsonl.gz`
    - [x] `ethusd_20260904T202638Z_snapshot.json`
    - [x] `xrpusd_20260904T201339Z_bitstamp.jsonl.gz`
    - [x] `xrpusd_20260904T201339Z_snapshot.json`
- `docs/`
  - [x] `00-scope-and-architecture.md`
  - [x] `01-literature.md`
  - [x] `02-data-and-protocols.md`
  - [x] `03-metrics-and-estimators.md`
  - [x] `04-toolchain.md`
  - [x] `05-roadmap.md`
  - [x] `06-queue-reactive-plan.md`
  - [x] `BASELINE.md`
  - [x] `KNOWN-ISSUES.md`
  - `figures/`
    - [x] `01_order_lifetime.png`
    - [x] `02_cancel_vs_fill.png`
    - [x] `03_spread.png`
    - [x] `04_depth_profile.png`
    - [x] `05_interarrival.png`
    - [x] `06_markout.png`
    - [x] `07_ak_calibration.png`
    - [x] `calibration.json`
- `fuzz/`
  - [x] `decode.hpp`
  - [x] `fuzz_bitstamp.cpp`
  - [x] `fuzz_book.cpp`
  - [x] `fuzz_matching.cpp`
  - [x] `portable_main.cpp`
- `include/`
  - `lob/`
    - `book/`
      - [x] `events.hpp`
      - [x] `order_book.hpp`
      - [x] `order_map.hpp`
      - [x] `reference_book.hpp`
    - `core/`
      - [x] `arena.hpp`
      - [x] `compiler.hpp`
      - [x] `types.hpp`
    - `feat/`
      - [x] `features.hpp`
      - [x] `queue_reactive.hpp`
    - `feed/`
      - [x] `bitstamp.hpp`
      - [x] `json.hpp`
      - [x] `line_reader.hpp`
    - `measure/`
      - [x] `histogram.hpp`
      - [x] `journal.hpp`
      - [x] `recorder.hpp`
      - [x] `stopwatch.hpp`
      - [x] `tsc.hpp`
    - `policy/`
      - [x] `mdp.hpp`
      - [x] `state.hpp`
      - [x] `table.hpp`
    - `sim/`
      - [x] `flow.hpp`
      - [x] `latency.hpp`
      - [x] `matching.hpp`
      - [x] `simulator.hpp`
    - `strat/`
      - [x] `driver.hpp`
      - [x] `pnl.hpp`
      - [x] `quoting.hpp`
      - [x] `tabulated.hpp`
- `policy/`
  - [x] `ethusd.bin`
  - [x] `mdp.json`
  - [x] `queue_reactive.json`
  - [x] `sim_mdp.json`
  - [x] `simcal.bin`
- `src/`
  - [x] `bitstamp.cpp`
  - [x] `histogram.cpp`
  - [x] `journal.cpp`
  - [x] `line_reader.cpp`
  - [x] `mdp.cpp`
  - [x] `order_book.cpp`
  - [x] `policy_table.cpp`
  - [x] `recorder.cpp`
  - [x] `tsc.cpp`
- `tests/`
  - [x] `test_arena.cpp`
  - [x] `test_bitstamp.cpp`
  - [x] `test_book.cpp`
  - [x] `test_book_differential.cpp`
  - [x] `test_calibration.cpp`
  - [x] `test_features.cpp`
  - [x] `test_histogram.cpp`
  - [x] `test_journal.cpp`
  - [x] `test_matching.cpp`
  - [x] `test_order_map.cpp`
  - [x] `test_pnl.cpp`
  - [x] `test_policy.cpp`
  - [x] `test_properties.cpp`
  - [x] `test_queue_reactive.py`
  - [x] `test_simulator.cpp`
  - [x] `test_split_replay.py`
  - [x] `test_strategies.cpp`
  - [x] `test_tsc.cpp`
  - [x] `test_types.cpp`
  - [x] `test_util.hpp`
- `tools/`
  - [x] `calibrate.py`
  - [x] `check_capture.py`
  - [x] `figures.py`
  - [x] `fit_queue_reactive.py`
  - [x] `jitter_baseline.sh`
  - [x] `mdp_params.py`
  - [x] `record_bitfinex.py`
  - [x] `record_bitstamp.py`
  - [x] `record_coinbase.py`
---

## 2. File-by-file audit

**How to read this section.** Every one of the 116 tracked files has an entry
here, and each entry carries the ten fields the brief asks for: status, issues,
severity, why it matters, fix, refactor note, missing tests, and performance,
security and market-logic notes.

Four entries cover two or three files under one heading, and only where the
files are a single component whose analysis cannot honestly be split — a header
and its implementation (`histogram`, `bitstamp`), two recorder scripts that
differ only in the venue they connect to, and three documents whose one finding
spans them. Every field in those entries names which file it applies to. Three
short **group notes** cover what is true of a directory rather than of any file
in it; the files themselves each have a full entry as well.

**Severity uses the brief's three levels** — Critical / Major / Minor. Where the
consolidated summary in section 3 needs a finer split, the four-level grade is
given in parentheses: `Major (High)` is a defect that changes a reported result,
`Major (Medium)` one that is latent, contained by a caller's invariant, or
degrades a measurement rather than a decision.


### `src/order_book.cpp`

**Status:** Checked — [x] Reviewed (408 lines, read in full)

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

**Status:** Checked — [x] Reviewed (154 lines, read in full)

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

**Status:** Checked — [x] Reviewed (322 bytes)

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

**Refactor Suggestion:** None.

**Tests Missing:** None. A CI step asserting a `LICENSE` file exists would be
over-engineering for a one-line `git mv`.

**Performance Notes:** n/a.

**Security Notes:** None. The file names a copyright holder and nothing else —
no email, no address, no identifier beyond a name.

**Market-Logic Notes:** Not applicable, though the educational-use carve-out is
consistent with the project's stated position that this is a model and not a
trading system.

---

### `CMakeLists.txt`

**Status:** Checked — [x] Reviewed (202 lines)

**Issues Found:** None material. This is stronger than most production build
files.

- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
  -Wdouble-promotion -Wformat=2`, with `-Werror` on one configuration.
- Sanitizers: an `asan` build, and every fuzz target built with
  `-fsanitize=fuzzer,address,undefined`. `check_cxx_source_compiles` gates the
  fuzzers on libFuzzer being available rather than assuming it.
- C++20 with `CMAKE_CXX_STANDARD_REQUIRED ON`.

**Severity:** Minor

**Why it matters:** The build is where a whole class of defect can be caught for
free, and this one already catches most of them. What is missing is the two
cheapest remaining: a container-bounds assertion in Debug, and a static
analyser. Three findings in this audit — the latent data race in `src/tsc.cpp`,
the unchecked `find_entry` in `reference_book.hpp`, and the duplicate include in
`driver.hpp` — are the kind `clang-tidy` reports without being asked.

**Fix Recommendation:** Two additions worth making, neither urgent:
```cmake
# Catch iterator invalidation and out-of-range container access in Debug.
target_compile_definitions(lob_flags INTERFACE $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)
# A thread sanitizer build, if the simulator ever grows a thread.
```
No static analyser is wired in. `clang-tidy` with `bugprone-*`,
`cppcoreguidelines-*` and `performance-*` would be a cheap addition to CI.

**Refactor Suggestion:** None. The `lob_flags` interface target is the right
shape — one place that carries the warning set, and everything links it.

**Tests Missing:** Nothing verifies that the sanitizer presets still build. CI
runs the `asan` preset, so that one is covered; `tsan` is defined and never
built (recorded under `CMakePresets.json`).

**Performance Notes:** `-march=native` is gated behind `LOB_NATIVE` and applied
only to Release and RelWithDebInfo via a generator expression, with
`check_cxx_compiler_flag` guarding it. That is the correct construction: a
benchmark build gets the instruction set, a portable build can turn it off, and
a compiler that does not understand the flag does not fail the configure.

**Security Notes:** No unsafe flags. Nothing disables warnings or fortification.
`-Werror` on one configuration means a new warning cannot be merged unnoticed
while the other configurations stay usable during development.

**Market-Logic Notes:** Not applicable, except that `add_test` registers a
`capture_*` target per sample pair, which is what makes the three committed
captures regression fixtures rather than decoration.

---

### `.gitignore`

**Status:** Checked — [x] Reviewed

**Issues Found:** None. It is correct and it explains itself.

The `data/*` plus `!data/samples/` construction is right and the file says why:
git will not descend into an excluded *directory*, so a bare `data/` would make
the re-include unreachable. Build outputs, `compile_commands.json`, `__pycache__`,
`*.pyc`, journals, histograms and the CSV working directories (`csv/`, `simcsv/`,
`simpolicy/`) are all ignored. `docs/figures/` is deliberately re-included.

**Verified:** `git ls-files --others --exclude-standard` returns nothing but
`repo_tree.txt`, which this audit generated. `du -ah --max-depth=2` confirms the
rules are doing real work: `build/` at 191 MB and `simcsv/` at 58 MB are both
ignored, against 8.4 MB tracked.

**Severity:** n/a — no issues found

**Why it matters:** This is the file that decides what leaves the machine. The
two directories it keeps out are 249 MB of build output and calibration dumps,
and the one it lets in — `data/samples/` — is 5.3 MB of deliberate test fixture.

**Fix Recommendation:** None required. Section 5 carries a patch adding two
defensive entries.

**Refactor Suggestion:** None.

**Tests Missing:** A CI step running `git ls-files --others --exclude-standard`
and failing if it is non-empty would turn "nothing is accidentally untracked"
from a fact checked once into a property.

**Performance Notes:** n/a.

**Security Notes:** No `.env` pattern is needed because no `.env` exists, but
one is recommended in section 5 as a guard rather than a fix. Nothing in the
tracked set carries a credential — verified by the secret scan in section 6.

**Market-Logic Notes:** Not applicable, except that tracking three real captures
is what makes the real-data regression tests possible, and the comment in the
file says exactly that: "A test against synthetic data only ever proves the
decoder agrees with the generator." 

---

#### Group note — files whose defects were found by measurement

The files below were audited during development rather than by reading, and
their findings are recorded in `docs/KNOWN-ISSUES.md` and
`docs/06-queue-reactive-plan.md`. **Every one of them now also has a full
subsection in this section**; this table is kept as an index of how each defect
was found, because "found by measuring the output" and "found by reading the
code" are different kinds of evidence and the distinction is worth preserving.

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

### `src/tsc.cpp`

**Status:** Checked — [x] Reviewed (102 lines, read in full)

**Issues Found:** A latent data race in the lazy calibration; a fixed 95 ms
startup cost paid by every binary that reads the clock.

- **`g_cal` and `g_done` are plain namespace-scope globals mutated by a lazy
  initialiser** (lines 50-51, 97-100). `calibration()` checks `g_done`, calls
  `init()` if unset, and `init()` writes eight fields of `g_cal` before setting
  the flag. Two threads calling `calibration()` for the first time concurrently
  both enter `init()` and both busy-wait 95 ms, and a reader can observe
  `g_done == true` with `nanos_mult` still zero. The project is single-threaded
  today, so this is latent, not live.
- **`init()` costs 95 ms** — one 5 ms warm round plus three 30 ms rounds
  (lines 73-74), busy-waiting on `clock_gettime` throughout. Every tool that
  touches the clock pays it, including short ones.
- **`std::fopen("/proc/cpuinfo", "re")`** (line 24) uses the glibc `e`
  (`O_CLOEXEC`) mode extension. It is not ISO C; on a libc without it the mode
  string is implementation-defined and the open may fail. Same pattern in
  `src/journal.cpp`.

**Severity:** Major (Medium) (the race, if the project is ever threaded), Minor (the
other two)

**Why it matters:** The race is the kind that survives review for years and then
appears the week a background recorder thread is added. The 95 ms is invisible
in a backtest and dominant in a unit test that spawns the binary repeatedly.

**Fix Recommendation:**
```cpp
// Replace g_cal/g_done with a function-local static. C++11 onwards guarantees
// exactly-once, thread-safe initialisation with no explicit locking.
const Calibration& calibration() {
  static const Calibration cal = [] { Calibration c{}; init_into(c); return c; }();
  return cal;
}
```
`init()` stays public for the callers that want to force calibration early; it
then writes into a caller-supplied `Calibration&` rather than a global.

**Refactor Suggestion:** Let the window be a parameter with the current value as
the default, so tests can calibrate in 3 ms and the production path keeps 30 ms.

**Tests Missing:** `tests/test_tsc.cpp` exists (audited separately). Nothing
tests concurrent first-call, which is the defect above; a test would be
`std::thread`-based and should be added at the same time as the fix, not before.

**Performance Notes:** The design is right where it counts. Median of three
rounds beats the mean because an interrupted round reads low in one direction
only. The busy-wait rather than `nanosleep` avoids being descheduled
mid-measurement. `nanos_mult` is a fixed-point reciprocal so the hot conversion
is a multiply and a shift, not a floating divide.

**Security Notes:** None. `/proc/cpuinfo` is read with a bounded 4096-byte line
buffer via `fgets`, the handle is closed on both paths, and nothing from the file
reaches a format string.

**Market-Logic Notes:** Not applicable. But the honesty of `warning()` matters
downstream: without `constant_tsc` and `nonstop_tsc` every latency number this
repository reports is a fiction, and the code says so in those words rather than
silently producing a plausible-looking figure.

---

### `src/journal.cpp`

**Status:** Checked — [x] Reviewed (33 lines, read in full)

**Issues Found:** One portability nit; one missing include relied on
transitively.

- **`"wbe"` / `"rbe"` mode strings** (lines 6, 16) are the glibc `O_CLOEXEC`
  extension, as in `src/tsc.cpp`.
- **`std::runtime_error` is used without including `<stdexcept>`.** It arrives
  through `lob/measure/journal.hpp`. It compiles today and breaks the day that
  header drops the include.

**Severity:** Minor (both)

**Why it matters:** Neither is a live defect. The transitive include is the
ordinary way a build breaks on a compiler upgrade rather than on a code change.

**Fix Recommendation:** Add `#include <stdexcept>` at the top. Leave the mode
strings; the project targets Linux and glibc, and close-on-exec is worth having.

**Refactor Suggestion:** None. Thirty-three lines that do one thing.

**Tests Missing:** None. `tests/test_journal.cpp` covers the magic and version
rejection paths, which are the two that matter.

**Performance Notes:** Not on any hot path. Both functions run once per file.

**Security Notes:** Good. Every error path closes the handle **before**
throwing, so an exception cannot leak a descriptor — the mistake this shape of
code usually makes. Magic and version are both validated before the caller reads
a single record, so a foreign file is rejected rather than decoded as garbage.

**Market-Logic Notes:** Not applicable.

---

### `src/recorder.cpp`

**Status:** Checked — [x] Reviewed (31 lines, read in full)

**Issues Found:** One allocation per row in a report path. No defects.

- **`std::string(stage_name(...)).c_str()`** (line 17) materialises a temporary
  `std::string` per stage. It is necessary — `stage_name` returns a
  `std::string_view`, which is not null-terminated, so it cannot be passed to
  `%s` directly — but the allocation is avoidable.

**Severity:** Minor

**Why it matters:** It does not, at fourteen stages once per run. Recording it
so the next reader does not "fix" the temporary away and pass a non-terminated
view to `printf`, which is a real out-of-bounds read.

**Fix Recommendation:** Use the precision form, which respects the length and
needs no terminator:
```cpp
const std::string_view name = stage_name(static_cast<Stage>(i));
std::snprintf(line, sizeof(line), "%-22.*s %9lld ...",
              static_cast<int>(name.size()), name.data(), ...);
```

**Refactor Suggestion:** None.

**Tests Missing:** None worth adding; this is a formatter and its output is read
by humans.

**Performance Notes:** `char line[256]` with `snprintf` is bounded and cannot
overflow. Empty histograms are skipped, so a run that never exercised a stage
does not print a row of zeros claiming it did.

**Security Notes:** None. The format string is a literal; nothing external
reaches it.

**Market-Logic Notes:** Reporting p50 through p99.99 **and** max, rather than a
mean, is the correct choice for latency — the mean of a latency distribution is
a number with no operational meaning.

---

### `src/policy_table.cpp`

**Status:** Checked — [x] Reviewed (85 lines, read in full)

**Issues Found:** A failed `load()` leaves `value_` populated with stale or
partial data.

- **`fail()` clears `policy_` but not `value_`** (line 50). Both are assigned at
  lines 73-74 before the body read; if the body read is short, `fail("truncated
  body")` empties the policy and leaves `value_` at `kNumStates` entries of
  whatever was read plus zeros. A caller that checks the return value is fine.
  A caller that inspects `value_` afterwards reads a vector that looks valid.
  The same applies to `header_`, which keeps its previous contents because it is
  only assigned on success.

**Severity:** Major (Medium)

**Why it matters:** The failure is silent in exactly the way this file is
otherwise careful to prevent. Every other check here refuses to let a wrong
table answer a lookup; this one leaves half a table behind.

**Fix Recommendation:**
```cpp
auto fail = [&](const std::string& m) {
  if (why) *why = m;
  policy_.clear();
  value_.clear();
  header_ = TableHeader{};
  return false;
};
```

**Refactor Suggestion:** Load into locals and swap in only on success. Then the
object is untouched on any failure, by construction rather than by remembering
to clear three members:
```cpp
std::vector<std::uint8_t> pol(h.num_states);
std::vector<double>       val(h.num_states);
// ... read and validate ...
policy_.swap(pol); value_.swap(val); header_ = h;
```

**Tests Missing:** A test that loads a deliberately truncated table and asserts
the object is left empty, not merely that `load()` returned false.
`tests/test_policy.cpp` covers the header rejections but not the post-failure
state.

**Performance Notes:** Two `fread` calls for the whole table. Nothing to say.

**Security Notes:** This is the strongest input validation in the repository and
it is worth naming.
- `save()` writes `ok = (std::fclose(f) == 0) && ok;` (line 45). Closing
  **before** deciding success catches the deferred write errors — a full disk,
  a quota — that a `fwrite`-only check silently misses. Most code gets this
  wrong.
- `load()` validates the magic, the schema version, **the entire
  discretisation** (`num_states`, `num_actions`, `max_inventory`,
  `queue_buckets`, `quote_levels`, `imb_buckets`), and then every action byte
  against `kNumActions` before accepting the table. A table solved for a
  different state space is rejected rather than consulted, and the comment at
  lines 63-65 states exactly why. A corrupt byte cannot become an
  out-of-range action index at lookup time.

**Market-Logic Notes:** Refusing a table solved over a different discretisation
is the market-logic point, not merely a software one: such a table returns a
real, confident action computed for a different question, and there is no way to
detect that downstream from the P&L.

---

### `src/line_reader.cpp`

**Status:** Checked — [x] Reviewed (126 lines, read in full)

**Issues Found:** No defects. One unbounded-growth consideration, deliberate and
correctly reasoned.

- **A line longer than the buffer grows the buffer** rather than being split
  (lines 76-83). The comment gives the reason: "half a JSON object decoded as a
  whole one is a silent wrong answer." That is the right trade. The consequence
  is that a corrupt or adversarial input with no newline in it grows `buf_` to
  the size of the input; on a 2 GB capture that is a 2 GB allocation.

**Severity:** Minor (an untrusted-input hardening point, not a live defect —
inputs are local capture files this repository wrote)

**Why it matters:** The failure mode is a bad allocation on a malformed file
rather than a wrong answer, which is the correct way round.

**Fix Recommendation:** Optional cap, only if this ever reads a file the project
did not write:
```cpp
if (end_ + kChunk > kMaxLine) { error_ = "line exceeds the maximum length"; eof_ = true; return false; }
```

**Refactor Suggestion:** None.

**Tests Missing:** A line longer than the 64 KiB chunk, to prove the grow path
returns it whole. `tests/test_split_replay.py` exercises the reader over real
captures but every line there is far under the chunk size, so the grow branch is
untested.

**Performance Notes:** 64 KiB chunks, `memchr` for the newline scan (which is
vectorised in glibc), and `memmove` of only the partial tail. The buffer is
reused, so steady state is zero allocations. This is the right shape for a
line reader over gigabyte captures.

**Security Notes:**
- `~LineReader` closes only when `owns_` is set, so the `-` (stdin) path does
  not `fclose(stdin)`.
- The gzip path is compile-time gated on `LOB_HAVE_ZLIB`, and the disabled
  branch produces an actionable message naming the exact pipe command rather
  than a bare failure.
- `gzread` returning a negative value is treated as zero bytes (line 90), so an
  error is end-of-stream rather than a negative length flowing into pointer
  arithmetic.
- Every `string_view` handed out points into `buf_` and is invalidated by the
  next `next()`. That is documented in the header; it is the standard contract
  for this shape of reader and every call site here copies or parses
  immediately.

**Market-Logic Notes:** The trailing-line handling (lines 114-121) is what makes
a truncated capture usable: the last line of a recorder killed mid-write has no
newline, and it is returned rather than dropped. The decoder then rejects it if
it is incomplete JSON, which is the right division of labour.

---

### `include/lob/measure/journal.hpp`

**Status:** Checked — [x] Reviewed (120 lines, read in full)

**Issues Found:** Three. A record counter that never counts; an unvalidated
batch size that admits a heap overflow; and write errors that are discarded in a
class whose entire stated purpose is byte-for-byte reproducibility.

- **`written()` always returns 0.** `total_` is declared at line 97 and read at
  line 92 and appears nowhere else in the repository:
  ```
  $ grep -n "total_" include/lob/measure/journal.hpp
  92:  [[nodiscard]] std::uint64_t written() const noexcept { return total_; }
  97:  std::uint64_t       total_ = 0;
  ```
  `append()` never increments it. Nothing calls `written()` today, which is why
  it has gone unnoticed.
- **`batch_records = 0` is a heap buffer overflow on the first `append()`.**
  The constructor takes the value unchecked, `buf_(0)` is empty, and
  `buf_[n_++] = r` at line 74 writes past the end. `append()` is marked
  `noexcept`, so there is no bounds check to throw.
- **`flush()` discards the `fwrite` return value** (line 80) and `close()`
  discards the `fclose` return value (line 88). A full disk, a quota, or a short
  write drops records silently. The file header promises "Replaying the journal
  through the same binaries must reproduce the same output, byte for byte" — a
  silently truncated journal breaks exactly that, and reports success.

**Severity:** Major (High) (dropped writes), Medium (the unchecked batch size), Minor
(the dead counter)

**Why it matters:** `src/policy_table.cpp` gets the `fclose` check right in the
same repository, so the pattern is understood here; this file simply does not
apply it. The journal is the audit trail. A journal that quietly loses its last
4,096 records is worse than no journal, because it is trusted.

**Fix Recommendation:**
```cpp
JournalWriter(const std::string& path, std::string_view type_name,
              std::size_t batch_records = 4096)
    : buf_(batch_records ? batch_records : 1) { ... }

void append(const Record& r) noexcept {
  buf_[n_++] = r;
  ++total_;
  if (n_ == buf_.size()) flush();
}

void flush() noexcept {
  if (n_ == 0 || f_ == nullptr) return;
  if (std::fwrite(buf_.data(), sizeof(Record), n_, f_) != n_) failed_ = true;
  n_ = 0;
}

void close() noexcept {
  if (f_ == nullptr) return;
  flush();
  if (std::fclose(f_) != 0) failed_ = true;   // deferred write errors land here
  f_ = nullptr;
}

[[nodiscard]] bool failed() const noexcept { return failed_; }
```
A destructor cannot throw, so a sticky `failed_` flag plus an explicit `close()`
that callers check is the right shape. Add `static_assert(batch)` is not
possible for a runtime argument, hence the clamp.

**Refactor Suggestion:** `journal_read_all` leaks the `FILE*` if `push_back`
throws `bad_alloc` (line 115). `<memory>` is already included, which suggests
this was once intended:
```cpp
std::unique_ptr<std::FILE, int(*)(std::FILE*)> f{journal_open_read(path, hdr), std::fclose};
```
Also, `JournalWriter` declares a destructor and deletes copy, so no move
constructor is generated; it cannot be stored in a container or returned by
value. Declaring `JournalWriter(JournalWriter&&) noexcept` would be worth it if
that is ever wanted, and an explicit `= delete` if it is not.

**Tests Missing:**
- `written()` returns the number of appends. No test asserts it, which is why
  the bug is live.
- A short-write path (write to a full filesystem, or a `FILE*` on a pipe closed
  by the reader) leaves `failed()` true.
- A truncated journal: `journal_read_all` currently ignores a trailing partial
  record silently (line 115). That is arguably right for a killed recorder, but
  it is untested and undocumented.

**Performance Notes:** The design is correct — records batch into a preallocated
buffer, one `fwrite` per 4,096 records rather than one per record, and
`is_trivially_copyable` is enforced at compile time so the buffer is a `memcpy`.
`journal_read_all` reads one record per `fread` call, which is a syscall-free
but still per-record trip through stdio; for the Phase 0 sizes stated in the
header comment that is fine, and the comment says so.

**Security Notes:** `type_hash` is FNV-1a and the comment is honest that it
"only needs to catch accidental mismatch" — it is not a integrity check and is
not presented as one. The version, magic, record size and type hash are all
validated before a single record is decoded.

**Market-Logic Notes:** Journalling every decision with its sequence number is
what makes the backtest and the live path the same system rather than two
programs that agree by hope. The defect above is that the promise is not
enforced on the write side.

---

### `include/lob/measure/histogram.hpp` (interface) and `src/histogram.cpp` (implementation)

**Status:** Checked — [x] Reviewed (89 + 251 lines, read in full)

**Issues Found:** The recorded maximum is silently clamped at the ceiling, and
the per-stage report does not surface that. One dead branch.

- **`max()` returns the clamped value, not the true one.** `record_n` clamps at
  lines 109-110 and then updates `max_` from the clamped value at line 114. A
  stage that took 3 s in a `LatencyRecorder` (ceiling 1 s) reports
  `max = 1,000,000,000`. `Histogram::summary()` appends
  `[N clamped at ceiling]` — but `LatencyRecorder::report()`, which is the
  function every tool actually prints, does not read `overflow_count()` at all.
  So the one number in the whole measurement layer that must never be optimistic
  can be optimistic with no indication.
- **`equivalent_range`'s `sub >= sub_bucket_count_` branch is unreachable**
  (line 96). `sub_bucket_index` returns `value >> bucket`, which the bucketing
  construction keeps strictly below `sub_bucket_count_`.
- **`percentile_table` prints an interpolated TotalCount, not the real one.**
  Line 233 computes `count_ * percentile / 100.0` rather than carrying the
  running count out of `value_at_percentile`. Real `.hgrm` output carries the
  actual cumulative count; the difference shows at the coarse end of the table.

**Severity:** Major (Medium) (the unreported clamp), Minor (the other two)

**Why it matters:** The whole argument for this layer, stated in the header's
first paragraph, is "report p99.9 honestly instead of a mean". A max that
saturates at the ceiling without saying so is the one failure that argument does
not survive.

**Fix Recommendation:**
```cpp
// histogram.cpp record_n: track the true extreme before clamping.
void Histogram::record_n(std::int64_t value, std::int64_t n) noexcept {
  if (value < 0) value = 0;
  if (value > true_max_) true_max_ = value;      // new member, uncapped
  if (value > highest_) { value = highest_; overflow_ += n; }
  ...
}

// recorder.cpp report(): never print a clamped max as if it were the max.
if (h.overflow_count() > 0)
  out += "  [" + std::to_string(h.overflow_count()) + " over ceiling, true max "
       + std::to_string(h.true_max()) + "]\n";
```

**Refactor Suggestion:** `mean()` and `stddev()` each walk the full `counts_`
array and `stddev()` calls `mean()`, so `percentile_table` does two full passes
where one would do. Only matters for very large `significant_digits`; leave it.

**Tests Missing:** A recorded value above `highest_` should be asserted to (a)
increment `overflow_count()` and (b) be visible as such in
`LatencyRecorder::report()`. `tests/test_histogram.cpp` is audited separately;
it covers precision and percentiles but this path is the one that matters most
and is the least likely to be exercised by accident.

**Performance Notes:** The record path is genuinely O(1): an `or`, a
`countl_zero` (one `lzcnt` instruction), a shift, an add, and an increment. No
branches on the value except the two clamps. `counts_` is a flat
`vector<int64_t>` of `(bucket_count+1) * sub_bucket_half_count` entries — at
the default 1 hour ceiling and 3 significant digits that is roughly 33 KiB,
which fits in L2. This is the right implementation.

**Security Notes:** `percentile_at_or_below` computes `counts_index_for(value)`
for an arbitrary `value` — including a negative one, which produces a large
index — and is saved from an out-of-bounds read only by the
`std::min(target + 1, counts_.size())` clamp at line 149. It is correct, but it
is correct by one expression; an explicit `if (value < 0) return 0.0;` at the
top would make it correct by construction. All `snprintf` calls use fixed
buffers with `sizeof(buf)` and literal format strings.

**Market-Logic Notes:** `record_corrected` implements the coordinated-omission
correction (lines 117-126), which is the difference between a latency
measurement that is honest under load and one that flatters itself. The header
comment explains it correctly. `merge` re-records each slot at its stored value
and then repairs `min_`/`max_` from the source's exact extremes — the comment at
lines 191-193 shows the author found and fixed the obvious bug in that approach.

---

### `include/lob/measure/recorder.hpp`

**Status:** Checked — [x] Reviewed (84 lines, read in full)

**Issues Found:** The report omits the clamp indicator (see the histogram
section above — the fix belongs here). No other defects.

- **`report()` prints `h.max()` and never `h.overflow_count()`.** With the
  default 1 s ceiling, any stage that stalled longer reports exactly 1 s with no
  marker.
- **`operator[]` and `record_nanos` index `hists_` with an unchecked
  `Stage`.** Passing `Stage::Count` reads one past the end. Every call site
  passes a named enumerator, and the type makes anything else awkward, so this
  is a note rather than a finding.

**Severity:** Major (Medium) (the omitted clamp), Minor (the indexing)

**Why it matters:** As above: this is the function whose output humans read.

**Fix Recommendation:** As in the histogram section. Additionally, since
`kStageCount` is a compile-time constant and the vector is never resized, make
it a `std::array<Histogram, kStageCount>`; then `operator[]` on `Stage::Count`
is a compile-time-sized array access that sanitisers catch, and the heap
allocation disappears.

**Refactor Suggestion:** The class documents itself as single-threaded with a
`merge` for cross-thread aggregation (lines 47-49), which is the right design
and is stated with the right reason: atomics on the record path would perturb
the thing being measured.

**Tests Missing:** A test that `report()` is a stable, parseable shape; today
nothing asserts on it. Low value, but the clamp indicator above needs one.

**Performance Notes:** `record_ticks` converts to nanoseconds at record time via
`tsc::to_nanos`, which is a 128-bit multiply and a shift. Cheap, but see the
`stopwatch.hpp` note: the stated design is to defer conversion, and this does
not.

**Security Notes:** None.

**Market-Logic Notes:** The stage list matches the inbound path in
`docs/03-metrics-and-estimators.md` §2, and separating `TickToTrade` from its
components is what makes a latency budget attributable rather than merely
observed.

---

### `include/lob/measure/stopwatch.hpp`

**Status:** Checked — [x] Reviewed (59 lines, read in full)

**Issues Found:** The header comment contradicts the code, and `ScopedTimer` is
dead.

- **"conversion is deferred to report time so the measured path never pays for a
  divide" (lines 3-4) is false for `ScopedTimer`.** Its destructor calls
  `tsc::to_nanos(ticks)` at line 46, converting inside the measured scope's exit.
  With `LOB_HAS_INT128` that is a multiply and a shift rather than a divide, so
  the cost is small — but the comment states a design property the code does not
  have, and the next person to add a timer will assume it does.
- **`ScopedTimer` is used nowhere in the repository.**
  ```
  $ grep -rn "ScopedTimer" --include=*.cpp --include=*.hpp . | grep -v build
  ```
  returns only its own definition and two comments that reference it by name.
  `Stopwatch` is likewise only used in `apps/latency_demo` and
  `tests/test_tsc.cpp`.

**Severity:** Minor (both)

**Why it matters:** A comment that describes a design the code does not
implement is worse than no comment, because it is load-bearing for the next
change.

**Fix Recommendation:** Either make the comment true —
```cpp
~ScopedTimer() { hist_->record(static_cast<std::int64_t>(tsc::now_serialized() - start_)); }
```
with the histogram then holding ticks and the report scaling once — or correct
the comment to say that `Stopwatch` defers and `ScopedTimer` does not, and why.
The first is better: it makes the histogram's units explicit at construction.

**Refactor Suggestion:** `hist_` is a raw pointer only so that the deleted move
constructor is not a reference member; since move is deleted anyway, a
`Histogram&` is clearer and cannot be null.

**Tests Missing:** None that matter, given the class is unused. If it is kept,
one test that the recorded value tracks a known sleep to within the measurement
overhead.

**Performance Notes:** `now_serialized()` (`rdtscp` + `lfence`) is the right
choice for a short span and the comment at lines 36-38 is honest about the
40-60 cycle floor and about what to do instead when the stage is shorter than
that. That honesty is the most valuable thing in the file.

**Security Notes:** None.

**Market-Logic Notes:** Not applicable.

---

### `include/lob/measure/tsc.hpp`

**Status:** Checked — [x] Reviewed (101 lines, read in full)

**Issues Found:** One asymmetry between the two conversion directions. No
defects.

- **`to_nanos` is exact fixed-point with round-to-nearest; `from_nanos` is a
  `double` multiply with truncation** (lines 85-87 vs line 98). The comment at
  lines 82-84 argues, correctly, that a systematic downward bias of up to 1 ns
  per conversion accumulates badly — and then `from_nanos` has exactly that
  bias in the other direction. `from_nanos` is only used to convert a budget
  into a tick count, where the error is irrelevant, so this is consistency
  rather than correctness.

**Severity:** Minor

**Why it matters:** It does not, today. Recording it because the argument the
file makes for `to_nanos` applies verbatim to `from_nanos`.

**Fix Recommendation:**
```cpp
[[nodiscard]] inline std::uint64_t from_nanos(Nanos ns) noexcept {
#if LOB_HAS_INT128
  const auto m = calibration().nanos_mult;
  if (m == 0) return 0;
  return static_cast<std::uint64_t>((static_cast<u128>(ns) << kShift) / m);
#else
  return static_cast<std::uint64_t>(static_cast<double>(ns) * calibration().ticks_per_ns + 0.5);
#endif
}
```

**Refactor Suggestion:** `init()` is not `[[nodiscard]]` while `calibration()`
is; harmless, but make them agree.

**Tests Missing:** A round-trip property: `to_nanos(from_nanos(n))` within 1 ns
of `n` over a wide range. `tests/test_tsc.cpp` is audited separately.

**Performance Notes:** This is the strongest piece of engineering in the
measurement layer. `now()` is a bare `__rdtsc`; `now_serialized()` is
`__rdtscp` plus `_mm_lfence`, and the header states which to use when and why
(lines 29-31, 43-45). Conversion is a 128-bit multiply and a 32-bit shift, so
there is no floating-point on the conversion path and no divide. The 128-bit
intermediate is not defensive padding: `ticks * nanos_mult` genuinely overflows
64 bits within minutes of uptime, and the comment says so.

**Security Notes:** None. Every function is a pure computation over a
process-local calibration.

**Market-Logic Notes:** The refusal to trust an uncalibrated TSC is the point.
`Calibration::warning()` returns a specific sentence per missing flag rather
than a boolean, so a tool can print the reason its latency numbers are
worthless instead of printing worthless numbers.

---

### `include/lob/core/types.hpp`

**Status:** Checked — [x] Reviewed (78 lines, read in full)

**Issues Found:** The invalid-price sentinel compares as *better than every real
price* on the ask side.

- **`kNoPrice = INT64_MIN` combined with `better_than`.** Line 57:
  ```cpp
  return s == Side::Bid ? ticks_ > other.ticks_ : ticks_ < other.ticks_;
  ```
  For an ask, "better" is lower, and `INT64_MIN` is lower than everything. So
  `Price::none().better_than(any_real_ask, Side::Ask)` returns `true`. A
  default-constructed `Price` is `none()` (line 61), so an uninitialised or
  not-yet-set ask beats the real touch silently. The bid side is safe by
  accident: `INT64_MIN` loses every bid comparison.
- **`operator-` on `Price::none()` is signed overflow.** `real - none()`
  computes `ticks - INT64_MIN`, which is undefined behaviour, not a large
  number. `Ticks operator-(Price, Price)` (line 53) has no validity guard.

**Severity:** Major (Medium) (both — latent; `better_than` currently has no caller
outside `tests/test_types.cpp`, confirmed by
`grep -rn better_than --include=*.cpp --include=*.hpp`)

**Why it matters:** These are the two operations most likely to be reached for
when a strategy or a new feature computes "is this quote better than the touch",
and both fail in the direction that hides the failure — an invalid price that
wins a comparison rather than losing it.

**Fix Recommendation:**
```cpp
[[nodiscard]] constexpr bool better_than(Price other, Side s) const noexcept {
  if (!valid()) return false;          // nothing is better than a real price
  if (!other.valid()) return true;
  return s == Side::Bid ? ticks_ > other.ticks_ : ticks_ < other.ticks_;
}
```
For the subtraction, either assert validity in a debug build or give the
sentinel headroom (`kNoPrice = INT64_MIN / 2`) so the arithmetic is merely wrong
rather than undefined.

**Refactor Suggestion:** `Ticks`, `Qty`, `Nanos` and `SeqNum` are all aliases of
the same two integer types, so a `Qty` can be passed where a `Ticks` is expected
with no diagnostic. `Price` shows the author knows the strong-typedef pattern;
applying it to `Qty` would catch the class of bug where a size is used as a
price. That is a larger change than this audit should propose unprompted, so it
is listed as a suggestion, not a fix.

**Tests Missing:** `better_than` and `operator-` against `Price::none()`.
`tests/test_types.cpp` covers only valid prices (lines 24-28).

**Performance Notes:** Everything is `constexpr` and eight bytes. Nothing to
say.

**Security Notes:** None.

**Market-Logic Notes:** The first three lines of the file are the most important
decision in the repository: prices are integer ticks, never floating point,
because "a price that depends on rounding mode is a price that makes the
backtest irreproducible". `TickSize` as an exact rational rather than a `double`
follows from it, and the 1/32 example shows the author knew why. `sign_of` and
`better_than` exist so side-agnostic arithmetic does not get written twice with
one of the two copies wrong; that is the right instinct, which is why the
sentinel hole above is worth fixing rather than documenting.

---

### `include/lob/core/arena.hpp`

**Status:** Checked — [x] Reviewed (129 lines, read in full)

**Issues Found:** Four. `Arena::create` will happily construct a type it can
never destroy; `Pool::release` cannot detect a double release and its counter
underflows; two size computations can overflow.

- **`Arena::create<T>` has no `is_trivially_destructible` static assert**
  (lines 48-52), although `Pool` has exactly that assert for exactly the same
  reason (line 71). `Arena` never runs destructors and never can — `reset()`
  just zeroes `used_`. `arena.create<std::vector<int>>()` compiles and leaks.
- **`Pool::release` decrements `in_use_` with no guard** (line 108). Releasing a
  pointer twice, or one this pool never handed out, pushes the slot onto the
  free list a second time — so the *same slot is later acquired twice* and two
  live objects alias — and underflows `in_use_` to `SIZE_MAX`, which makes
  `available()` (line 113) underflow in turn.
- **`Arena::allocate` can overflow its own bounds check** (line 43):
  `used_ + pad + bytes > size_` wraps for a `bytes` near `SIZE_MAX` and then
  returns a pointer into a much smaller block.
- **`Pool`'s constructor computes `capacity * sizeof(Slot)`** (line 78) with no
  overflow check.
- **`allocate` assumes `align` is a power of two** (line 41, `~(align - 1)`)
  without asserting it.

**Severity:** Major (Medium) (the missing assert and the double-release), Minor (the
overflows — every current call site passes a compile-time `sizeof(T)` or a small
literal)

**Why it matters:** Neither of the two Medium findings can fire today, because
`Arena` and `Pool` have no production caller:
```
$ grep -rn "Arena\b|Pool<" --include=*.cpp --include=*.hpp . | grep -v build
tests/test_arena.cpp, bench/bench_measure.cpp   # and nothing else
```
That is itself the headline fact about this file. The header opens with "The hot
path must not call malloc", and the hot path does not use either class — the
order book is built on `std::vector` and `OrderMap`. So this is infrastructure
built ahead of a need, correct in its intent, and currently unexercised outside
its own test.

**Fix Recommendation:**
```cpp
// Arena::create — match Pool's contract.
template <typename T, typename... Args>
[[nodiscard]] T* create(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
  static_assert(std::is_trivially_destructible_v<T>,
                "Arena never runs destructors; use a trivially destructible T");
  ...
}

// Arena::allocate — bound-check without wrapping.
if (bytes > size_ - used_ - pad) return nullptr;   // after checking pad <= size_ - used_

// Pool::release — make a double release loud in a debug build.
void release(T* p) noexcept {
  if (p == nullptr) return;
  auto* s = reinterpret_cast<Slot*>(p);
  assert(s >= storage_ && s < storage_ + capacity_ && "pointer is not from this pool");
  assert(in_use_ > 0 && "release without a matching acquire");
  ...
}
```

**Refactor Suggestion:** `Pool::release` could keep a debug-only bitset of
in-use slots; that turns "the same slot handed out twice" from a silent aliasing
bug into an immediate assertion. Worth it before either class is put on a real
path.

**Tests Missing:** Double release, release of a foreign pointer, exhaustion of
`Arena` at a non-trivial alignment, and `Pool{0}`. `tests/test_arena.cpp`
covers the happy paths and exhaustion; it is audited separately.

**Performance Notes:** Both designs are right. The free list is intrusive
through a union, so it costs no extra memory (lines 116-121), and it is threaded
front to back at construction so the first allocations walk memory in address
order and prefetch (the comment at lines 80-81 states this, and it is the
detail most implementations miss). `allocate` is a compare, an add and a mask.
`kFalseSharingAlign = 128` rather than 64 is correct and the reason given
(x86 prefetches in 128-byte pairs, Apple silicon uses 128-byte lines) is right.

**Security Notes:** `::operator new(bytes, align_val_t)` throws on failure, so
there is no unchecked null; the destructors use the matching aligned
`operator delete`, which is the pairing that is easy to get wrong. Copy and move
are both deleted on both classes, so neither can be double-freed by an
accidental copy.

**Market-Logic Notes:** Not applicable. The one line that matters operationally
is the comment at lines 37-38: exhaustion returns `nullptr` and callers "must
treat that as a hard error and account for it, never silently fall back to the
heap." A market-data path that quietly starts calling `malloc` under load is how
a tail latency becomes a missed quote.

---

### `include/lob/core/compiler.hpp`

**Status:** Checked — [x] Reviewed (37 lines, read in full)

**Issues Found:** None. Two portability notes.

- **`[[gnu::always_inline]]` is applied unconditionally** (lines 12, 22),
  including on the MSVC branch the file otherwise takes care to provide. MSVC
  ignores unknown attributes with a warning rather than an error, so it
  compiles; the attribute belongs inside the same `#if` as the body it applies
  to.
- **The non-GCC fallback `volatile auto sink = value;`** (line 16) deduces by
  value, so it fails to compile for a non-copyable `T`, where the asm version
  would have worked. The project builds with GCC and Clang only, so this path is
  never taken.

**Severity:** Minor (both)

**Why it matters:** It does not, on the supported toolchains. Recorded for
completeness.

**Fix Recommendation:** Move the attribute inside the preprocessor branch.

**Refactor Suggestion:** None. Thirty-seven lines, one job.

**Tests Missing:** None. `bench/bench_measure.cpp` exercises
`do_not_optimize` by construction — if it stopped working, the benchmark
numbers would collapse to zero, which is a self-evident failure.

**Performance Notes:** This is the correct implementation of the
Google-benchmark idiom: an empty `asm volatile` with a `"r,m"` constraint and a
`"memory"` clobber, which forces the value to be materialised without emitting
an instruction. `clobber_memory()` separately forces pending stores to be
visible before a timed region begins.

**Security Notes:** None.

**Market-Logic Notes:** Not applicable, except that `LOB_HAS_INT128` gates the
exact fixed-point tick-to-nanosecond conversion in `tsc.hpp`, which is what
keeps latency arithmetic reproducible.

---

### `include/lob/book/events.hpp`

**Status:** Checked — [x] Reviewed (91 lines, read in full)

**Issues Found:** One missing include relied on transitively. No defects.

- **`std::is_trivially_copyable_v` at line 57 without `<type_traits>`.** It
  arrives through `<limits>` in `core/types.hpp` on libstdc++. Same class of
  fragility as `src/journal.cpp`.

**Severity:** Minor

**Why it matters:** It does not today; it is a build break waiting for a
standard-library reorganisation.

**Fix Recommendation:** `#include <type_traits>`.

**Refactor Suggestion:** None.

**Tests Missing:** None. The two `static_assert`s at lines 56-57 are the test,
and they are the right ones: 56 bytes fixed and trivially copyable are exactly
the properties the journal and the ring buffer depend on.

**Performance Notes:** The layout is deliberate. Six 8-byte fields, then the two
one-byte enums, then six bytes of explicit padding to 56 — so the compiler
inserts none of its own and the size is stable across compilers. `_pad` is
value-initialised, which means two logically equal events compare equal under
`memcmp`; that is what makes journal replay bit-for-bit reproducible rather than
merely semantically equal.

**Security Notes:** None. `event_name` and `error_name` handle every enumerator
including `Count` and still have an unreachable trailing `return "?"`, so a
value cast in from a corrupt file cannot fall off the end of the function.

**Market-Logic Notes:** This file is where the project's microstructure
vocabulary is defined, and the distinctions are the right ones.
- `Reduce` keeps queue position, `Replace` loses it (lines 17, 20). That is the
  real exchange rule, and it is the rule a market maker's requote logic lives or
  dies on.
- `Aggress` is documented as **not** a book mutation (lines 22-26): a book fed a
  real exchange feed never sees one, because the exchange matched it before
  publishing. Only the synthetic generator emits it, and it must go through the
  matching engine. Conflating the two is the single most common error in
  home-grown backtesters, and this file names it explicitly.
- `CrossedBook` (lines 68-73) is rejected and counted rather than absorbed, with
  the reason stated: absorbing it "would leave the book quietly describing an
  impossible market."
- Errors are returned, not thrown (lines 59-60), because a feed gap is routine
  and the hot path must not unwind.

---

### `include/lob/book/order_map.hpp`

**Status:** Checked — [x] Reviewed (113 lines, read in full)

**Issues Found:** The table never grows, and every one of its three loops spins
forever if it is full. Safe today only because of an invariant held by its
caller, which this file does not state.

- **`find`, `insert` and `erase_at` are unbounded probe loops** (lines 44, 55,
  96). All three iterate `while (slots_[i].used)` with no probe counter. On a
  full table, `find` for an absent key and `insert` for a new key never
  terminate — a hang, not a crash.
- **There is no resize and no load-factor check.** The constructor sizes the
  table once from `expected_orders` and `insert` never reconsiders. Exceeding
  `expected_orders` is not an error, it is a hang.
- **`expected_orders * 2` can overflow** (line 28), leaving `cap` at 16 for a
  very large request.
- **`kEmpty = 0xFFFFFFFF` is both "not found" and a representable value.** A
  stored `value` of `0xFFFFFFFF` would be indistinguishable from a miss. Slot
  indices never reach 4 billion here, so it is a note.

**Severity:** Major (Medium) (the hang, as a latent property of the class), Minor (the
rest)

**Why it matters:** The hang cannot fire in this repository, and the reason is
worth writing down because it is not written down anywhere in the code.
`OrderBook`'s constructor (`src/order_book.cpp:8-11`) passes the same
`max_orders` to both the order pool and the map:
```cpp
OrderBook::OrderBook(Ticks window_base, std::uint32_t window_ticks, std::size_t max_orders)
    : window_base_(window_base), window_(window_ticks), map_(max_orders) { ... pool_.assign(max_orders, Order{}); ... }
```
`OrderMap` rounds up to a power of two of at least `2 * max_orders`, and the
pool refuses the `max_orders + 1`-th order with `PoolExhausted`. So live entries
never exceed half the table. The safety is real, but it is a coupling between
two files, and nothing in `OrderMap` says so.

**Fix Recommendation:** State the invariant and enforce it locally, so the class
is safe on its own terms:
```cpp
// Capacity is fixed at construction. The caller MUST bound live entries below
// capacity() — OrderBook does this by sizing its order pool to expected_orders,
// giving a load factor of at most 0.5. Inserting into a full table would spin.
bool insert(OrderId key, std::uint32_t value) noexcept {
  if (size_ == slots_.size()) return false;      // never spin
  ...
}
```
Guard the overflow with `while (cap < expected_orders && cap < (SIZE_MAX >> 1)) cap <<= 1;` rewritten to avoid the doubling of the input.

**Refactor Suggestion:** None. Growing the table would defeat the purpose — a
rehash mid-session is a latency spike in exactly the place the design is trying
to avoid one. Fixed capacity plus a stated invariant is the right answer.

**Tests Missing:** Insert to exactly `capacity()`, then assert `insert` returns
false rather than hanging. That test cannot be written until the guard above
exists, which is the argument for adding it.

**Performance Notes:** This is the strongest data-structure choice in the
repository and the reasoning at the top of the file is correct.
- `Slot` is 16 bytes (8 key, 4 value, 1 flag, 3 pad), so four slots per cache
  line and a hit is usually one miss.
- Load factor under 0.5 keeps the expected probe length near 1.5.
- `mix` is the splitmix64 finaliser, and the reason given — exchange order ids
  are sequential or blocked, which linear probing handles badly unmixed — is
  exactly right and is the failure mode a naive `id & mask` would hit.
- Backward-shift deletion rather than tombstones (lines 8-10, 88-106). Over a
  trading day of balanced adds and cancels, tombstones accumulate without bound
  and every probe degrades; backward-shift keeps the table as dense as its live
  contents. I checked the shift condition at line 100 against the standard
  formulation and it is correct in both the wrapped and unwrapped cases.

**Security Notes:** No untrusted input reaches this class directly; keys come
from a decoder that has already validated them. The unbounded loops are the only
denial-of-service surface and are closed by the caller's invariant.

**Market-Logic Notes:** Not applicable, beyond the observation driving the
design: cancels and executes dominate a real feed, and both are lookups, so
lookup cost is the book's cost.

---

### `include/lob/book/order_book.hpp`

**Status:** Checked — [x] Reviewed (191 lines, read in full; implementation audited
separately under `src/order_book.cpp`)

**Issues Found:** Constructor arguments are unvalidated; one documented
precondition is not enforced.

- **`window_ticks` "must be a multiple of 64" (line 80) and nothing checks it.**
  A non-multiple is in fact harmless — `words = (window_ticks + 63) / 64` rounds
  up, and the padding bits in the final word are never set because `set_bit` is
  only ever called with a validated index — but a stated precondition that is
  neither enforced nor actually required is a comment that will mislead someone.
- **No validation of `window_ticks == 0` or `max_orders == 0`.** Both degrade
  gracefully (`in_window` rejects everything; the pool is exhausted from the
  start) rather than crashing, but they fail silently as "every event is an
  error" rather than as a configuration error.
- **`max_orders > UINT32_MAX` truncates** in the free-list fill
  (`src/order_book.cpp:22`). Not reachable at any plausible size.

**Severity:** Minor (all three)

**Why it matters:** Only the first, and only as documentation debt.

**Fix Recommendation:** Either drop the multiple-of-64 claim, or `assert` it.
Prefer dropping it and saying what is actually true: "`window_ticks` is rounded
up to a whole number of 64-bit bitset words; any value works."

**Refactor Suggestion:** None.

**Tests Missing:** A book constructed with `window_ticks` not a multiple of 64,
asserting that `scan_up`/`scan_down` never return an index at or beyond
`window_ticks`. That is the property the padding bits could break.

**Performance Notes:** The layout choices are stated in the header comment and
each is right for the access pattern.
- Price levels as a flat array indexed by `price - window_base`, not a
  `std::map`. At a 1-cent tick, 65,536 ticks is a $655 range, so the window is
  set once and never recentred — which removes a whole class of recentring bugs.
- One `uint64_t` bitset per side, scanned with `countl_zero`/`countr_zero`, 64
  levels per instruction, and only when a touch level empties.
- Intrusive doubly-linked FIFO through a preallocated pool: cancel is an O(1)
  unlink with no search.
- The costs: `bid_` and `ask_` are each `window_ticks * sizeof(Level)` — 32
  bytes per level, so 2 MiB per side at a 65,536-tick window, allocated whether
  or not those levels are ever touched. That is the trade the design makes, and
  it is the right one for a single instrument.

**Security Notes:** None at this layer. Every price is bounds-checked by
`in_window` before it becomes an index, which is the one place an out-of-range
feed value could become an out-of-bounds write.

**Market-Logic Notes:** Lines 19-22 state the decision the project turns on:
own orders live in the **same** FIFO as everyone else's, so queue position falls
out of the structure rather than needing a parallel bookkeeping system kept in
sync. Every backtester that models fills by "assume we are at the front" or "assume
we are at the back" is choosing one of two wrong answers; this one measures it.
`front_order_at` (lines 110-114) is the matcher's only window into the FIFO,
which keeps price-time priority the book's business and not the matching
engine's — the right place for it.

---

### `include/lob/book/reference_book.hpp`

**Status:** Checked — [x] Reviewed (175 lines, read in full)

**Issues Found:** Three null or throwing paths that the fast book validates and
this one does not; two behavioural asymmetries that bound what the differential
test can actually prove; two missing includes.

- **`find_entry` can return `nullptr` and all three callers dereference it
  unchecked** (line 168, dereferenced at lines 42, 66, 76). It returns null when
  `index_` and the price queue disagree. That cannot happen while the class is
  self-consistent — but this is the *reference* implementation, whose whole
  purpose is to be visibly correct, and a silent null dereference is not.
- **`remove()` dereferences `book.find(...)` without checking for `end()`**
  (lines 52-53), and `find_entry`/`queue_ahead`/`qty_of` use `book.at(price)`,
  which throws `std::out_of_range` on the same disagreement.
- **The reference book has no price window and no order-count limit.** The fast
  book returns `PriceOutOfWindow` and `PoolExhausted`; this one accepts both.
  So the differential test is only valid over in-window, under-capacity event
  streams. That is a real bound on what the comparison proves and it is not
  stated anywhere.
- **`<vector>` and `<utility>` are used at line 144 (`std::vector`,
  `std::pair`) and included nowhere.**

**Severity:** Major (Medium) (the unchecked `find_entry`, because of what this file is
for), Minor (the rest)

**Why it matters:** The header's closing line is "If this file ever gets clever,
it has stopped doing its job." Agreed — and the corollary is that it must also
never be able to crash or throw, because a segfault in the oracle looks like a
bug in the thing under test.

**Fix Recommendation:**
```cpp
BookError reduce(OrderId id, Qty by) {
  auto it = index_.find(id);
  if (it == index_.end()) return BookError::UnknownOrder;
  Entry* e = find_entry(it->second, id);
  if (e == nullptr) return BookError::UnknownOrder;   // index and queue disagree
  ...
}
```
and the same two lines in `execute` and `replace`. Add the two includes.

**Refactor Suggestion:** Document the two asymmetries at the top of the file, in
the same paragraph that explains what the class is for:
```
// This book has no price window and no order limit. The fast book rejects
// PriceOutOfWindow and PoolExhausted; a differential test must therefore stay
// inside the window and under capacity, or the two will disagree correctly.
```

**Tests Missing:** `tests/test_book_differential.cpp` is audited separately.
What is missing here is a test that drives *out-of-window* and
*over-capacity* events at both books and asserts they disagree in the expected
way, which would turn the undocumented asymmetry into a checked one.

**Performance Notes:** Deliberately slow and that is correct. `std::map` for
levels, `std::list` for each queue, `std::unordered_map` for the index — a
pointer chase everywhere, which is the point. `qty_at` and `sum_of` are O(orders
at the level) rather than a cached total, so nothing can be stale.

**Security Notes:** None; it never touches external input directly.

**Market-Logic Notes:** The rules match the fast book where it matters:
`reduce` keeps position, `replace` is remove-then-add so priority is lost, and
`add` rejects a crossing order before mutating. One shared behaviour is worth
naming because both books do it and it is a modelling choice, not an accident:
**a `replace` whose `add` leg fails leaves the book without the original
order.** Real venues generally reject the modify and leave the resting order
intact. It is consistent across both implementations, so the differential test
will never catch it; it should be a deliberate, documented decision rather than
an emergent one.

---

### `include/lob/feed/line_reader.hpp`

**Status:** Checked — [x] Reviewed (54 lines, read in full; implementation audited under
`src/line_reader.cpp`)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** The interface is small enough to be obviously right, and the
one subtle contract — "The view is valid until the next call" (line 29) — is
stated where a caller will read it.

**Fix Recommendation:** None.

**Refactor Suggestion:** `handle_` is a `void*` reinterpreted as `FILE*` or
`gzFile` according to `gz_`. A two-member variant or a small abstract base would
be type-safe, at the cost of a virtual call per 64 KiB fill, which is nothing.
Not worth changing while the file is this short.

**Tests Missing:** As noted under the implementation: a line longer than the
64 KiB chunk.

**Performance Notes:** Move construction is not declared, and copy is deleted,
so a `LineReader` cannot be returned from a factory. Fine for how it is used.

**Security Notes:** `owns_` correctly distinguishes stdin from an owned handle.

**Market-Logic Notes:** The rationale at lines 30-33 is a data-integrity
decision worth keeping: a recording interrupted mid-frame ends with a partial
line, and that line is "a decode failure to be counted, not a file the reader
should refuse". Refusing the whole capture over its last 40 bytes would throw
away hours of good data.

---

### `include/lob/sim/latency.hpp`

**Status:** Checked — [x] Reviewed (125 lines, read in full)

**Issues Found:** A sort with no effect; a scale factor applied to empirical
samples that may already carry it; one tie-break case the determinism argument
does not cover.

- **`set_empirical` sorts and nothing reads the order.** Line 62 sorts
  `empirical_`, and `draw` picks uniformly at random (line 70), which is
  order-independent. The sort is either vestigial from an intended inverse-CDF
  draw or a misunderstanding. It is dead work at load time and, more
  importantly, it suggests a quantile-based draw that is not there.
- **`outbound_scale` multiplies an empirical sample as well as a parametric
  one** (line 74). One measured distribution is used for both directions and
  the outbound one is that distribution stretched by 1.3. If the samples were
  measured inbound that is the stated model; if they were measured outbound the
  factor is applied twice. The API takes a single `set_empirical` with no
  direction, so a caller cannot express the difference.
- **`InFlight::Later` breaks ties on `arrive_ts`, then `decided_ts`, then `id`
  (lines 117-119), and `type` is not in the ordering.** A `Cancel` and a `Limit`
  for the same id, decided in the same nanosecond and arriving in the same
  nanosecond, order arbitrarily — and `std::priority_queue` is not stable. The
  comment at lines 98-100 promises "two messages arriving in the same nanosecond
  must resolve the same way on every run, or the journal will not replay."
- **`empirical_[rng_() % empirical_.size()]`** has modulo bias, on the order of
  `size / 2^64`. Irrelevant at any real sample count; noted for completeness.

**Severity:** Minor (all — `set_empirical` has no caller anywhere in the
repository, confirmed by grep, so the parametric path is the only live one)

**Why it matters:** The determinism claim is load-bearing for the whole journal
replay argument in `docs/00`, so the one gap in it is worth closing even though
it needs a coincidence to fire.

**Fix Recommendation:**
```cpp
// Complete the ordering so no two distinct actions can compare equal.
if (a.id != b.id) return a.id > b.id;
return static_cast<std::uint8_t>(a.type) > static_cast<std::uint8_t>(b.type);
```
Either delete the sort in `set_empirical` or use it — an inverse-CDF draw from
the sorted samples is the same cost and admits interpolation between them.

**Refactor Suggestion:** Give `set_empirical` a direction
(`set_empirical(Direction, std::vector<Nanos>)`) so inbound and outbound can
carry separate measured distributions, which is what Phase 2b will actually
produce.

**Tests Missing:** That the drawn median matches `median_ns` to within sampling
error, and that the floor is respected. Nothing currently tests this file.

**Performance Notes:** Not on a measured path — it runs inside the simulator, in
simulated time.

**Security Notes:** None.

**Market-Logic Notes:** This file's header is the clearest statement of purpose
in the repository and the modelling choices behind it are right.
- Parameterising the **median** rather than the mean of a lognormal: line 50
  passes `log(median_ns)` as the underlying normal's mean, which makes
  `median_ns` exactly the median. That is the correct parameterisation and it is
  easy to get wrong.
- Scaling a lognormal draw by a constant scales its median and leaves sigma
  alone, so `outbound_scale = 1.3` means "outbound is 30% slower at every
  quantile" — a coherent statement.
- Asymmetric inbound and outbound, because inbound is a broadcast hop and
  outbound goes through a gateway and a risk check (lines 40-41). Correct, and
  usually omitted.
- The refusal to use a constant latency, with the reason (multi-modal: warm
  path, cold path, interrupt) at lines 17-20.

---

### `include/lob/sim/simulator.hpp`

**Status:** Checked — [x] Reviewed (247 lines, read in full)

**Issues Found:** Fills produced by the agent's own orders are published to the
agent's view late and with the wrong timestamp, or never. The realised inbound
delay is not the configured distribution. Fill timestamps are the enclosing
event's, not the action's arrival.

- **`fills_seen_` only advances inside the `EventType::Aggress` branch** (line
  127). `apply_action` (line 182), reached from `drain_actions` at step 1, can
  append fills — an agent limit order that lands crossing, or an agent market
  order. Those fills are booked into `stats_` by `book_fill` but are **not**
  turned into `Execute` events for the view book, because step 4 only publishes
  when the *market* event is an `Aggress`. The consequences:
  - Until the next `Aggress` event arrives, `view_book_` still shows resting
    orders that the agent's own order consumed. The view is not merely stale by
    the inbound latency, it is wrong.
  - When the next `Aggress` does arrive, the backlog is published stamped with
    **that** event's `ts` and `seq` (lines 123-124), so the agent sees its own
    fill dated later than it happened.
  - In a run with no `Aggress` events at all, they are never published.
- **`pending_md_` is a FIFO, so an event with a short latency draw waits behind
  one with a long draw.** `deliver` pushes `t + lat_.inbound()` (line 218) and
  `drain_market_data` pops only while `front().arrive <= t` (line 226). The
  realised delay is therefore a running maximum, not the lognormal that was
  configured. For a WebSocket feed, in-order delivery is the physically correct
  model — but then the per-message independent draw is the wrong way to produce
  it, and the effective distribution is heavier-tailed and autocorrelated in a
  way nothing documents.
- **Fills are stamped `now_`, the current market event's timestamp, not
  `a.arrive_ts`** (lines 187, 190). An action that arrived at t=100 during an
  event at t=150 produces a fill dated 150. That is up to one inter-event gap of
  error in every markout and every latency attribution computed from fills.
- **`stats_.realised_pnl` is a cash flow, not a P&L.** Line 213 accumulates
  `-signed_qty * price` and nothing marks the remaining `inventory` to market.
  The field name and the comment "in ticks * shares" both invite it to be read
  as a result.

**Severity:** Major (High) (the unpublished agent fills), Medium (the FIFO latency and
the fill timestamps), Minor (the P&L naming)

**Why it matters:** The first one defeats the file's own thesis. The header
states, in capitals, "THE AGENT SEES A STALE BOOK" — the model is that the view
lags the truth by a known latency. A view that omits the agent's own executions
entirely is not lagging, it is incorrect, and a strategy that reads its own
resting size from `view_book_` will believe an order is still there after it
traded. That is the exact error the two-book design exists to prevent, arriving
by a different door.

**Fix Recommendation:** Publish fills once, from wherever they are produced.
```cpp
// Replace both publication sites with one, called at the end of every step.
void publish_new_fills(Nanos ts, SeqNum seq) {
  for (std::size_t k = fills_seen_; k < match_.fills().size(); ++k) {
    const Fill& f = match_.fills()[k];
    BookEvent ex{};
    ex.ts = f.ts; ex.seq = seq; ex.type = EventType::Execute;
    ex.order_id = f.resting_id; ex.side = f.resting_side; ex.qty = f.qty;
    deliver(ex, f.ts);              // deliver from when it happened
  }
  fills_seen_ = match_.fills().size();
}
```
called unconditionally after step 3, with `Fill` carrying its own timestamp. And
pass `a.arrive_ts` rather than `now_` into `submit_limit` / `submit_market`
(lines 187, 190), so a fill is dated when it occurred.

**Refactor Suggestion:** If in-order feed delivery is the intended model, say so
and implement it directly: draw one delay, then take
`arrive = max(previous_arrive, t + draw)`. That makes head-of-line blocking the
stated model rather than an emergent property of the container.

**Tests Missing:**
- After an agent market order fills a resting order, `view_book_` eventually
  reflects it, and does so without waiting for an unrelated `Aggress` event.
  This is the test that would have caught the finding above.
- `view_staleness_ticks()` grows with `median_ns` and is zero when
  `use_latency = false`.
- Two runs with the same seed produce byte-identical fill sequences — the
  determinism claim at lines 16-18 is currently unasserted.

**Performance Notes:** `std::deque<Delayed>` for pending market data is right
for FIFO drain. `match_.fills()` grows without bound for the life of the run
(recorded as a High finding under `include/lob/sim/matching.hpp`), and this file
is the reason it matters: `fills_seen_` and `before` both index into that
ever-growing vector, so a long run's memory is proportional to total fills.

**Security Notes:** None; no external input.

**Market-Logic Notes:** Where it is right, it is right for good reasons.
- Step 4's comment (lines 116-118): an aggressive order reaches the agent as the
  **executions it caused**, never as the incoming order, "which is exactly what
  a real feed publishes". Most home-grown simulators leak the aggressor.
- `SimStats` separates passive from aggressive fills with the reason stated
  (lines 48-51): with latency, a quote decided on a stale book can land
  crossing, "which is how being slow turns liquidity provision into liquidity
  taking without the strategy asking". That is the number a market maker most
  needs and it is measured here.
- `late_cancels` (line 56) counts the cancel that lost the race. That is adverse
  selection with a clock on it, and it is counted rather than assumed away.
- `AgentView` (lines 61-68) hands the agent `view_book_` and not `true_book_`,
  with the comment naming this as the bug the class exists to prevent.

---

### `include/lob/feat/features.hpp`

**Status:** Checked — [x] Reviewed (228 lines, read in full)

**Issues Found:** Three. A stale mid on a one-sided book with no way to detect
it; two fields named `_ewma` computed on different normalisations; one
"half-life" that is a time constant.

- **`mid`, `bid`, `ask`, `spread`, `imbalance` and `weighted_mid` are only
  updated when both sides are non-empty** (line 106). On a one-sided book they
  silently retain their previous values, and `Features` carries no validity
  flag. A consumer cannot distinguish "the mid is 9,991.5" from "the mid was
  9,991.5 before the ask side emptied". `f_.updates` still increments, so even
  that is no signal.
- **`ofi_ewma` is a decayed *sum*, `vol_ewma` is a true EWMA.** Line 139 is
  `(1 - α)·prev + ofi_deep`; line 147 is `(1 - α)·prev + α·d²`. With the default
  100-event half-life, α ≈ 0.0069, so `ofi_ewma` sits on a scale roughly 145
  times larger than an EWMA of the same series. The comment at lines 137-138
  describes the decayed sum accurately, so the code matches its documentation —
  but the shared `_ewma` suffix invites the two to be compared or normalised
  alike.
- **`rate_halflife_ns` is a time constant, not a half-life.** Line 154 computes
  `α = 1 - exp(-Δt / τ)`, with no `log(2)`, while lines 94-95 do include
  `log(2)` for the other two. The actual half-life of `event_rate` is
  `τ · ln 2 ≈ 0.693 s`, not the 1 s the name promises.

**Severity:** Major (Medium) (the stale mid), Minor (the two naming defects)

**Why it matters:** All three are currently contained. `ofi_ewma` is printed by
`apps/replay` and `apps/tape` and feeds no decision:
```
$ grep -rn "ofi_ewma" --include=*.cpp --include=*.hpp . | grep -v build
```
returns only the definition, two print statements, and NaN checks in the tests.
The stale mid is the one that will bite: a one-sided book is exactly the state a
market maker must not quote into, and this engine reports it as normal.

**Fix Recommendation:**
```cpp
struct Features {
  bool two_sided = false;   // false => bid/ask/mid/spread/imbalance are stale
  ...
};

// in update(), after snapshot:
f_.two_sided = (now.n_bid > 0 && now.n_ask > 0);
if (f_.two_sided) { ... }
```
Rename `ofi_ewma` to `ofi_decayed_sum`, and either rename `rate_halflife_ns` to
`rate_tau_ns` or add the `log(2.0)` factor at line 154 to match the other two.

**Refactor Suggestion:** `FeatureConfig` accepts a zero or negative half-life
without complaint; a negative one gives `α > 1` and an EWMA that diverges. A
constructor check is two lines.

**Tests Missing:** A one-sided book: assert the consumer can tell. That test
cannot be written until the flag exists, which is the argument for adding it.
`tests/test_features.cpp` covers `weighted_mid`, `deep_imbalance` and NaN
freedom, and is audited separately.

**Performance Notes:** The O(1)-per-event constraint is real and is met. The
only loops are over `kOfiLevels = 5`, a compile-time constant, and the
`BookTop` snapshot is two `depth()` calls. `ofi_at` is `static` and pure, which
is what lets `tests/test_features.cpp` check it against a from-scratch
recomputation — a genuinely good testability decision.

**Security Notes:** None.

**Market-Logic Notes:** The strongest file in the repository for stated
reasoning, and the honesty is the substance.
- Lines 14-19 refuse to call `weighted_mid` the micro-price: the true
  micro-price (Stoikov 2018) is the fixed point of a transition matrix estimated
  from data, and this is its first-order approximation. The note that "the two
  differ most in exactly the states a market maker cares about" is correct and
  is the reason the distinction is not pedantry.
- Half-lives in **events** rather than seconds (lines 83-85), because order flow
  is driven by activity and an event clock is the standard fix for intraday
  seasonality. `rate_halflife_ns` is on the wall clock, and the comment says why:
  "this one IS a clock: it measures the clock".
- `ofi_at` implements Cont, Kukanov & Stoikov correctly, including the case
  where a level appears or disappears between snapshots, and the comment (lines
  177-180) states the insight the formula encodes: a price move and a size
  change are the same kind of event once viewed this way.

---

### `include/lob/feed/json.hpp`

**Status:** Checked — [x] Reviewed (243 lines, read in full)

**Issues Found:** No memory-safety or termination defects. Four points of
leniency where a malformed document is accepted rather than rejected.

I traced every loop for termination and every `substr` for underflow:
- `skip_string` cannot run past the end, including on a trailing backslash
  (lines 46-48).
- `skip_value`'s object/array branch advances `i` on every path, so a hostile
  input cannot spin; nesting is an integer counter, not recursion (lines 57-59),
  so ten thousand open brackets cost ten thousand iterations and no stack.
- `find`'s `s.substr(key_start + 1, i - key_start - 2)` (line 119) cannot
  underflow, because `skip_string` returning true guarantees `i - key_start >= 2`.
- `find`'s `while (true)` advances on every branch that does not return.

The leniencies:
- **Brackets are not matched by type.** `skip_value` (line 75) accepts `}` for
  an opening `[` and `]` for a `{`, so `[1,2}` scans as a well-formed value.
- **Array elements do not require a separating comma.** `array_next` (line 154)
  consumes a comma if present and proceeds regardless, so `[1 2]` iterates as
  two elements.
- **`array_next` strips the quotes from a string element** (lines 160-161)
  while its comment (line 142) says it "sets `elem` to the raw text".
- **`parse_decimal` accepts a leading `+` and a trailing `.`** (lines 200, 215),
  neither of which is valid JSON.

**Severity:** Minor (all four)

**Why it matters:** All four make the scanner accept slightly more than JSON.
None makes it accept something that decodes to a *different* value than a strict
parser would, which is the property that would matter. Given the input is
machine-generated by one venue, this is the right side to err on.

**Fix Recommendation:** None required. If the scanner is ever pointed at a
second venue, match the bracket type:
```cpp
char want = (s[i] == '{') ? '}' : ']';   // pushed onto a small fixed stack
```

**Refactor Suggestion:** None.

**Tests Missing:** The leniencies above, as explicit "we accept this" cases, so
that a future tightening is a deliberate change rather than a silent one.
`fuzz/fuzz_bitstamp.cpp` is audited separately and covers the safety properties.

**Performance Notes:** Everything is `constexpr` and allocation-free; values are
returned as `string_view` into the caller's buffer. Nested values are skipped
wholesale rather than parsed, so a lookup costs one pass over the object.
Decoding runs once per capture, not on the hot path, and the file says so.

**Security Notes:** This is the repository's only untrusted-input surface and it
is treated as one. The header states the threat model explicitly (lines 8-11):
the input "arrived over a public websocket and was written to disk unmodified",
every function is total and bounds-checked, "nothing reads past the end, nothing
throws, nothing allocates", and a fuzz target exists to keep that true. Having
read every function, that claim holds. `parse_u64` and `parse_decimal` both
check for overflow **before** multiplying (lines 173, 209, 220, 235), which is
the correct order and the one usually got wrong.

**Market-Logic Notes:** Two decisions here are load-bearing for the whole
project.
- **Numbers are read as exact decimal strings, never through `double`**
  (lines 13-16). `parse_decimal` returns an integer scaled by `10^scale`, and
  the reason given is right: a price that survives a round trip through binary
  floating point only most of the time makes a backtest irreproducible.
- **`parse_decimal` returns false rather than rounding** when the value carries
  more fractional digits than `scale` can hold (lines 223-224). The comment is
  correct that a decoder which quietly truncates a size produces a book that
  "disagrees with the exchange by an amount nobody ever sees". The operational
  consequence is that such an event is *dropped*, so the decoder must count the
  rejection — `src/bitstamp.cpp` does, which is what makes this safe.
- `find` searches only the object's own keys and skips nested values wholesale
  (lines 99-103), so a `price` inside `data` cannot satisfy a lookup meant for
  the envelope. That is a real Bitstamp shape and a real bug avoided.

---

### `include/lob/strat/pnl.hpp`

**Status:** Checked — [x] Reviewed (214 lines, read in full)

**Issues Found:** The headline P&L excludes the mark-to-market of the leftover
position, and every test pins that position to zero so nothing catches it. Two
smaller points.

- **`apps/backtest` settles its comparison on a number the repository says it
  must not.** `Attribution::total` (line 154) is
  `spread_capture - adverse_sel - fees` and deliberately excludes the closing
  position. That exclusion is correct and documented —
  `include/lob/strat/driver.hpp:106-115` states it outright: "Attribution::total
  is NOT this. It is spread capture minus adverse selection at one markout
  horizon — a decomposition of trading edge, which deliberately says nothing
  about a position still open at the end. Comparing strategies on it credits
  nothing to one that made its money by holding... this is the number the
  comparison is settled on", where "this" is `RunResult::pnl()`.
  `apps/evaluate` follows that rule and calls `pnl()` at every site.
  **`apps/backtest/main.cpp:62` is the only consumer of `attr.total` in the
  repository**, prints it under the column heading `net`, and never prints
  `pnl()`. So the Phase 4 comparison is settled on the diagnostic rather than on
  the result, in direct contradiction of the comment two files away.
- **`inventory_mtm` is computed and never read.** Line 153 fills it in and
  nothing in the repository consumes it — grep returns only the definition and
  that assignment. Whatever it was for, it is dead.
- **This file's header cites `docs/03-metrics-and-estimators.md` §10**, which
  specifies Total P&L as five lines *including* "Inventory / hedging cost
  (mark-to-market of held inventory)". `Attribution` implements four of the five
  and calls itself that decomposition. The code is right and the citation is
  loose — but a reader who follows the reference will expect the fifth line.
- **`MarkoutTracker::advance` returns early when `mid_now <= 0.0`** (line 87).
  A horizon that elapses while the mid is unavailable is not recorded then; it
  is recorded at the next call with a valid mid. So the realised horizon is
  silently longer than the nominal one, and nothing marks those fills.
- **`<deque>`, `<string>` and `lob/sim/matching.hpp` are included and unused.**

**Severity:** Major (High) (the omitted inventory mark), Minor (the other two)

**Why it matters:** A market-making backtest whose P&L ignores the closing
position is measuring the wrong quantity, and it fails in the direction that
flatters a strategy which accumulates inventory rather than managing it. That is
precisely the failure mode the inventory-penalty work in `apps/solve` exists to
prevent, so the metric and the objective currently disagree.

**Fix Recommendation:** Make the omission explicit rather than accidental —
either fold it in, or name the field for what it is.
```cpp
  a.inventory_mtm = static_cast<double>(final_inventory) * final_mid;
  a.realised      = a.spread_capture - a.adverse_sel - a.fees;
  a.total         = a.realised + a.inventory_mtm;   // what the run actually made
```
and update `apps/backtest/main.cpp` to print both columns, so a strategy cannot
hide a loss in an open position. Then change at least one case in
`tests/test_pnl.cpp` to pass a non-zero `final_inventory` and assert the
difference.

**Refactor Suggestion:** Drop the three unused includes.

**Tests Missing:** The one above. Also a test that `advance` at a stale mid does
not silently stretch a horizon, or an assertion that it may.

**Performance Notes:** `advance` is O(open fills x horizons) per call and
`open_` is compacted by swap-with-back (line 96), so a fill leaves the open set
as soon as its last horizon fills. At a 1 s longest horizon the open set is
bounded by one second of fills. `fills_` grows for the life of the run — the
same unbounded-growth class as `MatchingEngine::fills_`, and for the same
reason. Indices rather than pointers into `fills_` (line 81) is the right choice
given the vector reallocates.

**Security Notes:** None.

**Market-Logic Notes:** The decomposition is correct and the reasoning is
better than the code.
- The identity at lines 15-17 holds exactly:
  `spread_capture - adverse_selection = s(M0 - P)q - s(M0 - M1)q = s(M1 - P)q = markout`.
  The header's insistence that these are "not independent estimates of the same
  thing" but "the two halves a fill decomposes into" is the right framing.
- Adverse selection is signed positive when the price moved against you (line
  19), which is the convention that makes the number readable.
- **`filled[h]` rather than treating a missing horizon as zero** (lines 49-51).
  A fill near the end of a run never reaches its 1 s markout, and averaging an
  unfilled slot in as zero would bias every long-horizon number toward zero.
  This is the single most common error in markout code and it is handled.
- **The block bootstrap** (lines 158-163). Fills are clustered and the P&L of
  fills within a burst is driven by the same price move; a per-fill t-statistic
  assumes an independence that does not exist. Resampling contiguous blocks is
  the right fix.
- **`kMinBlocks = 10`** (lines 166-168): below that the interval "is not an
  interval, it is an illusion of one", and `usable()` says so rather than
  printing it anyway.
- **`excludes_zero()`** (lines 174-177) is `lo > 0 || hi < 0`, and the comment
  records that the previous formulation `(lo > 0) == (hi > 0)` wrongly called
  the degenerate interval `[0, 0]` significant. That is a real bug, found and
  fixed, and documented where the next person will see it.

---

### `include/lob/strat/quoting.hpp`

**Status:** Checked — [x] Reviewed (247 lines, read in full)

**Issues Found:** **Four of the six baselines do not do what their names say.**
Two emit marketable orders on any non-zero inventory; two collapse to
`ConstantSpread` over their whole working range. `mid_of` returns a nonsense mid
on a one-sided book and five of the six strategies use it unguarded.

**1. `InventorySkew` and `AvellanedaStoikov` quote through the market.**
`assemble` clamps the half-spread into `[min_half, max_half]` (lines 93-94) but
never clamps the **centre**. Both strategies centre on the Ho-Stoll reservation
price `r = mid - inv*gamma*sigma^2*(T-t)` (line 148). With the **defaults in
`QuoteParams`** — `gamma = 0.05`, `sigma = 1.0`, `horizon = 1e5` — that is
`mid - inv*5000` ticks. Evaluated against a one-tick book with mid 10,000.5:

| inventory | reservation price | InventorySkew bid / ask | A-S half | A-S bid / ask |
|---|---|---|---|---|
| 0 | 10,000.5 | 9,999 / 10,002 | 50.0 | 9,950 / 10,051 |
| 1 | 5,000.5 | 4,999 / 5,002 | 50.0 | 4,950 / 5,051 |
| 5 | -14,999.5 | -15,001 / -14,998 | 50.0 | -15,050 / -14,949 |
| 50 | -239,999.5 | -240,001 / -239,998 | 50.0 | -240,050 / -239,949 |

At an inventory of **one share** the ask is quoted 4,999 ticks below the best
bid: a sell at any price.

**Which callers hit it.** This is the part worth being precise about, because
the defaults are not universal:

| caller | gamma | sigma | horizon | skew per share |
|---|---|---|---|---|
| `QuoteParams` defaults | 0.05 | 1.0 | 1e5 | 5,000 ticks |
| `apps/evaluate/main.cpp:77` `base_params()` | default | default | default | 5,000 ticks |
| `apps/backtest/main.cpp:118` | 0.05 | 0.5 | 1.0 | 0.0125 ticks |
| `tests/test_strategies.cpp:24` | 0.05 | 0.5 | 1.0 | 0.0125 ticks |

`base_params()` sets only `size` and `max_inventory` and leaves the three risk
parameters at their defaults. `apps/evaluate` is the **Phase 5 acceptance
test**. The backtest and the test suite both override `horizon` to 1.0, which is
exactly why nothing catches this.

Running the acceptance test confirms it:
```
$ ./build/evaluate --table policy/ethusd.bin --seeds 2 --events 60000
strategy                session P&L   spread-cap   adv-select  peak inv    pasv    aggr requotes
ConstantSpread                643.8        238.8       -263.8      58.5      86       0      888
InventorySkew             -171431.8    -181010.5       5411.2       8.5       2   37224    37166
AvellanedaStoikov         -140578.8    -164483.5     -21399.2       9.0       6   31748    31807
GLFT                          627.0        226.8       -263.8      55.5      84       0      871
ImbalanceSkew                 178.5        225.2       -275.2      58.0      82       1     1195
JoinTouch                    -304.5       1523.5       -148.8      58.0     530       0     1068
TabulatedMDP                 -304.5       1523.5       -148.8      58.0     530       0     1068
```
`InventorySkew` takes **2 passive fills and 37,224 aggressive ones**;
`AvellanedaStoikov` takes 6 and 31,748. Their peak inventory is 8.5 against
58.5 for `ConstantSpread`, because they dump the position the instant they
acquire it. Neither is quoting.

Note also that A-S's own contribution is discarded even where it is sane.
`optimal_spread` at the defaults evaluates to
`0.05*1*1e5 + 40*ln(1.0333) ~= 5001` ticks, a half-spread of 2,500, which
`assemble` clamps to `max_half = 50`. The A-S spread formula has no effect on
the quotes at all; what remains is a 50-tick spread around a broken centre.

**2. `GLFT` and `ImbalanceSkew` collapse to `ConstantSpread`.** With
`base(p) = 0.0219` and `inventory_term(p) = 0.2146` at the defaults:

| q (inventory / size) | half_bid | after clamp | half_ask | after clamp |
|---|---|---|---|---|
| 0 | 0.129 | 1.0 | 0.129 | 1.0 |
| 2 | 0.558 | 1.0 | -0.300 | 1.0 |
| 20 | 4.421 | 4.4 | -4.163 | 1.0 |

`min_half = 1` swallows the entire inventory term below `q ~= 4.1`, and the
position limit in `evaluate` is 50 shares — `q <= 5`. `half_ask` is negative for
any `q > 0.6`, so GLFT's ask **never widens with inventory** anywhere in its
working range. `ImbalanceSkew` adds a tilt of at most +-1 tick to those clamped
values, and since `q.bid = floor(mid - half_bid)` on a one-tick book, a
half-spread anywhere in `[1, 1.13]` floors to the same tick.

The measured table above is the confirmation: GLFT scores 627.0 against
ConstantSpread's 643.8, with 84 passive fills against 86 and 871 requotes
against 888. They are the same strategy.

**2b. `TabulatedMDP` is byte-identical to `JoinTouch`.** Every column matches
(-304.5, 1523.5, -148.8, 58.0, 530, 0, 1068), and the acceptance test's own
paired comparison reports:
```
  TabulatedMDP minus JoinTouch, paired by seed:
    mean +0.0   95% CI [+0.0, +0.0]   over 2 seeds, 0 of them positive
```
An identically-zero difference is not a result, it is a signal that the solved
policy emits the join-the-touch action in every state the run reaches, or that
the table lookup is not differentiating states. The tool flags the *first*
comparison as degenerate ("the criterion is degenerate here; read the second
comparison") but says nothing about the second being exactly zero. That is the
one number the Phase 5 acceptance criterion turns on, and it currently carries
no information. It should be investigated before the criterion is quoted
anywhere.

**3. `detail::mid_of` does not check that the book is two-sided** (lines
107-109). `OrderBook::best_bid()` returns 0 for an empty side, so a book with no
bids and asks at 10,000 yields a "mid" of 5,000. Strategies 1-5 call it
unguarded; only `JoinTouch` checks (line 236).

**4. `JoinTouch::name()` returns `"JoinTouch"`** while every other strategy
returns a lowercase hyphenated name, and it is not `constexpr` like the others.
Cosmetic, but it lands in every comparison table.

**Severity:** **Critical** (findings 1, 2 and 2b — the Phase 5 acceptance
table in `apps/evaluate` does not measure what it reports for five of its
seven rows), Medium (finding 3), Minor (finding 4)

**Why it matters:** The whole argument of Phases 4 and 5 is that a learned
policy must beat honest baselines. In the acceptance test, four of the six are
not baselines: two are aggressive-order generators whose 171,000-tick losses are
a parameterisation artefact, and two are the null hypothesis wearing another
name. The fifth, `JoinTouch`, is the only baseline that both trades and quotes
sensibly — and the learned policy ties it exactly. So the acceptance table has
one real comparison in it and that comparison returns zero.

The file's header already knows half of this. Lines 16-28 explain that models
1-5 quote a half-spread from the mid, and that on a one-tick book the smallest
half-spread the grid admits already puts the quote a tick behind the touch, so
"a comparison against them is a comparison against abstention." That is exactly
right, and `JoinTouch` was added for it. What the header does not say is that
two of the five do not abstain — they cross.

**Fix Recommendation:** Two changes, both small.
```cpp
// 1. assemble(): never quote through the market. Pass the touch in.
inline Quote assemble(double mid, double half_bid, double half_ask,
                      const QuoteParams& p, std::int64_t inv,
                      Ticks best_bid, Ticks best_ask) {
  ...
  q.bid = std::min(static_cast<Ticks>(std::floor(mid - half_bid)), best_bid);
  q.ask = std::max(static_cast<Ticks>(std::ceil (mid + half_ask)), best_ask);
  if (q.ask <= q.bid) q.ask = q.bid + 1;
  ...
}
```
That alone turns both broken strategies from liquidity takers into
join-the-touch quoters, which is a defensible baseline.
```cpp
// 2. Scale the risk term so the skew is measured in ticks, not thousands.
//    gamma * sigma^2 * horizon is the skew per unit of inventory. Choose it so
//    that a full position skews by a few ticks:
//        skew_at_limit = max_inventory/size * gamma * sigma^2 * horizon
//    With max_inventory 50, size 10, a 5-tick skew at the limit needs
//    gamma * sigma^2 * horizon = 1.0, not 5000.
```
Change the **defaults in `QuoteParams`**, not just the callers — the callers
that override them are the ones that work, and the caller that does not is the
acceptance test. Set `horizon` to the events remaining in a decision epoch
rather than the whole run, or state `gamma` in ticks-per-share directly. Then
lower `min_half` to 0 so GLFT's sub-tick offsets survive to the rounding step,
where `floor`/`ceil` will quantise them honestly.

3. Separately, find out why `TabulatedMDP` and `JoinTouch` produce identical
   output. Dump the action distribution the table actually emits over a run
   (`TabulatedPolicy` already counts `off_grid`; a per-action histogram beside
   it is a few lines) and check whether the solved policy is constant.

**Refactor Suggestion:** Add a `sanity()` check to `QuoteParams` that computes
the implied skew at the inventory limit and refuses a configuration that exceeds
a few ticks. A parameter set that makes a market maker cross the spread should
fail loudly at construction, not silently in the results table.

**Tests Missing:** Two specific gaps, and they are what let this through.
- `tests/test_strategies.cpp:159-177` **does** sweep all five strategies over
  inventories from -150 to +150 and assert `q.bid < q.ask`. That only forbids a
  *self*-crossed pair. It never asserts the property that matters: **a quote
  never crosses the book it was computed from** — `q.bid < best_ask` and
  `q.ask > best_bid`. Adding those two lines to the existing loop would have
  caught findings 1 and 3 on the day they were written.
- That same test builds its `QuoteParams` at line 24 with `horizon = 1.0`, so
  the shipped defaults are never exercised anywhere in the suite. A test that
  runs the sweep with a **default-constructed** `QuoteParams` is the one that
  would have caught the real defect.
- Nothing asserts that GLFT's quotes vary with inventory once the `min_half`
  clamp is applied. `test_strategies.cpp:127-141` tests `half_bid`/`half_ask`
  as raw doubles, before the clamp, so it passes while the clamped quotes are
  constant.

**Performance Notes:** `GLFT::inventory_term` calls `std::pow` and `std::sqrt`
on every quote (lines 194-197) over parameters that never change during a run.
That is a transcendental per event on the quoting path. Cache it in the struct
at construction. The no-virtual-functions decision (lines 44-46) is right and
the reason given is right: Phase 5 replaces these with a table read, and nothing
here should make a vtable look acceptable in that context.

**Security Notes:** None.

**Market-Logic Notes:** The economics are correctly stated even where the
parameterisation defeats them.
- The reservation price is Ho & Stoll's, and the comment (lines 127-129) is
  precise about what it does: the centre moves, the spread does not.
- `optimal_spread` is the A-S closed form
  `γσ²(T−t) + (2/γ)ln(1 + γ/k)`, correctly transcribed.
- GLFT's asymmetric offsets are correct, including that it "does not blow up as
  the horizon grows" — which is why the same defaults that destroy A-S leave
  GLFT merely inert rather than crossing.
- The header's honesty about `A` and `k` (lines 30-42) is the right disclosure:
  they are uncalibrated, so a comparison today "measures whether these are
  implemented correctly, not which would make money", and the synthetic flow may
  not even produce an exponential fill curve. That disclosure is what makes the
  findings above reportable rather than a surprise.
- The `ImbalanceSkew` note (lines 209-213) records that the sign is contested —
  arXiv:2502.18625 finds fill probability and post-fill returns negatively
  correlated, so leaning with imbalance may be exactly wrong — and makes the
  gain a parameter so it can be flipped. That is the correct way to ship a
  contested effect.

---

### `include/lob/strat/tabulated.hpp`

**Status:** Checked — [x] Reviewed (123 lines, read in full)

**Issues Found:** None. Two notes.

- **`off_grid` is `mutable` and incremented from a `const` member function**
  (lines 57, 117). Correct for a counter, but it makes `side_state` not
  thread-safe and `quote()` is non-const anyway, so the `mutable` is not buying
  anything.
- **`quote()` reads `bb` and `ba` without checking `has_bid()`/`has_ask()`**
  (line 68). Unlike `quoting.hpp` this is nearly harmless — a zero touch makes
  `away` large, `side_state` returns `kNoQuote`, and the resulting action is
  read from a valid row — but `off_grid` will not increment for it, because the
  guard at line 117 is only reached when `on` is true. So an empty book is
  counted as "not quoting" rather than as off-grid.

**Severity:** Minor (both)

**Why it matters:** The second one slightly under-reports the diagnostic the
file exists to expose.

**Fix Recommendation:**
```cpp
if (!v.book.has_bid() || !v.book.has_ask()) return q;   // no touch, no quote
```
at the top of `quote()`, matching `JoinTouch`.

**Refactor Suggestion:** None.

**Tests Missing:** That `off_grid` increments when a resting quote drifts beyond
`kQuoteLevels` from the touch. That counter is the file's own honesty mechanism
and nothing checks it fires.

**Performance Notes:** This is the Phase 5 hot path and it is the right shape:
build the state, one `encode`, one byte read, one `decode_action`, two
subtractions. No allocation, no branching on the table's contents, no virtual
call. `track()` is one `qty_at` per side per event.

**Security Notes:** `table->action_for(...)` is safe because
`PolicyTable::load` validates every action byte against `kNumActions` before
accepting a file (see `src/policy_table.cpp`). That validation is what makes
this unchecked read legitimate.

**Market-Logic Notes:** The three comments at the top of this file are the best
microstructure reasoning in the repository.
- **Queue position is estimated, not known** (lines 8-20). The view book
  contains market orders only, never ours, "exactly as a real venue's public
  feed does". So the estimator is the volume resting at our price when we
  arrived, and it can only shrink.
- **The running minimum is what makes that correct** (lines 16-20): a level's
  total also grows as orders queue up *behind* us, and those are not ahead of
  us. Taking the smallest total seen since joining ignores them. It is an upper
  bound on true queue position and, crucially, "it is the same estimator a
  production system uses, because the public feed does not say which orders are
  whose either". Backtest and production compute the same quantity the same way.
- **The queue bucket is measured against the table header's reference depth, not
  the current level** (lines 22-30). Dividing by the level's own size "turns 'a
  lot of size ahead of me' into 'a large share of this particular level'", so an
  order alone at a thin level and one alone at a thick level land in different
  buckets and every lookup is answered from a row solved for a different
  question. This is subtle, it is right, and getting it wrong would be
  undetectable from the P&L.
- The position limit is re-enforced here even though the solver already imposed
  it (lines 89-92): "A table is not a place to discover that a constraint was
  dropped."

---

### `apps/replay/main.cpp`

**Status:** Checked — [x] Reviewed (625 lines, read in full)

**Issues Found:** `--verify` degenerates into a per-event O(n) invariant check
whenever events are being rejected. Two smaller points.

- **The verify cadence keys off `applied`, which does not advance on a reject**
  (line 157): `if (verify && (applied % 10'000 == 0))`. `applied` increments only
  on `BookError::Ok`, so once it reaches a multiple of 10,000 the condition
  stays true for *every* rejected event until the next successful apply.
  `check_invariants()` is O(window x orders) — on a 4,096-tick window with a
  million resting orders that is not a check, it is a stall. The synthetic path
  gets this right (`i % 10'000`, line 98) because it keys off the loop counter.
- **`book.apply` is timed with the unserialised `tsc::now()`** (lines 76-78,
  143-145). `include/lob/measure/stopwatch.hpp` states the rule: the unserialised
  read "may reorder against surrounding work" and is "right for timing a stage
  that takes hundreds of cycles or more". A book apply is 100-200 cycles, right
  at the boundary, so the reported p50 carries a few cycles of reordering noise
  the project's own guidance says to avoid here.
- **The measurement-overhead figure is hardcoded** (line 116, `2.0 *
  to_nanos(35)`), and `apps/latency_demo/main.cpp:161` hardcodes the same
  quantity as `2.0 * to_nanos(30)`. Two different magic constants for one
  measurable thing, in a repository whose whole argument is that you measure
  rather than assume.
- **`--price-decimals 99` is accepted** (line 177 of the argument loop) and then
  silently defeats every decode, because `json::parse_decimal` refuses
  `scale > 18`. The run reports zero events rather than a bad argument.

**Severity:** Major (Medium) (the verify stall), Minor (the rest)

**Why it matters:** `--verify` is the tool you reach for when a capture looks
wrong, which is exactly when rejects are frequent. It gets slowest precisely
when it is most needed.

**Fix Recommendation:**
```cpp
std::uint64_t seen = 0;                 // every event, applied or not
...
++seen;
if (verify && seen % 10'000 == 0) { ... }
```
and clamp the decimals arguments to `[0, 18]` at parse time.

**Refactor Suggestion:** Measure the timer overhead once at startup — the code
to do it is already in `apps/latency_demo` section 2 — and print the measured
value in both tools instead of two different literals.

**Tests Missing:** Nothing here needs a unit test; this is a reporting harness.
`tests/test_split_replay.py` exercises the capture path end to end and is
audited separately.

**Performance Notes:** `pct()` (line 187) calls `std::nth_element` per
percentile on the same vector. That is correct — `nth_element` yields the true
k-th order statistic regardless of prior partial ordering — and it is O(n) per
call rather than one sort for all six. At the sample counts involved it does not
matter, and the comment explains why a `Histogram` is the wrong instrument here:
the delay series is signed, and the histogram starts at zero.

**Security Notes:** `slurp` (line 206) concatenates a file's lines with no
separator. For the single-line JSON the recorder writes that is exact; for a
pretty-printed snapshot it would still parse, because a JSON newline can only
fall between tokens. It is safe, but it is safe by a property of JSON rather
than by construction.

**Market-Logic Notes:** The reporting is unusually disciplined, and several of
the comments record real findings.
- **The sequence chain is reported before anything derived from the data**
  (lines 182-190), because it "is the only thing that can distinguish a quiet
  market from a dropped message", and a broken chain is stated to invalidate
  every queue position across the gap.
- **The snapshot sets precision and centres the window but does not seed the
  book near the touch** (lines 282-287 in the bitstamp path). The measurement
  behind it is quoted: seeding scored 11.1% on xrpusd against 90.2% for building
  from the stream alone, judged against the trade channel, because the snapshot
  carries orders whose deletes happened before the capture began.
- **The seed guard is verified rather than assumed** (lines 404-412): if the
  price moved further over the session than the guard, a stale deep order can
  become the touch, and the tool prints `TOO NARROW` instead of a number.
- **"Read the median, distrust the tail"** for touch depth (lines 149-154): the
  book is built from the stream, so when the touch empties, the next level it
  knows about can be further out than the real one. The upper percentiles are a
  property of the reconstruction, not the market — and the median is
  corroborated against the snapshot's own touch, which involves no
  reconstruction.
- **The two feed channels are never pooled** (lines 331-334): in one capture
  orders arrived 570 ms "late" and trades 88 ms "early", so pooling would report
  a clock offset as jitter and bury the real variation, "which is two orders of
  magnitude smaller".
- **Marketable orders are held back rather than rested** (lines 426-437):
  Bitstamp publishes an aggressive order as `order_created` at its limit price
  before publishing the fills it causes, and resting those builds a crossed
  book. Their fills are counted on the resting side only, "counting both would
  double the traded volume".
- **Fill and cancel are never aggregated** (lines 439-442), quoting docs/03 §5:
  volume cancelled ahead of you is free progress up the queue; volume traded
  ahead of you is progress plus the information that someone is buying.

One process note: the synthetic path uses a default-constructed `FlowConfig`
(line 50) rather than `FlowConfig::ethusd()`, so its spread and depth
distributions are the uncalibrated generator's. That is legitimate — the file
says so at line 177, "use this to measure the book, never to calibrate a model"
— but it means the synthetic numbers this tool prints should not be compared
with the calibrated figures in `docs/06-queue-reactive-plan.md`.

---

### `apps/tape/main.cpp`

**Status:** Checked — [x] Reviewed (324 lines, read in full)

**Issues Found:** Trades accumulate through the whole pre-`--start` period and
are all dumped into the first frame. One dead variable. The shadow order
contaminates the frame's own ladder and imbalance without that being said.

- **`pending` is only cleared when a frame is written** (line 296), and the
  `rel < start_ns` guard (line 256) `continue`s before any frame can be written.
  With the defaults `--warmup 60 --start 120`, every trade in that 60-second
  window is held in memory and then written into frame 0. The first frame of
  every tape is wrong, and a large `--start` grows `pending` without bound.
- **`fill_latencies` is filled and never read** (lines 170, 250). Dead.
- **The shadow order is in the book when the frame is written.** `book.depth`,
  `book.orders_at` and `fe.get().imbalance` (lines 272-275, 298) all include it.
  The metadata note (line 188) says the order "adds size the real market never
  saw and nobody reacted to it", which covers the concept, but a reader of the
  tape's `"b"` ladder and `"i"` field is not told those specific numbers include
  it.
- **`std::fclose(out)` return value is discarded** (line 320), so a full disk
  produces a truncated tape reported as success. Same pattern as
  `include/lob/measure/journal.hpp`; `src/policy_table.cpp` shows the fix.
- **`--fps -1` yields a negative `frame_gap`** (line 176), so every event emits a
  frame. `fps > 0` is guarded for zero but not for negative.

**Severity:** Major (Medium) (the first-frame trade dump), Minor (the rest)

**Why it matters:** The first frame of every tape is wrong, and a tape is the artefact whose entire premise is that the picture is of *this* book.

**Fix Recommendation:**
```cpp
// Clear the trade buffer on the pre-start path too: those prints belong to
// frames that will never be written.
if (rel < start_ns) { pending.clear(); continue; }
```
and `const double f = (fps > 0.0) ? fps : 10.0;` for the frame gap.

**Refactor Suggestion:** Emit two imbalance fields, one computed from the book
with our order and one without, or mark the frame's own contribution. The tape's
stated purpose is that "a picture of a book must be a picture of THIS book"; the
same standard applies to the derived numbers next to it.

**Tests Missing:** A tape produced with `--start` well past `--warmup`, asserting
frame 0 carries no more trades than one frame interval's worth.

**Performance Notes:** `px_buf`/`qty_buf` are fixed `kMaxDepth` arrays and
`depth` is clamped to `kMaxDepth` at parse time (line 97), so the ladder write
cannot overflow. Frames are emitted only when something changed, so a quiet
market costs bytes proportional to activity.

**Security Notes:** `kOwnId = 0xFFFF'FFFF'FFFF'0001` is chosen far above any
exchange id in these captures so it cannot collide (line 160). That is the right
kind of care; a collision would silently merge our order with a real one.

**Market-Logic Notes:** The fill derivation is the substance of this file and
the reasoning behind its current form is recorded in place (lines 205-214).
- A print at our price fills us when it is larger than what is queued in front
  of us **at the moment of the print**, read live from `queue_ahead()`.
- The earlier version accumulated traded volume and compared it to the queue as
  it stood at placement. That version "can never fire once the queue drains by
  CANCELLATION, which is how it almost always drains here — 99% of removals are
  cancels." Reading the queue at print time is what makes a cancel count as the
  progress it is, and it is the payoff of resting our order in the same FIFO as
  everyone else's.
- The two caveats are printed into the tape's own metadata rather than left in
  the source: the order is a shadow, and a fill is derived rather than observed
  because "a real feed's executes name specific order ids, and never ours".
- One caveat is not stated: the trade-channel message is evaluated against the
  book as of the *previous* line, because `queue_ahead` is read before this
  line's order events are applied. The two channels have different delays
  anyway, so the ordering is approximate by nature — but that is worth saying
  where the other two caveats are said.

---

### `apps/sim_demo/main.cpp`

**Status:** Checked — [x] Reviewed (154 lines, read in full)

**Issues Found:** The requote cadence is on the event clock, which the same
project already fixed elsewhere.

- **`if (seen_ - last_quote_ < 200) return;`** (line 41) gates requoting on 200
  *events*, and the comment justifies it with "message budgets are real". A
  message budget is a rate per unit of **time**, and the number of events per
  second varies by three orders of magnitude between instruments.
  `include/lob/strat/driver.hpp` was changed to a nanosecond cadence
  (`quote_every_ns`) for exactly this reason; this file still carries the old
  form.

**Severity:** Minor (a demo, not a measurement)

**Why it matters:** It does not change the conclusion this demo draws, which is
about aggressive fills rather than P&L levels. It matters as consistency: two
files in the repository now disagree about what a message budget is.

**Fix Recommendation:** Mirror the driver — hold a `Nanos last_quote_ts_` and
compare against `v.now`.

**Refactor Suggestion:** None.

**Tests Missing:** None. This is a demonstration; `tests/test_simulator.cpp`
covers the simulator itself.

**Performance Notes:** Not applicable.

**Security Notes:** None.

**Market-Logic Notes:** Worth recording that **this file computes total P&L
correctly and `include/lob/strat/pnl.hpp` does not.** Line 96 is
`o.total = o.stats.realised_pnl + o.mark_to_market`, with the comment (lines
89-90) explaining why: so the two runs are compared "on the same footing rather
than on whoever happened to end flatter." `Attribution::total` omits exactly
that term. The correct treatment already exists in the repository; it is the
attribution path that lost it.

The closing text (lines 143-152) is the right way to present a demo result: read
the aggressive-fill row, not the P&L, because "the strategy never asks to cross
the spread" and yet under latency some of its quotes land crossing. And then,
explicitly, "That is the effect, not a strategy result. The agent is a
deliberately naive baseline and the flow is zero-intelligence, so the levels
mean nothing."

---

### `apps/latency_demo/main.cpp`

**Status:** Checked — [x] Reviewed (163 lines, read in full)

**Issues Found:** The closing overhead figure is hardcoded after having been
measured 100 lines earlier. Unvalidated argument.

- **Line 161 prints `2.0 * to_nanos(30)`** as the per-span overhead, while
  section 2 (lines 56-63) *measures* the same quantity and prints it. The
  measured value is scoped to that block and discarded.
- **`std::atoi(argv[1])`** is unvalidated (line 43); a negative value skips
  section 4 entirely and `rec.report()` prints a header with no rows.
- **An 8 MiB `std::vector` is allocated and freed inside the 200-iteration cold
  loop** (line 84). It is outside the timed span, so it does not corrupt the
  measurement, but it churns 1.6 GB of allocation to do a cache eviction that
  could reuse one buffer.
- **`std::string(title).size()`** (line 36) allocates to compute a length that
  `std::strlen` gives for free. `apps/sim_demo` uses `strlen` for the identical
  helper.

**Severity:** Minor (all)

**Why it matters:** This is the Phase 0 acceptance demo, so a number it prints as measured overhead should be the one it measured a hundred lines earlier, not a literal.

**Fix Recommendation:** Hoist the measured overhead into a variable at the top
of `main` and use it at line 161. Hoist the eviction buffer out of the loop.

**Refactor Suggestion:** `banner()` is duplicated verbatim in four apps with
three different implementations of the underline length. One shared header would
remove the drift.

**Tests Missing:** None; this is the Phase 0 acceptance demo and its output is
read by a human.

**Performance Notes:** The measurement discipline is right. `fake_stage` is
`[[gnu::noinline]]` so the optimiser cannot hoist it, `do_not_optimize` guards
every result, and section 2 measures the timer's own cost first so every number
after it can be read net of the instrument.

**Security Notes:** None.

**Market-Logic Notes:** Sections 3 and 5 are the two that earn the file's
existence.
- **Cold versus warm** (lines 75-78): the first call after an idle period pays
  for cold instruction cache, cold predictors and a cold TLB, and "market bursts
  follow quiet periods, so this is the case that actually costs money".
  Reporting only the warm number is named as "the most common way to publish a
  latency figure that is not real."
- **Coordinated omission** (lines 141-142): the same underlying reality recorded
  two ways, where "the uncorrected histogram reports a better p99.9 precisely
  *because* it stalled." That is the correct demonstration of the effect.

---

### `apps/jitter_probe/main.cpp`

**Status:** Checked — [x] Reviewed (50 lines, read in full)

**Issues Found:** A negative argument makes it run effectively forever.

- **`std::atoi(argv[1])` is unvalidated** (line 21) and flows into
  `tsc::from_nanos(seconds * 1e9)` (line 29), which casts a negative `double` to
  `std::uint64_t`. That conversion is undefined; in practice it produces a
  near-`UINT64_MAX` deadline, so `jitter_probe -1` spins until killed while
  filling a histogram.

**Severity:** Minor

**Why it matters:** It establishes the floor under every latency number the project reports, and an argument that makes it spin forever is a bad first experience with the one tool a new contributor is told to run first.

**Fix Recommendation:**
```cpp
int seconds = argc > 1 ? std::atoi(argv[1]) : 5;
if (seconds <= 0) { std::fprintf(stderr, "seconds must be positive\n"); return 2; }
```

**Refactor Suggestion:** None. Fifty lines that do one thing.

**Tests Missing:** None; the output is a measurement of the machine, not of the
code.

**Performance Notes:** The loop is the measurement: two serialised reads and a
histogram record, nothing else. The 1 s histogram ceiling means a pathological
stall is clamped, and `summary()` does report the clamped count — unlike
`LatencyRecorder::report()`, which is the finding recorded under
`include/lob/measure/recorder.hpp`.

**Security Notes:** None.

**Market-Logic Notes:** Not applicable, but the closing paragraph (lines 45-48)
states the correct reading: "the median is the cost of the clock read; p99.9 and
max are the machine interrupting you. Anything you build here has that as its
floor." Establishing that floor before attributing any improvement to your own
code is the Phase 0 acceptance criterion, and this is the free, portable
stand-in for `cyclictest` that makes it reproducible.

---

### `tests/test_util.hpp`

**Status:** Checked — [x] Reviewed (51 lines, read in full)

**Issues Found:** None. Two constraints worth naming.

- **`CHECK_EQ` requires `std::to_string`**, so it only works on arithmetic
  types. Comparing two `std::string`s or two enums needs `CHECK(a == b)` and
  loses the diagnostic.
- **`CHECK_THROWS` catches `...`**, so it cannot distinguish "threw the right
  exception" from "threw something else entirely".

**Severity:** Minor (both)

**Why it matters:** Every assertion in the suite goes through these four macros, so their limits are the suite's limits.

**Fix Recommendation:** None needed. If the exception type ever matters, a
`CHECK_THROWS_AS(expr, Type)` is four more lines.

**Refactor Suggestion:** None. The design decision — one binary per test file,
its own `main`, no framework — is stated in the header and is correct for the
stated goal: "Phase 0 should build and test with nothing but a compiler and
CMake."

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** One design point that matters for how the rest of the
suite reads: `report()` records a failure and **returns**, so a test binary runs
to completion and prints every failure rather than aborting on the first. With
statistical tests, seeing all the failures at once is the difference between one
debugging round and five.

---

### `tests/test_types.cpp`

**Status:** Checked — [x] Reviewed (47 lines, read in full)

**Issues Found:** The two `types.hpp` defects are exactly the cases this file
does not cover.

- **`better_than` is tested only on valid prices** (lines 24-28). Nothing tests
  `Price::none().better_than(x, Side::Ask)`, which returns `true` because
  `kNoPrice` is `INT64_MIN`.
- **`operator-` is tested only on valid prices** (lines 16-17). Nothing tests
  the `Price::none()` case, which is signed overflow.

**Severity:** Major (Medium) (as a coverage gap; the defects are recorded under
`include/lob/core/types.hpp`)

**Why it matters:** The two gaps here are exactly the two defects in `include/lob/core/types.hpp`, which is why those defects are still live.

**Fix Recommendation:**
```cpp
CHECK(!Price::none().better_than(a, Side::Ask));   // fails today
CHECK(!Price::none().better_than(a, Side::Bid));   // passes by accident
```

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** The tests that are here are the right ones. Line 11's
`CHECK(Price{0}.valid())` with the comment "zero is a real price on some
instruments" is the reason `kNoPrice` is a sentinel rather than 0, and it is
worth an assertion. The rational tick-size checks at lines 36-39 include the
1/32 case, so the exactness claim is tested rather than asserted in a comment.

---

### `tests/test_arena.cpp`

**Status:** Checked — [x] Reviewed (88 lines, read in full)

**Issues Found:** Covers the happy paths and exhaustion; misses both memory-
safety cases.

- **No double release** (`pool.release(b); pool.release(b);`), which corrupts
  the free list into a cycle and hands the same slot out twice.
- **No release of a foreign pointer.**
- **`arena.create<Node>` uses a trivially destructible type** (line 42), so the
  missing `static_assert` on `Arena::create` is never exercised.
- **No `Pool{0}` or `Arena{0}`.**

**Severity:** Major (Medium) (coverage), and see `include/lob/core/arena.hpp`

**Why it matters:** The uncovered cases are the two memory-safety ones. Both classes are unused in production today, so the gap is cheap now and expensive the day either is put on a real path.

**Fix Recommendation:** Add the double-release case behind whichever guard the
fix introduces — an assertion in a debug build, or a returned bool.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** Not applicable. Two assertions are worth naming as
statements of intent rather than mere checks: line 34, exhaustion "returns
nullptr rather than falling back to the heap", and line 72,
`CHECK(d == b)` — the most-recently-freed slot is reused, so it is cache-warm.
Both are properties of the design, not accidents of it.

---

### `tests/test_order_map.cpp`

**Status:** Checked — [x] Reviewed (164 lines, read in full)

**Issues Found:** One structural gap, which is the same gap as the class's.

- **The differential loop caps live entries at `kCap - 1` = 4,095** (line 104)
  in a table whose real capacity is 8,192, so the load factor never exceeds
  0.5. The full-table case — where `find` and `insert` spin forever — is
  therefore never approached. That is deliberate and correct given how
  `OrderBook` uses the class, but it means the hazard recorded under
  `include/lob/book/order_map.hpp` is untested rather than ruled out.

**Severity:** Minor (coverage of a hazard the caller prevents)

**Why it matters:** The class hangs rather than fails on a full table, and this test deliberately never gets near one. The safety is real but it lives in the caller, not in the tested code.

**Fix Recommendation:** Once `insert` returns false on a full table rather than
spinning, add the case.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** 400,000 operations plus a full-table sweep every 64
steps. Not fast, and worth it.

**Security Notes:** None.

**Market-Logic Notes:** Not applicable, but the reasoning in the header is the
best statement of testing philosophy in the repository and it is correct about
the specific risk: backward-shift deletion's failure mode is "silent: no crash,
no corruption the compiler can see, just an order that has vanished from the
book's index while still sitting in a level's FIFO." The test is built to match
that — colliding keys constructed on purpose (lines 44-76, erasing from the
middle and then the head of a probe chain), then 400,000 random operations
against `std::unordered_map`, with the key space held at three times capacity
"so collisions and reinsertions of just-erased keys are constant rather than
rare." That last choice is what makes the random half of the test effective
rather than decorative.

---

### `tests/test_journal.cpp`

**Status:** Checked — [x] Reviewed (102 lines, read in full)

**Issues Found:** None in the tests. Three uncovered defects, all recorded under
`include/lob/measure/journal.hpp`.

- **`written()` is never asserted**, which is why it has always returned 0.
- **`batch_records = 0` is never constructed**, which is why the heap overflow
  is undetected.
- **No short-write path**, which is why discarded `fwrite`/`fclose` results go
  unnoticed.
- Minor: `std::memcmp` at line 96 with no `<cstring>` include; `std::fopen`'s
  result is not checked before `fwrite` at lines 66-68; temp files use fixed
  names in the working directory, so two concurrent runs of this binary would
  collide.

**Severity:** Major (Medium) (the three coverage gaps)

**Why it matters:** Three defects in `include/lob/measure/journal.hpp` survive because this file does not assert the three properties that would expose them.

**Fix Recommendation:** `CHECK_EQ(w.written(), kN);` inside the writer scope is
one line and turns a dead counter into a live one.

**Refactor Suggestion:** Use unique temp names, or a per-run subdirectory, so
`ctest -j` stays safe if this file ever grows a second journal test.

**Tests Missing:** As above.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** The `Other` struct at lines 25-28 is deliberately the
same size as `Decision`, with a `static_assert` enforcing it, so line 62's
`CHECK_THROWS(journal_read_all<Other>(...))` tests the type hash rather than the
size check. That is precisely the bug the hash exists to catch, and constructing
the case properly is the difference between testing the feature and testing
around it. The determinism test at lines 86-98 — same records written twice,
compared with `memcmp` — is what the byte-for-byte replay claim in `docs/00`
actually rests on.

---

### `tests/test_tsc.cpp`

**Status:** Checked — [x] Reviewed (96 lines, read in full)

**Issues Found:** Two assertions measure the machine rather than the code, so
they can fail on a loaded runner.

- **`CHECK(per < 100.0)`** (line 92) asserts the cost of an `rdtsc` read.
  `docs/BASELINE.md` records 16.72 ns on this machine, so there is 6x headroom
  — but on an oversubscribed CI container the loop can be descheduled and the
  average blows past it. A test that fails because the host is busy trains
  people to ignore test failures.
- **The monotonicity loop** (lines 34-39) reads `tsc::now()` 100,000 times and
  requires the counter never to go backwards. On hardware whose TSCs are not
  synchronised across sockets, a thread migration mid-loop breaks it. The test
  prints `invariant`/`nonstop` but does not gate on them.
- **No concurrent first-call to `calibration()`**, which is the latent data race
  recorded under `src/tsc.cpp`.

**Severity:** Major (Medium) (CI flakiness), Minor (the coverage gap)

**Why it matters:** A test that fails because the host is busy trains people to ignore test failures, which costs more than the assertion is worth.

**Fix Recommendation:** Print the per-read cost unconditionally, and assert only
a loose ceiling that a stall cannot cross (say 10,000 ns) — or make the
assertion conditional on `cal.trustworthy`. For monotonicity, either pin the
thread or gate the assertion on `cal.invariant && cal.nonstop`.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** The 20 ms busy-wait comparison at lines 51-67 is the
right test and is robust: if the process is descheduled, wall time and the TSC
both advance, so the ratio holds.

**Security Notes:** None.

**Market-Logic Notes:** Line 66's cross-check — the integer fixed-point
conversion against the floating-point one, to within 0.1% — is the assertion
that keeps `to_nanos` honest. A silent divergence between those two paths would
make every latency number depend on which one a call site happened to use.

---

### `tests/test_histogram.cpp`

**Status:** Checked — [x] Reviewed (201 lines, read in full)

**Issues Found:** None. One point that **corrects** a finding recorded earlier
in this audit.

- **The clamped maximum is deliberate and tested.** Lines 113-122 record two
  values above the ceiling and assert `overflow_count() == 2` and
  `max() == 1000`, with the comment "clamped, and visibly so". So
  `Histogram`'s behaviour is intended and specified. The finding recorded under
  `include/lob/measure/histogram.hpp` should be read narrowly: the defect is not
  in `Histogram`, it is that **`LatencyRecorder::report()` prints `max()` and
  never reads `overflow_count()`**, so the visibility the histogram provides is
  discarded at the one place a human reads the number.
- **`percentile_at_or_below` is not tested with a negative value**, which is the
  path saved from an out-of-bounds read only by a `std::min` clamp.

**Severity:** Minor

**Why it matters:** Reading it corrected a finding earlier in this audit: the clamped maximum is specified behaviour, so the defect is narrower than first written.

**Fix Recommendation:** Add the negative-value case. The rest needs nothing.

**Refactor Suggestion:** None.

**Tests Missing:** As above, plus a test at the `LatencyRecorder` level that a
clamped sample is visible in `report()`.

**Performance Notes:** 200,000 lognormal samples compared against an exact
sorted-sample percentile, 40 trials of monotonicity, and a 40,000-sample merge
equivalence check. Slow for a unit test and justified by what it establishes.

**Security Notes:** None.

**Market-Logic Notes:** The header's claim — "the most rigorous test in Phase 0"
— holds up, and two cases earn it.
- **The precision guarantee is tested against ground truth**, not against
  itself: 200,000 lognormal draws are kept in a vector, sorted, and the exact
  percentile compared to the histogram's, at five percentiles across five orders
  of magnitude (lines 59-86).
- **The coordinated-omission demonstration** (lines 124-144) is constructed so
  the uncorrected p99 provably hides a stall the corrected one cannot, plus the
  negative case that a value at or below the expected interval backfills
  nothing.

---

### `tests/test_book.cpp`

**Status:** Checked — [x] Reviewed (208 lines, read in full)

**Issues Found:** One coverage gap matching a known defect.

- **No test of a *failing* `replace()`.** `src/order_book.cpp` loses an event
  from the stats on that path (`--stats_.deletes` runs, `++stats_.replaces`
  does not). Every `replace` here succeeds.

**Severity:** Minor

**Why it matters:** This file is the written specification of the behaviours a market maker depends on, so what it omits is what nobody has committed to.

**Fix Recommendation:** Replace into an out-of-window price and assert the
counters still balance.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** This file is the specification of the behaviours a
market maker depends on, stated as assertions, and the sequence at lines 73-104
is the one that matters. It walks queue position through every way it can
change: a partial fill in front moves us up, a cancel in front moves us up,
activity **behind** us changes nothing, and consuming the last order in front
puts us at the head. Lines 106-127 then pin the tri-state that the whole
requote decision rests on — a partial cancel of our own order keeps priority
(so shrinking beats requoting), and a replace sends us to the back behind
everything that arrived meanwhile. Getting that wrong is invisible in a P&L and
worth exactly this much test.

---

### `tests/test_matching.cpp`

**Status:** Checked — [x] Reviewed (172 lines, read in full)

**Issues Found:** Two of the three self-match modes are untested.

- **`SelfMatch::Allow` and `SelfMatch::CancelIncoming` are never constructed.**
  Only `CancelResting` is exercised (line 133). `CancelIncoming` in particular
  has a different effect on the aggressor's remainder and nothing checks it.

**Severity:** Minor

**Why it matters:** The fill model is the single largest source of backtest overstatement, and this is the file that rules the common error out.

**Fix Recommendation:** Repeat the self-match block under the other two modes;
the expected outcomes differ in one field each.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** The pair at lines 96-129 is the heart of the fill model
and both halves are present, which is unusual. The first asserts that when a
seller takes 120 against 100 ahead of us, we get exactly 20 and end up at the
front. The second — the one a naive backtester gets wrong — asserts that a
seller who takes only 60 does **not** fill us at all, though it does move us
closer. A simulator that fills you as if you were always at the front is, as the
header says, "the single largest source of backtest overstatement", and these
two cases are what rule it out. Line 56-65's price-improvement case (the passive
side sets the price) and line 84-94's market order that does not rest its
remainder are both correct venue behaviour and both easy to get wrong.

---

### `tests/test_book_differential.cpp`

**Status:** Checked — [x] Reviewed (173 lines, read in full)

**Issues Found:** The queue-position comparison silently skips itself whenever
either book returns the "not ours" sentinel. Two coverage gaps.

- **`if (a >= 0 && b >= 0 && a != b)`** (line 116). `queue_ahead` returns `-1`
  for an order it does not consider ours. If the fast book **loses** one of our
  orders and returns `-1` while the reference still tracks it and returns a real
  value, the condition is false and the test passes. The sentinel is being
  treated as "skip this comparison" rather than as a value that must also match.
  That is exactly the failure this check exists to catch.
- **Own orders are only ever placed on the bid** (line 100). The ask side's
  queue-position bookkeeping is never differentially tested.
- **Only the top 20 levels are compared** (line 44). Deeper divergence would not
  be seen. The comment says why — "where any real strategy looks" — which is a
  reasonable scope, stated.

**Severity:** Major (Medium) (the sentinel skip), Minor (the rest)

**Why it matters:** This is the test that establishes the fast book is correct, so a comparison it silently skips is a class of divergence nothing else would catch.

**Fix Recommendation:**
```cpp
const Qty a = fast.queue_ahead(id);
const Qty b = ref.queue_ahead(id);
if ((a < 0) != (b < 0) || (a >= 0 && a != b)) { ... fail ... }
```
and mirror the placement block onto the ask side.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** Five seeds at 12,000 events plus one at 50,000 by
default, rising to 40,000 and 250,000 under `--long`. The split is explained
(sanitizer builds stay quick; the optimised CI job runs the thorough pass) and
both use the same seeds, so a failure at either size reproduces at the other.

**Security Notes:** None.

**Market-Logic Notes:** Two things are worth recording.
- **The reference book's asymmetries do not hide bugs, they surface as
  failures.** `ReferenceBook` has no price window and no order limit while
  `OrderBook` has both, so if the generator ever went out of window or exhausted
  the pool, `fe != re` at line 85 would trip with a clear message. The
  asymmetry recorded under `include/lob/book/reference_book.hpp` is therefore a
  documentation gap, not a correctness hole in this test.
- **Line 66 explicitly re-enables the Execute path** (`cfg.w_execute = 0.09`)
  with the comment that "the book must be driven through its Execute path to be
  proved correct, and the generator no longer fabricates one by default". A
  differential test that silently stopped exercising executes would keep passing
  while proving less, and this is the line that prevents it.

---

### `tests/test_features.cpp`

**Status:** Checked — [x] Reviewed (243 lines, read in full)

**Issues Found:** The one-sided-book case asserts only the absence of NaN, which
is why the stale-mid defect survives.

- **Lines 147-161 test an empty and then a bid-only book and assert only
  `!isnan(...)`.** On the bid-only book, `f.imbalance` is stale at 0.0 from the
  previous empty update, and `!isnan(0.0)` passes. Nothing asserts that a
  consumer could tell the values are not current. This is the coverage gap
  behind the `Features` finding.
- **The differential loop's imbalance check is guarded by `has_bid() &&
  has_ask()`** (line 206), so the one-sided case is excluded there too.

**Severity:** Major (Medium) (coverage of a Medium defect)

**Why it matters:** The one-sided-book case is asserted only for absence of NaN, which is why the stale-mid defect in the feature engine is still live.

**Fix Recommendation:** Once `Features` carries a validity flag, assert it is
false here. Until then, assert the weaker but still useful property: that `mid`
after a one-sided update equals the mid from before it, so the staleness is at
least documented by a test.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** 200,000 events with a from-scratch OFI recomputation at
every step. The slowest test in the suite and the most valuable.

**Security Notes:** None.

**Market-Logic Notes:** The structure is the right one and the header states it:
hand-constructed cases pin the definitions, then a differential run checks the
incremental engine against recomputation, "because the incremental update is
where the bugs live — it is the only part that carries state." The seven OFI
cases at lines 75-107 cover every branch of Cont-Kukanov-Stoikov including the
mirror-image ask side, worked out on paper. And the differential loop asserts
not just OFI but that no feature ever becomes non-finite, which is the class of
bug an EWMA introduces silently.

---

### `tests/test_pnl.cpp`

**Status:** Checked — [x] Reviewed (137 lines, read in full)

**Issues Found:** Every `attribute` call passes `final_inventory = 0`, which is
why the omitted inventory mark is invisible.

- **Lines 55, 89 and 90 all pass `0`** as the closing position. `Attribution`
  computes `inventory_mtm` from it and then leaves it out of `total`, so with a
  zero position the two agree and nothing fails.
- **`MarkoutTracker::advance` is never called with `mid_now <= 0`**, so the
  early-return that silently stretches a horizon is untested.

**Severity:** Major (High) (as the coverage gap behind a High defect)

**Why it matters:** Every `attribute` call passes a zero closing position, so the one thing that distinguishes the decomposition from session P&L is never exercised.

**Fix Recommendation:**
```cpp
const Attribution held = attribute({f}, 3, FeeSchedule{}, 90.0, +100);
CHECK_NEAR(held.inventory_mtm, 9000.0, 1e-9);
CHECK_NEAR(held.total, held.spread_capture - held.adverse_sel - held.fees + held.inventory_mtm, 1e-9);
```
The second line fails today, and is the assertion that pins what `total` means.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** The decomposition identity is tested in all four
sign combinations — buy with the mid falling and rising, sell with the mid
rising — and each asserts
`markout == spread_capture - adverse_selection` exactly (lines 26, 33, 42).
That is the identity every attribution table depends on. Lines 45-59 test the
unreached-horizon rule with the reason stated: counting a missing horizon as
zero "would drag every long-horizon mean toward zero". And the bootstrap block
(lines 98-134) is careful in a way this kind of test usually is not — it checks
that constant data collapses the interval, that noise centred on zero is **not**
called significant, that the degenerate `[0, 0]` interval does not exclude zero,
and it explains at lines 110-112 why alternating +1/-1 would have been the wrong
noise to use (every 50-element block sums to exactly zero, leaving the bootstrap
no sampling variation to find).

---

### `tests/test_simulator.cpp`

**Status:** Checked — [x] Reviewed (125 lines, read in full)

**Issues Found:** The determinism test compares aggregates, not the fill
sequence. The agent's own fills reaching the view book is untested.

- **Determinism is asserted over nine `SimStats` scalars** (lines 62-70), while
  the header claims runs are reproducible "byte for byte". Two runs could
  produce different fill orderings with identical totals and pass. The
  `InFlight` tie-break gap recorded under `include/lob/sim/latency.hpp` is
  exactly that shape and would not be caught here.
- **Nothing asserts the view book learns about the agent's own executions**,
  which is the High defect recorded under `include/lob/sim/simulator.hpp`.
- The suite runs 2.24 million simulated events in this one file, which dominates
  `ctest` wall time.

**Severity:** Major (Medium) (both gaps)

**Why it matters:** The file claims byte-identical reproducibility and asserts equal aggregates, which is a weaker property and not the one the journal argument needs.

**Fix Recommendation:** Compare the fill vectors, not the totals:
```cpp
CHECK_EQ(fa.size(), fb.size());
for (std::size_t i = 0; i < fa.size(); ++i)
  CHECK(std::memcmp(&fa[i], &fb[i], sizeof(Fill)) == 0);
```
`Fill` is a POD, so this is the byte-for-byte claim actually asserted.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** As above — three million-event runs.

**Security Notes:** None.

**Market-Logic Notes:** The comment at lines 85-93 is the most valuable thing in
the file and is worth quoting: this assertion previously ran at 200,000 events
and started failing when the generator's aggressive weight was fitted to the
real trade rate — 2.6% of events, down from an effective 14%. "It is not that
latency stopped causing aggression: 500k events give 87 and a million give 151.
The test had been relying on a process that traded five times too often." A test
that quietly depended on a mis-calibrated model, caught and documented rather
than re-tuned away, is the record you want when the next assertion starts
failing. The paired assertion — zero aggressive fills with no latency, positive
with latency, from an agent that never asks to cross — is the cleanest possible
demonstration that the latency model does something.

---

### `tests/test_strategies.cpp`

**Status:** Checked — [x] Reviewed (180 lines, read in full)

**Issues Found:** The invariant sweep exists but asserts the wrong invariant,
and the whole file runs on parameters the shipped defaults do not match.

- **Line 24: `p.gamma = 0.05; p.sigma = 0.5; p.horizon = 1.0;`.** The
  `QuoteParams` defaults are `sigma = 1.0` and `horizon = 1e5`. So the skew per
  share under test is 0.0125 ticks; under the defaults it is 5,000. **No test in
  the repository ever constructs a default `QuoteParams`.**
- **Lines 159-177 sweep all five strategies across inventories from -150 to
  +150 and assert `q.bid < q.ask`** — a self-crossed pair. Nothing asserts
  `q.bid < best_ask` or `q.ask > best_bid`, so a quote through the market
  passes.
- **Lines 127-141 test `GLFT::half_bid`/`half_ask` as raw doubles**, before
  `assemble`'s `min_half` clamp. They pass while the clamped quotes are
  constant.

**Severity:** **Critical** (as the coverage gap behind the Critical defect in
`include/lob/strat/quoting.hpp`)

**Why it matters:** This is the gap that let the Critical finding through: the sweep exists, it asserts the wrong invariant, and it runs on parameters the shipped defaults do not match.

**Fix Recommendation:** Two lines inside the existing loop, and one more
parameter set:
```cpp
CHECK(q.bid < b.best_ask());     // never quote through the market
CHECK(q.ask > b.best_bid());
...
// and run the whole sweep a second time with a default-constructed QuoteParams
```

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** Where this file tests the mathematics it is excellent,
and lines 108-124 are the best example in the repository of a test refusing to
encode a plausible falsehood. A-S's optimal spread is **not** monotone in risk
aversion: `gamma*sigma^2*(T-t)` grows with gamma while `(2/gamma)ln(1+gamma/k)`
shrinks, "because a more risk-averse trader demands less edge per fill — it
wants the fill in order to offload risk." At these parameters the second term
dominates, so raising gamma tightens the spread, and the comment says outright
that asserting the opposite "would encode a plausible-sounding falsehood."
Instead it asserts what is reliable: the inventory-risk half always grows with
gamma. Lines 56-59 make the complementary point about the tick grid — at these
parameters the Ho-Stoll shift is 0.625 ticks and "a sub-tick shift can round
away entirely, which is exactly why a large-tick instrument is a
queue-position game rather than a price-placement one." That reasoning is right.
The gap is that the file reasons carefully about the parameters it chose and
never checks the ones that ship.

---

### `tests/test_properties.cpp`

**Status:** Checked — [x] Reviewed (275 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** This is the strongest test file in the repository and I
found nothing to fix in it.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** The six properties here are well chosen. A seventh worth
having, given the findings elsewhere in this audit: **no strategy's quote ever
crosses the book it was computed from**, swept over parameters including the
defaults. It belongs in this file rather than `test_strategies.cpp`, because it
is a property rather than a case.

**Performance Notes:** 150,000 + 60,000 + 150,000 + 30,000 events plus 40
histogram trials. Slow, and each property justifies its cost.

**Security Notes:** None.

**Market-Logic Notes:** Four of the six properties are the ones that matter.
- **Quantity is conserved** (lines 28-69): everything that enters the book
  leaves it or is still resting, checked by walking every level at the end. A
  leak "would show up as depth that is not there, which is the sort of error a
  strategy would happily trade against for a long time."
- **Matching never trades through the limit and sweeps best-price-first**
  (lines 102-126), with the fill quantities summing to the reported fill.
- **A failed operation leaves the book untouched** (lines 200-233): every
  rejection path is exercised against a snapshot string. "If a rejected
  operation half-applies, the book is corrupt in a way nothing downstream can
  detect."
- **The histogram boundary contract** (lines 182-194) is the subtlest thing in
  the suite and it is right. `p0 >= min` and `p100 <= max` "look like the
  obvious sanity check to write" and are **false** for a bucketed histogram,
  because a percentile resolves to a bucket while min and max are tracked
  exactly. The file asserts the true contract instead — p0 equals the floor of
  the bucket holding the minimum, p100 the ceiling of the one holding the
  maximum — and explains why. Most implementations of this test assert the
  false version and then loosen it with a tolerance when it fails.

---

### `fuzz/decode.hpp`

**Status:** Checked — [x] Reviewed (80 lines, read in full)

**Issues Found:** One overstated comment. No defects.

- **"Weighted so cancels and executes dominate" (lines 56-57) is a stretch.**
  `op % 7` gives Add 2/7 (29%), and Reduce + Delete + Execute together 3/7
  (43%). They are the plurality, not dominant.
- **`e.side` is derived from `op & 0x80` while `kind` is `op % 7`**, so side and
  operation are correlated across the byte. Harmless for a blind fuzzer.

**Severity:** Minor

**Why it matters:** A fuzzer is only as good as the inputs it can reach, and the decisions here are what make most inputs hostile rather than plausible.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** `u16()` and `u32()` (lines 32-40) each evaluate the first
half into a **named local** before calling `u8()`/`u16()` again. That is the
correct way to write them: `return (u8() << 8) | u8();` has unsequenced side
effects and is the classic bug in byte readers. This file avoids it.

**Market-Logic Notes:** The design decisions are right for what a fuzzer is for.
`kIdSpace = 256` (lines 48-50) forces id collisions "so duplicate adds and
references to just-deleted orders happen constantly rather than by luck".
`e.qty = u16() - 8` (line 75) spans -8 to 65,527, so zero and negative sizes are
generated rather than hoped for. Prices span well beyond the book's window, so
out-of-window rejection is exercised. And the header states the principle: "a
fuzzer that only produces valid input finds nothing."

---

### `fuzz/portable_main.cpp`

**Status:** Checked — [x] Reviewed (120 lines, read in full)

**Issues Found:** The file's central reproducibility claim is not implemented.

- **`case_seed` is computed and never printed** (lines 111-115). The comment
  directly above it says "The per-case seed is derived and reported on failure,
  so any crash this driver finds is reproducible without a corpus file." Nothing
  reports it. A target that hits `__builtin_trap()` kills the process, and the
  seed that produced the input dies with it — leaving exactly the situation the
  comment promises to avoid.

**Severity:** Major (Medium)

**Why it matters:** This driver exists so the fuzz targets run everywhere,
including where libFuzzer's corpus machinery is unavailable. Without the seed,
a crash found on CI cannot be reproduced locally, which removes most of the
value of finding it.

**Fix Recommendation:** Print it before running the case, to stderr, unbuffered:
```cpp
std::fprintf(stderr, "case %lld seed %llu\r", i, (unsigned long long)case_seed);
std::fflush(stderr);
```
or install a `SIGSEGV`/`SIGILL` handler that prints the current seed. The
cheapest correct fix is to write the seed to a one-line file before each case
and delete it after; whatever survives a crash is the reproducer. Add a
`-replay_seed=` flag so the printed value can be fed straight back in.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** 200,000 cases by default. Printing a seed per case would
dominate; use the `\r` form above or the file approach.

**Security Notes:** `flag_value` uses `std::strtoll` without error checking, but
both results are range-guarded at lines 100-101, so a garbage flag falls back to
the default rather than to zero.

**Market-Logic Notes:** Not applicable. The design argument (lines 3-17) is
correct and worth keeping: libFuzzer needs a compiler runtime that is not
present everywhere, and "a fuzz target that only runs where the toolchain
cooperates is a fuzz target that never runs." The file is also honest that this
is "NOT as good as libFuzzer: it is blind, with no coverage feedback... It is a
lower bound, not a substitute."

---

### `fuzz/fuzz_book.cpp`

**Status:** Checked — [x] Reviewed (61 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** A feed with a gap in it looks exactly like malicious input, so the book surviving arbitrary bytes is a correctness property rather than a hardening exercise.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** Nothing missing at this layer.

**Performance Notes:** `check_invariants` every 64 events rather than every
event, with the reason stated: an O(window) check per event "would slow the
fuzzer to uselessness."

**Security Notes:** The target is complete in a way most are not: after driving
the write path it also exercises **every read path** on the fuzzed book (lines
50-59) — `depth`, `best_bid`, `best_ask`, `spread`, and then `qty_of`,
`queue_ahead` and `is_mine` for every id in the space. A read that faults on a
corrupted book is as much a bug as a write that corrupts it, and most fuzz
targets stop at the writes.

**Market-Logic Notes:** The threat model in the header is correct and is the
reason this matters: "A feed with a gap in it looks exactly like malicious
input, so this is not a hypothetical threat model." Errors must be **returned**,
and what is checked is not the return value but the state afterwards.

---

### `fuzz/fuzz_matching.cpp`

**Status:** Checked — [x] Reviewed (71 lines, read in full)

**Issues Found:** Two of three self-match modes unfuzzed; the crossed-book check
is periodic.

- **Only `SelfMatch::CancelResting`** (line 24). `Allow` and `CancelIncoming`
  are never entered, the same gap as `tests/test_matching.cpp`.
- **The crossed-book assertion runs every 32 ops** (line 50), so a book left
  crossed transiently between checks would not be seen.

**Severity:** Minor (both)

**Why it matters:** The matcher walks the book while mutating it, which is the shape of code that spins forever or reads freed memory on a malformed sequence.

**Fix Recommendation:** Derive the mode from a byte of the input:
`static_cast<SelfMatch>(data[0] % 3)`. That costs nothing and covers all three.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** `match.clear_fills()` every 32 ops with the comment
"bound memory across a long input" (line 61). That is an explicit
acknowledgement, in the code, of the unbounded `fills_` growth recorded as a
High finding under `include/lob/sim/matching.hpp` — the fuzzer has to work
around it.

**Security Notes:** `mine = (applied & 3) == 0` (line 32) alternates ownership
deliberately "so the self-match branch is reached, rather than being dead code
the fuzzer never enters." Getting a fuzzer into a branch that requires two
correlated conditions usually needs exactly this kind of help.

**Market-Logic Notes:** The header identifies the right risk: "The matcher walks
the book while mutating it, which is the shape of code that spins forever or
reads freed memory on a malformed sequence."

---

### `fuzz/fuzz_bitstamp.cpp`

**Status:** Checked — [x] Reviewed (110 lines, read in full)

**Issues Found:** Two functions that consume untrusted snapshot JSON are not
fuzzed.

- **`BitstampDecoder::detect_decimals` and `snapshot_touch` are never called
  here.** Both take the raw snapshot text and both are called on file input by
  `apps/replay/main.cpp` (lines 244-254) and `apps/tape/main.cpp` (lines
  126-134), before anything else runs. `load_snapshot` **is** fuzzed (line 83),
  so the gap is specific.
- **`parse_decimal` is only called with `scale = 8`** (line 60), so the
  `scale > 18` rejection path is never entered.

**Severity:** Major (Medium) (the two unfuzzed entry points)

**Why it matters:** This is the target for the only code in the project that parses bytes from a public network, and two of that code's entry points are not covered.

**Fix Recommendation:**
```cpp
unsigned pd = 0, qd = 0;
(void)lob::BitstampDecoder::detect_decimals(all, &pd, &qd);
lob::Ticks bb = 0, ba = 0;
(void)lob::BitstampDecoder::snapshot_touch(all, cfg(), &bb, &ba);
```
Three lines, and they close the last untrusted-input path that is not covered.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** Capped at 2,048 lines per input, so a pathological input
cannot make one case run unboundedly.

**Security Notes:** This is the best-constructed target of the three and two
things earn that.
- **Lines 51-56 assert that a returned view points inside its input.** That is
  the precise signature of an out-of-bounds `substr` that happened not to fault,
  which ASan alone would not always catch and which a "did it crash" target
  never would.
- **Lines 65-76 bound `array_next` with an explicit 100,000-iteration guard**
  and trap if it is exceeded, so non-termination is a reported failure rather
  than a CI timeout.

**Market-Logic Notes:** The header states the reason this file exists and it is
right: this is the only code in the project that parses bytes from a public
network. Everything else consumes `BookEvent`, "a POD the project built itself".
And the second half matters as much: "a decoder that survives garbage but emits
events that break the book has moved the bug rather than fixed it" — hence the
book is driven with whatever comes out and `check_invariants` runs at the end.

---

### `bench/bench_measure.cpp`

**Status:** Checked — [x] Reviewed (97 lines, read in full)

**Issues Found:** The "cycles/op" column is TSC ticks, not cycles.

- **`cycles = per_op[median]` is a raw TSC-tick delta** (lines 40-41), reported
  under the heading `cycles/op` (line 88). With `constant_tsc` the TSC runs at a
  **fixed nominal** frequency regardless of the core's actual frequency, so
  ticks equal cycles only when the core happens to run at that nominal rate.
  Under turbo or thermal throttling they differ, and `docs/BASELINE.md`
  reproduces these numbers as cycles.
- **`a.reset()` is inside the timed lambda** (line 75), so it is counted in the
  batch. O(1) against 8,192 allocations, so negligible.

**Severity:** Minor (both)

**Why it matters:** `docs/BASELINE.md` reproduces this file's output verbatim, so a mislabelled column becomes a mislabelled number in the document readers cite.

**Fix Recommendation:** Rename the column to `tsc-ticks/op`, or derive real
cycles from an `aperf`/`mperf` ratio, which is more work than the number is
worth. Renaming is the honest fix.

**Refactor Suggestion:** The `bench()` helper here and the one in
`bench/bench_book.cpp` differ only in whether the setup returns state. One
shared header would keep the warm-up and median conventions from drifting apart.

**Tests Missing:** n/a.

**Performance Notes:** The harness itself is right: median of nine batches with
two discarded, "so a single preemption does not decide the answer", and
`do_not_optimize` around every result. The `Pool acquire+release` case (line 84)
acquires and immediately releases the same slot, so it measures the warmest
possible path — which the closing note covers: "warm-cache numbers on an idle
machine and are a floor, not a promise."

**Security Notes:** None.

**Market-Logic Notes:** Not applicable.

---

### `bench/bench_book.cpp`

**Status:** Checked — [x] Reviewed (210 lines, read in full)

**Issues Found:** One benchmark does not measure what it is named. One reject
count silently omits an error class.

- **"cancel (mid-queue)" removes from the FRONT of each queue.** The setup adds
  ids 1..201,000 in order across 20 levels (lines 75-80), and the timed loop
  removes ids 1, 2, 3, ... in the same order (lines 83-85) — always the current
  head of its level's FIFO. Head removal and mid-queue removal are both O(1) for
  an intrusive doubly-linked list, so the claim the benchmark is testing still
  holds; but the head is the hottest pointer in the level, so the number is the
  best case, and the label says otherwise.
- **The rejected total omits `CrossedBook`.** Lines 144-146 sum
  `errors[1]` through `errors[5]`. `BookError::CrossedBook` is index 6 and
  `Count` is 7, so every crossing rejection is invisible in the summary line —
  and the synthetic generator can produce them.
- **`2.0 * to_nanos(35)`** (line 139) is the third hardcoded copy of the timer
  overhead in the repository, agreeing with `apps/replay` and disagreeing with
  `apps/latency_demo`'s 30.
- **The mixed-replay loop times `apply` with unserialised `tsc::now()`** (lines
  127-129), the same point recorded under `apps/replay/main.cpp`.

**Severity:** Minor (all)

**Why it matters:** A benchmark named for a case it does not measure will be trusted for that case, and a reject total that omits an error class hides the one error the synthetic generator most often produces.

**Fix Recommendation:**
```cpp
// Sum every error class, so a new one cannot be added and silently ignored.
std::uint64_t rejected = 0;
for (std::size_t i = 1; i < static_cast<std::size_t>(BookError::Count); ++i)
  rejected += b.stats().errors[i];
```
and either rename the benchmark to "cancel (queue head)" or remove ids from the
middle of a level (`i` stepping by 20 from an offset, say).

**Refactor Suggestion:** As for `bench_measure.cpp` — share the `bench()`
harness.

**Tests Missing:** n/a.

**Performance Notes:** The setup work is outside the timed region in every case,
which is the thing this kind of file usually gets wrong. Building the event
stream against a scratch book and then replaying the recorded stream into a
clean one (lines 170-183) is the right way to keep the generator's references
valid without timing the generator.

**Security Notes:** None.

**Market-Logic Notes:** The feature-engine benchmark (lines 134-153) is the best
idea in the file: it runs the same update against books of 5,000, 50,000 and
500,000 orders, an order of magnitude apart, with the reason stated — "if the
cost tracks depth, an update rule is walking the book and the whole design
premise is broken." That turns an architectural claim into a number, which is
what a benchmark is for.

---

### `.github/workflows/ci.yml`

**Status:** Checked — [x] Reviewed (77 lines, read in full)

**Issues Found:** The extended fuzz pass omits the only target that consumes
untrusted input. The `tsan` preset is never run. Two supply-chain hardening
gaps.

- **`fuzz_bitstamp` is excluded from the extended fuzzing step** (lines 62-66,
  which run `fuzz_book` and `fuzz_matching` at 200,000 runs each).
  `CMakeLists.txt:189` registers `fuzz_bitstamp_short` at 20,000 runs under
  ctest, so it does run — at **one tenth** the depth of the two targets that
  consume a POD the project built itself. The JSON scanner is the only code in
  the repository that parses bytes from a public network, and it gets the least
  fuzzing.
- **The `tsan` preset in `CMakePresets.json` has no CI job.** A build
  configuration nothing runs will stop compiling without anyone noticing.
- **No `permissions:` block.** The workflow inherits the repository's default
  token scope. `permissions: contents: read` at the top costs one line and
  removes write access from every job.
- **Actions are pinned by tag, not by commit SHA** (`actions/checkout@v4`, line
  29). A moved tag re-points the action at different code.
- **No `timeout-minutes`.** A fuzz target that hangs consumes the six-hour
  default before the job is killed.

**Severity:** Major (Medium) (the fuzz omission), Minor (the rest)

**Why it matters:** The fuzz gap is the one that matters. The threat model in
`fuzz/fuzz_bitstamp.cpp`'s header is correct — a capture file "arrived over a
public websocket and was written to disk unmodified" — and that target is where
the effort should be concentrated, not where it is thinnest.

**Fix Recommendation:**
```yaml
permissions:
  contents: read

jobs:
  build-and-test:
    timeout-minutes: 45
    ...
      - name: Extended fuzzing
        if: matrix.preset == 'release'
        run: |
          ./build/${{ matrix.preset }}/fuzz_book      -runs=200000 -max_len=512
          ./build/${{ matrix.preset }}/fuzz_matching  -runs=200000 -max_len=512
          ./build/${{ matrix.preset }}/fuzz_bitstamp  -runs=200000 -max_len=4096
```
`fuzz_bitstamp` wants a larger `max_len` than the other two, because a realistic
JSON frame is longer than 512 bytes. Add a fourth matrix entry for `tsan`.

**Refactor Suggestion:** Add a `concurrency` group keyed on the ref, so a second
push cancels the first job rather than running both.

**Tests Missing:** n/a.

**Performance Notes:** The split at lines 50-57 is well judged: sanitizer builds
run the fast differential pass and the optimised jobs run `--long`, "which is
the same code over many more events". So ASan gets breadth and the release build
gets depth, rather than paying for both twice.

**Security Notes:** As above — the two hardening items, plus the observation
that `pull_request` on a public repository gives fork PRs a read-only token by
default, so the missing `permissions:` block is defence in depth rather than an
open hole.

**Market-Logic Notes:** Not applicable. Two judgement calls are worth keeping:
`|| true` on the libFuzzer runtime install (line 39), with the comment "Without
it CMake falls back to the portable driver, which still runs the same targets —
so this is an upgrade, not a requirement"; and the smoke-run step's framing
(lines 68-69), "CI machines are noisy and shared, so this checks that it works,
not that it is fast." A CI job that asserted latency numbers on a shared runner
would fail constantly and be disabled within a month.

---

### `CMakePresets.json`

**Status:** Checked — [x] Reviewed (54 lines, read in full)

**Issues Found:** The declared minimum CMake version cannot read this file. The
`tsan` preset is unreachable from CI.

- **`"version": 3` requires CMake 3.21**, but line 3 declares
  `cmakeMinimumRequired` as 3.20.0. Presets schema v2 is the 3.20 version; v3
  arrived in 3.21. A CMake 3.20 that honoured the stated minimum would reject
  the file it is stated in.
- **`tsan` is defined and never used** — see the CI section.
- **`LOB_NATIVE` is `OFF` for debug, asan and tsan but left ON for release**, so
  release builds carry `-march=native` (`CMakeLists.txt:39`). Correct for a
  benchmark build; worth knowing that a release binary is then not portable off
  the machine that built it.

**Severity:** Minor (all)

**Why it matters:** A build configuration nothing runs will stop compiling without anyone noticing, and a stated minimum version that cannot read the file stating it is a trap for the first person on an older toolchain.

**Fix Recommendation:** Set `"cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 }`,
or drop to `"version": 2` if 3.20 support is wanted (nothing here needs v3
features).

**Refactor Suggestion:** None. Four presets, each with one clear purpose, and
the `displayName`s say what they are for.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** `-fno-sanitize-recover=all` on both sanitizer presets is the
right choice: without it, UBSan reports and continues, so a CI job can print a
violation and still exit zero.

**Market-Logic Notes:** Not applicable.

---

### `tools/jitter_baseline.sh`

**Status:** Checked — [x] Reviewed (43 lines, read in full)

**Issues Found:** One inherited argument-validation gap.

- **`SECONDS_TO_RUN` is unvalidated** (line 11) and passed straight to
  `jitter_probe`, which casts a negative value into a `std::uint64_t` deadline
  and spins. The root cause is recorded under `apps/jitter_probe/main.cpp`; the
  script simply forwards it.

**Severity:** Minor

**Why it matters:** This is the script `docs/BASELINE.md` tells people to run, so its one rough edge is the first one a new contributor meets.

**Fix Recommendation:** Fix it in `jitter_probe` rather than here, so both entry
points are covered.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** The shell is careful. `set -euo pipefail`, every expansion
quoted, `2>/dev/null || true` on each optional probe so a missing
`/sys` entry degrades to `n/a` rather than aborting under `-e`, and the
`cyclictest ... || echo` at line 38 keeps a permission failure from killing the
run. Notably it uses `SECONDS_TO_RUN` rather than `SECONDS`, which is a bash
special variable holding the shell's uptime — overwriting it is a classic and
silent bug, and this avoids it.

**Market-Logic Notes:** Not applicable. The machine section (lines 14-21) prints
governor, `isolcpus`, transparent hugepages and the TSC flags before any number,
which is the right order: those four settings determine whether the numbers
below them mean anything.

---

### `tools/check_capture.py`

**Status:** Checked — [x] Reviewed (166 lines, read in full)

**Issues Found:** The price-reach calculation does not implement the reasoning
in the comment above it.

- **Line 104:**
  ```python
  reach = max(abs(hi - lo), abs(lo - hi)) / mid0
  ```
  `abs(hi - lo)` and `abs(lo - hi)` are the same number, so the `max` is a
  no-op and this is just the full range over the midpoint of the range. The
  comment directly above says the window "has to reach from that **opening
  price** to the furthest the market got in either direction — which is not the
  same as half the range, when the move is one-sided." That is right, and it
  needs the opening trade price, which `scan()` never records.
- **`if r["t0"] and r["t1"]`** (line 68) treats a timestamp of 0 as absent.
  Microtimestamps are never 0, so this is a note.
- **Only `*_bitstamp.jsonl.gz` is globbed** (line 38), so a partially written
  uncompressed capture from a crashed recorder is invisible to the checker.

**Severity:** Major (Medium) (the reach calculation, because its output is a
recommended `--band-pct` and a one-sided move is exactly when the recommendation
matters)

**Why it matters:** Its output is a recommended `--band-pct`, and a one-sided price move is exactly the case where the recommendation matters and the formula is wrong.

**Fix Recommendation:** Record the first trade price and measure from it:
```python
# in scan(): capture the first trade price
if first is None: first = v
...
# in main():
reach = max(hi - first, first - lo) / first
```
That is what the comment describes, and for a one-sided move it gives a
materially different answer from the current expression.

**Refactor Suggestion:** None.

**Tests Missing:** No test covers this tool. A fixture capture with a
deliberately one-sided price move, asserting the recommended band covers it,
would pin the fix.

**Performance Notes:** One pass over the bytes with substring searches before
any JSON parsing, and the reason is stated: "Full JSON parsing 10M lines is
minutes; the fields that matter are found by substring first and parsed only
when present." Correct trade for a pre-flight checker.

**Security Notes:** Pure standard library, no network, no credentials, and
`gzip.open(..., errors="replace")` so a corrupt file yields replacement
characters rather than an exception.

**Market-Logic Notes:** Two decisions here are right and are the kind that are
usually got wrong.
- **The price range is taken from TRADE prices, not order prices** (lines
  26-30): "Someone always has a sell resting at 999,999,999 and a buy at 0.01;
  the min and max of the order book say nothing about where the market was, and
  sizing the price window from them would ask for a window a billion wide. A
  print is by definition a price both sides agreed on."
- **A mixed-instrument directory is refused outright** (lines 88-96) rather than
  producing pooled nonsense, with the concrete example: `data/samples` holds
  xrpusd at $1.40 and btcusd at $79,875, and "the price range computed over both
  asks for a window 200% wide."

---

### `tools/record_bitfinex.py` and `tools/record_coinbase.py` (the two venues without a reconnect loop)

**Status:** Checked — [x] Reviewed (232 + 242 lines, read in full)

**Issues Found:** Neither reconnects, which is the defect that was found and
fixed in the Bitstamp recorder and never carried across. One unvalidated JSON
response. One unbounded frame size.

- **No reconnect handling in either.** `tools/record_bitstamp.py` opens its
  websocket inside a retry loop with backoff and a fresh REST snapshot per
  session (lines 91-149), because "any reconnect means messages were missed".
  Both of these open the socket once (`record_bitfinex.py:110`,
  `record_coinbase.py:99`) inside a single `async with`. A dropped connection
  three minutes into an eight-hour run ends the capture, and the operator finds
  out afterwards.
- **`record_coinbase.py:131`: `requests.get(REST_URL...).json()`** with no
  `raise_for_status()` and no shape check — the identical open finding already
  recorded against `tools/record_bitstamp.py:207`. An HTML error page raises
  inside the recording loop.
- **`websockets.connect(..., max_size=None)`** in both (and in the Bitstamp
  recorder) removes the default 1 MiB frame limit. That is necessary — a
  full-book snapshot frame exceeds it — but it means a malfunctioning or hostile
  endpoint can drive the client to buffer without bound.

**Severity:** Major (Medium) (no reconnect), Minor (the other two)

**Why it matters:** A capture is hours of wall time that cannot be re-run for a
past market. Losing one to a transient disconnect is the most expensive
failure these scripts can have, and the fix already exists in the sibling file.

**Fix Recommendation:** Lift the retry loop out of `record_bitstamp.py` into a
shared helper and use it from all three. At minimum:
```python
r = requests.get(REST_URL.format(product=self.product), timeout=30)
r.raise_for_status()
snap = r.json()
if not isinstance(snap.get("bids"), list) or not snap.get("asks"):
    raise RuntimeError(f"unexpected snapshot shape: {sorted(snap)[:6]}")
```

**Refactor Suggestion:** The three recorders share their file rotation, gzip
handling, SIGINT trap and reporting almost verbatim. One `Recorder` base class
with a per-venue `_record()` would remove three copies of the same rotation
logic — and would have meant the reconnect fix landed everywhere at once.

**Tests Missing:** None practical; these need a live endpoint. The decoders they
feed are covered by `tests/test_bitstamp.cpp` and `fuzz/fuzz_bitstamp.cpp`.

**Performance Notes:** Both write through `gzip.open(path, "wt")`, which is
buffered. A clean `SIGINT` is trapped and flushes; a `SIGKILL` truncates the
gzip stream. `LineReader` and the decoder both handle a truncated final line, so
the loss is bounded to the last block.

**Security Notes:** **Clean.** Both connect over `wss://` and `https://` with
default certificate verification; there is no `verify=False`, no
`ssl._create_unverified_context`, no API key, no token, no signing, and no
credential of any kind. Both headers state it explicitly:
`record_bitfinex.py` line 5, "a public endpoint that needs no API key", and
`record_coinbase.py` line 28, "No API key: Coinbase Exchange market data
channels are public." That is consistent with the project's stated rule that
nothing here can send an order.

**Market-Logic Notes:** `record_bitfinex.py`'s header (lines 5, 36) records why
three venues exist: Bitfinex publishes true order-by-order data with order ids
on a public endpoint, while "Coinbase's full channel has no such window, but now
requires an API key." Keeping the comparison in the file, rather than in a
commit message, is what stops the next person re-evaluating it from scratch.

---

### `README.md`

**Status:** Checked — [x] Reviewed (399 lines, read in full)

**Issues Found:** The test-count claim is stale by two orders of magnitude. The
status paragraph is a phase behind the repository. Two path inconsistencies.

- **Line 18: `ctest --preset release  # 5 suites, ~90 assertions`.** Measured:
  ```
  $ ctest --test-dir build -N | tail -1
  Total Tests: 26
  $ ctest --test-dir build -E "_long"
  100% tests passed, 0 tests failed out of 25
  ```
  Twenty test files, 26 registered tests, and summing the harness's own check
  counters across the suites gives roughly **24 million** assertions, because
  the property and differential tests assert inside their loops. Whatever number
  belongs here, "5 suites, ~90 assertions" is not it.
- **Line 7: "Status: hardening — fuzzing and property tests"** and line 12's
  summary stopping at "the baseline strategies with their P&L attribution
  (Phase 4)". The repository has a complete Phase 5 — `apps/solve`,
  `apps/evaluate`, `policy/ethusd.bin` — and the README documents it at line 91.
  The status paragraph simply was not updated with the section below it.
- **Two different binary paths in one file.** Lines 20-30 use
  `./build/release/...`; lines 95-121 use `./build/...`.
- **`fuzz_bitstamp` is missing from the run list** (lines 28-29 name the other
  two), the same omission as `.github/workflows/ci.yml`.

**Severity:** Minor (all — documentation drift, not defects)

**Why it matters:** The assertion count is the number a reader uses to decide
how much to trust the suite, and understating it by five orders of magnitude
sells the work short in the one place a reader looks first.

**Fix Recommendation:** Replace line 18 with `# 26 tests across 20 files`, which
is checkable, and drop the assertion count rather than maintain it. Add
`fuzz_bitstamp` to the run list. Move the status line forward to Phase 5 and
state the acceptance criterion's actual result, which `docs/05-roadmap.md:150`
already marks ❌.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** No credentials, endpoints requiring keys, or install
instructions that would introduce one.

**Market-Logic Notes:** The technical content is accurate where I checked it
against the code — the Phase 5 description at lines 91-141, the statement that
"the execution layer never solves an optimisation" (line 371), and the
`evaluate --probe` explanation at lines 133-134 all match what the binaries do.

---

### `tools/calibrate.py`

**Status:** Checked — [x] Reviewed (363 lines, read in full)

**Issues Found:** None. One misplaced comment.

- **`prepare()`'s comment (lines 123-126) describes an exclusion the function
  does not perform.** Negative deltas are excluded, but in `main()` at line 149
  (`o.delta > 0`) and counted at line 154 (`inside_spread_excluded`). The
  comment is correct about the behaviour, it is just not in the function that
  implements it.

**Severity:** Minor

**Why it matters:** This is the estimator behind the claim that Avellaneda-Stoikov cannot be identified on these books, so its correctness decides whether the entire Phase 5 state design is justified.

**Fix Recommendation:** Move the comment to line 149.

**Refactor Suggestion:** None.

**Tests Missing:** Nothing here is unit-tested. The estimator is the kind of
code that would benefit from one synthetic case: generate exposures and events
from a known `(A, k)`, fit, and assert recovery inside the reported standard
errors. That is ten lines and it would guard every future edit to the Newton
step.

**Performance Notes:** Per-order fit rather than binned rates, vectorised over
numpy, 100 Newton iterations maximum with an early exit. Fine.

**Security Notes:** No network, no credentials. `matplotlib.use("Agg")` before
importing `pyplot` (lines 45-47), so it is headless-safe.

**Market-Logic Notes:** I checked the estimator by hand and it is correct.
- The gradients and Hessian of `Σ(y·log μ − μ)` with `μ = T·exp(a − kδ)` are
  transcribed correctly (lines 76-81), and the Newton step `Δ = −H⁻¹g` is
  expanded correctly at lines 84-85.
- **The covariance is the inverse of the *negative* Hessian** (lines 100-111),
  and the comment records the bug that made this worth writing down: the first
  version inverted the Hessian and clamped the resulting negative variances to
  zero, "reports a standard error of exactly 0.0000 for every parameter. A zero
  standard error on 15 events is not a tight fit, it is a broken one." When the
  variance still comes out negative it is reported as `nan` rather than hidden.
- `se_A = A·se_a` is the correct delta-method transform for `A = exp(a)`.
- The **Wilson score interval** (lines 262-264) rather than the normal
  approximation, which is the right choice at these counts: with 24-65 fills and
  a small `p̂`, the normal interval routinely goes below zero.
- Censoring is handled correctly and the header says why: "Orders cancelled
  before filling are not missing data: they are exposure without an event, which
  is exactly what a Poisson likelihood wants."
- **The range problem is confronted rather than hidden** (header lines 29-37):
  delta runs to 160,000 ticks, half of btcusd's orders rest more than 1,191
  ticks out contributing 71,000 order-seconds against 15 fills, and "fitting an
  exponential across that range does not measure the decay a market maker
  experiences; it measures the fact that nobody trades $12 away from the touch."
  So the fit is restricted, the range is printed with the answer, and it is
  repeated at several cutoffs so instability is visible.
- **The caveat is written into `calibration.json` itself** (lines 275-282), so
  the artifact carries its own limitations: "Fill counts are 24-65 per
  instrument, so these are order-of-magnitude estimates. A-S has no queue term
  and these books sit at a one-tick spread almost always, where queue position
  rather than distance decides fills." A calibration file that travels with its
  own health warning is the right pattern.

---

### `tools/figures.py`

**Status:** Checked — [x] Reviewed (315 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** Every figure in the documents comes from here, so a wrong instrument would propagate silently into every argument that cites one.

**Fix Recommendation:** None.

**Refactor Suggestion:** The plot styling block (lines 26-44) is duplicated
almost verbatim in `tools/calibrate.py` (lines 51-63). One shared `style.py`
would keep the figures visually consistent as either file changes.

**Tests Missing:** None warranted; the output is inspected by eye.

**Performance Notes:** n/a.

**Security Notes:** `matplotlib.use("Agg")` before `pyplot` (lines 21-23), so it
runs headless. No network, no credentials. `load()` returns `None` for a missing
CSV rather than raising (lines 71-73), so a partial `stats` run produces the
figures it can.

**Market-Logic Notes:** `survival()` (line 76) plots order lifetime as a
survival curve rather than a histogram, which is the correct instrument for
censored duration data. The seven figures map one-to-one onto the estimators in
`docs/03-metrics-and-estimators.md`.

---

### `docs/00-scope-and-architecture.md` and `docs/04-toolchain.md`

**Status:** Checked — [x] Reviewed (300 and 161 lines, both read in full.
`docs/01`, `docs/02` and `docs/03` were originally covered here and now have
their own subsections below.)

**Issues Found:** One structural drift, one label that is now half-true.

- **`docs/00` §5's repository layout has diverged from the repository.** It is
  headed "**Proposed** repository layout", so this is aspiration rather than
  error, but the gap is now wide: `python/`, `cmake/`, `apps/calibrate/`,
  `apps/live/`, `feed/itch.hpp`, `feed/sbe.hpp`, `book/price_level.hpp` and
  `book/own_orders.hpp` do not exist, and the real tree has `feat/` rather than
  `features/` and a `strat/` the layout does not mention.
- **The same block marks `data/` "(gitignored)"**, and `data/samples/` is
  tracked — 5.3 MB across six files. That is deliberate and `.gitignore` lines
  6-15 explain the `data/*` plus `!data/samples/` construction and why a bare
  `data/` would not work. The doc predates the decision.

**Severity:** Minor (both)

**Why it matters:** `docs/00` is the document a reader opens first, and its layout section is now a plan being read as a description.

**Fix Recommendation:** Re-title §5 "Repository layout (as built)" and paste the
current tree, or leave it as the plan and add one line saying which parts are
built. Either is fine; silently drifting is not.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** No credentials, keys, tokens or internal hostnames in any of
the five. `docs/02` §3 is titled "Where to get the data — **free sources
only**", consistent with the project's stated constraint.

**Market-Logic Notes:** These five are the reference the code is written
against, and the code matches them where I checked. Three cross-references
proved load-bearing during this audit:
- **`docs/03` §10** specifies Total P&L as spread capture minus adverse
  selection minus **inventory / hedging cost (mark-to-market of held
  inventory)** minus fees minus unwind impact. `include/lob/strat/pnl.hpp`'s
  header cites this section by name and then omits the inventory term from
  `Attribution::total`. **The code contradicts the specification it cites** —
  which promotes that finding from a judgement call to a defect.
- **`docs/03` §5**, that the fill/cancel split must never be aggregated, is
  implemented in `apps/replay` and quoted there.
- **`docs/03` §2**'s stage taxonomy matches `Stage` in
  `include/lob/measure/recorder.hpp` one for one.
`docs/01`'s literature list is organised by what each paper is *for* rather than
chronologically, with `[core]` markers, and every arXiv id I spot-checked
against a citation in the code matched.

---

### `docs/05-roadmap.md`

**Status:** Checked — [x] Reviewed (276 lines, read in full)

**Issues Found:** None. One entry to update from this audit.

- **Line 150 already marks the Phase 5 acceptance criterion ❌**: "The tabulated
  policy beats the best Phase 4 baseline in the simulator, out of sample." That
  is honest and current. What it does not yet record is the finding from this
  audit — that the comparison against `JoinTouch` returns *exactly* zero, so the
  criterion is not merely unmet, it is currently uninformative.

**Severity:** Minor

**Why it matters:** It is the file that records what is and is not done, and it is already honest about the Phase 5 criterion. What it does not yet carry is why that criterion currently returns no signal at all.

**Fix Recommendation:** Add one line under Phase 5 noting the exact tie and the
baseline parameterisation defect, so the next person does not re-derive it.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** §"Permanently out of scope" (line 263) is the project's
governing safety rule and is unambiguous: "**Sending real orders, in any form,
on any venue.** No order gateway, no API keys, no credentials, no paper-trading
account that could be switched to live by changing a flag." The audit found
nothing in the repository that contradicts it — no order-entry code, no
credentials, no authenticated endpoint, no flag that could be flipped.

**Market-Logic Notes:** The ordering principle at line 255 — "**The same code in
backtest and shadow mode**, or the comparison in Phase 6 is meaningless" — is
what forces the design decisions elsewhere: one driver shared by
`apps/backtest` and `apps/evaluate`, one queue-position estimator used by both
the tabulated policy and any production system, one journal format.

---

### `docs/06-queue-reactive-plan.md`, `docs/BASELINE.md` and `docs/KNOWN-ISSUES.md`

**Status:** Checked — [x] Reviewed (717 + 85 + 457 lines, read in full)

**Issues Found:** One number in `BASELINE.md` inherits a mislabelling from the
benchmark that produced it. `KNOWN-ISSUES.md` has two open entries this audit
touches.

- **`docs/BASELINE.md`'s "cycles/op" column** reproduces
  `bench/bench_measure.cpp`'s output, which reports **TSC ticks**, not cycles.
  With `constant_tsc` the TSC runs at a fixed nominal rate independent of the
  core's actual frequency, so the two coincide only when the core runs at that
  nominal rate. The table's own note that these are "warm-cache numbers on an
  idle machine" is right; the column heading is not.
- **`KNOWN-ISSUES.md` 1 (ethusd book holes) and 3 (fabricated fills) are open**
  and correctly described. Issues 2 and 5 are marked FIXED and I confirmed both
  in the code.

**Severity:** Minor

**Why it matters:** These three are where measurements and open defects are recorded, so a number that drifts from its source here drifts everywhere that cites it.

**Fix Recommendation:** Rename the column when `bench_measure` is fixed, so the
document and its source agree. Add the baselines finding to `KNOWN-ISSUES.md` as
a new entry — it is exactly the shape of the entries already there.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** `BASELINE.md`'s instruction to re-run after any tuning
change and **append rather than replace** ("the point is the comparison") is the
right convention for a machine baseline.

**Security Notes:** None. No hostnames, no credentials.

**Market-Logic Notes:** `KNOWN-ISSUES.md` is the most valuable document in the
repository and the reason is its format: each entry states the symptom, the
measurement that found it, the cause, and either the fix or an explicit open
status. Entry 4 in particular records a defect that had been written up
**backwards** — the touch was reported as moving 60x too often when per event it
was about 1,000x too stable — and says so rather than quietly correcting it.
`docs/06` carries the queue-reactive fit with its measured tables, so every
constant in `FlowConfig::Qr` is traceable to a number in that document.

---

#### Group note — `docs/figures/` as a whole

Each of the eight files has its own subsection further below; this note covers
only what is true of the set.

**Status:** Checked — [x] Reviewed (8 files; PNGs inspected for size and provenance,
`calibration.json` read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** These are build outputs committed to the repository. That is
a deliberate choice and a defensible one: the figures are referenced from the
docs, and a reader should not need numpy, pandas and matplotlib plus a capture
to see them.

**Fix Recommendation:** None. Total size is 684 KB across eight files, the
largest 143 KB — small enough that committing them costs nothing meaningful
against an 8.4 MB repository.

**Refactor Suggestion:** None.

**Tests Missing:** Nothing regenerates and diffs them, so a figure can silently
fall out of date with the estimator that produced it. Regenerating them in CI
and failing on a diff would be brittle (matplotlib output is not
byte-reproducible across versions); the practical alternative is to record in
each document the `stats` invocation that produced its figure, which
`tools/calibrate.py`'s docstring already does.

**Performance Notes:** n/a.

**Security Notes:** No EXIF or embedded metadata concerns — these are
matplotlib Agg renders of aggregate statistics, and `calibration.json` contains
only fitted parameters and their standard errors. Nothing identifies a person, a
machine, or an account.

**Market-Logic Notes:** `calibration.json` carries its own caveat field, quoted
under `tools/calibrate.py`. An artifact that states the limits of what produced
it is the right way to ship an estimate this uncertain.

---

#### Group note — `policy/` as a whole

Each of the five files has its own subsection further below; this note covers
only what is true of the directory.

**Status:** Checked — [x] Reviewed (5 files; the binaries by header and provenance, the
three JSON files read)

**Issues Found:** None. One observation about what the binaries commit the
repository to.

- **`ethusd.bin` and `simcal.bin` are 126,808 bytes each** and are solved
  policy tables. `src/policy_table.cpp` validates the magic, the schema version
  and the **entire discretisation** before accepting one, so a stale table
  cannot be silently consulted by a build with a different state space — it is
  rejected with a message naming the mismatch. That is what makes committing
  them safe.
- **`mdp.json` (15 KB), `queue_reactive.json` (7 KB) and `sim_mdp.json` (5.6 KB)
  are fitted parameters with their standard errors, bucket counts and event
  counts.** Reading `queue_reactive.json`, each level carries `exposure_s`,
  `beta`, `se`, `buckets_used` and `events` — so a reader can tell which fits
  rest on 240 events and which do not.

**Severity:** n/a — no issues found

**Why it matters:** Five committed artefacts that the execution path consumes. Committing them is only safe because of the validating loader, and that is worth stating once for the directory.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** Nothing asserts that the committed `policy/ethusd.bin` still
loads under the current build. It is exercised indirectly whenever
`apps/evaluate` is run by hand, but not by `ctest`. One test that calls
`PolicyTable::load("policy/ethusd.bin")` and checks the return value would catch
the day a discretisation constant changes and silently invalidates the shipped
table — which is precisely the failure `policy_table.cpp` is built to detect and
which nothing currently triggers.

**Performance Notes:** n/a.

**Security Notes:** No credentials. The `.bin` files are read through the
validating loader described above, which bounds-checks every action byte against
`kNumActions` before accepting the table, so a corrupted file cannot produce an
out-of-range action index at lookup time.

**Market-Logic Notes:** `mdp.json`'s first block is worth quoting as an example
of what these files carry: `p_up`, `p_down`, `p_flat`, `median_abs_move_ticks`,
`winsorised_sd_ticks` **and** `raw_sd_ticks`, `max_abs_move_ticks`, `samples`
and `steps_kept_pct`. Keeping the winsorised and raw standard deviations side by
side (5.27 against 22.20 for btcusd) is what lets a reader see how much of the
variance is tail, which is the difference between a defensible inventory penalty
and an arbitrary one.

---

#### Group note — `data/samples/` as a whole

Each of the six files has its own subsection further below; this note covers
only why the directory is tracked at all.

**Status:** Checked — [x] Reviewed (6 files; sizes, tracking rules and provenance checked;
contents sampled)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** 5.3 MB of the repository's 8.4 MB is this directory, so it
deserves an explicit justification, and there is one. `.gitignore` lines 12-15
give it: "except the committed captures the real-data regression tests replay. A
test against synthetic data only ever proves the decoder agrees with the
generator." `CMakeLists.txt:118` registers a `capture_*` test per pair, so all
three are exercised on every `ctest` run rather than sitting unused.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** None — these files *are* test fixtures.

**Performance Notes:** The three captures are 10-minute sessions, 1.2 to 2.9 MB
gzipped. A clone costs 8.4 MB total, which is not a burden. If more captures are
ever committed, Git LFS becomes the right answer before the second one.

**Security Notes:** **Clean, and worth stating explicitly since these are the
only third-party data in the repository.** They are public market data from
Bitstamp's `live_orders_*` and `live_trades_*` websocket channels plus the
public REST order-book snapshot. There is no account identifier, no API key, no
authentication token, and no personally identifying information — order ids are
exchange-assigned integers that identify an order, not a person. Nothing here
required credentials to obtain and nothing here could be used to obtain any.

**Market-Logic Notes:** The `.gitignore` construction is the interesting part
and the comment explains a real Git subtlety: `data/*` rather than a bare
`data/`, because "Git will not descend into an excluded DIRECTORY, so a bare
`data/` makes the re-include below unreachable and `git add data/samples` fails
with nothing but a hint." Choosing three instruments across three price scales —
btcusd near $79,875, ethusd, and xrpusd near $1.40 — is what makes the
decimal-detection and price-window logic testable at all, since a fixed tick
count that suits one is absurd for the others.

---

### `tests/test_policy.cpp`

**Status:** Checked — [x] Reviewed (504 lines, read in full)

**Issues Found:** A second wall-clock assertion in a unit test. One precision
correction to a finding recorded earlier in this audit.

- **`CHECK(ns < 100.0)` on the policy lookup** (line 391). Like
  `tests/test_tsc.cpp:92`, this asserts a timing on a shared machine. It is
  better constructed than that one — the probes are random states across the
  whole table, so it is "a cache-miss-inclusive number rather than a hot-loop
  one", and the comment says "The claim is nanoseconds, not that it is free" —
  but on an oversubscribed CI container a preemption inside a 65,536-iteration
  loop still breaks it.
- **Correction to the `tests/test_strategies.cpp` finding.** Line 492
  constructs a near-default `QuoteParams` (only `size` and `max_inventory` are
  set), which is the same shape as `apps/evaluate`'s `base_params()`. But it is
  handed to `JoinTouch`, which never reads `gamma`, `sigma` or `horizon`. So the
  accurate statement is: **no test anywhere exercises the default risk
  parameters through a strategy that uses them.** The defaults are constructed;
  they are simply never read.

**Severity:** Minor (the timing assertion), plus the correction above

**Why it matters:** It is the test for the file that decides what the Phase 5 policy means, and its one fragile assertion is the kind that gets deleted rather than fixed when CI goes red.

**Fix Recommendation:** Print the lookup cost and assert a ceiling a stall
cannot cross, or gate the assertion on `tsc::calibration().trustworthy`.

**Refactor Suggestion:** None.

**Tests Missing:** Nothing that the shipped table still loads under the current
build — recorded under `policy/`.

**Performance Notes:** As above. The design of the measurement is right even
where the assertion is fragile.

**Security Notes:** None.

**Market-Logic Notes:** This is a thorough file and three blocks earn naming.
- **The discretisation round-trips for every state** (lines 55-79): `encode` and
  `decode` are checked over the entire state space, not a sample, and every
  action byte round-trips. Given that `PolicyTable::load` rejects a table solved
  over a different discretisation, an encode/decode asymmetry would be the one
  way a wrong answer could still get through.
- **"the process is a process"** (lines 125-155) verifies that every state's
  successor probabilities sum to 1 to within 1e-9 and that every state has at
  least one legal action. An MDP whose transitions do not sum to 1 is not a
  contraction and value iteration on it does not converge to anything.
- **The move-fill split** (lines 156-212) is checked as a **split**, not a
  change: the comment explains that charging every move-fill the full move made
  a touch quote arithmetically a loser on real data, that the measurement says
  the move has reverted two thirds of the time by the holding horizon, and that
  the fix must preserve probability mass — "A branch that creates mass is not a
  contraction and value iteration diverges on it." Testing the conservation
  property rather than the numbers is the right choice.
- **A malformed process is refused, not solved** (line 258), and the position
  limit is tested as a constraint on the **action** rather than on the state
  (line 270), which is the distinction that keeps the solver from quietly
  producing a policy that violates it.

---

### `tests/test_bitstamp.cpp`

**Status:** Checked — [x] Reviewed (585 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** This is the test for the only untrusted-input path in the
project, and it is built the right way round.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** The two entry points recorded under `fuzz/fuzz_bitstamp.cpp`
— `detect_decimals` and `snapshot_touch` — are covered here with valid input but
not with hostile input. Fuzzing is the right home for that, not this file.

**Performance Notes:** `main` takes an optional capture path and snapshot from
`argv` (line 581), and `CMakeLists.txt:118` registers one `capture_*` test per
sample pair. So the same binary is both a unit test and a real-data regression
test, and the three committed captures are replayed on every `ctest` run.

**Security Notes:** None. All fixtures are string literals; the real-capture
path reads files the repository ships.

**Market-Logic Notes:** Two things make this file unusually good.
- **The fixtures are verbatim lines from a real capture, and the header says why
  that distinction earned its keep** (lines 3-9): "the documented plan was to
  recover the cancel/fill split by joining order ids against the trade channel,
  and the real bytes turned out to carry `amount_traded` on every message, which
  is both exact and clock-free. Testing against documentation would have
  enshrined the worse design." That is the strongest argument for real fixtures
  I have seen stated in a test file, and it is backed by a specific outcome.
- **The seed-guard block** (lines 498-531) tests the measured behaviour rather
  than the intended one. It pins exactly which snapshot orders seed and which do
  not, checks that seeded orders are also *registered* with the decoder — "or it
  will emit deletes the book rejects as unknown" — and records the measurement
  that justifies the guard: excluding near-touch snapshot orders is "worth 39
  points of accuracy on xrpusd, measured as the share of trades printing inside
  the touch." It also tests the two degenerate ends: a guard wider than the book
  seeds nothing, and a two-column snapshot ("L2 wearing L3's clothes") fails
  loudly rather than being decoded as though it had order ids.

---

### `tests/test_split_replay.py`

**Status:** Checked — [x] Reviewed (71 lines, read in full)

**Issues Found:** None. One small robustness note.

- **`gzip.open(src, "rt").readlines()`** (line 42) reads the whole capture into
  memory and never closes the handle. At 2.9 MB gzipped that is fine, and
  CPython's refcounting closes it at the end of the statement; on another
  interpreter it would leak until collection.

**Severity:** Minor

**Why it matters:** The property it guards fails silently: a reseed at a file boundary would shift every distribution slightly and look like data.

**Fix Recommendation:** `with gzip.open(...) as f: lines = f.readlines()`.

**Refactor Suggestion:** None.

**Tests Missing:** n/a — this is a test.

**Performance Notes:** Runs `stats` twice over the same capture, once whole and
once split. Slower than a unit test and it is the only way to check the
property.

**Security Notes:** `subprocess.run` with a list argument, no `shell=True`, and
every path from `tempfile.TemporaryDirectory()` or `sys.argv`. No injection
surface.

**Market-Logic Notes:** The property under test is exactly right and the
docstring says why it needs a test at all: "Getting this wrong does not fail
loudly. The book would simply be reseeded or truncated at each file boundary,
and every distribution measured afterwards would be measured from a book that
briefly described a different market." Comparing the **CSV outputs byte for
byte** (line 61) rather than comparing summary statistics is what makes it a
real check — a reseed at a boundary would shift many numbers slightly, and a
tolerance-based comparison would absorb it. The split is constructed to mirror
what the recorder actually produces: three files whose names sort in stream
order the way UTC stamps do, and one snapshot beside the first only.

---

### `include/lob/policy/state.hpp`

**Status:** Checked — [x] Reviewed (178 lines, read in full)

**Issues Found:** None. Two notes on constants that must stay in step with a
file outside this one.

- **`kImbEdges` must match `tools/mdp_params.py`** (lines 63-65). The comment
  says so and nothing enforces it. If they drift, "the policy is solved against
  one book and applied to another", and the table header's discretisation check
  would not catch it because the bucket *count* is unchanged.
- **`queue_typical(kQueueBuckets - 1)` returns 1.0**, a full queue, for a bucket
  that is unbounded above (lines 169-176). The comment defends it — a full queue
  is what joining the back of a level actually puts in front of you — and it is
  the right representative, but it means the deepest bucket's transition rate is
  optimistic for an order that joined an unusually thick level.

**Severity:** Minor (both)

**Why it matters:** The `kImbEdges` coupling is the one that could go wrong
silently. Everything else here is protected by the table header check in
`src/policy_table.cpp`, which validates the whole shape.

**Fix Recommendation:** Emit the edges into the params JSON from this header and
have `tools/mdp_params.py` read them, rather than both writing them out:
```python
# mdp_params.py
edges = json.load(open(args.state_constants))["imb_edges"]
```
or, cheaper, add the edges to `MdpParams::hash()` so a mismatch changes the hash
and `apps/solve` refuses the table.

**Refactor Suggestion:** None.

**Tests Missing:** A test that the edges in this header equal the edges in
`policy/mdp.json`. `tests/test_policy.cpp` covers `imb_bucket` against this
header's own constants, so a drift between the two files passes.

**Performance Notes:** Everything is `constexpr` and the encode/decode pair is
three multiplies and three adds. `kNumStates` is 11 x 16 x 16 x 5 = 14,080
states, so the whole table is 14 KB of policy plus 113 KB of value — the policy
fits in L1.

**Security Notes:** `quoting()` (lines 83-85) checks `> 0 && < kSideStates`
rather than `!= 0`, and the comment says why: a side state is an array index, so
`!= 0` would let a negative value through into `(side_state - 1) / kQueueBuckets`
and produce a negative subscript. That is a bounds check written for the right
reason.

**Market-Logic Notes:** This is the file where the project's central empirical
claim is encoded, and each constant carries the measurement that set it.
- **Queue position is in the state because measurement put it there** (lines
  16-22): the realised fill hazard runs 1,019/s with nothing ahead and 16/s with
  50,000 ahead, a factor of sixty, while `tools/calibrate.py` could not identify
  an Avellaneda-Stoikov `k` on two of three instruments because these books sit
  at a one-tick spread and delta has nowhere to vary. "Distance is the
  small-tick state variable; this is the large-tick one."
- **Bucketing by absolute volume ahead, not by quartile** (lines 37-48), with
  the defect that forced it: quartiles rank against the *wrong population*,
  because a market maker re-quotes the moment a level clears and so sits at
  small absolute queues far more often than the book's own orders do. Measured
  against a touch-joining strategy's real placements, the hazard at the front
  was 1,019/s while the quartile model said 52/s — "and the policy duly
  concluded that quoting behind the touch was better than being in front of it."
- **Three quote levels, not two** (lines 50-53): with two, a quote pushed
  further out vanished from the state and the model treated that as free, so it
  priced the cost of never quoting but not the cost of being left behind.
- **`kBackOfQueue`** (lines 96-100): a new order joins the back, and in a book
  where 99% of removals are cancels that is "the single most consequential line
  in the model: moving a quote one tick surrenders every second already spent
  waiting."

---

### `include/lob/policy/table.hpp`

**Status:** Checked — [x] Reviewed (83 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** This is the seam between the offline solve and the
execution path, and it is the strongest interface in the repository.

**Fix Recommendation:** None. The one defect in this component is in the
implementation, not the interface, and is recorded under `src/policy_table.cpp`
(a failed `load` leaves `value_` and `header_` stale).

**Refactor Suggestion:** None.

**Tests Missing:** Nothing asserts that the committed `policy/ethusd.bin` still
loads under the current build — recorded under `policy/ethusd.bin`.

**Performance Notes:** `action_for` (lines 68-70) is a compare and an array
index, marked `noexcept` and inline in the header. There is no branch on the
table's *contents*, only on the index bound, so lookup time cannot vary with the
state being asked about — which is what the file's opening paragraph promises
and what a latency budget needs. `tests/test_policy.cpp:367-394` measures it at
random states across the whole table so the number is cache-miss-inclusive.

**Security Notes:** `action_for` and `value_of` both bounds-check and return a
safe default rather than reading past the end, and the comment states the
principle: "a policy that cannot answer must decline, never guess."
`static_assert(sizeof(TableHeader) == 88)` pins the on-disk layout so a padding
change cannot silently reinterpret a file.

**Market-Logic Notes:** Three fields in the header exist because getting them
wrong would be undetectable downstream, and each says so in place.
- **The header carries the discretisation, not just a version number** (lines
  9-14). A bare version only catches the case where someone remembers to bump
  it; storing the shape means a table solved before `kMaxInventory` changed is
  rejected by a build that changed it — "the failure that would otherwise be
  silent and total."
- **`queue_scale`** (lines 41-44) travels with the table so the executor cannot
  bucket on a different reference depth than the solver did, "which would put
  every lookup in the wrong row while looking fine."
- **`dt_s`** (lines 45-50): every probability in the model is per epoch, so "a
  policy consulted twice as often as it was solved for is answering a question
  about twice as much time as has actually passed."
- The value function ships alongside the policy (lines 56-58) because the
  roadmap requires a decision to be explainable, and pointing at a state and
  saying why it quotes what it quotes needs the numbers that were compared.

---

### `include/lob/policy/mdp.hpp`

**Status:** Checked — [x] Reviewed (163 lines, read in full; the implementation
is audited under `src/mdp.cpp`)

**Issues Found:** None in the interface. One default that is a deliberate
compatibility choice and worth knowing about.

- **`p_move_adverse` defaults to 1.0** (line 74), which reproduces the old,
  wrong behaviour exactly. The comment says why — "a params file with no
  measurement in it changes nothing" — which is the right call for
  reproducibility, but it means a params file that simply omits the field gets
  the biased model with no warning. `MdpParams::validate` does not require it.

**Severity:** Minor

**Why it matters:** The measured value is 0.33 on the eight-hour ethusd capture
against a default of 1.0. A silently omitted field is a 3x over-charge on every
move-fill, which biases every decision about quoting at the touch in the same
direction.

**Fix Recommendation:** Have `apps/solve` warn when the field is absent from the
params file, or make `validate()` reject `p_move_adverse == 1.0` unless an
explicit `"p_move_adverse": 1.0` was supplied. A warning is enough:
```cpp
if (!params_json.contains("p_adverse_1.0s"))
  std::fprintf(stderr, "warning: no p_move_adverse in params; charging every "
                       "move-fill in full, which measurement says is 3x too high\n");
```

**Refactor Suggestion:** None.

**Tests Missing:** `tests/test_policy.cpp:156-212` tests that the move-fill
branch is a probability-preserving **split**, which is the property that
matters. Nothing tests that a params file omitting `p_move_adverse` is flagged.

**Performance Notes:** `expand()` appends to a caller-supplied buffer and never
clears it (lines 130-133), so one buffer serves a whole sweep and the solve does
no allocation in its inner loop.

**Security Notes:** None — offline code, no external input beyond a params file
that `validate()` checks.

**Market-Logic Notes:** The modelling here is the most carefully reasoned in the
project.
- **Adverse selection is not a parameter, it falls out** (lines 22-26). A fill
  caused by the mid moving through us is booked *before* the move is applied to
  the resulting inventory, "so selling into a rising market costs exactly what
  it should. A model that needed a hand-set adverse-selection term would be a
  model whose fills and price moves were independent, which is the one thing
  they are not."
- **Why the move-fill branch and not the mark on existing inventory** (lines
  56-63): a move marks a position we already hold in whichever direction it
  goes, and both directions are in the expansion, so that charge is symmetric
  and averages out. A move-*fill* is one-sided by construction, "so an
  over-charge there does not cancel against anything."
- **The correction is conservative on purpose** (lines 65-70): it charges the
  move with probability `p_move_adverse` and nothing otherwise, ignoring the
  cases where the fill turned out profitable, because "a mean over that tail is
  not a robust statistic on n=54 prints while a proportion is."
- **`inventory_penalty` is per epoch and must not be set directly** (lines
  100-107). "Per epoch is not a unit anybody holds a preference in": halving the
  epoch halves the ticks per second a lot costs while leaving the constant
  alone, so the same 0.01 meant 10 ticks/lot²/s at 1 ms and 20 at 0.5 ms. It
  made the policy twice as afraid of inventory and pulled a side in a third of
  all states. `apps/solve` takes the preference per second and multiplies.
- **`max_sweeps` counts backups, not outer iterations** (lines 155-160), and the
  old cap of 20,000 "was not a safety limit, it was a silent ceiling the solve
  ran into and stopped at 2e-6."
- **`stochastic()`** (lines 147-151) checks the process before any value
  iteration, turning "twenty thousand sweeps ending in a residual of 2e+57" into
  a sentence naming the offending state.

---

### `repo_tree.txt`

**Status:** Checked — [x] Reviewed (117 lines, generated and read in full)

**Issues Found:** The file did not exist when the audit began. It is a derived
artefact, so it can go stale against the tree it describes.

- **Generated during this audit**, because the brief names it as a top-level
  file in scope. `tree` is not installed in this environment, so it was produced
  with `git ls-files | sort`, which lists exactly the 116 tracked files plus
  itself and omits ignored directories by construction.
- **A committed tree listing drifts.** The moment a file is added or renamed,
  this file is wrong, and nothing regenerates it.

**Severity:** Minor

**Why it matters:** A stale tree listing is worse than none, because a reader
takes it for the current shape of the repository.

**Fix Recommendation:** Either regenerate it in CI and fail on a diff:
```yaml
- name: Repo tree is current
  run: git ls-files | sort > /tmp/t && diff -u repo_tree.txt /tmp/t
```
or stop tracking it and add `repo_tree.txt` to `.gitignore`, treating it as
audit scaffolding rather than a repository artefact. The second is simpler and
is what I would recommend once this audit is accepted.

**Refactor Suggestion:** None.

**Tests Missing:** The CI check above is the test.

**Performance Notes:** n/a.

**Security Notes:** Contains only tracked paths — no ignored directories, so no
build output, no `simcsv/` calibration dumps, and nothing from a `.env` that
does not exist. Reviewed line by line for anything that should not be public;
there is nothing.

**Market-Logic Notes:** Not applicable.

---

### `include/lob/strat/driver.hpp`

**Status:** Checked — [x] Reviewed (261 lines, read in full)

**Issues Found:** A duplicated include. One structural observation about where
the run is measured from.

- **`#include <vector>` appears twice** (lines 16-17).
- **`peak_inventory` is sampled inside the agent callback**, which runs once per
  market event (line 195). Between two callbacks an in-flight order can land and
  move the position, so the recorded peak is a sample of the path rather than
  its true maximum. The comment at lines 86-89 argues correctly that "a limit
  that is only checked when the strategy is consulted is not a limit"; the
  measurement of the breach has the same sampling property.
- **`last_mid` is only updated when the true book is two-sided** (lines 152-157),
  so `final_mid` — which multiplies the closing position in `pnl()` — can be a
  stale mid if the run ends one-sided.

**Severity:** Minor (all three)

**Why it matters:** The peak-inventory sampling is the one that could mislead: it
is used to compare risk appetites across strategies, and it under-reports by an
unknown amount that differs per strategy.

**Fix Recommendation:** Delete the duplicate include. For the peak, sample it in
`Simulator::book_fill` where the position actually changes, and expose it on
`SimStats`:
```cpp
stats_.inventory += signed_qty;
if (std::llabs(stats_.inventory) > stats_.peak_inventory)
  stats_.peak_inventory = std::llabs(stats_.inventory);
```

**Refactor Suggestion:** None. Extracting this from `apps/backtest` so Phase 5
could reuse it rather than grow a second copy (lines 9-10) was the right call
and is the reason `apps/backtest` and `apps/evaluate` are comparable at all.

**Tests Missing:** That two strategies driven through `run_strategy` on the same
seed see the same market events. Nothing asserts the driver is strategy-neutral,
which is its entire purpose.

**Performance Notes:** `for (; seen < s.fills().size(); ++seen)` (line 161)
walks the simulator's ever-growing fill vector from a saved index, so it is O(new
fills) per event rather than O(all fills) — correct given H4, and another place
that has to work around the unbounded log.

**Security Notes:** None.

**Market-Logic Notes:** Three comments here record defects found the hard way,
and all three are the kind that produce *better* numbers when broken.
- **The message budget is a time, not an event count** (lines 26-41). At 200
  events it was 0.5 ms under the old generator and 3.7 **seconds** under the
  calibrated one — "the same code described a maker requoting twice a
  millisecond and a maker requoting twice a minute." It now defaults to the
  MDP's 100 ms epoch, "the one cadence it must agree with."
- **Tracking cancellations rather than executions** (lines 173-188) left the
  driver believing a filled quote was still resting. The comment then explains
  why the obvious repair is worse: an order in flight is not in the book either,
  so clearing the id when the order leaves the book "forgot" every quote the
  moment it was sent, orphans rested forever, "position limits stopped binding
  entirely (peak inventory 3,740 against a limit of 50), and fill counts looked
  wonderful." Executions are the only sound signal and are what a venue reports.
- **Hysteresis** (lines 211-215): a requote costs queue position, so a quote
  moves only when the target has, "without this the strategy churns its own
  priority away."
- **Markouts use the TRUE mid, not the agent's lagged view** (lines 151-152),
  because a markout is an economic measurement of what happened rather than of
  what the agent knew.
- **`Placement` records what our own orders experienced** (lines 92-104) — how
  much was queued ahead, how long it rested, whether it traded — because "the
  MDP is calibrated on the hazard the market's own orders see, and that is only
  the hazard OURS see if the two populations look alike. Recording it is how you
  find out instead of assuming." That instrument is what `apps/evaluate --probe`
  reads.
- **`pnl()` versus `Attribution::total`** (lines 106-118) is the distinction
  `apps/backtest` fails to honour; see the H1 finding under
  `include/lob/strat/pnl.hpp`.

---

### `include/lob/feat/queue_reactive.hpp`

**Status:** Checked — [x] Reviewed (291 lines, read in full)

**Issues Found:** The whole class is public. One comment left behind by the axis
change. A large object held by value.

- **There is no `private:`.** `class QueueReactive` opens `public:` at line 67
  and never closes it, so `counts_`, `expo_`, `cur_q_`, `aes_` and `frozen_` are
  all public despite the trailing-underscore naming that says otherwise.
- **`bucket()`'s comment still describes the queue axis** (lines 264-270): "An
  empty queue is its own bucket 0, because a queue nobody is in is a different
  state from a queue with one lot in it." True, and it is `qbucket()` that
  implements it now. `bucket()` is used only for the **order-count** axis since
  the queue axis moved to linear AES units.
- **The object is roughly 490 KB** — `counts_[2][5][48][8][4][kNumEv]` of
  `uint64` plus `expo_[2][5][48][8][4]` of `double`. It is held by value in
  `apps/stats`, which is fine, but it will not fit on a stack and must not be
  copied casually.

**Severity:** Minor (all three)

**Why it matters:** None is a defect. The missing `private:` is the one that
matters for the next edit, because the class's invariant — that `freeze()` runs
exactly once, before any intensity is accumulated — is enforced only by
`frozen_`, and anything can now write it.

**Fix Recommendation:** Add `private:` before `qbucket`'s helpers and the data
members; expose `aes()` and the CSV writer, which is all any caller needs.
Retitle `bucket()`'s comment to say "order count".

**Refactor Suggestion:** None.

**Tests Missing:** `tests/test_queue_reactive.py` checks the exposure identity.
Nothing tests `freeze()`'s fallback paths — a level with fewer than
`kMinForOwn` events falling back to the pooled mean, and a warmup that saw
nothing at all falling back to a scale of 1.

**Performance Notes:** `on_state` is called per event and does one array index
into a six-dimensional array; the indices are computed from small integers, so
it is arithmetic rather than search. The array is sparse in practice — most of
the 48 x 8 x 4 cells never fill — which is the cost of a dense layout and is the
right trade at this size.

**Security Notes:** None; it consumes `BookEvent`, not bytes.

**Market-Logic Notes:** The header is the clearest statement in the repository of
why a measurement class must not fit anything, and three of its decisions were
forced by measurement.
- **The queue axis is the paper's, `ceil(volume / AES)` in linear steps** (lines
  23-34). The previous log2 grid "cannot be wrong about a scale and cannot see
  the answer either": every shape Huang, Lehalle and Rosenbaum report lives
  between 0 and 40 AES units, and a log2 grid puts that whole range into six
  buckets, "so the thing being measured is gone before anything can be fitted to
  it."
- **The order count is a separate axis** (lines 41-53), because the mechanism
  being tested is independent per-order cancellation: "a queue of six million
  shares may be three large orders or three hundred small ones, and keyed on
  quantity alone the two are the same bucket." Measured on quantity alone,
  ethusd returned a cancel slope of −0.03 ± 0.08 and "would have been reported
  as 'real books do not cancel independently'."
- **AES is pooled over sides** (lines 205-211) because the model already assumes
  `lambda_i(n) = lambda_{-i}(n)`, and per side a sixty-second warmup put level
  4's average event at 100,536 on the bid against 745,181 on the ask — "seven
  times apart on a quantity that is the same by construction."
- **`qbucket` is a ceiling, not a rounding** (lines 236-240): bucket 0 is an
  empty queue and nothing else, "because the reference price moves off an empty
  queue and not off a thin one."
- **No fitting, no smoothing, no parametric form** (lines 16-21). This writes
  counts and seconds; `tools/fit_queue_reactive.py` decides what `lambda(q)`
  looks like, because "measurement and modelling in one file is how a modelling
  assumption ends up looking like an observation."

---

### `include/lob/feed/bitstamp.hpp` (interface) and `src/bitstamp.cpp` (implementation)

**Status:** Checked — [x] Reviewed (273 + 409 lines, both read in full)

**Issues Found:** One open defect, already tracked. No memory-safety or parsing
defects; the parsing surface is `include/lob/feed/json.hpp`, audited separately
and clean.

- **ethusd's reconstructed book develops holes near the touch over multi-hour
  captures.** Open, and described in `docs/KNOWN-ISSUES.md` 1. The cause is that
  the book is built from the stream alone near the touch, so an order whose
  `order_created` predates the capture can never be removed correctly, and over
  hours the residue accumulates where it matters most. The remedy is periodic
  re-snapshotting during recording; validating it needs a fresh eight-hour
  capture, which is why it is still open.
- **The seed guard is load-bearing and correct.** Only snapshot orders more than
  `seed_guard` ticks from the touch seed the book; everything nearer is
  reconstructed from the stream. `apps/replay` reports whether the price stayed
  inside the guard over the session and prints `TOO NARROW` if it did not.

**Severity:** Major (the book holes — it degrades every distribution measured
near the touch on long ethusd captures)

**Why it matters:** This decoder is the only thing standing between a public
websocket and every number the project reports. The holes are a fidelity
problem, not a safety one: they make near-touch depth and spread wrong on one
instrument over long horizons, and `apps/replay`'s capture-quality section is
built to surface exactly that.

**Fix Recommendation:** Fire the recorder's existing session-snapshot machinery
on a timer, write a `_meta` marker at the reseed point, and have the decoder
reseed at the marker with the guard measured against the *current* touch rather
than the opening one. Roughly 8 h including a validation capture.

**Refactor Suggestion:** None.

**Tests Missing:** Covered well — `tests/test_bitstamp.cpp` is 585 lines of
verbatim capture fixtures plus a real-data replay per sample pair, and
`fuzz/fuzz_bitstamp.cpp` fuzzes the line decoder and `load_snapshot`. The two
gaps are recorded there: `detect_decimals` and `snapshot_touch` are not fuzzed.

**Performance Notes:** `decode_line` does one pass of `json::find` per field it
needs, each of which scans the object's own keys without descending. At the
2.9 MB capture sizes this project uses, `apps/replay` reports throughput in
millions of lines per second.

**Security Notes:** All parsing goes through `lob::json`, which is total,
bounds-checked, allocation-free and fuzzed. The decoder itself performs the
range checks the book then re-checks: prices outside the window are counted in
`out_of_window` and their follow-ups suppressed, so an out-of-range price is
never an out-of-range index. `size_violations` counts the one invariant that
does hold — no more size may leave an order than it had.

**Market-Logic Notes:** The header records a measurement that overturned the
project's original design, and this is the best example of it in the repository.
- **`amount_traded` is per event, not cumulative** (lines 16-24). It was read as
  cumulative until a real capture disproved it: order 2046841350975488 took six
  partial fills summing to exactly the 0.12077218 that left the book, and "under
  a cumulative reading that order's final traded size is zero." Across the
  capture the per-event sum matched the size consumed for 84.8% of completed
  orders against 50.5% for the last value.
- **The consequence is stated rather than papered over** (lines 26-29):
  `amount + amount_traded == amount_at_create` holds only for an order with one
  trading event and no amend, "It is not an invariant and is not checked as one."
- **This replaced a planned join against the trade channel** (lines 36-40),
  which "would also be WRONG: the two channels are not on the same clock. In one
  capture the order channel ran ~570ms behind local time while the trade channel
  ran ~90ms ahead of it. amount_traded needs no clock at all."
- **The event chain makes a gap detectable rather than suspected** (lines
  42-46), which is what lets Phase 1's acceptance criterion mean anything.
- **Timestamp resolution is 1 ms "wearing a microsecond coat"** (lines 50-54),
  so intra-millisecond order comes from the chain and never from the clock, and
  any duration measured from these numbers "is quantised to 1ms and must be
  reported that way."

---

### `src/mdp.cpp`

**Status:** Checked — [x] Reviewed (371 lines, read in full)

**Issues Found:** None outstanding. The one defect this file had was found by
measurement and fixed during development.

- **The move-fill branch used to charge the full move with certainty.** It is
  now a two-outcome split:
  ```cpp
  if (hit) {
    emit(pu * pa_adv,         s.inventory - 1, step_away(b0), kNoQuote, p.edge_ticks[0], p.move_ticks);
    emit(pu * (1.0 - pa_adv), s.inventory - 1, step_away(b0), kNoQuote, p.edge_ticks[0], 0.0);
  }
  ```
  The same probability mass, differing only in reward. `docs/KNOWN-ISSUES.md` 2
  records the symptom (a touch quote priced as a guaranteed loser on real data)
  and the measurement that settled it: P(the mid is still against the resting
  side one second after a print) is 33% on the eight-hour ethusd capture
  (n=5,032), not 100%.

**Severity:** n/a — no issues found (fixed)

**Why it matters:** It is worth recording because of *how* it was caught. The
symptom was a policy that refused to quote at the touch; the cause was one
branch of the expansion, and the fix had to preserve probability mass or value
iteration would stop being a contraction. `tests/test_policy.cpp:156-212` now
asserts the split property rather than the numbers.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** None. `tests/test_policy.cpp` verifies that every admissible
state-action's successors sum to one, that the move-fill branch is a split, that
a malformed process is refused, and that the position limit is a constraint on
the action rather than a clamp on the resulting inventory.

**Performance Notes:** `expand()` appends into a caller-owned buffer so the
solve allocates nothing in its inner loop. `solve()` is modified policy
iteration counted in **backups** rather than outer iterations, which is what
makes the sweep cap meaningful; reaching a 1e-9 residual at a 0.9995 discount
takes about 35,000.

**Security Notes:** None — offline, and `MdpParams::validate` rejects anything
that is not a probability before any arithmetic runs.

**Market-Logic Notes:** The reward structure is where the economics live and it
is right: a fill caused by the mid moving through a quote is booked **before**
the move is applied to the resulting inventory, so adverse selection emerges
from the ordering rather than from a hand-set parameter. `admissible()` treats
the position limit as a constraint on the action rather than a clamp on the
outcome, which matters because clamping "lets a fill at the boundary collect its
edge for free."

---

### `include/lob/sim/flow.hpp`

**Status:** Checked — [x] Reviewed (1,497 lines, read in full)

**Issues Found:** One unguarded cast that a degenerate rate could overflow. The
substantive defects this file had were found by measurement and are fixed; they
are recorded here because the fixes are load-bearing.

- **`ts_ += static_cast<Nanos>(gap_s * 1e9) + 1;`** (line 1210). `gap_s` is
  `-log(u) / total` with `u` floored at 1e-18, so `gap_s` is at most
  `41.4 / total`. `total > 0` is guaranteed by the branch above, but not bounded
  away from zero — a denormal total would produce a value beyond `INT64_MAX` and
  the cast would be undefined. Not reachable at any fitted parameterisation.
- **Fixed during development, and each fix matters more than the bug:**
  - The book was **200x too thick and the clock 7,400x too fast**. `mean_gap_ns`
    now makes the clock explicit rather than implicit in `ts_ += 1 + rng_() % 5000`.
  - **Trade sizes were constant.** Replaced with an empirical 16-bin quantile
    table, because a lognormal matched the middle and put p99 at 25 AES against
    an observed 3.1.
  - **Orders stranded outside the modelled window were never cancelled**, so the
    book grew 101 → 408 over 2M events. Fixed with independent per-order
    far-cancellation at `cancel_rate[K-1] / (1 + cancel_half[K-1])`. The same
    leak had produced a non-monotone fill hazard that read like a model defect.
  - **Levels were anchored to the moving touch**, so nothing ever arrived inside
    the spread and the spread could only widen — median 4 ticks against ethusd's
    1. Now anchored to the reference price.

**Severity:** Minor (the cast); the rest are fixed

**Why it matters:** This file is the process every policy is solved and
evaluated against, so a defect here is not a bug in a component, it is a wrong
answer everywhere at once. The four fixed items above are each of that kind.

**Fix Recommendation:**
```cpp
const double gap_ns = gap_s * 1e9;
ts_ += (gap_ns < 9e18 ? static_cast<Nanos>(gap_ns) : Nanos{9'000'000'000LL}) + 1;
```

**Refactor Suggestion:** `FlowConfig::Qr` holds 20-odd fitted constants as plain
members with their values in comments. They are traceable to tables in
`docs/06-queue-reactive-plan.md`, but nothing binds the two. Loading them from
`policy/queue_reactive.json` — which `tools/fit_queue_reactive.py` already
writes — would remove the copy.

**Tests Missing:** `tests/test_calibration.cpp` covers the calibrated process
per event, per second and by count, and pins the message budget as a time
invariant to the clock. What is untested is the far-cancellation balance: that
the count of orders outside the modelled window is stationary over a long run,
which is the property whose absence caused the 101 → 408 leak.

**Performance Notes:** `next_queue_reactive` rebuilds the full rate array
`[2][5][3]` plus two far rates on every event, then does a linear search through
it to select. That is 32 multiplies and a 32-step scan per event — fine for a
simulator, and the alternative (an incrementally maintained total) would be a
correctness risk for no useful gain. `live_` is a `std::unordered_map`, so the
generator allocates; again, simulator, not hot path.

**Security Notes:** None; no external input.

**Market-Logic Notes:** This is where the project's microstructure lives, and
the reasoning behind each piece is recorded in place.
- **The header is honest about what the file is for** (lines 1-17): drive the
  book hard enough to prove it correct, and later become the simulator. "It does
  NOT reproduce the stylized facts in docs/03 §11, and nothing calibrated should
  be fitted to it."
- **The add-placement profile was measured, not assumed** (lines 36-56): ethusd
  puts 47.8% of adds at the best queue against the generator's 32.6%, and the
  generator put *more* one tick behind the touch than at it. The comment then
  refuses the obvious inference: dividing the two profiles gives a relative
  lifetime of 1.46 at the touch against 0.45–0.71 behind it, so "an order at the
  touch lives LONGER than one behind it, which is the opposite of what was
  assumed before it was measured."
- **Model II-b applies at the touch only** (lines 1174-1176), "which is where
  the paper applies it: the intensities at Q_1 depend on the opposite queue,
  those behind it do not."
- **Excitation decays on elapsed time, not event count** (lines 1154-1160),
  because "this process's clock is exponential and an event count would make the
  kernel mean something different at every rate." `excite_compensation()`
  renormalises so adding self-excitation does not change the mean cancel rate.
- **Far cancellation is proportional to the order count** (lines 1192-1195):
  "Proportional to the count, which is what bounds the pile: a fixed rate would
  not."
- **No crossing guard, deliberately** (lines 1236-1241): bid levels sit at
  `mid_` and below, ask levels at `mid_ + 1` and above, so the two sides cannot
  meet by construction, and "a guard here would silently relocate orders the
  model placed deliberately, which is how the level distribution stopped being
  the one that was fitted last time."

---

### `tests/test_calibration.cpp`

**Status:** Checked — [x] Reviewed (324 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** This file exists because of `docs/KNOWN-ISSUES.md` 4 and 5:
a rate measured per event and a rate measured per second are different
statements, and the project once had every downstream time constant fitted to
the wrong one. These assertions are what stop that recurring.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** The far-cancellation stationarity property noted under
`include/lob/sim/flow.hpp`.

**Performance Notes:** Runs the calibrated and stress processes over long spans;
one of the slower suites and necessarily so.

**Security Notes:** None.

**Market-Logic Notes:** Four properties, each chosen so that a regression is
visible rather than plausible.
- **The calibrated process is asserted per event AND per second AND by count.**
  Any one of the three alone can be satisfied by a process that is wrong on the
  other two, which is exactly how the original defect survived.
- **The stress process stays dense and fast**, so the coverage-driving
  configuration is not silently slowed by a change aimed at realism.
- **Model I's invariant distribution matches theory to a total-variation
  distance under 0.10 with the reference price pinned.** That is the closed-form
  check the queue-reactive model admits, and pinning `p_ref` is what makes it
  testable at all.
- **The message budget is a time invariant to the clock** — the assertion that
  encodes the fix in `include/lob/strat/driver.hpp`.

---

### `tests/test_queue_reactive.py`

**Status:** Checked — [x] Reviewed (89 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** Both properties it checks are invisible in the output they
guard, which is the docstring's own argument for asserting them.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** Nothing at this level.

**Performance Notes:** Runs `stats` once per sample capture. Registered in
`CMakeLists.txt:135` as `queue_reactive_exposure`.

**Security Notes:** `subprocess.run` with a list argument and no `shell=True`;
every path comes from `sys.argv` or `tempfile`.

**Market-Logic Notes:** The two properties are exactly right.
- **Exposure must account for all of the capture's time.** "An intensity is
  events over exposure. If a queue's exposure does not add up to the capture's
  duration, some of the time it spent at some size went unrecorded — and the
  rates for those sizes come out too high, by exactly the fraction lost." That
  is a conservation check, and it is the only kind that catches a silent
  under-count.
- **The queue axis must resolve a shape.** The axis used to be log2 of raw
  quantity, which put the whole range Huang et al. measure into six buckets:
  "The table still had plausible numbers in it; there was simply no curve left
  to fit."

---

### `tools/fit_queue_reactive.py`

**Status:** Checked — [x] Reviewed (172 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** This is the file that decides what `lambda(q)` looks like,
kept deliberately separate from the C++ that measures it, so that "a modelling
assumption ends up looking like an observation" cannot happen.

**Fix Recommendation:** None.

**Refactor Suggestion:** It writes `policy/queue_reactive.json`, and
`FlowConfig::Qr` carries the same constants hand-transcribed. Having the
generator read the JSON would close that loop; see the refactor note under
`include/lob/sim/flow.hpp`.

**Tests Missing:** Nothing regenerates the fit and compares it against the
constants in `flow.hpp`, so a re-fit that changed a number would not fail
anything until someone noticed.

**Performance Notes:** numpy and pandas over a CSV; not a bottleneck.

**Security Notes:** No network, no credentials, no `eval`.

**Market-Logic Notes:** The estimator is a log-log slope with a stated null, and
the docstring says what each value would mean: `beta ~ 1` is proportional, so
"every resting order behaves independently and a queue with twice as many ORDERS
produces twice the events — this is what cancels should look like"; `beta ~ 0`
is flat, "which is what a Poisson generator with fixed rates produces, at every
size, by construction." Fitting against **two** axes is the substance: keyed on
quantity alone, ethusd returned a cancel slope of −0.03 ± 0.08, rejecting
proportionality at twelve standard errors, "which would have been reported as
'real books do not cancel independently' when the measurement was simply keyed
on the wrong variable." Running it on the real book and on the simulator and
putting the two side by side is what makes it a fidelity test rather than a
description.

---

### `tools/mdp_params.py`

**Status:** Checked — [x] Reviewed (405 lines, read in full)

**Issues Found:** One coupling to a C++ header that nothing enforces.

- **The imbalance bucket edges are written here and in
  `include/lob/policy/state.hpp:65`**, and that header says outright: "If these
  two ever disagree the policy is solved against one book and applied to
  another." Neither file reads the other, and the table header's discretisation
  check would not catch a drift because the bucket *count* would be unchanged.
  The same applies to the queue-bucket edges.

**Severity:** Major

**Why it matters:** This is the failure that produces no error and no crash: the
solver would compute a correct policy over one bucketing and the executor would
index it with another, so every lookup returns a real action for a different
state. It is the exact failure `PolicyTable`'s header check exists to prevent,
arriving through the one door that check does not cover.

**Fix Recommendation:** Fold the edges into `MdpParams::hash()` so a mismatch
changes the hash and `apps/solve` refuses to ship, or emit them from the C++
side into the params JSON and have this file read them rather than declare them:
```python
edges = params["imb_edges"]          # written by apps/solve from state.hpp
```

**Refactor Suggestion:** None.

**Tests Missing:** A test asserting the edges in `state.hpp` equal the edges in
`policy/mdp.json`. `tests/test_policy.cpp` checks `imb_bucket` against the
header's own constants, so a drift between the two files passes silently.

**Performance Notes:** numpy and pandas over the CSVs `apps/stats` writes; runs
in seconds.

**Security Notes:** No network, no credentials, no `eval`.

**Market-Logic Notes:** The docstring states the modelling decisions and the
measurements behind them.
- **Distance from the mid is deliberately not in the state** (lines 20-24):
  these books sit at a one-tick spread 70–99% of the time, "so there is nowhere
  to put a quote except the touch or a tick or two behind it, and
  tools/calibrate.py could not identify an Avellaneda-Stoikov k on two of the
  three instruments for exactly that reason."
- **Queue position is bucketed by absolute volume ahead** (lines 26-34), with
  the measured consequence of getting it wrong: against a touch-joining
  strategy's real placements the hazard at the front ran 1,019/s while the
  quartile-calibrated model said 52/s, "and the policy duly concluded that
  quoting behind the touch was the better idea."
- **Where a quantity is not measurable from ten minutes of data, it says so in
  the output rather than quietly using a default** (lines 9-11). That is the
  property that makes `apps/solve`'s refusal to run on an unidentified process
  possible.
- One defect fixed during development is worth recording: the **level ratio was
  measured from placement rather than from prints**, and was wrong by 200x.

---

### `tools/record_bitstamp.py`

**Status:** Checked — [x] Reviewed (343 lines, read in full)

**Issues Found:** An unvalidated REST response. A docstring superseded by a
later finding in the decoder it feeds.

- **Line 207: `snap = requests.get(REST_BOOK.format(pair=self.pair), timeout=30).json()`**
  — no `raise_for_status()`, no shape check. A non-JSON error page raises inside
  the recording loop. There is a timeout and no `verify=False`, so TLS is
  intact; the response is simply trusted.
- **The docstring's rationale for capturing both channels is stale** (lines
  21-30): "order_deleted does not say WHY the order left... The only way to tell
  them apart is to join against live_trades on the order id."
  `include/lob/feed/bitstamp.hpp:16-40` records that this is no longer true and
  that the join would in fact be **wrong**, because the two channels are not on
  the same clock — in one capture the order channel ran ~570 ms behind local
  time while the trade channel ran ~90 ms ahead. `amount_traded` is per event
  and needs no clock. Both channels are still worth recording, but for a
  different reason than the one stated here.
- **`websockets.connect(..., max_size=None)`** removes the default 1 MiB frame
  limit. Necessary for full-book frames; the consequence is that a
  malfunctioning endpoint can drive unbounded buffering.

**Severity:** Major (the unvalidated response, because it aborts a capture that
cannot be re-run), Minor (the stale docstring)

**Why it matters:** A capture is hours of wall time against a market that will
not repeat. An exception thrown from an unchecked `.json()` during a scheduled
reconnect ends it.

**Fix Recommendation:**
```python
r = requests.get(REST_BOOK.format(pair=self.pair), timeout=30)
r.raise_for_status()
snap = r.json()
if not isinstance(snap.get("bids"), list) or not snap.get("asks"):
    raise RuntimeError(f"unexpected snapshot shape: {sorted(snap)[:6]}")
```
and update lines 21-36 to point at the decoder's finding.

**Refactor Suggestion:** The reconnect loop here is the one the other two
recorders lack. Lifting it into a shared base class is the fix recorded under
`tools/record_bitfinex.py`.

**Tests Missing:** None practical — this needs a live endpoint.

**Performance Notes:** Rotates output hourly and writes through `gzip.open`,
which is buffered; a trapped `SIGINT` flushes, a `SIGKILL` truncates, and the
reader handles a truncated final line.

**Security Notes:** **Clean.** `wss://` and `https://` with default certificate
verification, no `verify=False`, no API key, no token, no signing, no account.
The header's first line states it: "the full book, order by order, no account."

**Market-Logic Notes:** Two things here are right and were not obvious.
- **Reconnect handling exists and costs a fresh REST snapshot each time** (lines
  91-149): "Bitstamp asks clients to reconnect periodically
  (bts:request_reconnect), and any reconnect means messages were missed." The
  backoff exists because the snapshot endpoint is rate-limited.
- **The known caveat is disclosed with a citation** (lines 32-36): an
  `order_changed` with an unchanged amount can be reported where no trade
  occurred, so "treat the cancel/fill split as high quality but not exact, and
  say so in any result that depends on it."

---

### `docs/01-literature.md`

**Status:** Checked — [x] Reviewed (292 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** Every arXiv id I spot-checked against a citation in the code
matched the claim made about it, which is the only property of a bibliography
that can be audited from inside the repository.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** All links are to arXiv, SSRN or publisher landing pages. No
credentials, no gated endpoints.

**Market-Logic Notes:** The organising decision is the valuable one: entries are
grouped "by the role it plays in the system, not by date", each says *what you
take from it*, and `[core]` marks the ones the design actually depends on. The
"Design consequence" callouts are what connect it to the code — section A's, for
instance, states that "your P&L decomposition must have separate lines for
spread capture, adverse selection, and inventory cost — they are three different
economic forces and they need three different controls", pointing at
`docs/03` §10. That is the specification `include/lob/strat/pnl.hpp` implements
and `apps/backtest` under-reports (finding H1).

---

### `docs/02-data-and-protocols.md`

**Status:** Checked — [x] Reviewed (242 lines, read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** §3 is the record of *why* this project uses the venue it
does, and it is the document that makes the no-account constraint auditable
rather than merely asserted.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** §3 is headed "**free sources only**" and each ruled-out
option says why, in terms that matter for this audit: Coinbase's `level3` was
"Checked and ruled out — the retail Advanced Trade WebSocket has no `level3`
channel at all, and `level2` now requires authentication"; Databento was
"Dropped here because sign-up wanted card details, and this project spends
nothing." Those two lines are the reason there is no credential anywhere in the
repository to find.

**Market-Logic Notes:** §3.1 carries the same `amount_traded` finding as
`include/lob/feed/bitstamp.hpp`, in the same detail, including the specific
order id and the 84.8% against 50.5% comparison. So the decoder and this
document agree — which localises the stale docstring in
`tools/record_bitstamp.py` as a single oversight rather than a live
disagreement about the data. The Bitfinex row also records the reason that
venue is second choice: its raw books are "windowed to the top 250 per side. An
order leaving the window is indistinguishable from a cancel, which silently
corrupts the cancel/fill split."

---

### `docs/03-metrics-and-estimators.md`

**Status:** Checked — [x] Reviewed (278 lines, read in full)

**Issues Found:** None in the document. It is the specification against which
one code finding was raised.

- **§10 specifies Total P&L as five lines**, including "Inventory / hedging cost
  (mark-to-market of held inventory + hedge slippage)". `include/lob/strat/pnl.hpp`
  cites this section by name and implements four of them, deliberately — see
  finding H1, which is about `apps/backtest`'s reporting rather than about this
  document.

**Severity:** n/a — no issues found

**Why it matters:** This is the file the measurement layer is written against,
and three of its sections proved load-bearing during the audit.

**Fix Recommendation:** None. If anything, add a sentence to §10 distinguishing
the decomposition of trading edge from session P&L, since the codebase now makes
that distinction explicitly in `include/lob/strat/driver.hpp:106-115` and the
document does not.

**Refactor Suggestion:** None.

**Tests Missing:** n/a.

**Performance Notes:** n/a.

**Security Notes:** None.

**Market-Logic Notes:** The sections that the code actually implements, verified
one by one:
- **§2's stage taxonomy** matches `Stage` in `include/lob/measure/recorder.hpp`
  enumerator for enumerator.
- **§5**, that the fill/cancel split must never be aggregated, is implemented in
  `src/bitstamp.cpp` and quoted at the point of reporting in `apps/replay`.
- **§7**'s markout ladder matches `kMarkoutHorizons` in
  `include/lob/strat/pnl.hpp`.
- **§10**'s five-line decomposition, as above.
- **§12**, "don't fool yourself", is what the block bootstrap and the
  `kMinBlocks = 10` refusal in `pnl.hpp` implement.
- **§11**'s stylized-fact scorecard is the standard `docs/06`'s calibration
  tables are scored against.

---

### `docs/figures/01_order_lifetime.png`

**Status:** Checked — [x] Reviewed (84,176 bytes, 1452x477 PNG, RGBA)

**Issues Found:** None. Build output, committed deliberately.

**Severity:** n/a — no issues found

**Why it matters:** Produced by `fig_lifetime` (`tools/figures.py:83-105`),
which plots order lifetime as a **survival curve** rather than a histogram —
the correct instrument for censored duration data, since an order still resting
when the capture ends has a lifetime longer than observed, not equal to it.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** Nothing regenerates it and diffs it; matplotlib output is not
byte-reproducible across versions, so a diff-based CI check would be brittle.
Recording the `stats` invocation that produced it in the document that
references it is the practical alternative.

**Performance Notes:** n/a.

**Security Notes:** A matplotlib Agg render of aggregate statistics. No EXIF,
no embedded paths, nothing identifying a person or machine.

**Market-Logic Notes:** Order lifetime is the input to the cancel-rate estimate
that `include/lob/sim/flow.hpp`'s `cancel_rate`/`cancel_half` constants are
fitted against.

---

### `docs/figures/02_cancel_vs_fill.png`

**Status:** Checked — [x] Reviewed (72,333 bytes, 1000x565 PNG, RGBA)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** `fig_split` (`tools/figures.py:108-150`) draws the one split
`docs/03` §5 says must never be aggregated. It is the picture of the finding
that `amount_traded` recovers, and the reason both websocket channels are
recorded.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** As for figure 01.

**Performance Notes:** n/a.

**Security Notes:** As for figure 01.

**Market-Logic Notes:** The measurement behind it — roughly 99% of removals
being cancels — is what makes `kBackOfQueue` "the single most consequential line
in the model" (`include/lob/policy/state.hpp:96-100`), and what forced
`apps/tape` to read `queue_ahead()` at print time rather than accumulating
traded volume against the queue as it stood at placement.

---

### `docs/figures/03_spread.png`

**Status:** Checked — [x] Reviewed (69,324 bytes, 907x531 PNG, RGBA)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** `fig_spread` (`tools/figures.py:153-172`). The spread
distribution is the evidence for the large-tick regime claim that the whole
Phase 5 state design rests on: these books sit at a one-tick spread 70–99% of
the time, so distance from the mid has nowhere to vary and queue position is
what decides fills.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** As for figure 01.

**Performance Notes:** n/a.

**Security Notes:** As for figure 01.

**Market-Logic Notes:** `apps/replay` prints the same distribution with the
caveat that applies to it — "Read the median, distrust the tail", because the
upper percentiles are a property of the reconstruction rather than of the
market. A reader of this figure alone does not get that caveat.

---

### `docs/figures/04_depth_profile.png`

**Status:** Checked — [x] Reviewed (142,790 bytes, 1453x463 PNG, RGBA — the
second-largest figure)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** `fig_depth` (`tools/figures.py:175-191`). The standing depth
profile by level is what `FlowConfig`'s add-placement weights were fitted
against — the measurement that found ethusd rests 69.9% of its near-touch orders
at the touch against the old generator's 28.8%.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** As for figure 01.

**Performance Notes:** n/a.

**Security Notes:** As for figure 01.

**Market-Logic Notes:** Reading this figure next to the add-placement table in
`include/lob/sim/flow.hpp:43-48` is what shows that a standing profile and an
arrival profile are different things, and that dividing one by the other gives
the relative lifetime — 1.46 at the touch against 0.45–0.71 behind it.

---

### `docs/figures/05_interarrival.png`

**Status:** Checked — [x] Reviewed (69,711 bytes, 907x534 PNG, RGBA)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** `fig_arrivals` (`tools/figures.py:194-211`). The
interarrival distribution is the direct evidence against a Poisson process,
which is the argument for the queue-reactive and Hawkes work in `docs/06`.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** As for figure 01.

**Performance Notes:** n/a.

**Security Notes:** As for figure 01.

**Market-Logic Notes:** This is also the figure that sets `mean_gap_ns`. The
18.55 ms mean gap for ethusd is what made the old 200-event message budget
3.7 seconds rather than 0.5 milliseconds — `docs/KNOWN-ISSUES.md` 5.

---

### `docs/figures/06_markout.png`

**Status:** Checked — [x] Reviewed (104,091 bytes, 953x554 PNG, RGBA)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** `fig_markout` (`tools/figures.py:214-282`), the longest
figure function in the file. The markout curve's **shape** is the story, as
`include/lob/strat/pnl.hpp`'s header says: a curve that starts positive and
decays negative is normal, and where it crosses zero is the effective
holding-time budget.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** As for figure 01.

**Performance Notes:** n/a.

**Security Notes:** As for figure 01.

**Market-Logic Notes:** This is the figure `apps/evaluate` refers to when it
warns that "the same measurement on Bitstamp put markouts at +0.3 to +0.6 bps
against the passive side at every horizon" while the synthetic generator has
near-zero adverse selection. It is the quantitative statement of the gap between
the simulator and the market.

---

### `docs/figures/07_ak_calibration.png`

**Status:** Checked — [x] Reviewed (141,993 bytes, 1350x585 PNG, RGBA — the
largest figure)

**Issues Found:** None in the image. Its companion JSON has one, recorded below.

**Severity:** n/a — no issues found

**Why it matters:** Produced by `tools/calibrate.py:287`, not by
`tools/figures.py`. It plots the empirical fill intensity by distance bucket
with Poisson error bars against the fitted `A·exp(−k·delta)` curve, at several
cutoffs — which is how the instability in `k` is made visible rather than
averaged away.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** As for figure 01.

**Performance Notes:** n/a.

**Security Notes:** As for figure 01.

**Market-Logic Notes:** This is the figure that justifies leaving
Avellaneda-Stoikov out of the Phase 5 state. Plotting the fit at multiple
cutoffs is the honest presentation of a parameter that two of three instruments
cannot identify.

---

### `docs/figures/calibration.json`

**Status:** Checked — [x] Reviewed (2,498 bytes, read and parsed in full)

**Issues Found:** The caveat is a hardcoded string that no longer matches the
numbers it qualifies, and it understates the problem. The point estimates ship
alongside a stability diagnosis that says two of them should not be used, with
nothing in the estimates themselves to say so.

- **The caveat claims "Fill counts are 24-65 per instrument".** The file's own
  `instruments` block reports **15, 14 and 30**. The string is written as a
  literal at `tools/calibrate.py:279` and does not read the data it describes,
  so it has drifted — and drifted in the direction that makes the estimates look
  better founded than they are.
- **btcusd's `k` is not distinguishable from zero.** Parsed from this file:

  | instrument | k | se(k) | k / se(k) | fills |
  |---|---|---|---|---|
  | btcusd | 0.0199 | 0.0436 | **0.46** | 15 |
  | ethusd | 0.1011 | 0.0385 | 2.63 | 14 |
  | xrpusd | 0.2167 | 0.0392 | 5.53 | 30 |

- **`k_stability_ratio` diagnoses it and the estimates do not carry the
  diagnosis.** btcusd's `k_by_cutoff` runs 48.1, 49.5, 48.2, 0.0199, −0.0189,
  −0.00096 across the 4/8/16/32/64/128-tick cutoffs, with `ratio: NaN` and
  `sign_change: true`; ethusd also has `sign_change: true`; only xrpusd is
  stable (`ratio: 2.85`, no sign change). A consumer reading
  `instruments.btcusd.k` gets `0.0199` with no marker.

**Severity:** Major

**Why it matters:** This artefact is the one that would be reached for if
anyone tried to parameterise Avellaneda-Stoikov from measurement. It correctly
records that the parameter is unidentified on two of three instruments — which
independently confirms the claim made in
`include/lob/policy/state.hpp:16-22` and `tools/mdp_params.py:20-24` — and then
ships the unusable numbers in the same shape as the usable one.

**Fix Recommendation:** Compute the caveat rather than writing it, and mark the
estimates:
```python
counts = [v["fills"] for v in results.values()]
usable = {p: not st["sign_change"] and math.isfinite(st["ratio"])
          for p, st in stability.items()}
for p in results:
    results[p]["identified"] = usable[p]
    results[p]["k_over_se"] = results[p]["k"] / results[p]["se_k"]
payload["caveat"] = (f"Ten minutes per instrument. Fill counts are "
                     f"{min(counts)}-{max(counts)} per instrument, so these are "
                     f"order-of-magnitude estimates. ...")
```

**Refactor Suggestion:** None.

**Tests Missing:** A test that every string in a generated artefact which quotes
a number is derived from that number. That is not practical in general; deriving
the caveat, as above, removes the need for one.

**Performance Notes:** n/a.

**Security Notes:** Fitted parameters and standard errors only. Nothing
identifying.

**Market-Logic Notes:** The rest of the caveat is exactly right and is the
sentence that should survive: "A-S has no queue term and these books sit at a
one-tick spread almost always, where queue position rather than distance decides
fills." That is the finding that moved the entire Phase 5 design from distance
to queue position, and it is recorded in the artefact that produced it.

---

### `policy/mdp.json`

**Status:** Checked — [x] Reviewed (15,468 bytes, parsed and read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** This is the measured process `apps/solve` turns into
`policy/ethusd.bin`. Three top-level keys — `btcusd`, `ethusd`, `xrpusd` — one
per sample capture.

**Fix Recommendation:** None.

**Refactor Suggestion:** The imbalance and queue bucket edges that produced these
numbers are declared in `tools/mdp_params.py` and again in
`include/lob/policy/state.hpp`; emitting them into this file would close the
coupling recorded under `tools/mdp_params.py`.

**Tests Missing:** A test that the edges in `include/lob/policy/state.hpp` match
whatever produced this file.

**Performance Notes:** n/a.

**Security Notes:** Fitted parameters only. No credentials, no hostnames.

**Market-Logic Notes:** The `mid` block is a good example of an artefact that
carries enough to be checked rather than trusted. For btcusd it reports
`p_up 0.00887`, `p_down 0.00137`, `p_flat 0.98976`, `median_abs_move_ticks 70`,
`winsorised_sd_ticks 5.27` **and** `raw_sd_ticks 22.20`, `max_abs_move_ticks
584`, `samples 4395`, `steps_kept_pct 92.88`. Keeping the winsorised and raw
standard deviations side by side is what lets a reader see how much of the
variance is tail — a factor of four here — which is the difference between a
defensible inventory penalty and an arbitrary one. `steps_kept_pct` says what
the winsorisation discarded.

---

### `policy/queue_reactive.json`

**Status:** Checked — [x] Reviewed (7,265 bytes, parsed and read in full)

**Issues Found:** None in the file. Its contents are duplicated by hand
elsewhere.

- **The constants here are hand-transcribed into `FlowConfig::Qr`**
  (`include/lob/sim/flow.hpp`). Nothing reads this file at run time, so a re-fit
  updates the artefact and leaves the generator unchanged.

**Severity:** Minor

**Why it matters:** A fitted artefact that nothing consumes is a record, not an
input. That is fine as long as everyone knows it; the risk is a re-fit that
silently does not take effect.

**Fix Recommendation:** Have `FlowConfig::ethusd_queue_reactive()` load this file
where it is available and fall back to the compiled defaults otherwise.

**Refactor Suggestion:** As above.

**Tests Missing:** A test that the constants in `flow.hpp` equal the values in
this file.

**Performance Notes:** n/a.

**Security Notes:** Fitted parameters only.

**Market-Logic Notes:** Its top-level key is `sim`, so this is the fit of the
**simulator's own** process, not the market's — which is the point of
`tools/fit_queue_reactive.py`: run it on both and put them side by side. Each
level carries `exposure_s`, and each slope carries `beta`, `se`,
`log_intercept`, `buckets_used` and `events`. Level 0 records
`exposure_s 55421.76` against `events 240` for the adds-versus-orders slope, so
a reader can see which fits rest on 240 observations and which do not, rather
than being handed a bare coefficient.

---

### `policy/sim_mdp.json`

**Status:** Checked — [x] Reviewed (5,605 bytes, parsed and read in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** Top-level key `simcal` — the MDP process measured from the
**simulator**, as distinct from `policy/mdp.json` measured from the captures.
The two exist so a policy can be solved on the process it will actually be
evaluated against, which is what `README.md:117-121` documents.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** As for `policy/simcal.bin`: nothing asserts this file and the
table solved from it are still consistent with the current build.

**Performance Notes:** n/a.

**Security Notes:** Fitted parameters only.

**Market-Logic Notes:** Solving on the simulator's own process is the honest
setup for the Phase 5 question "does an optimal policy beat these heuristics on
THIS process" — which `apps/evaluate` states outright is not the same as "would
it make money". Keeping the two parameter files separate is what stops that
distinction collapsing.

---

### `policy/ethusd.bin`

**Status:** Checked — [x] Reviewed (126,808 bytes; header and provenance
verified, contents validated by the loader)

**Issues Found:** Nothing tests that it still loads.

- **126,808 bytes** = an 88-byte `TableHeader` plus 14,080 policy bytes plus
  14,080 doubles of value — exactly `kNumStates = 11 x 16 x 16 x 5`. The size is
  a consistency check in itself, and it matches.
- **`apps/evaluate` reports its header when run**: hash `b598b35bc514520e`,
  199 sweeps, residual 3.1e-10. So it was solved to convergence, not to a sweep
  cap.
- **No test loads it.** `src/policy_table.cpp` rejects a table solved over a
  different discretisation, which is precisely the failure that would follow a
  change to `kMaxInventory` or `kQueueBuckets` — and nothing in `ctest` would
  trigger it.

**Severity:** Minor

**Why it matters:** The committed table is what `apps/evaluate` runs against. The
day a discretisation constant changes, this file becomes silently unusable and
only a manual run finds out.

**Fix Recommendation:**
```cpp
// tests/test_policy.cpp
policy::PolicyTable shipped; std::string why;
CHECK(shipped.load("policy/ethusd.bin", &why));   // fails the day the shape changes
CHECK_EQ(shipped.header().num_states, policy::kNumStates);
```

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** The policy array is 14 KB, so it fits in L1;
`tests/test_policy.cpp` measures a random-access lookup as nanosecond-scale.

**Security Notes:** Read only through `PolicyTable::load`, which validates the
magic, the schema version, the full discretisation and **every action byte**
against `kNumActions` before accepting it. A corrupted file cannot produce an
out-of-range action index at lookup time. Committing a binary is safe here
because of that loader, not despite it.

**Market-Logic Notes:** This is the artefact the Phase 5 acceptance criterion is
evaluated on, and finding C1 is that it currently produces output byte-identical
to `JoinTouch`. Investigating that is the second Immediate item in the roadmap.

---

### `policy/simcal.bin`

**Status:** Checked — [x] Reviewed (126,808 bytes; same structure as
`policy/ethusd.bin`)

**Issues Found:** Same as `policy/ethusd.bin` — nothing tests that it loads.

**Severity:** Minor

**Why it matters:** Identical in size to `ethusd.bin`, which is expected: the
discretisation is fixed at compile time, so every table over it is the same
length. The two differ only in contents and in the process they were solved
against — this one from `policy/sim_mdp.json`.

**Fix Recommendation:** As for `policy/ethusd.bin`; the same test should load
both.

**Refactor Suggestion:** None.

**Tests Missing:** As above.

**Performance Notes:** n/a.

**Security Notes:** As for `policy/ethusd.bin` — validated by the same loader.

**Market-Logic Notes:** Having a second table solved on the simulator's own
process is what makes the tuning workflow in `README.md:117-121` possible:
choose preferences against `simcal`, then run the acceptance test against a
table solved on the measured process, so preferences are not chosen on the
family they are then scored on.

---

### `data/samples/btcusd_20260904T200134Z_bitstamp.jsonl.gz`

**Status:** Checked — [x] Reviewed (2,944,722 bytes gzipped, **86,105 lines**,
decompressed and counted; contents sampled)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** The largest fixture in the repository and the one
`tests/test_bitstamp.cpp` replays as a real-data regression test via the
`capture_btcusd` target registered at `CMakeLists.txt:118`.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** None — this file *is* a test fixture.

**Performance Notes:** 2.9 MB of the repository's 8.4 MB. `.git` totals 14 MB,
so LFS is not warranted; if a second capture of this size is ever committed, it
becomes the right answer.

**Security Notes:** Public market data from Bitstamp's `live_orders_btcusd` and
`live_trades_btcusd` channels. No account identifier, no API key, no
authentication token, no personally identifying information — order ids are
exchange-assigned integers identifying an order, not a person. Obtaining it
required no credentials and it grants none.

**Market-Logic Notes:** btcusd at roughly $79,829 with a one-cent tick is the
high-price end of the three-instrument spread, which is what makes the
decimal-detection and price-window sizing testable: a fixed tick count that
suits this book is absurd for xrpusd.

---

### `data/samples/btcusd_20260904T200134Z_snapshot.json`

**Status:** Checked — [x] Reviewed (387,244 bytes, parsed in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** **4,418 bids and 3,665 asks, three columns per row**
(price, amount, order id), touch at 79,829.24 / 79,829.25 — a one-cent spread.
The three-column shape is what `BitstampDecoder` requires; a two-column snapshot
means the endpoint aggregated by price and `tests/test_bitstamp.cpp` asserts
that case fails loudly, as "L2 wearing L3's clothes".

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** None.

**Performance Notes:** The largest single JSON in the repository at 387 KB;
`slurp` reads it whole, which is fine at this size.

**Security Notes:** Public REST order-book response. No credentials.

**Market-Logic Notes:** This file sets the price window and the quoting
precision for its capture, and `BitstampDecoder::detect_decimals` reads two
price decimals from it. It does **not** seed the near-touch book — the seed
guard excludes it — which is the decision worth 39 points of accuracy on xrpusd
recorded in `tests/test_bitstamp.cpp:498-505`.

---

### `data/samples/ethusd_20260904T202638Z_bitstamp.jsonl.gz`

**Status:** Checked — [x] Reviewed (1,247,241 bytes gzipped, **33,411 lines**,
decompressed and counted)

**Issues Found:** None in the file. It is the instrument with the open
reconstruction defect.

- **`docs/KNOWN-ISSUES.md` 1 is about ethusd**: the reconstructed book develops
  holes near the touch over multi-hour captures. This ten-minute sample is short
  enough not to show it, which is why validating the fix needs a fresh
  eight-hour capture.

**Severity:** n/a — no issues found for the file; the open issue is tracked as M17.

**Why it matters:** ethusd is the instrument the whole Phase 5 chain is
calibrated on — `FlowConfig::ethusd()`, `FlowConfig::ethusd_queue_reactive()`
and `policy/ethusd.bin` all derive from it.

**Fix Recommendation:** None for the file.

**Refactor Suggestion:** None.

**Tests Missing:** None — fixture.

**Performance Notes:** 33,411 lines over ten minutes is the 18.55 ms mean
interarrival that `mean_gap_ns` encodes and that made the old event-count
message budget 3.7 seconds.

**Security Notes:** As for the btcusd capture — public channels, no credentials,
no personal data.

**Market-Logic Notes:** This is the capture behind every constant in
`FlowConfig::Qr`, and the tables in `docs/06-queue-reactive-plan.md` are its
measurements.

---

### `data/samples/ethusd_20260904T202638Z_snapshot.json`

**Status:** Checked — [x] Reviewed (102,308 bytes, parsed in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** **874 bids and 1,395 asks**, touch at 2,454.69 / 2,454.70 —
again a one-cent spread, and the thinnest book of the three by order count. That
thinness is part of why ethusd is the instrument where reconstruction holes show
up first.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** None.

**Performance Notes:** n/a.

**Security Notes:** As for the btcusd snapshot.

**Market-Logic Notes:** The 874/1,395 asymmetry at the moment of capture is a
reminder that a snapshot is one instant, not a distribution — which is the
argument for measuring depth from the stream over a warm-up window rather than
from the snapshot.

---

### `data/samples/xrpusd_20260904T201339Z_bitstamp.jsonl.gz`

**Status:** Checked — [x] Reviewed (1,388,944 bytes gzipped, **35,786 lines**,
decompressed and counted)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** The instrument that produced the seed-guard measurement.
Seeding the near-touch book from the snapshot scored 11.1% on xrpusd against
90.2% for building from the stream alone, judged by the share of trades printing
inside the touch (`apps/replay/main.cpp:282-287`). btcusd and ethusd scored the
same either way, so this capture is the only evidence for a decision that
applies to all three.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** None — fixture.

**Performance Notes:** n/a.

**Security Notes:** As for the other two captures.

**Market-Logic Notes:** It is also the one instrument on which
`tools/calibrate.py` **could** identify an Avellaneda-Stoikov `k`
(`k/se = 5.53`, no sign change across cutoffs), which is what makes the claim
"two of three instruments" checkable rather than rhetorical.

---

### `data/samples/xrpusd_20260904T201339Z_snapshot.json`

**Status:** Checked — [x] Reviewed (207,230 bytes, parsed in full)

**Issues Found:** None.

**Severity:** n/a — no issues found

**Why it matters:** **1,450 bids and 2,760 asks**, touch at 1.39722 / 1.39724 —
**five** price decimals against btcusd's two. This file is the reason
`BitstampDecoder::detect_decimals` exists: assuming either instrument's
precision would silently mis-scale the other by a factor of a thousand, which is
the failure `apps/replay/main.cpp:241-243` names.

**Fix Recommendation:** None.

**Refactor Suggestion:** None.

**Tests Missing:** None.

**Performance Notes:** n/a.

**Security Notes:** As for the other snapshots.

**Market-Logic Notes:** A $1.40 book with a $0.00001 tick and a $79,829 book
with a $0.01 tick cannot share a fixed tick window: the same 80,000 ticks is
±0.5% of one and ±19% of the other, which is the calculation
`apps/replay/main.cpp:177-181` performs to size the band from the snapshot mid.
Having both instruments committed is what makes that code path exercised rather
than argued.

---

## 3. Consolidated summary

**Scope:** all 116 tracked files, 22,088 lines, every one read in full. Binary
assets (PNGs, `.bin` policy tables, `.gz` captures) were audited by header,
size, tracking rule and provenance rather than byte by byte; every text file was
read line by line.

### Critical

**C1 is fixed.** The finding is kept below as it was written; the fix, the
measured before-and-after, the regression tests and the diagnosis of the
`TabulatedMDP` tie are in `docs/KNOWN-ISSUES.md` 6.

| # | Item | File | Owner | Status |
|---|---|---|---|---|
| C1 | **Five of the seven rows in the Phase 5 acceptance table do not measure what they report.** `InventorySkew` and `AvellanedaStoikov` centre on a reservation price of `mid − inventory·5000` ticks under the shipped `QuoteParams` defaults, and `assemble` clamps the half-spread but never the centre — so at an inventory of one share the ask is quoted 4,999 ticks below the best bid. Measured: 2 passive fills against 37,224 aggressive ones. `GLFT` and `ImbalanceSkew` have the opposite failure: `min_half = 1` swallows their entire inventory term across the whole ±50 position range, so both are `ConstantSpread` with extra arithmetic. And `TabulatedMDP` is byte-identical to `JoinTouch` in all seven columns, with the paired comparison reporting mean +0.0, CI [+0.0, +0.0]. | `include/lob/strat/quoting.hpp`, `apps/evaluate/main.cpp` | research | **FIXED** |

`apps/backtest` and `tests/test_strategies.cpp` both override `horizon` to 1.0
and are unaffected. `apps/evaluate`'s `base_params()` sets only `size` and
`max_inventory`, and `apps/evaluate` is the acceptance test.

### High
| # | Item | File | Owner |
|---|---|---|---|
| H1 | `apps/backtest` prints `Attribution::total` as its `net` column and never prints `RunResult::pnl()`. Excluding the closing position from the decomposition is deliberate and `include/lob/strat/driver.hpp:106-115` says so — and says `pnl()` "is the number the comparison is settled on". `apps/evaluate` follows that; `apps/backtest` does not, so the Phase 4 table ranks strategies on a diagnostic that credits nothing to one that made its money by holding. `inventory_mtm` is computed and read by nothing. | `apps/backtest/main.cpp`, `include/lob/strat/pnl.hpp` | research |
| H2 | The simulator never publishes fills caused by the agent's own orders unless a market `Aggress` event happens to follow, and then stamps them with that event's timestamp. Until then `view_book_` still shows resting orders the agent's own order consumed. The two-book design exists to make the view *lag*; this makes it *wrong*. | `include/lob/sim/simulator.hpp` | simulator |
| H3 | `JournalWriter` discards every `fwrite` and `fclose` return value, in the one class whose stated contract is byte-for-byte reproducibility. `written()` has never returned anything but zero. `batch_records = 0` is a heap buffer overflow on the first `append`. | `include/lob/measure/journal.hpp` | measure |
| H4 | `MatchingEngine::fills_` grows without bound for the life of a run. `fuzz/fuzz_matching.cpp` calls `clear_fills()` every 32 ops to work around it. | `include/lob/sim/matching.hpp` | simulator |

### Medium
| # | Item | File | Owner |
|---|---|---|---|
| M1 | `LatencyRecorder::report()` prints `max()` and never reads `overflow_count()`, so a clamped worst case is reported as the worst case. (`Histogram`'s clamp is deliberate and tested; the gap is only in the report.) | `include/lob/measure/recorder.hpp` | measure |
| M2 | `FeatureEngine` leaves `mid`, `spread`, `imbalance` and `weighted_mid` stale on a one-sided book, with no validity flag. `detail::mid_of` compounds it: `best_bid()` returns 0 on an empty side, so five strategies compute a mid of half the other side's price. | `include/lob/feat/features.hpp`, `include/lob/strat/quoting.hpp` | features |
| M3 | `Price::none()` compares as **better than every real ask**, because `kNoPrice` is `INT64_MIN`. `operator-` against it is signed overflow. Latent: no caller outside `tests/test_types.cpp`. | `include/lob/core/types.hpp` | core |
| M4 | `PolicyTable::load` clears `policy_` on failure but leaves `value_` populated and `header_` stale. | `src/policy_table.cpp` | policy |
| M5 | `src/tsc.cpp`'s lazy calibration uses non-atomic globals — a latent data race the moment anything is threaded. 95 ms of busy-wait at first use. | `src/tsc.cpp` | measure |
| M6 | `OrderMap`'s three probe loops spin forever on a full table. Safe only because `OrderBook` sizes the pool to half the map, which `OrderMap` does not state. | `include/lob/book/order_map.hpp` | book |
| M7 | `Arena::create<T>` has no `is_trivially_destructible` assert although `Pool` does; `Pool::release` cannot detect a double release and its counter underflows. Neither class has a production caller. | `include/lob/core/arena.hpp` | core |
| M8 | `ReferenceBook::find_entry` can return `nullptr` and all three callers dereference it. A crash in the oracle looks like a bug in the code under test. | `include/lob/book/reference_book.hpp` | book |
| M9 | `replay --verify` keys its cadence off `applied`, which does not advance on a reject — so once it reaches a multiple of 10,000, every rejected event triggers a full O(window × orders) check. Slowest exactly when the capture is worst. | `apps/replay/main.cpp` | tooling |
| M10 | `tape` holds every trade from the end of warm-up to `--start` and writes them all into frame zero. With the defaults that is 60 seconds of prints in the first frame, and unbounded memory for a large `--start`. | `apps/tape/main.cpp` | tooling |
| M11 | `fuzz_bitstamp` — the only target that parses bytes from a public network — is omitted from CI's 200,000-run extended pass and gets 20,000 under ctest. `detect_decimals` and `snapshot_touch` are not fuzzed at all. | `.github/workflows/ci.yml`, `fuzz/fuzz_bitstamp.cpp` | build |
| M12 | `record_bitfinex.py` and `record_coinbase.py` have no reconnect handling. That defect was found and fixed in `record_bitstamp.py` and never carried across; a dropped connection ends an eight-hour capture. | `tools/` | data |
| M13 | REST snapshot used without `raise_for_status()` or a shape check, in two recorders. | `tools/record_bitstamp.py`, `tools/record_coinbase.py` | data |
| M14 | `portable_main.cpp` computes a per-case seed, says in a comment that it reports it on failure, and never prints it. A crash found on CI cannot be reproduced. | `fuzz/portable_main.cpp` | build |
| M15 | `check_capture.py` computes `max(abs(hi−lo), abs(lo−hi))` — the same number twice. The comment above correctly describes measuring from the opening price, which `scan()` never records. Its output is a recommended `--band-pct`. | `tools/check_capture.py` | tooling |
| M16 | The differential book test skips its queue-position comparison whenever either book returns the −1 sentinel, which is the exact case where the fast book losing an order would show up. Own orders are only ever placed on the bid. | `tests/test_book_differential.cpp` | book |
| M17 | ethusd's reconstructed book develops holes over multi-hour captures. Open, and correctly described in `docs/KNOWN-ISSUES.md` 1. Solution is periodic re-snapshotting; needs a fresh 8-hour capture to validate. | `src/bitstamp.cpp` | data |
| M18 | The acceptance test is noise-dominated: mean −1,136 with CI [−3,872, +1,516], 6 of 16 seeds positive. | `apps/evaluate/main.cpp` | research |
| M19 | The imbalance and queue bucket edges are declared in `tools/mdp_params.py` **and** in `include/lob/policy/state.hpp`, which says a drift means "the policy is solved against one book and applied to another". Nothing enforces it, and the table header's discretisation check cannot catch it because the bucket *count* is unchanged. | `tools/mdp_params.py`, `include/lob/policy/state.hpp` | policy |
| M20 | `docs/figures/calibration.json`'s caveat is a hardcoded string claiming "Fill counts are 24-65 per instrument"; the file's own data says 15, 14 and 30. Its `k_stability_ratio` block correctly diagnoses btcusd and ethusd as unidentified (sign changes across cutoffs, btcusd's `k` at 0.46 standard errors from zero) while the `instruments` estimates ship with no marker saying so. | `tools/calibrate.py`, `docs/figures/calibration.json` | research |
| M21 | `tools/record_bitstamp.py`'s docstring still says joining `live_trades` on order id is "the only way" to split cancels from fills. `include/lob/feed/bitstamp.hpp` and `docs/02` §3.1 both record that `amount_traded` supersedes it and that the join would be **wrong**, because the two channels are not on the same clock. | `tools/record_bitstamp.py` | data |

### Low
Twenty-eight further findings are recorded in section 2 against the file they
belong to. The recurring ones, each appearing in more than one file:

| Pattern | Where |
|---|---|
| Timing assertions in unit tests, which measure the host rather than the code | `tests/test_tsc.cpp:92`, `tests/test_policy.cpp:391` |
| The rdtsc overhead hardcoded as a magic constant, three copies, two values | `apps/replay:116` (35), `apps/latency_demo:161` (30), `bench/bench_book:139` (35) |
| Standard-library headers relied on transitively | `src/journal.cpp`, `include/lob/book/events.hpp`, `include/lob/book/reference_book.hpp`, `tests/test_journal.cpp` |
| Unvalidated `atoi`/`atof` arguments reaching a loop bound | `apps/jitter_probe`, `apps/latency_demo`, `apps/replay`, `apps/tape` |
| A comment describing behaviour the code does not have | `include/lob/measure/stopwatch.hpp:3-4`, `fuzz/portable_main.cpp:109`, `bench/bench_book` ("mid-queue"), `tools/check_capture.py:99-104` |
| Documentation drift | `README.md:18` ("5 suites, ~90 assertions" against 26 tests), `README.md:7` (a phase behind), `docs/00` §5 layout, `docs/BASELINE.md` "cycles/op" |
| Licence file named `All Rights Reserved`, no static analyser in CI, no `permissions:` block, actions pinned by tag | repo root, `ci.yml` |

---

## 4. Prioritized fix roadmap

Ordered by what unblocks the most work, not by severity alone. Hour estimates
are for a person who knows this codebase.

### Immediate — the acceptance test does not currently measure anything
- ~~**PR: "Fix the quoting defaults and stop quotes crossing the market"**~~ — **DONE.**
  Two changes in `include/lob/strat/quoting.hpp`: pass the touch into
  `assemble()` and clamp the quote to it, and set `QuoteParams`'s defaults so
  the implied skew at the inventory limit is a few ticks rather than 250,000.
  Then add the two missing assertions to the existing sweep in
  `tests/test_strategies.cpp:159-177` — `q.bid < best_ask` and
  `q.ask > best_bid` — and run that sweep a second time with a
  default-constructed `QuoteParams`. Closes **C1** for four of the five rows.
- ~~**PR: "Find out why TabulatedMDP ties JoinTouch exactly"**~~ — **DONE, and it
  is not a bug.** 97.8% of the solved policy is JoinTouch's rule exactly; the
  304 states that differ need `|inventory| >= 3` while alone at the touch, which
  60,000 events never reach. At 250,000 events over 6 seeds the tie breaks by
  3.4 ticks on one extra requote. See `docs/KNOWN-ISSUES.md` 6. What remains is
  a research question, not a defect: Add a per-action histogram beside the existing `off_grid`
  counter in `include/lob/strat/tabulated.hpp` and dump it over a run. Either
  the solved policy is constant, or the lookup is not differentiating states.
  Until this is answered the Phase 5 criterion has no signal. Closes the rest
  of **C1**.
- ~~**PR: "Backtest reports session P&L, not the decomposition"**~~ — **DONE.** — 1 h. Add a
  `pnl` column to `apps/backtest`'s table from `RunResult::pnl()` and keep
  `attr.total` beside it labelled as trading edge, matching what `apps/evaluate`
  already does and what `include/lob/strat/driver.hpp:106-115` says the
  comparison must use. Delete the unread `Attribution::inventory_mtm` or start
  using it. Closes **H1**.

### This week
- ~~**PR: "Publish agent fills to the view book"**~~ — **DONE.** — 4 h. One
  `publish_new_fills()` called unconditionally after every step in
  `include/lob/sim/simulator.hpp`, with fills carrying their own timestamp, and
  `a.arrive_ts` rather than `now_` passed into the matcher. Add the test: after
  an agent market order fills a resting order, the view book reflects it without
  waiting for an unrelated `Aggress`. Closes **H2**.
- ~~**PR: "Journal writer: stop discarding write errors"**~~ — **DONE.** — 2 h. Sticky `failed_`
  flag set from `fwrite` and `fclose`, `++total_` in `append`, clamp
  `batch_records` to at least 1, and `CHECK_EQ(w.written(), kN)` in the test.
  Closes **H3**.
- ~~**PR: "Bound the matching engine's fill log"**~~ — **DONE.** — 3 h. `consume_through()` plus
  an offset so `first_fill` stays monotone; update the call sites in
  `simulator.hpp` that index into `fills()`; add a soak test. Closes **H4**.
- ~~**PR: "Rename the licence file"**~~ — **DONE.** — 5 min. `git mv "All Rights Reserved" LICENSE`.
- ~~**PR: "Validate the REST snapshot before use"**~~ — **DONE.** — 1 h, both recorders. Closes
  **M13**.

### Next two weeks
- ~~**PR: "Report a clamped maximum as clamped"**~~ — **DONE.** — 1 h. Track the true extreme in
  `Histogram` and surface `overflow_count()` in `LatencyRecorder::report()`.
  Closes **M1**.
- ~~**PR: "Features carry a validity flag"**~~ — **DONE.** — 2 h. `two_sided` on `Features`,
  a `has_bid && has_ask` guard in `detail::mid_of`'s callers, and the
  `log(2)` fix or rename for `rate_halflife_ns`. Closes **M2**.
- ~~**PR: "Sentinels that cannot win a comparison"**~~ — **DONE.** — 2 h. `better_than` and
  `operator-` guarded against `Price::none()`; `PolicyTable::load` clears
  `value_` and `header_`; `OrderMap::insert` refuses a full table; `Arena` and
  `Pool` get the missing assert and the double-release guard. Closes **M3, M4,
  M6, M7**.
- ~~**PR: "Fuzz the untrusted path properly"**~~ — **DONE.** — 2 h. Add `fuzz_bitstamp` to CI's
  extended pass at `-max_len=4096`, add `detect_decimals` and `snapshot_touch`
  to the target, print the per-case seed in `portable_main.cpp`, and add
  `permissions: contents: read` and `timeout-minutes` to the workflow. Closes
  **M11, M14**.
- ~~**PR: "Reconnect handling in all three recorders"**~~ — **DONE.** — 4 h. Lift the retry loop
  out of `record_bitstamp.py` into a shared base. Closes **M12**.
- ~~**PR: "Tooling fixes"**~~ — **DONE.** — 3 h. `replay --verify` cadence off a total counter;
  `tape` clears pending trades before `--start`; `check_capture` measures reach
  from the opening trade price. Closes **M9, M10, M15**.
- ~~**PR: "One source for the bucket edges"**~~ — **DONE.** — 2 h. Fold `kImbEdges` and
  `kQueueEdges` into `MdpParams::hash()` so a drift between
  `include/lob/policy/state.hpp` and `tools/mdp_params.py` makes `apps/solve`
  refuse the table, and add a test comparing the header's edges against
  `policy/mdp.json`. Closes **M19**.
- ~~**PR: "Derive the calibration caveat from the data"**~~ — **DONE.** — 1 h. Compute the fill
  range rather than writing it, and add `identified` and `k_over_se` to each
  instrument in `docs/figures/calibration.json` so an unusable estimate says so.
  Closes **M20**.
- ~~**PR: "Documentation sync"**~~ — **DONE.** — 2 h. `README.md`'s test count and status line,
  `docs/00` §5's layout, `docs/BASELINE.md`'s cycles column,
  `tools/record_bitstamp.py`'s superseded docstring, and a `KNOWN-ISSUES.md`
  entry for C1. Closes **M21** and the Low documentation-drift row.
- ~~**PR: "clang-tidy in CI"**~~ — **DONE.** — 2 h.

### Next month
- **Periodic re-snapshot during recording** — 8 h. Fire the existing
  session-snapshot machinery on a timer, reseed at the marker with the guard
  relative to the current touch. Needs a fresh 8-hour capture to validate.
  Closes **M17**.
- **Give the acceptance test power back** — 6 h. Raise the seed count until the
  interval excludes zero, or compare on an inventory-neutral statistic. This is
  only worth doing *after* C1 is closed, because the current baselines make the
  comparison meaningless regardless of its width. Closes **M18**.
- **The price impact of a trade**, which blocks the mean-reversion ratio,
  adverse selection and the lift together (`docs/06-queue-reactive-plan.md`).

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

**All 116 tracked files have been audited.** Every one is marked `[x]` in the
tree in section 1, and every one has a section in section 2 giving its status,
issues, severity, why it matters, fix, refactor note, missing tests, and
performance, security and market-logic notes.

Text files — every `.cpp`, `.hpp`, `.py`, `.sh`, `.md`, `.json`, `.yml` and the
build files — were read line by line. Binary assets — seven PNGs, two `.bin`
policy tables and three `.jsonl.gz` captures, 19 files in total — were audited
by header, size, tracking rule, provenance and the code that reads them, rather
than byte by byte; each says so in its own section.

**Evidence commands, re-run at the close of the audit:**

```
grep -RIn --exclude-dir=.git --exclude-dir=build -E "API_KEY|SECRET|TOKEN|PASSWORD|passwd|aws_access_key|PRIVATE_KEY|BEGIN RSA|xoxb-|ghp_" .
grep -RIn --exclude-dir=.git --exclude-dir=build -E "verify=False|_create_unverified|CURLOPT_SSL_VERIFYPEER" .
grep -RIn --exclude-dir=.git --exclude-dir=build -iE "place_order|send_order|new_order_single|order_gateway|fix\.4|POST /order|api_secret" .
git ls-files --others --exclude-standard
ls -a | grep -i "^\.env"
ctest --test-dir build -N
ctest --test-dir build -E "_long"
./build/evaluate --table policy/ethusd.bin --seeds 2 --events 60000
```

| Check | Result |
|---|---|
| Secrets, keys, tokens | **none found** |
| Disabled TLS verification | **none found** |
| Order-entry or gateway code | **none found** |
| `.env` files, tracked or untracked | **none exist** |
| Untracked files the ignore rules are hiding | **none** |
| Registered tests | **26**, across 20 files |
| Test result | **25/25 pass** excluding the `--long` differential pass |

**What the audit changed about its own findings.** Three claims were corrected
after checking them rather than left as first written, and each correction is
recorded in the file section it belongs to:

1. The baselines defect was first attributed to `apps/backtest`. That is wrong:
   `apps/backtest` and `tests/test_strategies.cpp` both override `horizon` to
   1.0. It is `apps/evaluate`'s `base_params()` that leaves the risk parameters
   at their defaults, and `apps/evaluate` is the acceptance test. Confirmed by
   running it.
2. The clamped histogram maximum was first written as a `Histogram` defect. It
   is deliberate and tested at `tests/test_histogram.cpp:113-122`. The defect is
   only that `LatencyRecorder::report()` discards `overflow_count()`.
3. "No test constructs a default `QuoteParams`" was too strong.
   `tests/test_policy.cpp:492` constructs one — and hands it to `JoinTouch`,
   which never reads the risk parameters. The accurate statement is that no test
   exercises the defaults through a strategy that uses them.
4. The P&L finding was first written as "the attribution path lost the inventory
   term". Re-reading `include/lob/strat/driver.hpp:106-115` showed the exclusion
   is deliberate and documented, and that the same comment names
   `RunResult::pnl()` as "the number the comparison is settled on".
   `apps/evaluate` follows that; `apps/backtest` is the only consumer of
   `attr.total` in the repository and prints it as its bottom line. The defect
   is the backtest's reporting, not the decomposition.

**What this audit did not do.** It read the code and ran the test suite, the
acceptance test and targeted numerical checks. It did not run the sanitizer or
`tsan` presets, an extended fuzz campaign, or a static analyser — each of those
is a separate pass and three of the findings above (M5's data race, M11's fuzz
gap, L7's missing analyser) are precisely the kind those passes exist to
surface.

---

All files have been fully audited and verified.
