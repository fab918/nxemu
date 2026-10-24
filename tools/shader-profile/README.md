# NXShader POC (cache v20)

Offline CLI prototype. Export and discovery read a local `vulkan.bin` (or the
supplied ZIP); import reads only the `.nxshader` profile and the recipient's game
files. No renderer or internal cache format change is required. The production
UI change only makes the existing game-path command-line argument work.

This is **not yet a portable community pipeline format**. The v20 graphics key
contains host feature choices and a C++ structure image. NXEmu skips incompatible
graphics keys when prewarming. A successful reconstruction does not imply a
successful precompilation, gameplay coverage, or reduced stutters.

## Commands

Python 3.11+ and the standard library are sufficient. Run from the repository root.
All output paths must be new: tools refuse to overwrite an existing cache/profile.

```powershell
python tools/shader-profile/nxshader.py export cache.zip profile.nxshader `
  --title-id 0100152000022000 --content-label "source game build unknown"

python tools/shader-profile/nxshader.py scan cache.zip GAME_ROOT raw.nxshader `
  "base.dnsp" "update.dnsp" --title-id 0100152000022000 `
  --content-label "base v0 + update v3.0.3; source cache game build unknown"

python tools/shader-profile/scan_compressed.py cache.zip raw.nxshader GAME_ROOT `
  resolved.nxshader "base.dnsp" --yaz0-library PATH_TO_LOCAL_YAZ0_LIBRARY

python tools/shader-profile/nxshader.py import resolved.nxshader GAME_ROOT `
  NEW_TEST_CACHE/vulkan.bin --allow-partial --yaz0-library PATH_TO_LOCAL_YAZ0_LIBRARY

python -m unittest discover -s tools/shader-profile -v
```

`scan` arguments name files **relative to GAME_ROOT**. Rename the example game
container names to the user's actual local files. `export` cannot infer the title
or game version from the cache: the labels are explicitly supplied context.

`import` fails closed if any pipeline lacks a verified shader reference: exit 2,
report only, no output cache. `--allow-partial` writes only complete pipelines.
An empty partial cache does not constitute successful sharing. Import checks each
recovered shader's full SHA-256; it never substitutes an approximate match. An
existing output cache is never changed. Only install the generated local cache
into a **private test user/shader/TITLE_ID/** directory with NXEmu stopped.

For raw matches the reference contains a physical game-container offset and byte
size. Yaz0 references contain a physical compressed-container span plus an offset
in its **decoded** bytes. Shader full hashes verify both forms. References label
the NCA/RomFS resource when the plaintext parser can map it. An unmapped patch
section is explicitly marked `patch-unmapped`; its physical offset is not claimed
to be a virtual RomFS offset. GPU addresses in compilation metadata are never
used as file offsets.

## Optional fast Yaz0 decoder

The Python decoder is portable but slow on multi-gigabyte resource discovery.
Build the provided bounded decoder locally; do not load an untrusted library.

```text
Windows (Visual Studio developer shell):
  cl /O2 /LD /EHsc tools/shader-profile/yaz0.cpp /link /OUT:yaz0.dll
Linux:
  c++ -O2 -shared -fPIC tools/shader-profile/yaz0.cpp -o libyaz0.so
macOS:
  c++ -O2 -dynamiclib tools/shader-profile/yaz0.cpp -o libyaz0.dylib
```

The library path is a local CLI option, never a manifest field. Shader
reconstruction from compressed resources groups requests so each required
resource is decoded once per import. No scanning/decoder runs during gameplay.

## Profile contents and boundaries

`.nxshader` is deterministic gzip-compressed UTF-8 JSON, schema `nxshader-poc/1`:

- Source cache digest, explicitly supplied title/build context, v20 ABI tag.
- Shader table: SHA-256, byte size, verified local game references.
- Environment table: captured compilation scalars, texture/cbuf query maps,
  stage, workgroup/shared-memory parameters, and program-header digest.
- Pipeline records: environment IDs and little-endian packed v20 key (hashes,
  cbuf/texture sizes, fixed configuration). No guest shader instructions,
  SPIR-V, MSL, native driver binary, game files, or copied program header.

Program headers are recovered from the shader's local game bytes. The parser
rejects an environment whose separately serialized header differs from that
prefix; it does not silently export otherwise unclassified binary payloads.

Only the observed LE v20 layout (1400-byte graphics key, 64-byte compute key,
32-bit enums, 80-byte graphics header) is implemented. Unsupported versions,
inconsistent spans, counts, stages and truncated records are rejected. This
does not replace the validation/security work needed before accepting arbitrary
community uploads. Container references are tied to the exact layout of the
local plaintext `.dnsp` packages; repacked/encrypted copies need an extractor
adapter and resource/content identifiers, not the same raw offsets.

Discovery covers raw bytes of the supplied plaintext packages and Yaz0 resources
indexed in base RomFS. Update BKTR virtual assembly, Yaz0 streams in unmapped
patch sections, compressed NSO sections, arbitrary resource compression and
runtime shader mutation/generation are not classified by this POC. A missing
match is **unresolved**, not evidence of dynamic generation.

## Local measurement helpers

`prepare_probe.py` applies startup counters and frame-interval CSV collection to
a **fresh separate source checkout**. These probes are not production changes.
The startup log distinguishes attempted builds, successful builds and host-feature
rejections. Existing build-function logs count runtime compilation attempts,
including attempts which could fail. Frame CSVs distinguish active emulator
frame duration and wall-clock interval between emulated frames; these are not
GPU-present timestamps and do not prove shader causation for each slow frame.

`nvidia_profile.py` creates/verifies/removes one temporary profile for the uniquely
named `nxemu-shader-poc.exe`, with NVIDIA shader disk cache OFF. It never changes
the global profile and does not delete the user's native caches. APIs and ABI:
[NVIDIA NVAPI](https://github.com/NVIDIA/nvapi),
[driver setting](https://docs.nvidia.com/nvapi/_nv_api_driver_settings_8h_source.html).
Only use it for the explicitly authorized local POC; always remove the test
profile afterwards. The module's `use_vulkan_driver_pipeline_cache=false`
independently disables NXEmu's `vulkan_pipelines.bin` use.

`prepare_runtime.py`, `stage_run.py` and `run_matrix.py` are this-machine test
helpers with explicit local paths. They copy private profiles/saves/firmware and
must not be treated as portable shipping scripts or distributed with those data.
The original Mario Kart emulator caches are moved to a local backup, not erased.
The run matrix uses 90-second no-input sessions, automatic graceful shutdown,
identical copied saves and per-process disk-cache policy verification. It samples
title/attract rendering, not a controlled player-driven course or full game.

The timer in the local test checkout is enabled by `NXEMU_POC_RUN_SECONDS`;
it is not part of the production direct-launch patch. Windows measurement source
is based on upstream/master `e1faa8c99c5571884d934a23e30d4fab9a539c0f`.
