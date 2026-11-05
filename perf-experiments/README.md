# TOTK performance experiments (archive)

This branch keeps everything from the November 2026 performance work that is not in a clean branch. It is for reference only: do not merge it.

The clean work is in two branches:
- `perf/gpu-thread-write-tracking`: G2 JIT write map, draw texture cache, pipeline fast path. Pushed.
- `perf/draw-cuts`: first per-draw cuts on the GPU thread. Work in progress; see its `HANDOFF-draw-cuts.md`.

## Code committed here (not in the clean branches)

- **LTO/PGO build support** (`src/cmake/modules/nxemu_lto.cmake`, `ipo_probe/`): PGO on the video and cpu modules gives about +8 % in TOTK. The profile must be retrained after code changes. LTO on nxemu-os breaks TOTK.
- **Perf CSV logging** in `src/nxemu/ui/views/main_window.cpp`: env `NXEMU_PERF_CSV` and `NXEMU_PERF_UNLOCK`, used by `kit/Run.ps1` and `kit/analyze.py`.

## patches/ (experiments kept as patches)

| Patch | What it does | Result |
|---|---|---|
| `direct_ubo_full.patch` | UBOs read in place from guest memory imported with `VK_EXT_external_memory_host`, plus TEMP `NXEMU_HOST_SYNC_FENCES` and `NXEMU_NO_DIRECT_UBO` | Wrong rendering without host sync: guest fences fire before the host GPU runs. With sync, no gain (104.7 vs 105.6 fps) |
| `ubo_stream_reuse.patch` | Reuse the previous UBO stream copy when the bytes match | Wrong rendering, same cause as above |
| `ubo_epoch_cache.patch` | UBO binding cache keyed on the memory tracker epoch | 4 % hits, no gain |
| `critical-path-tests.patch` | Causal tests: fence stats, `NXEMU_NO_DELAY`, `NXEMU_IDLE_SPIN_US`, `NXEMU_DRAW_SPIN_NS`, GPU and guest spins | +300 ns per draw gives +1.46 ms per frame, so GPU-thread work is on the critical path |
| `jit_perfmap.patch` | Windows perf map for dynarmic blocks (`NXEMU_PERF_MAP_DIR`) | Used with `tools/jitmap.py` to profile guest code |
| `g2-async-reprotect-full.patch`, `g2-hot-pages.patch`, `full-before-cleanup.patch` | Earlier G2 variants and the full tree before TEMP cleanup | Superseded by `perf/gpu-thread-write-tracking` |

## tools/

| File | Purpose |
|---|---|
| `stackprof2.cpp` (and the older `stackprof.cpp`) | Sampling profiler that runs from outside the process: `stackprof <pid> <seconds> <symbol path> [tid...]`. Set `STACKPROF_RAW` to dump raw addresses. Build with `cl /O2 /EHsc stackprof2.cpp` |
| `jitmap.py`, `jitregion.py` | Attribute raw JIT samples to guest blocks or regions, using the perf map |
| `hostimport.cpp` | Test of `VK_EXT_external_memory_host` on a pagefile-backed section |
| `g2_stats.h` | Old draw counters |

## kit/

The A/B measurement scripts. They expect the layout of `D:\Emulation\switch\NXDev\nxemu\build\PR2-AB-20261104`, with variant folders that hold the binaries.
- `Run.ps1` runs one game for N seconds, presses keys to load the save, and takes screenshots.
- `analyze.py <label>` prints frames, mean, p50, p5 and frametimes.

## profiles/

GPU thread and guest thread profiles at about 104 fps, used for the analysis in `HANDOFF-draw-cuts.md`.
