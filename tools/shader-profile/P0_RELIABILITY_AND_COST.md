# P0 reliability and recorder cost — 2026-10-24

## Import and recovery

Resource integration and native warmup now have separate completion records.
`community.integrated` records the unique IDs of the local pipeline records
actually appended. Startup checks membership in the verified index it already
reads, without rehashing the entire cache or reparsing it a second time.
The receipt describes a reconstructible subset, not complete game coverage.

An isolated Mario Kart application test establishes:

| Launch | Resource reader calls | Resource bytes read | Result |
| --- | ---: | ---: | --- |
| Fresh local store | 1 | 2,430,998,419 | 1,076 records integrated |
| Same profile again | 0 | 0 | Existing records reused |
| After clean truncation removing an imported record | 1 | 2,430,998,419 | Missing record restored |
| Launch after repair | 0 | 0 | Existing records reused |

The three later launches each completed 969 warmup requests with no failures.
All four sessions closed normally. The original user caches were untouched.
Repair currently reconstructs the profile and relies on store deduplication;
it does not yet read resources for only the missing recipes.

Unknown content identity does not trigger repeated automatic reconstruction.
A fresh GUI selection authorizes one attempt; a separate `community.attempted`
receipt consumes that request even after a failed/interrupted reconstruction.
An explicit CLI request can retry, but skips resource reconstruction if the
matching integrated records are already present. These policy branches have
native tests; mods/multiple-update scenarios still need application validation.

Local metadata is published by writing/closing a temporary file and replacing
the destination atomically (MoveFileEx on Windows, rename on POSIX). A partial
metadata file never proves integration. This is not a claim of power-loss
durability for every filesystem or for the underlying cache data.

`native-warmup.json` maintains bounded failure backoff per safe startup key.
Its scope includes GPU vendor/device/model, driver identity/version, Vulkan
pipeline-cache UUID, shader ABI, this renderer compilation's build stamp and
the set of verified cache-record IDs. Changed compilation inputs reset backoff
conservatively, including unrelated cache growth. Failure retries are spaced
by 1, 2, 4, then at most 8 applicable startups; success removes the failure.
Malformed metadata fails open to ordinary warmup. Cancelled startup does not
publish updated failure state. Legacy recovery inputs do not use this backoff.
Runtime compilation remains available for deferred keys. There is no runtime
failure-ledger lookup, portable-format change or permanent blacklist.

## Recorder implementation

Register writes mark relevant dependency groups. A bounded, exact input cache
reuses complete recipes and their host projection. Every hash match compares
the full inputs/key; forced-collision and eviction tests verify coverage.
Unchanged inputs return before canonical capture. Known recent recipes avoid
capture, serialization and journal deduplication. New guest combinations remain
recorded even when they do not cause a local shader compilation.

Writes retain the same independent checked `.nxr` frames and are batched on
the existing serialization worker: at 32 new recipes, at a GPU frame boundary,
and before shutdown drain. No draw performs journal I/O or waits for that worker.
Empty frame flushes enqueue nothing. A crash can lose the currently buffered
partial batch, in addition to already asynchronous work; this is at most 31
new entries buffered between flushes. Normal shutdown drains the final batch.
Asynchronous write failure leaves the conservative `.failed` export blocker.
Automatic clearing of that marker is not implemented because completeness is
not established by the surviving journal alone.

The current memo occupies 1,232,114 bytes, plus a 49,024-byte pending batch and
the pre-existing bounded journal-entry set. Memo capacity remains a limitation
for the course workload below; it is not yet a universal zero-cost observer.

## Measurements

Results and denominators: [p0-2026-10-24.json](reports/p0-2026-10-24.json).
Two alternating baseline/optimized native benchmark pairs used the same input
sequences, 5 passes per configuration, discarding the first and taking the
median of the other 4. Timed scope includes ObserveState and register tracking,
without per-draw clocks. Recipe sets are verified after every enabled pass.

| Workload | Earlier implementation | Current implementation |
| --- | ---: | ---: |
| Unchanged inputs | 7.11–7.17 ns/observation | 7.33–7.54 ns/observation |
| Repeated 64-state working set | 344.48–348.71 ns/observation | 75.08–77.72 ns/observation |
| 512 newly discovered recipes | 1,584–1,595 ns/observation | 1,319–1,536 ns/observation |
| Main-thread allocations for those 512 discoveries | 1,796 | 569 |
| Worker drain for those 512 discoveries | 23.20–23.95 ms | 2.92–3.27 ms |

Known-state paths allocate nothing in this benchmark. Allocation counts cover
ordinary C++ allocations on the observer thread, not driver/worker allocations.
Worker drain includes filesystem variability; neither it nor this synthetic
working set establishes FPS or a real-course speedup.

A real course with the current observer loaded 1,998 recipes and added 43,
with no recorder error. Startup completed 1,182 warmup requests without failure.
Three graphics compiler entries were logged after warmup; these log entries
do not independently establish the exact native-create count. Importantly,
the recorder still performed **2,792,536 full canonical captures**. It therefore
needs a representative capacity/input-dependency investigation before claiming
negligible course overhead. No shader compilation does not prove no new recipe.

An additional automatic 40-second menu run loaded the resulting 2,041 recipes:
183,048 observed draws, 94,794 input-cache candidates, 169 canonical captures,
94,625 memo hits, zero new recipes and zero worker writes. Its clock-instrumented
observer mean was 210 ns/observation; this is elapsed instrumented time, not
thread CPU time and not the uninstrumented production cost.

## Instrumentation and remaining P0 validation

Report schema 3 distinguishes observations, candidates, full captures, memo
hits, worker batches and frames. Observer mean divides by observations;
capture mean divides by full captures. Timing is opt-in; normal draws do not
read clocks or query CPU counters.

Optional `NXEMU_RECIPE_INPUT_TRACE` saves at most 524,288 hashed input keys for
offline capacity analysis. It contains no shader instructions. Tracing is
diagnostic only and is not suitable for FPS measurement. `recorder-cache-trace`
simulates several capacities on that trace; diagnostic hash equality does not
replace exact equality in the actual recorder. The menu trace has 169 unique
input keys and does not reproduce the course limitation.

`recorder-session-monitor` is an external Windows tool sampling process CPU and
memory every 500 ms. Alt+Shift+F10 records QPC start/stop markers. PresentMon
must record continuously and share the same QPC time base. The first manual
course has 2,389 PresentMon rows but **no race markers**. It cannot establish
a matched recorder-on/off FPS, 1% low or stutter comparison. ETW recording was
also restarted during that session; keep both problems explicit rather than
substituting application GPU-tick CSVs for presentation measurements.

11 native tests and 61 export/import regression tests pass. Native tests cover
clean truncation, corrupt/torn cache and journal data, subset receipt validation,
unknown-content import policy, failure retry/success/reset, forced memo hash
collisions, recipe-set equivalence, final partial-batch drain and asynchronous
journal write failure. The isolated application compiles.

Remaining P0: diagnose the course's repeated canonical captures, remeasure the
final implementation, and complete matched race segments with reliable
presentation and CPU measurements. Real native-failure/backoff behavior remains
unit-tested rather than demonstrated by a deliberately broken Vulkan driver.
