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
  the measure function, a paint function, and a baseline function. The
  text service provides all three (`muiMeasureText`, `muiPaintText`,
  `muiTextBaseline`, whose user pointer names the service and the
  context); the first baseline is the first line's, which does not
  depend on the width, and empty text, which has no lines, has none; a node's host key is a text block's key,
  and a text style's font is a font's key, 0 being the service's
  default font. The service reads a node's resolved text style and
  direction through public getters.
- **Text blocks** (`maul-ui/text_block.h`) hold UTF-8 text the service
  copies. When the text is set, its line break opportunities (UAX #14)
  and script runs are found; on first use with a font and direction,
  its paragraphs get bidi levels (UAX #9), are split into items of one
  level and script, and each item is shaped by HarfBuzz with the whole
  text as context, without a language, so the result does not depend
  on the process's locale. That shaping is kept until the text, font or
  direction changes; sizes and widths only scale and break it.
- **Lines** break greedily at opportunities from the shaped advances;
  the text's own line breaks end lines, white space is kept as written,
  spaces ending a wrapped line hang, and a word wider than the line
  overflows. A line that breaks where HarfBuzz marks shaping unsafe to
  break, or inside a cluster such as a ligature, is shaped alone with
  its trailing white space, as Blink reshapes line edges, and its
  glyphs and width come from that; the break itself is chosen from the
  block's shaping. Max-content keeps only the text's line breaks;
  min-content breaks at every opportunity. A text ending in a line
  break has an empty last line. Letter spacing follows each cluster.
  The line height is the style's, or the font's ascent, descent and
  line gap, with the leading split above and below. Painting reorders
  each line by UAX #9 rules L1 and L2, aligns it by the paragraph
  direction, and draws a glyph run per line and item; default
  ignorables, such as bidi controls, draw no glyph.
- **Style:** text properties (color, font, size, weight, slant, line
  height, letter spacing, alignment, wrapping, decoration and its
  color) belong to the core's style and are inherited, so classes,
  states, tokens, themes and transitions apply to them. Decorations are
  lines under, over and through the text (a mask), solid, in their
  color or, when its alpha is 0, the text's own, as CSS's currentColor.
- **Spans** (`muiTextSpan`, `muiTextBlock_SetSpans`) style parts of a
  block apart from its node's style: a byte range on character edges and
  the text properties a mask names, later spans winning where they
  overlap, as a stack of styles flattens; they are what a host's
  rich-text markup turns into, and Maul UI parses none. Spans set what
  painting reads (color, decoration, decoration color); those that
  change shaping and line metrics (font, size, weight, slant, baseline
  shift) come next. Setting the text drops them; a replacement moves
  those after it and trims those it cuts, a span growing with text put
  strictly inside it, and `muiTextBlock_GetSpans` reads them back.
  Painting splits a line's glyph runs where the ink (color and
  decorations) changes, in visual order, and draws underlines and
  overlines before the glyphs and line-through after, as CSS paints
  them, from the first font's metrics (CSS's usual ones without them).
- **The list:** glyph runs carry a font key, a size, a color, an origin
  and glyph ids with positions. Glyph images are not in the list, so a
  list does not depend on atlas state.
- **Glyph images** (`maul-ui/glyph_image.h`): `muiRenderGlyph` renders
  a glyph of a font key at an em in device pixels and a pen offset
  right of a pixel boundary into the caller's bytes, as 8-bit linear
  coverage, and gives its size and bearing; a size it needs that the
  bytes lack is told, not allocated. Outlines are unhinted, without the
  font's embedded bitmaps, so images sit on the unhinted advances text
  is laid out with; FreeType renders in integers, so images are the
  same bytes everywhere. Color glyphs and LCD rendering are not drawn.
  `muiRenderGlyphField` renders a signed distance field that scales:
  128 at the outline, 128 / spread a pixel inside and out, reaching the
  spread past the outline. Maul UI draws it itself rather than with
  FreeType's `sdf` module, which spends most of its time in fixed-point
  vector lengths (about 30 times slower): the outline is cut into
  segments within 1/32 pixel by Wang's formula, each pixel's center is
  inside by nonzero winding along its row, or even-odd when the outline
  says so, and its distance is to the nearest part of a segment on the
  edge of the union of the contours.
  Overlapping contours, as variable fonts' and composite glyphs' are,
  make one shape: segments are cut into pieces of at most a pixel,
  sorted into the pixel cells their boxes' corners are in, and the
  places where pieces cross or touch others are found among those in
  nearby cells. Between such places a contour is on the edge or within
  the union throughout, which the windings just either side of it tell,
  and a piece others cross is cut there and each part told apart, so
  the edge is exact; a stray point or a contour without area is no
  edge. Only correctly rounded float operations are used, so fields
  too are the same bytes everywhere.
- **Font instances:** a font key names a font and an instance of it:
  the wght axis's value, ital, slnt or a shear, the opsz axis's value,
  and a made bold, decided once when text is laid out from the style's
  weight, slant and size as CSS decides them (wght from the weight; ital
  for italic, slnt -14 for oblique or for italic without ital; opsz from
  the size; bold from 600 where the face cannot reach 600, outlines and
  advances grown by an em/24; an oblique shear of a quarter where the
  face is upright without either axis). Glyph runs carry the key, and
  glyph images and atlases rebuild the instance from it, so instances
  have no lifetime and a regular upright static font keeps its own key.
  Shaping fonts of instances are made from the face as needed, a few
  kept per font; line metrics are the default instance's.
- **Font families** (`muiCreateFontFamily`) are objects of the
  service holding up to 256 faces; a text style names one by its key,
  which the family bit sets apart from fonts'. Layout matches a face as
  CSS Fonts 4 does (the width nearest normal, narrower first; then
  italic, oblique and normal faces in CSS's order for the slant; then
  the weight in CSS's order, ties to the earlier face), a variable face
  matching every value its wght, ital and slnt axes reach, and then
  makes the face's instance. Faces destroyed after are passed over.
- **Fallback:** text is drawn with a chain of fonts: the face of the
  style's font or family, then up to 8 fallbacks the family names, then
  up to 8 the service names (`muiSetFallbackFonts`), each a font or a
  family's face matched to the style, in its instance, once each. Each
  grapheme cluster is drawn in the first font of the chain with all its
  characters (default ignorables and controls passed over); a cluster
  whose first character is of no one script (Common or Inherited) stays
  in the font before it when that font has it; with no font having them
  all, the first with its first character, else the first font. Items
  split where the font changes, each shaped in its own font's units per
  em, and the sums line breaking reads are in ems; glyph runs split with
  the items. Lines take the first font's metrics.
- **Editing primitives** (`maul-ui/text_edit.h`) work on a node's text
  laid out as painting lays it out, at the content width the caller
  gives. A position is a byte offset between grapheme clusters and an
  affinity, downstream or upstream, which picks the place of an offset
  that has two: the end of a wrapped line or the start of the next, and
  either side of a change of direction. Each line is read as boxes of
  grapheme clusters left to right, from the glyphs painting draws, the
  letter spacing after each included and a glyph several clusters share
  cut into equal parts. `muiTextHitTest` finds the line at a point's y
  and the nearer edge of the box at its x; `muiTextGetCaret` the edge
  of a position's box, leading for downstream and trailing for
  upstream; `muiTextGetRangeRects` a rectangle for each stretch of
  boxes in a range, so a range across a change of direction is several.
  `muiTextMove` moves a position: to the next or previous grapheme
  cluster boundary in the text; a cluster left or right on screen, from
  one edge of a box to its other, crossing to the next line or the one
  before at a line's ends as the paragraph's direction has it; to the
  start or end of a word (a UAX #29 word segment with a letter or a
  number); to its line's start or end; up or down a line at an x the
  caller keeps (the text's start or end past the first or last line);
  to the text's start or end. `muiTextBlock_FindDeletion` gives what a
  deletion removes: forward, the next grapheme cluster; back, as Blink
  and Android delete, one code point, but a cluster with an emoji, a
  regional indicator or a keycap whole, a variation selector with the
  code point before it, and CR with its LF. `muiTextBlock_Replace`
  replaces a range of a block's text, analyzing the new text whole
  before the old goes, and `muiTextBlock_GetText` reads it.
- **Input method compositions** live in the block's text, as browsers
  and platform controls keep them, so they shape, wrap and reorder with
  the text around them: `muiTextBlock_SetComposition` replaces the
  composition (or inserts one at an offset) with the method's text and
  its styled segments, an empty text removing it;
  `muiTextBlock_EndComposition` keeps its text as typed; a replacement
  before or after it moves it, one over it or new text ends it.
  Painting underlines it after the glyphs where a decoration's
  underline goes: thin for underlined and converted segments, twice as thick
  for the target, none for plain, the whole composition thin when it has
  no segments (as Wayland's are drawn). The host maps its window
  library's compositions to segments and gives the platform the caret
  rectangle from `muiTextGetCaret` for the candidate window. Selection,
  the x kept for vertical moves, input and undo stay with the caller.
- **Glyph atlases** (`maul-ui/glyph_atlas.h`) are owner objects of a
  service, in its memory: pages of the caller's size, made as needed up
  to a limit and cut into plots, each packed with a skyline bottom-left
  and each image kept inside a one-pixel empty gutter. A glyph is asked
  for with its pen and baseline in device pixels; the pen is taken to
  the nearest quarter pixel, the baseline to the nearest pixel, and the
  key is the font, glyph, size and quarter. When no plot has room, the
  least recently used plot the current frame does not use is emptied:
  its entries go stale at once, as each names its plot's generation.
  The renderer marks frames, reads the pages' pixels, and takes each
  changed plot's rectangle to upload; the atlas uses no graphics API.
  Distance fields share the pages (`muiGlyphAtlas_GetField`), keyed by
  font, glyph, size and spread, without a pen: a field is placed from
  the pen and baseline at its own size and drawn at any size by
  scaling. The empty gutter reads as far outside, as a field's border
  does.
- **Measuring** uses the font's own advances, shaped at a scale of its
  units per em and scaled by size over units per em, so sizes are the
  same at every device scale; nothing is hinted.
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
  failing in turn. A seeded fuzz test damages Ahem and Liberation Sans
  in the ways files are damaged and lays text out in what is read, and
  lays out random UTF-8 (ill-formed bytes, controls, bidi controls,
  four scripts, emoji) in random styles, directions and widths,
  checking that painting draws no more lines than measuring found;
  the sanitizers run it on every change.

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
