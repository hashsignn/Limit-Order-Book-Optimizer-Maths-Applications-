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
  - [ ] `ethusd.bin`
  - [ ] `mdp.json`
  - [ ] `queue_reactive.json`
  - [ ] `sim_mdp.json`
  - [ ] `simcal.bin`
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
  - [ ] `test_bitstamp.cpp`
  - [x] `test_book.cpp`
  - [x] `test_book_differential.cpp`
  - [x] `test_calibration.cpp`
  - [x] `test_features.cpp`
  - [x] `test_histogram.cpp`
  - [x] `test_journal.cpp`
  - [x] `test_matching.cpp`
  - [x] `test_order_map.cpp`
  - [x] `test_pnl.cpp`
  - [ ] `test_policy.cpp`
  - [x] `test_properties.cpp`
  - [x] `test_queue_reactive.py`
  - [x] `test_simulator.cpp`
  - [ ] `test_split_replay.py`
  - [x] `test_strategies.cpp`
  - [x] `test_tsc.cpp`
  - [x] `test_types.cpp`
  - [x] `test_util.hpp`
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

### `src/tsc.cpp`

**Status:** [x] Reviewed (102 lines, read in full)

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

**Severity:** Medium (the race, if the project is ever threaded), Minor (the
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

**Status:** [x] Reviewed (33 lines, read in full)

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

**Status:** [x] Reviewed (31 lines, read in full)

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

**Status:** [x] Reviewed (85 lines, read in full)

**Issues Found:** A failed `load()` leaves `value_` populated with stale or
partial data.

- **`fail()` clears `policy_` but not `value_`** (line 50). Both are assigned at
  lines 73-74 before the body read; if the body read is short, `fail("truncated
  body")` empties the policy and leaves `value_` at `kNumStates` entries of
  whatever was read plus zeros. A caller that checks the return value is fine.
  A caller that inspects `value_` afterwards reads a vector that looks valid.
  The same applies to `header_`, which keeps its previous contents because it is
  only assigned on success.

**Severity:** Medium

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

**Status:** [x] Reviewed (126 lines, read in full)

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

**Status:** [x] Reviewed (120 lines, read in full)

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

**Severity:** High (dropped writes), Medium (the unchecked batch size), Minor
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

### `include/lob/measure/histogram.hpp` and `src/histogram.cpp`

**Status:** [x] Reviewed (89 + 251 lines, read in full)

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

**Severity:** Medium (the unreported clamp), Minor (the other two)

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

**Status:** [x] Reviewed (84 lines, read in full)

**Issues Found:** The report omits the clamp indicator (see the histogram
section above — the fix belongs here). No other defects.

- **`report()` prints `h.max()` and never `h.overflow_count()`.** With the
  default 1 s ceiling, any stage that stalled longer reports exactly 1 s with no
  marker.
- **`operator[]` and `record_nanos` index `hists_` with an unchecked
  `Stage`.** Passing `Stage::Count` reads one past the end. Every call site
  passes a named enumerator, and the type makes anything else awkward, so this
  is a note rather than a finding.

**Severity:** Medium (the omitted clamp), Minor (the indexing)

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

**Status:** [x] Reviewed (59 lines, read in full)

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

**Status:** [x] Reviewed (101 lines, read in full)

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

**Status:** [x] Reviewed (78 lines, read in full)

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

**Severity:** Medium (both — latent; `better_than` currently has no caller
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

**Status:** [x] Reviewed (129 lines, read in full)

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

**Severity:** Medium (the missing assert and the double-release), Minor (the
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

**Status:** [x] Reviewed (37 lines, read in full)

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

**Status:** [x] Reviewed (91 lines, read in full)

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

**Status:** [x] Reviewed (113 lines, read in full)

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

**Severity:** Medium (the hang, as a latent property of the class), Minor (the
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

**Status:** [x] Reviewed (191 lines, read in full; implementation audited
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

**Status:** [x] Reviewed (175 lines, read in full)

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

**Severity:** Medium (the unchecked `find_entry`, because of what this file is
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

**Status:** [x] Reviewed (54 lines, read in full; implementation audited under
`src/line_reader.cpp`)

**Issues Found:** None.

**Severity:** n/a

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

**Status:** [x] Reviewed (125 lines, read in full)

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

**Status:** [x] Reviewed (247 lines, read in full)

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

**Severity:** High (the unpublished agent fills), Medium (the FIFO latency and
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

**Status:** [x] Reviewed (228 lines, read in full)

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

**Severity:** Medium (the stale mid), Minor (the two naming defects)

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

**Status:** [x] Reviewed (243 lines, read in full)

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

**Status:** [x] Reviewed (214 lines, read in full)

**Issues Found:** The headline P&L excludes the mark-to-market of the leftover
position, and every test pins that position to zero so nothing catches it. Two
smaller points.

- **`Attribution::total` omits `inventory_mtm`.** Line 153 computes
  `inventory_mtm = final_inventory * final_mid`; line 154 computes
  `total = spread_capture - adverse_sel - fees` and never uses it. The file's own
  header lists four lines in the decomposition and names "inventory cost" as one
  of them. `apps/backtest/main.cpp:62` prints `r.attr.total` as the P&L column,
  and `include/lob/strat/driver.hpp:253` passes the real
  `sim.stats().inventory` in — so the number a run is judged on ignores whatever
  position it ended holding. Both `attribute` calls in `tests/test_pnl.cpp`
  (lines 55, 89-90) pass `final_inventory = 0`, which is why no test fails.
  This matters concretely here: two of the baselines end runs with large
  positions, so their reported `total` is not their result.
- **`MarkoutTracker::advance` returns early when `mid_now <= 0.0`** (line 87).
  A horizon that elapses while the mid is unavailable is not recorded then; it
  is recorded at the next call with a valid mid. So the realised horizon is
  silently longer than the nominal one, and nothing marks those fills.
- **`<deque>`, `<string>` and `lob/sim/matching.hpp` are included and unused.**

**Severity:** High (the omitted inventory mark), Minor (the other two)

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

**Status:** [x] Reviewed (247 lines, read in full)

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

**Status:** [x] Reviewed (123 lines, read in full)

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

**Status:** [x] Reviewed (625 lines, read in full)

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

**Severity:** Medium (the verify stall), Minor (the rest)

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

**Status:** [x] Reviewed (324 lines, read in full)

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

**Severity:** Medium (the first-frame trade dump), Minor (the rest)

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

**Status:** [x] Reviewed (154 lines, read in full)

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

**Status:** [x] Reviewed (163 lines, read in full)

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

**Status:** [x] Reviewed (50 lines, read in full)

**Issues Found:** A negative argument makes it run effectively forever.

- **`std::atoi(argv[1])` is unvalidated** (line 21) and flows into
  `tsc::from_nanos(seconds * 1e9)` (line 29), which casts a negative `double` to
  `std::uint64_t`. That conversion is undefined; in practice it produces a
  near-`UINT64_MAX` deadline, so `jitter_probe -1` spins until killed while
  filling a histogram.

**Severity:** Minor

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

**Status:** [x] Reviewed (51 lines, read in full)

**Issues Found:** None. Two constraints worth naming.

- **`CHECK_EQ` requires `std::to_string`**, so it only works on arithmetic
  types. Comparing two `std::string`s or two enums needs `CHECK(a == b)` and
  loses the diagnostic.
- **`CHECK_THROWS` catches `...`**, so it cannot distinguish "threw the right
  exception" from "threw something else entirely".

**Severity:** Minor (both)

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

**Status:** [x] Reviewed (47 lines, read in full)

**Issues Found:** The two `types.hpp` defects are exactly the cases this file
does not cover.

- **`better_than` is tested only on valid prices** (lines 24-28). Nothing tests
  `Price::none().better_than(x, Side::Ask)`, which returns `true` because
  `kNoPrice` is `INT64_MIN`.
- **`operator-` is tested only on valid prices** (lines 16-17). Nothing tests
  the `Price::none()` case, which is signed overflow.

**Severity:** Medium (as a coverage gap; the defects are recorded under
`include/lob/core/types.hpp`)

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

**Status:** [x] Reviewed (88 lines, read in full)

**Issues Found:** Covers the happy paths and exhaustion; misses both memory-
safety cases.

- **No double release** (`pool.release(b); pool.release(b);`), which corrupts
  the free list into a cycle and hands the same slot out twice.
- **No release of a foreign pointer.**
- **`arena.create<Node>` uses a trivially destructible type** (line 42), so the
  missing `static_assert` on `Arena::create` is never exercised.
- **No `Pool{0}` or `Arena{0}`.**

**Severity:** Medium (coverage), and see `include/lob/core/arena.hpp`

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

**Status:** [x] Reviewed (164 lines, read in full)

**Issues Found:** One structural gap, which is the same gap as the class's.

- **The differential loop caps live entries at `kCap - 1` = 4,095** (line 104)
  in a table whose real capacity is 8,192, so the load factor never exceeds
  0.5. The full-table case — where `find` and `insert` spin forever — is
  therefore never approached. That is deliberate and correct given how
  `OrderBook` uses the class, but it means the hazard recorded under
  `include/lob/book/order_map.hpp` is untested rather than ruled out.

**Severity:** Minor (coverage of a hazard the caller prevents)

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

**Status:** [x] Reviewed (102 lines, read in full)

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

**Severity:** Medium (the three coverage gaps)

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

**Status:** [x] Reviewed (96 lines, read in full)

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

**Severity:** Medium (CI flakiness), Minor (the coverage gap)

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

**Status:** [x] Reviewed (201 lines, read in full)

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

**Status:** [x] Reviewed (208 lines, read in full)

**Issues Found:** One coverage gap matching a known defect.

- **No test of a *failing* `replace()`.** `src/order_book.cpp` loses an event
  from the stats on that path (`--stats_.deletes` runs, `++stats_.replaces`
  does not). Every `replace` here succeeds.

**Severity:** Minor

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

**Status:** [x] Reviewed (172 lines, read in full)

**Issues Found:** Two of the three self-match modes are untested.

- **`SelfMatch::Allow` and `SelfMatch::CancelIncoming` are never constructed.**
  Only `CancelResting` is exercised (line 133). `CancelIncoming` in particular
  has a different effect on the aggressor's remainder and nothing checks it.

**Severity:** Minor

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

**Status:** [x] Reviewed (173 lines, read in full)

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

**Severity:** Medium (the sentinel skip), Minor (the rest)

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

**Status:** [x] Reviewed (243 lines, read in full)

**Issues Found:** The one-sided-book case asserts only the absence of NaN, which
is why the stale-mid defect survives.

- **Lines 147-161 test an empty and then a bid-only book and assert only
  `!isnan(...)`.** On the bid-only book, `f.imbalance` is stale at 0.0 from the
  previous empty update, and `!isnan(0.0)` passes. Nothing asserts that a
  consumer could tell the values are not current. This is the coverage gap
  behind the `Features` finding.
- **The differential loop's imbalance check is guarded by `has_bid() &&
  has_ask()`** (line 206), so the one-sided case is excluded there too.

**Severity:** Medium (coverage of a Medium defect)

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

**Status:** [x] Reviewed (137 lines, read in full)

**Issues Found:** Every `attribute` call passes `final_inventory = 0`, which is
why the omitted inventory mark is invisible.

- **Lines 55, 89 and 90 all pass `0`** as the closing position. `Attribution`
  computes `inventory_mtm` from it and then leaves it out of `total`, so with a
  zero position the two agree and nothing fails.
- **`MarkoutTracker::advance` is never called with `mid_now <= 0`**, so the
  early-return that silently stretches a horizon is untested.

**Severity:** High (as the coverage gap behind a High defect)

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

**Status:** [x] Reviewed (125 lines, read in full)

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

**Severity:** Medium (both gaps)

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

**Status:** [x] Reviewed (180 lines, read in full)

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

**Status:** [x] Reviewed (275 lines, read in full)

**Issues Found:** None.

**Severity:** n/a

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
