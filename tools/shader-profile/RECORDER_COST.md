# Recorder cost reduction — 2026-10-24

Stabilization commit: c37f6e9e. Missing-resource and HLE findings remain documented
in P0_RESOURCE_AND_REUSE_AUDIT.md. The four missing programs are still excluded.

## Changes

* `GuestPipelineState::WriteWords` writes the same 278 field-wise words directly
  into caller storage. The recorder no longer allocates a vector per candidate.
  `Words()` remains available and byte-compatible. `FromWords` also avoids its
  two previous temporary vectors.
* Exact local recipe deduplication uses CityHash64 to index complete entries in
  a bounded hash table instead of repeated ordered-tree comparisons of long
  common prefixes. Hash collisions still compare all 383 words. Persistent
  journal integrity and shared profile/resource verification retain SHA-256.
* The recorder caches serialized guest words and their projected state. A dirty
  revision that yields identical complete guest state and key prefix returns
  before serialization/projection/dedup. Prefix-only changes reuse guest words.
  Genuine dynamic-state combinations remain captured even if their local Vulkan
  keys are identical. DynamicFeatures are fixed for this renderer instance.
* No register-write tracking instructions, public recipe schema, cache binary
  layout or journal frame bytes were changed. Full canonical capture attempts
  still occur on relevant register revisions; this does not eliminate all
  9.35 million captures from the original workload.

The hash-table bucket reserve is bounded by the existing 16,384-entry journal
limit. State hits allocate nothing; newly discovered recipes still allocate a
table node and queue a background write.

## Measurements and correctness

Native `recorder-cost` replays the real journals through the frozen legacy word
writer/tree lookup and the new word writer/hash lookup. Seven passes are used,
discarding the first and reporting the median of the remaining six. This scope
is **serialization plus exact dedup only**, not the full recorder or gameplay.

| Corpus | Unique recipes | Candidate observations | Legacy median | Optimized median |
| --- | ---: | ---: | ---: | ---: |
| Windows cold capture | 2,206 | 220,600 | 97.458 ms | 48.244 ms |
| Windows profile replay | 1,464 | 146,400 | 63.835 ms | 32.124 ms |

The serialized words and final recipe sets match exactly for both corpora.
The benchmark does not reproduce actual transition frequencies, capture,
projection, worker IO, GPU execution or FPS. Its roughly 2x speedup applies only
to the measured operations. Evidence is in ignored
`build/nxshader-native-workflow/recorder-cost-{cold,profile}.json`.

The native projection regression compares the new writer with the frozen writer
across feature masks and transitions, including output-buffer guards. The new
`recorder-replay` test exercises the actual observer with explicit engine inputs:
repeated transitions, hidden vertex layouts, source-prefix changes, HLE hints,
channel switching and reopening an existing journal. It compares complete recipe
sets with legacy capture and verifies reopening adds no duplicate frames.
Ten native tests and 61 Python-orchestrated export/import regressions pass.
The isolated application also compiled and passed a short real-engine check:
1,464 loaded recipes, zero new recipes, zero failed recordings and zero journal
writes. Its instrumented 4,654 observations are a boot/menu sample, not a course
performance benchmark.

## Optional profiling and manual A/B

Recorder report schema 2 uses elapsed-time fields, never labels observer wall
time as CPU time, and distinguishes loaded/new recipes. Optional stage counters:

* `capture_wall_ns`: full canonical capture attempts;
* `canonical_wall_ns`: updating serialized words and projected state;
* `dedup_wall_ns`: exact lookup/insertion;
* `enqueue_wall_ns`: new-recipe job submission;
* `unchanged_captures`: canonical captures skipped before downstream work;
* `canonical_updates`: cached words/projection updates, not unique recipes;
* `duplicate_entries`: candidates already present in the dedup table.

Timing is opt-in and adds clock calls. Do not compare these instrumented values
directly with FPS measured without instrumentation. Observer timing excludes
register revision tracking and worker work. The normal path does not sample
clocks or per-thread CPU counters. Worker CPU-time resolution can round to zero.

`NXEMU_RECIPE_RECORD_DISABLE=1` is an explicit diagnostic A/B switch; normal runs
continue recording automatically. It bypasses recorder construction/observation.
The existing register-write tracking instruction remains compiled in both
variants, so this switch does not estimate the cost of removing that instruction.

Two manual cases are prepared in ignored `recorder-ab-on-1` / `recorder-ab-off-1`
with identical initial `.nxc` and journal hashes, the same binary/settings, and
VSync set to **Immediate** as requested. All per-draw profiling is disabled.
PresentMon capture is limited to the test PID and gated by ALT+SHIFT+F11 at race
start/end to separate gameplay from menus. Display/frametime results are pending
the two user-piloted courses. Check swapchain identity and new cache entries when
analyzing results; differing workloads/compilations can confound the comparison.
No claim of zero FPS overhead or a measured FPS improvement is made yet.

## Query lifetime fix imported for unlocked testing

The later FIFO sessions are saved as `recorder-ab-{on,off}-vsync-1` after the
user reported a failure with VSync disabled. No usable PresentMon CSV was found
for either session. Their internal application tick logs have different durations
and include menus; they do not establish a presentation FPS comparison. Results
are preserved in ignored `recorder-ab-vsync-results.json`. The disabled recorder
session leaves its initial recipe journal unchanged.

The query pool lifetime fix from `a89fcb2601c83429d1c3ae09ce8c1b4a449a2b54`
(`fix/vulkan-query-lifetime`, also present as `5bbc551c` on
`origin/fix/stability-macos`) was imported without the diagnostic renderer edits
or reverted acquisition-semaphore candidate. It retains query banks until
abandoned-query work and GPU result copies complete, and reads traversal links
before releasing the references that permit another thread to recycle a bank.
It does not add a per-draw GPU wait or change shader/cache/profile formats.

Both query lifetime regressions and the independent acquisition-semaphore test
pass locally, and the isolated shader application compiles. These tests verify
specific lifetime defects, not elimination of every in-game unlocked freeze.
Re-run both recorder modes using the same corrected video module. A capped
comparison cannot establish available throughput above its cap; use unlocked
game throughput/CPU work as well as displayed-frame pacing, and verify the
actual limiter and Vulkan present mode in each run.

The first corrected boot check (`queryfix-smoke-1`) used `run_smoke.py` with a
22-second duration. Its normal WM_CLOSE at that deadline was reported by the
user as another crash. The runner exited with code 0 after 22.735 seconds; the
engine log enters EmulationStopping at 22.033 seconds with no device-loss or
native exception evidence. This particular exit is an automation artifact, not
proof of a persistent Vulkan crash. A separate `queryfix-manual-1` was launched
without a timer, with the actual video DLL digest saved in launch.json. No claim
of sustained unlocked stability is made from the timed boot check.
