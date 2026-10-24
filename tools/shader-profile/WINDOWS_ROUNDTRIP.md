# Windows native recording/export/import round trip — 2026-10-23

## Capture

The user replayed Mario Kart in `windows-roundtrip-cold-1`, with no imported
profile, an empty emulator shader cache and the Vulkan driver-pipeline cache
option disabled. A temporary NVIDIA profile disabling disk shader cache was
created and verified for `nxemu-shader-poc.exe` only. It does not alter the global
profile or delete driver files. Its owner marker is
`build/nxshader-native-workflow/windows-roundtrip-cold-1/nvidia-policy.json`;
restore it with the existing test utility after the comparison sessions finish.

The recorder produced 2,206 unique recipes from 28,202,649 observed draws and
9,351,946 canonical captures, without recording failures. Local outputs:

| Item | Measured value |
| --- | ---: |
| Guest recipe journal | 3,520,784 bytes |
| Local `.nxc` | 5,318,576 bytes, 1,080 records |
| Observer elapsed sum | 9.424 s, about 334 ns per draw |
| Journal encoding elapsed sum | 10.140 ms |
| Journal open/write/flush elapsed sum | 132.946 ms |
| Local cache serialization elapsed sum | 180.074 ms, 1,080 calls |

Observer time is steady-clock elapsed time, despite the legacy log field being
named `cpu_ns`. The structured report calls it `observer_wall_ns`. It includes
measurement overhead and excludes dirty-register revision tracking. Background
IO phases are elapsed work, not frame stall duration. These numbers do not prove
zero FPS overhead. The timing option adds clock calls on draws and should be
disabled in normal use.

## Native export and offline reconstruction

Both were performed by the C++ tools, using `vulkan.nxc`, `guest-recipes.nxr` and
the local base/update 3.0.3 game files. The profile contains no shader instructions.

| Operation | Result |
| --- | ---: |
| First export and resource discovery | 51.516 s, 7,735 resources searched |
| Repeat export with saved index | 4.130 s, 1,052 references reused, zero resources searched |
| Compact Zstd `.nxshader` | 122,129 bytes |
| Verified exportable recipes | 1,444 graphics + 10 compute |
| Unresolved observations | 33 |
| Programs not found by resource discovery | 4 |
| Reconstructed local cache | 1,076 / 1,080 records (99.63%) |
| Offline reader / resource reconstruction | 3.202 s |
| Offline cache append elapsed sum | 253.788 ms |

The two exports are byte-identical. All 1,066 reconstructed graphics records
match original normalized keys and serialized environment semantics, with map
order canonicalized for comparison. No unexpected graphics records were added.
The other ten reconstructed records are compute records. Native creation and
on-course coverage must not be inferred from raw record counts.

The four missing normalized graphics keys require four small programs of
584, 496, 624 and 544 bytes. Their hashes and environment digests are saved in
`roundtrip-coverage.json`. They occur in the captured cache but the current
resource search did not locate them. This does not yet prove they are dynamic;
resource coverage, transformations and boundary handling still need examination.

## Automatic discovery and one-time import

New convention: `<16-digit-lowercase-title-id>.nxshader` beside the game file.
For Mario Kart: `0100152000022000.nxshader`. The export file picker suggests this
name. The main export action is now labeled `Export .nxshader`, rather than
`Choose file`. Existing explicit command-line and manual profile selections
retain priority. Automatic discovery currently requires a trustworthy loaded
content identity; ambiguous multi-update or mod configurations remain a limit.

The test used workspace-local hard links to base/update files, with the sidecar
profile next to the game, no `community.json` and no initial emulator cache.
No source game bytes were modified. Results:

| Startup | Profile reconstruction | Startup wall time |
| --- | ---: | ---: |
| First | 1 reader call, resource verification and decompression | 3,483.1 ms |
| Second | 0 reader calls, 0 resource bytes decoded | 98.169 ms |
| Third | 0 reader calls, 0 resource bytes decoded | 100.521 ms |

All 969 queued native builds completed on the first startup, plus 102 HLE key
aliases; zero ambiguous keys were quarantined. The completion marker's bytes
and modification time remained unchanged across the repeated launches. These
startup figures are not whole-application launch times or course benchmarks.
A small profile fingerprint read still checks whether the selected file changed;
zero reader calls means no repeated reconstruction or game-resource decode,
not that absolutely no profile bytes were read.

Manual import previously verified all game resources in its dialog and repeated
that work at startup. It now validates bounded profile metadata/title only,
then stages the selection. Game verification, reconstruction and native warmup
occur at startup. The UI describes this as selection, not completed warmup.
Both JSON and compact metadata inspection succeed with game resources absent;
the full reader still rejects those absent resources and mismatched title IDs.

## Workflow still to finish

The general export dialog still needs automatic resolution of the effective
update source. Its saved last-export update is reused, and this particular test
saved the successfully used update and index at the dialog's normal locations.
That is not a general solution for multiple updates, moved files or mods.
Do not infer the active update from a filename or silently pick the newest one.

The first export is not instantaneous. It still performs resource search. The
next improvements should target the large number of repeated canonical captures
and allocations (9.35 million captures for 2,206 unique recipes), then retaining
verified resource provenance so an export does not discover everything from
scratch. Journal and `.nxc` layout changes need measured benefits; their current
background write totals are much smaller than observer and discovery costs.
The portable file is already small at 122 KB.

## Validation and remaining comparison

The isolated Windows app/modules and native tools compiled successfully.
Nine native tests and 61 Python-orchestrated tests passed, including sidecar
selection and metadata inspection without game-resource IO. The user completed
the profile replay: 47 runtime translations and 33 native creations, compared
with 1,080 and 1,007 in the cold capture. See WINDOWS_RACE_ANALYSIS.md for complete
classification, duplicate candidates and benchmark limitations. The temporary
per-executable NVIDIA disk-cache override was removed after this comparison.
No claim of 100% overall recovery or zero stutter is made.

Ignored evidence: `build/nxshader-native-workflow/windows-roundtrip-cold-1/`
and `windows-roundtrip-autoimport-1/`; inspect `roundtrip-results.json`,
`recording-cost.json`, `runtime.json`, `export-result.json`, `first-timings.json`,
`second-timings.json`, `timings.json` and `marker-reuse-check.json`.
