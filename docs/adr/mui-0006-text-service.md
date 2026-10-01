# mui-0006. The text service

Status: Accepted

## Context

Text needs fonts, shaping, bidirectional text, line breaking, glyph
images and editing. The core must stay free of all of it (record
mui-0001), so that a host with its own text stack can use the core
alone, while a host that wants text gets one that gives the same
results on every machine. Shaping and font loading are large, mature
domains: FreeType and HarfBuzz are what nearly every text stack uses.

## Decision

- **A component of the library:** the text modules are built into
  `maul-ui` when `MAUL_UI_TEXT` is on, the default. With it off the
  library is the core alone and depends on the C library only. No core
  module includes a text module.
- **The text service** is an owner object of its own, made from a def
  with an allocator and limits (`muiCreateTextService`), separate from
  any context and used by one thread at a time. Fonts are ids it gives
  out (`muiCreateFont`); a font's bytes are copied, or borrowed from a
  caller who keeps them unchanged until the font is destroyed.
- **The boundary:** the core reaches text through host content only:
  the measure function, a paint function, and later a baseline
  function, which the text service provides with a host key naming a
  text block. The service reads a node's resolved text style through
  public getters.
- **Style:** text properties (color, font, size, weight, slant, line
  height, letter spacing, alignment, wrapping) belong to the core's
  style and are inherited, so classes, states, tokens, themes and
  transitions apply to them.
- **The list:** glyph runs carry a font key, a size, a color, an origin
  and glyph ids with positions. Glyph images are not in the list, so a
  list does not depend on atlas state.
- **Measuring** uses the font's own advances, shaped at a scale of its
  units per em and scaled by size over units per em, so sizes are the
  same at every device scale; hinting affects glyph images only.
- **Metrics** (`muiFont_GetMetrics`) are in ems. The ascent, descent
  and line gap are the typographic ones when the font sets
  `USE_TYPO_METRICS`, else the horizontal header's, else the
  typographic or Windows ones, as HarfBuzz chooses.
- **Dependencies,** built into the library:
  - FreeType 2.14.3 (FreeType License) and HarfBuzz 14.5.1 (MIT),
    fetched as their release archives, checked by SHA-256, and compiled
    by this build in reduced configurations: FreeType's TrueType, CFF
    and PostScript-hinter modules with the smooth rasterizer, without
    zlib, bzip2, PNG or Brotli, so compressed web fonts are refused,
    and without environment properties; HarfBuzz lean, with variable
    fonts, without its Unicode tables, exceptions or RTTI.
  - Maul Unicode 0.2.0, fetched at its tag, which supplies HarfBuzz's
    Unicode functions, so one Unicode version answers every question.
  - `MAUL_UI_TEXT_SYSTEM_LIBRARIES` links an installed FreeType and
    HarfBuzz instead, for programs that already carry them; results
    then depend on their versions.
  - A shared library exports Maul UI's functions only.
- **Memory:** the service's memory and FreeType's come from the
  service's allocator. HarfBuzz allocates from the C library: its
  allocator is chosen at build time for the whole process, and objects
  it creates lazily and shares would otherwise land in the first
  service's allocator. This is the exception to family record 0010.
- **Errors:** `mui_errorFormat` for data that fails validation or is in
  a format a call does not take.
- **Hostile input:** fonts are validated by FreeType and HarfBuzz when
  created; tests feed damaged and truncated fonts and every allocation
  failing in turn.

## Consequences

A host gets text that measures and shapes the same everywhere, and
pays for it with two third-party libraries built from source (and a
C++ compiler for HarfBuzz). Building with text needs the three
archives, fetched at configure time or given as local copies with
`FETCHCONTENT_SOURCE_DIR_MAUL_UI_FREETYPE`,
`FETCHCONTENT_SOURCE_DIR_MAUL_UI_HARFBUZZ` and
`FETCHCONTENT_SOURCE_DIR_MAUL-UNICODE`. Their objects are built into
the library, so an installed library links with nothing more than the
C library, libm and, where the C library lacks them, threads. HarfBuzz
needs no C++ runtime, but CMake sees its objects and links programs
that link a static library as C++ unless they set `LINKER_LANGUAGE C`,
as this build does for its own; in Visual Studio projects that also
keeps a C program's C standard.
