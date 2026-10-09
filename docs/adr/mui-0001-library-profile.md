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
  - the text service's two dependencies (record mui-0006) are pinned
    releases whose results are integers.
- **Threads:** none of its own (family record 0017). A context is used
  by one thread at a time; the host may run separate contexts on
  separate threads, with the same results as on one (`test_threads`,
  under ThreadSanitizer in CI).
- **Memory:** the owner object is the context, created with the
  caller's allocator, and the text service is an owner object of its
  own; HarfBuzz, inside it, allocates from the C library (record
  mui-0006). Memory is allocated when the host creates
  nodes, styles or fonts, explicitly raises a named limit, sets an
  accessibility text, or enables accessibility for a first root
  (record mui-0008). Running a
  frame (style, layout, emission) and handling input do not allocate:
  per-node results live with the node, and retained command lists are
  bounded by a limit set at creation. The text service grows its scratch
  buffers to the largest text it has laid out, shaped and painted, and
  then allocates nothing more for frames of it (`test_frame_memory`).
- **Platform dependencies:** the C library only, with `sqrt` from
  libm, for the core. The text component, on by default, builds
  FreeType 2.14.3, HarfBuzz 14.5.1 and Maul Unicode 0.3.0 into the
  library (record mui-0006). Accessibility adapters use their platform's
  accessibility API, each in its own optional target.
- **No engine concept in the API:** no identifier of the public
  headers has a word naming an engine's or Rawframe's concepts (world,
  entity, schema, asset, game, scripting and the like), checked by
  `tools/check_litmus.py` in CI: another engine or a plain application
  drives Maul UI over its own tree and draws its list with its own
  renderer.
- **Size budget (family record 0013):** in wasm at `-Oz` with
  link-time optimization, the most a program can link of each part,
  every public function taken: the core at most 160,000 bytes above an
  empty program, and the text component, Maul UI's text code with
  FreeType and HarfBuzz, at most 640,000 above the core. Clay's layout
  and command list measure 72,545 bytes, and FreeType's and HarfBuzz's
  builds 304,248 and 224,100. CI reports both (`tools/size_report.py`);
  a release checks them, and they are only tightened.
- **Commit areas:** `a11y`, `api`, `bench`, `build`, `ci`, `docs`,
  `draw`, `input`, `layout`, `rhi`, `samples`, `style`, `tests`,
  `text`, `tools`, `tree`, `virtual`, `window`. The reference renderer
  and the window glue are parts with sources of their own (family
  record 0020), `rhi` and `window`; fuzz targets and tests found by
  mutants are `tests`, motion and easing `style`, pointers and
  navigation `input`.

## Consequences

The same tree and the same inputs give the same list on every machine,
which makes golden lists and layout fixtures portable tests. The
library pays with its own transcendental functions and with explicit
limits a host sets up front.
