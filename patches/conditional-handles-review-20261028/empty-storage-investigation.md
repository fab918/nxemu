# Repeated empty storage descriptor warnings

A temporary, per-pipeline diagnostic in Mario Party identified a null descriptor
at cbuf0 + 0x510, guest descriptor address 0x556600510. Both its address and the
following size word were zero. Four fragment shaders requested it:
`bc95db29cf34f5a0`, `744d29c7c4d229f2`, `0819396b766882a4`, `4f2534ed16ce1e6d`.
The observed descriptors were read-only and these shaders did not use the global
memory lookup fallback.

Offline translation with the instruction stream, program header, cbuf values and
texture pool metadata extracted from the session's shader cache retains actual
LoadStorage32 instructions for that descriptor under control flow. It is not an
unused entry that can simply be removed. The CPU-side warning reports binding,
not execution of a particular shader branch; it does not establish whether those
loads executed in this scene. No GPU pointer substitution, forced buffer size,
branch elimination or claim that the descriptor is harmless is justified here.

The committed change only throttles repeated warnings for the same empty
(address=0, requested size=0) descriptor. Occurrences 1, 2, 4, 8, ... are reported,
with their count. A change of descriptor address, cbuf index/offset or read/write
mode is immediately reported and starts a new count. The state is fixed-size and
belongs to BufferCache, under its existing locking contract. Nonempty invalid
buffers retain their warning on every occurrence. The NULL_BINDING return and
all memory access behavior are unchanged. Temporary shader diagnostics were
removed from production sources after collecting the evidence.

The synthetic regression exercises the production counter: 12,600 identical
empty bindings yield 14 messages; each changed key field and a return to a prior
key are immediately reported, and different caches have independent state.
This removes repeated formatting/logging work; it is not an emulator FPS
benchmark or proof that the underlying optional-buffer path is correct.

Raw session logs, shader bytecode, cached environments and replay IR remain local
under build/null-storage-investigation and are not committed.
