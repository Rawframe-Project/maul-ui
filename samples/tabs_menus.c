// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Tabs, a tooltip and a context menu, composed over Maul UI's public
// capabilities with the patterns of the WAI-ARIA Authoring Practices
// (record mui-0005). The tabs select on a click or as arrows, Home and
// End move the focus among them (automatic activation), only the
// selected one in the Tab order, each naming the panel it controls. The
// tooltip is a manual popup above its trigger that takes no input,
// shown once the pointer has rested on the trigger for a delay the host
// times (the core keeps no clock of its own) and hidden when it leaves,
// the trigger described by it. The context menu is a popup at the
// pointer, or at the area's corner for the context menu key or
// Shift+F10, taking the focus: arrows move it, Enter or a click picks
// an item, and the library's light dismissal (an unhandled Escape, a
// press outside) closes it, the focus going back to the area. Headless,
// each is used as a person would and the texts, the pixels, the focus
// and the accessibility tree checked.

#include "app.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"
#include "maul-ui/popup.h"
#include "maul-ui/style.h"

#include <stdio.h>
#include <string.h>

#define TABS  3
#define ITEMS 3

// The USB HID usages of the context menu key and F10.
#define CONTEXT_MENU_KEY 101u
#define F10_KEY          67u

// How long the pointer rests on the trigger before the tooltip shows.
#define TOOLTIP_DELAY_NS 500000000u

static const char* const s_tabs[TABS] = {"General", "Audio", "Video"};
static const char* const s_panels[TABS] = {"Name and language", "Volume and output",
                                           "Resolution and vsync"};
static const char* const s_items[ITEMS] = {"Cut", "Copy", "Paste"};

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_text[3] = {242, 242, 242};
static const uint8_t s_field[3] = {48, 56, 64};
static const uint8_t s_tab[3] = {40, 48, 56};
static const uint8_t s_active[3] = {48, 112, 224};
static const uint8_t s_menu[3] = {64, 72, 84};
static const uint8_t s_tip[3] = {250, 230, 140};
static const uint8_t s_tipText[3] = {24, 24, 24};
static const uint8_t s_swatches[TABS][3] = {{200, 60, 60}, {60, 200, 60}, {60, 60, 200}};

typedef struct Panels
{
    muiNodeId tabs[TABS];
    muiNodeId panel;
    muiNodeId panelText;
    muiTextBlockId panelBlock;
    muiNodeId swatch;
    int selected;
    muiNodeId help;
    muiNodeId tip;
    uint64_t restingSince;
    muiNodeId area;
    muiTextBlockId areaBlock;
    muiNodeId menuAnchor;
    muiNodeId menu;
    muiNodeId items[ITEMS];
    muiStyleId itemStyle;
    SampleApp* app;
} Panels;

static void SetText(SampleApp* app, muiNodeId node, muiTextBlockId block, const char* text)
{
    SampleAppCheck(app,
                   muiTextBlock_SetText(app->text, block, text, strlen(text)) == mui_success &&
                       muiNode_MarkContentChanged(app->context, node) == mui_success,
                   "a text");
}

static void Focus(SampleApp* app, muiNodeId node)
{
    SampleAppCheck(app, muiFocus_Set(app->context, 0, node, mui_focusByNavigation) == mui_success,
                   "the focus moved");
}

static void Padding(SampleApp* app, muiNodeId node, float x, float y)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding = (muiEdges){x, x, y, y};
    SampleSetLayout(
        app, node, &layout,
        MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
            MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
}

static void FocusMode(SampleApp* app, muiNodeId node, muiFocusMode mode)
{
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mode;
    SampleAppCheck(app,
                   muiNode_SetInteractionValues(app->context, node, &interaction,
                                                MUI_PROPERTY_BIT(mui_propertyFocusMode)) ==
                       mui_success,
                   "a focus mode");
}

// A popup in the overlay layer beside an anchor.
static muiNodeId Popup(SampleApp* app, muiNodeId anchor, muiPopupSide side, muiPopupAlign align,
                       bool lightDismiss, muiHitMode hit)
{
    muiNodeId popup = SampleNode(app, app->root, 0.0f, 0.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.placement.position = mui_positionAbsolute;
    SampleSetLayout(app, popup, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyPosition));
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.layer = mui_layerOverlay;
    interaction.hitMode = hit;
    muiPopup place = muiDefaultPopup();
    place.anchor = anchor;
    place.side = side;
    place.align = align;
    place.gap = 4.0f;
    place.margin = 4.0f;
    place.lightDismiss = lightDismiss;
    SampleAppCheck(app,
                   muiNode_SetInteractionValues(app->context, popup, &interaction,
                                                MUI_PROPERTY_BIT(mui_propertyLayer) |
                                                    MUI_PROPERTY_BIT(mui_propertyHitMode)) ==
                           mui_success &&
                       muiNode_SetPopup(app->context, popup, &place) == mui_success,
                   "a popup");
    SampleRound(app, popup, 4.0f);
    return popup;
}

// Selects a tab: its state, the Tab order, the panel's text, swatch and
// name.
static void Select(Panels* sample, int tab)
{
    SampleApp* app = sample->app;
    sample->selected = tab;
    for (int i = 0; i < TABS; i++)
    {
        SampleAppCheck(app,
                       muiNode_SetStates(app->context, sample->tabs[i],
                                         i == tab ? mui_stateSelected : 0) == mui_success,
                       "a tab's state");
        FocusMode(app, sample->tabs[i], i == tab ? mui_focusAll : mui_focusPointer);
    }
    SetText(app, sample->panelText, sample->panelBlock, s_panels[tab]);
    SampleFill(app, sample->swatch, s_swatches[tab]);
    SampleAppCheck(app,
                   muiNode_SetAccessRelation(app->context, sample->panel, mui_relationLabelledBy,
                                             &sample->tabs[tab], 1) == mui_success,
                   "the panel named by its tab");
}

static void ShowTip(Panels* sample)
{
    SampleApp* app = sample->app;
    sample->tip =
        Popup(app, sample->help, mui_popupAbove, mui_popupAlignCenter, false, mui_hitNone);
    SampleFill(app, sample->tip, s_tip);
    Padding(app, sample->tip, 8.0f, 4.0f);
    SampleLabel(app, sample->tip, "Opens the manual", 14.0f, s_tipText);
    SampleAppCheck(
        app,
        muiNode_SetAccessRole(app->context, sample->tip, mui_roleTooltip) == mui_success &&
            muiNode_SetAccessRelation(app->context, sample->help, mui_relationDescribedBy,
                                      &sample->tip, 1) == mui_success,
        "the tooltip");
}

static void HideTip(Panels* sample)
{
    SampleApp* app = sample->app;
    if (sample->tip.index1 == 0)
    {
        return;
    }
    SampleAppCheck(app,
                   muiNode_SetAccessRelation(app->context, sample->help, mui_relationDescribedBy,
                                             NULL, 0) == mui_success &&
                       muiDestroyNode(app->context, sample->tip) == mui_success,
                   "the tooltip hidden");
    sample->tip = (muiNodeId){0, 0};
}

// Opens the menu at a point of the area, its first item focused.
static void OpenMenu(Panels* sample, float x, float y)
{
    SampleApp* app = sample->app;
    sample->menuAnchor = SampleNode(app, sample->area, 1.0f, 1.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.placement.position = mui_positionAbsolute;
    layout.placement.inset.start = SampleLength(x);
    layout.placement.inset.top = SampleLength(y);
    SampleSetLayout(app, sample->menuAnchor, &layout,
                    MUI_PROPERTY_BIT(mui_propertyPosition) |
                        MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                        MUI_PROPERTY_BIT(mui_propertyInsetTop));
    sample->menu =
        Popup(app, sample->menuAnchor, mui_popupBelow, mui_popupAlignStart, true, mui_hitAuto);
    SampleFill(app, sample->menu, s_menu);
    Padding(app, sample->menu, 0.0f, 4.0f);
    SampleAppCheck(app,
                   muiNode_SetAccessRole(app->context, sample->menu, mui_roleMenu) == mui_success,
                   "the menu");
    for (int i = 0; i < ITEMS; i++)
    {
        sample->items[i] = SampleLabel(app, sample->menu, s_items[i], 16.0f, s_text);
        Padding(app, sample->items[i], 16.0f, 3.0f);
        layout = muiDefaultLayoutStyle();
        layout.sizing.width = SampleLength(120.0f);
        SampleSetLayout(app, sample->items[i], &layout, MUI_PROPERTY_BIT(mui_propertyWidth));
        FocusMode(app, sample->items[i], mui_focusAll);
        SampleAppCheck(app,
                       muiNode_SetClasses(app->context, sample->items[i], &sample->itemStyle, 1) ==
                               mui_success &&
                           muiNode_SetAccessRole(app->context, sample->items[i],
                                                 mui_roleMenuItem) == mui_success,
                       "an item");
    }
    Focus(app, sample->items[0]);
}

// Closes the menu, the focus back on the area.
static void CloseMenu(Panels* sample)
{
    SampleApp* app = sample->app;
    if (sample->menu.index1 == 0)
    {
        return;
    }
    SampleAppCheck(app,
                   muiDestroyNode(app->context, sample->menu) == mui_success &&
                       muiDestroyNode(app->context, sample->menuAnchor) == mui_success,
                   "the menu closed");
    sample->menu = (muiNodeId){0, 0};
    sample->menuAnchor = (muiNodeId){0, 0};
    Focus(app, sample->area);
}

static void Choose(Panels* sample, int item)
{
    char text[32];
    snprintf(text, sizeof text, "%s chosen", s_items[item]);
    SetText(sample->app, sample->area, sample->areaBlock, text);
    CloseMenu(sample);
}

static int IndexOf(const muiNodeId* nodes, int count, muiNodeId node)
{
    for (int i = 0; i < count; i++)
    {
        if (SampleSame(nodes[i], node))
        {
            return i;
        }
    }
    return -1;
}

static bool Clicked(const muiEvent* event)
{
    return event->kind == mui_eventPointer && event->pointer->kind == mui_pointerRecordClick &&
           event->pointer->button == mui_buttonPrimary;
}

// Arrows, Home and End among the tabs, wrapping, selecting as they go.
static bool TabKey(Panels* sample, int tab, const muiEvent* event)
{
    int next = event->code == mui_codeArrowRight  ? (tab + 1) % TABS
               : event->code == mui_codeArrowLeft ? (tab + TABS - 1) % TABS
               : event->code == mui_codeHome      ? 0
               : event->code == mui_codeEnd       ? TABS - 1
                                                  : -1;
    if (next < 0)
    {
        return false;
    }
    Select(sample, next);
    Focus(sample->app, sample->tabs[next]);
    return true;
}

// Up and Down among the items, wrapping; Enter or Space picks one.
static bool ItemKey(Panels* sample, int item, const muiEvent* event)
{
    switch (event->code)
    {
    case mui_codeArrowDown:
        Focus(sample->app, sample->items[(item + 1) % ITEMS]);
        return true;
    case mui_codeArrowUp:
        Focus(sample->app, sample->items[(item + ITEMS - 1) % ITEMS]);
        return true;
    case mui_codeEnter:
    case mui_codeSpace:
        Choose(sample, item);
        return true;
    default:
        return false;
    }
}

// The area opens the menu on a secondary press at the pointer, or for
// the context menu key or Shift+F10 at its corner.
static bool AreaHears(Panels* sample, const muiEvent* event)
{
    if (event->kind == mui_eventPointer && event->pointer->kind == mui_pointerRecordPress &&
        event->pointer->button == mui_buttonSecondary)
    {
        OpenMenu(sample, event->pointer->x, event->pointer->y);
        return true;
    }
    bool shiftF10 = event->code == F10_KEY && event->modifiers == mui_modShift;
    if (event->kind == mui_eventKeyDown && (event->code == CONTEXT_MENU_KEY || shiftF10))
    {
        OpenMenu(sample, 8.0f, 8.0f);
        return true;
    }
    return false;
}

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Panels* sample = user;
    if (phase != mui_phaseBubble)
    {
        return false;
    }
    int tab = IndexOf(sample->tabs, TABS, nodeId);
    if (tab >= 0)
    {
        if (Clicked(event))
        {
            Select(sample, tab);
            return true;
        }
        return event->kind == mui_eventKeyDown && TabKey(sample, tab, event);
    }
    int item = sample->menu.index1 != 0 ? IndexOf(sample->items, ITEMS, nodeId) : -1;
    if (item >= 0)
    {
        if (Clicked(event))
        {
            Choose(sample, item);
            return true;
        }
        return event->kind == mui_eventKeyDown && ItemKey(sample, item, event);
    }
    if (SampleSame(nodeId, sample->area))
    {
        return AreaHears(sample, event);
    }
    return false;
}

static void MakeTabs(Panels* sample, SampleApp* app)
{
    muiNodeId list = SampleNode(app, app->root, 0.0f, 0.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    SampleSetLayout(app, list, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    muiStyleId tabStyle = {0};
    muiVisualStyle base = muiDefaultVisualStyle();
    base.background = SampleColor(s_tab);
    muiVisualStyle selected = muiDefaultVisualStyle();
    selected.background = SampleColor(s_active);
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    const muiStyleDef styleDef = muiDefaultStyleDef();
    SampleAppCheck(app,
                   muiNode_SetAccessRole(app->context, list, mui_roleTabList) == mui_success &&
                       muiCreateStyle(app->context, &styleDef, &tabStyle) == mui_success &&
                       muiStyle_SetVisualValues(app->context, tabStyle, mui_variantBase, &base,
                                                background) == mui_success &&
                       muiStyle_SetVisualValues(app->context, tabStyle, mui_variantSelected,
                                                &selected, background) == mui_success,
                   "the tab list");
    for (int i = 0; i < TABS; i++)
    {
        sample->tabs[i] = SampleLabel(app, list, s_tabs[i], 15.0f, s_text);
        Padding(app, sample->tabs[i], 12.0f, 6.0f);
        SampleAppCheck(
            app,
            muiNode_SetClasses(app->context, sample->tabs[i], &tabStyle, 1) == mui_success &&
                muiNode_SetAccessRole(app->context, sample->tabs[i], mui_roleTab) == mui_success &&
                muiNode_SetAccessFlags(app->context, sample->tabs[i], mui_accessSelectable) ==
                    mui_success,
            "a tab");
    }
    sample->panel = SampleNode(app, app->root, 288.0f, 56.0f);
    layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    SampleSetLayout(app, sample->panel, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    Padding(app, sample->panel, 12.0f, 12.0f);
    SampleFill(app, sample->panel, s_field);
    sample->swatch = SampleNode(app, sample->panel, 32.0f, 32.0f);
    sample->panelText = SampleTextNode(app, sample->panel, "", 15.0f, s_text, &sample->panelBlock);
    layout = muiDefaultLayoutStyle();
    layout.margin.start = 12.0f;
    SampleSetLayout(app, sample->panelText, &layout, MUI_PROPERTY_BIT(mui_propertyMarginStart));
    SampleAppCheck(
        app, muiNode_SetAccessRole(app->context, sample->panel, mui_roleTabPanel) == mui_success,
        "the panel");
    for (int i = 0; i < TABS; i++)
    {
        SampleAppCheck(app,
                       muiNode_SetAccessRelation(app->context, sample->tabs[i],
                                                 mui_relationControls, &sample->panel,
                                                 1) == mui_success,
                       "a tab controls the panel");
    }
    Select(sample, 0);
}

static void Build(void* user, SampleApp* app)
{
    Panels* sample = user;
    sample->app = app;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.container.rowGap = 12.0f;
    layout.padding = (muiEdges){16.0f, 16.0f, 16.0f, 16.0f};
    SampleSetLayout(
        app, app->root, &layout,
        MUI_PROPERTY_BIT(mui_propertyFlexDirection) | MUI_PROPERTY_BIT(mui_propertyRowGap) |
            MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingTop));
    SampleFill(app, app->root, s_background);
    MakeTabs(sample, app);
    sample->help = SampleLabel(app, app->root, "Help", 15.0f, s_text);
    Padding(app, sample->help, 12.0f, 6.0f);
    layout = muiDefaultLayoutStyle();
    layout.item.alignSelf = mui_alignCenter;
    layout.margin.top = 24.0f;
    SampleSetLayout(app, sample->help, &layout,
                    MUI_PROPERTY_BIT(mui_propertyAlignSelf) |
                        MUI_PROPERTY_BIT(mui_propertyMarginTop));
    SampleFill(app, sample->help, s_field);
    SampleRound(app, sample->help, 4.0f);
    SampleAppCheck(app,
                   muiNode_SetAccessRole(app->context, sample->help, mui_roleButton) == mui_success,
                   "the help button");
    sample->area =
        SampleTextNode(app, app->root, "Right-click here", 15.0f, s_text, &sample->areaBlock);
    layout = muiDefaultLayoutStyle();
    layout.sizing.height = SampleLength(64.0f);
    SampleSetLayout(app, sample->area, &layout, MUI_PROPERTY_BIT(mui_propertyHeight));
    Padding(app, sample->area, 12.0f, 8.0f);
    SampleFill(app, sample->area, s_field);
    FocusMode(app, sample->area, mui_focusAll);
    muiVisualStyle focused = muiDefaultVisualStyle();
    focused.background = SampleColor(s_active);
    const muiStyleDef styleDef = muiDefaultStyleDef();
    SampleAppCheck(
        app,
        muiNode_SetAccessRole(app->context, sample->area, mui_roleGroup) == mui_success &&
            muiCreateStyle(app->context, &styleDef, &sample->itemStyle) == mui_success &&
            muiStyle_SetVisualValues(app->context, sample->itemStyle, mui_variantFocused, &focused,
                                     MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success &&
            muiSetEventFunction(app->context, Hear, sample) == mui_success &&
            muiSetAccessTextFunction(app->context, muiAccessTextOf, &app->host) == mui_success,
        "the area, the items' class and the event function");
}

// Each frame: the tooltip after the pointer has rested on its trigger,
// hidden when it leaves; a dismissed menu closed.
static void Update(void* user, SampleApp* app)
{
    Panels* sample = user;
    muiNotification notification;
    while (muiNextNotification(app->context, &notification) == mui_success)
    {
        if (notification.kind == mui_notificationPopupDismissed &&
            SampleSame(notification.nodeId, sample->menu))
        {
            CloseMenu(sample);
        }
    }
    bool resting = (muiNode_GetStates(app->context, sample->help) & mui_stateHovered) != 0;
    if (!resting)
    {
        sample->restingSince = 0;
        HideTip(sample);
    }
    else if (sample->restingSince == 0)
    {
        sample->restingSince = app->now;
    }
    else if (sample->tip.index1 == 0 && app->now - sample->restingSince >= TOOLTIP_DELAY_NS)
    {
        ShowTip(sample);
    }
}

static bool Holds(const SampleApp* app, muiTextBlockId block, const char* expected)
{
    const char* text = "";
    size_t length = 0;
    (void)muiTextBlock_GetText(app->text, block, &text, &length);
    return length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static const muiAccessNode* Found(const SampleApp* app, muiNodeId node)
{
    return muiAccessTree_Find(muiWindowAccess_GetTree(app->access), muiAccessIdOf(node));
}

static bool Shows(const SampleApp* app, muiNodeId node, float x, float y, const uint8_t rgb[3])
{
    return SampleNear(SamplePixelOf(app, node, x, y), rgb);
}

static bool Focused(const SampleApp* app, muiNodeId node)
{
    return SampleSame(muiFocus_Get(app->context, 0), node);
}

// Whether a tab is the selected one: in its colour, reported selected
// alone, the panel showing its text and swatch.
static bool Selected(const Panels* sample, const SampleApp* app, int tab)
{
    bool reported = true;
    for (int i = 0; i < TABS; i++)
    {
        const muiAccessNode* found = Found(app, sample->tabs[i]);
        reported = reported && found != NULL &&
                   ((found->flags & mui_accessSelected) != 0) == (i == tab) &&
                   Shows(app, sample->tabs[i], 4.0f, 4.0f, i == tab ? s_active : s_tab);
    }
    return reported && Holds(app, sample->panelBlock, s_panels[tab]) &&
           Shows(app, sample->swatch, 16.0f, 16.0f, s_swatches[tab]);
}

// Whether the menu shows at a point of the area, an item focused.
static bool MenuAt(const Panels* sample, const SampleApp* app, float x, float y, int item)
{
    if (sample->menu.index1 == 0)
    {
        return false;
    }
    float menuX = 0.0f;
    float menuY = 0.0f;
    float areaX = 0.0f;
    float areaY = 0.0f;
    bool placed =
        muiNode_MapToRoot(app->context, sample->menu, 0.0f, 0.0f, &menuX, &menuY) == mui_success &&
        muiNode_MapToRoot(app->context, sample->area, x, y, &areaX, &areaY) == mui_success;
    const muiAccessNode* menu = Found(app, sample->menu);
    return placed && menuX == areaX && menuY > areaY && menuY < areaY + 8.0f &&
           Shows(app, sample->items[item], 4.0f, 4.0f, s_active) &&
           Focused(app, sample->items[item]) && menu != NULL && menu->role == mui_roleMenu;
}

// The first frame: General selected, no tooltip, no menu.
static void Still(void* user, SampleApp* app)
{
    const Panels* sample = user;
    SampleAppCheck(app, Selected(sample, app, 0), "General selected");
    SampleAppCheck(app, sample->tip.index1 == 0 && sample->menu.index1 == 0, "no tooltip, no menu");
}

static void Key(SampleApp* app, mwinKeyCode code)
{
    SamplePostKey(app, code, MWIN_KEY_NAMED | code, NULL);
}

// The tooltip shows above the help button in its colour, describing it.
static bool TipShows(const Panels* sample, const SampleApp* app)
{
    muiNodeId described = {0, 0};
    uint32_t count = 0;
    if (sample->tip.index1 == 0 ||
        muiNode_GetAccessRelation(app->context, sample->help, mui_relationDescribedBy, &described,
                                  1, &count) != mui_success)
    {
        return false;
    }
    float tipY = 0.0f;
    float helpY = 0.0f;
    float x = 0.0f;
    muiRect tip = muiNode_GetRect(app->context, sample->tip);
    bool above =
        muiNode_MapToRoot(app->context, sample->tip, 0.0f, tip.height, &x, &tipY) == mui_success &&
        muiNode_MapToRoot(app->context, sample->help, 0.0f, 0.0f, &x, &helpY) == mui_success &&
        tipY <= helpY;
    const muiAccessNode* found = Found(app, sample->tip);
    return above && count == 1 && SampleSame(described, sample->tip) &&
           Shows(app, sample->tip, 2.0f, tip.height / 2.0f, s_tip) && found != NULL &&
           found->role == mui_roleTooltip;
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Panels* sample = user;
    switch (frame)
    {
    case 0:
        Still(sample, app);
        SamplePost(app, mwin_eventCursorMoved, sample->tabs[2], 0);
        SamplePost(app, mwin_eventButtonDown, sample->tabs[2], 1);
        SamplePost(app, mwin_eventButtonUp, sample->tabs[2], 0);
        return true;
    case 1:
        SampleAppCheck(app, Selected(sample, app, 2) && Focused(app, sample->tabs[2]),
                       "a click selected Video");
        Key(app, mwin_codeArrowRight);
        return true;
    case 2:
        SampleAppCheck(app, Selected(sample, app, 0) && Focused(app, sample->tabs[0]),
                       "Right wrapped to General");
        Key(app, mwin_codeEnd);
        SamplePost(app, mwin_eventCursorMoved, sample->help, 0);
        return true;
    case 3:
        SampleAppCheck(app, Selected(sample, app, 2) && Focused(app, sample->tabs[2]),
                       "End selected Video");
        SampleAppCheck(app, sample->tip.index1 == 0, "no tooltip as the pointer arrives");
        return true;
    case 4:
        SampleAppCheck(app, sample->tip.index1 == 0, "no tooltip a quarter second on");
        return true;
    case 5:
        SampleAppCheck(app, TipShows(sample, app), "the tooltip half a second on");
        SamplePost(app, mwin_eventCursorMoved, sample->panel, 0);
        return true;
    case 6:
    {
        SampleAppCheck(app, sample->tip.index1 == 0, "the tooltip hidden on leaving");
        float x = 0.0f;
        float y = 0.0f;
        SampleAppCheck(
            app, muiNode_MapToRoot(app->context, sample->area, 40.0f, 20.0f, &x, &y) == mui_success,
            "the area's place");
        SamplePostButton(app, mwin_eventCursorMoved, x, y, mwin_buttonRight, 0);
        SamplePostButton(app, mwin_eventButtonDown, x, y, mwin_buttonRight, 2);
        SamplePostButton(app, mwin_eventButtonUp, x, y, mwin_buttonRight, 0);
        return true;
    }
    case 7:
        SampleAppCheck(app, MenuAt(sample, app, 40.0f, 20.0f, 0),
                       "a secondary press opened the menu at the pointer, Cut focused");
        Key(app, mwin_codeArrowDown);
        Key(app, mwin_codeEnter);
        return true;
    case 8:
        SampleAppCheck(app,
                       sample->menu.index1 == 0 && Holds(app, sample->areaBlock, "Copy chosen") &&
                           Focused(app, sample->area),
                       "Down and Enter chose Copy, the focus back on the area");
        Key(app, CONTEXT_MENU_KEY);
        return true;
    case 9:
        SampleAppCheck(app, MenuAt(sample, app, 8.0f, 8.0f, 0),
                       "the context menu key opened the menu at the area's corner");
        Key(app, mwin_codeEscape);
        return true;
    default:
        SampleAppCheck(app,
                       sample->menu.index1 == 0 && Focused(app, sample->area) &&
                           Holds(app, sample->areaBlock, "Copy chosen"),
                       "Escape dismissed the menu, the focus back on the area");
        return false;
    }
}

int main(int count, char** arguments)
{
    static Panels sample;
    const SampleAppDef def = {
        .width = 320,
        .height = 360,
        .frameNs = 250000000u,
        .build = Build,
        .update = Update,
        .script = Script,
        .still = Still,
        .user = &sample,
    };
    return SampleRunApp(&def, count, arguments);
}
