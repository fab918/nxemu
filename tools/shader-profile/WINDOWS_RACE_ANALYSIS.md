# Windows profile replay analysis — 2026-10-23

Follow-up: P0_RESOURCE_AND_REUSE_AUDIT.md records the GPU verification and fixes
for the four duplicate candidates, and the expanded search for missing programs.

## Result

The recorded Windows profile substantially reduces on-demand work, but does not
provide zero compilation or establish a stutter improvement. Both sessions used
an initially empty emulator cache and the same temporary per-executable NVIDIA
disk-cache override. That override was removed after the comparison. This is not
proof that every in-memory driver/compiler cache was cold.

| Runtime measurement | Cold capture | Profile replay |
| --- | ---: | ---: |
| Pipeline translation invocations | 1,080 | 47 |
| Native pipeline creation calls | 1,007 | 33 |
| Translation followed by native reuse | 73 | 14 |
| Translation/build elapsed sum | 446.126 ms | 19.152 ms |
| Native creation elapsed sum | 324.765 ms | 9.525 ms |
| Longest native creation | 22.767 ms | 0.635 ms |
| Local cache serialization elapsed sum | 180.074 ms | 9.015 ms |
| Translation exceptions / native failures | 0 / 0 | 0 / 0 |
| Quarantined keys | 0 | 0 |

Native calls fell by 96.72% between these runs. These are measured workloads,
not an exact percentage of identical draws avoided: the sessions contain menus,
different durations and dynamic gameplay. A native Vulkan creation call is not
necessarily a fresh driver machine-code compilation. Phase elapsed sums may
overlap across workers; do not add them to estimate main-thread stalls.

The fresh profile replay imported exactly once and completed all 969 queued
startup builds (964 graphics, five compute), with 102 HLE aliases. Startup shader
work took 3,627.48 ms, including 3,389.42 ms resource reconstruction. This startup
work is excluded from the runtime counts above. The earlier automatic-discovery
test separately verified second/third startup reconstruction was skipped,
without changing the completion marker. Native warmup still occurs on launch;
one-time import does not mean native objects survive process termination.

## Every remaining translation classified

| Reason | Translations | Native calls | Assessment |
| --- | ---: | ---: | --- |
| Code absent from the original cold capture | 42 | 30 | Uncovered observations, with duplicate candidates below |
| New pipeline combinations using known programs | 3 | 1 | Preserve combinations; two already share native pipelines |
| Captured source entries not reconstructed by export | 2 | 2 | Real coverage limitation to investigate |

The first category contains 34 distinct new program hashes. None has the same
full code SHA-256 as a program in the cold graphics inventory. Thus these are
not simply renamed copies of identical captured bytes. The evidence does not
identify why they were encountered only in the second workload, or prove that
the scene, kart, characters, effects and menus were identical. This profile
cannot prewarm observations that were never captured.

The two uncovered source entries normalize to keys `2344219451836853840` and
`11341256750336785000`. They need programs `9086712710020038545` (624 bytes) and
`12150558870624721378` (544 bytes). Both are among the four programs that resource
discovery failed to locate during export. They are not quarantined. Excluding
unverifiable resources is safe behavior, but these omissions are not a desired
final coverage result. We have not established whether the cause is dynamic code,
unhandled transformations, resource coverage or code boundaries. The other two
missing source programs were not requested in this replay.

No runtime translation maps to a reconstructed graphics key after normalization.
Runtime keys were first matched to the inventory's **raw** hashes, then normalized
using discovered constant-buffer dependencies. Directly matching runtime hashes
to fully normalized inventory hashes misses 27 events and misclassifies them.
All 47 runtime events are graphics events; this replay adds no compute fallback.

## Redundant work candidates

Four pairs each made two native calls despite having the same normalized state
after clearing only the HLE engine hint; all have zero constant replacements.
For one pair, even the full serialized environments match semantically after
map-order canonicalization:

* `10510699491315295506` and `13335286841417451168`.

The other pairs are:

* `14997705120588252926` and `14745725912590945382`;
* `7528734986971013168` and `10041349195182648927`;
* `13937247735336248170` and `13473222722866684920`.

Their environments differ, so they must not be merged based on hashes alone.
The likely area to audit is the transition from conservative unknown CBUF-size
dependencies to normalized keys and HLE twin lookup. Verify emitted SPIR-V,
bindings, Shader::Info and native state before changing aliasing. This is a
targeted reuse investigation, not justification to erase real combinations.
The 14 translations that already reuse a native pipeline are another measured
optimization opportunity; eliminating their translation requires a proven
compatible-environment identity, not just an identical guest-code hash.

## Stutters: no demonstrated overall improvement yet

| GPU-tick interval | Cold | Profile |
| --- | ---: | ---: |
| Samples | 44,184 | 37,908 |
| Observed duration | 383.994 s | 336.954 s |
| p99 | 16.826 ms | 16.863 ms |
| Greater than 33.333 ms | 15 | 18 |
| Greater than 50 ms | 10 | 12 |
| Greater than 100 ms | 4 | 4 |

Only two of the 18 long profile intervals overlap recorded translation/native
work, versus seven of 15 cold intervals. Overlap is not causation and these hooks
do not cover every scheduler wait. These are GPU ticks, not presentation events;
menus are included and there are no explicit race start/end markers. Therefore
do not claim higher FPS or fewer whole-course stutters from this comparison.
Measure presentation timing with explicit race boundaries in the next benchmark.

## Costs and next priorities

The complete export/import figures are in WINDOWS_ROUNDTRIP.md. The compressed
Zstd profile is 122,129 bytes (119.3 KiB), rebuilding 1,076/1,080 source records
(99.63%). First export takes 51.516 s searching 7,735 resources; repeat indexed
export takes 4.130 s and produces identical bytes. Improving compression is a
low priority: profile decompression is only 0.316 ms in this replay.

First-import resource work reads 2.431 GB and decodes 566.912 MB to recover small
shader spans. IO is 1,403 ms, decompression 589 ms, verification 1,002 ms. These
are the important import costs. Keep locators grouped by logical resource, reuse
already verified/decompressed data within the import, and audit verification
granularity before changing hashes or weakening validation. No algorithm change
has been benchmarked here.

Recording remains the principal runtime optimization candidate. The cold run
performed 9,351,946 captures for 2,206 unique recipes; the replay performed
7,108,723 captures for 1,464 unique recipes. Instrumented observer elapsed time is
about 334 ns/draw in both (9.424 s / 7.596 s over their respective sessions).
This includes timing overhead and excludes dirty-register revision tracking;
it is not a measured FPS penalty. Replay journal encoding totals 6.033 ms and
open/write/flush 97.449 ms, much less than observer work. Replay CPU-time samples
round to zero at the OS counter resolution; CPU work is not zero. Prefer dirty
state gating, bounded allocation-free canonical capture and background batching
before a cache-layout rewrite. Benchmark recording enabled/disabled without
per-draw diagnostic clocks before promising negligible overhead.

## Version provenance and portability

The current manifest has title ID, schema/guest-state versions and a free-form
content label. It does not reliably store a structured automatically captured
game/update version. This exported test profile's label is merely
`Mario Kart Windows roundtrip`. The local import stamp tracks compatibility and
loaded content separately; it does not substitute for portable provenance.

Record automatically the effective loaded base/update content identities,
numeric title/update versions, display version, producer build/revision and
recipe ABI. Record provenance during capture, not just at export: one local
cache may contain observations from multiple updates. Associate observations
with content-set identities and allow multiple origins for shared recipes.
Avoid absolute machine paths in the shared file.

Use provenance for reporting and selecting locators, not blanket refusal when
the destination has another update. Reuse a recipe only when local resource
bytes and required environment are verified compatible. Updated/missing shaders
fall back to local capture/compilation. Preserve genuine pipeline combinations;
deduplicate program references, identical environments and verified aliases.
Keep local driver products and GPU-specific identity outside the portable recipe
identity. All of this needs a separately reviewed format extension and migration.

## Recommended order

1. Investigate the four missing resources (two exercised here) and the four native
   duplicate candidates; preserve conservative fallback and add targeted evidence.
2. Automatically capture effective content/version provenance and resolve the
   active update for export. Complete the single-export-button workflow.
3. Reduce repeated capture/allocation work and retain verified resource provenance
   so exports do not redo discovery. Group/batch import IO and verification.
4. Repeat a controlled benchmark with race markers and presentation timing, then
   repeat Mac/Windows compatibility tests after any format extension.

GO for continuing the POC. NO-GO for a production claim of complete reconstruction,
zero compilation, zero runtime overhead or broad cross-version/GPU compatibility.
No renderer/compiler or binary cache redesign was made for this analysis.

Evidence: ignored `build/nxshader-native-workflow/windows-roundtrip-profile-race-1/`
contains `comparison-analysis.json`, `post-inventory.json`, `runtime.json`,
`recording-cost.json` and `timings.json`; the cold case contains source and
reconstruction inventories. JSON analysis exports digests/state only, no shader
instructions. Existing validation: isolated app build, nine native tests and
61 Python-orchestrated regression tests passed before this replay.
