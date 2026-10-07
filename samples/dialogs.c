// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A modal dialog and floating dialogs, composed over Maul UI's layers
// (maul-ui/interaction.h) and popups with the dialog patterns of the
// WAI-ARIA Authoring Practices (record mui-0005). The modal dialog is a
// modal layer centred on the root, made when it opens: the library keeps
// Tab inside it and lets no point reach what lies below; the host
// focuses its first button, closes it on Escape or a button, and gives
// the focus back to the button that opened it. The floating dialogs are
// activation layers placed by their insets: a press in one raises it
// above the other, and a drag of its title bar moves it. Headless, each
// is used as a person would and the pixels, the focus and the
// accessibility tree checked.

#include "app.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"
#include "maul-ui/popup.h"
#include "maul-ui/style.h"

#include <string.h>

#define FLOATING 2

static const char* const s_titles[FLOATING] = {"Notes", "Tools"};

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_text[3] = {242, 242, 242};
static const uint8_t s_button[3] = {48, 112, 224};
static const uint8_t s_focused[3] = {224, 112, 48};
static const uint8_t s_dialog[3] = {56, 64, 76};
static const uint8_t s_bodies[FLOATING][3] = {{120, 70, 140}, {40, 130, 110}};
static const uint8_t s_bar[3] = {32, 36, 44};

typedef struct Floating
{
    muiNodeId node;
    muiNodeId bar;
    float x;
    float y;
    // Where it was when a drag of its bar began.
    float dragX;
    float dragY;
} Floating;

typedef struct Dialogs
{
    muiNodeId opener;
    muiNodeId status;
    muiTextBlockId statusBlock;
    muiNodeId modal;
    muiNodeId cancel;
    muiNodeId discard;
    Floating floating[FLOATING];
    muiStyleId buttonStyle;
    int baseClicks;
    SampleApp* app;
} Dialogs;

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

static void SetStatus(Dialogs* dialogs, const char* text)
{
    SampleApp* app = dialogs->app;
    SampleAppCheck(app,
                   muiTextBlock_SetText(app->text, dialogs->statusBlock, text, strlen(text)) ==
                           mui_success &&
                       muiNode_MarkContentChanged(app->context, dialogs->status) == mui_success,
                   "the status");
}

// A button: a label on blue, orange while focused, taking Tab.
static muiNodeId Button(Dialogs* dialogs, muiNodeId parent, const char* text)
{
    SampleApp* app = dialogs->app;
    muiNodeId button = SampleLabel(app, parent, text, 15.0f, s_text);
    Padding(app, button, 12.0f, 6.0f);
    SampleRound(app, button, 4.0f);
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    SampleAppCheck(
        app,
        muiNode_SetClasses(app->context, button, &dialogs->buttonStyle, 1) == mui_success &&
            muiNode_SetInteractionValues(app->context, button, &interaction,
                                         MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success &&
            muiNode_SetAccessRole(app->context, button, mui_roleButton) == mui_success,
        "a button");
    return button;
}

// Opens the modal dialog centred on the root, Cancel focused.
static void OpenModal(Dialogs* dialogs)
{
    SampleApp* app = dialogs->app;
    dialogs->modal = SampleNode(app, app->root, 220.0f, 0.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.container.rowGap = 12.0f;
    layout.placement.position = mui_positionAbsolute;
    SampleSetLayout(app, dialogs->modal, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyRowGap) |
                        MUI_PROPERTY_BIT(mui_propertyPosition));
    Padding(app, dialogs->modal, 16.0f, 16.0f);
    SampleFill(app, dialogs->modal, s_dialog);
    SampleRound(app, dialogs->modal, 8.0f);
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.layer = mui_layerModal;
    muiPopup popup = muiDefaultPopup();
    popup.anchor = app->root;
    popup.side = mui_popupCenter;
    popup.lightDismiss = false;
    muiNodeId title = SampleLabel(app, dialogs->modal, "Discard changes?", 17.0f, s_text);
    muiNodeId row = SampleNode(app, dialogs->modal, 0.0f, 0.0f);
    layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    layout.container.columnGap = 8.0f;
    SampleSetLayout(app, row, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyColumnGap));
    dialogs->cancel = Button(dialogs, row, "Cancel");
    dialogs->discard = Button(dialogs, row, "Discard");
    SampleAppCheck(
        app,
        muiNode_SetInteractionValues(app->context, dialogs->modal, &interaction,
                                     MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success &&
            muiNode_SetPopup(app->context, dialogs->modal, &popup) == mui_success &&
            muiNode_SetAccessRole(app->context, dialogs->modal, mui_roleDialog) == mui_success &&
            muiNode_SetAccessRelation(app->context, dialogs->modal, mui_relationLabelledBy, &title,
                                      1) == mui_success,
        "the modal dialog");
    Focus(app, dialogs->cancel);
}

// Closes the modal dialog, the focus back on its opener.
static void CloseModal(Dialogs* dialogs, const char* status)
{
    SampleApp* app = dialogs->app;
    SampleAppCheck(app, muiDestroyNode(app->context, dialogs->modal) == mui_success,
                   "the modal dialog closed");
    dialogs->modal = (muiNodeId){0, 0};
    SetStatus(dialogs, status);
    Focus(app, dialogs->opener);
}

static void Place(SampleApp* app, Floating* floating)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.placement.position = mui_positionAbsolute;
    layout.placement.inset.start = SampleLength(floating->x);
    layout.placement.inset.top = SampleLength(floating->y);
    SampleSetLayout(app, floating->node, &layout,
                    MUI_PROPERTY_BIT(mui_propertyPosition) |
                        MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                        MUI_PROPERTY_BIT(mui_propertyInsetTop));
}

// A floating dialog: an activation layer of a title bar that takes
// drags over a body, placed at a point.
static void MakeFloating(Dialogs* dialogs, int index, float x, float y)
{
    SampleApp* app = dialogs->app;
    Floating* floating = &dialogs->floating[index];
    floating->node = SampleNode(app, app->root, 150.0f, 96.0f);
    floating->x = x;
    floating->y = y;
    Place(app, floating);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    SampleSetLayout(app, floating->node, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    SampleFill(app, floating->node, s_bodies[index]);
    floating->bar = SampleLabel(app, floating->node, s_titles[index], 14.0f, s_text);
    Padding(app, floating->bar, 8.0f, 4.0f);
    SampleFill(app, floating->bar, s_bar);
    muiInteractionStyle layer = muiDefaultInteractionStyle();
    layer.layer = mui_layerActivation;
    muiInteractionStyle bar = muiDefaultInteractionStyle();
    bar.drags = true;
    SampleAppCheck(
        app,
        muiNode_SetInteractionValues(app->context, floating->node, &layer,
                                     MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success &&
            muiNode_SetInteractionValues(app->context, floating->bar, &bar,
                                         MUI_PROPERTY_BIT(mui_propertyDrags)) == mui_success &&
            muiNode_SetAccessRole(app->context, floating->node, mui_roleDialog) == mui_success &&
            muiNode_SetAccessRelation(app->context, floating->node, mui_relationLabelledBy,
                                      &floating->bar, 1) == mui_success,
        "a floating dialog");
}

static bool Activates(const muiEvent* event)
{
    switch (event->kind)
    {
    case mui_eventPointer:
        return event->pointer->kind == mui_pointerRecordClick &&
               event->pointer->button == mui_buttonPrimary;
    case mui_eventKeyDown:
        return !event->repeat && (event->code == mui_codeEnter || event->code == mui_codeSpace);
    default:
        return false;
    }
}

// A press in a floating dialog raises it; a drag of its bar moves it.
static bool FloatingHears(Dialogs* dialogs, Floating* floating, muiNodeId nodeId,
                          const muiEvent* event)
{
    SampleApp* app = dialogs->app;
    if (event->kind != mui_eventPointer)
    {
        return false;
    }
    const muiPointerRecord* record = event->pointer;
    if (SampleSame(nodeId, floating->node) && record->kind == mui_pointerRecordPress)
    {
        SampleAppCheck(app, muiNode_RaiseLayer(app->context, floating->node) == mui_success,
                       "a dialog raised");
        return false;
    }
    if (!SampleSame(nodeId, floating->bar))
    {
        return false;
    }
    switch (record->kind)
    {
    case mui_pointerRecordDragStart:
        floating->dragX = floating->x;
        floating->dragY = floating->y;
        [[fallthrough]];
    case mui_pointerRecordDragMove:
        floating->x = floating->dragX + record->offsetX;
        floating->y = floating->dragY + record->offsetY;
        Place(app, floating);
        return true;
    default:
        return false;
    }
}

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Dialogs* dialogs = user;
    if (phase != mui_phaseBubble)
    {
        return false;
    }
    if (dialogs->modal.index1 != 0)
    {
        if (SampleSame(nodeId, dialogs->modal) && event->kind == mui_eventKeyDown &&
            event->code == mui_codeEscape)
        {
            CloseModal(dialogs, "Kept");
            return true;
        }
        if ((SampleSame(nodeId, dialogs->cancel) || SampleSame(nodeId, dialogs->discard)) &&
            Activates(event))
        {
            CloseModal(dialogs, SampleSame(nodeId, dialogs->cancel) ? "Kept" : "Discarded");
            return true;
        }
    }
    if (SampleSame(nodeId, dialogs->opener) && Activates(event))
    {
        OpenModal(dialogs);
        return true;
    }
    if (SampleSame(nodeId, dialogs->status) && Activates(event))
    {
        dialogs->baseClicks++;
        return true;
    }
    for (int i = 0; i < FLOATING; i++)
    {
        if (FloatingHears(dialogs, &dialogs->floating[i], nodeId, event))
        {
            return true;
        }
    }
    return false;
}

static void Build(void* user, SampleApp* app)
{
    Dialogs* dialogs = user;
    dialogs->app = app;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    layout.container.columnGap = 12.0f;
    layout.container.alignItems = mui_alignStart;
    layout.padding = (muiEdges){16.0f, 16.0f, 16.0f, 16.0f};
    SampleSetLayout(
        app, app->root, &layout,
        MUI_PROPERTY_BIT(mui_propertyFlexDirection) | MUI_PROPERTY_BIT(mui_propertyColumnGap) |
            MUI_PROPERTY_BIT(mui_propertyAlignItems) | MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
            MUI_PROPERTY_BIT(mui_propertyPaddingEnd) | MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
            MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
    SampleFill(app, app->root, s_background);
    muiVisualStyle base = muiDefaultVisualStyle();
    base.background = SampleColor(s_button);
    muiVisualStyle focused = muiDefaultVisualStyle();
    focused.background = SampleColor(s_focused);
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    SampleAppCheck(app,
                   muiCreateStyle(app->context, &dialogs->buttonStyle) == mui_success &&
                       muiStyle_SetVisualValues(app->context, dialogs->buttonStyle, mui_variantBase,
                                                &base, background) == mui_success &&
                       muiStyle_SetVisualValues(app->context, dialogs->buttonStyle,
                                                mui_variantFocused, &focused,
                                                background) == mui_success,
                   "the buttons' class");
    dialogs->opener = Button(dialogs, app->root, "Discard...");
    dialogs->status =
        SampleTextNode(app, app->root, "Editing", 15.0f, s_text, &dialogs->statusBlock);
    Padding(app, dialogs->status, 0.0f, 6.0f);
    MakeFloating(dialogs, 0, 24.0f, 80.0f);
    MakeFloating(dialogs, 1, 104.0f, 120.0f);
    SampleAppCheck(app,
                   muiSetEventFunction(app->context, Hear, dialogs) == mui_success &&
                       muiSetAccessTextFunction(app->context, muiAccessTextOf, &app->host) ==
                           mui_success,
                   "the event and access text functions");
}

static bool Shows(const SampleApp* app, muiNodeId node, float x, float y, const uint8_t rgb[3])
{
    return SampleNear(SamplePixelOf(app, node, x, y), rgb);
}

static bool Focused(const SampleApp* app, muiNodeId node)
{
    return SampleSame(muiFocus_Get(app->context, 0), node);
}

static bool Holds(const SampleApp* app, muiTextBlockId block, const char* expected)
{
    const char* text = "";
    size_t length = 0;
    (void)muiTextBlock_GetText(app->text, block, &text, &length);
    return length == strlen(expected) && memcmp(text, expected, length) == 0;
}

// The colour where the floating dialogs overlap: the body on top.
static const uint8_t* Overlap(const Dialogs* dialogs, const SampleApp* app)
{
    const Floating* tools = &dialogs->floating[1];
    return SamplePixelOf(app, tools->node, 20.0f, 40.0f);
}

// Whether the modal dialog shows centred, reported modal and named.
static bool ModalShows(const Dialogs* dialogs, const SampleApp* app)
{
    if (dialogs->modal.index1 == 0)
    {
        return false;
    }
    muiRect rect = muiNode_GetRect(app->context, dialogs->modal);
    float x = 0.0f;
    float y = 0.0f;
    char name[32] = {0};
    size_t length = 0;
    const muiAccessTree* tree = muiWindowAccess_GetTree(app->access);
    const muiAccessNode* found = muiAccessTree_Find(tree, muiAccessIdOf(dialogs->modal));
    return muiNode_MapToRoot(app->context, dialogs->modal, rect.width / 2.0f, rect.height / 2.0f,
                             &x, &y) == mui_success &&
           x == (float)app->def->width / 2.0f && y == (float)app->def->height / 2.0f &&
           Shows(app, dialogs->modal, 4.0f, rect.height / 2.0f, s_dialog) && found != NULL &&
           (found->flags & mui_accessModal) != 0 &&
           muiAccessTree_GetName(tree, muiAccessIdOf(dialogs->modal), name, sizeof name, &length) ==
               mui_success &&
           length == 16 && memcmp(name, "Discard changes?", 16) == 0;
}

// The first frame: the base, Tools over Notes, no modal.
static void Still(void* user, SampleApp* app)
{
    const Dialogs* dialogs = user;
    SampleAppCheck(app,
                   Shows(app, dialogs->opener, 4.0f, 4.0f, s_button) &&
                       Holds(app, dialogs->statusBlock, "Editing"),
                   "the base");
    SampleAppCheck(app,
                   SampleNear(Overlap(dialogs, app), s_bodies[1]) && dialogs->modal.index1 == 0,
                   "Tools over Notes, no modal dialog");
}

static void Key(SampleApp* app, mwinKeyCode code)
{
    SamplePostKey(app, code, MWIN_KEY_NAMED | code, NULL);
}

static void Click(SampleApp* app, muiNodeId node, float x, float y)
{
    float rootX = 0.0f;
    float rootY = 0.0f;
    SampleAppCheck(app, muiNode_MapToRoot(app->context, node, x, y, &rootX, &rootY) == mui_success,
                   "a point");
    SamplePostAt(app, mwin_eventCursorMoved, rootX, rootY, 0);
    SamplePostAt(app, mwin_eventButtonDown, rootX, rootY, 1);
    SamplePostAt(app, mwin_eventButtonUp, rootX, rootY, 0);
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Dialogs* dialogs = user;
    Floating* notes = &dialogs->floating[0];
    switch (frame)
    {
    case 0:
        Still(dialogs, app);
        Click(app, dialogs->opener, 8.0f, 8.0f);
        return true;
    case 1:
        SampleAppCheck(app, ModalShows(dialogs, app) && Focused(app, dialogs->cancel),
                       "the modal dialog centred, named, Cancel focused");
        Key(app, mwin_codeTab);
        return true;
    case 2:
        SampleAppCheck(app,
                       Focused(app, dialogs->discard) &&
                           Shows(app, dialogs->discard, 4.0f, 4.0f, s_focused),
                       "Tab moved to Discard");
        Key(app, mwin_codeTab);
        return true;
    case 3:
        SampleAppCheck(app, Focused(app, dialogs->cancel), "Tab wrapped inside the dialog");
        // Presses outside the modal dialog reach nothing; like a press on
        // anything that takes no focus, they leave none.
        Click(app, notes->node, 20.0f, 30.0f);
        Click(app, dialogs->status, 4.0f, 4.0f);
        return true;
    case 4:
        SampleAppCheck(app,
                       dialogs->baseClicks == 0 && dialogs->modal.index1 != 0 &&
                           muiFocus_Get(app->context, 0).index1 == 0,
                       "presses outside the modal dialog reached nothing");
        Key(app, mwin_codeTab);
        return true;
    case 5:
        SampleAppCheck(app, Focused(app, dialogs->cancel),
                       "Tab started again inside the modal dialog");
        Key(app, mwin_codeEscape);
        return true;
    case 6:
    {
        SampleAppCheck(app,
                       dialogs->modal.index1 == 0 && Focused(app, dialogs->opener) &&
                           Holds(app, dialogs->statusBlock, "Kept") &&
                           SampleNear(Overlap(dialogs, app), s_bodies[1]),
                       "Escape closed the dialog, the focus back on its opener, Notes not "
                       "raised by the blocked press");
        // A press on Notes' body raises it; a drag of its bar moves it.
        Click(app, notes->node, 20.0f, 40.0f);
        float x = 0.0f;
        float y = 0.0f;
        SampleAppCheck(
            app, muiNode_MapToRoot(app->context, notes->bar, 20.0f, 8.0f, &x, &y) == mui_success,
            "Notes' bar");
        SamplePostAt(app, mwin_eventCursorMoved, x, y, 0);
        SamplePostAt(app, mwin_eventButtonDown, x, y, 1);
        SamplePostAt(app, mwin_eventCursorMoved, x + 10.0f, y + 5.0f, 1);
        SamplePostAt(app, mwin_eventCursorMoved, x + 30.0f, y + 20.0f, 1);
        SamplePostAt(app, mwin_eventButtonUp, x + 30.0f, y + 20.0f, 0);
        return true;
    }
    default:
    {
        float x = 0.0f;
        float y = 0.0f;
        SampleAppCheck(app,
                       SampleNear(Overlap(dialogs, app), s_bodies[0]) &&
                           muiNode_MapToRoot(app->context, notes->node, 0.0f, 0.0f, &x, &y) ==
                               mui_success &&
                           x == 54.0f && y == 100.0f,
                       "Notes raised over Tools and moved by its bar's drag");
        return false;
    }
    }
}

int main(int count, char** arguments)
{
    static Dialogs dialogs;
    const SampleAppDef def = {
        .width = 320,
        .height = 260,
        .build = Build,
        .script = Script,
        .still = Still,
        .user = &dialogs,
    };
    return SampleRunApp(&def, count, arguments);
}
