# MoltenVK descriptor microbenchmark

Manual benchmark for macOS; no game or window is needed. It dynamically loads the
exact MoltenVK dylib supplied on the command line. Timings cover host descriptor
updates, not GPU rendering or total frame time. The optional compute test checks
actual GPU descriptor contents before timing begins.

```sh
cmake -S . -B build -DNXEMU_BUILD_DESCRIPTOR_BENCHMARK=ON
cmake --build build --target nxemu_descriptor_benchmark
glslangValidator -V src/yuzu_video_core/renderer_vulkan/descriptor_benchmark/verify.comp -o /tmp/nxemu-descriptor-verify.spv
build/src/yuzu_video_core/nxemu_descriptor_benchmark "$PWD/bin/arm64/Release/NxEmu.app/Contents/Frameworks/libMoltenVK.dylib" /tmp/nxemu-descriptor-verify.spv
```

CSV output compares individual template entries, coalesced entries, changing
buffer offsets, direct descriptor copies, and copies including snapshot lookup.
Each result is the median of seven batches. Image handles in timed batches are
stable; the compute check alternates two image sizes and changes the pattern on
all twelve submissions. This detects stale copies when destination sets are reused.

The renderer optimization copies only large image/sampler arrays on MoltenVK.
The cache owns at most 16 snapshots of 64 KiB each and forgets all sources when
the recorded submission tick changes. Destinations still come from the original
GPU-fenced allocator. Source descriptors are never changed within that tick.
`NXEMU_DISABLE_DESCRIPTOR_COPY=1` restores full template updates for an A/B run.
`NXEMU_TRACE_PERFORMANCE=1` adds cumulative copied/written descriptor counters.
