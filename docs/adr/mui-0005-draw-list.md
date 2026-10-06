# mui-0005. The draw-command list

Status: Accepted

## Context

Maul UI does not draw: it hands a renderer a description of what to
draw, which another engine, a plain application or the optional
reference renderer turns into pixels. That description must batch well
on a GPU, look the same on every platform, and compare byte for byte
between runs so that tests can pin it. Clay's command array clips with a
scissor stack, so a renderer flushes at every clip change; Dear ImGui
issues a draw call per clip rectangle. WebRender gives every item a
clip chain evaluated in its shaders, which keeps batches whole.

## Decision

- **One list per build** (`maul-ui/draw.h`): `muiBuildDrawList` paints
  a root's subtree, as its last layout left it, into the context's
  list, which `muiGetDrawList` shows until the next build. The list has
  a header (the root's logical size, the scale, the host's surface key
  and a generation that counts builds), a clip table, a gradient table,
  a transform table whose entry 0 is the identity, and the commands.
  The context reserves the tables by its limits; a list that does not
  fit fails whole and leaves the list empty.
- **Commands are fixed-size records:** a kind, a clip index, a transform
  index, and a union per kind. A box has its border box, four radii, a
  fill, an optional gradient by index, and four border widths and
  colors; a shadow its box, radii, color, offset, blur, spread and
  whether it is inset; an image the host's key, a uv rectangle, nine-
  slice insets in image pixels and a tint. Records have no padding and
  are zeroed before they are written, so identical trees give identical
  bytes. A glyph run has a font key, a size, a color, an origin on the
  baseline and a span of the list's glyph table, whose entries are a
  glyph id and a position from the origin, as WebRender and Vello
  carry them; glyph images are not in the list, so it does not depend
  on a renderer's atlas.
- **Host content paints through a function** the draw input names, as
  the layout input names a measure function: for each visible node
  whose content is the host's, with its id, host key and content box
  size, it adds glyph runs and filled rectangles (box commands with one
  fill, snapped as boxes, a side that was not empty keeping a device
  pixel, for underlines and the like) through a sink at positions
  relative to the content box, and the build converts their colors and
  multiplies opacity as for every command. The context refuses edits made from
  it; reads, such as a node's computed text style, are allowed.
- **Paint order** is depth first; per node, its outer shadow, its box,
  its inner shadow (inside the padding box), its image and its host
  content, then its children; host content is drawn inside the node's
  own clip. Nodes, and the subtrees below them, that would draw nothing
  at opacity 0 are skipped, as are commands with nothing visible.
- **Clips are a chain:** a node that clips adds a clip of its rounded
  border box whose parent is the clip it is painted in, and its
  children's commands carry that clip's index. A renderer evaluates the
  chain per command, so clip changes need not break a batch.
- **Corners and sides are physical in the list,** top left first and
  top first, resolved from start and end by the node's direction; an
  image's slice insets do not mirror.
- **Colors are linear light with premultiplied alpha.** Gradients carry
  their stops so, and an interpolation tag, Oklab, as transitions move
  colors. Opacity multiplies down the subtree into each command's
  colors; without a group layer, overlapping descendants show through
  each other.
- **Snapping is per kind,** at the identity transform: box and image
  edges and clip rectangles go to the nearest device pixel by edge, so
  adjacent boxes share an edge, and a non-zero size keeps at least one
  device pixel; border widths go to whole device pixels, at least one.
  A glyph run's baseline goes to a device pixel and its x keeps its
  fraction, as Skia and Chromium position horizontal text. Shadows and
  radii keep their exact values.
- **Retained emission:** the context keeps two of each table, the
  list shown and the one the next build writes. A build with no paint
  requested below the root, of the same root, surface and scale as the
  last, keeps the list and its generation. Otherwise each node's spans
  of the tables are recorded; a subtree no paint request reaches, at
  the origin and opacity it was painted at, copies its spans from the
  last list, renumbering its clips, gradients and glyphs and its
  descendants' spans. Layout requests paint on every node whose rectangle or
  direction is not what was last painted, so a copy is never stale; a
  test checks over hundreds of random edits that a list built from the
  last equals one built whole. Memory is the two lists the limits
  reserve, so nothing is evicted.
- **Golden lists:** the tests describe lists in text, floats with nine
  significant digits, and compare them against checked-in lists; each
  list is also built twice and its bytes compared.
- **The reference renderer** (`maul-ui-rhi`, the option `MAUL_UI_RHI`,
  off by default) draws lists with Maul RHI, found installed or fetched
  at its release tag, as a static library beside `maul-ui` that the
  core and the text component never see (`rhi/`). Every command is an
  instance of one pipeline of quads, its record in a storage buffer
  uploaded each frame and its six vertices made in the vertex shader;
  the fragment shader evaluates a rounded rect's signed distance in
  pixels for coverage, splitting the fill (inside the borders' inner
  edge) from the borders, each side's color where that side is
  nearest. Colors blend premultiplied into an sRGB target. Each frame
  `muiRhiRenderer_AddPasses` adds its upload and draw passes into the
  host's target while the host builds the frame, and
  `muiRhiRenderer_Record` records them after it is compiled; the
  pipeline's creation is answered on the device's queue, which the host
  reads and hands on, and until then frames draw nothing. Shaders are
  GLSL and WGSL made into a Maul RHI container offline
  (`tools/gen_rhi_shaders.py`), the header committed. Its tests run on
  Maul RHI's test driver and on lavapipe, comparing probed pixels with
  what the list says, under the Vulkan validation layer, and its WGSL
  is compiled by headless Chrome's WebGPU, which nothing else compiles.
  Gradients come from the list's table in a second storage buffer,
  mixed in premultiplied Oklab as the core's transitions mix colors;
  shadows are Gaussian blurs of their rounded shape in Evan Wallace's
  closed form (exact along one axis, four samples along the other),
  their shape spread with CSS's radius adjustment and drawn outside
  their box, or inside it when inset. Transforms and clips come from
  their tables in two more buffers: each quad's corners go through its
  transform, and distances are measured in its own pixels and made
  screen pixels by the transform's scale, so edges stay a pixel wide at
  any scale; each fragment is brought back through each of its clips'
  transforms, and its parents', for coverage, an inverted clip keeping
  the outside. The tables always have entry 0, and every index into
  them is checked as the list is packed, so no list makes the shaders
  read past one. Images are textures the host's function names by
  key, asked once a key a frame; an image is one instance whose
  fragment maps its place to a uv piecewise, so a nine slice's corners
  and edges keep their insets' size (shrunk by one factor where facing
  ones would not fit) and its middle stretches, with no seams between
  parts; the texture and a linear sampler are bound in a second table,
  and draws break only where the texture changes, boxes and shadows
  joining any draw. Glyph runs come from a glyph atlas of the
  renderer's own over the host's text service (mui-0006's atlas), its
  pages R8 textures made as the atlas makes them and only the rectangles
  it changed uploaded, gutters included, so every texel a glyph samples
  has been written and a page never has to fit a frame's uploads whole.
  A run whose transform only moves it is drawn as coverage rendered at
  its device pixels, sampled half a texel into the gutter at most; a
  host without the text component's atlas gives no text service, and
  one given is refused. A run a transform scales or turns, or a glyph
  too large for the atlas as coverage, is drawn from distance fields of
  an em of 32, 64 or 128 pixels (the least at least the em drawn, its
  spread an eighth), each sample made a distance in screen pixels.
  As the list is packed, an instance whose quad through its transform
  misses its clip chain's bounds (each clip's rect through its
  transform, met with its parent's; an inverted clip bounding nothing)
  or the target is not drawn, nor is its image asked for; no scissor
  is set, as culling already drops what one would.

## Consequences

A renderer draws the list in order, binds the clip chain and transform
per command, and batches across both. A static frame costs nothing,
and a frame with one change copies the rest of the list, which still
takes time in proportion to it (a third of a build for the benchmark's
40,001 nodes); scrolling alone rewrites only the transform table, one
entry per scroll container (record mui-0007). Byte-identical output lets a host
skip a frame whose list did not change, and lets a test pin a scene.
Per-command opacity is cheaper than a layer and wrong where a faded
subtree overlaps itself; a group command may come later. Fixed records
waste the bytes a small command does not use, and in exchange need no
parsing.
