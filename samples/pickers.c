// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A select, a searchable select and a number input with drag scrub,
// composed over Maul UI's public capabilities with the patterns of the
// WAI-ARIA Authoring Practices (record mui-0005). Both selects are
// comboboxes: the focus stays on them while their list, a popup in the
// overlay layer (maul-ui/popup.h) made when it opens and destroyed when
// it closes, shows the active option, which the combobox names by the
// active descendant relation. The select opens by a click, Space, Enter
// or Down; arrows move the active option and Enter picks it; the
// library's light dismissal (a press outside, an unhandled Escape)
// reports when the list should close. The searchable select is a text
// field whose typing filters the options. The number input is a range
// (maul-ui/range.h) the library moves by ARIA's keys, its drag taken by
// the host, which scrubs the value by the pointer's travel instead of
// following it. Headless, each is used as a person would and the
// texts, the pixels and the accessibility tree checked.

#include "app.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"
#include "maul-ui/popup.h"
#include "maul-ui/range.h"
#include "maul-ui/style.h"
#include "maul-ui/text_edit.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define QUALITIES 4
#define FRUITS    8

// Pixels of travel per step of the number input's scrub.
#define SCRUB 2.0f

static const char* const s_qualities[QUALITIES] = {"Low", "Medium", "High", "Ultra"};
static const char* const s_fruits[FRUITS] = {"Apple", "Apricot", "Banana", "Cherry",
                                             "Grape", "Lemon",   "Mango",  "Orange"};

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_text[3] = {242, 242, 242};
static const uint8_t s_field[3] = {48, 56, 64};
static const uint8_t s_list[3] = {64, 72, 84};
static const uint8_t s_active[3] = {48, 112, 224};
static const uint8_t s_caret[3] = {255, 200, 64};

// A combobox: the node, the text it shows, and its list while open.
typedef struct Combo
{
    muiNodeId node;
    muiTextBlockId block;
    const char* const* options;
    int optionCount;
    // The options the list shows, by index into options, and the active
    // one among them; the list is the null id while closed.
    int shown[FRUITS];
    int shownCount;
    int active;
    muiNodeId list;
    muiNodeId items[FRUITS];
    // The option picked, or -1.
    int picked;
} Combo;

typedef struct Pickers
{
    Combo select;
    Combo search;
    muiNodeId number;
    muiNodeId numberLabel;
    muiTextBlockId numberBlock;
    float scrubStart;
    float shownValue;
    muiStyleId optionStyle;
    SampleApp* app;
} Pickers;

static void SetText(SampleApp* app, muiNodeId node, muiTextBlockId block, const char* text)
{
    SampleAppCheck(app,
                   muiTextBlock_SetText(app->text, block, text, strlen(text)) == mui_success &&
                       muiNode_MarkContentChanged(app->context, node) == mui_success,
                   "a text");
}

static const char* TextOf(const SampleApp* app, muiTextBlockId block, size_t* lengthOut)
{
    const char* text = "";
    *lengthOut = 0;
    (void)muiTextBlock_GetText(app->text, block, &text, lengthOut);
    return text;
}

// Whether an option holds the text typed, ignoring case.
static bool Matches(const char* option, const char* typed, size_t length)
{
    size_t size = strlen(option);
    for (size_t at = 0; at + length <= size; at++)
    {
        size_t i = 0;
        while (i < length &&
               tolower((unsigned char)option[at + i]) == tolower((unsigned char)typed[i]))
        {
            i++;
        }
        if (i == length)
        {
            return true;
        }
    }
    return false;
}

// The combobox names its active option and says whether it is open.
static void Report(SampleApp* app, Combo* combo)
{
    bool open = combo->list.index1 != 0;
    muiAccessFlags flags = mui_accessExpandable | (open ? mui_accessExpanded : 0);
    muiNodeId active = open && combo->active >= 0 ? combo->items[combo->active] : (muiNodeId){0, 0};
    SampleAppCheck(app,
                   muiNode_SetAccessFlags(app->context, combo->node, flags) == mui_success &&
                       muiNode_SetAccessRelation(app->context, combo->node,
                                                 mui_relationActiveDescendant, &active,
                                                 active.index1 != 0 ? 1u : 0u) == mui_success,
                   "the combobox's state");
}

// Marks the active option selected, which its class shows.
static void Activate(SampleApp* app, Combo* combo, int active)
{
    combo->active = active;
    for (int i = 0; i < combo->shownCount; i++)
    {
        SampleAppCheck(app,
                       muiNode_SetStates(app->context, combo->items[i],
                                         i == active ? mui_stateSelected : 0) == mui_success,
                       "an option's state");
    }
    Report(app, combo);
}

// Destroys the list, keeping what it showed.
static void Destroy(SampleApp* app, Combo* combo)
{
    if (combo->list.index1 != 0)
    {
        SampleAppCheck(app, muiDestroyNode(app->context, combo->list) == mui_success,
                       "the list closed");
    }
    combo->list = (muiNodeId){0, 0};
}

static void Close(SampleApp* app, Combo* combo)
{
    Destroy(app, combo);
    combo->shownCount = 0;
    combo->active = -1;
    Report(app, combo);
}

// Opens the list below the combobox with the options shown, the active
// one first marked; none shown keeps it closed.
static void Open(Pickers* pickers, Combo* combo, int active)
{
    SampleApp* app = pickers->app;
    Destroy(app, combo);
    if (combo->shownCount == 0)
    {
        Close(app, combo);
        return;
    }
    muiRect anchor = muiNode_GetRect(app->context, combo->node);
    combo->list = SampleNode(app, app->root, anchor.width, 0.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.placement.position = mui_positionAbsolute;
    layout.padding = (muiEdges){0.0f, 0.0f, 4.0f, 4.0f};
    SampleSetLayout(
        app, combo->list, &layout,
        MUI_PROPERTY_BIT(mui_propertyFlexDirection) | MUI_PROPERTY_BIT(mui_propertyPosition) |
            MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
    SampleFill(app, combo->list, s_list);
    SampleRound(app, combo->list, 4.0f);
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.layer = mui_layerOverlay;
    muiPopup popup = muiDefaultPopup();
    popup.anchor = combo->node;
    popup.gap = 2.0f;
    popup.margin = 4.0f;
    SampleAppCheck(
        app,
        muiNode_SetInteractionValues(app->context, combo->list, &interaction,
                                     MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success &&
            muiNode_SetPopup(app->context, combo->list, &popup) == mui_success &&
            muiNode_SetAccessRole(app->context, combo->list, mui_roleListBox) == mui_success,
        "the list");
    for (int i = 0; i < combo->shownCount; i++)
    {
        const char* name = combo->options[combo->shown[i]];
        combo->items[i] = SampleLabel(app, combo->list, name, 16.0f, s_text);
        layout = muiDefaultLayoutStyle();
        layout.padding = (muiEdges){10.0f, 10.0f, 3.0f, 3.0f};
        SampleSetLayout(app, combo->items[i], &layout,
                        MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                            MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
                            MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
                            MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
        SampleAppCheck(app,
                       muiNode_SetClasses(app->context, combo->items[i], &pickers->optionStyle,
                                          1) == mui_success &&
                           muiNode_SetAccessRole(app->context, combo->items[i],
                                                 mui_roleListBoxOption) == mui_success,
                       "an option");
    }
    Activate(app, combo, active);
}

// Picks an option: the combobox shows it and its list closes.
static void Pick(Pickers* pickers, Combo* combo, int shown)
{
    SampleApp* app = pickers->app;
    combo->picked = combo->shown[shown];
    const char* name = combo->options[combo->picked];
    SetText(app, combo->node, combo->block, name);
    SampleAppCheck(app,
                   muiNode_SetAccessText(app->context, combo->node, mui_accessValue, name,
                                         strlen(name)) == mui_success,
                   "the combobox's value");
    Close(app, combo);
}

// Shows every option, the picked one active.
static void OpenAll(Pickers* pickers, Combo* combo)
{
    combo->shownCount = combo->optionCount;
    for (int i = 0; i < combo->optionCount; i++)
    {
        combo->shown[i] = i;
    }
    Open(pickers, combo, combo->picked >= 0 ? combo->picked : 0);
}

// Shows the options holding what the field holds, none active until an
// arrow moves into the list.
static void Filter(Pickers* pickers, Combo* combo)
{
    size_t length = 0;
    const char* typed = TextOf(pickers->app, combo->block, &length);
    combo->shownCount = 0;
    for (int i = 0; i < combo->optionCount && length > 0; i++)
    {
        if (Matches(combo->options[i], typed, length))
        {
            combo->shown[combo->shownCount++] = i;
        }
    }
    Open(pickers, combo, -1);
}

// Up and Down move the active option, wrapping; Enter picks it.
static bool ListKey(Pickers* pickers, Combo* combo, const muiEvent* event)
{
    int count = combo->shownCount;
    switch (event->code)
    {
    case mui_codeArrowDown:
        Activate(pickers->app, combo, (combo->active + 1) % count);
        return true;
    case mui_codeArrowUp:
        Activate(pickers->app, combo, combo->active <= 0 ? count - 1 : combo->active - 1);
        return true;
    case mui_codeEnter:
        if (combo->active >= 0)
        {
            Pick(pickers, combo, combo->active);
        }
        return true;
    default:
        return false;
    }
}

// The select-only combobox: a click, Space, Enter or Down opens it.
static bool SelectHears(Pickers* pickers, const muiEvent* event)
{
    Combo* select = &pickers->select;
    bool open = select->list.index1 != 0;
    if (event->kind == mui_eventPointer)
    {
        if (event->pointer->kind != mui_pointerRecordClick)
        {
            return false;
        }
        if (open)
        {
            Close(pickers->app, select);
        }
        else
        {
            OpenAll(pickers, select);
        }
        return true;
    }
    if (event->kind != mui_eventKeyDown)
    {
        return false;
    }
    if (open)
    {
        return ListKey(pickers, select, event) || event->code == mui_codeSpace;
    }
    if (event->code == mui_codeSpace || event->code == mui_codeEnter ||
        event->code == mui_codeArrowDown)
    {
        OpenAll(pickers, select);
        return true;
    }
    return false;
}

// The searchable combobox: typing and Backspace edit its text at the
// end and filter the list; Down opens or moves into it.
static bool SearchHears(Pickers* pickers, const muiEvent* event)
{
    Combo* search = &pickers->search;
    SampleApp* app = pickers->app;
    size_t length = 0;
    (void)TextOf(app, search->block, &length);
    if (event->kind == mui_eventText && event->length > 0 && (unsigned char)event->text[0] >= 0x20)
    {
        SampleAppCheck(app,
                       muiTextBlock_Replace(app->text, search->block, (uint32_t)length,
                                            (uint32_t)length, event->text,
                                            event->length) == mui_success &&
                           muiNode_MarkContentChanged(app->context, search->node) == mui_success,
                       "typed");
        Filter(pickers, search);
        return true;
    }
    if (event->kind != mui_eventKeyDown)
    {
        return false;
    }
    if (event->code == mui_codeBackspace)
    {
        uint32_t start = 0;
        uint32_t end = 0;
        if (muiTextBlock_FindDeletion(app->text, search->block, (uint32_t)length,
                                      mui_deleteBackward, &start, &end) == mui_success)
        {
            SampleAppCheck(app,
                           muiTextBlock_Replace(app->text, search->block, start, end, NULL, 0) ==
                                   mui_success &&
                               muiNode_MarkContentChanged(app->context, search->node) ==
                                   mui_success,
                           "a deletion");
        }
        Filter(pickers, search);
        return true;
    }
    if (search->list.index1 == 0)
    {
        if (event->code == mui_codeArrowDown)
        {
            OpenAll(pickers, search);
            return true;
        }
        return false;
    }
    return ListKey(pickers, search, event);
}

// The number input takes its drag: the value moves a step for each
// SCRUB of travel from where the press began, the range's own drag (the
// value following the pointer) and its press (paging) left out.
static bool NumberHears(Pickers* pickers, const muiEvent* event)
{
    SampleApp* app = pickers->app;
    if (event->kind != mui_eventPointer)
    {
        return false;
    }
    const muiPointerRecord* record = event->pointer;
    muiValueRange range = muiDefaultValueRange();
    (void)muiNode_GetValueRange(app->context, pickers->number, &range);
    switch (record->kind)
    {
    case mui_pointerRecordPress:
        pickers->scrubStart = range.value;
        return true;
    case mui_pointerRecordDragStart:
    case mui_pointerRecordDragMove:
    {
        float value = pickers->scrubStart + roundf(record->offsetX / SCRUB) * range.step;
        SampleAppCheck(app,
                       muiNode_SetRangeValue(app->context, pickers->number, value) == mui_success,
                       "a scrub");
        return true;
    }
    default:
        return false;
    }
}

// Whether a node is an option of a combobox's list, and which.
static int OptionOf(const Combo* combo, muiNodeId node)
{
    for (int i = 0; i < combo->shownCount; i++)
    {
        if (SampleSame(combo->items[i], node))
        {
            return i;
        }
    }
    return -1;
}

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Pickers* pickers = user;
    if (phase != mui_phaseBubble)
    {
        return false;
    }
    Combo* combos[2] = {&pickers->select, &pickers->search};
    for (int c = 0; c < 2; c++)
    {
        int option = OptionOf(combos[c], nodeId);
        if (option >= 0 && event->kind == mui_eventPointer &&
            event->pointer->kind == mui_pointerRecordClick)
        {
            Pick(pickers, combos[c], option);
            return true;
        }
    }
    if (SampleSame(nodeId, pickers->select.node))
    {
        return SelectHears(pickers, event);
    }
    if (SampleSame(nodeId, pickers->search.node))
    {
        return SearchHears(pickers, event);
    }
    if (SampleSame(nodeId, pickers->number))
    {
        return NumberHears(pickers, event);
    }
    return false;
}

// A field: a caption, then a box of a combobox's or a number's text.
static muiNodeId Field(SampleApp* app, const char* caption, const char* text, float width,
                       muiTextBlockId* blockOut)
{
    muiNodeId label = SampleLabel(app, app->root, caption, 14.0f, s_text);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.margin.top = 10.0f;
    SampleSetLayout(app, label, &layout, MUI_PROPERTY_BIT(mui_propertyMarginTop));
    muiNodeId field = SampleTextNode(app, app->root, text, 16.0f, s_text, blockOut);
    layout = muiDefaultLayoutStyle();
    layout.sizing.width = SampleLength(width);
    layout.margin.top = 4.0f;
    layout.padding = (muiEdges){8.0f, 8.0f, 5.0f, 5.0f};
    SampleSetLayout(
        app, field, &layout,
        MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyMarginTop) |
            MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
            MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
    SampleFill(app, field, s_field);
    SampleRound(app, field, 4.0f);
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    SampleAppCheck(
        app,
        muiNode_SetInteractionValues(app->context, field, &interaction,
                                     MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success &&
            muiNode_SetAccessText(app->context, field, mui_accessLabel, caption, strlen(caption)) ==
                mui_success,
        "a field");
    return field;
}

static void MakeCombo(Pickers* pickers, Combo* combo, const char* caption, const char* text,
                      const char* const* options, int count)
{
    SampleApp* app = pickers->app;
    combo->options = options;
    combo->optionCount = count;
    combo->active = -1;
    combo->picked = -1;
    combo->node = Field(app, caption, text, 160.0f, &combo->block);
    muiAccessValues values = muiDefaultAccessValues();
    values.popup = mui_popupListBox;
    SampleAppCheck(app,
                   muiNode_SetAccessRole(app->context, combo->node, mui_roleComboBox) ==
                           mui_success &&
                       muiNode_SetAccessValues(app->context, combo->node, &values) == mui_success,
                   "a combobox");
    Report(app, combo);
}

static void Build(void* user, SampleApp* app)
{
    Pickers* pickers = user;
    pickers->app = app;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.padding = (muiEdges){16.0f, 16.0f, 2.0f, 16.0f};
    SampleSetLayout(app, app->root, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingTop));
    SampleFill(app, app->root, s_background);
    muiVisualStyle active = muiDefaultVisualStyle();
    active.background = SampleColor(s_active);
    SampleAppCheck(app,
                   muiCreateStyle(app->context, &pickers->optionStyle) == mui_success &&
                       muiStyle_SetVisualValues(
                           app->context, pickers->optionStyle, mui_variantSelected, &active,
                           MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
                   "the options' class");
    MakeCombo(pickers, &pickers->select, "Quality", "Medium", s_qualities, QUALITIES);
    pickers->select.picked = 1;
    MakeCombo(pickers, &pickers->search, "Fruit", "", s_fruits, FRUITS);
    pickers->number = Field(app, "Count", "20", 80.0f, &pickers->numberBlock);
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    interaction.drags = true;
    muiValueRange range = muiDefaultValueRange();
    range.value = 20.0f;
    range.axis = mui_rangeVertical;
    pickers->shownValue = 20.0f;
    SampleAppCheck(app,
                   muiNode_SetInteractionValues(app->context, pickers->number, &interaction,
                                                MUI_PROPERTY_BIT(mui_propertyFocusMode) |
                                                    MUI_PROPERTY_BIT(mui_propertyDrags)) ==
                           mui_success &&
                       muiNode_SetAccessRole(app->context, pickers->number, mui_roleSpinButton) ==
                           mui_success &&
                       muiNode_SetValueRange(app->context, pickers->number, &range) == mui_success,
                   "the number input");
    SampleAppCheck(app,
                   muiSetEventFunction(app->context, Hear, pickers) == mui_success &&
                       muiSetAccessTextFunction(app->context, muiAccessTextOf, &app->host) ==
                           mui_success,
                   "the event and access text functions");
}

// Each frame: a dismissed list closes, and the number shows its value.
static void Update(void* user, SampleApp* app)
{
    Pickers* pickers = user;
    muiNotification notification;
    while (muiNextNotification(app->context, &notification) == mui_success)
    {
        Combo* combos[2] = {&pickers->select, &pickers->search};
        for (int c = 0; c < 2; c++)
        {
            if (notification.kind == mui_notificationPopupDismissed &&
                SampleSame(notification.nodeId, combos[c]->list))
            {
                Close(app, combos[c]);
            }
        }
    }
    muiValueRange range = muiDefaultValueRange();
    (void)muiNode_GetValueRange(app->context, pickers->number, &range);
    if (range.value != pickers->shownValue)
    {
        char text[16];
        snprintf(text, sizeof text, "%d", (int)range.value);
        SetText(app, pickers->number, pickers->numberBlock, text);
        pickers->shownValue = range.value;
    }
}

// Text, then the searchable field's caret at its end while focused.
static void Paint(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                  muiDrawSink* sink)
{
    Pickers* pickers = user;
    SampleApp* app = pickers->app;
    muiPaintText(&app->host, nodeId, hostKey, width, height, sink);
    if (!SampleSame(nodeId, pickers->search.node) ||
        !SampleSame(muiFocus_Get(app->context, 0), nodeId))
    {
        return;
    }
    size_t length = 0;
    (void)TextOf(app, pickers->search.block, &length);
    muiTextCaret caret;
    const muiTextPosition end = {(uint32_t)length, mui_affinityDownstream};
    if (muiTextGetCaret(&app->host, nodeId, width, end, &caret) == mui_success)
    {
        const muiRect bar = {caret.x, caret.y, 2.0f, caret.height};
        (void)muiDrawSink_AddRect(sink, bar, SampleColor(s_caret));
    }
}

static bool Holds(const SampleApp* app, muiTextBlockId block, const char* expected)
{
    size_t length = 0;
    const char* text = TextOf(app, block, &length);
    return length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static const muiAccessNode* Found(const SampleApp* app, muiNodeId node)
{
    return muiAccessTree_Find(muiWindowAccess_GetTree(app->access), muiAccessIdOf(node));
}

// Whether the accessibility tree reports a combobox open or closed, its
// active option the shown one at an index (-1 for none), and its value.
static bool Reported(const SampleApp* app, const Combo* combo, bool open, int active,
                     const char* value)
{
    const muiAccessNode* found = Found(app, combo->node);
    muiNodeId named = {0, 0};
    uint32_t count = 0;
    if (found == NULL ||
        muiNode_GetAccessRelation(app->context, combo->node, mui_relationActiveDescendant, &named,
                                  1, &count) != mui_success)
    {
        return false;
    }
    bool expanded = (found->flags & mui_accessExpanded) != 0;
    bool names = active < 0 ? count == 0 : count == 1 && SampleSame(named, combo->items[active]);
    size_t length = strlen(value);
    return expanded == open && names && found->textLength[mui_accessValue] == length &&
           (length == 0 || memcmp(found->text[mui_accessValue], value, length) == 0);
}

// Whether a combobox's list shows, its active option in its colour.
static bool ListShows(const SampleApp* app, const Combo* combo, int active)
{
    if (combo->list.index1 == 0 || active < 0)
    {
        return false;
    }
    return SampleNear(SamplePixelOf(app, combo->items[active], 4.0f, 4.0f), s_active) &&
           SampleNear(SamplePixelOf(app, combo->list, 4.0f, 1.0f), s_list);
}

// The first frame: the select showing Medium, the lists closed, the
// number at 20.
static void Still(void* user, SampleApp* app)
{
    const Pickers* pickers = user;
    const muiAccessNode* number = Found(app, pickers->number);
    SampleAppCheck(app,
                   Holds(app, pickers->select.block, "Medium") &&
                       Reported(app, &pickers->select, false, -1, "Medium"),
                   "the select closed on Medium");
    SampleAppCheck(app, Reported(app, &pickers->search, false, -1, ""),
                   "the searchable select closed and empty");
    SampleAppCheck(app,
                   number != NULL && (number->flags & mui_accessNumeric) != 0 &&
                       number->value == 20.0f && Holds(app, pickers->numberBlock, "20"),
                   "the number at 20");
}

static void Click(SampleApp* app, muiNodeId node)
{
    SamplePost(app, mwin_eventCursorMoved, node, 0);
    SamplePost(app, mwin_eventButtonDown, node, 1);
    SamplePost(app, mwin_eventButtonUp, node, 0);
}

static void Key(SampleApp* app, mwinKeyCode code)
{
    SamplePostKey(app, code, MWIN_KEY_NAMED | code, NULL);
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Pickers* pickers = user;
    Combo* select = &pickers->select;
    Combo* search = &pickers->search;
    switch (frame)
    {
    case 0:
        Still(pickers, app);
        Click(app, select->node);
        return true;
    case 1:
        SampleAppCheck(app, ListShows(app, select, 1) && Reported(app, select, true, 1, "Medium"),
                       "a click opened the list, Medium active");
        Key(app, mwin_codeArrowDown);
        Key(app, mwin_codeEnter);
        return true;
    case 2:
        SampleAppCheck(app,
                       select->list.index1 == 0 && Holds(app, select->block, "High") &&
                           Reported(app, select, false, -1, "High"),
                       "Down and Enter picked High and closed the list");
        Key(app, mwin_codeSpace);
        return true;
    case 3:
        SampleAppCheck(app, ListShows(app, select, 2) && Reported(app, select, true, 2, "High"),
                       "Space opened the list, High active");
        Key(app, mwin_codeEscape);
        return true;
    case 4:
        SampleAppCheck(app, select->list.index1 == 0 && Reported(app, select, false, -1, "High"),
                       "Escape dismissed the list, High kept");
        Click(app, search->node);
        SamplePostKey(app, mwin_codeKeyA, 'a', "a");
        SamplePostKey(app, mwin_codeKeyN, 'n', "n");
        return true;
    case 5:
        SampleAppCheck(app,
                       search->shownCount == 3 && search->list.index1 != 0 &&
                           Holds(app, search->block, "an") && Reported(app, search, true, -1, "an"),
                       "typing an showed Banana, Mango and Orange");
        Key(app, mwin_codeArrowDown);
        Key(app, mwin_codeArrowDown);
        return true;
    case 6:
        SampleAppCheck(app, ListShows(app, search, 1) && Reported(app, search, true, 1, "an"),
                       "two Downs made Mango active");
        // A press outside the field and its list dismisses the list.
        SamplePostAt(app, mwin_eventCursorMoved, 300.0f, 260.0f, 0);
        SamplePostAt(app, mwin_eventButtonDown, 300.0f, 260.0f, 1);
        SamplePostAt(app, mwin_eventButtonUp, 300.0f, 260.0f, 0);
        return true;
    case 7:
        SampleAppCheck(app, search->list.index1 == 0 && Reported(app, search, false, -1, "an"),
                       "a press outside dismissed the list");
        Click(app, search->node);
        SamplePostKey(app, mwin_codeArrowDown, MWIN_KEY_NAMED | mwin_codeArrowDown, NULL);
        return true;
    case 8:
    {
        SampleAppCheck(app, ListShows(app, search, 0), "Down opened every option");
        float x = 0.0f;
        float y = 0.0f;
        muiRect rect = muiNode_GetRect(app->context, search->items[3]);
        SampleAppCheck(app,
                       muiNode_MapToRoot(app->context, search->items[3], rect.width / 2.0f,
                                         rect.height / 2.0f, &x, &y) == mui_success,
                       "Cherry's place");
        SamplePostAt(app, mwin_eventCursorMoved, x, y, 0);
        SamplePostAt(app, mwin_eventButtonDown, x, y, 1);
        SamplePostAt(app, mwin_eventButtonUp, x, y, 0);
        return true;
    }
    case 9:
        SampleAppCheck(app,
                       search->list.index1 == 0 && Holds(app, search->block, "Cherry") &&
                           Reported(app, search, false, -1, "Cherry"),
                       "a click picked Cherry");
        {
            float x = 0.0f;
            float y = 0.0f;
            SampleAppCheck(app,
                           muiNode_MapToRoot(app->context, pickers->number, 20.0f, 10.0f, &x, &y) ==
                               mui_success,
                           "the number's place");
            SamplePostAt(app, mwin_eventCursorMoved, x, y, 0);
            SamplePostAt(app, mwin_eventButtonDown, x, y, 1);
            SamplePostAt(app, mwin_eventCursorMoved, x + 10.0f, y, 1);
            SamplePostAt(app, mwin_eventCursorMoved, x + 30.0f, y, 1);
            SamplePostAt(app, mwin_eventButtonUp, x + 30.0f, y, 0);
        }
        return true;
    case 10:
    {
        const muiAccessNode* number = Found(app, pickers->number);
        SampleAppCheck(
            app, number != NULL && number->value == 35.0f && Holds(app, pickers->numberBlock, "35"),
            "a drag of 30 scrubbed 15 steps, from 20 to 35");
        // A press without a drag leaves the value, where a range's would
        // page toward the point.
        float x = 0.0f;
        float y = 0.0f;
        SampleAppCheck(app,
                       muiNode_MapToRoot(app->context, pickers->number, 20.0f, 4.0f, &x, &y) ==
                           mui_success,
                       "the number's place");
        SamplePostAt(app, mwin_eventButtonDown, x, y, 1);
        SamplePostAt(app, mwin_eventButtonUp, x, y, 0);
        Key(app, mwin_codeArrowUp);
        return true;
    }
    default:
    {
        const muiAccessNode* number = Found(app, pickers->number);
        SampleAppCheck(
            app, number != NULL && number->value == 36.0f && Holds(app, pickers->numberBlock, "36"),
            "a press left the number and Up stepped it to 36");
        return false;
    }
    }
}

int main(int count, char** arguments)
{
    static Pickers pickers;
    const SampleAppDef def = {
        .width = 320,
        .height = 280,
        .build = Build,
        .update = Update,
        .paint = Paint,
        .script = Script,
        .still = Still,
        .user = &pickers,
    };
    return SampleRunApp(&def, count, arguments);
}
