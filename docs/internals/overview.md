# Overview

Maul UI is a retained UI core. The host keeps its interface as a tree of nodes in a context. It writes sizes, flex fields, classes and content, and reads back rectangles, a draw-command list and an accessibility update. The library owns no window, no GPU and no document format. This page states the library's profile within the Maul family and the shape every other page assumes.

## Profile

- **Determinism.** The same tree and the same inputs give the same bits on every platform, compiler and architecture. This covers computed layout, resolved style, the bytes of the draw list, the accessibility tree, shaped runs, rasterized glyphs and distance fields. The rules behind it:
  - Geometry is binary32 arithmetic with no contraction.
  - Only the exactly rounded functions in [libm-allowed.txt](../../tools/libm-allowed.txt) come from the platform. The exponentials, powers, roots and trigonometry of springs, easing and color conversion are the library's own ([motion_math.c](../../src/motion_math.c)).
  - Time enters only as the timestamps the host passes in.
  - No result depends on pointer values, hash iteration or allocation order. Children are visited in the order the host gave them.
  - FreeType and HarfBuzz are pinned releases whose results are integers ([Text](text.md#dependencies)).
- **Threads.** The library starts no thread. A context or a text service is used by one thread at a time. Separate contexts on separate threads give the same results as on one, which `test_threads` checks under ThreadSanitizer. The accessibility adapters answer their platform on the thread the platform calls on, from their own copy of the tree ([Accessibility](accessibility.md)).
- **Memory.** The context is the owner object. It takes the caller's allocator and reserves everything its limits name at creation ([context.c](../../src/context.c)):
  - Memory is allocated when the host creates nodes, styles or fonts, sets an accessibility text, or enables accessibility for a first root.
  - Running a frame (style, layout, emission) and handling input allocate nothing. A limit reached refuses with `mui_errorCapacity` rather than growing.
  - The text service is a second owner object. It grows its scratch buffers to the largest text it has handled, then allocates nothing more for frames of it (`test_frame_memory`).
  - HarfBuzz allocates from the C library ([Text](text.md#memory)).
- **Untrusted input.** Every argument is validated and refused with a status in every build. An argument a correct host never passes counts one misuse against the live context or service (`muiGetContextMisuse`), so a host can watch for its own bugs in release builds. Bad data that another program controls is refused with `mui_errorFormat` and counts none, so that program cannot move the counter: a font file too short or malformed, and a screen reader's request whose number is not finite or names offsets of a text that changed since its adapter's last update. Every `mui_errorInvalid` is therefore a host's bug. The adapters drop a client's numbers that are not finite or lie past a range's ends before they reach the host.
- **Stale ids.** Ids outlive their objects by design: a host keeps node ids in its own structures, and records and notifications name nodes the host may destroy before it reads them. A stale id therefore counts no misuse; the call returns `mui_errorStale`, and a getter returns the zeroed value. Fonts, the PNG images of colour bitmap fonts with their zlib streams, and the AT-SPI calls of other programs are hostile bytes. Each has a fuzz target.
- **Assertions.** `MUI_ASSERT` ([invariant.h](../../src/invariant.h)) checks internal invariants only. It traps in builds without `NDEBUG` (Debug, the sanitizers, fuzzing) and compiles out of Release, whose per-node loops must not pay for it.
- **Dependencies.**
  - The core needs the C library and libm.
  - The text component, on by default, builds FreeType, HarfBuzz and Maul Unicode into the library.
  - Each accessibility adapter uses its platform's accessibility API, and the Linux one opens libdbus-1 at run time.
  - The reference renderer needs Maul RHI, and the window glue needs Maul Window. Each is a library of its own beside the core (`rhi/`, `window/`), which the core and the text component never include.
- **No engine concept in the API.** No public identifier names an engine's concepts (world, entity, asset, game, scripting and the like). [check_litmus.py](../../tools/check_litmus.py) enforces this, so any engine or plain application can drive Maul UI over its own tree.
- **Size.** In wasm at `-Oz` with link-time optimization, a program that takes every public function links at most 160,000 bytes of the core and 640,000 more for the text component with FreeType and HarfBuzz. [size_report.py](../../tools/size_report.py) measures both, and a release checks them ([Development](../operations/development.md#benchmarks-and-size)).

## The frame

A frame is a handful of calls, each over one root:

1. `muiComputeLayout` runs the style pass over the nodes whose style was requested ([Style](style.md)), advances running transitions and scroll motion, and solves layout for what changed ([Layout](layout.md)). Host content is measured through the measure function the layout input names. Popups are placed and virtual lists report their windows here.
2. `muiBuildDrawList` paints the root into the context's draw list, copying what did not change from the last list ([Draw list](draw-list.md)).
3. Input arrives between frames as event records the host passes in. Hit testing reads the last layout, and routing reaches one host function ([Interaction](interaction.md)).
4. With a root enabled for accessibility, `muiBuildAccessUpdate` gives the nodes that changed since the last update ([Accessibility](accessibility.md)).

Each stage marks what the next must redo, and a pass descends only into marked subtrees ([Node tree](node-tree.md#dirty-tracking)). A frame where nothing changed visits no node, and `muiGetWorkCounts` lets a test hold a frame to its cost.

No stage feeds an earlier one within a frame. A condition that reads a node's size sees the last layout, and the frame after shows its effect ([Style](style.md#conditions)). `muiIsUpdatePending` tells the host a frame is owed: by such a condition, a running transition, an easing scroll or a fling.

## Host code the library calls

The library calls into the host only inside the call that needs it:

- the allocator;
- the measure, baseline and paint functions of a frame;
- the event function during routing;
- the accessibility text function while an update is built.

A function the context is running may read the context but not change it. A change is refused with `mui_errorState` and counted as misuse. The event function is the exception: it may edit the tree, focus and capture, but not feed input ([Interaction](interaction.md#routing)).

## What lives where

- [Node tree](node-tree.md): the context block, ids, links and dirty tracking.
- [Layout](layout.md): the flex solver, conformance with browsers, caching.
- [Style](style.md): layers, conditions, transitions, tokens and themes.
- [Draw list](draw-list.md): commands, clips, transforms, snapping and retained emission.
- [Interaction](interaction.md): hit testing, pointers, focus, routing, drags, ranges, popups and exits. [Scrolling and lists](scrolling.md): scroll containers, motion and virtual lists.
- [Text](text.md), [Glyphs](glyphs.md) and [Text editing](text-editing.md): the text component.
- [Accessibility](accessibility.md): the update, the consumer tree and the six adapters.
- [Reference renderer](renderer.md), [Window glue](window-glue.md) and [The web](web.md): the parts beside the core.
- [Glossary](glossary.md), and [Maul conventions](maul-conventions.md), the family's rules.
