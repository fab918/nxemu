The NCE mapping tests execute the production Mach mapping code on macOS ARM64.

```sh
cmake -S . -B build -DNXEMU_BUILD_TESTS=ON -DNXEMU_BUILD_PLATFORM_TESTS=ON
cmake --build build --target test_nce_range_clipping test_direct_map nce_native_mapping -j 6
ctest --test-dir build -R '^(test_nce_range_clipping|test_direct_map|nce_native_mapping)$' -V
```

`test_range_clipping.cpp` reproduces broad page-table operations that begin outside
a native reservation or cross the hole between the low and high windows. Before
the fix, guest permissions and the canonical pointer table could change while a
host mapping retained old access. In particular, a read-only protection spanning
the gap left the second host mapping writable. Leading-hole unmaps could leave a
live host mapping after clearing the guest permission.

The regression checks real Mach protections, host/guest permissions, canonical
pointer entries, preservation of the relative backing offset during clipping,
empty and entirely outside ranges, and rejection of a wrapping range before any
mutation. The explicitly invalid wrapping request intentionally logs one error;
valid clipped ranges must log none.

For reproducing the old implementation only, pass `--skip-overflow`: the old
range loop otherwise walks essentially the whole U64 address space. The normal
CTest run includes this overflow rejection check and has a 30-second timeout.
The existing model test additionally checks 4,000 randomized host pages, GPU
access masks and mapping costs. The native mapping test executes guest code and
1,000 NRO text/BSS materialization and restoration cycles.
