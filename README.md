# Maul UI

An engine-agnostic retained UI core: a flexbox layout solver with
right-to-left mirroring, typed style classes with state variants,
conditions and transitions, a text service with shaping, bidirectional
text, distance-field glyphs and editing with IME, hit testing, focus
and directional navigation, virtualization, an accessibility tree with
platform adapters, and a renderer-agnostic draw-command list. Written
in C23 with public headers any C17 or C++17 program can include, with
an MIT license.

It works over a retained tree its host edits through ids: the host
writes sizes, flex fields, classes and content, and reads back
rectangles and the command list. A screen that does not change costs
no work per frame. It owns no windows, input capture, GPU or document
format: input arrives as the library's own event records, and any
renderer draws the list. Output is bit-identical on every platform. It
starts no threads and calls application code only to measure content
the host itself owns, inside the call that runs the frame.

## Status

Not released. The first decisions are made and the skeleton builds;
the node tree and the layout solver with its fixture corpus come
first, then style, the command list and the text service.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/mui.md`.

## License

MIT; see `LICENSE`.
