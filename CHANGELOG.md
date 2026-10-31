# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every data format.

## [0.6.1](https://github.com/Rawframe-Project/maul-ui/compare/v0.6.0...v0.6.1) (2026-10-31)


### Fixed

* **virtual:** set a measured item's sums from their parts ([#232](https://github.com/Rawframe-Project/maul-ui/issues/232)) ([8f6e7c9](https://github.com/Rawframe-Project/maul-ui/commit/8f6e7c9d121358c83b23eee582721fe81021312a)), closes [#231](https://github.com/Rawframe-Project/maul-ui/issues/231)

## [0.6.0](https://github.com/Rawframe-Project/maul-ui/compare/v0.5.0...v0.6.0) (2026-10-31)

This release finishes the review of Maul UI's API before 1.0.0
([#199](https://github.com/Rawframe-Project/maul-ui/issues/199)) and
builds against Maul RHI 0.15.0. A program written for 0.5 has to
rebuild against the new headers, rename the counters listed below, read
a notification's typed fields instead of its `detail`, and give a
string's length in event and request records as `size_t`. Typing,
pasting and deleting beside a composition, and edits that run out of
memory, no longer lose text or undo history. Layouts, draw lists,
shaped text and glyph images are the same as in 0.5.0.

### Changed

- **Breaking:** a string's byte length is `size_t` in
  `muiTextEvent.length`, `muiEvent.length` and
  `muiAccessRequest.length`, as it already was in every parameter.
  Code that fills or reads these fields as `uint32_t` has to use
  `size_t`. On 64-bit, `muiTextEvent` grows from 24 to 32 bytes and
  `muiEvent` from 72 to 80, with `length` now right after `text`;
  `muiAccessRequest` keeps its 56. Offsets and ranges inside a text stay
  `uint32_t`. `muiNode_RouteText` refuses a text longer than
  `MUI_MAX_TEXT_BYTES` with `mui_errorLimit`, and the text guide states
  the rule
  ([#203](https://github.com/Rawframe-Project/maul-ui/pull/203)).
- **Breaking:** `muiNotification.detail` is replaced by four typed
  fields: `notificationsDropped` for `mui_notificationDropped`, `player`
  (`uint8_t`) for the focus kinds, `reason` (`muiDismissReason`) for
  `mui_notificationPopupDismissed` and `action` (`muiAccessAction`) for
  `mui_notificationAccessAction`. Each is 0 for the other kinds. Code
  that read `detail` reads the field of its kind, with no cast.
  `muiDismissReason` and `muiAccessAction` are now declared in
  `maul-ui/context.h`; their values stay in `maul-ui/popup.h` and
  `maul-ui/access.h`. The record grows from 16 to 28 bytes, so a
  context takes 12 bytes more for each notification its limit allows
  ([#225](https://github.com/Rawframe-Project/maul-ui/pull/225),
  [#230](https://github.com/Rawframe-Project/maul-ui/pull/230)).
- **Breaking:** counters are named for what they count.
  `muiTextService_GetFailures` is now
  `muiTextService_GetLayoutFailureCount`, and `muiVirtualList.count` is
  now `itemCount`. What each counts is the same
  ([#212](https://github.com/Rawframe-Project/maul-ui/pull/212)).
- **Breaking:** `muiDrawGradient.kind` is a `muiGradientKind`, an
  `int32_t`, instead of a `uint32_t`. Code that stores or compares it as
  `uint32_t` takes the type instead. The record keeps its 96 bytes and
  its layout, and draw lists have the same bytes
  ([#216](https://github.com/Rawframe-Project/maul-ui/pull/216)).
- **Breaking:** the renderer builds against Maul RHI 0.15.0. A host that
  hands the renderer its device, or builds Maul UI against its own copy
  of Maul RHI, has to use that release. Maul RHI's own changelog says
  what changed in its API. The renderer's shaders and pixels are the
  same
  ([#211](https://github.com/Rawframe-Project/maul-ui/pull/211)).
- `muiDrawSink_GetVisibleRect` takes a `const muiDrawSink*`, so a paint
  function holding a const sink can call it without a cast. Callers
  with a non-const sink compile unchanged
  ([#222](https://github.com/Rawframe-Project/maul-ui/pull/222)).
- The kinds a drag offers and a node accepts have a type of their own,
  `muiDropKinds`, declared in `maul-ui/pointer.h` and used by
  `muiInteractionStyle.accepts`, `muiContext_OfferDrop` and
  `muiPointerRecord.dropKind`. It is still `uint32_t`, so no caller
  changes
  ([#223](https://github.com/Rawframe-Project/maul-ui/pull/223)).

### Fixed

- Text component: `muiTextBlock_Type` and `muiTextBlock_Paste` read a
  text past `MUI_MAX_TEXT_BYTES`, and grew the service's scratch to its
  size, before they refused it; a string shorter than the length given
  with it was read past its end. Every way a text comes in now refuses
  such a length with `mui_errorLimit` before reading a byte. A
  `muiTextBlock_Compose` refused for its segments, its length or its
  fit, or for want of memory, took the selection out anyway; it now
  keeps the text, the selection and the undo history. A composition
  over a selection goes at the selection's start
  ([#202](https://github.com/Rawframe-Project/maul-ui/pull/202)).
- Text component: `muiNode_HandleEditEvent` read through a NULL pointer
  for a text event with no text and a nonzero length. It now answers
  `mui_errorInvalid` and counts one misuse
  ([#210](https://github.com/Rawframe-Project/maul-ui/pull/210)).
- Text component: an edit that ran out of memory emptied the block's
  whole undo history. It now answers `mui_errorCapacity` with the text,
  the selection and the history as they were
  ([#215](https://github.com/Rawframe-Project/maul-ui/pull/215)).
- Text component: typing, pasting or deleting while a composition was
  shown took the composition out as a change of its own first, so a
  call then refused for its limit or for want of memory lost the
  composition's text. The composition now goes in the same change as
  the edit, and a refused edit leaves it shown
  ([#220](https://github.com/Rawframe-Project/maul-ui/pull/220),
  [#226](https://github.com/Rawframe-Project/maul-ui/pull/226)).
- `muiDrawSink_GetVisibleRect` with a NULL output on a live sink counted
  no misuse, while the other sink calls count one for a bad argument. It
  now counts one on the sink's context
  ([#228](https://github.com/Rawframe-Project/maul-ui/pull/228)).

## [0.5.0](https://github.com/Rawframe-Project/maul-ui/compare/v0.4.0...v0.5.0) (2026-10-31)

Maul UI now follows the Maul family's rules for enumeration types,
counter names and the outputs of a call that does not succeed, and
builds against Maul RHI 0.14.0. A program written for 0.4 has to
rebuild against the new headers, rename the counters listed below, and
stop reading the part of an answer a call wrote before it failed.
Layouts, draw lists, shaped text and glyph images have the same content
as in 0.4.0, but a draw command's image record lays out its repeat
fields differently.

### Changed

- **Breaking:** every public enumeration type is `int32_t` for a kind,
  mode or choice, and `uint32_t` for a set of bits. Before, most were
  `uint8_t` or `uint16_t`, so the structs that hold them change layout:
  among others `muiLayoutStyle`, `muiVisualStyle`, `muiTextStyle`,
  `muiEvent`, `muiPointerRecord`, `muiAccessNode`, `muiCondition`,
  `muiEnvironment` and `muiTextEditRules` grow. `muiDrawCommand` keeps
  its 152 bytes, but an image command's `repeatX` is four bytes, so its
  `repeatY` moves from offset 89 to 92. `muiPropertyMask` stays
  `uint64_t`. Every check of a kind refuses -1 and `INT32_MIN` as well
  as values past the last. `muiNode_RouteKey` refuses a key code above
  0xFFFF, and `muiNode_MoveTextPosition` refuses a starting position
  whose affinity is neither value
  ([#192](https://github.com/Rawframe-Project/maul-ui/pull/192)).
- **Breaking:** counters are named for what they count.
  `muiWorkCounts`' `styled`, `sized`, `measured` and `painted` are now
  `styleCount`, `sizeCount`, `measureCount` and `paintCount`, and
  `muiPointerRecord.dropped` is now `recordsDropped`. What each counts
  is the same
  ([#193](https://github.com/Rawframe-Project/maul-ui/pull/193)).
- **Breaking:** a call whose first parameter is an owner object now
  zeroes the caller's array or buffer over its whole capacity whenever
  it answers anything but `mui_success` or `mui_errorBusy`: a failure,
  `mui_empty` and a capacity failure alike. Before, it left the array as
  it was, or wrote the part of the answer that fit. This covers
  `muiNode_GetAccessRelation`, `muiAccessTree_GetShownChildren`,
  `muiAccessTree_GetName`, `muiAccessTree_GetTextRects`,
  `muiAccessTree_Write`, the four `muiTextService_Render` calls and
  `muiGlyphAtlas_TakeUpdates`. After a capacity failure, ask again with
  the count or length the call gives
  ([#166](https://github.com/Rawframe-Project/maul-ui/pull/166)).
- **Breaking:** a call refused with `mui_errorBusy`, because host code
  its owner was running made it, writes none of its outputs, single
  values and buffers alike. Before, it cleared them, though the call in
  progress may have been filling them: for example an allocator calling
  `muiTextBlock_Undo` while text was typed set the outer call's
  `changed` to false. A creation still clears the id it gives on any
  failure. A program that read a cleared output after a busy refusal
  now reads what was there before the call
  ([#173](https://github.com/Rawframe-Project/maul-ui/pull/173)).
- **Breaking:** the renderer builds against Maul RHI 0.14.0. A host that
  hands the renderer its device, or builds Maul UI against its own copy
  of Maul RHI, has to use that release. Maul RHI's own changelog says
  what changed in its API. The renderer's shaders and pixels are the
  same
  ([#195](https://github.com/Rawframe-Project/maul-ui/pull/195)).
- The window glue: at a Maul Window input reset of the window that holds
  a field, a host now takes the field's composition out with an empty
  preedit (`muiTextBlock_ShowPreedit`), as Maul Window asks since its
  0.6.0. A reset of another window, or a gamepad's, leaves it. The glue
  cannot do this itself, because the host hands preedits to its field.
  The headers, the user guide and the text sample say and do so
  ([#161](https://github.com/Rawframe-Project/maul-ui/pull/161)).

### Fixed

- Text component: `muiDestroyFont`, `muiDestroyFontFamily` and
  `muiDestroyTextBlock` freed the object's memory before its id went
  stale. An allocator's free that read the object back could read freed
  memory: `muiFontFamily_MatchFace` on the family being destroyed read
  its freed faces, and `muiTextBlock_GetText` answered a pointer into
  freed text. The id is now stale before any free runs, and such a call
  answers `mui_errorStale`
  ([#189](https://github.com/Rawframe-Project/maul-ui/pull/189)).
- Samples: a window's surface took the first 8-bit color in Rec. 709 it
  listed, whatever its transfer and range, so on a display whose driver
  lists a linear or extended color first the samples showed wrong
  colors. They now ask Maul RHI for 8-bit sRGB
  (`mrhiSuggestSurfaceColor`)
  ([#197](https://github.com/Rawframe-Project/maul-ui/pull/197)).

## [0.4.0](https://github.com/Rawframe-Project/maul-ui/compare/v0.3.3...v0.4.0) (2026-10-30)

Maul UI now follows the Maul family's rules for names, results and
outputs, and builds against the latest Maul Tween, Maul Unicode, Maul
RHI and Maul Window. A program written for 0.3 has to change the names
and result checks listed under Changed and Removed. Apart from the
fixes below, layouts, shaped text and glyph images are the same as in
0.3.3. Draw lists have the same content, but a box record is now 132
bytes.

### Changed

- **Breaking:** public functions are renamed by the family's naming
  rule. A call on one object is now `muiObject_Verb`, where the object
  is the kind of the call's first id, or the context or service the call
  acts on. Creation, defaults and calls that act on no object keep
  `muiVerbNoun`. Parameters and behaviour are the same unless noted.
  `muiNode_CapturePointer` takes the node before the pointer, and
  `muiNode_EnableAccess` takes a bool in place of the old enable and
  disable pair
  ([#149](https://github.com/Rawframe-Project/maul-ui/pull/149)). Every
  new name in this release, with those of the changes below:

  | 0.3 | 0.4 |
  |---|---|
  | `muiResultName` | `muiGetResultName` |
  | `muiAccessRoleName` | `muiGetAccessRoleName` |
  | `muiNodeIdOfAccess` | `muiGetNodeOfAccessId` |
  | `muiAccessIdOf` | `muiNode_GetAccessId` |
  | `muiGetContextMisuse` | `muiContext_GetMisuse` |
  | `muiGetWorkCounts` | `muiContext_GetWorkCounts` |
  | `muiNextNotification` | `muiContext_TakeNotification` |
  | `muiNextPointerRecord` | `muiContext_TakePointerRecord` |
  | `muiDispatchPointerRecord` | `muiContext_DispatchPointerRecord` |
  | `muiPerformAccessAction` | `muiContext_PerformAccessAction` |
  | `muiGetDrawList` | `muiContext_GetDrawList` |
  | `muiSetClickRule` | `muiContext_SetClickRule` |
  | `muiSetDragThreshold` | `muiContext_SetDragThreshold` |
  | `muiSetScrollRule` | `muiContext_SetScrollRule` |
  | `muiSetContextEnvironment` | `muiContext_SetEnvironment` |
  | `muiGetContextEnvironment` | `muiContext_GetEnvironment` |
  | `muiSetAccessTextFunction` | `muiContext_SetAccessTextFunction` |
  | `muiSetEventFunction` | `muiContext_SetEventFunction` |
  | `muiFocus_Set` | `muiContext_SetFocus` |
  | `muiFocus_Get` | `muiContext_GetFocus` |
  | `muiPointer_ReleaseCapture` | `muiContext_ReleasePointerCapture` |
  | `muiPointer_GetState` | `muiContext_GetPointerState` |
  | `muiPointer_Offer` | `muiContext_OfferDrop` |
  | `muiPointer_SetCapture(context, pointer, nodeId)` | `muiNode_CapturePointer(context, nodeId, pointer)` |
  | `muiComputeLayout` | `muiNode_ComputeLayout` |
  | `muiIsUpdatePending` | `muiNode_IsUpdatePending` |
  | `muiBuildDrawList` | `muiNode_BuildDrawList` |
  | `muiBuildAccessUpdate` | `muiNode_BuildAccessUpdate` |
  | `muiHitTest` | `muiNode_HitTest` |
  | `muiFocus_Move` | `muiNode_MoveFocus` |
  | `muiFocus_MoveToward` | `muiNode_MoveFocusToward` |
  | `muiKeyInput` | `muiNode_RouteKey` |
  | `muiTextInput` | `muiNode_RouteText` |
  | `muiNavigationInput` | `muiNode_RouteNavigation` |
  | `muiWheelInput` | `muiNode_RouteWheel` |
  | `muiPointerInput` | `muiNode_RoutePointer` |
  | `muiAccess_Enable(context, rootId)` | `muiNode_EnableAccess(context, rootId, true)` |
  | `muiAccess_Disable(context, rootId)` | `muiNode_EnableAccess(context, rootId, false)` |
  | `muiNode_SetItem` | `muiNode_BindVirtualItem` |
  | `muiNode_ClearItem` | `muiNode_UnbindVirtualItem` |
  | `muiNode_GetItem` | `muiNode_GetBoundVirtualItem` |
  | `muiSetTokenValue` | `muiToken_SetValue` |
  | `muiSetTokenAlias` | `muiToken_SetAlias` |
  | `muiGetTokenValue` | `muiToken_GetValue` |
  | `muiTextHitTest` | `muiNode_HitTestText` |
  | `muiTextGetCaret` | `muiNode_GetTextCaret` |
  | `muiTextGetRangeRects` | `muiNode_GetTextRangeRects` |
  | `muiTextMove` | `muiNode_MoveTextPosition` |
  | `muiTextEditEvent` | `muiNode_HandleEditEvent` |
  | `muiTextEditMove` | `muiNode_MoveEditCaret` |
  | `muiTextEditPress` | `muiNode_HandleEditPress` |
  | `muiTextEditDrag` | `muiNode_HandleEditDrag` |
  | `muiTextPerformAccessAction` | `muiPerformTextAccessAction` |
  | `muiTextEditDef`, `muiDefaultTextEditDef` | `muiTextEditRules`, `muiDefaultTextEditRules` |
  | `muiTextBaseline` | `muiMeasureTextBaseline` |
  | `muiAccessTextOf` | `muiReadAccessText` |
  | `muiGetTextServiceMisuse` | `muiTextService_GetMisuse` |
  | `muiGetTextServiceFailures` | `muiTextService_GetFailures` |
  | `muiSetDefaultFont` | `muiTextService_SetDefaultFont` |
  | `muiSetFallbackFonts` | `muiTextService_SetFallbackFonts` |
  | `muiRenderGlyph` | `muiTextService_RenderGlyph` |
  | `muiRenderColorGlyph` | `muiTextService_RenderColorGlyph` |
  | `muiRenderGlyphField` | `muiTextService_RenderGlyphField` |
  | `muiRenderGlyphMultiField` | `muiTextService_RenderGlyphMultiField` |
  | `muiGlyphAtlas_Get` | `muiGlyphAtlas_GetGlyph` |
  | `muiGlyphAtlas_GetColor` | `muiGlyphAtlas_GetColorGlyph` |
  | `muiGlyphAtlas_GetField` | `muiGlyphAtlas_GetGlyphField` |
  | `muiGlyphAtlas_GetMultiField` | `muiGlyphAtlas_GetGlyphMultiField` |
  | `muiGlyphAtlas_NextFrame` | `muiGlyphAtlas_BeginFrame` |
  | `muiGetAccessTreeMisuse` | `muiAccessTree_GetMisuse` |
  | `muiAccessTree_Count` | `muiAccessTree_GetNodeCount` |
  | `muiGetAndroidAdapterMisuse` | `muiAndroidAdapter_GetMisuse` |
  | `muiGetAriaAdapterMisuse` | `muiAriaAdapter_GetMisuse` |
  | `muiGetAtspiAppMisuse` | `muiAtspiApp_GetMisuse` |
  | `muiGetNsAdapterMisuse` | `muiNsAdapter_GetMisuse` |
  | `muiGetUiaAdapterMisuse` | `muiUiaAdapter_GetMisuse` |
  | `muiGetUikitAdapterMisuse` | `muiUikitAdapter_GetMisuse` |
  | `muiAriaActionFunction`, `muiAtspiActionFunction`, `muiAndroidActionFunction`, `muiNsActionFunction`, `muiUiaActionFunction`, `muiUikitActionFunction`, `muiWindowAccessActionFunction` | `muiAccessActionFunction` |
  | `muiRangeAxis`, `muiListAxis` (`mui_rangeHorizontal`, `mui_listVertical`, ...) | `muiAxis` (`mui_axisHorizontal`, `mui_axisVertical`) |
  | `muiTextDirection` (`mui_textInherit`, `mui_textLeftToRight`, `mui_textRightToLeft`), `muiDirectionMatch` (`mui_directionAny`, ...) | `muiInlineDirection` (`mui_inlineInherit`, `mui_inlineLeftToRight`, `mui_inlineRightToLeft`) |
  | `mui_popupNone`, `mui_popupMenu`, `mui_popupListBox`, `mui_popupTree`, `mui_popupGrid`, `mui_popupDialog` (aria-haspopup) | `mui_hasPopupNone`, `mui_hasPopupMenu`, `mui_hasPopupListBox`, `mui_hasPopupTree`, `mui_hasPopupGrid`, `mui_hasPopupDialog` |
  | `muiRange` (a condition's interval) | `muiInterval` |
  | `muiWindowSetComposition` | `muiTextBlock_SetPreedit` |
  | `muiWindowCompose` | `muiTextBlock_ShowPreedit` |
  | `muiWindowGlue_Paste` | `muiTextBlock_PasteFromWindow` |
  | `muiWindowGlue_WriteClipboard` | `muiWriteWindowClipboard` |
  | `muiWindowGlue_SetCaret` | `muiNode_SetWindowCaret` |
  | `muiWindowGlue_SetTextCaret` | `muiNode_SetWindowTextCaret` |
  | `muiWindowGlue_RequestKeyboard` | `muiNode_RequestWindowKeyboard` |
  | `muiWindowAccess_HasAdapter` | `muiWindowAccess_IsAdapterCreated` |

- **Breaking:** fields renamed. `muiWindowGlueDef.gamepads` is now
  `enableGamepads`
  ([#149](https://github.com/Rawframe-Project/maul-ui/pull/149)).
  `muiRhiRendererDef.imageContext` is now `imageUser`,
  `muiWindowGlueDef.gamepadContext` is now `gamepadUser`, and
  `muiLayoutInput.measureUser` is now `user`, which goes to both the
  measure and the baseline functions
  ([#104](https://github.com/Rawframe-Project/maul-ui/pull/104)).
  `muiNotification.count` is now `detail`. A dropped pointer record
  gives its count in the new `muiPointerRecord.dropped`, and its
  `clickCount` is 0
  ([#108](https://github.com/Rawframe-Project/maul-ui/pull/108)).
- **Breaking:** a value range's and a virtual list's `axis` take
  `muiAxis`. `muiNode_GetScrollThumb` takes a `muiAxis` in place of
  `bool horizontal`, and refuses an unknown axis. A layout style's
  `textDirection` and a condition's `direction` take
  `muiInlineDirection`. For a condition, inherit holds for either
  direction, as "any" did
  ([#95](https://github.com/Rawframe-Project/maul-ui/pull/95)).
- **Breaking:** results take the family's values. `mui_errorFormat` is
  now -8, `mui_errorVersion` is -7, and `mui_errorState` is -6. What
  `mui_errorState` used to mean, host code calling back into the object
  it serves, is now `mui_errorBusy` (-10). `mui_errorState` now answers
  the editor's calls on a block that is not editing, which used to be
  `mui_errorInvalid`, and counts no misuse. `mui_errorUnsupported` (-4)
  is new. It answers an atlas getter on an atlas of another format,
  which used to be `mui_errorInvalid`, and a WOFF or WOFF2 font, which
  used to be `mui_errorFormat`. `mui_errorLimit` (-9) is new too. It
  answers a count or size past a published bound, which used to be
  `mui_errorInvalid`, and counts one misuse. Each such bound is now a
  `MUI_MAX_` or `MUI_MIN_` constant, and a context limit can be at most
  2^31 - 2
  ([#126](https://github.com/Rawframe-Project/maul-ui/pull/126)).
- **Breaking:** a call whose first parameter is a context, text service,
  access tree, glyph atlas, draw sink, adapter, AT-SPI application or
  window glue now clears its outputs when it fails, before checking
  anything: zeros, null ids, NULL, false or 0. It also leaves zeros with
  `mui_empty`, unless its header says otherwise. A count, length or
  image size that a capacity failure reports is still given. The class
  getters copy their result only on success. Each output's documentation
  says what a failure leaves
  ([#87](https://github.com/Rawframe-Project/maul-ui/pull/87)).
- **Breaking:** `muiTextBlock_SetEditing` refuses NULL rules with
  `mui_errorInvalid`. End editing with the new
  `muiTextBlock_ClearEditing`
  ([#150](https://github.com/Rawframe-Project/maul-ui/pull/150)).
- **Breaking:** a measure function's NaN width or height, and a baseline
  function's +infinity, now mean content that could not be measured.
  They used to be read as 0 and as no baseline. `muiNode_ComputeLayout`
  lays such content out as empty, measures it again on the next layout,
  and answers `mui_errorCapacity`. A paint function that could not paint
  calls the new `muiDrawSink_Fail`, so the node is painted again on the
  next build, which answers `mui_errorCapacity`. The text component does
  both when memory runs out
  ([#136](https://github.com/Rawframe-Project/maul-ui/pull/136)).
- **Breaking:** while a text call given a `muiTextHost` runs, host code
  it calls (the service's allocator, FreeType's memory through it, the
  clipboard writer) may read the host's context but not change it. A
  change answers `mui_errorBusy` and counts one misuse, and
  `muiDestroyContext` does nothing
  ([#130](https://github.com/Rawframe-Project/maul-ui/pull/130)).
- **Breaking:** style: `muiState` is a `uint16_t`, so eight states can
  be added later without moving anything. The conditions' variants start
  at 17 (`mui_variantCondition0`), and variants 9 to 16 are refused.
  `muiNode_SetStates` refuses a bit that names no state
  ([#91](https://github.com/Rawframe-Project/maul-ui/pull/91)).
- **Breaking:** draw list: the records lose their reserved fields, so a
  box record is 132 bytes. The other records keep their sizes
  ([#122](https://github.com/Rawframe-Project/maul-ui/pull/122)).
- **Breaking:** glyphs: `muiAtlasFormat` is a `uint8_t`, so a glyph
  atlas def's and an atlas page's `format` are one byte. The constants
  keep their names and values
  ([#88](https://github.com/Rawframe-Project/maul-ui/pull/88)).
- **Breaking:** the dependencies move to Maul Tween 0.12.0, Maul Unicode
  0.8.0, Maul RHI 0.13.0 and Maul Window 0.7.0. A program that builds
  Maul UI against its own copy of one of them has to provide the same
  release. Each one's own changelog says what changed in its API
  ([#152](https://github.com/Rawframe-Project/maul-ui/pull/152),
  [#155](https://github.com/Rawframe-Project/maul-ui/pull/155),
  [#156](https://github.com/Rawframe-Project/maul-ui/pull/156)).
  - The renderer: a host that hands the renderer its device uses Maul
    RHI 0.13.0. Its own shader containers have to be version 2, and
    pipeline caches written by an older Maul RHI are refused. New
    buffers and textures read as zeros in the first frame that uses
    them, cleared in a pass of Maul RHI's own, so the host's pass ids
    start at 2
    ([#109](https://github.com/Rawframe-Project/maul-ui/pull/109)). On
    D3D12, the quad shader's float math now runs as written, as it does
    on the other APIs, so a pixel can differ from 0.3 by rounding
    ([#156](https://github.com/Rawframe-Project/maul-ui/pull/156)).
  - The window glue: configuring stops, with a message naming both
    releases, when the Maul Unicode release that Maul Window names
    differs from Maul UI's
    ([#155](https://github.com/Rawframe-Project/maul-ui/pull/155)).
- The library is smaller on WebAssembly, where entering the float
  environment now costs nothing
  ([#115](https://github.com/Rawframe-Project/maul-ui/pull/115)). On
  32-bit x86 it is built with SSE math, as the rest of the family is
  ([#69](https://github.com/Rawframe-Project/maul-ui/pull/69)).

### Added

- `muiTextBlock_ClearEditing` ends a block's editing
  ([#150](https://github.com/Rawframe-Project/maul-ui/pull/150)).
- `muiNode_IsAccessEnabled` tells whether a root's accessibility is on
  ([#149](https://github.com/Rawframe-Project/maul-ui/pull/149)).
- `muiDrawSink_Fail` lets a paint function report that it could not
  paint ([#136](https://github.com/Rawframe-Project/maul-ui/pull/136)).
- `mui_errorUnsupported`, `mui_errorLimit` and the `MUI_MAX_` and
  `MUI_MIN_` bounds they answer for
  ([#126](https://github.com/Rawframe-Project/maul-ui/pull/126)).

### Fixed

- Accessibility, macOS: with VoiceOver on, Tab could move the focus back
  to the control it had just left, or stop moving it. A focus move
  between nodes that are shown anyway no longer reports a shown change,
  so VoiceOver is no longer told that the layout changed. Android sends
  no subtree change for such a move, as Android's own views do
  ([#121](https://github.com/Rawframe-Project/maul-ui/pull/121),
  [#142](https://github.com/Rawframe-Project/maul-ui/pull/142)).
- Text component: a host's allocator that destroyed or changed the
  context during a text call (a hit test, caret, range or editor call)
  could make the call read freed memory
  ([#130](https://github.com/Rawframe-Project/maul-ui/pull/130)).
- Glyphs: a colour font with a reversed COLR clip box, or with a CBDT or
  sbix bitmap of 2^28 pixels or more, could make
  `muiTextService_RenderColorGlyph` and a colour atlas write past their
  memory. Such a glyph is now refused with `mui_errorFormat`, and the
  font's other glyphs draw as before. The PNG decoder counts its sizes
  in 64 bits
  ([#120](https://github.com/Rawframe-Project/maul-ui/pull/120)).
- Text component: when memory runs out, an editing field scrolled to its
  caret answered hit tests, carets and range rectangles for the
  unscrolled text. These calls now answer `mui_errorCapacity`. A replace
  from a screen reader whose paste failed left the selection moved; it
  now restores the selection
  ([#135](https://github.com/Rawframe-Project/maul-ui/pull/135)).
- Text component: text that could not be measured or painted for lack of
  memory stayed empty until the host marked it changed. Now it is
  measured and painted again on the next frame
  ([#136](https://github.com/Rawframe-Project/maul-ui/pull/136)).
- Editing: after an edit took out a composition whose two sides join
  into one character (ill-formed UTF-8), the caret could land inside
  that character. It now goes to the next character start
  ([#97](https://github.com/Rawframe-Project/maul-ui/pull/97)).
- Layout: a flex column with an aspect ratio, stretched in a single-line
  row of definite height, resolved its children's percentage heights
  against its ratio's height instead of its stretched height, as Chrome
  and Firefox do
  ([#128](https://github.com/Rawframe-Project/maul-ui/pull/128)).

### Removed

- `muiNode_SetLayoutStyle`. Call `muiNode_SetLayoutValues` with
  `MUI_LAYOUT_PROPERTIES`
  ([#108](https://github.com/Rawframe-Project/maul-ui/pull/108)).
- `MUI_MAX_DRAW_STOPS`. Use `MUI_MAX_GRADIENT_STOPS`, which is also 4
  ([#108](https://github.com/Rawframe-Project/maul-ui/pull/108)).

## [0.3.3](https://github.com/Rawframe-Project/maul-ui/releases/tag/v0.3.3) (2026-10-25)

### Fixed

- A fling's decay rate comes from Maul Tween's
  `mtwnMakeDecayFromDeceleration`, whose logarithm is correctly rounded
  more often than maul-ui's own. The rate changes in its last bit for
  about 0.9% of deceleration rates; the default, 0.998, is unchanged,
  and iOS's "fast" 0.99 moves by one ulp. No fling or drag output moved
  in a grid of 60,047 fling frames and 23,780 drags at 0.998 and 0.99,
  nor in 320 million sampled offsets at 0.99, but an offset within
  about 1e-16 of a float rounding boundary may still round differently
  ([#66](https://github.com/Rawframe-Project/maul-ui/pull/66)).

## [0.3.2](https://github.com/Rawframe-Project/maul-ui/releases/tag/v0.3.2) (2026-10-25)

### Fixed

- With overscroll on, a pan that catches the children at or past the
  scrollport's extent beyond a limit goes on from where they are. The
  pan used to start at the limit, so the children jumped back by the
  whole overscroll on the first move: 201 units under the finger after
  a fast fling in the reproduction in
  [#64](https://github.com/Rawframe-Project/maul-ui/issues/64). Such a pan now takes a band twice as wide
  as the overscroll it caught, for that pan only. Twice is the
  library's own choice, and what iOS does here is unverified. Below the
  scrollport's extent nothing changes, and the spring back after
  release is unchanged
  ([#63](https://github.com/Rawframe-Project/maul-ui/pull/63)).

### Changed

- The text component's wasm is 557,106 bytes, from 615,064 (87% of its
  640,000 budget, from 96%). FreeType is built without the Adobe Glyph
  List, which only makes a Unicode charmap from glyph names: the text
  component maps characters through HarfBuzz and calls no FreeType
  charmap or glyph-name function. The bytecode interpreter stays,
  since FreeType runs it for tricky fonts. Results do not change
  ([#62](https://github.com/Rawframe-Project/maul-ui/pull/62)).

## [0.3.1](https://github.com/Rawframe-Project/maul-ui/releases/tag/v0.3.1) (2026-10-25)

### Fixed

- On Windows, a program using maul-ui-window now times flings, double
  clicks and wheel latching from when the input happened, as on every
  other platform. Input that waited behind a long frame or a modal loop
  was timed by that wait: a fling could take the wrong velocity, two
  clicks could be counted wrongly as a double click or not, and a wheel
  turn could latch to the wrong scroll container. maul-ui-window now
  takes Maul Window 0.4.0, which stamps Windows input with the time it
  happened. Gamepad repeat is unchanged.
  ([#59](https://github.com/Rawframe-Project/maul-ui/pull/59))

### Changed

- The reference renderer takes Maul RHI 0.3.0 (from 0.0.1). Drawing is
  unchanged on Vulkan. On D3D12 the renderer's upload can now run on
  the copy queue, with the drawing waiting for it. Maul RHI's changelog
  lists what else changes for a program that also calls it directly.
  ([#58](https://github.com/Rawframe-Project/maul-ui/pull/58))
- The core takes Maul Tween 0.2.1 (from 0.1.0) and the text component
  Maul Unicode 0.1.0 (from 0.0.1). Results are unchanged. In wasm the
  core grows by about 0.5 KB and the text component by 0.3 KB.
  ([#56](https://github.com/Rawframe-Project/maul-ui/pull/56),
  [#57](https://github.com/Rawframe-Project/maul-ui/pull/57))

## [0.3.0](https://github.com/Rawframe-Project/maul-ui/releases/tag/v0.3.0) (2026-10-25)

### Changed

- **Breaking:** with overscroll on, a touch pan past a limit moves the
  children by Maul Tween's rubber band, iOS's `c x d / (c x + d)`
  computed in double and rounded once, in place of Maul UI's own float
  form. During such a drag the overscroll offset, and so the drawn
  transform and the draw list's bytes, can change in the last bits: by
  at most 2.4e-4 units, which no screen shows, and toward the exact
  value near the limit, where the float form lost precision. Springing
  back, flings and scrolling without overscroll are unchanged.
  ([#50](https://github.com/Rawframe-Project/maul-ui/pull/50))
- Flings and the spring back from overscroll come from Maul Tween's
  decay and spring. On every input measured they give the same bits as
  before; elsewhere a value can rarely differ by one float step.
  ([#49](https://github.com/Rawframe-Project/maul-ui/pull/49))

## [0.2.1](https://github.com/Rawframe-Project/maul-ui/releases/tag/v0.2.1) (2026-10-25)

### Fixed

- With overscroll on, a touch fling no longer throws the content far
  past a limit that layout moved behind where the fling began, when the
  fling heads back toward it. A fling up at 1,000 units a second from
  357, whose limit fell from 400 to 100, drew the content about 20,650
  units past the new limit and brought it back over about 400 ms. Such a
  fling now stops at the new limit, as one moving away from it already
  did. Other flings, and all scrolling without overscroll, are
  unchanged.
  ([#47](https://github.com/Rawframe-Project/maul-ui/pull/47))

## [0.2.0](https://github.com/Rawframe-Project/maul-ui/releases/tag/v0.2.0) (2026-10-24)

### Changed

- Easing curves and springs come from Maul Tween, the family's
  animation library, in place of Maul UI's own. Maul UI now fetches
  Maul Tween 0.1.0 and links it. A timed transition with an easing
  keyword or a cubic Bezier, and rarely a spring, can give a value one
  float step from what 0.1.0 gave, toward the exact curve: about 1% of
  values on a curve and about 3 in 100,000 on a spring. Linear
  transitions, and the frame on which a spring comes to rest, are
  unchanged.
  ([#37](https://github.com/Rawframe-Project/maul-ui/pull/37))

### Fixed

- A host that runs its threads with flush-to-zero, denormals-are-zero
  or another rounding mode now gets the same layout, drawing, hit
  testing, accessibility and text results as every other host. Every
  call that computes (public functions, and the calls platforms make
  into the accessibility adapters) switches to the library's
  floating-point setting and gives the thread's back on return.
  Functions the host passes (measure, baseline, paint, access text,
  events, the clipboard writer, the allocator and the adapters' action
  functions) run in the host's own setting. Results in the default
  setting do not change.
  ([#42](https://github.com/Rawframe-Project/maul-ui/pull/42))
- Ill-formed UTF-8 is shaped the way the rest of the text component
  reads it: each maximal ill-formed subpart becomes one U+FFFD, so caret
  moves and hit tests no longer land inside a character.
  ([#28](https://github.com/Rawframe-Project/maul-ui/pull/28))
- An edit whose bytes join the characters beside them no longer leaves
  the caret inside the joined character, where the next keystroke split
  it. A composition's caret at its end is checked too.
  ([#29](https://github.com/Rawframe-Project/maul-ui/pull/29))
- Undo after `muiTextBlock_EndComposition` no longer loses text: the
  kept composition is recorded in the edit history.
  ([#30](https://github.com/Rawframe-Project/maul-ui/pull/30))
- The WebGPU quad shader embedded in the renderer matches its source
  again.
  ([#25](https://github.com/Rawframe-Project/maul-ui/pull/25))

## [0.1.0](https://github.com/Rawframe-Project/maul-ui/compare/v0.0.1...v0.1.0) (2026-10-24)

A release of fixes that change the API: a program built against 0.0.1
must be rebuilt, and code that compares results to `mui_errorInvalid`
may need to look for `mui_errorFormat` instead.

### Changed

- The access tree, AT-SPI application and ARIA, Android,
  NSAccessibility, UI Automation and UIKit adapter defs carry the
  version of the headers the program was built with, as the context
  and text service defs do. A def built against other headers is
  refused with `mui_errorVersion` before anything else in it is read
  ([#22](https://github.com/Rawframe-Project/maul-ui/pull/22)).
- Bad data that another program controls is refused with
  `mui_errorFormat` and no longer counts as misuse, so every
  `mui_errorInvalid` is now a bug in the host
  ([#19](https://github.com/Rawframe-Project/maul-ui/pull/19)):
  - `muiCreateFont` returns `mui_errorFormat` for font data shorter
    than a header or longer than 2^31 - 1 bytes;
  - `muiPerformAccessAction` returns it for a screen reader's value or
    point that is not finite;
  - `muiTextPerformAccessAction` returns it for offsets that are not
    character starts of the text as it is now, or a field no longer
    editing.

### Removed

- The library no longer exports `muiDefaultAccessTreeDef`,
  `muiDefaultAtspiAppDef`, `muiDefaultAriaAdapterDef`,
  `muiDefaultAndroidAdapterDef`, `muiDefaultNsAdapterDef`,
  `muiDefaultUiaAdapterDef` or `muiDefaultUikitAdapterDef`. They are
  `static inline` in the headers, so they stamp the program's version
  ([#22](https://github.com/Rawframe-Project/maul-ui/pull/22)).

### Fixed

- Layout no longer hangs on a child far wider than its row that is
  shrunk onto its minimum width
  ([#17](https://github.com/Rawframe-Project/maul-ui/issues/17)).
- The NSAccessibility, Android and ARIA adapters no longer pass a
  screen reader's value that is not finite, or lies outside the
  range, to the program's action function, as the AT-SPI and UI
  Automation adapters already did not
  ([#19](https://github.com/Rawframe-Project/maul-ui/pull/19)).

## [0.0.1](https://github.com/Rawframe-Project/maul-ui/releases/tag/v0.0.1) (2026-10-23)

The first release: a retained UI core that lays out, styles, draws,
takes input and speaks to screen readers, the same on every platform.

### Added

- A context holding the interface as a tree of nodes, with ids that
  never name a destroyed node's successor, limits fixed at creation and
  no allocation during a frame.
- CSS flexbox layout with wrapping, gaps, automatic margins, aspect
  ratios, absolute placement with anchors, baseline alignment,
  right-to-left mirroring and safe-area padding, held to Chrome on over
  a thousand fixtures. A frame where nothing changed does no work.
- Style in fixed layers: classes and node types with state variants,
  conditions on a node's size and on the viewport, input modality, text
  scale and reduced motion, and values set on the node itself. Design
  tokens with aliases, themes scoped to subtrees, and transitions,
  timed or spring, that move colors in Oklab.
- A draw list of boxes with rounded corners, borders and gradients,
  shadows, nine-slice images and glyph runs, with clip chains and
  transforms a renderer can batch across, snapped to device pixels and
  the same bytes on every platform. Content the program draws itself
  goes into the list through a paint function.
- A text component on FreeType and HarfBuzz: shaping, bidirectional
  text, line breaking, rich-text spans, font fallback, families and
  variable fonts, and glyph images as coverage, multi-channel distance
  fields and colour (COLR, CBDT and sbix).
- Text fields with selection, word and line movement, undo, input
  method compositions, passwords, number filters, maximum lengths, PC
  and Mac keymaps and the program's clipboard.
- Input: hit testing through rounded clips, scrolling and scales;
  hover, press, clicks and pointer capture; focus for up to eight
  players with Tab order and directional navigation for keys and
  gamepads; drags, drag and drop, sliders; routing to the program with
  what the interface leaves passed to the game behind it.
- Scrolling with eased steps, touch panning, flings and optional
  overscroll; virtual lists that keep only the rows near the viewport
  and hold the scroll position through edits; popups placed beside an
  anchor and dismissed by a press outside or Escape; dialogs and menus
  as layers; exits that play out before a node goes.
- An accessibility tree built from what the interface already holds,
  with adapters for UI Automation, AT-SPI, NSAccessibility,
  UIAccessibility, Android and the web's ARIA, including text being read
  and edited.
- `maul-ui-rhi`, a reference renderer on Maul RHI, which can also draw
  a panel in 3D against the scene's depth.
- `maul-ui-window`, the glue to Maul Window: input, gamepads, input
  methods, the clipboard, the on-screen keyboard and the platform's
  accessibility adapter for a window.
- Support for Linux, Windows with `clang-cl`, macOS, iOS, Android and
  the web, with Emscripten or Clang's wasm32-wasi; a static or shared
  library, a CMake package and a pkg-config module.

### Changed

- For programs moving from an earlier build of Maul UI (0.6.1 and
  before): `MUI_DEF_VERSION` is now `MUI_ABI_VERSION`, and before 1.0 it
  carries the whole version, patch included. `muiCreateContext` and
  `muiCreateTextService` now refuse a def built with the headers of any
  other release, another patch release too, with `mui_errorVersion`.
  `muiDefaultContextDef` and `muiDefaultTextServiceDef` are inline and
  stamp the headers' version, so build against the headers of the exact
  library you link.
