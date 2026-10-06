// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ARIA adapter's attributes (record mui-0008): each Maul UI role's
// ARIA role, and every attribute of a node's element from its record,
// written whole or where an old record and the new one differ.

#include "aria.h"

#include <stdio.h>
#include <string.h>

// Each role's ARIA role; "" for none (static text). ARIA has no media,
// window or scroll view roles: those are groups.
static const char* const s_roles[MUI_ROLE_LAST + 1] = {
    [mui_roleGeneric] = "group",
    [mui_roleLabel] = "",
    [mui_roleImage] = "img",
    [mui_roleLink] = "link",
    [mui_roleButton] = "button",
    [mui_roleDefaultButton] = "button",
    [mui_roleCheckBox] = "checkbox",
    [mui_roleRadioButton] = "radio",
    [mui_roleRadioGroup] = "radiogroup",
    [mui_roleSwitch] = "switch",
    [mui_roleTextInput] = "textbox",
    [mui_roleMultilineTextInput] = "textbox",
    [mui_roleSearchInput] = "searchbox",
    [mui_rolePasswordInput] = "textbox",
    [mui_roleNumberInput] = "spinbutton",
    [mui_roleEmailInput] = "textbox",
    [mui_rolePhoneNumberInput] = "textbox",
    [mui_roleUrlInput] = "textbox",
    [mui_roleDateInput] = "textbox",
    [mui_roleTimeInput] = "textbox",
    [mui_roleDateTimeInput] = "textbox",
    [mui_roleComboBox] = "combobox",
    [mui_roleEditableComboBox] = "combobox",
    [mui_roleListBox] = "listbox",
    [mui_roleListBoxOption] = "option",
    [mui_roleList] = "list",
    [mui_roleListItem] = "listitem",
    [mui_roleTree] = "tree",
    [mui_roleTreeItem] = "treeitem",
    [mui_roleTreeGrid] = "treegrid",
    [mui_roleTable] = "table",
    [mui_roleRow] = "row",
    [mui_roleCell] = "cell",
    [mui_roleRowHeader] = "rowheader",
    [mui_roleColumnHeader] = "columnheader",
    [mui_roleRowGroup] = "rowgroup",
    [mui_roleGrid] = "grid",
    [mui_roleGridCell] = "gridcell",
    [mui_roleMenu] = "menu",
    [mui_roleMenuBar] = "menubar",
    [mui_roleMenuItem] = "menuitem",
    [mui_roleMenuItemCheckBox] = "menuitemcheckbox",
    [mui_roleMenuItemRadio] = "menuitemradio",
    [mui_roleTab] = "tab",
    [mui_roleTabList] = "tablist",
    [mui_roleTabPanel] = "tabpanel",
    [mui_roleToolbar] = "toolbar",
    [mui_roleTooltip] = "tooltip",
    [mui_roleDialog] = "dialog",
    [mui_roleAlertDialog] = "alertdialog",
    [mui_roleAlert] = "alert",
    [mui_roleStatus] = "status",
    [mui_roleLog] = "log",
    [mui_roleTimer] = "timer",
    [mui_roleProgressIndicator] = "progressbar",
    [mui_roleMeter] = "meter",
    [mui_roleSlider] = "slider",
    [mui_roleSpinButton] = "spinbutton",
    [mui_roleScrollBar] = "scrollbar",
    [mui_roleScrollView] = "group",
    [mui_roleSplitter] = "separator",
    [mui_roleGroup] = "group",
    [mui_rolePane] = "group",
    [mui_roleWindow] = "group",
    [mui_roleTitleBar] = "group",
    [mui_roleHeading] = "heading",
    [mui_roleParagraph] = "paragraph",
    [mui_roleRegion] = "region",
    [mui_roleNavigation] = "navigation",
    [mui_roleMain] = "main",
    [mui_roleBanner] = "banner",
    [mui_roleComplementary] = "complementary",
    [mui_roleContentInfo] = "contentinfo",
    [mui_roleSearch] = "search",
    [mui_roleForm] = "form",
    [mui_roleArticle] = "article",
    [mui_roleDocument] = "document",
    [mui_roleApplication] = "application",
    [mui_roleFigure] = "figure",
    [mui_roleCaption] = "caption",
    [mui_roleNote] = "note",
    [mui_roleDetails] = "group",
    [mui_roleDisclosureTriangle] = "button",
    [mui_roleCanvas] = "img",
    [mui_roleVideo] = "group",
    [mui_roleAudio] = "group",
    [mui_roleColorWell] = "button",
    [mui_roleTerminal] = "log",
    [mui_roleFeed] = "feed",
    [mui_roleMarquee] = "marquee",
};

// The attributes written, in this order.
enum
{
    A_role,
    A_label,
    A_description,
    A_roleDescription,
    A_placeholder,
    A_keyShortcuts,
    A_disabled,
    A_checked,
    A_pressed,
    A_expanded,
    A_selected,
    A_required,
    A_readOnly,
    A_busy,
    A_modal,
    A_multiselectable,
    A_hasPopup,
    A_invalid,
    A_level,
    A_positionInSet,
    A_setSize,
    A_orientation,
    A_valueNow,
    A_valueMin,
    A_valueMax,
    A_valueText,
    A_tabIndex,
    A_min,
    A_max,
    A_step,
    A_value,
    A_count
};

static const char* const s_names[A_count] = {
    [A_role] = "role",
    [A_label] = "aria-label",
    [A_description] = "aria-description",
    [A_roleDescription] = "aria-roledescription",
    [A_placeholder] = "aria-placeholder",
    [A_keyShortcuts] = "aria-keyshortcuts",
    [A_disabled] = "aria-disabled",
    [A_checked] = "aria-checked",
    [A_pressed] = "aria-pressed",
    [A_expanded] = "aria-expanded",
    [A_selected] = "aria-selected",
    [A_required] = "aria-required",
    [A_readOnly] = "aria-readonly",
    [A_busy] = "aria-busy",
    [A_modal] = "aria-modal",
    [A_multiselectable] = "aria-multiselectable",
    [A_hasPopup] = "aria-haspopup",
    [A_invalid] = "aria-invalid",
    [A_level] = "aria-level",
    [A_positionInSet] = "aria-posinset",
    [A_setSize] = "aria-setsize",
    [A_orientation] = "aria-orientation",
    [A_valueNow] = "aria-valuenow",
    [A_valueMin] = "aria-valuemin",
    [A_valueMax] = "aria-valuemax",
    [A_valueText] = "aria-valuetext",
    [A_tabIndex] = "tabindex",
    [A_min] = "min",
    [A_max] = "max",
    [A_step] = "step",
    [A_value] = "value",
};

static const char* const s_popups[] = {nullptr, "menu", "listbox", "tree", "grid", "dialog"};
static const char* const s_invalid[] = {nullptr, "true", "grammar", "spelling"};
static const char* const s_orientations[] = {nullptr, "horizontal", "vertical"};

bool muiAriaIsRange(const muiAccessNode* node)
{
    return (node->role == mui_roleSlider || node->role == mui_roleSpinButton) &&
           (node->flags & mui_accessNumeric) != 0 &&
           (node->actions & (1u << mui_actionSetValue)) != 0;
}

// The text a node names itself by: its label, or a label node's value.
static const char* NameOf(const muiAccessNode* node)
{
    return node->text[mui_accessLabel] == nullptr && node->role == mui_roleLabel
               ? node->text[mui_accessValue]
               : node->text[mui_accessLabel];
}

// Whether a node's name is its element's text, as ARIA takes the name
// from content for these roles: a leaf only, as the text would replace
// children.
static bool NameIsText(const muiAccessNode* node)
{
    muiRole role = node->role;
    bool fromContent = role == mui_roleLabel || role == mui_roleHeading ||
                       role == mui_roleParagraph || role == mui_roleLink ||
                       role == mui_roleButton || role == mui_roleDefaultButton;
    return fromContent && node->childCount == 0;
}

static const char* Flag(const muiAccessNode* node, uint32_t flag)
{
    return (node->flags & flag) != 0 ? "true" : nullptr;
}

static const char* Either(bool on)
{
    return on ? "true" : "false";
}

// A number written shortest, as "%g" does to nine digits; NULL for 0
// when zero means none.
static const char* Number(char buffer[32], double value, bool zeroIsNone)
{
    if (zeroIsNone && value == 0.0)
    {
        return nullptr;
    }
    (void)snprintf(buffer, 32, "%.9g", value);
    return buffer;
}

// A check box's state, or a toggle button's.
static const char* Checked(const muiAccessNode* node)
{
    uint32_t flags = node->flags;
    if ((flags & mui_accessCheckable) == 0)
    {
        return nullptr;
    }
    return (flags & mui_accessMixed) != 0 ? "mixed" : Either((flags & mui_accessChecked) != 0);
}

static bool IsToggleButton(const muiAccessNode* node)
{
    return node->role == mui_roleButton || node->role == mui_roleDefaultButton;
}

// The attributes of the record alone, in the order of s_names.
static const char* StateOf(const muiAccessNode* node, uint32_t which)
{
    uint32_t flags = node->flags;
    switch (which)
    {
    case A_disabled:
        return Flag(node, mui_accessDisabled);
    case A_checked:
        return IsToggleButton(node) ? nullptr : Checked(node);
    case A_pressed:
        return IsToggleButton(node) ? Checked(node) : nullptr;
    case A_expanded:
        return (flags & mui_accessExpandable) != 0 ? Either((flags & mui_accessExpanded) != 0)
                                                   : nullptr;
    case A_selected:
        return (flags & mui_accessSelectable) != 0 ? Either((flags & mui_accessSelected) != 0)
                                                   : nullptr;
    case A_required:
        return Flag(node, mui_accessRequired);
    case A_readOnly:
        return Flag(node, mui_accessReadOnly);
    case A_busy:
        return Flag(node, mui_accessBusy);
    case A_modal:
        return Flag(node, mui_accessModal);
    case A_multiselectable:
        return Flag(node, mui_accessMultiselectable);
    case A_hasPopup:
        return node->values.popup < 6 ? s_popups[node->values.popup] : nullptr;
    case A_invalid:
        return node->values.invalid < 4 ? s_invalid[node->values.invalid] : nullptr;
    case A_orientation:
        return node->values.orientation < 3 ? s_orientations[node->values.orientation] : nullptr;
    case A_tabIndex:
        return (flags & mui_accessFocusable) != 0 ? "-1" : nullptr;
    default:
        return nullptr;
    }
}

// A text of the record, or the name where it is not the element's text.
static const char* TextOf(const muiAccessNode* node, uint32_t which)
{
    switch (which)
    {
    case A_label:
        return NameIsText(node) ? nullptr : NameOf(node);
    case A_description:
        return node->text[mui_accessDescription];
    case A_roleDescription:
        return node->text[mui_accessRoleDescription];
    case A_placeholder:
        return node->text[mui_accessPlaceholder];
    case A_keyShortcuts:
        return node->text[mui_accessKeyboardShortcut];
    case A_valueText:
        return node->role == mui_roleLabel ? nullptr : node->text[mui_accessValue];
    default:
        return nullptr;
    }
}

// The numbers of the record: a set's, and a range's, which an input of
// type range takes as its own.
static const char* NumberOf(const muiAccessNode* node, uint32_t which, char buffer[32])
{
    bool numeric = (node->flags & mui_accessNumeric) != 0;
    bool range = muiAriaIsRange(node);
    bool aria = numeric && !range;
    switch (which)
    {
    case A_level:
        return Number(buffer, node->values.level, true);
    case A_positionInSet:
        return Number(buffer, node->values.setPosition, true);
    case A_setSize:
        return Number(buffer, node->values.setSize, true);
    case A_valueNow:
        return aria ? Number(buffer, (double)node->value, false) : nullptr;
    case A_valueMin:
        return aria ? Number(buffer, (double)node->minimum, false) : nullptr;
    case A_valueMax:
        return aria ? Number(buffer, (double)node->maximum, false) : nullptr;
    case A_min:
        return range ? Number(buffer, (double)node->minimum, false) : nullptr;
    case A_max:
        return range ? Number(buffer, (double)node->maximum, false) : nullptr;
    case A_step:
        return range && node->step > 0.0f ? Number(buffer, (double)node->step, false) : nullptr;
    case A_value:
        return range ? Number(buffer, (double)node->value, false) : nullptr;
    default:
        return nullptr;
    }
}

// An attribute's value for a record, NULL for none.
static const char* ValueOf(const muiAccessNode* node, uint32_t which, char buffer[32])
{
    if (which == A_role)
    {
        // A range input is a slider already; saying so again is harmless.
        const char* role = node->role <= MUI_ROLE_LAST ? s_roles[node->role] : "group";
        return role[0] != '\0' ? role : nullptr;
    }
    const char* text = TextOf(node, which);
    if (text != nullptr)
    {
        return text;
    }
    const char* state = StateOf(node, which);
    return state != nullptr ? state : NumberOf(node, which, buffer);
}

static bool Same(const char* a, const char* b)
{
    return a == b || (a != nullptr && b != nullptr && strcmp(a, b) == 0);
}

void muiAriaWriteAttributes(const muiAriaAdapter* adapter, uint32_t slot, const muiAccessNode* old,
                            const muiAccessNode* node)
{
    for (uint32_t which = 0; which < A_count; which++)
    {
        char before[32];
        char after[32];
        const char* value = ValueOf(node, which, after);
        if (old == nullptr ? value != nullptr : !Same(ValueOf(old, which, before), value))
        {
            muiAriaPageAttribute(adapter->page, slot, s_names[which], value);
        }
    }
    bool text = NameIsText(node);
    bool wasText = old != nullptr && NameIsText(old);
    if (text ? !wasText || !Same(NameOf(old), NameOf(node)) : wasText)
    {
        muiAriaPageText(adapter->page, slot, text ? NameOf(node) : nullptr);
    }
}
