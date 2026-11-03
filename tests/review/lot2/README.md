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
