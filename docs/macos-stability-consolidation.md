# macOS stability consolidation

Branch: fix/stability-macos. Base: upstream/master 687cab47. Date: 2026-10-22.

## Included changes

- a89fcb26: preserve Vulkan query banks until GPU completion and preserve traversal links while releasing/recycling banks.
- 9af751bd: reset host-thread state across kernel sessions and cancel shutdown syncpoint waits.
- 8c51d39f: size Metal constant-buffer declarations from actual usage and clamp GPU readbacks to each source buffer.

The video ABI advances from 0x0126 to 0x0127 for cancellable WaitHost. CPU/OS ABI versions retain upstream values. The SPIR-V test owns its logging/assertion stubs instead of depending on the NCE test tree.

## Scope and remaining candidates

No NCE enablement, experimental 36-bit support, framebuffer-fetch optimization, UI redesign, fullscreen changes or runtime performance instrumentation is imported.

cb71438e (Dynarmic startup stability) applies empty: already integrated upstream. Runtime changes from e49b3071 (device loss and presentation errors) are also already present; its older build wiring is not restored.

Older shutdown candidates 33cdf15a and a5ba1279 remain preserved on their original branches. They predate the current CDMA/composite/display ownership model and need separate adaptation and runtime review before inclusion. This branch does not claim that work completed. NCE startup/NRO fixes remain with their native-execution dependencies.

The remaining integer-texture/float-declaration Metal validation error and earlier Sonic shutdown heap crash are not claimed fixed.

## Validation

- Session lifecycle: AddressSanitizer/UndefinedBehaviorSanitizer pass, including 300 completion/cancellation races.
- Production query-bank traversal helper: AddressSanitizer/UndefinedBehaviorSanitizer pass (recycled links, partial/empty banks, concurrent recycling).
- Syntax compilation with this branch's source headers and existing macOS dependency configuration: core.cpp, kernel.cpp, nvhost_ctrl.cpp, video_manager.cpp, spirv_emit_context.cpp, vk_query_cache.cpp and vk_buffer_cache.cpp pass.
- git diff --check passes.

Full app build, Vulkan validation-layer integration tests and game start/stop/reopen tests have not been repeated on this combined branch. Link the SPIR-V layout test against a shader-recompiler library built from this branch, not the framebuffer-fetch branch: their shader structures differ.
