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

[The guide](docs/guide.md) walks through each part, and
[the API reference](docs/api.md) lists all 340 public functions,
generated from the headers.

## Status

0.1.0 is the current release, the first. Every part above is in
place: the node tree, flex layout checked against Chrome, style, the
text service with font fallback and editing, interaction,
virtualization and popups, the accessibility tree with its six
platform adapters, the reference renderer on Maul RHI and the glue to
Maul Window. Its known issues are in the changelog.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

The text component (`MAUL_UI_TEXT`, on by default) also needs a C++
compiler for HarfBuzz, and fetches FreeType, HarfBuzz and Maul Unicode
at configure time (`THIRD_PARTY.md`); to build offline, point
`FETCHCONTENT_SOURCE_DIR_MAUL_UI_FREETYPE`,
`FETCHCONTENT_SOURCE_DIR_MAUL_UI_HARFBUZZ` and
`FETCHCONTENT_SOURCE_DIR_MAUL-UNICODE` at local copies of the same
releases. The installed library carries all three inside it.
`-DMAUL_UI_TEXT=OFF` builds the core alone, which needs none of
them.

`cmake --install build --prefix <prefix>` installs the headers, the
library, a CMake package (`find_package(maul-ui)`, target
`maul-ui::maul-ui`) and a pkg-config file; `samples/minimal` is a
program built against them alone.

## Layout fixtures

The layout tests run the corpus in `test/layout/`, whose expected
rectangles come from Chrome. After adding or changing a fixture,
`python3 tools/gen_layout_fixtures.py --oracle` renders the corpus in
headless Chrome and writes the rectangles back; it needs Node and
puppeteer (`MUI_NODE_MODULES` names the `node_modules` that holds it).
Without `--oracle` the script only regenerates the C tables. Neither
Chrome nor puppeteer is needed to build or test the library.
`tools/random_layouts.py` draws random layouts to compare with Chrome
the same way and reduces one that disagrees to a smallest case.

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/mui.md`, and the published sources its algorithms come from
in `docs/references.md`. A release follows the conventions' checklist
(section 15) and Maul UI's own steps in `docs/releasing.md`.

## License

MIT; see `LICENSE`.
