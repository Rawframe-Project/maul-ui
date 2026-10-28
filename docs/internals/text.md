# Text

Text needs fonts, shaping, bidirectional text, line breaking, glyph images and editing. The core stays free of all of it, so that a host with its own text stack uses the core alone. A host that wants text gets a component that gives the same results on every machine. Shaping and font loading are large, mature domains, and FreeType and HarfBuzz are what nearly every text stack uses, so the component builds on them rather than replacing them.

## Contents

- [The component and its boundary](#the-component-and-its-boundary)
- [Text blocks and shaping](#text-blocks-and-shaping)
- [Lines](#lines)
- [Spans](#spans)
- [Fonts, families, instances and fallback](#fonts-families-instances-and-fallback)
- [Metrics and measuring](#metrics-and-measuring)
- [Dependencies](#dependencies)
- [Memory](#memory)
- [Hostile input](#hostile-input)

## The component and its boundary

The text modules are built into `maul-ui` when `MAUL_UI_TEXT` is on, the default. With it off the library is the core alone. No core module includes a text module, which [modules.txt](../../tools/modules.txt) enforces, and [text-headers.txt](../../tools/text-headers.txt) names the public headers an install without the component leaves out.

The text service is an owner object of its own (`muiCreateTextService`), separate from any context and used by one thread at a time ([text_service.c](../../src/text_service.c)). Fonts are ids it gives out. A font's bytes are copied, or borrowed from a caller who keeps them unchanged until the font is destroyed.

The core reaches text only through host content: the measure, paint and baseline functions. The service provides all three (`muiMeasureText`, `muiPaintText`, `muiMeasureTextBaseline`), with a user pointer naming the service and the context. A node's host key is a text block's key, and a text style's font is a font's key, 0 being the service's default font. The service reads a node's resolved text style and direction through public getters, so it uses the core exactly as any other text stack would.

## Text blocks and shaping

A text block holds UTF-8 text the service copies ([text_block.c](../../src/text_block.c)).

- When the text is set, its line break opportunities (UAX #14) and script runs (UAX #24) are found through Maul Unicode.
- On first use with a font and direction, its paragraphs get bidi levels (UAX #9) and are split into items of one level and script. Each item is shaped by HarfBuzz with the text around it as context and without a language, so the result does not depend on the process's locale ([text_shape.c](../../src/text_shape.c)). HarfBuzz is given the item's code points as Maul Unicode decodes them, with the five on each side it reads as context, and its clusters are mapped back to bytes. Its own UTF-8 decoder would make each byte of an ill-formed sequence a U+FFFD and a cluster, so a caret could stop inside what the rest of the component reads as one character; decoded here, each maximal subpart is one U+FFFD, as the [Encoding Standard's UTF-8 decoder](https://encoding.spec.whatwg.org/#utf-8-decoder) and browsers have it.
- That shaping is kept until the text, font or direction changes. Sizes and widths only scale it and break it into lines, so a resize reshapes nothing.

`muiTextBlock_Replace` replaces a range, analyzing the new text whole before the old goes, and reanalysis is kept to the paragraphs an edit touched.

## Lines

Lines break greedily at opportunities, from the shaped advances ([text_lines.c](../../src/text_lines.c)).

- The text's own line breaks end lines, and white space is kept as written. Spaces ending a wrapped line hang, and a word wider than the line overflows.
- A line that breaks where HarfBuzz marks shaping unsafe to break, or inside a cluster such as a ligature, is shaped alone with its trailing white space, as Blink reshapes line edges. Its glyphs and width come from that, while the break itself is chosen from the block's shaping.
- Max-content keeps only the text's line breaks, and min-content breaks at every opportunity.
- Letter spacing follows each cluster.
- The line height is the style's, or the font's ascent, descent and line gap, with the leading split above and below.

Painting reorders each line by UAX #9 rules L1 and L2, aligns it by the paragraph direction, and draws a glyph run per line and item. Default ignorables, such as bidi controls, draw no glyph.

## Spans

Spans (`muiTextSpan`) style parts of a block apart from its node's style: a byte range on character edges and the text properties a mask names, later spans winning where they overlap. They are what a host's rich-text markup turns into, since Maul UI parses no markup.

- Spans set what painting reads (color, decoration and its color), what shaping does (font, size, weight, slant) and a baseline shift, CSS's `baseline-shift` as Scale+Offset of the node's size.
- Text under spans that shape is shaped in run styles: the node's and up to 31 distinct ones the spans make, a byte each, with items splitting where the style changes.
- A line is as tall as the runs on it reach above and below the baseline, each font's ascent and descent at its run's size with half its leading each way, shifted as the run is, as CSS places an inline box. Hit tests, carets and selections read each line's own top and height.
- Painting splits a line's glyph runs where the ink changes, in visual order, and draws underlines and overlines before the glyphs and line-through after, as CSS paints them.
- Setting the text drops the spans. A replacement moves those after it and trims those it cuts, and a span grows with text put strictly inside it.

## Fonts, families, instances and fallback

- **Instances.** A font key names a font and an instance of it: the wght axis's value, ital, slnt or a shear, the opsz axis's value, and a synthetic bold ([font_instance.c](../../src/font_instance.c)). The instance is decided once when text is laid out, from the style's weight, slant and size, as CSS decides them: wght from the weight; ital for italic, slnt -14 for oblique or for italic without ital; opsz from the size; bold from 600 where the face cannot reach 600, outlines and advances grown by an em/24; an oblique shear of a quarter where the face is upright without either axis. Glyph runs carry the key, and glyph images and atlases rebuild the instance from it, so instances have no lifetime.
- **Families.** A font family holds up to 256 faces (`MUI_MAX_FAMILY_FACES`), and a text style names one by its key ([font_family.c](../../src/font_family.c)). Layout matches a face as CSS Fonts 4 does: the width nearest normal, narrower first; then italic, oblique and normal faces in CSS's order for the slant; then the weight in CSS's order, ties going to the earlier face. A variable face matches every value its axes reach.
- **Fallback.** Text is drawn with a chain of fonts: the face of the style's font or family, then up to 8 fallbacks the family names, then up to 8 the service names (`MUI_MAX_FALLBACKS` each) (`muiSetFallbackFonts`) ([font_chain.c](../../src/font_chain.c)). Each grapheme cluster is drawn in the first font of the chain that has all its characters, controls and default ignorables passed over. A cluster whose first character belongs to no one script stays in the font before it when that font has it. Items split where the font changes, each shaped in its own font's units per em, and line breaking reads sums in ems. Lines take the first font's metrics.

## Metrics and measuring

Measuring uses the font's own advances, shaped at a scale of its units per em and scaled by size over units per em. Nothing is hinted, so sizes are the same at every device scale and glyph images sit on the advances text is laid out with.

Metrics (`muiFont_GetMetrics`) are in ems. The ascent, descent and line gap are the typographic ones when the font sets `USE_TYPO_METRICS`, else the horizontal header's, else the typographic or Windows ones, as HarfBuzz chooses.

## Dependencies

[TextDependencies.cmake](../../cmake/TextDependencies.cmake) builds the dependencies into the library:

- FreeType and HarfBuzz are fetched as their release archives, checked by SHA-256 and compiled in reduced configurations. FreeType gets its TrueType, CFF and PostScript-hinter modules with the smooth rasterizer, without zlib, bzip2, PNG or Brotli, so compressed web fonts are refused, and without environment properties ([cmake/text/](../../cmake/text)). HarfBuzz is built lean, with variable fonts, without its Unicode tables, exceptions or RTTI.
- Maul Unicode is fetched at its release tag. It supplies HarfBuzz's Unicode functions, so one Unicode version answers every question the text component asks.
- `MAUL_UI_TEXT_SYSTEM_LIBRARIES` links an installed FreeType and HarfBuzz instead, for programs that already carry them. Results then depend on their versions.

Their objects are built into the library, so an installed library links with nothing more than the C library and libm. HarfBuzz needs no C++ runtime, but CMake sees its objects and would link a program that links the static library as C++, so this build sets `LINKER_LANGUAGE C` for its own programs; in Visual Studio projects that also keeps a C program's C standard. A shared library exports Maul UI's functions only.

## Memory

The service's memory and FreeType's come from the service's allocator. HarfBuzz allocates from the C library: its allocator is chosen at build time for the whole process, and objects it creates lazily and shares would otherwise land in the first service's allocator and outlive it. This is the one exception to the family's allocator rule in the text component.

The service grows its scratch buffers to the largest text it has laid out, shaped and painted, and then allocates nothing more for frames of it.

## Hostile input

Fonts are validated by FreeType and HarfBuzz when created, and data that fails is `mui_errorFormat`. Tests feed damaged and truncated fonts and make every allocation fail in turn. `test_alloc_failure` runs each call of the text component, the context and the accessibility tree that allocates with its first, second and every later allocation failing in turn. Each must answer `mui_errorCapacity` (a measure or paint counts a failure of its service), change nothing, leak nothing, and succeed when run again; or, where a failure is absorbed, answer as the run with nothing failing does. FreeType absorbs failures while it reads a font's `name` and `gasp` tables and its character maps, none of which the library reads. `test_text_fuzz` damages Ahem and Liberation Sans in the ways files are damaged and lays text out in what is read, and lays out random UTF-8 (ill-formed bytes, controls, bidi controls, four scripts, emoji) in random styles, directions and widths, checking that painting draws no more lines than measuring found. The `fuzz_font` and `fuzz_text` targets do the same under libFuzzer.
