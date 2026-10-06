# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Changed

- `muiDrawInput` has two more fields, `paint` and `paintUser`; an
  initializer that lists its fields by position needs them.
- `muiLayoutInput` has one more field, `baseline`; an initializer that
  lists its fields by position needs it.
- A context's parts start on 64-byte cache lines, so drawing no longer
  slows by up to 30% when an unrelated part of the context changes
  size; a context takes up to about 2 KB more.
- Property ids come in groups of 64, one per values struct: visual ids
  move from 40 to 56 to 64 to 80, and `MUI_VISUAL_PROPERTIES` is the
  visual group's mask. `muiStyle_ResetProperties`,
  `muiNode_ResetProperties`, `muiNode_GetDirectProperties` and
  `muiStyle_SetTransition` take a `muiPropertyGroup`;
  `MUI_ALL_PROPERTIES` and `mui_propertyCount` are gone, and
  `MUI_PROPERTY_GROUP` gives a property's group.

### Added

- Font families (`muiCreateFontFamily`, `muiFontFamily_GetKey`,
  `muiFontFamily_MatchFace`): faces a text style names together, matched
  by width, slant and weight as CSS matches them, with fallbacks of
  their own and a `fontFamilies` limit (record mui-0006).
- Editing primitives (`maul-ui/text_edit.h`): hit testing a point to a
  position (a byte offset and an affinity), a position's caret, the
  rectangles a range covers, and moving a position by cluster (in the
  text or on screen), word, line or to the text's ends, over text laid
  out as it is painted; what a deletion either way removes
  (`muiTextBlock_FindDeletion`), and replacing and reading a block's
  text (`muiTextBlock_Replace`, `muiTextBlock_GetText`) (record
  mui-0006).
- Font fallback: each grapheme cluster is drawn in the first font of a
  chain (the style's, its family's fallbacks, then the service's,
  `muiSetFallbackFonts`) that has its characters, so a line may hold
  glyph runs of several fonts (record mui-0006).

- Font instances: a text style's weight, slant and size set a variable
  font's wght, ital, slnt and opsz axes, and make bold and oblique for
  faces without them, as CSS does; glyph runs carry font keys naming
  the instance, which glyph images and atlases draw (record mui-0006).

- Distance fields: `muiRenderGlyphField` renders a glyph as a signed
  distance field a renderer scales to any size, overlapping contours as
  one shape, the same bytes on every platform; `muiGlyphAtlas_GetField`
  packs fields into an atlas's pages beside coverage, keyed by font,
  glyph, size and spread (record mui-0006).

- Glyph atlases (`maul-ui/glyph_atlas.h`): glyph images packed into
  pages of 8-bit coverage a renderer uploads, found by font, glyph, size
  and quarter-pixel pen position, with plots evicted least recently
  used across frames and the changed rectangles reported (record
  mui-0006).

- Glyph images (`maul-ui/glyph_image.h`): `muiRenderGlyph` renders a
  glyph of a glyph run's font at a size in device pixels and a subpixel
  offset into the caller's memory as 8-bit coverage, unhinted, the same
  bytes on every platform (record mui-0006).

- Baseline alignment: `mui_alignBaseline` for `alignItems` and
  `alignSelf`, and a `muiBaselineFunction` in `muiLayoutInput` that
  gives host content's first baseline; `muiTextBaseline` is the text
  service's. Containers take their baselines from their children as
  Chrome does, checked by 37 more Chrome fixtures (record mui-0003).

- A text benchmark (`bench/bench_text.c`): 2,000 labels and a
  4,000-word paragraph in Liberation Sans, laid out, laid out again at
  a new width, and painted.
- Text blocks (`maul-ui/text_block.h`): UTF-8 text the text service
  lays out as host content, measured by `muiMeasureText` and painted
  by `muiPaintText` in the node's text style: lines broken by UAX #14,
  bidi order by UAX #9, glyphs shaped by HarfBuzz; font keys and a
  default font; `muiNode_IsRightToLeft`; a `textBlocks` limit
  (record mui-0006). A line that breaks where shaping is unsafe to
  break is shaped alone, so a kern or ligature across the break does
  not carry over.
- Glyph runs in the draw list: `muiDrawInput` gains a paint function
  for host content (`muiPaintFunction`), which adds runs with
  `muiDrawSink_AddGlyphRun`; the list gains `mui_drawGlyphRun`
  commands and a glyph table, reserved by the new `drawGlyphs` limit
  (record mui-0005).

- Text style (`maul-ui/text_style.h`): the text property group (color,
  font key, size, weight, slant, line height, letter spacing,
  alignment, wrapping), inherited down the tree with sizes relative to
  the parent's, through classes, tokens, themes, transitions and direct
  writes; `muiNode_GetTextStyle` reads the computed values, and a
  change marks host content to be measured or painted again (record
  mui-0004).
- The library skeleton: the build, the family rules and tools, the
  version and result API (`muiGetVersion`, `muiResultName`) and the
  library profile.
- The context (`muiCreateContext`, `muiDestroyContext`) with its
  allocator and node limit, and the node tree: creation with a host
  key, insertion before a sibling, detaching, subtree destruction,
  traversal and stale-id refusal (record mui-0002).
- Layout: authored values (`muiLayoutStyle`: Scale+Offset sizes and
  limits, flex container and item fields, margin, border and padding,
  host content), `muiComputeLayout` with a host measure function, and
  `muiNode_GetRect`. The solver follows CSS Flexbox for a single line
  and is checked against Chrome by a fixture corpus (record mui-0003).
- A layout benchmark (`bench/`): cold, static, one-change and resize
  frames over a 40,001-node list and a 9,841-node nested tree.
- Wrapping (`muiFlexWrap`) and line placement (`muiAlignContent`), with
  15 more fixtures from Chrome.
- Absolute nodes (`muiPlacement`: Scale+Offset insets in the parent's
  padding box and an anchor point) and automatic margins
  (`marginAuto`), with 22 more fixtures from Chrome.
- Right-to-left layout (`muiTextDirection`, inherited), with 11 more
  fixtures from Chrome; the oracle now writes CSS's logical properties.
- Aspect ratio (`muiSizing.aspectRatio`), with 10 more fixtures from
  Chrome.
- Style classes with state variants, node types and direct writes
  (`maul-ui/style.h`), resolved in fixed layers before layout (record
  mui-0004); new context limits for styles, node types and property
  sets. `muiNode_SetLayoutStyle` now writes every layout property
  directly. The benchmark gains a list styled through node types.
- Style conditions over a node's last size and direction and over the
  environment (`muiCondition`, `muiSetContextEnvironment`), with values
  that may not set what their condition reads; `muiIsUpdatePending`.
- A notification queue (`muiNextNotification`, `mui_empty`, the
  `notifications` limit) and oscillation reports: a node whose
  conditions flip with its own layout is reported once and held.
- The library's own exponential, sine and cosine, CSS's cubic Bezier
  easing solved with fixed iteration counts, and a closed-form spring,
  for transitions; `tools/source-bans.txt` keeps `src/` off the C
  library's transcendental functions.
- Transitions (`maul-ui/transition.h`): timed and spring specs named by
  class variants and resolved through the style layers, run against the
  host's `muiLayoutInput.timeNs`, with CSS reversal shortening, springs
  that keep their speed, reduced motion, and new `transitions` and
  `animations` limits.
- Visual values (`maul-ui/visual.h`): background, gradient, corner
  radii, border colors, shadows, image with 9-slice and tint, opacity
  and clipping, set through the same classes, variants, conditions,
  transitions and direct writes as layout values and marking paint
  only. `MUI_VISUAL_PROPERTIES` and `MUI_ALL_PROPERTIES` join
  `MUI_LAYOUT_PROPERTIES`. The benchmark gains a painted list.
- Colors, shadows and image slice insets move with transitions; colors
  in premultiplied Oklab.
- Tokens (`maul-ui/token.h`): typed values and aliases that class
  variants name in place of values, for themes; new `tokens` and
  `tokenNames` limits.
- Themes (`maul-ui/theme.h`): token overrides set on subtrees, nested up
  to eight deep, and `muiNode_GetTokenValue`; new `themes` and
  `themeOverrides` limits. A detached node is restyled.
- The draw-command list (`maul-ui/draw.h`, record mui-0005): boxes,
  shadows and images in paint order with clip chains, linear
  premultiplied colors and per-kind pixel snapping, checked against
  golden lists; new `drawCommands`, `drawClips` and `drawGradients`
  limits. A static frame keeps the last list, and a build copies the
  subtrees that did not change from it.
- The text component (`MAUL_UI_TEXT`, record mui-0006): the text
  service (`muiCreateTextService`) and fonts from memory
  (`muiCreateFont`, `muiCountFontFaces`, `muiFont_GetMetrics`), over
  FreeType 2.14.3 and HarfBuzz 14.5.1 fetched by hash and built into
  the library, and Maul Unicode 0.2.0. Damaged fonts and compressed web
  fonts are refused with the new `mui_errorFormat`.

### Fixed

- The text component builds for WASI (wasm32-wasi): it no longer asks
  for a threads library there, where HarfBuzz is built without threads,
  and HarfBuzz keeps `errno`, which wasi-libc declares thread-local.
