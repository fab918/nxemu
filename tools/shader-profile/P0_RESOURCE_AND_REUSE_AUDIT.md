# P0: missing resources and duplicate native creations

Recorder reporting clarification (2026-10-24): report schema 2 distinguishes
loaded and newly observed recipes. `observer_wall_ns` is elapsed Observe time,
not thread CPU time, and excludes register revision tracking. The log now uses
the same name. Worker IO includes close as well as open/write/flush; its CPU
samples may round to zero and are not a frame-time measurement.

Follow-up to WINDOWS_RACE_ANALYSIS.md, 2026-10-23.

## HLE reuse: two defects corrected

The four candidate pairs from the manual replay were compiled independently at
startup using their own HLE/plain keys and captured environments, with a repeated
control compilation. All four produced identical SPIR-V, full Shader::Info
interfaces and rasterization flags, and successful native creations on this RTX.
The broader audit compared 120 HLE/plain pairs: 120 identical, zero different,
zero failed, zero nondeterministic, zero unsupported and no truncation. This is
evidence for this GPU/corpus, not a general cross-GPU proof.

Two runtime defects explain lost reuse:

1. A first translation can narrow conservative CBUF sizes. The cache published a
   normalized alias of the same pipeline but did not propagate its known
   replacement-free HLE eligibility. A subsequent plain draw missed that flag.
2. A plain asynchronous draw could queue a native build before reaching the
   synchronous constructor's HLE twin lookup.

Normalized alias publication now transfers eligibility only if the alias points
to the exact same pipeline. An existing different pipeline cannot inherit it.
The plain slow path checks a known replacement-free HLE twin before scheduling
an asynchronous build. These checks run on pipeline misses/alias publication,
not on every draw. Replacement-bearing variants retain their own pipelines.
No shader/profile/cache wire-format change was made.

Regression cases cover normalized eligibility, plain twin lookup, replacement
variants, conflicting existing aliases, identity aliases, failed and missing
pipelines. Nine native and 61 Python-orchestrated regression tests passed. The
isolated Windows application compiled successfully. A new full manual course
measurement is still required to quantify the reduction from these fixes; do
not present four fewer native calls as an already measured replay result.

The corrected application launch check exited normally, completed all 998
startup pipeline requests with 120 HLE aliases, and recorded no runtime
translations during its short boot/menu window. It reused the copied local
cache without reading or decoding profile resources. This does not replace a
manual cold/profile course comparison.

## Four missing programs: exclusion still necessary

The new C++ offline `resource-gap-audit` reuses the discovery logical storage
walkers. It scans base and update physical RomFS, NSO segments and the patched
logical RomFS, decoding supported Yaz0/LZ4 resources. It compares complete target
bytes around header/body anchors, retaining only candidate digests, offsets and
differing byte positions. Its output contains no instructions. This expensive
diagnostic is not part of production export, startup or draw execution.

The audit visited 11,810 resources (28,924,079,649 decoded bytes across base,
physical update and logical views; not unique on-disk bytes), evaluating 95,040
candidate anchors. None of the four missing programs matched exactly. An
additional pass allowing overlapping prefix anchors produced byte-identical
reports, so the non-overlap batching convention does not explain these four
omissions. Discovery's production search semantics were left unchanged.

The nearest candidates still differ substantially. The 624-byte target shares
341 initial bytes with a candidate in `UI/cmn/boot/bt_TitleLoading_00.szs`, but
195 bytes differ over the full target-sized window. That candidate is not a
verified source reference and must not be imported as the missing program.
The other targets likewise lack a verified exact reference. These observations
do not prove dynamic generation, establish a safe transformation, or exclude
unsupported/nested encodings. The terminal word is included in the captured
programs; there is no demonstrated safe eight-byte truncation repair here.

Coverage remains 1,076/1,080 source records. Two omitted programs were requested
in the manual replay. Keeping local compilation for them is currently required
for correctness. The next useful investigation is opt-in provenance tracing at
guest shader upload/resource transformation, if full recovery remains a goal;
further blind substitutions or relaxing verification are not justified.

## Evidence

Ignored files under `build/nxshader-native-workflow/`:

* `hle-alias-audit-1/alias-audit.json`: GPU output/interface audit;
* `resource-gap-audit.json` and `resource-gap-overlap-audit.json`: resource results;
* `windows-roundtrip-profile-race-1/comparison-analysis.json`: original candidates;
* `hle-reuse-smoke-1/`: corrected application launch check.

The resource audit is a standalone test executable. It intentionally includes
the discovery implementation in its own translation unit to reuse private
walkers without exposing a new production resource API.
