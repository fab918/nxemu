# Lot 2 review and A/B validation

The original cumulative patch is experimental and is **not approved for merge**.

## Revisions

- Reference: `42ef588ea0` (startup fixes, without Lot 2).
- Civilization candidate: `d89e325b50`.
- Sacred candidate: `81962a2dc6`.
- Candidate build prerequisite: `f95376b05a` (missing strided descriptor helper).

The original patch applies to the reference but does not compile: `texture_pass.cpp`
calls `BindlessDescriptorCountStrided`, which is missing from that revision. The
helper is supplied as a separate commit rather than hidden inside the experiments.

## Confirmed blocker: TIR loses untouched color

`vk_render_pass_cache.cpp` discards the temporary multisample color contents
(`DONT_CARE`, `UNDEFINED`) and resolves that image into the single-sample target.
There is no initialization from the existing target. Partial draws therefore do
not preserve the original destination. Destination-dependent blending has the
same missing precondition and remains unvalidated.

A standalone Vulkan reproduction uses an 8x8 blue target, a 2x2 red clear inside a
render pass, and readback of the untouched bottom-right pixel. It reproduces the
attachment setup used by the patch; it does not launch the emulator.

On NVIDIA GeForce RTX 5070 Ti:

| Variant | Untouched pixel (RGBA8 word) | Redrawn pixel |
|---|---|---|
| Reference LOAD | `ffff0000` (blue) | `ff0000ff` (red) |
| Candidate MSAA scratch + resolve | `00000000` | `ff0000ff` (red) |

Khronos validation reported **0 errors**. This is a semantic content-preservation
failure; a clean validation log does not establish correctness.

Run `tir_partial.cpp` with Vulkan SDK headers/library and the installed
`VK_LAYER_KHRONOS_validation`. Exit 1 means preservation fails, as expected for
this candidate. This file is a manual GPU reproduction, not permanent runtime
instrumentation.

## Sacred: partial-write coherence remains a merge gate

`ForEachWrittenComputeStorageBuffer` enumerates bound storage-buffer ranges that
*may* be written; it does not determine the actual bytes written by a dispatch.
`UpdateImagesFromBuffer` then imports the whole image if that binding covers it.
Exact base address and full binding coverage do not demonstrate that untouched
bytes in the buffer match a newer GPU-rendered image. A regression case must start
with newer GPU-only texture contents, modify only part through a storage buffer,
and verify that the remaining texels survive. That integration case has not yet
been demonstrated; no correctness claim is made for it.

## Checks completed

- Original patch applicability: PASS.
- Original full module build: FAIL (missing helper above).
- Descriptor-helper bounds: 1,164,800 enumerated cases PASS.
- New unswizzle compute shader: glslang compilation and `spirv-val` PASS.
- Vulkan partial-color preservation: FAIL (hardware reproduction above).

## Remaining measurements

Game FPS and GPU time comparisons, OpenGL gameplay, TIR blending, resolution
changes, and Sacred indexed-texture/partial-write gameplay coverage are **not
measured** in this report. Do not substitute startup/menu FPS for comparable
in-game samples. No performance improvement or regression percentage is claimed.

The two executable packages are intended for user A/B testing. Keep resolution,
renderer, driver, game update, save, scene, and shader warmup comparable. Run one
instance at a time. The experimental package must not replace the reference.

The two supplied Eden commit URLs returned HTTP 403 during this review; their
exact diffs could not be independently retrieved. Related Eden source in the
previously supplied archive was available, but is not proof of those commit IDs.

## A/B packages built

Both complete Windows Release runtimes compiled successfully with the same MSVC
2022 toolchain. Both include the Vulkan and OpenGL renderer translation units.
The candidate requires the separate helper commit above; the unmodified supplied
patch fails to compile. Both packages opened the library UI in a smoke check.
This does not establish in-game stability or an FPS/GPU-time comparison.

Local packages:
- `D:/Emulation/switch/NXDev/nxemu-lot2-ab/A-reference-42ef588`
- `D:/Emulation/switch/NXDev/nxemu-lot2-ab/B-experimental-lot2`

The candidate's application version string can still show the base `42ef588`
because the project was configured before the experimental commits. The variant
folder, `REVISION.txt`, and `SHA256.json` identify the actual packaged modules.
The reference was rebuilt from the exact committed `42ef588ea0` source tree.

Packaged graphics module SHA-256:
- A: `826648C93FB4624446DE015C0B214F233A7445FB4E231AA13263208BD26562B1`
- B: `2F7511DF071E13E8C1CAE60E5AD7F07A945FD726F580618E32C6B314C658D4FC`

## Sacred performance investigation (2026-11-03)

The user's A/B captures show a missing 3D scene at 30 FPS in A and a visible
scene at approximately 5 FPS in B. Both logs contain thousands of Maxwell DMA
assertions. A additionally reports sampled-descriptor budget exhaustion. Those
shared DMA assertions alone do not explain the performance difference.

Reverse applicability of the supplied cumulative patch passed: the experimental
implementation had not diverged from it except for the missing bounds helper.
The latest available `eden-master (7).zip` was inspected. Its handle-combination
code supports fixed descriptors; it does not implement our draw-uniform index
metadata and therefore does not directly supply this correction.

Temporary external stack sampling identified descriptor preparation on the CPU.
Temporary shader diagnostics then showed thousands of descriptors for an index
loaded from a constant buffer. `TryGetConstBuffer` discarded `has_index_cbuf`
and associated metadata when OR-combining that texture with a fixed sampler.
Copying the primary address before assigning the sampler preserves that proof.
The shader cache versions are incremented for Vulkan and OpenGL so stale shader
metadata and compiled shaders cannot retain the expanded arrays.

`test_uniform_sampler` calls production `TexturePass` with the observed handle
expression. Before the correction it fails with 4,146 descriptors and no uniform
index. Afterward all four cases pass: one descriptor for a draw-uniform index,
and a full array for a varying index, each with either OR operand order.
The standalone regression and Release graphics module build pass. No diagnostic
logging or profiler is added to the runtime.

Local variant C is `D:/Emulation/switch/NXDev/nxemu-lot2-ab/C-sacred-performance`.
The corrected Vulkan title menu renders the character and background at 30 FPS
(33.33 ms displayed), compared with the original candidate's roughly 5 FPS.
This is a menu observation, not a GPU timestamp benchmark or a full gameplay
validation. Character-selection and gameplay comparisons remain user tests;
OpenGL has compiled but has not been measured in-game. The existing experimental
TIR preservation and compute partial-write merge gates above still apply.

## NVIDIA uniform-upload optimization (2026-11-03)

With the same experimental 240 FPS game mod enabled, external stack sampling
showed repeated buffer synchronization and memory protection calls. When the CPU
changes uniform data already consumed by a draw in the current frame, the cached
upload cannot be reordered before that draw. The existing streaming path makes a
separate upload snapshot instead. Enable it for the NVIDIA proprietary Vulkan
driver as well as MoltenVK. Other drivers and OpenGL retain their current policy.
The existing caller guards still require CPU-modified contents and reject any
GPU-modified range, preserving the cached synchronization path for GPU writes.

Local comparison: Sacred 2 1.0.1 title menu, Vulkan, RTX 5070 Ti, 1x resolution,
same 240 FPS mod, same configuration and warmed shader caches. The existing
opt-in FPS counter was enabled only in the measurement processes, without adding
runtime instrumentation. Twenty one-second samples after startup yielded:

| Graphics module | Frames / seconds | Weighted FPS | One-second range |
| --- | --- | --- | --- |
| C before uniform streaming | 1,506 / 20.117974 | 74.86 | 71.41–77.57 |
| C with uniform streaming | 2,155 / 20.141772 | 106.99 | 99.40–110.26 |

The old module was restored for the control run and returned to approximately
77 FPS; the candidate was then restored. This is about 43% more menu frames per
second in this local sample, not a gameplay guarantee. The character and animated
background remained visible. No GPU timestamp benchmark or full gameplay
coherence validation is claimed. The Release module and the existing uniform
sampler regression build/pass; this regression covers the earlier shader fix,
not all buffer-coherence scenarios. C packages the optimization; A and B retain
their original graphics modules.
