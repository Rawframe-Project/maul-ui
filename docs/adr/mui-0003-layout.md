# mui-0003. Layout

Status: Accepted

## Context

The host writes sizes, flex fields and edges on its nodes and reads back
rectangles. The solver has to give the same rectangles on every
platform, agree with a reference anyone can check, and do no work for a
tree that did not change.

## Decision

- **CSS Flexbox, with CSS's defaults.** The solver implements CSS
  Flexible Box Layout Level 1 (the section 9 algorithm and the section
  4.5 automatic minimum size) and `gap`, for the properties Maul UI
  has. Defaults are CSS's initial values: `row`, grow 0, shrink 1,
  automatic basis and sizes, `align-items: stretch`, `justify-content:
  flex-start`. Every node is a flex container and every box is a
  border box. A difference from Chrome is a defect or is written here.
- **Scale+Offset.** A size, minimum, maximum or basis is automatic, or
  `scale x parent content extent + offset`, which is CSS's
  `calc(scale * 100% + offset)`. Against an indefinite extent a value
  with a scale is automatic; a value without one is a plain length.
  Padding, border, margin and gap are lengths.
- **Roots.** A root lays out in the space the host gives. An automatic
  width fits its content within that space (fit-content); an automatic
  height is its content's height. A scale is a fraction of the space.
- **Lines.** A container that wraps collects children into lines by
  their outer hypothetical sizes and the gap, resolves flexible lengths
  per line, and places the lines by `align-content` (stretch by
  default, as CSS's `normal`); a container that does not wrap has one
  line as large as its inner cross size. At its narrowest, a wrapping
  container is as wide as its widest child.
- **Absolute nodes.** A node placed absolutely takes no part in its
  parent's flex layout or size. Its Scale+Offset insets and sizes
  resolve against the parent's padding box (CSS's containing block).
  Both insets of an axis stretch an automatic size between them;
  otherwise the width is fit-content and the height the content's; with
  no inset the node sits where it would as the parent's only flex item.
  Automatic margins center it between two insets. Its anchor point then
  moves it back by that fraction of its own size, as Roblox's
  `AnchorPoint` and CSS's `translate` do; in-flow nodes ignore it.
- **Automatic margins** on flex items take positive free space before
  `justify-content`, and on the cross axis center or push an item, which
  is then not stretched; on overflow the start margin is zero.
- **Right to left.** A node's text direction is inherit (the default),
  left to right or right to left; a root that inherits is left to
  right. Every edge, inset and the anchor's x are logical, so the solver
  lays each container out with start on the left and mirrors its
  children's horizontal positions when it is right to left. The
  inherited direction travels in a node's sizing input; sizes do not
  depend on it, and a node's last full layout records the direction it
  was done in, so a change on an ancestor re-lays out exactly the
  inheriting nodes.
- **Distributed justification on overflow.** `space-around` and
  `space-evenly` pack at the start when the children overflow, as CSS
  Box Alignment's safe fallback does; `space-between` always does.
- **Unrounded results.** Rectangles are binary32 logical units, relative
  to the parent's border box. Snapping happens where the draw-command
  list says, per command kind.
- **Measurement.** Host content is measured by a function passed to
  `muiComputeLayout`, called only inside it and only for nodes marked as
  host content. The context refuses edits while it runs. Results are
  cached per node until the node, a descendant, or the node's content
  changes; a cached size also answers a constraint it provably answers
  the same (an exact size equal to an unshrunk result, a max-content
  result that fits, a smaller space the result fits). A query with both
  sizes exact needs no computation, and an item's automatic minimum is
  computed only when its line shrinks.
- **No work for unchanged subtrees.** Before a run, the solver forgets
  what it cached for the nodes on a path to a change, and only those. A
  node whose subtree is unchanged and whose size is the same keeps its
  children's rectangles without visiting them.
- **Fixtures.** `test/layout/*.txt` holds fixtures written for Maul UI
  with Chrome's rectangles. `tools/gen_layout_fixtures.py` turns them
  into the tables `test_layout_fixtures` runs, and with `--oracle`
  renders each in Chrome (through `tools/layout_oracle.mjs`) and writes
  the rectangles back. CI checks that the tables match the corpus but
  does not run Chrome. Comparisons allow 1/32 unit, twice Chrome's 1/64
  px step.

## Consequences

Anyone can check a layout against a browser, and the corpus grows with
every feature. The solver carries CSS's cost where CSS is subtle (the
automatic minimum needs a min-content pass per item) and gains no
shortcuts that would later need a compatibility flag.
