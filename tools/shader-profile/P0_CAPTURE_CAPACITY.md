# Course capture diagnosis and correction — 2026-10-24

## Observed workload

The user drove two Mario Kart courses with the diagnostic enabled. We retained
the completed recorder report, log and bounded input trace before closing the
test window normally. One start marker exists, but no stop marker; these data
do not isolate the first course for an FPS comparison. No PresentMon capture
was requested for this diagnostic session.

* 23,124,236 observations; 9,334,225 input-cache candidates.
* 2,478,168 canonical captures, of which 2,478,125 were duplicate recipes.
* 2,041 recipes loaded, 43 added, 2,084 total, no recorder failure.
* 43 checked journal frames written in 10 worker batches.
* 4.716 seconds of instrumented observer elapsed time across the session;
  this includes diagnostic clocks/tracing and is not thread CPU time.

The worker's elapsed encoding/I/O counters are nonzero, as are its 8.21 million
CPU cycles. Its sampled CPU-time total rounds to zero on the Windows thread-time
clock for these short batches; that value does not mean zero worker CPU cost.

The full working set outgrew the initial 256-state memo and some of its raw
input tables. The first 524,288 candidate transitions contained 1,119 distinct
input-hash keys. In particular, the prefix table needed 285 distinct inputs;
the vertex group needed 225. Four-way table conflicts caused repeated eviction
even before a table's nominal capacity was fully used.

An offline C++ simulation of the captured transitions gives:

| Raw input slots / state slots | Canonical captures |
| --- | ---: |
| Original: 256 / 256 | 98,877 |
| Uniform: 512 / 1,024 | 3,417 |
| Uniform: 1,024 / 2,048 | 1,120 |
| Uniform: 2,048 / 4,096 | 1,120 |
| Selected: 1,024 for prefix/vertex, 256 for other groups / 2,048 | 1,120 |

This is a **98.87% reduction in captures for the bounded trace**, not a measured
full-course or FPS improvement. The simulator uses hashed diagnostic identifiers;
the production memo always compares full raw inputs and full token keys.

## Implementation

The selected capacities avoid increasing every table indiscriminately. Raw
groups still have four-way bounded replacement; generation IDs prevent a reused
slot from matching an older input. State memo entries reference the recorder's
existing immutable journal nodes instead of retaining another 1,532-byte recipe
copy. Those unordered-set node references remain stable during rehash and until
recorder teardown; they never escape to asynchronous writes. The worker retains
its own checked batch copies. Host projections and collision comparisons remain
unchanged, so hidden cross-GPU combinations are still captured.

Memo storage is now **3,416,498 bytes (3.26 MiB)**, versus 1,232,114 bytes before:
an additional 2.08 MiB, bounded independently of draw count. Known-input paths
still allocate nothing. Portable manifests, journal frames and `.nxc` bytes are
unchanged by this correction.

## CPU measurement and validation

Two alternating reference/updated pairs compile the same native benchmark
against frozen `f321d33b` recorder headers or the updated headers. Each result
uses 5 passes, discards the first, and reports the median of the remaining 4.
The disabled reference still executes register tracking with an empty table;
this measures the recorder toggle's cost, not all differences from upstream.

| Known working set | Before | After |
| --- | ---: | ---: |
| 64 states | 76.61–78.43 ns/observation | 73.54–74.58 ns/observation |
| 512 states | 473.10–475.36 ns/observation | 80.73–83.57 ns/observation |

Unchanged inputs remain around 7–8 ns. All known-state benchmark paths have zero
ordinary main-thread C++ allocations. The 512-state sequence preserves all
expected recipes and shows approximately 5.7–5.9x lower observer time; it is
synthetic, not a substitute for game presentation measurements.

All 11 native tests pass, including forced hash collisions, bounded eviction,
legacy recipe-set equivalence and asynchronous journal failure. The isolated
application compiles and exits normally from its 16-second boot/menu check:
2,084 loaded recipes, 169 canonical captures, zero new recipes, zero writer
batches and no recorder error. This boot check does not validate the corrected
memo's complete two-course working set.

## Remaining compilation work in the diagnostic session

The initial cache had 1,324 graphics records; the completed session has 1,340.
Its log contains 16 graphics compiler entries after warmup:

* 12 use at least one exact program absent from the initial cache. Together they
  introduce 10 distinct program byte sequences.
* 4 form new associations of individually known programs; neither program pair
  existed in the initial cache.
* No normalized complete key among those 16 was present in the initial cache.

None of the 10 new program byte sequences is explained solely by a changed
80-byte header or by adding/removing one trailing eight-byte word.

These are legitimate missing inputs/combinations, rather than evidence that an
already covered complete key was compiled again. Log entries count compiler
invocations, not independently measured native Vulkan creation calls. The 43 new
portable recipes must remain recorded even though only 16 compiler entries were
observed. Recording only on compilation would lose guest-state combinations.

Evidence: [p0-capacity-2026-10-24.json](reports/p0-capacity-2026-10-24.json).
Remaining validation: replay the corrected implementation over complete course
segments, then compare recorder enabled/disabled with matched, marked segments
and presentation/CPU measurements. Larger working sets in other games can still
evict this bounded memo and need separate measurement. No claim of zero FPS
overhead is made yet.
