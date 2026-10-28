# Pokémon Legends: Z-A 2.0.2: surface reduction translation

Four pipeline failures share fragment shader 7863af07078f69a3. The captured
instruction at byte offset 0x370 is 0xeb5006080207000f: a bindless unsigned 32-bit
MAX reduction on a 2D array image. The initial exception was `SURED: Invalid type
4`, from the coordinate builder's missing array cases.

Comparison with the user-provided Ryubing tree also exposes an independent
encoding error: SURED's operation occupies bits 24..26, not 21..23. The old field
misreads this MAX as ADD. Merely adding array coordinates would compile the shader
with the wrong reduction. The corrected translator reads the proper field and
passes full coordinate registers (x, y, array layer) as Ryubing does. It does not
copy the 16-bit layer truncation from NxEmu's separate surface load/store path.
Both 1D and 2D array atomic-coordinate cases are supported by the existing IR.
SUATOM's separate operation encoding is unchanged.

Reference implementation inspected locally:
- Ryujinx.Graphics.Shader/Decoders/InstDecoders.cs: InstSured, InstSuredB, RedOp.
- Ryujinx.Graphics.Shader/Instructions/InstEmitSurface.cs: EmitSured/EmitSuatom.

The captured instruction uses texel addressing. Byte-addressed SURED requires a
coordinate conversion and is explicitly rejected, rather than silently treated
as texel addressing. Existing unsupported clamp modes and 64-bit/float formats
remain rejected. No broad extension of those formats is claimed.

The production translator test covers the captured instruction (expects UMax,
not IAdd), all six dimensional types, SURED/SUATOM, bound/bindless handles, the
full array-layer value, all eight SURED operations and rejection cases. Before
the array fix it reports nine failures. The extended operation checks exposed
the initial incorrect ADD expectation for the captured instruction. After both
corrections the test passes. Offline translation of the full captured shader
now emits ImageAtomicUMax32 and produces SPIR-V (2203 words in the synthetic
replay environment). The in-game diagnostic repeat has zero SURED errors.
Eleven targeted tests pass. This does not establish visual equivalence across
all games or all surface atomic instruction variants.

The repeated unmapped uniform reads are a separate issue: diagnostics show vertex
stage cbuf1, GPU address 0x404002100, declared/bound/used size 65536, but a single
mapped prefix of 57088 bytes followed by a hole. The experimental uniform-buffer
routing change is kept separate from this verified shader fix until tested.
