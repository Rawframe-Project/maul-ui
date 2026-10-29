# Draw list

Maul UI does not draw. It hands a renderer a description of what to draw, which another engine, a plain application or the [reference renderer](renderer.md) turns into pixels. That description must batch well on a GPU, look the same on every platform, and compare byte for byte between runs so that tests can pin it.

Clay's command array clips with a scissor stack, so a renderer flushes at every clip change, and Dear ImGui issues a draw call per clip rectangle. WebRender gives every item a clip chain evaluated in its shaders, which keeps batches whole. Maul UI's list follows WebRender.

## Contents

- [The list](#the-list)
- [Commands](#commands)
- [Paint order and layers](#paint-order-and-layers)
- [Clips and transforms](#clips-and-transforms)
- [Colors](#colors)
- [Snapping](#snapping)
- [Host content](#host-content)
- [Retained emission](#retained-emission)

## The list

`muiNode_BuildDrawList` paints a root's subtree, as its last layout left it, into the context's list, which `muiContext_GetDrawList` shows until the next build ([draw.c](../../src/draw.c)). The list has:

- a header: the root's logical size, the scale, the host's surface key, and a generation that counts builds;
- a clip table, a gradient table and a glyph table;
- a transform table, whose entry 0 is the identity;
- the commands.

The context reserves the tables by its limits. A list that does not fit fails whole and leaves the list empty, so a renderer never draws half a frame.

## Commands

Commands are fixed-size records: a kind, a clip index, a transform index, and a union per kind.

- A box has its border box, four radii, a fill, an optional gradient by index, and four border widths and colors.
- A shadow has its box, radii, color, offset, blur, spread and whether it is inset.
- An image has the host's key, a uv rectangle, nine-slice insets in image pixels, a tint and how it fills its middle.
- A glyph run has a font key, a size, a color, an origin on the baseline and a span of the glyph table, whose entries are a glyph id and a position from the origin, as WebRender and Vello carry them.

Records are zeroed before their fields are written, so the padding of an image, a glyph run and a command, and a gradient's unused stops, are zero and identical trees give identical bytes. `test_draw` builds one scene in two contexts whose earlier lists held boxes, gradients, clips and shadows of two colors, and a paint function's run struct of two byte patterns, and requires the same bytes in every table. Fixed records waste the bytes a small command does not use, and in exchange need no parsing. Glyph images are not in the list, so a list does not depend on a renderer's atlas ([Glyphs](glyphs.md)).

Corners and sides are physical in the list, top left first and top first, resolved from start and end by the node's direction. An image does not mirror under right to left unless its node says it does (`imageMirrors`, as Android's `autoMirrored` and Flutter's `matchTextDirection`). A mirrored image is drawn flipped, its uv rectangle of negative width, and its slice insets are given as drawn. A renderer runs slices the way the uv runs.

An image fills its middle as CSS's `border-image-repeat` says, across and up: stretched (the default), repeated with its tiles centred, rounded to a whole number of tiles stretched to fit, or spaced with equal gaps. A tile is the middle's texels at one logical unit each, times the slices' shared fit factor. An image without slices is all middle, so whole images follow the same rule.

## Paint order and layers

Paint order is depth first. Per node it is its outer shadow, its box, its inner shadow (inside the padding box), its image and its host content, then its children. Nodes and subtrees that would draw nothing at opacity 0 are skipped, as are commands with nothing visible.

Activation layers, modal layers and the overlay band ([Interaction](interaction.md#layers)) paint above the content they are not in. A build paints a root's subtree without the layers below it, then each layer from the bottom: at its laid-out place, but outside its ancestors' clips and opacity, as the web's top layer is. A layer takes its ancestors' transforms, as it takes their scrolling.

Opacity multiplies down the subtree into each command's colors. Without a group layer, overlapping descendants of a faded node show through each other. Per-command opacity is cheaper than a layer and wrong only where a faded subtree overlaps itself.

## Clips and transforms

Clips are a chain. A node that clips adds a clip of its rounded border box (its rounded padding box for a scroll container) whose parent is the clip it is painted in, and its children's commands carry that clip's index. A renderer evaluates the chain per command, so a clip change need not break a batch. `test_rhi_pack` holds the reference renderer to this with a list that changes clip, transform and gradient at every command.

Transforms are scroll offsets and local scales, each owned by a node and composed after its parent entry ([draw_transform.c](../../src/draw_transform.c)).

- A node's local scale (`muiLocalScale`: x and y of 0 or more about an origin given as fractions of its border box) is the pipeline's step after layout. Its own commands, clips and subtree go through it, while layout and what places by layout (scroll extents, scrolling into view, popups, directional navigation, virtual windows) keep its laid-out box. Hit testing, `muiNode_MapToRoot`, pointer records and accessibility bounds go through it. A scale of 1 adds no entry.
- A scroll container's offset goes through the scale above it ([Scrolling and lists](scrolling.md)).
- Scales are never negative, since mirroring is direction's job, and there is no rotation, so every transform only scales and moves.

## Colors

The list's colors are linear light with premultiplied alpha. Gradients carry their stops the same way and an interpolation tag, Oklab, as transitions move colors ([Style](style.md#visual-values-and-colors)). Linear, radial and conic gradients follow CSS's three kinds.

## Snapping

Snapping is per kind, at the identity transform ([paint.c](../../src/paint.c)):

- Box and image edges and clip rectangles go to the nearest device pixel by edge, so adjacent boxes share an edge, and a non-zero size keeps at least one device pixel.
- Border widths go to whole device pixels, at least one.
- A glyph run's baseline goes to a device pixel and its x keeps its fraction, as Skia and Chromium position horizontal text.
- Shadows and radii keep their exact values.

## Host content

A node whose content is the host's is painted by the paint function the draw input names, as layout measures through a measure function ([paint_host.c](../../src/paint_host.c)). For each visible such node it is given the node's id, host key and content box size, and adds glyph runs and filled rectangles through a sink at positions relative to the content box. The build converts their colors and multiplies opacity as for every command. A rectangle is a box with one fill, snapped as boxes, and a side that was not empty keeps a device pixel, for underlines and the like.

The sink also tells the part of the content box that can be seen, within the surface and the clips, through scales and scroll offsets, so long content paints only what is seen. A node whose function asks for that is painted again by every build, scrolling alone included, never copied from the last list with what it saw then. The context refuses edits made from the paint function; reads, such as a node's computed text style, are allowed. A paint function that could not paint all it had to calls `muiDrawSink_Fail`. The node's paint state is flagged (each paint of the node clears the flag first, so a build too full to keep leaves none behind), and once the build has swept its paint requests, each flagged node is requested again, so the next build paints it rather than copying it or keeping the list whole. The build answers `mui_errorCapacity` with the rest of the list in place.

## Retained emission

The context keeps two of each table, the list shown and the one the next build writes ([draw_store.h](../../src/draw_store.h)).

- A build with no paint requested below the root, of the same root, surface and scale as the last, keeps the list and its generation. A host can skip presenting a frame whose generation did not change.
- Otherwise each node's spans of the tables are recorded. A subtree no paint request reaches, at the origin and opacity it was painted at, copies its spans from the last list, renumbering its clips, gradients, transforms and glyphs and its descendants' spans.
- Layout requests paint on every node whose rectangle or direction is not what was last painted, so a copy is never stale. `test_draw` checks over hundreds of random edits that a list built from the last equals one built whole.
- A build whose only change is scroll offsets rewrites the transform table and keeps the commands.

Memory is the two lists the limits reserve, so nothing is evicted. A frame with one change still copies the rest of the list, which takes time in proportion to it: about a third of a full build for the benchmark's 40,001 nodes.
