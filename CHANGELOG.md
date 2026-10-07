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
  quit. On the web the
  headless tour runs in headless
  Chrome's WebGPU through the web runner, which now gives Chrome WebGPU
  and a program its arguments (record mui-0005).
- The Maul Window glue, `maul-ui-window` (`MAUL_UI_WINDOW`, off by
  default; `maul-ui-window/glue.h`), with Maul Window 0.8.0: a glue a
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
  0.3.0: `muiCreateRhiRenderer` for a device and a target format,
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
  uploads would not fit the def's `uploadBytes` (the device's
  `frameUploadBytes`) is refused with `mui_errorCapacity`, nothing
  added, and `muiRhiRenderer_Forget` makes the next frame upload all
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
  the library, and Maul Unicode 0.2.0. Damaged fonts and compressed web
  fonts are refused with the new `mui_errorFormat`.

### Fixed

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
