# MoltenVK constant-buffer declarations

After building the application, run `tests/spirv_constant_buffers/run.sh`.
The test instantiates the production SPIR-V EmitContext and inspects the emitted
uniform variable, pointer, struct, array and constant declarations.

It covers the Yoshi Metal validation case (offset 116992 within a 131072-byte
buffer), independent sizes for different descriptors, 16-byte alignment, minimum
size, dynamic access, global-memory helpers, and unchanged other-driver profiles.
The change adds no per-draw allocation, copy, or synchronization.
