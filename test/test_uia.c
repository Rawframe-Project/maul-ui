// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter, read by a UI Automation client on a second
// thread while the window's thread pumps its messages:
// - the root's children as shown: a generic node flattened away;
// - names, control types, a heading's level and a landmark;
// - bounds in screen pixels at a scale of 1.5, and the node under a
//   point;
// - the focus, and focusing asked of the host;
// - patterns: Invoke, Toggle, RangeValue, ExpandCollapse, Scroll, Value
//   and SelectionItem, read and turned into the host's actions; Text's
//   ranges read, moved by unit and selected;
// - events: the focus moving, and a checkbox's state changing;
// - a node removed answering UIA_E_ELEMENTNOTAVAILABLE.

#include "test_harness.h"

#include "maul-ui/access_uia.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include "uia_com.h"
#include "uia_ids.h"

#include <ole2.h>
#include <uiautomationclient.h>
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
#include <windows.h>

// The adapter's own declarations of UI Automation (src/uia_com.h,
// src/uia_ids.h) against the headers: the same layouts and values.
static_assert(sizeof(muiUiaSimpleTable) == sizeof(IRawElementProviderSimpleVtbl),
              "IRawElementProviderSimpleVtbl");
static_assert(sizeof(muiUiaRangeTable) == sizeof(ITextRangeProviderVtbl), "ITextRangeProviderVtbl");
static_assert(offsetof(muiUiaRangeTable, Move) == offsetof(ITextRangeProviderVtbl, Move),
              "ITextRangeProviderVtbl Move");
static_assert(offsetof(muiUiaRangeTable, GetChildren) ==
                  offsetof(ITextRangeProviderVtbl, GetChildren),
              "ITextRangeProviderVtbl GetChildren");
static_assert(sizeof(muiUiaTextTable) == sizeof(ITextProvider2Vtbl), "ITextProvider2Vtbl");
static_assert(offsetof(muiUiaTextTable, get_SupportedTextSelection) ==
                  offsetof(ITextProvider2Vtbl, get_SupportedTextSelection),
              "ITextProvider2Vtbl get_SupportedTextSelection");
static_assert(offsetof(muiUiaTextTable, GetCaretRange) ==
                  offsetof(ITextProvider2Vtbl, GetCaretRange),
              "ITextProvider2Vtbl GetCaretRange");
static_assert(sizeof(muiUiaPoint) == sizeof(struct UiaPoint), "UiaPoint");
static_assert(offsetof(muiUiaSimpleTable, QueryInterface) ==
                  offsetof(IRawElementProviderSimpleVtbl, QueryInterface),
              "IRawElementProviderSimpleVtbl QueryInterface");
static_assert(offsetof(muiUiaSimpleTable, AddRef) ==
                  offsetof(IRawElementProviderSimpleVtbl, AddRef),
              "IRawElementProviderSimpleVtbl AddRef");
static_assert(offsetof(muiUiaSimpleTable, Release) ==
                  offsetof(IRawElementProviderSimpleVtbl, Release),
              "IRawElementProviderSimpleVtbl Release");
static_assert(offsetof(muiUiaSimpleTable, get_ProviderOptions) ==
                  offsetof(IRawElementProviderSimpleVtbl, get_ProviderOptions),
              "IRawElementProviderSimpleVtbl get_ProviderOptions");
static_assert(offsetof(muiUiaSimpleTable, GetPatternProvider) ==
                  offsetof(IRawElementProviderSimpleVtbl, GetPatternProvider),
              "IRawElementProviderSimpleVtbl GetPatternProvider");
static_assert(offsetof(muiUiaSimpleTable, GetPropertyValue) ==
                  offsetof(IRawElementProviderSimpleVtbl, GetPropertyValue),
              "IRawElementProviderSimpleVtbl GetPropertyValue");
static_assert(offsetof(muiUiaSimpleTable, get_HostRawElementProvider) ==
                  offsetof(IRawElementProviderSimpleVtbl, get_HostRawElementProvider),
              "IRawElementProviderSimpleVtbl get_HostRawElementProvider");
static_assert(sizeof(muiUiaFragmentTable) == sizeof(IRawElementProviderFragmentVtbl),
              "IRawElementProviderFragmentVtbl");
static_assert(offsetof(muiUiaFragmentTable, QueryInterface) ==
                  offsetof(IRawElementProviderFragmentVtbl, QueryInterface),
              "IRawElementProviderFragmentVtbl QueryInterface");
static_assert(offsetof(muiUiaFragmentTable, AddRef) ==
                  offsetof(IRawElementProviderFragmentVtbl, AddRef),
              "IRawElementProviderFragmentVtbl AddRef");
static_assert(offsetof(muiUiaFragmentTable, Release) ==
                  offsetof(IRawElementProviderFragmentVtbl, Release),
              "IRawElementProviderFragmentVtbl Release");
static_assert(offsetof(muiUiaFragmentTable, Navigate) ==
                  offsetof(IRawElementProviderFragmentVtbl, Navigate),
              "IRawElementProviderFragmentVtbl Navigate");
static_assert(offsetof(muiUiaFragmentTable, GetRuntimeId) ==
                  offsetof(IRawElementProviderFragmentVtbl, GetRuntimeId),
              "IRawElementProviderFragmentVtbl GetRuntimeId");
static_assert(offsetof(muiUiaFragmentTable, get_BoundingRectangle) ==
                  offsetof(IRawElementProviderFragmentVtbl, get_BoundingRectangle),
              "IRawElementProviderFragmentVtbl get_BoundingRectangle");
static_assert(offsetof(muiUiaFragmentTable, GetEmbeddedFragmentRoots) ==
                  offsetof(IRawElementProviderFragmentVtbl, GetEmbeddedFragmentRoots),
              "IRawElementProviderFragmentVtbl GetEmbeddedFragmentRoots");
static_assert(offsetof(muiUiaFragmentTable, SetFocus) ==
                  offsetof(IRawElementProviderFragmentVtbl, SetFocus),
              "IRawElementProviderFragmentVtbl SetFocus");
static_assert(offsetof(muiUiaFragmentTable, get_FragmentRoot) ==
                  offsetof(IRawElementProviderFragmentVtbl, get_FragmentRoot),
              "IRawElementProviderFragmentVtbl get_FragmentRoot");
static_assert(sizeof(muiUiaFragmentRootTable) == sizeof(IRawElementProviderFragmentRootVtbl),
              "IRawElementProviderFragmentRootVtbl");
static_assert(offsetof(muiUiaFragmentRootTable, QueryInterface) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, QueryInterface),
              "IRawElementProviderFragmentRootVtbl QueryInterface");
static_assert(offsetof(muiUiaFragmentRootTable, AddRef) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, AddRef),
              "IRawElementProviderFragmentRootVtbl AddRef");
static_assert(offsetof(muiUiaFragmentRootTable, Release) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, Release),
              "IRawElementProviderFragmentRootVtbl Release");
static_assert(offsetof(muiUiaFragmentRootTable, ElementProviderFromPoint) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, ElementProviderFromPoint),
              "IRawElementProviderFragmentRootVtbl ElementProviderFromPoint");
static_assert(offsetof(muiUiaFragmentRootTable, GetFocus) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, GetFocus),
              "IRawElementProviderFragmentRootVtbl GetFocus");
static_assert(sizeof(muiUiaInvokeTable) == sizeof(IInvokeProviderVtbl), "IInvokeProviderVtbl");
static_assert(offsetof(muiUiaInvokeTable, QueryInterface) ==
                  offsetof(IInvokeProviderVtbl, QueryInterface),
              "IInvokeProviderVtbl QueryInterface");
static_assert(offsetof(muiUiaInvokeTable, AddRef) == offsetof(IInvokeProviderVtbl, AddRef),
              "IInvokeProviderVtbl AddRef");
static_assert(offsetof(muiUiaInvokeTable, Release) == offsetof(IInvokeProviderVtbl, Release),
              "IInvokeProviderVtbl Release");
static_assert(offsetof(muiUiaInvokeTable, Invoke) == offsetof(IInvokeProviderVtbl, Invoke),
              "IInvokeProviderVtbl Invoke");
static_assert(sizeof(muiUiaToggleTable) == sizeof(IToggleProviderVtbl), "IToggleProviderVtbl");
static_assert(offsetof(muiUiaToggleTable, QueryInterface) ==
                  offsetof(IToggleProviderVtbl, QueryInterface),
              "IToggleProviderVtbl QueryInterface");
static_assert(offsetof(muiUiaToggleTable, AddRef) == offsetof(IToggleProviderVtbl, AddRef),
              "IToggleProviderVtbl AddRef");
static_assert(offsetof(muiUiaToggleTable, Release) == offsetof(IToggleProviderVtbl, Release),
              "IToggleProviderVtbl Release");
static_assert(offsetof(muiUiaToggleTable, Toggle) == offsetof(IToggleProviderVtbl, Toggle),
              "IToggleProviderVtbl Toggle");
static_assert(offsetof(muiUiaToggleTable, get_ToggleState) ==
                  offsetof(IToggleProviderVtbl, get_ToggleState),
              "IToggleProviderVtbl get_ToggleState");
static_assert(sizeof(muiUiaExpandCollapseTable) == sizeof(IExpandCollapseProviderVtbl),
              "IExpandCollapseProviderVtbl");
static_assert(offsetof(muiUiaExpandCollapseTable, QueryInterface) ==
                  offsetof(IExpandCollapseProviderVtbl, QueryInterface),
              "IExpandCollapseProviderVtbl QueryInterface");
static_assert(offsetof(muiUiaExpandCollapseTable, AddRef) ==
                  offsetof(IExpandCollapseProviderVtbl, AddRef),
              "IExpandCollapseProviderVtbl AddRef");
static_assert(offsetof(muiUiaExpandCollapseTable, Release) ==
                  offsetof(IExpandCollapseProviderVtbl, Release),
              "IExpandCollapseProviderVtbl Release");
static_assert(offsetof(muiUiaExpandCollapseTable, Expand) ==
                  offsetof(IExpandCollapseProviderVtbl, Expand),
              "IExpandCollapseProviderVtbl Expand");
static_assert(offsetof(muiUiaExpandCollapseTable, Collapse) ==
                  offsetof(IExpandCollapseProviderVtbl, Collapse),
              "IExpandCollapseProviderVtbl Collapse");
static_assert(offsetof(muiUiaExpandCollapseTable, get_ExpandCollapseState) ==
                  offsetof(IExpandCollapseProviderVtbl, get_ExpandCollapseState),
              "IExpandCollapseProviderVtbl get_ExpandCollapseState");
static_assert(sizeof(muiUiaValueTable) == sizeof(IValueProviderVtbl), "IValueProviderVtbl");
static_assert(offsetof(muiUiaValueTable, QueryInterface) ==
                  offsetof(IValueProviderVtbl, QueryInterface),
              "IValueProviderVtbl QueryInterface");
static_assert(offsetof(muiUiaValueTable, AddRef) == offsetof(IValueProviderVtbl, AddRef),
              "IValueProviderVtbl AddRef");
static_assert(offsetof(muiUiaValueTable, Release) == offsetof(IValueProviderVtbl, Release),
              "IValueProviderVtbl Release");
static_assert(offsetof(muiUiaValueTable, SetValue) == offsetof(IValueProviderVtbl, SetValue),
              "IValueProviderVtbl SetValue");
static_assert(offsetof(muiUiaValueTable, get_Value) == offsetof(IValueProviderVtbl, get_Value),
              "IValueProviderVtbl get_Value");
static_assert(offsetof(muiUiaValueTable, get_IsReadOnly) ==
                  offsetof(IValueProviderVtbl, get_IsReadOnly),
              "IValueProviderVtbl get_IsReadOnly");
static_assert(sizeof(muiUiaRangeValueTable) == sizeof(IRangeValueProviderVtbl),
              "IRangeValueProviderVtbl");
static_assert(offsetof(muiUiaRangeValueTable, QueryInterface) ==
                  offsetof(IRangeValueProviderVtbl, QueryInterface),
              "IRangeValueProviderVtbl QueryInterface");
static_assert(offsetof(muiUiaRangeValueTable, AddRef) == offsetof(IRangeValueProviderVtbl, AddRef),
              "IRangeValueProviderVtbl AddRef");
static_assert(offsetof(muiUiaRangeValueTable, Release) ==
                  offsetof(IRangeValueProviderVtbl, Release),
              "IRangeValueProviderVtbl Release");
static_assert(offsetof(muiUiaRangeValueTable, SetValue) ==
                  offsetof(IRangeValueProviderVtbl, SetValue),
              "IRangeValueProviderVtbl SetValue");
static_assert(offsetof(muiUiaRangeValueTable, get_Value) ==
                  offsetof(IRangeValueProviderVtbl, get_Value),
              "IRangeValueProviderVtbl get_Value");
static_assert(offsetof(muiUiaRangeValueTable, get_IsReadOnly) ==
                  offsetof(IRangeValueProviderVtbl, get_IsReadOnly),
              "IRangeValueProviderVtbl get_IsReadOnly");
static_assert(offsetof(muiUiaRangeValueTable, get_Maximum) ==
                  offsetof(IRangeValueProviderVtbl, get_Maximum),
              "IRangeValueProviderVtbl get_Maximum");
static_assert(offsetof(muiUiaRangeValueTable, get_Minimum) ==
                  offsetof(IRangeValueProviderVtbl, get_Minimum),
              "IRangeValueProviderVtbl get_Minimum");
static_assert(offsetof(muiUiaRangeValueTable, get_LargeChange) ==
                  offsetof(IRangeValueProviderVtbl, get_LargeChange),
              "IRangeValueProviderVtbl get_LargeChange");
static_assert(offsetof(muiUiaRangeValueTable, get_SmallChange) ==
                  offsetof(IRangeValueProviderVtbl, get_SmallChange),
              "IRangeValueProviderVtbl get_SmallChange");
static_assert(sizeof(muiUiaScrollTable) == sizeof(IScrollProviderVtbl), "IScrollProviderVtbl");
static_assert(offsetof(muiUiaScrollTable, QueryInterface) ==
                  offsetof(IScrollProviderVtbl, QueryInterface),
              "IScrollProviderVtbl QueryInterface");
static_assert(offsetof(muiUiaScrollTable, AddRef) == offsetof(IScrollProviderVtbl, AddRef),
              "IScrollProviderVtbl AddRef");
static_assert(offsetof(muiUiaScrollTable, Release) == offsetof(IScrollProviderVtbl, Release),
              "IScrollProviderVtbl Release");
static_assert(offsetof(muiUiaScrollTable, Scroll) == offsetof(IScrollProviderVtbl, Scroll),
              "IScrollProviderVtbl Scroll");
static_assert(offsetof(muiUiaScrollTable, SetScrollPercent) ==
                  offsetof(IScrollProviderVtbl, SetScrollPercent),
              "IScrollProviderVtbl SetScrollPercent");
static_assert(offsetof(muiUiaScrollTable, get_HorizontalScrollPercent) ==
                  offsetof(IScrollProviderVtbl, get_HorizontalScrollPercent),
              "IScrollProviderVtbl get_HorizontalScrollPercent");
static_assert(offsetof(muiUiaScrollTable, get_VerticalScrollPercent) ==
                  offsetof(IScrollProviderVtbl, get_VerticalScrollPercent),
              "IScrollProviderVtbl get_VerticalScrollPercent");
static_assert(offsetof(muiUiaScrollTable, get_HorizontalViewSize) ==
                  offsetof(IScrollProviderVtbl, get_HorizontalViewSize),
              "IScrollProviderVtbl get_HorizontalViewSize");
static_assert(offsetof(muiUiaScrollTable, get_VerticalViewSize) ==
                  offsetof(IScrollProviderVtbl, get_VerticalViewSize),
              "IScrollProviderVtbl get_VerticalViewSize");
static_assert(offsetof(muiUiaScrollTable, get_HorizontallyScrollable) ==
                  offsetof(IScrollProviderVtbl, get_HorizontallyScrollable),
              "IScrollProviderVtbl get_HorizontallyScrollable");
static_assert(offsetof(muiUiaScrollTable, get_VerticallyScrollable) ==
                  offsetof(IScrollProviderVtbl, get_VerticallyScrollable),
              "IScrollProviderVtbl get_VerticallyScrollable");
static_assert(sizeof(muiUiaScrollItemTable) == sizeof(IScrollItemProviderVtbl),
              "IScrollItemProviderVtbl");
static_assert(offsetof(muiUiaScrollItemTable, QueryInterface) ==
                  offsetof(IScrollItemProviderVtbl, QueryInterface),
              "IScrollItemProviderVtbl QueryInterface");
static_assert(offsetof(muiUiaScrollItemTable, AddRef) == offsetof(IScrollItemProviderVtbl, AddRef),
              "IScrollItemProviderVtbl AddRef");
static_assert(offsetof(muiUiaScrollItemTable, Release) ==
                  offsetof(IScrollItemProviderVtbl, Release),
              "IScrollItemProviderVtbl Release");
static_assert(offsetof(muiUiaScrollItemTable, ScrollIntoView) ==
                  offsetof(IScrollItemProviderVtbl, ScrollIntoView),
              "IScrollItemProviderVtbl ScrollIntoView");
static_assert(sizeof(muiUiaSelectionItemTable) == sizeof(ISelectionItemProviderVtbl),
              "ISelectionItemProviderVtbl");
static_assert(offsetof(muiUiaSelectionItemTable, QueryInterface) ==
                  offsetof(ISelectionItemProviderVtbl, QueryInterface),
              "ISelectionItemProviderVtbl QueryInterface");
static_assert(offsetof(muiUiaSelectionItemTable, AddRef) ==
                  offsetof(ISelectionItemProviderVtbl, AddRef),
              "ISelectionItemProviderVtbl AddRef");
static_assert(offsetof(muiUiaSelectionItemTable, Release) ==
                  offsetof(ISelectionItemProviderVtbl, Release),
              "ISelectionItemProviderVtbl Release");
static_assert(offsetof(muiUiaSelectionItemTable, Select) ==
                  offsetof(ISelectionItemProviderVtbl, Select),
              "ISelectionItemProviderVtbl Select");
static_assert(offsetof(muiUiaSelectionItemTable, AddToSelection) ==
                  offsetof(ISelectionItemProviderVtbl, AddToSelection),
              "ISelectionItemProviderVtbl AddToSelection");
static_assert(offsetof(muiUiaSelectionItemTable, RemoveFromSelection) ==
                  offsetof(ISelectionItemProviderVtbl, RemoveFromSelection),
              "ISelectionItemProviderVtbl RemoveFromSelection");
static_assert(offsetof(muiUiaSelectionItemTable, get_IsSelected) ==
                  offsetof(ISelectionItemProviderVtbl, get_IsSelected),
              "ISelectionItemProviderVtbl get_IsSelected");
static_assert(offsetof(muiUiaSelectionItemTable, get_SelectionContainer) ==
                  offsetof(ISelectionItemProviderVtbl, get_SelectionContainer),
              "ISelectionItemProviderVtbl get_SelectionContainer");
static_assert(UIA_OPTION_SERVER_SIDE == ProviderOptions_ServerSideProvider,
              "ProviderOptions_ServerSideProvider");
static_assert(UIA_OPTION_COM_THREADING == ProviderOptions_UseComThreading,
              "ProviderOptions_UseComThreading");
static_assert(NAVIGATE_PARENT == NavigateDirection_Parent, "NavigateDirection_Parent");
static_assert(NAVIGATE_NEXT == NavigateDirection_NextSibling, "NavigateDirection_NextSibling");
static_assert(NAVIGATE_PREVIOUS == NavigateDirection_PreviousSibling,
              "NavigateDirection_PreviousSibling");
static_assert(NAVIGATE_FIRST == NavigateDirection_FirstChild, "NavigateDirection_FirstChild");
static_assert(NAVIGATE_LAST == NavigateDirection_LastChild, "NavigateDirection_LastChild");
static_assert(TOGGLE_OFF == ToggleState_Off, "ToggleState_Off");
static_assert(TOGGLE_ON == ToggleState_On, "ToggleState_On");
static_assert(TOGGLE_MIXED == ToggleState_Indeterminate, "ToggleState_Indeterminate");
static_assert(EXPAND_COLLAPSED == ExpandCollapseState_Collapsed, "ExpandCollapseState_Collapsed");
static_assert(EXPAND_EXPANDED == ExpandCollapseState_Expanded, "ExpandCollapseState_Expanded");
static_assert(AMOUNT_LARGE_BACK == ScrollAmount_LargeDecrement, "ScrollAmount_LargeDecrement");
static_assert(AMOUNT_SMALL_BACK == ScrollAmount_SmallDecrement, "ScrollAmount_SmallDecrement");
static_assert(AMOUNT_LARGE_FORWARD == ScrollAmount_LargeIncrement, "ScrollAmount_LargeIncrement");
static_assert(AMOUNT_SMALL_FORWARD == ScrollAmount_SmallIncrement, "ScrollAmount_SmallIncrement");
static_assert(sizeof(muiUiaRect) == sizeof(struct UiaRect) &&
                  offsetof(muiUiaRect, height) == offsetof(struct UiaRect, height),
              "UiaRect");
static_assert(APPEND_RUNTIME_ID == UiaAppendRuntimeId && ROOT_OBJECT_ID == UiaRootObjectId,
              "runtime and root ids");

// The ids, which the Windows SDK gives as const variables rather than
// constants.
static void TestIds(void)
{
    CHECK(
        PROPERTY_CONTROL_TYPE == UIA_ControlTypePropertyId &&
            PROPERTY_LOCALIZED_CONTROL_TYPE == UIA_LocalizedControlTypePropertyId &&
            PROPERTY_NAME == UIA_NamePropertyId &&
            PROPERTY_ACCELERATOR_KEY == UIA_AcceleratorKeyPropertyId &&
            PROPERTY_HAS_KEYBOARD_FOCUS == UIA_HasKeyboardFocusPropertyId &&
            PROPERTY_IS_KEYBOARD_FOCUSABLE == UIA_IsKeyboardFocusablePropertyId &&
            PROPERTY_IS_ENABLED == UIA_IsEnabledPropertyId &&
            PROPERTY_HELP_TEXT == UIA_HelpTextPropertyId &&
            PROPERTY_IS_PASSWORD == UIA_IsPasswordPropertyId &&
            PROPERTY_IS_OFFSCREEN == UIA_IsOffscreenPropertyId &&
            PROPERTY_ORIENTATION == UIA_OrientationPropertyId &&
            PROPERTY_IS_REQUIRED_FOR_FORM == UIA_IsRequiredForFormPropertyId &&
            PROPERTY_ITEM_STATUS == UIA_ItemStatusPropertyId &&
            PROPERTY_VALUE_VALUE == UIA_ValueValuePropertyId &&
            PROPERTY_RANGE_VALUE_VALUE == UIA_RangeValueValuePropertyId &&
            PROPERTY_EXPAND_COLLAPSE_EXPAND_COLLAPSE_STATE ==
                UIA_ExpandCollapseExpandCollapseStatePropertyId &&
            PROPERTY_SELECTION_ITEM_IS_SELECTED == UIA_SelectionItemIsSelectedPropertyId &&
            PROPERTY_TOGGLE_TOGGLE_STATE == UIA_ToggleToggleStatePropertyId &&
            PROPERTY_LIVE_SETTING == UIA_LiveSettingPropertyId &&
            PROPERTY_POSITION_IN_SET == UIA_PositionInSetPropertyId &&
            PROPERTY_SIZE_OF_SET == UIA_SizeOfSetPropertyId &&
            PROPERTY_LEVEL == UIA_LevelPropertyId &&
            PROPERTY_LANDMARK_TYPE == UIA_LandmarkTypePropertyId &&
            PROPERTY_LOCALIZED_LANDMARK_TYPE == UIA_LocalizedLandmarkTypePropertyId &&
            PROPERTY_FULL_DESCRIPTION == UIA_FullDescriptionPropertyId &&
            PROPERTY_HEADING_LEVEL == UIA_HeadingLevelPropertyId &&
            PROPERTY_IS_DIALOG == UIA_IsDialogPropertyId &&
            EVENT_AUTOMATION_FOCUS_CHANGED == UIA_AutomationFocusChangedEventId &&
            EVENT_LIVE_REGION_CHANGED == UIA_LiveRegionChangedEventId &&
            CONTROL_BUTTON == UIA_ButtonControlTypeId &&
            CONTROL_CHECK_BOX == UIA_CheckBoxControlTypeId &&
            CONTROL_COMBO_BOX == UIA_ComboBoxControlTypeId &&
            CONTROL_EDIT == UIA_EditControlTypeId &&
            CONTROL_HYPERLINK == UIA_HyperlinkControlTypeId &&
            CONTROL_IMAGE == UIA_ImageControlTypeId &&
            CONTROL_LIST_ITEM == UIA_ListItemControlTypeId &&
            CONTROL_LIST == UIA_ListControlTypeId && CONTROL_MENU == UIA_MenuControlTypeId &&
            CONTROL_MENU_BAR == UIA_MenuBarControlTypeId &&
            CONTROL_MENU_ITEM == UIA_MenuItemControlTypeId &&
            CONTROL_PROGRESS_BAR == UIA_ProgressBarControlTypeId &&
            CONTROL_RADIO_BUTTON == UIA_RadioButtonControlTypeId &&
            CONTROL_SCROLL_BAR == UIA_ScrollBarControlTypeId &&
            CONTROL_SLIDER == UIA_SliderControlTypeId &&
            CONTROL_SPINNER == UIA_SpinnerControlTypeId &&
            CONTROL_STATUS_BAR == UIA_StatusBarControlTypeId &&
            CONTROL_TAB == UIA_TabControlTypeId && CONTROL_TAB_ITEM == UIA_TabItemControlTypeId &&
            CONTROL_TEXT == UIA_TextControlTypeId && CONTROL_TOOL_BAR == UIA_ToolBarControlTypeId &&
            CONTROL_TOOL_TIP == UIA_ToolTipControlTypeId && CONTROL_TREE == UIA_TreeControlTypeId &&
            CONTROL_TREE_ITEM == UIA_TreeItemControlTypeId &&
            CONTROL_GROUP == UIA_GroupControlTypeId &&
            CONTROL_DATA_GRID == UIA_DataGridControlTypeId &&
            CONTROL_DATA_ITEM == UIA_DataItemControlTypeId &&
            CONTROL_DOCUMENT == UIA_DocumentControlTypeId &&
            CONTROL_WINDOW == UIA_WindowControlTypeId && CONTROL_PANE == UIA_PaneControlTypeId &&
            CONTROL_HEADER_ITEM == UIA_HeaderItemControlTypeId &&
            CONTROL_TABLE == UIA_TableControlTypeId &&
            CONTROL_TITLE_BAR == UIA_TitleBarControlTypeId &&
            CONTROL_SEPARATOR == UIA_SeparatorControlTypeId &&
            PATTERN_INVOKE == UIA_InvokePatternId && PATTERN_VALUE == UIA_ValuePatternId &&
            PATTERN_RANGE_VALUE == UIA_RangeValuePatternId &&
            PATTERN_SCROLL == UIA_ScrollPatternId &&
            PATTERN_EXPAND_COLLAPSE == UIA_ExpandCollapsePatternId &&
            PATTERN_SELECTION_ITEM == UIA_SelectionItemPatternId &&
            PATTERN_TOGGLE == UIA_TogglePatternId &&
            PATTERN_SCROLL_ITEM == UIA_ScrollItemPatternId && PATTERN_TEXT == UIA_TextPatternId &&
            PATTERN_TEXT2 == UIA_TextPattern2Id &&
            EVENT_TEXT_CHANGED == UIA_Text_TextChangedEventId &&
            EVENT_TEXT_SELECTION_CHANGED == UIA_Text_TextSelectionChangedEventId &&
            UNIT_CHARACTER == TextUnit_Character && UNIT_FORMAT == TextUnit_Format &&
            UNIT_WORD == TextUnit_Word && UNIT_LINE == TextUnit_Line &&
            UNIT_PARAGRAPH == TextUnit_Paragraph && UNIT_PAGE == TextUnit_Page &&
            UNIT_DOCUMENT == TextUnit_Document &&
            ENDPOINT_START == TextPatternRangeEndpoint_Start &&
            ENDPOINT_END == TextPatternRangeEndpoint_End &&
            TEXT_SELECTION_SINGLE == SupportedTextSelection_Single,
        "the ids");
}

#define DEADLINE_MS 20000u
#define SCALE       1.5f

typedef struct Built
{
    muiAccessNode nodes[16];
    const muiAccessNode* sent[16];
    uint64_t children[16];
    uint32_t nodeCount;
    uint32_t childCount;
} Built;

static muiAccessNode* Add(Built* built, uint64_t id, muiRole role, const char* label, float x,
                          float y, float width, float height)
{
    muiAccessNode* node = &built->nodes[built->nodeCount];
    *node = (muiAccessNode){.id = id,
                            .role = role,
                            .bounds = {0.0f, 0.0f, width, height},
                            .transform = {1.0f, 0.0f, 0.0f, 1.0f, x, y}};
    if (label != NULL)
    {
        node->text[role == mui_roleLabel ? mui_accessValue : mui_accessLabel] = label;
        node->textLength[role == mui_roleLabel ? mui_accessValue : mui_accessLabel] =
            (uint32_t)strlen(label);
    }
    built->sent[built->nodeCount++] = node;
    return node;
}

static void List(Built* built, muiAccessNode* parent, const uint64_t* ids, uint32_t count)
{
    parent->firstChild = built->childCount;
    parent->childCount = count;
    memcpy(&built->children[built->childCount], ids, count * sizeof(uint64_t));
    built->childCount += count;
}

// The window 1: a button 2; a generic 3 around a label 4; a heading 5 of
// level 2; a navigation landmark 6; a text input 7, focused, holding
// "Ada"; a checked checkbox 8; a slider 9 at 30 of 0 to 100; a tree item
// 10, collapsed; a list 11 scrolled 50 of 200 with a selected item 12.
static muiAccessUpdate Build(Built* built)
{
    *built = (Built){0};
    muiAccessNode* root = Add(built, 1, mui_roleWindow, "Main", 0, 0, 400, 300);
    Add(built, 2, mui_roleButton, "OK", 10, 10, 100, 40)->flags = mui_accessFocusable;
    muiAccessNode* generic = Add(built, 3, mui_roleGeneric, NULL, 10, 60, 100, 20);
    (void)Add(built, 4, mui_roleLabel, "Hello", 0, 0, 100, 20);
    Add(built, 5, mui_roleHeading, "Title", 10, 90, 100, 20)->values.level = 2;
    (void)Add(built, 6, mui_roleNavigation, "Links", 10, 120, 100, 20);
    muiAccessNode* input = Add(built, 7, mui_roleTextInput, "Name", 10, 150, 200, 24);
    input->flags = mui_accessFocusable;
    // Edited, the caret before its second word.
    static const uint32_t s_lines[1] = {0};
    static const muiAccessWord s_words[2] = {{0, 3}, {4, 12}};
    input->text[mui_accessValue] = "Ada Lovelace";
    input->textLength[mui_accessValue] = 12;
    input->marks = (muiAccessTextMarks){.anchor = 4,
                                        .focus = 4,
                                        .selected = true,
                                        .lineStarts = s_lines,
                                        .lineCount = 1,
                                        .words = s_words,
                                        .wordCount = 2};
    input->actions = 1u << mui_actionSetSelection | 1u << mui_actionReplaceText;
    muiAccessNode* check = Add(built, 8, mui_roleCheckBox, "Agree", 220, 10, 100, 20);
    check->flags = mui_accessCheckable | mui_accessChecked;
    check->actions = 1u << mui_actionClick;
    muiAccessNode* slider = Add(built, 9, mui_roleSlider, "Volume", 220, 40, 100, 20);
    slider->flags = mui_accessNumeric;
    slider->actions = 1u << mui_actionSetValue;
    slider->value = 30.0f;
    slider->maximum = 100.0f;
    slider->step = 1.0f;
    Add(built, 10, mui_roleTreeItem, "Section", 220, 70, 100, 20)->flags = mui_accessExpandable;
    muiAccessNode* list = Add(built, 11, mui_roleList, "Items", 220, 100, 100, 100);
    list->flags = mui_accessScrolls | mui_accessClipsChildren;
    list->scrollY = 50.0f;
    list->scrollYMax = 200.0f;
    muiAccessNode* item = Add(built, 12, mui_roleListItem, "First", 0, 0, 100, 20);
    item->flags = mui_accessSelectable | mui_accessSelected;
    built->nodes[1].actions = 1u << mui_actionClick;
    List(built, root, (const uint64_t[]){2, 3, 5, 6, 7, 8, 9, 10, 11}, 9);
    List(built, generic, (const uint64_t[]){4}, 1);
    List(built, list, (const uint64_t[]){12}, 1);
    return (muiAccessUpdate){built->sent, built->nodeCount, built->children, 1, 7};
}

typedef struct Program
{
    HWND window;
    muiUiaAdapter* adapter;
    // Set by the client: removal wanted, and done.
    HANDLE removal;
    HANDLE removed;
    // Set by the client: a change wanted; by the handlers: events seen.
    HANDLE change;
    volatile LONG focusSeen;
    volatile LONG activeSeen;
    volatile LONG toggleSeen;
    HANDLE done;
    // What the host was asked last, by the window's thread.
    volatile LONG focusAsked;
    muiAccessRequest asked;
} Program;

static Program s_program;

static bool Act(void* user, const muiAccessRequest* request)
{
    Program* program = user;
    if (request->action == mui_actionFocus)
    {
        InterlockedExchange(&program->focusAsked, (LONG)request->target);
    }
    program->asked = *request;
    MemoryBarrier();
    return true;
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    intptr_t result = 0;
    if (message == WM_GETOBJECT && s_program.adapter != NULL &&
        muiUiaAdapter_HandleGetObject(s_program.adapter, wParam, lParam, &result))
    {
        return result;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static bool NameIs(IUIAutomationElement* element, const wchar_t* expected)
{
    BSTR name = NULL;
    bool same = SUCCEEDED(IUIAutomationElement_get_CurrentName(element, &name)) && name != NULL &&
                wcscmp(name, expected) == 0;
    SysFreeString(name);
    return same;
}

static bool TypeIs(IUIAutomationElement* element, CONTROLTYPEID expected)
{
    CONTROLTYPEID type = 0;
    return SUCCEEDED(IUIAutomationElement_get_CurrentControlType(element, &type)) &&
           type == expected;
}

static bool IntPropertyIs(IUIAutomationElement* element, PROPERTYID property, int expected)
{
    VARIANT value;
    VariantInit(&value);
    bool same =
        SUCCEEDED(IUIAutomationElement_GetCurrentPropertyValue(element, property, &value)) &&
        value.vt == VT_I4 && value.lVal == expected;
    VariantClear(&value);
    return same;
}

// The root's children as the client walks them, in order; the caller
// releases them.
static uint32_t Children(IUIAutomation* automation, IUIAutomationElement* root,
                         IUIAutomationElement** out, uint32_t capacity)
{
    IUIAutomationTreeWalker* walker = NULL;
    uint32_t count = 0;
    if (FAILED(IUIAutomation_get_RawViewWalker(automation, &walker)))
    {
        return 0;
    }
    IUIAutomationElement* at = NULL;
    (void)IUIAutomationTreeWalker_GetFirstChildElement(walker, root, &at);
    while (at != NULL && count < capacity)
    {
        out[count++] = at;
        IUIAutomationElement* next = NULL;
        (void)IUIAutomationTreeWalker_GetNextSiblingElement(walker, at, &next);
        at = next;
    }
    IUIAutomationTreeWalker_Release(walker);
    return count;
}

// Prints what the client found, for a run that fails.
static void Report(IUIAutomationElement** children, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        BSTR name = NULL;
        CONTROLTYPEID type = 0;
        (void)IUIAutomationElement_get_CurrentName(children[i], &name);
        (void)IUIAutomationElement_get_CurrentControlType(children[i], &type);
        fwprintf(stderr, L"child %u: %ls, control type %d\n", i, name != NULL ? name : L"",
                 (int)type);
        SysFreeString(name);
    }
}

static void CheckChildren(IUIAutomationElement** children, uint32_t count)
{
    CHECK(count == 9, "nine children, the generic flattened");
    if (count != 9)
    {
        Report(children, count);
        return;
    }
    CHECK(NameIs(children[0], L"OK") && TypeIs(children[0], UIA_ButtonControlTypeId), "a button");
    CHECK(NameIs(children[1], L"Hello") && TypeIs(children[1], UIA_TextControlTypeId),
          "a label, named by its value");
    // HeadingLevel2 and the navigation landmark of UI Automation.
    CHECK(NameIs(children[2], L"Title") && IntPropertyIs(children[2], 30173, 80052),
          "a heading of level 2");
    CHECK(NameIs(children[3], L"Links") && IntPropertyIs(children[3], 30157, 80003),
          "a navigation landmark");
    BOOL focused = FALSE;
    CHECK(NameIs(children[4], L"Name") && TypeIs(children[4], UIA_EditControlTypeId) &&
              SUCCEEDED(IUIAutomationElement_get_CurrentHasKeyboardFocus(children[4], &focused)) &&
              focused,
          "a text input, focused");
}

// What the host was last asked.
static bool Asked(muiAccessAction action, uint64_t target)
{
    MemoryBarrier();
    return s_program.asked.action == action && s_program.asked.target == target;
}

static void CheckInvokeAndToggle(IUIAutomationElement** children)
{
    IUIAutomationInvokePattern* invoke = NULL;
    CHECK(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(children[0], UIA_InvokePatternId,
                                                             &IID_IUIAutomationInvokePattern,
                                                             (void**)&invoke)) &&
              invoke != NULL && SUCCEEDED(IUIAutomationInvokePattern_Invoke(invoke)) &&
              Asked(mui_actionClick, 2),
          "a button invoked, a click asked of the host");
    IUIAutomationTogglePattern* toggle = NULL;
    enum ToggleState state = ToggleState_Off;
    CHECK(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(children[5], UIA_TogglePatternId,
                                                             &IID_IUIAutomationTogglePattern,
                                                             (void**)&toggle)) &&
              toggle != NULL &&
              SUCCEEDED(IUIAutomationTogglePattern_get_CurrentToggleState(toggle, &state)) &&
              state == ToggleState_On && SUCCEEDED(IUIAutomationTogglePattern_Toggle(toggle)) &&
              Asked(mui_actionClick, 8),
          "a checkbox on, toggled by a click");
    IUIAutomationInvokePattern* none = NULL;
    CHECK(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(
              children[5], UIA_InvokePatternId, &IID_IUIAutomationInvokePattern, (void**)&none)) &&
              none == NULL,
          "a checkbox toggles rather than invokes");
    if (invoke != NULL)
    {
        IUIAutomationInvokePattern_Release(invoke);
    }
    if (toggle != NULL)
    {
        IUIAutomationTogglePattern_Release(toggle);
    }
}

static void CheckRangeAndExpand(IUIAutomationElement** children)
{
    IUIAutomationRangeValuePattern* range = NULL;
    double value = 0.0;
    double maximum = 0.0;
    CHECK(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(children[6], UIA_RangeValuePatternId,
                                                             &IID_IUIAutomationRangeValuePattern,
                                                             (void**)&range)) &&
              range != NULL &&
              SUCCEEDED(IUIAutomationRangeValuePattern_get_CurrentValue(range, &value)) &&
              SUCCEEDED(IUIAutomationRangeValuePattern_get_CurrentMaximum(range, &maximum)) &&
              value == 30.0 && maximum == 100.0 &&
              SUCCEEDED(IUIAutomationRangeValuePattern_SetValue(range, 55.0)) &&
              Asked(mui_actionSetValue, 9) && s_program.asked.value == 55.0f &&
              FAILED(IUIAutomationRangeValuePattern_SetValue(range, 200.0)),
          "a slider's value read and set, a value past its range refused");
    IUIAutomationExpandCollapsePattern* expand = NULL;
    enum ExpandCollapseState state = ExpandCollapseState_Expanded;
    CHECK(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(
              children[7], UIA_ExpandCollapsePatternId, &IID_IUIAutomationExpandCollapsePattern,
              (void**)&expand)) &&
              expand != NULL &&
              SUCCEEDED(IUIAutomationExpandCollapsePattern_get_CurrentExpandCollapseState(
                  expand, &state)) &&
              state == ExpandCollapseState_Collapsed &&
              SUCCEEDED(IUIAutomationExpandCollapsePattern_Expand(expand)) &&
              Asked(mui_actionExpand, 10),
          "a tree item collapsed, expanded on request");
    if (range != NULL)
    {
        IUIAutomationRangeValuePattern_Release(range);
    }
    if (expand != NULL)
    {
        IUIAutomationExpandCollapsePattern_Release(expand);
    }
}

// A range's text is expected.
static bool RangeReads(IUIAutomationTextRange* range, const WCHAR* expected)
{
    BSTR text = NULL;
    bool same = range != NULL && SUCCEEDED(IUIAutomationTextRange_GetText(range, -1, &text)) &&
                text != NULL && wcscmp(text, expected) == 0;
    if (!same)
    {
        printf("range: %ls\n", text != NULL ? text : L"(none)");
    }
    SysFreeString(text);
    return same;
}

// The text input's Text pattern: the document, the caret's word, moves
// by unit, a selection asked of the host.
static void CheckText(IUIAutomationElement** children)
{
    IUIAutomationTextPattern2* text = NULL;
    IUIAutomationTextRange* document = NULL;
    IUIAutomationTextRange* caret = NULL;
    BOOL active = FALSE;
    int moved = 0;
    CHECK(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(
              children[4], UIA_TextPattern2Id, &IID_IUIAutomationTextPattern2, (void**)&text)) &&
              text != NULL &&
              SUCCEEDED(IUIAutomationTextPattern2_get_DocumentRange(text, &document)) &&
              RangeReads(document, L"Ada Lovelace"),
          "the document");
    CHECK(SUCCEEDED(IUIAutomationTextPattern2_GetCaretRange(text, &active, &caret)) &&
              caret != NULL && active && RangeReads(caret, L"") &&
              SUCCEEDED(IUIAutomationTextRange_ExpandToEnclosingUnit(caret, TextUnit_Word)) &&
              RangeReads(caret, L"Lovelace"),
          "the caret, focused, in its word");
    CHECK(SUCCEEDED(IUIAutomationTextRange_Move(caret, TextUnit_Word, -1, &moved)) && moved == -1 &&
              RangeReads(caret, L"Ada ") &&
              SUCCEEDED(IUIAutomationTextRange_Move(caret, TextUnit_Word, 5, &moved)) &&
              moved == 1 && RangeReads(caret, L"Lovelace"),
          "a word back, the space with it; forward no further than the last");
    CHECK(SUCCEEDED(IUIAutomationTextRange_MoveEndpointByUnit(caret, TextPatternRangeEndpoint_End,
                                                              TextUnit_Character, -6, &moved)) &&
              moved == -6 && RangeReads(caret, L"Lo") &&
              SUCCEEDED(IUIAutomationTextRange_Select(caret)) && Asked(mui_actionSetSelection, 7) &&
              s_program.asked.anchor == 4 && s_program.asked.focus == 6,
          "its end six characters back, selected through the host");
    if (caret != NULL)
    {
        IUIAutomationTextRange_Release(caret);
    }
    if (document != NULL)
    {
        IUIAutomationTextRange_Release(document);
    }
    if (text != NULL)
    {
        IUIAutomationTextPattern2_Release(text);
    }
}

static void CheckScrollAndValue(IUIAutomation* automation, IUIAutomationElement** children)
{
    IUIAutomationScrollPattern* scroll = NULL;
    double percent = 0.0;
    double view = 0.0;
    CHECK(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(children[8], UIA_ScrollPatternId,
                                                             &IID_IUIAutomationScrollPattern,
                                                             (void**)&scroll)) &&
              scroll != NULL &&
              SUCCEEDED(
                  IUIAutomationScrollPattern_get_CurrentVerticalScrollPercent(scroll, &percent)) &&
              SUCCEEDED(IUIAutomationScrollPattern_get_CurrentVerticalViewSize(scroll, &view)) &&
              percent == 25.0 && view > 33.3 && view < 33.4 &&
              SUCCEEDED(IUIAutomationScrollPattern_SetScrollPercent(scroll, -1.0, 50.0)) &&
              Asked(mui_actionSetScrollOffset, 11) && s_program.asked.y == 100.0f &&
              s_program.asked.x == 0.0f,
          "a list scrolled a quarter, set to half");
    IUIAutomationValuePattern* value = NULL;
    BSTR text = NULL;
    CHECK(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(
              children[4], UIA_ValuePatternId, &IID_IUIAutomationValuePattern, (void**)&value)) &&
              value != NULL &&
              SUCCEEDED(IUIAutomationValuePattern_get_CurrentValue(value, &text)) && text != NULL &&
              wcscmp(text, L"Ada Lovelace") == 0,
          "a text input's value");
    SysFreeString(text);
    IUIAutomationTreeWalker* walker = NULL;
    IUIAutomationElement* item = NULL;
    IUIAutomationSelectionItemPattern* selection = NULL;
    IUIAutomationElement* container = NULL;
    BOOL selected = FALSE;
    CHECK(SUCCEEDED(IUIAutomation_get_RawViewWalker(automation, &walker)) &&
              SUCCEEDED(IUIAutomationTreeWalker_GetFirstChildElement(walker, children[8], &item)) &&
              item != NULL &&
              SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(
                  item, UIA_SelectionItemPatternId, &IID_IUIAutomationSelectionItemPattern,
                  (void**)&selection)) &&
              selection != NULL &&
              SUCCEEDED(
                  IUIAutomationSelectionItemPattern_get_CurrentIsSelected(selection, &selected)) &&
              selected &&
              SUCCEEDED(IUIAutomationSelectionItemPattern_get_CurrentSelectionContainer(
                  selection, &container)) &&
              container != NULL && NameIs(container, L"Items"),
          "a selected item, in its list");
    if (container != NULL)
    {
        IUIAutomationElement_Release(container);
    }
    if (selection != NULL)
    {
        IUIAutomationSelectionItemPattern_Release(selection);
    }
    if (item != NULL)
    {
        IUIAutomationElement_Release(item);
    }
    if (walker != NULL)
    {
        IUIAutomationTreeWalker_Release(walker);
    }
    if (scroll != NULL)
    {
        IUIAutomationScrollPattern_Release(scroll);
    }
    if (value != NULL)
    {
        IUIAutomationValuePattern_Release(value);
    }
}

static void CheckPlaces(IUIAutomation* automation, IUIAutomationElement* button)
{
    POINT origin = {0, 0};
    (void)ClientToScreen(s_program.window, &origin);
    RECT rect = {0};
    CHECK(SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(button, &rect)) &&
              rect.left == origin.x + 15 && rect.top == origin.y + 15 &&
              rect.right == origin.x + 165 && rect.bottom == origin.y + 75,
          "bounds in screen pixels");
    POINT middle = {origin.x + 90, origin.y + 45};
    IUIAutomationElement* under = NULL;
    CHECK(SUCCEEDED(IUIAutomation_ElementFromPoint(automation, middle, &under)) && under != NULL &&
              NameIs(under, L"OK"),
          "the node under a point");
    if (under != NULL)
    {
        IUIAutomationElement_Release(under);
    }
    CHECK(SUCCEEDED(IUIAutomationElement_SetFocus(button)) && s_program.focusAsked == 2,
          "focusing asked of the host");
}

// The client's event handlers: COM objects of the test's own, called on
// UI Automation's threads.
typedef struct FocusHandler
{
    IUIAutomationFocusChangedEventHandler iface;
    LONG references;
} FocusHandler;

typedef struct PropertyHandler
{
    IUIAutomationPropertyChangedEventHandler iface;
    LONG references;
} PropertyHandler;

static HRESULT STDMETHODCALLTYPE FocusQuery(IUIAutomationFocusChangedEventHandler* self, REFIID id,
                                            void** out)
{
    if (IsEqualIID(id, &IID_IUnknown) || IsEqualIID(id, &IID_IUIAutomationFocusChangedEventHandler))
    {
        *out = self;
        InterlockedIncrement(&((FocusHandler*)self)->references);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE FocusAddRef(IUIAutomationFocusChangedEventHandler* self)
{
    return (ULONG)InterlockedIncrement(&((FocusHandler*)self)->references);
}

static ULONG STDMETHODCALLTYPE FocusRelease(IUIAutomationFocusChangedEventHandler* self)
{
    return (ULONG)InterlockedDecrement(&((FocusHandler*)self)->references);
}

static HRESULT STDMETHODCALLTYPE FocusChanged(IUIAutomationFocusChangedEventHandler* self,
                                              IUIAutomationElement* sender)
{
    (void)self;
    if (sender != NULL && NameIs(sender, L"Agree"))
    {
        InterlockedExchange(&s_program.focusSeen, 1);
    }
    if (sender != NULL && NameIs(sender, L"First"))
    {
        InterlockedExchange(&s_program.activeSeen, 1);
    }
    return S_OK;
}

static IUIAutomationFocusChangedEventHandlerVtbl s_focusTable = {FocusQuery, FocusAddRef,
                                                                 FocusRelease, FocusChanged};

static HRESULT STDMETHODCALLTYPE PropertyQuery(IUIAutomationPropertyChangedEventHandler* self,
                                               REFIID id, void** out)
{
    if (IsEqualIID(id, &IID_IUnknown) ||
        IsEqualIID(id, &IID_IUIAutomationPropertyChangedEventHandler))
    {
        *out = self;
        InterlockedIncrement(&((PropertyHandler*)self)->references);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE PropertyAddRef(IUIAutomationPropertyChangedEventHandler* self)
{
    return (ULONG)InterlockedIncrement(&((PropertyHandler*)self)->references);
}

static ULONG STDMETHODCALLTYPE PropertyRelease(IUIAutomationPropertyChangedEventHandler* self)
{
    return (ULONG)InterlockedDecrement(&((PropertyHandler*)self)->references);
}

static HRESULT STDMETHODCALLTYPE PropertyChanged(IUIAutomationPropertyChangedEventHandler* self,
                                                 IUIAutomationElement* sender, PROPERTYID property,
                                                 VARIANT value)
{
    (void)self;
    (void)sender;
    if (property == UIA_ToggleToggleStatePropertyId && value.vt == VT_I4 &&
        value.lVal == ToggleState_Off)
    {
        InterlockedExchange(&s_program.toggleSeen, 1);
    }
    return S_OK;
}

static IUIAutomationPropertyChangedEventHandlerVtbl s_propertyTable = {
    PropertyQuery, PropertyAddRef, PropertyRelease, PropertyChanged};

// Waits for a flag the handlers set.
static bool Seen(volatile LONG* flag)
{
    for (int i = 0; i < 100 && *flag == 0; i++)
    {
        Sleep(50);
    }
    return *flag != 0;
}

static void CheckEvents(IUIAutomation* automation, IUIAutomationElement* check)
{
    static FocusHandler s_focus = {{&s_focusTable}, 1};
    static PropertyHandler s_property = {{&s_propertyTable}, 1};
    PROPERTYID properties[1] = {UIA_ToggleToggleStatePropertyId};
    CHECK(SUCCEEDED(IUIAutomation_AddFocusChangedEventHandler(automation, NULL, &s_focus.iface)) &&
              SUCCEEDED(IUIAutomation_AddPropertyChangedEventHandlerNativeArray(
                  automation, check, TreeScope_Element, NULL, &s_property.iface, properties, 1)),
          "handlers added");
    SetEvent(s_program.change);
    CHECK(Seen(&s_program.toggleSeen), "the checkbox's state change raised");
    CHECK(Seen(&s_program.focusSeen), "the focus moving raised");
    CHECK(Seen(&s_program.activeSeen), "a focused list's active descendant raised as the focus");
    (void)IUIAutomation_RemoveAllEventHandlers(automation);
}

static void Inspect(IUIAutomation* automation)
{
    IUIAutomationElement* root = NULL;
    CHECK(SUCCEEDED(IUIAutomation_ElementFromHandle(automation, s_program.window, &root)) &&
              root != NULL,
          "the window's element");
    if (root == NULL)
    {
        return;
    }
    IUIAutomationElement* children[16] = {NULL};
    uint32_t count = Children(automation, root, children, 16);
    CheckChildren(children, count);
    if (count == 9)
    {
        CheckInvokeAndToggle(children);
        CheckRangeAndExpand(children);
        CheckScrollAndValue(automation, children);
        CheckText(children);
        CheckEvents(automation, children[5]);
    }
    if (count != 0)
    {
        CheckPlaces(automation, children[0]);
        SetEvent(s_program.removal);
        (void)WaitForSingleObject(s_program.removed, DEADLINE_MS);
        BSTR name = NULL;
        CHECK(IUIAutomationElement_get_CurrentName(children[0], &name) ==
                  (HRESULT)UIA_E_ELEMENTNOTAVAILABLE,
              "a node removed is no longer available");
        SysFreeString(name);
    }
    for (uint32_t i = 0; i < count; i++)
    {
        IUIAutomationElement_Release(children[i]);
    }
    IUIAutomationElement_Release(root);
}

// The client, which may not run on the window's thread.
static DWORD WINAPI Client(void* user)
{
    (void)user;
    IUIAutomation* automation = NULL;
    if (SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    {
        CHECK(SUCCEEDED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                         &IID_IUIAutomation, (void**)&automation)),
              "a client");
        if (automation != NULL)
        {
            Inspect(automation);
            IUIAutomation_Release(automation);
        }
        CoUninitialize();
    }
    SetEvent(s_program.done);
    return 0;
}

static HWND MakeWindow(void)
{
    WNDCLASSW type = {.lpfnWndProc = WindowProcedure,
                      .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"maul-ui uia test"};
    (void)RegisterClassW(&type);
    // A popup without a caption, so UI Automation finds no title bar
    // among the root's children.
    HWND window = CreateWindowExW(WS_EX_TOPMOST, type.lpszClassName, L"Maul UI", WS_POPUP, 100, 100,
                                  640, 480, NULL, NULL, type.hInstance, NULL);
    if (window != NULL)
    {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
    return window;
}

// The checkbox unchecked and focused.
static void Change(const Built* built)
{
    muiAccessNode check = built->nodes[7];
    check.flags = mui_accessCheckable;
    const muiAccessNode* sent[1] = {&check};
    const muiAccessUpdate update = {sent, 1, NULL, 0, 8};
    CHECK(muiUiaAdapter_Apply(s_program.adapter, &update) == mui_success, "changed");
    // The list focused, its item the active descendant: the item is the
    // focus shown, as browsers show aria-activedescendant.
    static const muiAccessLink s_active[1] = {{12, mui_relationActiveDescendant}};
    muiAccessNode list = built->nodes[10];
    list.links = s_active;
    list.linkCount = 1;
    const muiAccessNode* listSent[1] = {&list};
    const muiAccessUpdate active = {listSent, 1, built->children, 0, 11};
    CHECK(muiUiaAdapter_Apply(s_program.adapter, &active) == mui_success, "an active descendant");
}

// Pumps the window's messages until the client is done, changing the
// checkbox and removing the button when the client asks.
static void Pump(Built* built)
{
    const HANDLE waits[3] = {s_program.done, s_program.removal, s_program.change};
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < DEADLINE_MS)
    {
        DWORD woke = MsgWaitForMultipleObjects(3, waits, FALSE, 100, QS_ALLINPUT);
        if (woke == WAIT_OBJECT_0)
        {
            return;
        }
        if (woke == WAIT_OBJECT_0 + 2)
        {
            ResetEvent(s_program.change);
            Change(built);
        }
        if (woke == WAIT_OBJECT_0 + 1)
        {
            ResetEvent(s_program.removal);
            muiAccessNode root = built->nodes[0];
            root.firstChild = 1;
            root.childCount = 8;
            const muiAccessNode* sent[1] = {&root};
            const muiAccessUpdate update = {sent, 1, built->children, 0, 0};
            CHECK(muiUiaAdapter_Apply(s_program.adapter, &update) == mui_success, "removed");
            SetEvent(s_program.removed);
        }
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CHECK(false, "the client finished in time");
}

static void TestContract(void)
{
    muiUiaAdapterDef def = muiDefaultUiaAdapterDef();
    muiUiaAdapter* adapter = NULL;
    CHECK(muiCreateUiaAdapter(&def, &adapter) == mui_errorInvalid && adapter == NULL,
          "no window or action");
    def.window = s_program.window;
    def.action = Act;
    def.scale = 0.0f;
    CHECK(muiCreateUiaAdapter(&def, &adapter) == mui_errorInvalid, "no scale");
    CHECK(muiUiaAdapter_Apply(NULL, NULL) == mui_errorInvalid &&
              muiUiaAdapter_GetTree(NULL) == NULL && muiUiaAdapter_GetRoot(NULL) == NULL &&
              muiUiaAdapter_SetScale(NULL, 1.0f) == mui_errorInvalid,
          "NULL adapters");
    muiDestroyUiaAdapter(NULL);
}

int main(void)
{
    CHECK(SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)), "an STA");
    s_program.window = MakeWindow();
    CHECK(s_program.window != NULL, "a window");
    TestIds();
    TestContract();
    muiUiaAdapterDef def = muiDefaultUiaAdapterDef();
    def.window = s_program.window;
    def.scale = SCALE;
    def.action = Act;
    def.user = &s_program;
    CHECK(muiCreateUiaAdapter(&def, &s_program.adapter) == mui_success, "an adapter");
    CHECK(muiUiaAdapter_SetScale(s_program.adapter, 0.0f) == mui_errorInvalid &&
              muiUiaAdapter_GetRoot(s_program.adapter) != NULL,
          "the adapter's contract");
    static Built s_built;
    muiAccessUpdate update = Build(&s_built);
    CHECK(muiUiaAdapter_Apply(s_program.adapter, &update) == mui_success &&
              muiAccessTree_Count(muiUiaAdapter_GetTree(s_program.adapter)) == 12,
          "the tree");
    s_program.removal = CreateEventW(NULL, TRUE, FALSE, NULL);
    s_program.change = CreateEventW(NULL, TRUE, FALSE, NULL);
    s_program.removed = CreateEventW(NULL, TRUE, FALSE, NULL);
    s_program.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE client = CreateThread(NULL, 0, Client, NULL, 0, NULL);
    CHECK(client != NULL, "the client's thread");
    if (client != NULL)
    {
        Pump(&s_built);
        (void)WaitForSingleObject(client, DEADLINE_MS);
        CloseHandle(client);
    }
    muiUiaAdapter* adapter = s_program.adapter;
    s_program.adapter = NULL;
    muiDestroyUiaAdapter(adapter);
    DestroyWindow(s_program.window);
    CoUninitialize();
    return s_failures == 0 ? 0 : 1;
}
