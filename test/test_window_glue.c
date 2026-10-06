// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Maul Window glue on Maul Window's headless test backend: a
// window's records posted as a platform reports them, drained, and fed
// to a context whose tree is a root that passes input through and a
// button in it. The cursor over the button is the UI's and over the root
// is not; a press, its release and its click reach the button; keys and
// text reach the focus, a key no one handles is not the UI's; the wheel
// turns at the cursor's last place; another window's records and a
// leave are not the UI's; a lost focus cancels the press it held.

#include "test_harness.h"

#include "maul-ui-window/glue.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"
#include "maul-window/context.h"
#include "maul-window/test.h"
#include "maul-window/window.h"

#include <string.h>

enum
{
    MAX_RECORDS = 32
};

typedef struct Test
{
    int frame;
    bool done;
    mwinWindowId window;
    mwinWindowId other;
    muiContext* context;
    muiNodeId root;
    muiNodeId button;
    muiWindowGlue* glue;
    // What the glue said of each record fed, by type.
    mwinEventType types[MAX_RECORDS];
    bool handled[MAX_RECORDS];
    int count;
    // What the host's function heard at the button.
    muiPointerRecordKind records[MAX_RECORDS];
    int recordCount;
    int keys;
    int texts;
    uint8_t pressedButton;
    int wheels;
    float wheelX;
    float wheelY;
    muiModifiers wheelModifiers;
} Test;

static const muiNodeId s_null = {0, 0};

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// Handles, at the button as it bubbles, the key 'a', text, and pointer
// presses, releases and clicks; hears the wheel and leaves it.
static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Test* test = user;
    if (!Same(nodeId, test->button) || phase != mui_phaseBubble)
    {
        return false;
    }
    switch (event->kind)
    {
    case mui_eventKeyDown:
        test->keys++;
        return event->key == 'a';
    case mui_eventText:
        test->texts++;
        return true;
    case mui_eventWheel:
        test->wheels++;
        test->wheelX = event->wheel->x;
        test->wheelY = event->wheel->y;
        test->wheelModifiers = event->wheel->modifiers;
        return false;
    case mui_eventPointer:
        if (test->recordCount < MAX_RECORDS)
        {
            test->records[test->recordCount++] = event->pointer->kind;
        }
        test->pressedButton = event->pointer->button;
        return event->pointer->kind == mui_pointerRecordPress ||
               event->pointer->kind == mui_pointerRecordRelease ||
               event->pointer->kind == mui_pointerRecordClick;
    default:
        return false;
    }
}

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

static muiNodeId Node(muiContext* context, muiNodeId parent, float x, float y, float width,
                      float height)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_null;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "a node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_null) == mui_success, "inserted");
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = Length(width);
    layout.sizing.height = Length(height);
    layout.placement.position = parent.index1 != 0 ? mui_positionAbsolute : mui_positionFlow;
    layout.placement.inset.start = Length(x);
    layout.placement.inset.top = Length(y);
    CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight) |
                                      MUI_PROPERTY_BIT(mui_propertyPosition) |
                                      MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                                      MUI_PROPERTY_BIT(mui_propertyInsetTop)) == mui_success,
          "placed");
    return node;
}

// A root of the window's size that passes input through, and a button
// from 10 to 110 that takes focus.
static void MakeTree(Test* test)
{
    muiContextDef def = muiDefaultContextDef();
    CHECK(muiCreateContext(&def, &test->context) == mui_success, "a context");
    test->root = Node(test->context, s_null, 0, 0, 640, 480);
    test->button = Node(test->context, test->root, 10, 10, 100, 100);
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.passThrough = true;
    CHECK(muiNode_SetInteractionValues(test->context, test->root, &values,
                                       MUI_PROPERTY_BIT(mui_propertyPassThrough)) == mui_success,
          "the root passes input through");
    values = muiDefaultInteractionStyle();
    values.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(test->context, test->button, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "the button takes focus");
    const muiLayoutInput input = {640.0f, 480.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(test->context, test->root, &input) == mui_success &&
              muiSetEventFunction(test->context, Hear, test) == mui_success &&
              muiFocus_Set(test->context, 0, test->button, mui_focusByCode) == mui_success,
          "laid out, heard and focused");
}

static mwinWindowId Create(mwinContext* windows)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){640.0f, 480.0f};
    mwinWindowId window = {0};
    CHECK(mwinCreateWindow(windows, &def, &window, NULL) == mwin_success, "a window");
    return window;
}

static mwinResult Init(mwinContext* windows, void* user)
{
    Test* test = user;
    test->window = Create(windows);
    test->other = Create(windows);
    MakeTree(test);
    muiWindowGlueDef def = muiDefaultWindowGlueDef();
    def.windows = windows;
    def.window = test->window;
    def.context = test->context;
    def.root = test->root;
    CHECK(muiCreateWindowGlue(&def, &test->glue) == mui_success, "a glue");
    return mwin_success;
}

// Feeds the frame's records to the glue, keeping what it said of input.
static void Feed(Test* test, mwinContext* windows)
{
    test->count = 0;
    mwinEvent event;
    while (mwinNextEvent(windows, &event) == mwin_success)
    {
        bool handled = true;
        CHECK(muiWindowGlue_HandleEvent(test->glue, &event, &handled) == mui_success,
              "a record taken");
        bool input = event.type >= mwin_eventInputStateReset && event.type <= mwin_eventWheel;
        if ((input || event.type == mwin_eventFocusLost) && test->count < MAX_RECORDS)
        {
            test->types[test->count] = event.type;
            test->handled[test->count++] = handled;
        }
    }
}

static mwinEvent Cursor(mwinWindowId window, mwinEventType type, float x, float y, uint8_t button,
                        uint8_t buttons)
{
    mwinEvent event = {.type = type, .window = window};
    event.data.pointer.position = (mwinPosition){x, y};
    event.data.pointer.button = button;
    event.data.pointer.buttons = buttons;
    return event;
}

static mwinEvent Key(mwinWindowId window, mwinKeyCode code, mwinKey key, mwinModifiers modifiers)
{
    mwinEvent event = {.type = mwin_eventKeyDown, .window = window};
    event.data.key.code = code;
    event.data.key.key = key;
    event.data.key.modifiers = modifiers;
    return event;
}

static void Post(mwinContext* windows, const mwinEvent* events, int count)
{
    for (int i = 0; i < count; i++)
    {
        CHECK(mwinTestPost(windows, &events[i]) == mwin_success, "posted");
    }
}

static void PostInput(Test* test, mwinContext* windows)
{
    mwinEvent text = {.type = mwin_eventTextInput, .window = test->window};
    text.data.text.text = "x";
    text.data.text.length = 1;
    mwinEvent wheel = {.type = mwin_eventWheel, .window = test->window};
    wheel.data.wheel.y = 1.0f;
    const mwinEvent events[] = {
        Cursor(test->window, mwin_eventCursorMoved, 50, 50, 0, 0),
        Cursor(test->window, mwin_eventCursorMoved, 300, 300, 0, 0),
        Cursor(test->window, mwin_eventCursorMoved, 60, 40, 0, 0),
        Cursor(test->window, mwin_eventButtonDown, 60, 40, mwin_buttonLeft, 1),
        Cursor(test->window, mwin_eventButtonUp, 60, 40, mwin_buttonLeft, 0),
        Key(test->window, mwin_codeKeyA, 'a', 0),
        Key(test->window, mwin_codeKeyB, 'b', mwin_modShift),
        text,
        wheel,
        Key(test->other, mwin_codeKeyA, 'a', 0),
        Cursor(test->window, mwin_eventCursorLeft, 700, 40, 0, 0),
    };
    Post(windows, events, (int)(sizeof events / sizeof events[0]));
}

static void CheckInput(const Test* test)
{
    static const mwinEventType types[] = {
        mwin_eventCursorMoved, mwin_eventCursorMoved, mwin_eventCursorMoved, mwin_eventButtonDown,
        mwin_eventButtonUp,    mwin_eventKeyDown,     mwin_eventKeyDown,     mwin_eventTextInput,
        mwin_eventWheel,       mwin_eventKeyDown,     mwin_eventCursorLeft,
    };
    static const bool handled[] = {true,  false, true,  true,  true, true,
                                   false, true,  false, false, false};
    bool same = test->count == 11;
    for (int i = 0; same && i < 11; i++)
    {
        same = test->types[i] == types[i] && test->handled[i] == handled[i];
    }
    CHECK(same, "the UI's: over the button, a press and release, 'a', text; not: over the root, "
                "'b', the wheel, another window's key, a leave");
    CHECK(test->recordCount == 3 && test->records[0] == mui_pointerRecordPress &&
              test->records[1] == mui_pointerRecordRelease &&
              test->records[2] == mui_pointerRecordClick &&
              test->pressedButton == mui_buttonPrimary,
          "a press, its release and its click of the primary button at the button");
    CHECK(test->keys == 2 && test->texts == 1, "two keys and text at the focus");
    CHECK(test->wheels == 1 && test->wheelX == 60.0f && test->wheelY == 40.0f &&
              test->wheelModifiers == mui_modShift,
          "the wheel at the cursor's last place, with the modifiers last reported");
}

static mwinFrameResult Frame(mwinContext* windows, void* user)
{
    Test* test = user;
    switch (test->frame++)
    {
    case 0:
        Feed(test, windows);
        PostInput(test, windows);
        break;
    case 1:
        Feed(test, windows);
        CheckInput(test);
        test->recordCount = 0;
        Post(windows,
             (const mwinEvent[]){
                 Cursor(test->window, mwin_eventCursorMoved, 50, 50, 0, 0),
                 Cursor(test->window, mwin_eventButtonDown, 50, 50, mwin_buttonLeft, 1),
                 {.type = mwin_eventFocusLost, .window = test->window}},
             3);
        break;
    default:
        Feed(test, windows);
        CHECK(test->recordCount == 2 && test->records[0] == mui_pointerRecordPress &&
                  test->records[1] == mui_pointerRecordCancel,
              "a lost focus cancels the press held");
        test->done = true;
        break;
    }
    return test->done || test->frame > 10 ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Test test = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.init = Init;
    def.frame = Frame;
    def.user = &test;
    CHECK(mwinRun(&def) == mwin_success && test.done, "the program ran");
    muiDestroyWindowGlue(test.glue);
    muiDestroyContext(test.context);
    return s_failures == 0 ? 0 : 1;
}
