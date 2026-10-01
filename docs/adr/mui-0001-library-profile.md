# mui-0001. Library profile

Status: Accepted

## Context

Every Maul library states in one record what its domain adds to the
family rulebook (family record 0005). Maul UI computes layout, resolves
style, routes input and emits a draw-command list over a retained tree
its host edits. Hosts compare its output across platforms (golden
lists, layout fixtures) and rely on a static screen costing nothing.

## Decision

- **Determinism:** bit-exact on every supported platform, compiler and
  architecture. It covers computed layout (rectangles, content sizes,
  clips), resolved style values, the bytes of the draw-command list,
  the accessibility tree, shaped runs, rasterized glyphs and generated
  distance fields. The rules that follow from it:
  - geometry is binary32 arithmetic with no contraction (family rule
    11); only the operations IEEE 754 rounds exactly and `sqrt` come
    from the platform, and anything else (the exponentials and
    trigonometry of springs and easing) is the library's own;
  - time enters only as the timestamps the host passes in, so the same
    inputs at the same timestamps give the same frames;
  - no output depends on hash-table iteration, pointer values or
    allocation order; children are visited in the order the host gave
    them;
  - the text service's two dependencies (record 0002 of this library,
    when it lands) are pinned releases whose results are integers.
- **Threads:** none of its own (family record 0017). A context is used
  by one thread at a time; the host may run separate contexts on
  separate threads.
- **Memory:** the owner object is the context, created with the
  caller's allocator, with the text service's font and glyph stores as
  owner objects of their own. Memory is allocated when the host creates
  nodes, styles or fonts, or explicitly raises a named limit. Running a
  frame (style, layout, emission) and handling input do not allocate:
  per-node results live with the node, and retained command lists are
  bounded by a limit set at creation.
- **Platform dependencies:** the C library only, with `sqrt` from
  libm, for the core. Accessibility adapters use their platform's
  accessibility API, each in its own optional target.
- **Commit areas:** `a11y`, `api`, `bench`, `build`, `ci`, `docs`,
  `draw`, `input`, `layout`, `samples`, `style`, `tests`, `text`,
  `tools`, `tree`, `virtual`.

## Consequences

The same tree and the same inputs give the same list on every machine,
which makes golden lists and layout fixtures portable tests. The
library pays with its own transcendental functions and with explicit
limits a host sets up front.
