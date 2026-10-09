# References

Published sources the library's algorithms are implemented from, as
the family's conventions ask (section 17). No code is taken from any of
them; where an entry names another engine, the library matches that
engine's behaviour, not its source.

## Layout and style

- **Flexbox:** W3C, "CSS Flexible Box Layout Module Level 1", section 9
  (the layout algorithm). `src/flex.c`, `src/flex_item.c`,
  `src/flex_resolve.c`.
- **Alignment:** W3C, "CSS Box Alignment Module Level 3".
  `src/flex_resolve.c`.
- **Aspect ratios:** W3C, "CSS Box Sizing Module Level 4", the
  ratio-dependent minimum. `src/flex_item.c`.
- **Cached sizes:** the rules by which Yoga's measure cache answers a
  new constraint with an earlier size. `src/solve.c`.

## Motion

- **Easing:** W3C, "CSS Easing Functions Level 1", its keywords and
  cubic Bézier curves, solved for time by a fixed number of bisection
  steps. `src/easing.c`, `src/transition.c`.
- **Reversed transitions:** W3C, "CSS Transitions", the shortening of a
  transition reversed partway. `src/animation.c`.
- **Springs:** the closed-form solutions of the damped harmonic
  oscillator in its underdamped, critically damped and overdamped cases,
  started from an offset and a velocity, as Flutter's
  SpringSimulation is. `src/spring.c`; scrolling's spring takes
  Flutter's iOS scroll physics' constants, `src/scroll.c`.
- **Elementary functions:** W. J. Cody and W. Waite, "Software Manual
  for the Elementary Functions", Prentice-Hall, 1980, for argument
  reduction by a constant split in two parts, with the splits of Sun's
  fdlibm (1993); Taylor series evaluated by Horner's rule.
  `src/motion_math.c`.

## Colour and compositing

- **sRGB:** IEC 61966-2-1:1999, the sRGB transfer function.
  `src/color.c`.
- **Oklab:** B. Ottosson, "A perceptual color space for image
  processing", 2020. `src/color.c`.
- **Compositing:** T. Porter and T. Duff, "Compositing Digital Images",
  SIGGRAPH 1984; W3C, "Compositing and Blending Level 1", for the
  separable and non-separable blend modes. `src/colr_composite.c`.

## Fonts and glyph images

- **OpenType:** Microsoft and Adobe, "OpenType Specification":
  COLR and CPAL (`src/colr_paint.c`, `src/colr_gradient.c`,
  `src/colr_composite.c`), CBLC and CBDT and sbix
  (`src/bitmap_glyph.c`).
- **Font matching:** W3C, "CSS Fonts Module Level 4", its font
  matching algorithm: width, then style, then weight.
  `src/font_family.c`.
- **Curve flattening:** Wang Guo-Zhao's bound on the number of segments
  within a tolerance of a Bézier curve (1984). `src/flatten.c`.
- **Multi-channel distance fields:** V. Chlumský, "Shape Decomposition
  for Multi-channel Distance Fields", master's thesis, Czech Technical
  University in Prague, 2015, and the edge colouring of msdfgen.
  `src/multi_field.c`.
- **Atlas packing:** J. Jylänki, "A Thousand Ways to Pack the Bin: A
  Practical Approach to Two-Dimensional Rectangle Bin Packing", 2010,
  its skyline bottom-left method. `src/skyline.c`.
- **Images scaled:** area averaging when shrinking and bilinear
  sampling at pixel centres when growing. `src/image_scale.c`.

## Images

- **PNG:** W3C, "Portable Network Graphics (PNG) Specification (Third
  Edition)", with its CRC-32 and Paeth predictor. `src/png.c`.
- **DEFLATE and zlib:** P. Deutsch, RFC 1951, "DEFLATE Compressed Data
  Format Specification version 1.3", and P. Deutsch and J.-L. Gailly,
  RFC 1950, "ZLIB Compressed Data Format Specification version 3.3",
  with its Adler-32; canonical Huffman codes decoded from the counts of
  each length, the approach of Mark Adler's puff. `src/inflate.c`.

## Text

- **Bidirectional text:** Unicode Standard Annex #9, "Unicode
  Bidirectional Algorithm", rules L1 and L2 for reordering a line
  (the levels come from Maul Unicode). `src/text_layout.c`.
- **Line breaking:** Unicode Standard Annex #14, "Unicode Line Breaking
  Algorithm" (the opportunities come from Maul Unicode).
  `src/text_blocks.c`, `src/text_block.c`.
- **Scripts:** Unicode Standard Annex #24, "Unicode Script Property",
  for runs of one script. `src/text_blocks.c`.
- **Backspace:** a code point back, but emoji, flags and keycaps
  whole, as Blink's and Android's Backspace delete.
  `src/text_delete.c`.
- **Length limits:** a field's maximum length counted in grapheme
  clusters, as Flutter's is. `src/text_rules.c`.

## Input and accessibility

- **Directional navigation:** the comparisons of Android's focus
  search (FocusFinder): in the direction, in the beam, then by a
  distance weighted toward the direction's axis. `src/navigate.c`.
- **Arrow keys in a scroll container:** Android ScrollView's rule, a
  focus move to a node within half a scrollport of the visible part,
  else a line's scroll. `src/event.c`.
- **Semantics on the web:** an invisible semantics tree, and a live
  region emptied 300 ms after an announcement, as Flutter's web engine
  makes them. `src/aria_page.c`.

## Hashing

- **FNV-1a:** G. Fowler, L. C. Noll and K.-P. Vo, the
  Fowler/Noll/Vo hash, as described in the IETF draft "The FNV
  Non-Cryptographic Hash Algorithm". `src/access_build.c`,
  `src/font_chain.c`, `src/text_runs.c`.
