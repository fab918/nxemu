# Startup variant audit — Windows, 2026-10-23

This audit validates differing cached environments that normalize to the same
pipeline key. It does not change the normal renderer's selection policy.

## Method

Set `NXEMU_NXSHADER_AUDIT_VARIANTS` to an output JSON path before launching a
validation build. The audit runs during startup only, with explicit opt-in.
For each ambiguous graphics key, it captures the first conflicting pair of
cached environments (at most 256 pairs and 32 MiB of copied inputs).
Each pair goes through the normal graphics pipeline compiler three times:
first environment, the same environment again as a determinism control, then
the second environment. All three native creations must succeed.

The audit compares:

- SHA-256 hashes of every emitted SPIR-V module, in emission order;
- every field of `Shader::Info`, including descriptor collections, constant
  buffer sizes, varying masks and interface flags, by value rather than padding;
- stage indices and the final line-rasterization flag.

VTG interface emulation is explicitly unsupported by this comparison: an audit
using it is counted separately and cannot report full equivalence. The report
contains hashes, difference masks and comparison results, without guest shader
instructions or SPIR-V instructions.

## Observed result

Mario Kart Mac profile corpus, RTX 5070 Ti, Windows:

| Check | Result |
| --- | ---: |
| Distinct ambiguous graphics keys examined | 191 |
| First conflicting pairs with identical SPIR-V and interfaces | 191 |
| Different results | 0 |
| Native creation failures | 0 |
| Non-deterministic controls | 0 |
| Unsupported VTG interfaces | 0 |
| Truncated / cancelled | no / no |

The startup subsequently completed 1,096 normal pipeline requests
(1,091 graphics and 5 compute). The test instance closed normally, exit code 0.
The audit's three diagnostic compilations per pair are additional work;
this run is not a cold-cache timing or FPS benchmark.

This proves equivalence for the examined pairs on this GPU and build. It does
not examine every possible pair within each key, prove cross-GPU equivalence,
or validate a different game update. Conservative quarantine of ambiguous
startup keys remains enabled. No automatic merging follows from this result.

## Build and regression validation

NXEmu application and modules rebuilt in the isolated validation checkout
`build/shader-profile-poc/master`, with the feature sources and latest fixes.
This excludes unrelated dirty controller and Vulkan diagnostic changes.
Build completed successfully. Existing version-generation / absent Sciter
project warnings remain in the build log.

The native tools and tests also rebuilt successfully. Validation:

- `ctest --test-dir build/nxshader-feature-tests -C Release --output-on-failure`:
  8 tests passed, including metadata inequality and startup variant safety.
- Python regression orchestration using the rebuilt C++ exporter, reader and
  codec/search library: 59 tests passed. Production processing remains C++.

Local evidence (ignored build artifacts):

- `build/nxshader-native-workflow/review-community/variant-audit-interfaces.json`
- `build/nxshader-native-workflow/review-community/timings.json`
- `build/nxshader-native-workflow/review-community/smoke.json`
- `build/nxshader-native-workflow/review-interface-build.log`

Audit JSON SHA-256: `3750e88e6f83ed8f0a400de2c9a75791eb72376d993c61ddf8582002bdb28c53`.

Mac validation and controlled gameplay/FPS comparisons remain separate work.
