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
  bytes. Glyph runs come with the text service.
- **Paint order** is depth first; per node, its outer shadow, its box,
  its inner shadow (inside the padding box) and its image, then its
  children. Nodes, and the subtrees below them, that would draw nothing
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
  Shadows and radii keep their exact values.
- **Golden lists:** the tests describe lists in text, floats with nine
  significant digits, and compare them against checked-in lists; each
  list is also built twice and its bytes compared.

## Consequences

A renderer draws the list in order, binds the clip chain and transform
per command, and batches across both. Byte-identical output lets a host
skip a frame whose list did not change, and lets a test pin a scene.
Per-command opacity is cheaper than a layer and wrong where a faded
subtree overlaps itself; a group command may come later. Fixed records
waste the bytes a small command does not use, and in exchange need no
parsing.
