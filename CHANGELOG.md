# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Added

- NVDA walks the controls sample in Chrome over the ARIA adapter in the
  `screen-readers` workflow, enabling the page's tree through its hidden
  button as a screen reader user does; `tools/screen_reader/walk.mjs`
  serves and opens a web sample, and its steps may ask the reader what
  has the focus (`focus`).
- The ARIA adapter shows a text input the host edits as a real `input`
  or `textarea`, holding its value and the program's selection: typed
  text and IME compositions are asked of the host as replacements of
  the selection, and a client's selection as the set-selection action,
  while other edits and caret keys stay the program's.
- `mui_errorVersion`: a context or a text service refuses a def built
  against headers of another major or minor version (family record
  0044), before reading the rest of it.

### Changed

- `muiContextDef` and `muiTextServiceDef` carry the version of the
  headers the program was built with (`version`, after the cookie), and
  `muiDefaultContextDef` and `muiDefaultTextServiceDef` are `static
  inline` in the headers, so the program builds them and they stamp its
  version, not the library's. Source compatible; a def built field by
  field must set `version` to `MUI_DEF_VERSION`.
- The reference renderer builds against Maul RHI 0.7.0 (contract
  version 5), whose default instance def is built in the program: a
  program using `maul-ui-rhi` rebuilds against 0.7.0's headers.

### Fixed

- ARIA: Tab on the adapter's elements is kept from the browser. The
  program moves its focus and the elements follow it, but the browser
  moved the DOM focus too, out of the elements, which are not in the
  page's tab order, to the canvas; a screen reader then lost the focus
  after one Tab (found by NVDA walking the controls sample in Chrome).

## [0.2.0] - 2026-10-10

Text for assistive technology on every platform: a field's selection,
lines, words and where its characters are, read through each adapter's
text interface, and screen readers' edits applied as a paste; VoiceOver
and NVDA walking the samples in CI beside Orca; Maul Window 0.13.0 and
Maul Unicode 0.3.0. Known issues: Orca 46 does not announce the first
focus in a window that opens with nothing focused; TalkBack, VoiceOver
on iOS and a browser's screen reader have not walked the samples, their
adapters being tested against the platforms' interfaces in the
emulator, the simulator and headless Chrome.

### Added

- `tools/screen_reader_walk.sh`: a sample walked with Orca from a file
  of steps (`tools/screen_reader/*.steps`), under Xvfb on a private
  session bus, Orca's speech written out a line each, as a release's
  assistive technology step runs on Linux.

- A value text's marks in the accessibility tree (`muiAccessTextMarks`
  in `muiAccessNode`): its selection, the caret at its focus, where its
  lines start and its words, as byte offsets, for the platform
  adapters' text interfaces. The text component gives an editing
  block's selection, its lines as painted and its UAX #29 words; the
  build asks for lines and words only of a node it sends, and leaves
  out marks that do not fit the text; the tree copies them and refuses
  an update whose marks do not fit. `muiNode_MarkAccessChanged` tells
  the tree a selection moved.
- AT-SPI's Text interface on text inputs and nodes with a value text:
  the text by character, word, line and paragraph (the older boundary
  methods too), the character count, the caret and the selection; text
  changes told as deletions and insertions, the caret's moves and the
  selection's changes. Orca reads a field's words and lines and follows
  its caret. Sentences, attributes and character geometry are not
  given yet.
- Assistive technology sets a text's selection and replaces its text:
  `mui_actionSetSelection` and `mui_actionReplaceText`, offered while
  the text is edited (the selection alone when read only), applied by
  the host with the text component's `muiTextPerformAccessAction`,
  which edits as a paste does. AT-SPI's caret, selection and
  EditableText methods ask for them; the clipboard stays the host's.
- UI Automation's Text pattern (ITextProvider2) on text inputs and
  text being edited: the document, the selection and the caret as
  ranges, which normalize and move by character, word, line, paragraph
  and document, read their text, find text in it and select through
  the host; text changed and text selection changed raised. Character
  geometry and attributes are not given yet. One module gives every
  adapter a text's boundaries by unit and its offsets in code points
  and UTF-16.
- UI Automation's Value pattern sets a text input's text whole through
  the host (`mui_actionReplaceText`), UTF-16 read as UTF-8, while it is
  being edited; its IsReadOnly is false only for a text input neither
  read only nor disabled, as Chromium answers.
- NSAccessibility's text on text inputs and text being edited: the
  number of characters, the selected text and its range, the
  insertion point's line, a line's range and an index's line, the
  string and the character of a range, all in UTF-16; the selection,
  the selected text and the whole value set through the host; the
  selected text changed posted.
- UIAccessibility's text on text inputs and text being edited: the
  element adopts UITextInput, through which VoiceOver reads the text
  and moves by character, word, line and paragraph (a tokenizer on the
  boundaries every adapter shares), in UTF-16; the selection set, text
  typed, deleted and replaced through the host; the input delegate told
  of text and selection changes. A node that starts or ends being
  edited gets a new element, and the layout is told.
- Android's text: an edit text's value, and a text view's when its name
  is its value text, moved through by character, word, line and
  paragraph (`ACTION_NEXT_AT_MOVEMENT_GRANULARITY` and its previous)
  as Android's own views move, words being the tree's, with the
  traversal event; text being edited moves its caret through the host,
  extending the selection when asked, other text a cursor of the
  provider's. The selection in the node info; `ACTION_SET_SELECTION`
  and `ACTION_SET_TEXT` through the host; text changed and selection
  changed events, in UTF-16. The provider's packed record grows by the
  granularities and the selection.
- Character geometry in the accessibility tree: `muiAccessTextMarks`
  carries a box each line and the grapheme clusters line after line
  (`muiAccessLineBox`, `muiAccessCluster`), in the node's own space, from
  the text component's hit testing; `muiAccessTree_GetTextRects` gives a
  range's rectangles a line each and `muiAccessTree_GetTextOffsetAt` the
  character at a point, through the transforms. AT-SPI's character and
  range extents and offset at a point, UI Automation's bounding
  rectangles and range from a point, NSAccessibility's frame for a
  range and range for a position, and UIKit's first rectangle, caret
  rectangle, selection rectangles and positions at a point answer from
  them, or from the node's rectangle for text without clusters.
  Android's provider offers `EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY`,
  a rectangle on the screen for each UTF-16 unit asked for.
- `java/proguard-rules.pro`: what an application that shrinks its code
  keeps for the Android adapter's Java, which native code reaches by
  name; the Android test application is shrunk with it, as a release
  build is.

- The `screen-readers` workflow: VoiceOver on macOS and NVDA on Windows
  walk the controls and text samples on every push through Guidepup
  (`tools/screen_reader/walk.mjs`), from the steps files the Orca walk
  reads; steps files name phrases every reader must say (`expect`),
  which the Orca walk checks too.

### Changed

- The Maul Window glue builds on Maul Window 0.13.0 and the text
  component on Maul Unicode 0.3.0, the release Window 0.13.0 builds on.
  `muiWindowGlue_Paste` reads the text of the read its event answers,
  and passes (`mui_empty`) a read whose text a later read replaced.
- `muiAccessTextFunction`, and the text component's `muiAccessTextOf`,
  fill a `muiAccessContent` (the text, its selection, its lines, words
  and clusters) and are given the part of the node shown when lines,
  words and clusters are wanted (NULL otherwise), in place of a text and
  its length. The text component reads clusters only for the lines
  within a shown height of it, at most 16,384, the other lines marked
  `omitted`: a 1 MB field's send after a keystroke went from 101 ms and
  922,545 clusters to 32 ms and 9,678.
- The text component keeps a block's words between reads and, after an
  edit, segments again only the paragraphs it changed, moving the words
  after them: the 1 MB field's send went from 32 ms to 7 ms.
- `muiTextEditOutcome` says whether the selection moved (`selected`),
  which the host tells the accessibility tree.
- A typed id is named for what it names, as across the family:
  `muiNotification`'s `nodeId` field is `node`, a source change; the
  out-parameters `nodeIdOut`, `styleIdOut`, `themeIdOut`, `tokenIdOut`,
  `transitionIdOut` and `typeIdOut` are `nodeOut`, `styleOut`,
  `themeOut`, `tokenOut`, `transitionOut` and `typeOut`, which changes
  no caller.

### Fixed

- AT-SPI's `GetCharacterExtents` at the largest offset a client can
  name no longer overflows working out the next one; it answers no
  extents, as past any text's end (found by fuzzing).
- `maul-ui-rhi` links as C, as maul-ui does: linked with maul-ui's
  HarfBuzz objects it linked as C++, and Visual Studio's project lost
  its C standard, so it did not build with ClangCL.
- A cell, a header, a row, an option, a tree item, a tooltip and a
  heading with no name of their own are named from their content, as
  ARIA 1.2's roles supporting name from content are (accname 1.2, step
  2F), for every platform adapter: a tree's and a list box's items were
  nameless to Orca. A node named so takes its own content's text first,
  and a labelled node inside stands for its whole subtree.

- A COLR version 1 graph that reaches one paint by many paths is
  refused once 4096 paints were visited (`MUI_MAX_PAINT_VISITS`): a
  damaged or hostile font of 30 levels, each painting the next twice,
  stayed within the depth bound and took about two billion paints. The
  size of a graph's surfaces, and of a colour glyph's image in floats,
  is checked before it is reserved, so a large box many levels deep no
  longer wraps where a size_t is 32 bits and has its surfaces written
  past their end; a surface that cannot be had is a capacity error.

- Every platform adapter shows a focused container's active descendant
  (`mui_relationActiveDescendant`) as the focus, as browsers do: AT-SPI
  tells it focused in the container's place, with active-descendant-
  changed on the container; UI Automation raises its focus change and
  gives it keyboard focus; NSAccessibility, UIAccessibility and Android
  move their focus to it. Moving within a tree or a list box by arrows
  was silent to Orca; only the ARIA adapter, through the browser, had
  it.

## [0.1.0] - 2026-10-09

The first release: the node tree, flex layout checked against Chrome,
style, the text service with font fallback and editing, interaction,
virtualization and popups, the accessibility tree with its six
platform adapters, the reference renderer on Maul RHI and the glue to
Maul Window. Known issues: no adapter has text interfaces yet, the
active descendant is shown by the ARIA adapter alone, and Orca 46 does
not announce the first focus in a window that opens with nothing
focused.

### Changed

- A ratio item's width, floored by its content, is capped by its
  maximum height through the ratio when no height is given (CSS Sizing
  4), as a column's lines measure it; a row's ratio item still counts
  its content in full for its automatic minimum. As Chrome does; seven
  Chrome fixtures hold the cases.

- Absolute boxes, as Chrome does (CSS Position 3, section 4.1): one with
  both insets automatic on an axis fits the room from its static
  position to the containing block's edge it aligns away from, not the
  whole containing block; one stretched by its own align-self is placed
  by start's overflow rule when a given height overflows its insets, and
  with a ratio takes its height from its insets, its width following. A
  maximum carried through a ratio no longer falls under its axis's
  minimum. Twenty-three Chrome fixtures hold the cases.

- A wrapping row sized by its content is at least as wide as each item's
  min-content contribution: an item that cannot grow, alone on a line,
  counts its content at its narrowest under a smaller basis, not its
  content at its widest. As Chrome does; seven Chrome fixtures with
  wrapping content hold the cases.

- A flex item's base from its content is no longer clamped by its own
  minimum or maximum (Flexbox 9.2.3): an item with a minimum width and a
  grow factor took less of the free space than Chrome gives it. And an
  absolute ratio box's height carried from its width stays under its own
  maximum height. As Chrome does; eight Chrome fixtures hold the cases.

- In a column, a ratio item's automatic minimum counts its content's
  width through the ratio when its width is not exact, as Chrome does;
  the solver asks a node for its content's answer with its aspect ratio
  left out to find it. One Chrome fixture holds it.

- A maximum carried through an aspect ratio is no lower than the box's
  padding and border; and an absolute box stretched by its own
  align-self over insets that leave too little overflows as start does,
  kept within its container's padding box. As Chrome does; ten Chrome
  fixtures hold the cases.

- In a row, a ratio item's content size suggestion is capped by its
  cross maximum through the ratio while its cross size is not definite,
  and in rows and columns the cap is no lower than its cross padding
  and border through the ratio. As Chrome does; twelve Chrome fixtures
  hold the cases.

- In a column, a given basis replaces an item's height, so the height
  alone does not make it definite, and a ratio item's height from the
  width it fits is definite; an absolute child aligned by baseline sits
  at the writing mode's start under wrap-reverse too; and over crossed
  insets an absolute box's horizontal automatic margins share none of
  the space, so a box may overflow it. All as Chrome does; nineteen
  Chrome fixtures hold the cases.

- Asked an item's baseline, a row lays the item out at its content's
  height, as it lays it out after, so a percentage height below it does
  not resolve there either. As Chrome does; one Chrome fixture holds it.

- A wrapping row's max-content width is the larger of its items'
  single-line sum and its widest item alone on a line, a basis capping
  only the sum; and an absolute box aligned by baseline between both
  insets falls back to start (CSS Box Alignment section 9.3). Both as
  Chrome does; eight Chrome fixtures hold the cases.

- A line's largest ascent and descent may be negative when baselines lie
  outside their items (Flexbox 9.4); baseline-aligned items in a column
  line up their line-left edges as one group rather than acting as
  start; a minimum whose scale cannot resolve keeps its offset (CSS
  Sizing 3 section 5.2.1); and a column of open width lays its items out
  at the width it finds, so stretched ones take it. All as Chrome does;
  twenty-five Chrome fixtures hold the cases.

- In a column, a ratio item's content size suggestion is capped by its
  maximum width through the ratio (Flexbox 4.5), and its base comes from
  the width it fits through the ratio, its content counting through its
  automatic minimum (Flexbox 9.2.3 B); the suggestion's floor through
  the ratio counts the item's cross padding and border; and an absolute
  box's vertical automatic margins centre it in the space its insets
  leave, none when they cross. All as Chrome does; six Chrome fixtures
  hold the cases.

- A column asking an item's width contribution gives it the height its
  style gives; a ratio item's content size suggestion is floored by its
  cross minimum through the ratio (Flexbox 4.5); and an absolute box
  with a ratio and a height from both insets is at least its width's
  height through the ratio and, its minimum automatic, its content's.
  All as Chrome does; eight Chrome fixtures hold the cases.

- An absolute box with an aspect ratio and a height from its style takes
  its width through the ratio rather than from its horizontal insets; a
  stretched item's padding and border hold the floor of a ratio row's
  height; and a basis caps an item's contribution to its row's content
  width only on a single line, a wrapping row counting the item's
  preferred width or content. All as Chrome does; eleven Chrome fixtures
  hold the cases.

- Three aspect-ratio cases follow Chrome: a root given both sizes keeps
  its width's limits, its ratio flooring it only at the content; an
  absolute box's width from both insets is clamped by its height's
  limits, padding and border among them, through the ratio; a column
  item's height its ratio gives from a definite width is definite for
  percentages below. Three Chrome fixtures hold the cases; the corpus
  keeps seed 28's 196 random layouts that agree with Chrome
  (`test/layout/random28.txt`) and one more of seed 26.

- Absolute boxes between both vertical insets follow CSS Position 3 and
  CSS Box Alignment as Chrome does: with start, end or centre
  `align-self` the automatic height is fit-content, not stretched; a box
  that overflows the space between its insets covers it, aligned as far
  as the bounding box of that space and the container allows. An
  absolute box's aspect ratio carries its width's padding and border to
  its height as a minimum. A column's min-content height query of an
  item with a ratio and a given height is answered by the content, so
  such an item can shrink. Eight Chrome fixtures hold the cases, and the
  corpus keeps seed 27's 194 random layouts that agree with Chrome
  (`test/layout/random27.txt`).

- A box whose style gives its height and whose width is automatic takes
  its width through its aspect ratio when asked its width with its
  height left open, as a column asks its items, as Chrome does: an item
  60 high at 2:1 in a column of automatic width is 120 wide, and so is
  the column. Two Chrome fixtures hold it, and one more random layout
  joins the corpus.

- The content floor of a row's height from its aspect ratio counts a
  stretched item's minimum height, a percentage resolved against the
  ratio's height, and its margins, as Chrome does; the row is measured
  at that height. Two Chrome fixtures hold it, and one more random
  layout joins the corpus.

- A single-line column sized by its content takes its width from its
  items' contributions, each item's width with its height left open
  (Flexbox 9.9.2), as Chrome does, rather than from their widths at
  their flexed heights: an item whose width follows its height through
  an aspect ratio no longer widens the column. Four Chrome fixtures hold
  the cases.

- Absolute boxes follow Chrome in two more ways. With an aspect ratio
  and a fixed width, the height comes from the ratio even when both
  vertical insets are set; otherwise a height from the insets gives the
  width, clamped by the width's limits through the ratio. With both
  vertical insets and a height of its own, a box is placed between them
  by its own `align-self` (CSS Position 3), at the top inset when they
  cross. Fifteen Chrome fixtures hold the cases, and one more random
  layout joins the corpus.

- In a column of no definite height, a percentage flex-basis that cannot
  resolve is `content`, as CSS Flexbox 7.2.3 says and Chrome does: the
  item takes its content's height, not its height property's. Rows
  resolve the percentage once their width is known, as before. Four
  Chrome fixtures hold the cases, and one more random layout joins the
  corpus.

- A box's start and end margins, automatic margins and insets follow its
  own direction, as CSS Logical maps them and Chrome does: a
  right-to-left item in a left-to-right row takes its start margin on
  its right. Children that inherit their direction are unchanged. Nine
  Chrome fixtures hold the cases, and three more random layouts join
  the corpus.

- `space-around` and `space-evenly` with too little room put the content's
  start at the writing mode's start, as CSS Box Alignment's safe-centre
  fallback says and Chrome does, also when the direction is reversed or
  the lines wrap in reverse, where the content started at the flex start
  before. `space-between` keeps the flex start. Twelve Chrome fixtures
  hold the cases, and seven more random layouts join the corpus.

- A height an aspect ratio gives is definite, as in Chrome: percentages
  below resolve against it for a flex item in a row, a root and an
  absolute node, where they behaved as automatic. Four Chrome fixtures
  hold the cases, and two more random layouts join the corpus.

- A width given as a percentage that cannot resolve, on a box with an
  aspect ratio, keeps the content's min-content width as its automatic
  minimum where the ratio gives less, as in Chrome: an empty-looking
  item 100% wide and 0 high with content 80 wide is 80 wide, not 0. A
  height so given still takes no such minimum. Three Chrome fixtures
  hold the cases, and three more random layouts join the corpus.

- A height from an aspect ratio is floored at the content's height as
  Chrome does: a row's stretched items take that height (Flexbox 9.8),
  so they no longer raise it; a 2:1 row 20 wide holding content 40 high
  is 20 by 10, not 20 by 40. Items not stretched, a column's items and a
  leaf's content still count, and a column measuring an item with a
  ratio counts its content in full. An item with a ratio takes its
  automatic minimum at once, as a base through the ratio may fall below
  it. Seven Chrome fixtures hold the cases.

- A wrapping column's min-content height is its max-content height, as
  for any height in CSS, rather than its tallest item: its automatic
  minimum no longer lets a column of content height shrink it into more
  lines than Chrome does. Two Chrome fixtures hold it.

- A row sized by its content sums its items' contributions, the
  specification's Web-compatible algorithm as Chrome applies it, not
  their flex bases: an item counts its preferred width, else its
  content's, capped by a given basis if it cannot grow and floored by
  it if it cannot shrink (neither for a wrapping row's min-content
  width), within its limits. An empty item with a basis of 120 in rows
  of automatic width leaves them 0 wide, as in Chrome. Columns keep
  summing their items' hypothetical sizes, as Chrome does. Thirteen
  Chrome fixtures hold the cases (test/layout/intrinsic.txt).

- A box with an aspect ratio and neither size known takes its content's
  width within its height's limits carried through the ratio, padding
  and border among them, its own width limits winning, and its height
  from that width, as in Chrome: an empty 2:1 box with padding 10 is 40
  by 20, not 20 by 20, and a minimum or maximum height widens or narrows
  it. Nine Chrome fixtures hold the cases.

- An aspect ratio's automatic minimum follows CSS Sizing 4 as Chrome
  reads it. A flex item whose width comes through its ratio from a
  definite height keeps that width as its automatic minimum; a root or
  absolute node given a height and a width keeps at least its content's
  min-content width, unless its minimum width is set; and a percentage
  size that cannot resolve takes no content minimum. Five Chrome
  fixtures hold the cases.

- A height that is a node's own content height is indefinite, as in
  CSS (Flexbox 9.8), matching Chrome: a flex item not stretched across
  its line, a column item flexed in a column of no definite height, and
  a root or absolute node of automatic height. Percentage heights below
  such a node are automatic, and its stretched items take no flex base
  from an aspect ratio, so a row of content height keeps an
  aspect-ratio item at its content's width rather than squeezing its
  siblings. A cross size given as a percentage no longer stretches when
  it cannot resolve. Four Chrome fixtures hold each case.

- The measure function's contract is written out: its answer depends on
  the request alone, and content that fits within a size measures the
  same within any smaller size it still fits, as text broken greedily
  into lines does. Layout's cache has always relied on it.

- Every creation takes a def, as the family's API asks: `muiCreateStyle`,
  `muiCreateNodeType`, `muiCreateTheme`, `muiCreateToken` and
  `muiCreateTextBlock` take a `muiStyleDef`, `muiNodeTypeDef` (its
  classes), `muiThemeDef`, `muiTokenDef` (its value) or
  `muiTextBlockDef` (its text) from its `...Default...Def`, and refuse
  one without its cookie, so each can gain a field without breaking its
  callers.

- A smaller library: FreeType is built without the TrueType bytecode
  interpreter and the PostScript hinter, as glyphs are always loaded
  unhinted, 13.5 KB less of the text component's wasm, and a CFF test
  font now checks the CFF driver; the core writes its numbers itself,
  as printf writes them, without the C library's printf, 6.6 KB less.

- Layout bounded by the change: a node whose answers to its parent's
  sizing queries hold after a change is laid out alone, its parent not
  solved again, unless a baseline is read through it. One label's new
  text in a list of 10,000 rows goes from milliseconds to tens of
  microseconds; a changed node's parent is still solved when the
  node's own style changed.

- Long text: an edit to a text block analyses, shapes and breaks into
  lines again only the paragraphs it reaches, and a paint function may
  ask its sink what part of the content box can be seen
  (`muiDrawSink_GetVisibleRect`); text of more than 64 lines paints the
  lines seen and one more each way. A node that asks is painted again
  by every build, scrolling included. A keystroke in 100 KB of text in
  a scrolling view goes from about 9.7 ms to about 0.15 ms on the
  project's machine. A combining mark that starts a paragraph is now
  drawn in the first font that has it, not the font before the line
  break.

- World-space panels: `muiRhiTarget` takes an optional projection, a
  column-major 4x4 matrix from the list's logical units into clip
  space, so the reference renderer draws a list as a panel in a 3D
  scene, its edges, clips and glyphs (all from distance fields) sharp
  at any angle. The projection rides at the end of the transform table.
  A renderer made with `depthFormat` and `depthCompare` tests such a
  panel against a depth texture the target names (`depth`), never
  writing it, through a second pipeline.

- Image tiling: `muiDrawImage` carries `repeatX` and `repeatY`, and the
  visual style `imageRepeatX` and `imageRepeatY` (properties 86 and
  87): an image's middle, or the whole of one without slices, stretched
  (as before), repeated, rounded or spaced across and up, as CSS's
  border-image-repeat. `muiDrawImage` is 80 bytes; the command is not
  larger. The reference renderer tiles per fragment, half a texel inside
  the middle, its texture gradients unwrapped across seams.

- Colour bitmap glyphs: `muiRenderColorGlyph` draws a glyph's PNG from
  a font's CBLC and CBDT tables (Noto Color Emoji's) or its sbix table
  (Apple's), from the strike that suits the size, scaled to it in linear
  light; sbix graphics are placed from their glyph's outline box where it
  has one, and 'dupe' graphics followed. Fonts of bitmaps alone, without
  outlines, are accepted, their glyphs without coverage.
  PNG is decoded by Maul UI itself, FreeType staying without libpng.

- Colour glyphs of COLR version 1: `muiRenderColorGlyph` draws a
  glyph's paint graph where it has one, its layers, solid fills,
  linear, radial and sweep gradients (padded, repeated or reflected,
  interpolated in premultiplied linear light), glyph outlines, other
  colour glyphs, transforms, the 28 composite modes of W3C Compositing
  and Blending, and clip box; a graph more than 64 paints deep is
  `mui_errorFormat`.

- The reference renderer draws colour glyphs from an atlas of colour
  glyphs of its own, faded by the run's alpha, the text's colour where
  a layer asks for it; a run a transform scales or turns draws them
  from images at the em drawn.

- The reference renderer draws glyphs a transform scales or turns from
  multi-channel distance fields, the median of their colours, so their
  corners stay sharp when magnified; it keeps an atlas of four channels
  beside its atlas of coverage. Its fields reach 4 field pixels rather
  than an eighth of their em, so their bytes are fine enough for
  magnified edges to fall where the outline is; `test_field_quality`
  measures them against FreeType's exact coverage.

- `muiLayoutInput` ends with `safeArea` and `muiLayoutStyle` with
  `safeArea`: initializers that list every field by position give them
  too (four zeros for none). `muiSides` moved from `maul-ui/draw.h` to
  `maul-ui/layout.h`, which `draw.h` includes. The layout solver's size
  cache keys on direction, which a safe area on a start or end edge
  makes sizes depend on.
- `muiLimits` has five more fields, `layers` (64 by default),
  `pointers` (16, at most 32), `pointerRecords` (64), `neighbors` (256)
  and `drawTransforms` (64); an initializer that lists its fields by
  position needs them. `muiLayoutStyle` ends with `scrollAxes`.
- `muiNode_GetStates` returns the hover and press pointer input gives,
  and the players' focus, as well as the states the host set.
- `mui_stateFocusVisible` comes after `mui_stateFocused`, so hovered,
  pressed, disabled and exiting are one bit higher and their variants
  one more; conditions start at variant 9. Every bit of `muiState` is a
  state, so `muiNode_SetStates` refuses no bits.
- `muiPointerEvent` ends with the player whose device it is.
- Style values are checked, compared and copied with each group's
  struct found once per group: about 5% fewer instructions over the
  core benchmark.
- `muiDrawInput` has two more fields, `paint` and `paintUser`; an
  initializer that lists its fields by position needs them.
- `muiLayoutInput` has one more field, `baseline`; an initializer that
  lists its fields by position needs it.
- `muiLimits` has two more fields, `accessNodes` (512 by default) and
  `accessRoots` (4); an initializer that lists its fields by position
  needs them. Every mark of a node for any stage also marks it for the
  accessibility tree.
- The measure function is never asked with both axes exact, as the
  final pass always gave: the size is then decided. A list of 100,000
  rebound rows asks a half to a third fewer times (record mui-0003).
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

- The samples, windowed on Linux, join the accessibility bus as a host
  does (one application each, named as the program), so a screen reader
  reads them.

- Wrapping host content in the layout fixtures: `content=WxH*N` is N
  boxes W by H that break greedily into lines, as text does, rendered
  in Chrome as inline blocks and measured the same way by the fixture
  test; `test/layout/words.txt` holds fifteen cases.
  `tools/random_layouts.py generate SEED COUNT --words` draws a seed's
  `--wide` layouts with some content made words; earlier draws are
  unchanged. `--deep` draws word layouts in trees up to five levels deep and
  four children a node.

- `docs/releasing.md`: Maul UI's own release steps, taken within the
  family's checklist (the size budget's check, recorded runs with each
  screen reader, fuzzing, the guide and API reference current).

- `tools/random_layouts.py generate SEED COUNT --wide` also draws scaled
  dimensions with an offset, automatic margins, baseline alignment and
  anchors; the default draws are unchanged.

- `tools/random_layouts.py`, a development tool: `generate SEED COUNT`
  draws random layouts in the fixture corpus's format, to render in
  Chrome with `gen_layout_fixtures.py --oracle`; `reduce FILE NAME`
  shrinks one that parts from Chrome to a smallest case, in a temporary
  copy of the tree. The corpus keeps seed 26's 172 layouts that agree
  with Chrome (`test/layout/random.txt`).

- The text service counts misuse, as the context does: every call
  refused as invalid input against a live service, its glyph atlases'
  and text editing's included, read by `muiGetTextServiceMisuse`.

- A guide (`docs/guide.md`): the model, then part by part, from nodes
  and style to text, editing, painting, input, scrolling, transitions,
  accessibility, building and testing; each of its C snippets is built
  and run by the tests as written. The README points to it and to the
  API reference, which CI now holds to the headers.

- Fuzz targets (`MAUL_UI_FUZZ`, with Clang): libFuzzer over zlib
  streams, PNG images and fonts, the font target laying out, painting
  and rendering every glyph of any bytes that open; over text, laid out
  and edited by every kind of edit, an allocation the input chooses
  failing; and over AT-SPI method calls as
  libdbus reads them off the bus; CI runs each for a minute on every
  push, from the seeds `tools/fuzz_seed.py` writes.

- Atlases of colour glyphs: `mui_atlasColor` pages hold colour glyphs,
  which `muiGlyphAtlas_GetColor` renders and packs for a palette and a
  text colour; glyphs without colour layers, and fonts without a COLR
  table, give `mui_empty`.

- Colour glyphs: `muiRenderColorGlyph` renders a COLR version 0 glyph's
  layers from a chosen CPAL palette, the text's colour where a layer
  asks for it, as premultiplied RGBA stored the way an sRGB texture
  holds it.

- Atlases of four channels: `muiGlyphAtlasDef.format` makes an atlas's
  pages a byte a pixel (`mui_atlasOneChannel`, the default) or four
  (`mui_atlasFourChannel`), and `muiGlyphAtlas_GetMultiField` packs
  multi-channel fields into the latter; pages tell their format.

- Multi-channel distance fields: `muiRenderGlyphMultiField` renders a
  glyph's MTSDF, four bytes a pixel, whose colour channels' median
  keeps corners sharp at any scale and whose alpha is the one-channel
  field; overlapping contours are their union, and fields are the same
  bytes on every platform.

- Conic gradients (`mui_gradientConic`): around the box's centre from
  the gradient's angle, stops over a turn, as CSS's `conic-gradient`;
  the reference renderer paints them. A fixture holds the renderer's
  plan to one draw while clips, transforms and gradients change.

- Text editing (`maul-ui/text_editor.h`): a text block opted into
  editing with a field's rules (`muiTextBlock_SetEditing`: multi-line,
  read-only, password, integer and decimal filters, a maximum length in
  grapheme clusters, an undo limit) keeps a selection and an undo
  history; `muiTextBlock_Type`, `_Paste`, `_Erase`, `_EraseTo`,
  `_Undo`, `_Redo`, `_Select`, `_GetSelection`, `_GetSelectedText` and
  `_GetUndoState` edit it, typing undone a word at a time and deletions
  in runs; `muiTextEditMove`, `muiTextEditPress` (one, two and three
  clicks) and `muiTextEditDrag` place the selection through a node's
  laid-out text; `muiTextEditEvent` takes typed text, keys by a PC or
  Mac keymap (`muiKeymap`), the clipboard through a host writer
  (`muiTextEditInput`) and the pointer; `muiTextBlock_Compose` shows an
  input method's composition. The window glue adds `muiWindowCompose`
  and `maul-ui-window/clipboard.h` (`muiWindowGlue_WriteClipboard`,
  `muiWindowGlue_RequestPaste`, `muiWindowGlue_Paste`); the text,
  pickers and colour samples edit through the editor. A password field
  shows, hits and reads as a bullet per character; an editing field's
  text scrolls to keep the caret in view; a field names its input
  purpose (`muiInputPurpose`, `muiTextBlock_GetInputPurpose`), which
  `muiWindowGlue_RequestKeyboard` gives the on-screen keyboard.

- Text decorations and spans: the text style's `decoration` (underline,
  overline, line-through) and `decorationColor` (the text's color at
  alpha 0), and spans over a text block (`muiTextSpan`,
  `muiTextBlock_SetSpans`, `muiTextBlock_GetSpans`) that give parts of
  it their own color, decorations, font, size, weight, slant and
  baseline shift (`baselineShift`, for sub- and superscripts); edits
  move them. Text under spans that shape is shaped in their runs, and a
  line is as tall as its runs reach. Painting splits glyph runs where
  the ink changes and draws decorations from the font's metrics.
- Safe-area insets: `muiLayoutInput.safeArea` takes a surface's
  insets (physical, `muiSides`), and a node names the edges whose
  padding is at least the inset there (`safeArea`,
  `mui_propertySafeArea`), mapped by its direction. Content boxes,
  scroll extents, virtual lists and ranges use the padding with the safe
  area; the samples' root keeps its content inside Maul Window's.
- Images that mirror (`imageMirrors`, `mui_propertyImageMirrors`):
  under right to left the node's image is drawn flipped, a uv rectangle
  of negative width with its slice insets as drawn, which the reference
  renderer draws by running its slices the way the uv runs.
- Local scale (`muiLocalScale`, the visual properties
  `mui_propertyScaleX`, `ScaleY`, `ScaleOriginX` and `ScaleOriginY`): a
  node and its subtree scaled after layout about an origin in its
  border box, drawn through a transform of its own and hit tested,
  mapped (`muiNode_MapToRoot`, pointer records) and reported to
  accessibility through it, while layout keeps the laid-out box. Scales
  transition and take number tokens as other visual values.
- `muiGetWorkCounts` returns the work a context has done since it was
  made: the nodes styled, the sizes the solver computed rather than
  cached, the host's measure calls and the nodes painted rather than
  copied. A test holds that a static frame counts none, and the
  benchmark prints the counts beside its timings.
- `tools/check_litmus.py`, run in the checks job, refuses public
  identifiers whose words name an engine's concepts (world, entity,
  schema, asset, game, scripting and the like), the litmus test of
  record mui-0001.
- `tools/size_report.py` reports each part's wasm at `-Oz` with LTO as
  record mui-0001's new size budget measures it, every public function
  taken: the core above an empty program (144,873 bytes of 160,000),
  the text component above the core without Maul Unicode (565,136 of
  640,000), and Maul Unicode as the text component calls it (70,273).
  The web cell runs it; a release checks it.
- The samples (`samples/`, `MAUL_UI_BUILD_SAMPLES`, built where the
  renderer, the Maul Window glue and text are): programs that use Maul
  UI's parts together and check their own results, so each is also a
  test, skipped (77) without an adapter unless `MUI_RHI_REQUIRED` is
  set. The first, `sample_tour`, opens a window on Maul Window's test
  backend, builds a title, a generated picture, a button and a scroll
  container of rows, feeds input through the glue, keeps the
  accessibility tree, and draws with the reference renderer into a
  texture whose pixels it checks (`--headless`, as its test runs it).
  Without it the tour opens a real window and presents onto a Maul RHI
  surface made from the window's native handles, configured again as
  the window's size changes and let go while the window has no surface,
  until the window is closed, or for `--frames N`, the last frame read
  back and checked. Every sample shares the tour's application frame
  (`samples/app.c`) and is its tree, its scripted input and its checks.
  `sample_controls` composes a button with an icon and a label, a
  checkbox, a switch and a radio group from roles, focus, classes with
  state variants and the host's event function, with ARIA's keys
  (Space, Enter for the button, arrows moving the choice), checked by
  pixels and the accessibility tree's checked states. `sample_ranges`
  composes a slider, a progress bar and a scrollbar over range values,
  the host showing each value and forwarding the scrollbar's to a
  horizontally scrolling list and back. `sample_text` composes single
  and multi-line text inputs over the text editing primitives: the host
  keeps the caret and the selection, edits by typed text, Backspace,
  Delete and Enter, moves by arrows, Home and End with Shift extending,
  paints the selection and the caret, and puts an input method's
  preedit into the focused field with the window's caret following.
  The application frame gains record and paint hooks and
  SampleTextNode. `sample_pickers` composes a select, a searchable
  select and a number input with drag scrub: comboboxes keeping the
  focus while an overlay popup list, made on opening and destroyed on
  closing, shows the active option they name as their active
  descendant, closed by the library's light dismissal; the number a
  range whose keys the library takes and whose drag the host turns
  into a scrub. SampleSame and SampleRound join the frame.
  `sample_tabs_menus` composes tabs with automatic activation and a
  roving Tab stop, a tooltip the host shows after the pointer rests on
  its trigger for half a second, and a context menu at the pointer or
  at the area's corner for the context menu key, taking the focus and
  giving it back when it closes. The frame gains SamplePostButton and a
  per-sample headless clock step. `sample_dialogs` composes a modal
  dialog, a modal layer centred on the root whose focus trap, hit
  blocking and modal report are the library's, closed by Escape or its
  buttons with the focus given back to its opener, and floating dialogs
  as activation layers raised by a press and moved by their title bars.
  `sample_collections` composes a list, a tree and a table of 100,000
  rows over virtual lists, pooled rows bound to the window the library
  wants after each layout, the active row named as the active
  descendant and scrolled into view from offsets the library keeps for
  rows that do not exist, the tree's folders inserting and removing
  their files' items. The frame gains a settle hook that lays out
  again after the host realizes what the layout asked. `sample_color`
  composes a colour picker: a saturation and brightness area of layered
  gradients over the pure hue, hue and opacity sliders that are ranges
  over gradient tracks, a hex field and a swatch. Windowed, the samples
  never wait inside a frame: the renderer is pumped until ready, a
  busy device skips a frame, and the last frame's readback is taken in
  a later one, so they run from the browser's frames too, presenting
  into a canvas; where a surface offers sRGB only as a view (a WebGPU
  canvas), frames are drawn into a staging texture in the sRGB twin and
  copied onto it, and in a browser the program ends in Maul Window's
  quit. For canvas runs (`MUI_WEB_CANVAS`) the web runner starts
  Chrome with its compositor on Vulkan (lavapipe in CI) and ANGLE over
  it, which a canvas's WebGPU frames need; the ARIA test waits for the host's scroll to be put back rather
  than a fixed 20 ms, a scroll event coming with the next rendering. On
  the web the
  headless tour runs in headless
  Chrome's WebGPU through the web runner, which now gives Chrome WebGPU
  and a program its arguments (record mui-0005).
- The Maul Window glue, `maul-ui-window` (`MAUL_UI_WINDOW`, off by
  default; `maul-ui-window/glue.h`), with Maul Window 0.10.0: a glue a
  window takes the records the host drains and feeds them to a context,
  saying of each whether the UI handled it: keys and text to the focus,
  the cursor as a mouse pointer, touches as touch pointers (up to
  `MUI_WINDOW_TOUCHES` at once) and the pen as a pen pointer (its tip,
  barrel and eraser numbered as the W3C's Pointer Events number them),
  their records dispatched; the wheel at the cursor's last place; a
  reset or a lost focus cancelling every pointer. A glue that takes
  gamepads makes their records navigation for the player the host names:
  the d-pad and the left stick the directions, held ones repeating
  through `muiWindowGlue_Tick`, the faces activate and cancel, the
  shoulders previous and next. `muiWindowGlue_SetCaret` asks the window
  to accept text with its caret at a node, and with the text component
  `maul-ui-window/composition.h` sets an input method's preedit into a
  text block (`muiWindowSetComposition`) and places the caret at a
  position of a node's text (`muiWindowGlue_SetTextCaret`). With the
  accessibility tree's consumer, `maul-ui-window/access.h` makes the
  adapter for the window's platform (UI Automation, NSAccessibility,
  UIAccessibility, Android's, ARIA, AT-SPI), sends it the root's
  updates and hands its root to the
  window; a window with no adapter keeps the tree alone (record
  mui-0008).
- `muiNode_MapToRoot`, a point of a node's border box carried through
  its ancestors' places and scroll offsets into its root's space, and
  `muiNode_GetContentRect`, a node's content box in its border box
  (record mui-0007).
- The reference renderer, `maul-ui-rhi` (`MAUL_UI_RHI`, off by
  default; `maul-ui-rhi/renderer.h`), which draws a list with Maul RHI
  0.5.0: `muiCreateRhiRenderer` for a device and a target format,
  `muiRhiRenderer_Notify` with the device's notifications until its
  pipeline is ready, then each frame `muiRhiRenderer_AddPasses` while
  the frame is built and `muiRhiRenderer_Record` once it is compiled.
  Boxes are drawn: fills, borders, rounded corners and gradients
  (linear and radial, mixed in premultiplied Oklab as the core mixes
  colors); and shadows, outer and inset, blurred in closed form; in
  their clips (rounded, inverted and nested, each in its own transform)
  and through their transforms; and images, which the host's function
  (`muiRhiImageFunction`) names by key as a texture and its size, with
  their uv rects, tints and nine slices; and glyph runs, from an atlas
  of the renderer's own over the text service its def names, drawn as
  coverage at device pixels where their transform only moves them, and
  from distance fields where it scales or turns them. What lies wholly
  outside its clips or the target is culled as the list is packed. A
  frame uploads only the records that changed since the last; one whose
  uploads would not fit the device's `frameUploadBytes`, read from the
  device, or the share of it the def's `uploadBytes` gives, is refused
  with `mui_errorCapacity`, nothing added, and `muiRhiRenderer_Forget` makes the next frame upload all
  after the host drops a recorded frame.
- Interaction properties (`maul-ui/interaction.h`): a fourth property
  group with a hit mode (the node and its children, its children alone,
  or neither), a pass-through flag and a layer kind (an activation
  layer, a modal one, or the overlay band), styled through classes and
  states; `muiHitTest`, the topmost node at a point, layers first, cut
  by clips and rounded corners; layers painted after the content they
  are in, outside its clips and opacity, and `muiNode_RaiseLayer`
  (record mui-0007).
- Pointer input (`maul-ui/pointer.h`): `muiPointerInput` takes the
  host's pointer events and keeps hover and press as CSS's `:hover` and
  `:active` do, for several pointers at once; capture, implicit for
  touch and pen and by `muiPointer_SetCapture`; records of presses,
  releases, clicks with their counts, cancels, lost captures and
  captured moves, taken with `muiNextPointerRecord`;
  `muiPointer_GetState` and `muiSetClickRule` (record mui-0007).
- Focus (`maul-ui/focus.h`): a focus per player slot set by code,
  pointer presses and sequential navigation (`muiFocus_Set`,
  `muiFocus_Get`, `muiFocus_Move`), the focus mode and tab order
  properties, `mui_stateFocusVisible` by the cause of each move, focus
  scoped to layers and refused under modal ones, and focus gained and
  lost notifications (record mui-0007).
- Directional navigation: `muiFocus_MoveToward` by Android's focus
  search within the focused layer, and links per direction that win,
  stop or fall back to geometry (`muiNode_SetNeighbor`,
  `muiNode_GetNeighbor`; record mui-0007).
- Routed input (`maul-ui/event.h`): keys, text and navigation actions to
  a player's focus and pointer records to their node, through one host
  function per routed event (`muiSetEventFunction`), tunnelling then
  bubbling on a route fixed before dispatch, ended by a handled event;
  focus moves by default for unhandled Tab, arrows and navigation; each
  input reports whether the UI handled it (record mui-0007).
- Scrolling (`maul-ui/scroll.h`): the `scrollAxes` layout property makes
  a scroll container, clipped at its padding box with an automatic
  minimum of 0; layout measures its extent; `muiNode_SetScroll`,
  `muiNode_GetScroll`, `muiNode_GetScrollExtent` and
  `muiNode_ScrollIntoView`; draw lists carry a transform per scroll
  container, so scrolling alone rewrites only the transform table; hit
  testing, pointer records and navigation follow the offsets, and
  navigation scrolls focus into view (record mui-0007).
- Wheel input (`muiWheelInput`, `mui_eventWheel`): routed, then by
  default scrolling the nearest scroll container that can move, chaining
  outward and latched per wheel transaction; `muiScrollRule` with
  `muiDefaultScrollRule` and `muiSetScrollRule`;
  `muiNode_GetScrollThumb` for scrollbars (record mui-0007).
- Keys scroll: arrows and navigation directions inside a scroll
  container move the focus to a near candidate or step a line, as
  Android's ScrollView; Page Up and Down, Home, End and Space step the
  vertical scroll container holding the focus. Whole wheel detents and
  key steps ease out over the scroll rule's time; the rule gains
  `lineStep`, `pageFraction` and `easeNs` (record mui-0007).
- Drags: the `drags` interaction property, the drag threshold
  (`muiSetDragThreshold`), drag start, move and end records with the
  offset from the press, no click after a drag, and cancelling by a
  pointer cancel, a lost capture or Escape (record mui-0007).
- Range values (`maul-ui/range.h`): `muiValueRange` with
  `muiNode_SetValueRange`, `muiNode_GetValueRange`,
  `muiNode_SetRangeValue` and `muiNode_ClearValueRange`; ARIA's slider
  keys, track presses and thumb drags by default;
  `mui_notificationRangeChanged`; `limits.ranges` (record mui-0007).
- Touch scrolling: touch and pen drags pan scroll containers when
  their records are dispatched, and flings follow by iOS's decay; the
  scroll rule gains `decelerationRate` (record mui-0007). With the
  rule's `overscroll`, a pan past a limit rubber bands as on iOS and
  springs back when released, and a fling bounces off a limit.
- Virtual lists (`maul-ui/virtual.h`): `muiNode_SetVirtualList`,
  `muiNode_ClearVirtualList`, `muiNode_GetVirtualWindow`,
  `muiNode_GetVirtualItem`, `muiNode_SetItem` and `muiNode_ClearItem`;
  `mui_notificationWindowChanged`; `limits.virtualLists` and
  `limits.virtualItems`. Items inserted, removed and moved by index
  (`muiNode_InsertVirtualItems`, `muiNode_RemoveVirtualItems`,
  `muiNode_MoveVirtualItem`, `muiNode_GetItem`), bound nodes following
  them, with scroll anchoring (record mui-0007).
- The accessibility tree (`maul-ui/access.h`): roles, texts and flags
  per node (`muiNode_SetAccessRole`, `muiNode_SetAccessText`,
  `muiNode_SetAccessFlags` and their getters), relations
  (`muiNode_SetAccessRelation`, `muiNode_GetAccessRelation`) and typed
  values (`muiAccessValues`, `muiNode_SetAccessValues`,
  `muiNode_GetAccessValues`, `muiDefaultAccessValues`); host content
  read through `muiSetAccessTextFunction`, which the text component's
  `muiAccessTextOf` serves for text blocks; updates in AccessKit's
  shape for enabled roots (`muiAccess_Enable`, `muiAccess_Disable`,
  `muiBuildAccessUpdate`), with bounds, transforms, states, focus,
  scrolling, ranges and virtual list positions read from the library;
  requests performed or routed (`muiPerformAccessAction`, a click as
  `mui_navigateActivate`), `mui_notificationAccessAction`;
  `muiAccessIdOf` and `muiNodeIdOfAccess`. The consumer adapters read
  (`maul-ui/access_tree.h`, `MAUL_UI_ACCESS_TREE`): `muiCreateAccessTree`,
  `muiAccessTree_Apply` with `muiAccessChanges` (refusing lists that
  would not leave a tree; `shownChanged` when the tree as platforms see
  it may differ), the readers, the tree as platforms see it
  (`muiAccessTree_GetShownChildren`, `muiAccessTree_GetShownParent`,
  `muiAccessTree_IsShown`), `muiAccessTree_GetName`,
  `muiAccessTree_GetBounds`, and `muiAccessTree_Write` and
  `muiAccessRoleName`. The UI Automation adapter
  (`maul-ui/access_uia.h`, `MAUL_UI_UIA`, on Windows): `muiCreateUiaAdapter`,
  `muiUiaAdapter_Apply`, `muiUiaAdapter_GetRoot` for Maul Window's
  accessibility root, `muiUiaAdapter_HandleGetObject`,
  `muiUiaAdapter_SetScale`, with a provider object per node giving
  control types, names, descriptions, states, positions, headings,
  landmarks, bounds, navigation, hit testing and focus, and the
  patterns Invoke, Toggle, ExpandCollapse, Value (read; setting text
  comes with text editing), RangeValue, Scroll, ScrollItem and
  SelectionItem, each turned into the host's actions; and events
  raised from what an update changed while a client listens: the
  focus moving, property changes (name, help text, enabled, toggle,
  expand and selection states, values) and live regions. The AT-SPI
  adapter (`maul-ui/access_atspi.h`, `MAUL_UI_ATSPI`, on Linux):
  `muiCreateAtspiApp` joining the accessibility bus and registering
  its root, `muiAtspiApp_GetDescriptor` and `muiAtspiApp_Pump` for the
  host's loop, `muiCreateAtspiAdapter` for each window, with the
  Accessible, Component and Application interfaces: children as shown,
  names, roles, states, parents, extents in screen, window and parent
  pixels, the node under a point, focusing and scrolling; the Action
  interface (click, expand, collapse, increment, decrement, scroll),
  the Value interface (a range's numbers, its value set through the
  host), relations and attributes; events (states, names, values and
  announcements as records change, children added, removed and moved
  as clients were told them, the focus; the shown tree walked only
  when an update may change it). The ARIA adapter
  (`maul-ui/access_aria.h`, `MAUL_UI_ARIA`, for Emscripten):
  `muiCreateAriaAdapter` in an element of the host's over the canvas,
  the shown tree mirrored into invisible elements nested as it is, each
  at its node's box, with ARIA roles, names (labels, or text where ARIA
  names from content), states and values, a range the host sets as an
  `input type=range`; building deferred behind a visually hidden
  enabling button or `muiAriaAdapter_Enable`; `muiAriaAdapter_SetScale`
  for CSS pixels per unit; clients' clicks (or expanding and
  collapsing), focus and range settings asked of the host, the
  program's focus followed from within the elements, relations as
  ARIA id references, live names announced through `ariaNotify` or
  live regions. The NSAccessibility adapter
  (`maul-ui/access_ns.h`, `MAUL_UI_NSACCESSIBILITY`, on macOS):
  `muiCreateNsAdapter` for a view, one object a shown node answering
  the NSAccessibility protocol (parent and children as shown, frames on
  the screen, roles and subroles, titles, values, states, the focused
  node and the node under a point), press, increment, decrement,
  setting a range and the focus asked of the host, the methods a node
  allows answered per node; `muiNsAdapter_GetRoot` for the view;
  notifications (titles and values changed, the focus, elements
  destroyed, the layout, live names announced on the window). The
  UIAccessibility adapter (`maul-ui/access_uikit.h`,
  `MAUL_UI_UIACCESSIBILITY`, on iOS): `muiCreateUikitAdapter` for a
  view, an element a shown node and a container a shown node with
  shown children (its own element first), labels, values, hints,
  traits, container types, frames on the screen, activation,
  increment, decrement, scrolling by direction and VoiceOver's cursor
  asked of the host; `muiUikitAdapter_GetRoot` for the view;
  notifications (a new screen, the layout with the focus when it
  moved, live names announced, queued when polite). The Android
  accessibility adapter (`maul-ui/access_android.h`,
  `MAUL_UI_ANDROID_ACCESSIBILITY`, on Android, with
  `java/maul/ui/AccessProvider.java` for the host to build in):
  `muiCreateAndroidAdapter` for a view, a provider whose virtual views
  are the shown nodes, with class names, texts, content descriptions,
  hints, states, ranges, live settings and bounds on the screen; click,
  focus, scrolling, setting a range, expanding and collapsing asked of
  the host; the screen reader's cursor kept by the provider;
  `virtualViewAt` for touch exploration; events (content changes with
  their types, the subtree, the view focused), sent only while
  accessibility is on. The
  result
  `mui_errorPlatform` (record mui-0008).
- Exit transitions (`maul-ui/exit.h`): `muiNode_BeginExit` and
  `muiNode_CancelExit`; an exiting subtree leaves hit testing, focus
  and navigation, and `mui_notificationExitFinished` reports when no
  transition runs in it; `limits.exits`. The exiting state is now set
  by exits alone: `muiNode_SetStates` keeps it as it is. The
  `exitLayout` interaction property pops an exiting node out of its
  parent's flow at its last rectangle (record mui-0007).
- Popups (`maul-ui/popup.h`): `muiNode_SetPopup`, `muiNode_GetPopup`,
  `muiNode_GetPopupSide` and `muiNode_ClearPopup`; each layout places
  a popup beside its anchor, flipping then clamping into the root's
  box; `limits.popups` (record mui-0007). Light dismissal on presses
  outside, Escape and focus leaving, reported by
  `mui_notificationPopupDismissed` with a `muiDismissReason`; the
  popup's `lightDismiss` turns it off.
- Drag and drop within the application: the `accepts` interaction
  property, `muiPointer_Offer`, and drop enter, leave and drop records
  carrying the kind and the host's key (record mui-0007).
- Font families (`muiCreateFontFamily`, `muiFontFamily_GetKey`,
  `muiFontFamily_MatchFace`): faces a text style names together, matched
  by width, slant and weight as CSS matches them, with fallbacks of
  their own and a `fontFamilies` limit (record mui-0006).
- Editing primitives (`maul-ui/text_edit.h`): hit testing a point to a
  position (a byte offset and an affinity), a position's caret, the
  rectangles a range covers, and moving a position by cluster (in the
  text or on screen), word, line or to the text's ends, over text laid
  out as it is painted; what a deletion either way removes
  (`muiTextBlock_FindDeletion`), replacing and reading a block's text
  (`muiTextBlock_Replace`, `muiTextBlock_GetText`), and input method
  compositions held in a block and underlined by style
  (`muiTextBlock_SetComposition`, `muiTextBlock_EndComposition`,
  `muiTextBlock_GetComposition`) (record mui-0006).
- Filled rectangles from paint functions (`muiDrawSink_AddRect`), as
  box commands snapped to device pixels (record mui-0005).
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
  the library, and Maul Unicode 0.2.1. Damaged fonts and compressed web
  fonts are refused with the new `mui_errorFormat`.

### Fixed

- A password input's value text set by the host is never given to
  assistive technology, which would have read the password through UI
  Automation's Value pattern, AT-SPI and the others; its value is only
  the text service's mask, a bullet a cluster, which a host's own text
  had also kept from being read.

- AT-SPI, as Orca 46 found in a first recorded run: a window's root is
  a frame (unless a dialog), titled by the application when it has no
  name of its own, and told active when it appears (window:activate);
  a root that cannot take focus is not shown focused, so the first
  control focused in it is announced; a switch is a toggle button, as
  AT-SPI before 2.56 has no switch role and showed it as "last
  defined". Orca had stopped following focus in a window it could not
  find.

- A bounded layout lays the nodes that hold out alone outermost first,
  leaving one an outer one's layout reached: a node below a container
  whose direction changed kept the direction it had before. A popped
  exit in a right-to-left container keeps the rectangle it was drawn at
  instead of mirroring it to and fro, which moved it by a rounding step
  on each layout. Two histories of the bounded layout test hold them.

- A scroll container whose children are all removed has the extent of
  its padding box, and its offsets are brought within it; before, it
  kept the extent its children reached. One made without children has
  that extent too, where it had none.

- An estimated virtual list whose items are measured smaller than their
  estimate has the shorter extent at once; before, the extent only grew
  until the list was laid out again, and its offset could stay past
  the end.

- A container sized by its content whose children, or nodes below them,
  take a percentage of their parent's size is as large as they are at
  the size it gets; before, a size it was measured at could be taken
  from its max-content answer, where the percentages counted as
  automatic, so text half its width overflowed it, and which answer
  came first depended on the layouts before.

- A node with an aspect ratio, or with one below it, is laid out the
  same whatever its cache holds: a definite size on one axis, such as a
  stretched cross size, makes the ratio give the other, which a content
  query does not, so a content size no longer answers its exact or
  limited queries, as for percentages.

- A node whose own width or height is a percentage answers a query from
  its cache only for the same parent extents: one asked with none, as an
  aspect ratio leaves its size open, took a size resolved against
  another's.

- A virtual list's extent no longer counts where its bound items were
  placed before: an item popped by its exit, which keeps its rectangle
  through layout, held the extent at its old offset after the list was
  shortened, until the list was laid out again.

- Text held to a maximum width narrower than its line wraps at that
  width and its node is as tall as the lines; before, the node could
  take the height of the one line its max-content measurement gave, so
  the text overflowed it. Any node with a minimum or maximum on an axis
  is measured there again rather than answered from its content size.

- A node popped out of a right-to-left container by its exit stays
  where it was; before, each layout of the container mirrored it to
  the other side and back.

- Text that holds stray UTF-8 continuation bytes treats each as a
  character, the U+FFFD it is drawn as; text starting with one no
  longer takes minutes to keep an editing block's selection within it.
  A drag after the text changed extends from the selection, not from a
  press's word or paragraph in the old text, which put the selection
  past the text's end. Found by the text fuzz target.

- An AT-SPI client's point at the edge of the 32-bit range, in parent
  or screen coordinates, is held by no node; before, its sum with an
  origin overflowed. A node's extents are kept within the range too.
  The registry's answer to Embed is taken only with the desktop's path
  as an object path, never a string, which libdbus would have aborted
  on when the path was later sent. Found by the AT-SPI fuzz target.

- Text in a damaged font whose cmap or substitutions name a glyph past
  the count its maxp gives is drawn with the font's missing glyph;
  before, the glyph image calls refused the glyphs shaping gave. Found
  by the font fuzz target.

- An install without the text component no longer carries
  `text_editor.h`, and the size report counts the editor as text, not
  core (the core read 301% of its ceiling; it is at 93%). The text
  component's public headers are one list, `tools/text-headers.txt`,
  which `tools/check_text_headers.py` holds to the headers declaring a
  function the component's sources define.

- A layer whose parent is painted after it (in a layer above it) or not
  at all (below a node of opacity 0) is drawn through its ancestors'
  transforms, as hit testing finds it, instead of none; a node of
  opacity 0 still gives the layers below it its scroll offset. A scroll
  offset goes through the scale above it.
- `muiDefaultWindowAccessDef` gives the web's adapter its default
  label and deferral, as the header says; the label was NULL, which the
  ARIA adapter refuses, so no access was made on the web.
- The reference renderer no longer hands `qsort` a null array when a
  frame uses no images, undefined even for no elements; the linux-rhi
  cell now runs the renderer's tests and the samples under AddressSanitizer
  and UBSan.
- The samples let go of their surface, renderer, glue and access in
  Maul Window's quit, while the window still exists: the surface was
  destroyed after the window, whose connection the driver's swapchain
  could still use, corrupting the heap in about one run in a hundred.
- `MUI_NODISCARD` is `[[nodiscard]]` under MSVC's C++17 too, which leaves
  `__cplusplus` at 199711L without `/Zc:__cplusplus`; the public headers
  are compiled alone by MSVC in CI. Without the text component, installing
  leaves out its headers, which declared functions the library lacked.
- The text component builds for WASI (wasm32-wasi): it no longer asks
  for a threads library there, where HarfBuzz is built without threads,
  and HarfBuzz keeps `errno`, which wasi-libc declares thread-local.
