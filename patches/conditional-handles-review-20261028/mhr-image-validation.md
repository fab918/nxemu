# Monster Hunter Rise 16.0.2: image validation

The diagnostic repeat records program 0100b04011742000 (Monster Hunter), channel 0,
for both cases. The earlier temporal association with the software-keyboard
applet 0100000000001008 did not establish ownership; the channel diagnostic now
shows these requests belong to the game.

## 2D views of layered backing memory

Captured TIC entries select Texture2D, dimensions 256x256, Depth()=6, and successive
BaseLayer() values 0..5. Their addresses advance by 0x56000. Both ImageInfo and
ImageViewInfo asserted Depth()==1, although their non-array 2D handling already
uses a single plane and the selected base layer. The correction removes that
extra restriction for Texture2D and Texture2DNoMipmap only. Backing image sizing,
layer stride, selected view layer and single-layer view extent remain unchanged.
Array, cubemap and 3D handling are not altered.

## 2D source slices joining a volume

The captured joins have a 32x80x1 single-layer e2D source and a 32x80x32 e3D
destination. Successive source addresses map to base.layer 0,1,2,... . The existing
copy builder already emits extent.depth=1, source layerCount=1, destination
layerCount=1, destination baseArrayLayer=0 and destination offset.z=base.layer.
Its assertion nevertheless required an e3D source.

The validation now also accepts an e2D source with exactly one layer and depth
one. The single-mip restriction is retained. Copy generation is unchanged.
Vulkan explicitly supports 2D/3D copies: for a 2D source, extent.depth equals the
source layerCount (VUID-vkCmdCopyImage-srcImage-01791), and the 3D destination uses
baseArrayLayer=0 and layerCount=1 (VUID-vkCmdCopyImage-dstImage-04444):
https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImage.html

## Validation and limits

A regression runs production ImageInfo, ImageViewInfo and MakeShrinkImageCopies.
Before the fix, the captured-descriptor test and slice-copy test each abort on
the original assertions. Afterwards they pass. Tests cover all six face indices,
Texture2D/Texture2DNoMipmap, one-layer views, unchanged array/volume depth, all 32
slice indices, copy bounds, and existing 2D scaling and volume-copy behavior.
Ten targeted tests pass and the application builds with project MoltenVK
**eb9a230c**. Bundle signature verification succeeds.

This corrects overly restrictive validation; it does not change GPU data or
prove a visual improvement. In-game repetition with the corrected build is still
needed to confirm the assertions disappear. The five MSAA-download warnings are
a separate, unsupported readback path and are not suppressed or implemented by
this change. No general cross-dimension copy support is claimed.

Temporary per-program diagnostics were removed from production sources. Raw
logs and before/after artifacts remain local in build/mhr-image-investigation.
