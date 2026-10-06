// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter's property values (record mui-0008): control
// types from roles, texts as BSTRs, and states, positions and landmarks
// from a node's flags and values.

#include "allocator.h"
#include "uia.h"

#include <string.h>

// Landmark types and heading levels, from Windows 10's UI Automation,
// which MinGW's headers lack.
#define LANDMARK_CUSTOM     80000
#define LANDMARK_FORM       80001
#define LANDMARK_MAIN       80002
#define LANDMARK_NAVIGATION 80003
#define LANDMARK_SEARCH     80004
#define HEADING_LEVEL_1     80051

// Each role's control type; a role left out is a group.
static const uint16_t s_controlTypes[MUI_ROLE_LAST + 1] = {
    [mui_roleLabel] = UIA_TextControlTypeId,
    [mui_roleImage] = UIA_ImageControlTypeId,
    [mui_roleLink] = UIA_HyperlinkControlTypeId,
    [mui_roleButton] = UIA_ButtonControlTypeId,
    [mui_roleDefaultButton] = UIA_ButtonControlTypeId,
    [mui_roleCheckBox] = UIA_CheckBoxControlTypeId,
    [mui_roleRadioButton] = UIA_RadioButtonControlTypeId,
    [mui_roleSwitch] = UIA_ButtonControlTypeId,
    [mui_roleTextInput] = UIA_EditControlTypeId,
    [mui_roleMultilineTextInput] = UIA_EditControlTypeId,
    [mui_roleSearchInput] = UIA_EditControlTypeId,
    [mui_rolePasswordInput] = UIA_EditControlTypeId,
    [mui_roleNumberInput] = UIA_EditControlTypeId,
    [mui_roleEmailInput] = UIA_EditControlTypeId,
    [mui_rolePhoneNumberInput] = UIA_EditControlTypeId,
    [mui_roleUrlInput] = UIA_EditControlTypeId,
    [mui_roleDateInput] = UIA_EditControlTypeId,
    [mui_roleTimeInput] = UIA_EditControlTypeId,
    [mui_roleDateTimeInput] = UIA_EditControlTypeId,
    [mui_roleComboBox] = UIA_ComboBoxControlTypeId,
    [mui_roleEditableComboBox] = UIA_ComboBoxControlTypeId,
    [mui_roleListBox] = UIA_ListControlTypeId,
    [mui_roleListBoxOption] = UIA_ListItemControlTypeId,
    [mui_roleList] = UIA_ListControlTypeId,
    [mui_roleListItem] = UIA_ListItemControlTypeId,
    [mui_roleTree] = UIA_TreeControlTypeId,
    [mui_roleTreeItem] = UIA_TreeItemControlTypeId,
    [mui_roleTreeGrid] = UIA_DataGridControlTypeId,
    [mui_roleTable] = UIA_TableControlTypeId,
    [mui_roleRow] = UIA_DataItemControlTypeId,
    [mui_roleCell] = UIA_DataItemControlTypeId,
    [mui_roleRowHeader] = UIA_HeaderItemControlTypeId,
    [mui_roleColumnHeader] = UIA_HeaderItemControlTypeId,
    [mui_roleGrid] = UIA_DataGridControlTypeId,
    [mui_roleGridCell] = UIA_DataItemControlTypeId,
    [mui_roleMenu] = UIA_MenuControlTypeId,
    [mui_roleMenuBar] = UIA_MenuBarControlTypeId,
    [mui_roleMenuItem] = UIA_MenuItemControlTypeId,
    [mui_roleMenuItemCheckBox] = UIA_MenuItemControlTypeId,
    [mui_roleMenuItemRadio] = UIA_MenuItemControlTypeId,
    [mui_roleTab] = UIA_TabItemControlTypeId,
    [mui_roleTabList] = UIA_TabControlTypeId,
    [mui_roleTabPanel] = UIA_PaneControlTypeId,
    [mui_roleToolbar] = UIA_ToolBarControlTypeId,
    [mui_roleTooltip] = UIA_ToolTipControlTypeId,
    [mui_roleDialog] = UIA_WindowControlTypeId,
    [mui_roleAlertDialog] = UIA_WindowControlTypeId,
    [mui_roleAlert] = UIA_TextControlTypeId,
    [mui_roleStatus] = UIA_StatusBarControlTypeId,
    [mui_roleProgressIndicator] = UIA_ProgressBarControlTypeId,
    [mui_roleMeter] = UIA_ProgressBarControlTypeId,
    [mui_roleSlider] = UIA_SliderControlTypeId,
    [mui_roleSpinButton] = UIA_SpinnerControlTypeId,
    [mui_roleScrollBar] = UIA_ScrollBarControlTypeId,
    [mui_roleScrollView] = UIA_PaneControlTypeId,
    [mui_roleSplitter] = UIA_SeparatorControlTypeId,
    [mui_rolePane] = UIA_PaneControlTypeId,
    [mui_roleWindow] = UIA_WindowControlTypeId,
    [mui_roleTitleBar] = UIA_TitleBarControlTypeId,
    [mui_roleHeading] = UIA_TextControlTypeId,
    [mui_roleParagraph] = UIA_TextControlTypeId,
    [mui_roleDocument] = UIA_DocumentControlTypeId,
    [mui_roleApplication] = UIA_PaneControlTypeId,
    [mui_roleCaption] = UIA_TextControlTypeId,
    [mui_roleDisclosureTriangle] = UIA_ButtonControlTypeId,
    [mui_roleCanvas] = UIA_ImageControlTypeId,
    [mui_roleColorWell] = UIA_ButtonControlTypeId,
    [mui_roleTerminal] = UIA_DocumentControlTypeId,
    [mui_roleFeed] = UIA_ListControlTypeId,
    [mui_roleMarquee] = UIA_TextControlTypeId,
};

static int ControlTypeOf(muiRole role)
{
    int type = role <= MUI_ROLE_LAST ? s_controlTypes[role] : 0;
    return type != 0 ? type : UIA_GroupControlTypeId;
}

// A landmark's type, 0 for none, and the name of a custom one.
static int LandmarkOf(muiRole role, const char** nameOut)
{
    *nameOut = nullptr;
    switch (role)
    {
    case mui_roleNavigation:
        return LANDMARK_NAVIGATION;
    case mui_roleMain:
        return LANDMARK_MAIN;
    case mui_roleSearch:
        return LANDMARK_SEARCH;
    case mui_roleForm:
        return LANDMARK_FORM;
    case mui_roleBanner:
        *nameOut = "banner";
        return LANDMARK_CUSTOM;
    case mui_roleComplementary:
        *nameOut = "complementary";
        return LANDMARK_CUSTOM;
    case mui_roleContentInfo:
        *nameOut = "content information";
        return LANDMARK_CUSTOM;
    case mui_roleRegion:
        *nameOut = "region";
        return LANDMARK_CUSTOM;
    default:
        return 0;
    }
}

static void SetBool(VARIANT* out, bool value)
{
    out->vt = VT_BOOL;
    out->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

static void SetInt(VARIANT* out, int value)
{
    out->vt = VT_I4;
    out->lVal = value;
}

// UTF-8 as a BSTR; left empty when there is none or memory runs out.
static HRESULT SetText(VARIANT* out, const char* text, size_t length)
{
    if (text == nullptr || length == 0 || length > INT32_MAX)
    {
        return S_OK;
    }
    int wide = MultiByteToWideChar(CP_UTF8, 0, text, (int)length, nullptr, 0);
    BSTR string = wide > 0 ? SysAllocStringLen(nullptr, (UINT)wide) : nullptr;
    if (string == nullptr)
    {
        return wide > 0 ? E_OUTOFMEMORY : S_OK;
    }
    (void)MultiByteToWideChar(CP_UTF8, 0, text, (int)length, string, wide);
    out->vt = VT_BSTR;
    out->bstrVal = string;
    return S_OK;
}

static HRESULT SetNodeText(VARIANT* out, const muiAccessNode* node, muiAccessTextKind kind)
{
    return SetText(out, node->text[kind], node->textLength[kind]);
}

static HRESULT SetName(VARIANT* out, const muiUiaAdapter* adapter, uint64_t id)
{
    size_t length = 0;
    if (muiAccessTree_GetName(adapter->tree, id, nullptr, 0, &length) != mui_errorCapacity)
    {
        return S_OK;
    }
    char* name = muiAllocate(&adapter->allocator, length + 1, 1);
    if (name == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT result =
        muiAccessTree_GetName(adapter->tree, id, name, length + 1, &length) == mui_success
            ? SetText(out, name, length)
            : S_OK;
    muiRelease(&adapter->allocator, name, length + 1, 1);
    return result;
}

// Whether a clipping ancestor shows none of a node.
static bool IsOffscreen(const muiUiaAdapter* adapter, uint64_t id)
{
    const muiAccessTree* tree = adapter->tree;
    muiRect box = {0};
    (void)muiAccessTree_GetBounds(tree, id, &box);
    uint32_t steps = 0;
    for (uint64_t at = muiAccessTree_GetParent(tree, id); at != 0 && steps < adapter->nodes;
         at = muiAccessTree_GetParent(tree, at), steps++)
    {
        muiRect clip = {0};
        if ((muiAccessTree_Find(tree, at)->flags & mui_accessClipsChildren) != 0 &&
            muiAccessTree_GetBounds(tree, at, &clip) == mui_success &&
            (box.x + box.width <= clip.x || box.x >= clip.x + clip.width ||
             box.y + box.height <= clip.y || box.y >= clip.y + clip.height))
        {
            return true;
        }
    }
    return false;
}

// The properties taken from a node's texts.
static bool TextProperty(VARIANT* out, const muiAccessNode* node, PROPERTYID property,
                         HRESULT* result)
{
    const char* landmark = nullptr;
    switch (property)
    {
    case UIA_HelpTextPropertyId:
    case UIA_FullDescriptionPropertyId:
        *result = SetNodeText(out, node, mui_accessDescription);
        return true;
    case UIA_LocalizedControlTypePropertyId:
        *result = SetNodeText(out, node, mui_accessRoleDescription);
        return true;
    case UIA_ItemStatusPropertyId:
        *result = SetNodeText(out, node, mui_accessStateDescription);
        return true;
    case UIA_AcceleratorKeyPropertyId:
        *result = SetNodeText(out, node, mui_accessKeyboardShortcut);
        return true;
    case UIA_LocalizedLandmarkTypePropertyId:
        (void)LandmarkOf(node->role, &landmark);
        *result = node->text[mui_accessRoleDescription] != nullptr
                      ? SetNodeText(out, node, mui_accessRoleDescription)
                      : SetText(out, landmark, landmark != nullptr ? strlen(landmark) : 0);
        return true;
    default:
        return false;
    }
}

// The properties taken from a node's flags.
static bool StateProperty(VARIANT* out, const muiUiaAdapter* adapter, const muiAccessNode* node,
                          PROPERTYID property)
{
    switch (property)
    {
    case UIA_IsKeyboardFocusablePropertyId:
        SetBool(out, (node->flags & mui_accessFocusable) != 0);
        return true;
    case UIA_HasKeyboardFocusPropertyId:
        SetBool(out, node->id == muiAccessTree_GetFocus(adapter->tree));
        return true;
    case UIA_IsEnabledPropertyId:
        SetBool(out, (node->flags & mui_accessDisabled) == 0);
        return true;
    case UIA_IsOffscreenPropertyId:
        SetBool(out, IsOffscreen(adapter, node->id));
        return true;
    case UIA_IsPasswordPropertyId:
        SetBool(out, node->role == mui_rolePasswordInput);
        return true;
    case UIA_IsRequiredForFormPropertyId:
        SetBool(out, (node->flags & mui_accessRequired) != 0);
        return true;
    case UIA_IsDialogPropertyId:
        SetBool(out, node->role == mui_roleDialog || node->role == mui_roleAlertDialog);
        return true;
    default:
        return false;
    }
}

// The properties taken from a node's typed values; none for a value the
// node lacks.
static bool ValueProperty(VARIANT* out, const muiAccessNode* node, PROPERTYID property)
{
    const muiAccessValues* values = &node->values;
    const char* custom = nullptr;
    int number = 0;
    switch (property)
    {
    case UIA_LevelPropertyId:
        number = (int)values->level;
        break;
    case UIA_PositionInSetPropertyId:
        number = (int)values->setPosition;
        break;
    case UIA_SizeOfSetPropertyId:
        number = (int)values->setSize;
        break;
    case UIA_HeadingLevelPropertyId:
        number = node->role == mui_roleHeading && values->level >= 1 && values->level <= 9
                     ? HEADING_LEVEL_1 + (int)values->level - 1
                     : 0;
        break;
    case UIA_LandmarkTypePropertyId:
        number = LandmarkOf(node->role, &custom);
        break;
    case UIA_OrientationPropertyId:
        // Maul UI's orientations are UI Automation's.
        number = (int)values->orientation;
        break;
    case UIA_LiveSettingPropertyId:
        number = (int)values->live;
        break;
    default:
        return false;
    }
    if (number != 0)
    {
        SetInt(out, number);
    }
    return true;
}

HRESULT muiUiaPropertyValue(muiUiaAdapter* adapter, const muiAccessNode* node, PROPERTYID property,
                            VARIANT* out)
{
    HRESULT result = S_OK;
    if (property == UIA_ControlTypePropertyId)
    {
        SetInt(out, ControlTypeOf(node->role));
    }
    else if (property == UIA_NamePropertyId)
    {
        result = SetName(out, adapter, node->id);
    }
    else if (!TextProperty(out, node, property, &result))
    {
        (void)(StateProperty(out, adapter, node, property) || ValueProperty(out, node, property));
    }
    return result;
}
