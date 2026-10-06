# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Changed

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
  would not leave a tree; `childrenChanged` for a node whose children
  differ, in which or in order), the readers, the tree as platforms see it
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
  when an update may change it). The result
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
  the library, and Maul Unicode 0.2.0. Damaged fonts and compressed web
  fonts are refused with the new `mui_errorFormat`.

### Fixed

- `MUI_NODISCARD` is `[[nodiscard]]` under MSVC's C++17 too, which leaves
  `__cplusplus` at 199711L without `/Zc:__cplusplus`; the public headers
  are compiled alone by MSVC in CI. Without the text component, installing
  leaves out its headers, which declared functions the library lacked.
- The text component builds for WASI (wasm32-wasi): it no longer asks
  for a threads library there, where HarfBuzz is built without threads,
  and HarfBuzz keeps `errno`, which wasi-libc declares thread-local.
