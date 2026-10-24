# Runtime quarantine diagnostic — 2026-10-23

The diagnostic preserves the startup quarantine and rendering policy. No local
GPU equivalence decision is written into `.nxshader`.

## Collection

Set `NXEMU_NXSHADER_RUNTIME_REPORT` to a JSON path before launching NXEmu. The
report is flushed after compiler queues drain when the game stops normally.
It contains key hashes, timing and counters, never shader or SPIR-V instructions.

Collection is opt-in. There is a disabled pointer check at the frame hook, but no
CPU clock sampling, report allocation or diagnostic IO during normal draws.
In diagnostic mode, full keys establish quarantine membership; hashes are only
report labels. Native worker callbacks explicitly retain their diagnostic event.
Thread CPU time and Windows thread cycles are recorded separately: short CPU
work can round to zero in Windows thread-time accounting. Cycles are not converted
to time. Compiler/native spans can nest or overlap and must not be summed as
frame stall duration. Wait spans currently cover pending graphics translation
waits, not every renderer scheduler/native wait.

Bounds: 4,096 tracked quarantined keys per kind, 65,536 events and 120,000 GPU
frame-tick intervals. Every relevant truncation is reported. GPU frame ticks
are not a direct measurement of presentation latency or proof of FPS loss.

## Manual Mario Kart session, RTX 5070 Ti / Windows

The user replayed their scenario in `quarantine-race-1`, with only the Mac profile
and no local shader cache at launch. Global driver caches were not cleared.
Startup processed 2,838 recipes and retained 36 unresolved observations.
All 1,096 startup native builds succeeded; 191 graphics keys were quarantined.
180 quarantined keys were subsequently requested through the slow path.

| Cause | Translation invocations | Native creations | Native elapsed sum |
| --- | ---: | ---: | ---: |
| Key excluded by startup quarantine | 163 | 134 | 23.590 ms |
| Program code absent from profile | 36 | 30 | 700.434 ms |
| Mac observation without serialized environment | 2 | 2 | 11.039 ms |
| Additional combinations/configurations | 8 | 2 | 0.288 ms |
| Total | 209 | 168 | 735.351 ms |

41 translation invocations reused a pipeline instead of creating a native one.
Native creations all succeeded. No runtime pipeline key was translated twice.
This does not assert that individual shader modules were translated only once
across different pipeline keys.

The 36 absent-code invocations involve 35 distinct guest program hashes. Their
code SHA-256 values are absent from the original Mac manifest as well as the
native reconstruction. Comparing full code, eight-byte tail alternatives and
code bodies without the header found no explanation based solely on those
known boundary/header differences. This proves absence from the supplied
profile, not whether the source Mac capture or its export lost them: the Mac
archive for this profile contains no corresponding `vulkan.bin` to distinguish
those possibilities. The separately supplied older cache is not established
as the source of this manifest.

Two invocations match the original manifest's unresolved observations with
reason `no_serialized_environment_for_source_key`. The necessary compilation
environment was not provided in that profile.

Of eight additional variant invocations, three keys use combinations absent
from the profile (two distinct program couples). Five differ from a prewarmed
counterpart only by the HLE hint and have no serialized constant replacements.
All five HLE invocations have zero native creations: the existing HLE sharing
path worked after translation checked the replacement dependencies. They do
not demonstrate an aliasing bug. The other absent-combination HLE invocation
also reused its native twin, leaving two actual native builds in this group.

The largest quarantined native build was 0.741 ms; the largest absent-program
native build was 55.728 ms. These are individual build times, not measured stall
attribution. The session covers about 460 seconds and includes menus/activity
around the course; it has no explicit course start/end markers. 26 GPU tick
intervals exceeded 33.333 ms; 13 did not overlap a compiler/native span. Timing
overlap is correlation, not proof that compilation caused the frame delay.

## Robustness and header comparison

Nine native tests and 60 Python-orchestrated regression tests pass after a
successful isolated NXEmu build. Runtime import/search/decompression and these
collectors are C++.

Native reader tests cover cancellation of both JSON and compact imports without
publishing a cache, successful retry, and preservation of existing output on
repeated import. Completion-policy tests reject cancellation, partial work and
native failures. Completion markers now require nonnegative integer counters;
missing fields can no longer pass as zero.

Integration: removing counters from a real completion marker triggered one
resource revalidation and restored a complete marker. The next launch performed
zero reader calls and zero resource decompression, with all 1,096 startup native
requests completed. Existing truncated-cache-tail tests remain green. Forced
GUI termination and rapid GUI import/close/export cycling were not automated.

`CanonicalGuestHeader` replaces object `memcmp` with explicit comparison of all
20 little-endian guest header words. NOINIT regions denote guest header bytes;
reserved bits remain significant. This preserves the comparison policy and does
not claim that ignoring those fields is safe or recover quarantined keys.

## Evidence

Ignored local artifacts: `build/nxshader-native-workflow/quarantine-race-1/`,
including `runtime.json`, `timings.json`, `launch.json`, `summary.json`,
`compilation-analysis.json` and both inventories. `cache-inventory` is an offline
C++ target using the validated reader, existing key normalization and CityHash;
its inventories contain state and digests, not proprietary instructions.

Next priorities: validate local equivalence against relevant environments before
relaxing quarantine, improve capture/export completeness, and inspect the Mac
source cache corresponding to this profile to distinguish uncaptured programs
from export omissions. Cross-update compatibility and Mac validation stay open.
