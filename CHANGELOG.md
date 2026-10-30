# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every data format.

## [0.4.0](https://github.com/Rawframe-Project/maul-ui/compare/v0.3.3...v0.4.0) (2026-10-30)


### ⚠ BREAKING CHANGES

* **edit:** `muiTextBlock_SetEditing(service, blockId, NULL)` now answers `mui_errorInvalid`; call `muiTextBlock_ClearEditing(muiTextService*, muiTextBlockId)` to opt a block out of editing.
* functions are renamed with the same parameters: `muiResultName` becomes `muiGetResultName`; `muiAccessRoleName` becomes `muiGetAccessRoleName`; `muiAccessIdOf` becomes `muiNode_GetAccessId`; `muiNodeIdOfAccess` becomes `muiGetNodeOfAccessId`; `muiBuildAccessUpdate` becomes `muiNode_BuildAccessUpdate`; `muiPerformAccessAction` becomes `muiContext_PerformAccessAction`; `muiGetAndroidAdapterMisuse` becomes `muiAndroidAdapter_GetMisuse`; `muiGetAriaAdapterMisuse` becomes `muiAriaAdapter_GetMisuse`; `muiGetAtspiAppMisuse` becomes `muiAtspiApp_GetMisuse`; `muiGetNsAdapterMisuse` becomes `muiNsAdapter_GetMisuse`; `muiGetAccessTreeMisuse` becomes `muiAccessTree_GetMisuse`; `muiGetUiaAdapterMisuse` becomes `muiUiaAdapter_GetMisuse`; `muiGetUikitAdapterMisuse` becomes `muiUikitAdapter_GetMisuse`; `muiGetContextMisuse` becomes `muiContext_GetMisuse`; `muiGetTextServiceMisuse` becomes `muiTextService_GetMisuse`; `muiAccessTree_Count` becomes `muiAccessTree_GetNodeCount`; `muiNextNotification` becomes `muiContext_TakeNotification`; `muiGetWorkCounts` becomes `muiContext_GetWorkCounts`; `muiBuildDrawList` becomes `muiNode_BuildDrawList`; `muiGetDrawList` becomes `muiContext_GetDrawList`; `muiKeyInput` becomes `muiNode_RouteKey`; `muiTextInput` becomes `muiNode_RouteText`; `muiNavigationInput` becomes `muiNode_RouteNavigation`; `muiWheelInput` becomes `muiNode_RouteWheel`; `muiPointerInput` becomes `muiNode_RoutePointer`; `muiDispatchPointerRecord` becomes `muiContext_DispatchPointerRecord`; `muiNextPointerRecord` becomes `muiContext_TakePointerRecord`; `muiFocus_Set` becomes `muiContext_SetFocus`; `muiFocus_Get` becomes `muiContext_GetFocus`; `muiFocus_Move` becomes `muiNode_MoveFocus`; `muiFocus_MoveToward` becomes `muiNode_MoveFocusToward`; `muiGlyphAtlas_NextFrame` becomes `muiGlyphAtlas_BeginFrame`; `muiGlyphAtlas_Get` becomes `muiGlyphAtlas_GetGlyph`; `muiGlyphAtlas_GetColor` becomes `muiGlyphAtlas_GetColorGlyph`; `muiGlyphAtlas_GetField` becomes `muiGlyphAtlas_GetGlyphField`; `muiGlyphAtlas_GetMultiField` becomes `muiGlyphAtlas_GetGlyphMultiField`; `muiRenderGlyph` becomes `muiTextService_RenderGlyph`; `muiRenderColorGlyph` becomes `muiTextService_RenderColorGlyph`; `muiRenderGlyphField` becomes `muiTextService_RenderGlyphField`; `muiRenderGlyphMultiField` becomes `muiTextService_RenderGlyphMultiField`; `muiHitTest` becomes `muiNode_HitTest`; `muiComputeLayout` becomes `muiNode_ComputeLayout`; `muiIsUpdatePending` becomes `muiNode_IsUpdatePending`; `muiSetClickRule` becomes `muiContext_SetClickRule`; `muiSetDragThreshold` becomes `muiContext_SetDragThreshold`; `muiSetScrollRule` becomes `muiContext_SetScrollRule`; `muiSetContextEnvironment` becomes `muiContext_SetEnvironment`; `muiGetContextEnvironment` becomes `muiContext_GetEnvironment`; `muiSetDefaultFont` becomes `muiTextService_SetDefaultFont`; `muiSetFallbackFonts` becomes `muiTextService_SetFallbackFonts`; `muiGetTextServiceFailures` becomes `muiTextService_GetFailures`; `muiTextHitTest` becomes `muiNode_HitTestText`; `muiTextGetCaret` becomes `muiNode_GetTextCaret`; `muiTextGetRangeRects` becomes `muiNode_GetTextRangeRects`; `muiTextMove` becomes `muiNode_MoveTextPosition`; `muiTextEditEvent` becomes `muiNode_HandleEditEvent`; `muiTextEditMove` becomes `muiNode_MoveEditCaret`; `muiTextEditPress` becomes `muiNode_HandleEditPress`; `muiTextEditDrag` becomes `muiNode_HandleEditDrag`; `muiTextPerformAccessAction` becomes `muiPerformTextAccessAction`; `muiWindowSetComposition` becomes `muiTextBlock_SetPreedit`; `muiWindowCompose` becomes `muiTextBlock_ShowPreedit`; `muiWindowGlue_WriteClipboard` becomes `muiWriteWindowClipboard`; `muiWindowAccess_HasAdapter` becomes `muiWindowAccess_IsAdapterCreated`; `muiSetAccessTextFunction` becomes `muiContext_SetAccessTextFunction`; `muiSetEventFunction` becomes `muiContext_SetEventFunction`; `muiPointer_ReleaseCapture` becomes `muiContext_ReleasePointerCapture`; `muiPointer_GetState` becomes `muiContext_GetPointerState`; `muiPointer_Offer` becomes `muiContext_OfferDrop`; `muiWindowGlue_SetCaret` becomes `muiNode_SetWindowCaret`; `muiWindowGlue_SetTextCaret` becomes `muiNode_SetWindowTextCaret`; `muiWindowGlue_RequestKeyboard` becomes `muiNode_RequestWindowKeyboard`; `muiWindowGlue_Paste` becomes `muiTextBlock_PasteFromWindow`. `muiPointer_SetCapture(context, pointer, nodeId)` becomes `muiNode_CapturePointer(context, nodeId, pointer)`. `muiAccess_Enable(context, rootId)` and `muiAccess_Disable(context, rootId)` become `muiNode_EnableAccess(context, rootId, true)` and `muiNode_EnableAccess(context, rootId, false)`, and `muiNode_IsAccessEnabled(const muiContext*, muiNodeId)` is new. `muiWindowGlueDef.gamepads` becomes `enableGamepads`. Refs Rawframe-Project/.github#82, Rawframe-Project/.github#84
* a measure function's NaN width or height, and a baseline function's +infinity, now mean content that could not be measured, which muiComputeLayout lays out as empty, measures again next time and reports with mui_errorCapacity. muiMeasureText answers NaN and muiMeasureTextBaseline +infinity when memory runs out. muiBuildDrawList answers mui_errorCapacity when a paint function calls the new muiDrawSink_Fail.
* **text:** while a text call given a muiTextHost runs, host code it calls (the service's allocator, FreeType's memory through it, the clipboard writer) gets mui_errorBusy from every call that changes the host's context, with one misuse counted, and muiDestroyContext does nothing.
* mui_errorFormat is -8 and mui_errorVersion -7. What mui_errorState meant, a call back in from host code, is mui_errorBusy (-10); mui_errorState is -6 and now answers editor calls on a block not editing, with no misuse counted. mui_errorUnsupported (-4) and mui_errorLimit (-9) are new: an atlas of another format and compressed web fonts are unsupported, and a count or size past a published MUI_MAX_ or MUI_MIN_ bound is a limit. A context limit's maximum is 2^31 - 2.
* **family:** the draw records lose their reserved fields, and a box record is 132 bytes.
* **rhi:** `maul-ui-rhi` builds against Maul RHI 0.8.0, and a host that hands it a device must use that release too. What the host gets from it: every new buffer and texture reads as zeros in the first frame that uses it, cleared in a pass of Maul RHI's own, so its own pass ids start at 2; its shader containers must be version 2, written again with 0.8.0's `tools/mrhi_container.py`, since version 1 is refused with `mrhi_errorVersion`; pipeline caches written by 0.6.0 are refused with `mrhi_errorVersion`, so keep containers across the upgrade, not caches; a call that fails sets each pointer or id it was given to NULL or the null id; and drivers built outside Maul RHI's tree must declare SPI version 8. What the renderer draws does not change.
* `muiAriaActionFunction`, `muiAtspiActionFunction`, `muiAndroidActionFunction`, `muiNsActionFunction`, `muiUiaActionFunction`, `muiUikitActionFunction` and `muiWindowAccessActionFunction` are replaced by `muiAccessActionFunction` in `maul-ui/access.h`, the type of the `action` field of `muiAriaAdapterDef`, `muiAtspiAdapterDef`, `muiAndroidAdapterDef`, `muiNsAdapterDef`, `muiUiaAdapterDef`, `muiUikitAdapterDef` and `muiWindowAccessDef`. `muiRhiRendererDef.imageContext` becomes `imageUser`, `muiWindowGlueDef.gamepadContext` becomes `gamepadUser`, and `muiLayoutInput.measureUser` becomes `user`. `muiTextBaseline(void* user, muiNodeId, uint64_t hostKey, float width, float height)` becomes `muiMeasureTextBaseline`, and `muiAccessTextOf(void* user, muiNodeId, uint64_t hostKey, const muiRect* shown, muiAccessContent* contentOut)` becomes `muiReadAccessText`, with the same parameters.
* **style:** `muiState` is `uint16_t` in place of `uint8_t`, so `muiNode_SetStates(muiContext*, muiNodeId, muiState)` and `muiNode_GetStates`'s result widen. `mui_variantCondition0` is 17 in place of 9. Variants 9 to 16 are refused with `mui_errorInvalid`, by `muiStyle_SetLayoutValues`, `muiStyle_SetVisualValues`, `muiStyle_SetTextValues`, `muiStyle_SetInteractionValues`, their `Get` counterparts, `muiStyle_ResetProperties`, `muiStyle_SetCondition`, `muiStyle_GetCondition`, `muiStyle_SetToken`, `muiStyle_GetToken`, `muiStyle_SetTransition` and `muiStyle_GetTransition`. `muiNode_SetStates` refuses a bit that names no state with `mui_errorInvalid`.
* a failing call whose first parameter is a context, text service, access tree, glyph atlas, draw sink, adapter, AT-SPI application or window glue now clears its outputs (zeros, null ids, NULL, false, 0) instead of leaving them as they were, and leaves zeros with mui_empty unless its header says otherwise. The exception is a count, length or image size that a capacity failure reports. `muiFontFamily_MatchFace` takes `muiFontSlant slant` in place of `uint8_t slant`. Changed calls:
    - `access.h`: `muiBuildAccessUpdate`, `muiPerformAccessAction`,
    `muiNode_GetAccessRole`, `muiNode_GetAccessText`,
    `muiNode_GetAccessFlags`, `muiNode_GetAccessRelation`,
    `muiNode_GetAccessValues`
    - `access_atspi.h`: `muiCreateAtspiAdapter`
    - `access_tree.h`: `muiAccessTree_GetShownChildren`,
    `muiAccessTree_GetName`, `muiAccessTree_GetBounds`,
    `muiAccessTree_GetTextRects`, `muiAccessTree_GetTextOffsetAt`,
    `muiAccessTree_Write`
    - `access_uia.h`: `muiUiaAdapter_HandleGetObject`
    - `clipboard.h`: `muiWindowGlue_Paste`
    - `composition.h`: `muiWindowSetComposition`, `muiWindowCompose`,
    `muiWindowGlue_SetTextCaret`
    - `context.h`: `muiNextNotification`
    - `draw.h`: `muiDrawSink_GetVisibleRect`, `muiGetDrawList`
    - `event.h`: `muiKeyInput`, `muiTextInput`, `muiNavigationInput`,
    `muiWheelInput`, `muiDispatchPointerRecord`
    - `font.h`: `muiCreateFont`, `muiFont_GetMetrics`,
    `muiCreateFontFamily`, `muiFontFamily_MatchFace`
    - `glue.h`: `muiWindowGlue_HandleEvent`, `muiWindowGlue_Tick`,
    `muiWindowGlue_SetCaret`
    - `glyph_atlas.h`: `muiCreateGlyphAtlas`, `muiGlyphAtlas_Get`,
    `muiGlyphAtlas_GetColor`, `muiGlyphAtlas_GetField`,
    `muiGlyphAtlas_GetMultiField`, `muiGlyphAtlas_GetPage`,
    `muiGlyphAtlas_TakeUpdates`
    - `glyph_image.h`: `muiRenderGlyph`, `muiRenderColorGlyph`,
    `muiRenderGlyphField`, `muiRenderGlyphMultiField`
    - `interaction.h`: `muiStyle_GetInteractionValues`,
    `muiNode_GetInteractionStyle`, `muiHitTest`
    - `layout.h`: `muiNode_GetLayoutStyle`, `muiNode_MapToRoot`
    - `node.h`: `muiCreateNode`
    - `pointer.h`: `muiNextPointerRecord`, `muiPointer_GetState`
    - `popup.h`: `muiNode_GetPopup`, `muiNode_GetPopupSide`
    - `range.h`: `muiNode_GetValueRange`
    - `scroll.h`: `muiNode_GetScroll`, `muiNode_GetScrollExtent`,
    `muiNode_GetScrollThumb`
    - `style.h`: `muiCreateStyle`, `muiStyle_GetLayoutValues`,
    `muiStyle_AddCondition`, `muiStyle_GetCondition`, `muiCreateNodeType`
    - `text_block.h`: `muiCreateTextBlock`, `muiTextBlock_GetSpans`,
    `muiTextBlock_GetText`
    - `text_edit.h`: `muiTextBlock_FindDeletion`,
    `muiTextBlock_GetComposition`
    - `text_editor.h`: `muiTextBlock_GetSelection`, `muiTextBlock_Type`,
    `muiTextBlock_Paste`, `muiTextBlock_Erase`, `muiTextBlock_EraseTo`,
    `muiTextBlock_GetSelectedText`, `muiTextBlock_Undo`,
    `muiTextBlock_Redo`, `muiTextBlock_GetInputPurpose`,
    `muiTextBlock_GetUndoState`, `muiTextBlock_Compose`
    - `text_style.h`: `muiStyle_GetTextValues`, `muiNode_GetTextStyle`
    - `theme.h`: `muiCreateTheme`, `muiTheme_GetToken`, `muiNode_GetTheme`,
    `muiNode_GetTokenValue`
    - `token.h`: `muiCreateToken`, `muiGetTokenValue`, `muiStyle_GetToken`
    - `transition.h`: `muiCreateTransition`, `muiStyle_GetTransition`
    - `virtual.h`: `muiNode_GetVirtualWindow`, `muiNode_GetVirtualItem`,
    `muiNode_GetItem`
    - `visual.h`: `muiStyle_GetVisualValues`, `muiNode_GetVisualStyle`
* **glyph:** `muiAtlasFormat` is `typedef uint8_t` in place of `typedef enum muiAtlasFormat`, so `muiGlyphAtlasDef.format` and `muiAtlasPage.format` are one byte, and `enum muiAtlasFormat` no longer names a type. The constants keep their names and values.

### Fixed

* **access:** no shown change for a focus leaving the root ([#142](https://github.com/Rawframe-Project/maul-ui/issues/142)) ([d9389ee](https://github.com/Rawframe-Project/maul-ui/commit/d9389ee5fe23e3dd9e27ce3cbd5033ead3b31694)), closes [#4](https://github.com/Rawframe-Project/maul-ui/issues/4) [#141](https://github.com/Rawframe-Project/maul-ui/issues/141)
* **access:** no shown change for a focus moving between shown nodes ([#121](https://github.com/Rawframe-Project/maul-ui/issues/121)) ([5a65788](https://github.com/Rawframe-Project/maul-ui/commit/5a657886d0872f744dfe769f554c6809c0326603)), closes [#4](https://github.com/Rawframe-Project/maul-ui/issues/4)
* clear every output on failure, as the family requires ([#87](https://github.com/Rawframe-Project/maul-ui/issues/87)) ([32ceb71](https://github.com/Rawframe-Project/maul-ui/commit/32ceb71ce9edd79dd84c0f1c905f52e39cd1ea14))
* clear the open code scanning alerts ([#94](https://github.com/Rawframe-Project/maul-ui/issues/94)) ([5336405](https://github.com/Rawframe-Project/maul-ui/commit/5336405cc94d0f5246e142793a7e5e183eaf12bd)), closes [#92](https://github.com/Rawframe-Project/maul-ui/issues/92)
* **edit:** keep the caret off a character an ended composition joins ([#97](https://github.com/Rawframe-Project/maul-ui/issues/97)) ([8cb4fc9](https://github.com/Rawframe-Project/maul-ui/commit/8cb4fc904bd905092a3ffba7b4bab0fb842095b7)), closes [#96](https://github.com/Rawframe-Project/maul-ui/issues/96)
* give callbacks one action type, user pointers and verb-first names ([#104](https://github.com/Rawframe-Project/maul-ui/issues/104)) ([9398d1f](https://github.com/Rawframe-Project/maul-ui/commit/9398d1fe0edc61560f38c2a97ffa7abec0000fe0))
* **glyph:** make muiAtlasFormat a byte, as family enumerations are ([#88](https://github.com/Rawframe-Project/maul-ui/issues/88)) ([9193a98](https://github.com/Rawframe-Project/maul-ui/commit/9193a988f60f4094b5094f59b3834506f3c2197c))
* **glyph:** refuse reversed clip boxes and oversized colour bitmaps ([#120](https://github.com/Rawframe-Project/maul-ui/issues/120)) ([e41f061](https://github.com/Rawframe-Project/maul-ui/commit/e41f061048f3bae8cf68ae285f2e085294f23913)), closes [#119](https://github.com/Rawframe-Project/maul-ui/issues/119)
* **layout:** resolve stretched ratio columns' percentages at the stretch ([#128](https://github.com/Rawframe-Project/maul-ui/issues/128)) ([ec0fc86](https://github.com/Rawframe-Project/maul-ui/commit/ec0fc867a4a05b4d53454ded888853d268cd3039)), closes [#127](https://github.com/Rawframe-Project/maul-ui/issues/127)
* measure and paint host content again after it fails ([#136](https://github.com/Rawframe-Project/maul-ui/issues/136)) ([7e32aab](https://github.com/Rawframe-Project/maul-ui/commit/7e32aab9f5fc13cf2d2c0b3d526f4c7e5b323226))
* **style:** give muiState room for more states ([#91](https://github.com/Rawframe-Project/maul-ui/issues/91)) ([c5cf0aa](https://github.com/Rawframe-Project/maul-ui/commit/c5cf0aa2f601902f01a8afc8d796b75b81502f94))
* **text:** change nothing and answer right when memory runs out ([#135](https://github.com/Rawframe-Project/maul-ui/issues/135)) ([6e36cda](https://github.com/Rawframe-Project/maul-ui/commit/6e36cda7beb9eeb72cb270176c6c058b5240e69b)), closes [#131](https://github.com/Rawframe-Project/maul-ui/issues/131) [#132](https://github.com/Rawframe-Project/maul-ui/issues/132)
* **text:** hold the host's context while a text call runs ([#130](https://github.com/Rawframe-Project/maul-ui/issues/130)) ([80ff014](https://github.com/Rawframe-Project/maul-ui/commit/80ff0143fb2c29b6ee97bcfa0b61cda9f94f7c78))
* **tools:** escape backslashes and quotes in javac's argument file ([#101](https://github.com/Rawframe-Project/maul-ui/issues/101)) ([c369b8d](https://github.com/Rawframe-Project/maul-ui/commit/c369b8d6d574a88374a896cbc5bb6019c7d8794b)), closes [#100](https://github.com/Rawframe-Project/maul-ui/issues/100)
* **tools:** measure every public function's body in the size budget ([#103](https://github.com/Rawframe-Project/maul-ui/issues/103)) ([72157a5](https://github.com/Rawframe-Project/maul-ui/commit/72157a5672ca726f5b50afb343d78895b12931b6)), closes [#102](https://github.com/Rawframe-Project/maul-ui/issues/102)


### Changed

* **edit:** end editing with muiTextBlock_ClearEditing ([#150](https://github.com/Rawframe-Project/maul-ui/issues/150)) ([7b9f4b4](https://github.com/Rawframe-Project/maul-ui/commit/7b9f4b4f7ea37ab89d6591c5e24bfd94e7305f7f))
* give results the family's values and statuses ([#126](https://github.com/Rawframe-Project/maul-ui/issues/126)) ([0f76ef2](https://github.com/Rawframe-Project/maul-ui/commit/0f76ef29c83e6abc3057333f8b3a58efeccd1abc))
* name calls by the family's PObject_Verb rule ([#149](https://github.com/Rawframe-Project/maul-ui/issues/149)) ([d82937d](https://github.com/Rawframe-Project/maul-ui/commit/d82937d8db156e4fee2d78a88cbda7647df9a548))


### Build

* **family:** take .github v0.0.33 and drop reserved draw fields ([#122](https://github.com/Rawframe-Project/maul-ui/issues/122)) ([6165d88](https://github.com/Rawframe-Project/maul-ui/commit/6165d882610f6be3c0f41dfbbfcec6e23fd15a19))
* **rhi:** take Maul RHI 0.8.0 ([#109](https://github.com/Rawframe-Project/maul-ui/issues/109)) ([659cdd2](https://github.com/Rawframe-Project/maul-ui/commit/659cdd285ad38333592438e6c45de18c8c179224))

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
