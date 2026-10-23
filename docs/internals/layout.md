# Layout

The host writes sizes, flex fields and edges, and reads back rectangles. The solver has to give the same rectangles on every platform, agree with a reference anyone can check, and do no work for a tree that did not change. The reference is the browser: the solver implements CSS Flexible Box Layout Level 1 for the properties Maul UI has, with CSS's initial values as defaults. A difference from Chrome is a defect unless this page names it.

## Contents

- [The model](#the-model)
- [Sizes and lines](#sizes-and-lines)
- [Aspect ratio](#aspect-ratio)
- [Absolute nodes](#absolute-nodes)
- [Baselines](#baselines)
- [Right to left and the safe area](#right-to-left-and-the-safe-area)
- [Measurement and caching](#measurement-and-caching)
- [Conformance](#conformance)

## The model

Every node is a flex container and every box is a border box. Defaults are CSS's: a row, grow 0, shrink 1, an automatic basis and sizes, `align-items: stretch`, `justify-content: flex-start`.

A size, minimum, maximum or basis is automatic, or `scale x parent content extent + offset`, which is CSS's `calc(scale * 100% + offset)` (Scale+Offset, as Roblox's `UDim`). Against an indefinite extent a value with a scale is automatic, and a value without one is a plain length. Padding, border, margin and gap are lengths.

A root lays out in the space the host gives. An automatic width fits its content within that space, as fit-content does, and an automatic height is its content's height.

Results are unrounded binary32 logical units relative to the parent's border box. Snapping to device pixels happens in the draw list, per command kind ([Draw list](draw-list.md#snapping)), so layout stays independent of the device scale.

The solver is split by concern: [flex.c](../../src/flex.c) runs CSS Flexbox's section 9 for a container's in-flow children, [flex_item.c](../../src/flex_item.c) sizes one item, [flex_resolve.c](../../src/flex_resolve.c) resolves flexible lengths, [absolute.c](../../src/absolute.c) places absolute nodes, and [solve.c](../../src/solve.c) answers sizing queries from a node's cache or computes them. Algorithms reach their children only through `solve`, so the modules that size a node and the ones that call back into it do not include each other.

## Sizes and lines

- The automatic minimum of a flex item is CSS Flexbox's section 4.5 content-based minimum. It is computed only when the item's line shrinks, because it costs a min-content pass per item.
- A wrapping container collects children into lines by their outer hypothetical sizes and the gap, resolves flexible lengths per line, and places the lines by `align-content` (stretch by default, as CSS's `normal`). A container that does not wrap has one line as large as its inner cross size.
- At its narrowest, a wrapping container is as wide as its widest child. A wrapping column whose height is its content's breaks its lines only at its maximum height. A wrapping column of open width is as wide as its lines' contributions, each item's width with its height left open.
- Automatic margins on flex items take positive free space before `justify-content`. On the cross axis they center or push an item, which is then not stretched. On overflow the start margin is zero.
- `space-around` and `space-evenly` pack at the start when the children overflow, as CSS Box Alignment's safe fallback does. `space-between` always does.
- A scroll container's automatic minimum is 0, as CSS's, and its extent is measured as the furthest end of its children ([Scrolling and lists](scrolling.md)).

## Aspect ratio

A node's ratio (width over height of its border box) turns a definite size on one axis into the other, within that axis's limits.

- A flex item with a definite cross size takes its flex base size from it (CSS Flexbox 9.2.3 B).
- An item whose main size flexes takes its cross size from the flexed size.
- On the axis the ratio gives, an automatic minimum is the content's min-content size, so the ratio never crushes content (CSS Sizing 4).
- A size that came through the ratio does not count as a specified size for a flex item's automatic minimum.

Ratios meet stretching, percentages and limits in ways the specification leaves to engines. The corpus records each case where Chrome and Firefox agree, and the solver follows them. Where the two engines part, the solver may follow either, and the fixture says which.

## Absolute nodes

An absolute node takes no part in its parent's flex layout or size. Its Scale+Offset insets and sizes resolve against the parent's padding box, CSS's containing block.

- Both insets of an axis stretch an automatic size between them.
- Otherwise the width is fit-content and the height is the content's.
- With no inset the node sits where it would as the parent's only flex item.
- Automatic margins center it between two insets.

Its anchor point then moves it back by that fraction of its own size, as Roblox's `AnchorPoint` and a CSS `translate` do. In-flow nodes ignore the anchor.

## Baselines

Children aligned by `baseline` in a row share their line's baseline, flush as a group with the line's cross start (its bottom under `wrap-reverse`), and the line holds their largest ascent and descent. In a column the cross axis is the inline axis, so `baseline` is `start`, as it is for an absolute node.

A node's first baseline is its host content's, from the baseline function the layout input names. A container's comes from its first line: from that line's baseline-aligned children if it has any, else from its child first in the container's direction (the last when reversed), as Chrome reads CSS Flexbox section 8.5. A node without one has the bottom of its border box, as CSS synthesizes it. Baselines are found where a line needs them and not cached.

## Right to left and the safe area

A node's direction is inherit (the default), left to right or right to left. A root that inherits is left to right. Every edge, inset and the anchor's x are logical. The solver lays each container out with start on the left and mirrors its children's horizontal positions when it is right to left.

The inherited direction travels in a node's sizing input, and a node's last full layout records the direction it was done in. A change on an ancestor therefore lays out again exactly the inheriting nodes. Sizes depend on direction only through the safe area, and the size cache keys on it.

The host gives the surface's safe-area insets with the space (`muiLayoutInput`), physical as platforms report them. A node names the logical edges (`safeArea`) whose padding is at least the inset on the physical side each falls on, as CSS's `max(env(safe-area-inset-left), ...)` and Flutter's `SafeArea` pad: where content meets the surface's edge, a bar's background reaches the edge while its content does not. Nothing is consumed, so a node below one that asks pads again. The solver keeps the padding it computes beside the authored one, which stays the style's. Content boxes, scroll extents, virtual lists and ranges read the computed padding.

## Measurement and caching

Host content is measured by the measure function the layout input names, called only inside `muiComputeLayout` and only for nodes marked as host content. Results are cached per node until the node, a descendant or its content changes ([layout_node.h](../../src/layout_node.h)). A cached size also answers a constraint it provably answers the same way, by the rules Yoga's measure cache uses:

- an exact size equal to an unshrunk result;
- a max-content result that fits;
- a smaller space the result still fits.

A query with both sizes exact needs no computation, and host content is never measured for one. This includes the final placement: a host lays its content out at the node's rectangle.

A run is bounded by the change ([layout_bound.c](../../src/layout_bound.c)). A parent reads of a child only its answers to the sizing queries it asked, each kept in the child's cache, and its baseline where one is aligned by. A node whose layout was requested is solved again under the queries its cache held. When every answer holds and no baseline is read through it, it is laid out alone at its last size and its parent is left as it is. When one does not, its parent is cleared and checked the same way, up to the root, which the whole solve then takes. A node whose subtree is unchanged and whose size is the same keeps its children's rectangles without visiting them.

## Conformance

`test/layout/*.txt` holds the fixture corpus: layouts written for Maul UI with Chrome's rectangles, about 1,300 of them. [gen_layout_fixtures.py](../../tools/gen_layout_fixtures.py) turns the corpus into the tables `test_layout_fixtures` runs. With `--oracle` it renders each fixture in headless Chrome through [layout_oracle.mjs](../../tools/layout_oracle.mjs) and writes the rectangles back. Comparisons allow 1/32 unit, twice Chrome's 1/64 pixel step. CI checks that the tables match the corpus and does not run Chrome.

[random_layouts.py](../../tools/random_layouts.py) draws random layouts in the corpus format to compare with Chrome, and reduces one that disagrees to a smallest case, optionally against Firefox too, so a reduction does not slide into a case where the engines themselves disagree. [Development](../operations/development.md#layout-fixtures) gives the procedure.

The cost of following the browser is that the solver carries CSS's subtleties, such as the min-content pass for automatic minimums, and takes no shortcut that would later need a compatibility flag.
