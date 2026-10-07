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
// leave are not the UI's; a lost focus cancels the press it held. Then
// touches: one on the button the UI's and one on the root not, a move,
// a release and its click, a cancel, a touch past the glue's ten left,
// and a lost focus cancelling the ten; and the pen: hovering, its tip,
// its barrel and its eraser, each the button the W3C numbers it. Last, a
// press on a panel the host leaves is still the UI's, as the panel does
// not pass input through, and so is the mouse it holds over the root
// until it lets go. And a gamepad: the d-pad navigating at once and
// repeating after the delay at the interval, through ticks; the stick
// holding a direction past its threshold, keeping it above seven tenths
// of it and letting go below; its faces and shoulders. Then the caret:
// text input on with the caret placed in the window, off with the null
// id, and carets refused.

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

#include <math.h>
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
    muiNodeId panel;
    muiWindowGlue* glue;
    // What the glue said of each record fed, by type.
    mwinEventType types[MAX_RECORDS];
    bool handled[MAX_RECORDS];
    int count;
    // What the host's function heard at the button.
    muiPointerRecordKind records[MAX_RECORDS];
    muiPointerKind recordKinds[MAX_RECORDS];
    uint8_t recordButtons[MAX_RECORDS];
    muiPointerButtons recordHeld[MAX_RECORDS];
    int recordCount;
    int keys;
    int texts;
    uint8_t pressedButton;
    int wheels;
    float wheelX;
    float wheelY;
    muiModifiers wheelModifiers;
    // The navigations the root heard, in order.
    muiNavigation navigations[MAX_RECORDS];
    int navigationCount;
    mwinGamepadId gamepad;
    uint64_t start;
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
    if (event->kind == mui_eventNavigation && Same(nodeId, test->root) &&
        phase == mui_phaseTunnel && test->navigationCount < MAX_RECORDS)
    {
        test->navigations[test->navigationCount++] = event->navigation;
    }
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
            test->recordKinds[test->recordCount] = event->pointer->pointerKind;
            test->recordButtons[test->recordCount] = event->pointer->button;
            test->recordHeld[test->recordCount] = event->pointer->buttons;
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
    // A panel from 200 to 300 whose presses the host leaves.
    test->panel = Node(test->context, test->root, 200, 10, 100, 100);
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
    const muiLayoutInput input = {640.0f, 480.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
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
    def.gamepads = true;
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
        bool input = event.type >= mwin_eventInputStateReset && event.type <= mwin_eventPenButtonUp;
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

static mwinEvent Touch(mwinWindowId window, mwinEventType type, uint64_t id, float x, float y)
{
    mwinEvent event = {.type = type, .window = window};
    event.data.touch.id = id;
    event.data.touch.position = (mwinPosition){x, y};
    return event;
}

static mwinEvent Pen(mwinWindowId window, mwinEventType type, mwinPenFlags flags, uint8_t button)
{
    mwinEvent event = {.type = type, .window = window};
    event.data.pen.position = (mwinPosition){50, 50};
    event.data.pen.flags = flags;
    event.data.pen.button = button;
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

static void PostTouches(Test* test, mwinContext* windows)
{
    const uint64_t first = 0xA000000000000001u;
    const uint64_t second = 0xB000000000000002u;
    const mwinEvent events[] = {
        Touch(test->window, mwin_eventTouchDown, first, 50, 50),
        Touch(test->window, mwin_eventTouchDown, second, 300, 300),
        Touch(test->window, mwin_eventTouchMoved, first, 55, 55),
        Touch(test->window, mwin_eventTouchUp, first, 55, 55),
        Touch(test->window, mwin_eventTouchCancelled, second, 300, 300),
    };
    Post(windows, events, 5);
}

static void CheckTouches(const Test* test)
{
    static const bool handled[] = {true, false, true, true, false};
    bool same = test->count == 5;
    for (int i = 0; same && i < 5; i++)
    {
        same = test->handled[i] == handled[i];
    }
    CHECK(same, "a touch on the button the UI's, one on the root not, a move, an up, a cancel");
    // A touch captures what it presses, so its move is recorded, and
    // lets it go as it lifts.
    CHECK(test->recordCount == 5 && test->records[0] == mui_pointerRecordPress &&
              test->records[1] == mui_pointerRecordMove &&
              test->records[2] == mui_pointerRecordRelease &&
              test->records[3] == mui_pointerRecordClick &&
              test->records[4] == mui_pointerRecordCaptureLost &&
              test->recordKinds[0] == mui_pointerTouch,
          "the touch pressed, moved, released and clicked the button, its capture let go");
}

// Eleven touches on the button, then a lost focus.
static void PostMany(Test* test, mwinContext* windows)
{
    for (uint64_t i = 0; i < MUI_WINDOW_TOUCHES + 1; i++)
    {
        const mwinEvent down = Touch(test->window, mwin_eventTouchDown, 100 + i, 20, 20);
        Post(windows, &down, 1);
    }
    const mwinEvent lost = {.type = mwin_eventFocusLost, .window = test->window};
    Post(windows, &lost, 1);
}

static void CheckMany(const Test* test)
{
    int presses = 0;
    int cancels = 0;
    for (int i = 0; i < test->recordCount; i++)
    {
        presses += test->records[i] == mui_pointerRecordPress ? 1 : 0;
        cancels += test->records[i] == mui_pointerRecordCancel ? 1 : 0;
    }
    CHECK(presses == (int)MUI_WINDOW_TOUCHES && cancels == (int)MUI_WINDOW_TOUCHES,
          "ten touches followed, the eleventh left, the ten cancelled by a lost focus");
}

static void PostPen(Test* test, mwinContext* windows)
{
    const mwinEvent events[] = {
        Pen(test->window, mwin_eventPenMoved, 0, 0),
        Pen(test->window, mwin_eventPenDown, mwin_penContact, 0),
        Pen(test->window, mwin_eventPenButtonDown, mwin_penContact | mwin_penBarrel, 1),
        Pen(test->window, mwin_eventPenUp, mwin_penBarrel, 0),
        Pen(test->window, mwin_eventPenButtonUp, 0, 1),
        Pen(test->window, mwin_eventPenDown, mwin_penContact | mwin_penEraser, 0),
        Pen(test->window, mwin_eventPenUp, mwin_penEraser, 0),
    };
    Post(windows, events, 7);
}

static void CheckPen(const Test* test)
{
    CHECK(test->count == 7 && test->handled[0], "the pen hovering over the button the UI's");
    // Presses and releases with their buttons, a click after each
    // release, the pen's capture let go as its last button, the barrel
    // held after the tip lifts, lifts.
    static const muiPointerRecordKind kinds[] = {
        mui_pointerRecordPress,       mui_pointerRecordPress,       mui_pointerRecordRelease,
        mui_pointerRecordClick,       mui_pointerRecordRelease,     mui_pointerRecordClick,
        mui_pointerRecordCaptureLost, mui_pointerRecordPress,       mui_pointerRecordRelease,
        mui_pointerRecordClick,       mui_pointerRecordCaptureLost,
    };
    static const uint8_t buttons[] = {0, 1, 0, 0, 1, 1, 0, 5, 5, 5, 0};
    bool same = test->recordCount == 11;
    for (int i = 0; same && i < 11; i++)
    {
        same = test->records[i] == kinds[i] && test->recordButtons[i] == buttons[i] &&
               test->recordKinds[i] == mui_pointerPen;
    }
    if (!same)
    {
        for (int i = 0; i < test->recordCount; i++)
        {
            printf("  record %d: kind %u button %u\n", i, test->records[i], test->recordButtons[i]);
        }
    }
    // As the tip lifts the barrel alone is held, and the eraser in
    // contact holds button 5 alone.
    same =
        same && test->recordHeld[2] == 1u << mui_buttonSecondary && test->recordHeld[7] == 1u << 5;
    CHECK(same, "the pen's tip primary, its barrel secondary and held after the tip lifts, its "
                "eraser button 5");
}

static void PostHold(Test* test, mwinContext* windows)
{
    const mwinEvent events[] = {
        Cursor(test->window, mwin_eventCursorMoved, 250, 50, 0, 0),
        Cursor(test->window, mwin_eventButtonDown, 250, 50, mwin_buttonLeft, 1),
        Cursor(test->window, mwin_eventCursorMoved, 400, 300, 0, 1),
        Cursor(test->window, mwin_eventButtonUp, 400, 300, mwin_buttonLeft, 0),
        Cursor(test->window, mwin_eventCursorMoved, 410, 300, 0, 0),
    };
    Post(windows, events, 5);
}

static void CheckHold(const Test* test)
{
    static const bool handled[] = {true, true, true, true, false};
    bool same = test->count == 5;
    for (int i = 0; same && i < 5; i++)
    {
        same = test->handled[i] == handled[i];
    }
    CHECK(same, "a press the host leaves on a panel holds the mouse for the UI until it lets go");
}

// Ticks the glue at a time after the start, in milliseconds.
static void TickAt(Test* test, uint64_t ms)
{
    bool handled = false;
    CHECK(muiWindowGlue_Tick(test->glue, test->start + ms * 1000000u, &handled) == mui_success,
          "ticked");
}

// Moves the test clock to a time after the start, in milliseconds.
static void ClockAt(Test* test, mwinContext* windows, uint64_t ms)
{
    CHECK(mwinTestSetTime(windows, test->start + ms * 1000000u) == mwin_success, "the clock");
}

static void PostDpad(Test* test, mwinContext* windows)
{
    mwinGamepadInfo info = {.mapped = true};
    test->start = 1000000000u;
    ClockAt(test, windows, 0);
    CHECK(mwinTestAddGamepad(windows, &info, &test->gamepad) == mwin_success &&
              mwinTestGamepadButton(windows, test->gamepad, mwin_padDpadDown, true) == mwin_success,
          "a gamepad, its d-pad down");
    test->navigationCount = 0;
}

static void RepeatDpad(Test* test, mwinContext* windows)
{
    CHECK(test->navigationCount == 1, "the d-pad navigates at once");
    TickAt(test, 300);
    CHECK(test->navigationCount == 1, "nothing before the delay");
    TickAt(test, 400);
    TickAt(test, 450);
    CHECK(test->navigationCount == 2, "once at the delay");
    TickAt(test, 500);
    CHECK(test->navigationCount == 3, "again at the interval");
    ClockAt(test, windows, 600);
    mwinGamepadId pad = test->gamepad;
    CHECK(mwinTestGamepadButton(windows, pad, mwin_padDpadDown, false) == mwin_success &&
              mwinTestGamepadAxis(windows, pad, mwin_padStickLeftY, 0.8f) == mwin_success &&
              mwinTestGamepadAxis(windows, pad, mwin_padStickLeftY, 0.4f) == mwin_success,
          "the d-pad let go, the stick down and eased to 0.4");
}

// The stick eased above seven tenths of its threshold still holds down.
static void EaseStick(Test* test, mwinContext* windows)
{
    CHECK(test->navigationCount == 4, "the stick down navigates once");
    TickAt(test, 1000);
    CHECK(test->navigationCount == 5, "eased, it still repeats");
    ClockAt(test, windows, 1100);
    mwinGamepadId pad = test->gamepad;
    CHECK(mwinTestGamepadAxis(windows, pad, mwin_padStickLeftY, 0.2f) == mwin_success &&
              mwinTestGamepadAxis(windows, pad, mwin_padStickLeftX, -0.9f) == mwin_success,
          "the stick let go, then left");
    static const uint8_t buttons[] = {mwin_padFaceSouth, mwin_padFaceEast, mwin_padShoulderLeft,
                                      mwin_padShoulderRight};
    for (int i = 0; i < 4; i++)
    {
        CHECK(mwinTestGamepadButton(windows, pad, buttons[i], true) == mwin_success,
              "a face or a shoulder");
    }
}

static void CheckPad(Test* test)
{
    TickAt(test, 3000);
    static const muiNavigation expected[] = {
        mui_navigateDown,     mui_navigateDown, mui_navigateDown,     mui_navigateDown,
        mui_navigateDown,     mui_navigateLeft, mui_navigateActivate, mui_navigateCancel,
        mui_navigatePrevious, mui_navigateNext, mui_navigateLeft,
    };
    bool same = test->navigationCount == 11;
    for (int i = 0; same && i < 11; i++)
    {
        same = test->navigations[i] == expected[i];
    }
    if (!same)
    {
        for (int i = 0; i < test->navigationCount; i++)
        {
            printf("  navigation %d: %u\n", i, test->navigations[i]);
        }
    }
    CHECK(same, "the stick down once, eased and let go, then left; activate, cancel, previous, "
                "next; the stick's left repeating");
}

static const muiNodeId s_nullNode = {0, 0};

static bool AcceptsText(const Test* test, mwinContext* windows)
{
    mwinWindowState state;
    return mwinGetWindowState(windows, test->window, &state) == mwin_success && state.textInput;
}

// A caret 20 tall at the button's 5, 6: in the window at 15, 16.
static void SetCaret(Test* test)
{
    mwinRect placed = {0};
    CHECK(muiWindowGlue_SetCaret(test->glue, test->button, (muiRect){5.0f, 6.0f, 0.0f, 20.0f},
                                 &placed) == mui_success &&
              placed.x == 15.0f && placed.y == 16.0f && placed.width == 0.0f &&
              placed.height == 20.0f,
          "the caret placed in the window");
    placed.x = 7.0f;
    CHECK(muiWindowGlue_SetCaret(test->glue, test->button, (muiRect){0.0f, 0.0f, -1.0f, 20.0f},
                                 &placed) == mui_errorInvalid &&
              muiWindowGlue_SetCaret(test->glue, test->button, (muiRect){0.0f, 0.0f, 0.0f, NAN},
                                     &placed) == mui_errorInvalid &&
              placed.x == 7.0f,
          "a caret of a negative size or not finite refused, the output kept");
}

static void StopCaret(Test* test, mwinContext* windows)
{
    CHECK(AcceptsText(test, windows), "the window accepts text");
    mwinRect placed = {1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(muiWindowGlue_SetCaret(test->glue, s_nullNode, (muiRect){0}, &placed) == mui_success &&
              placed.x == 0.0f && placed.height == 0.0f,
          "the null id stops");
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
    case 2:
        Feed(test, windows);
        CHECK(test->recordCount == 2 && test->records[0] == mui_pointerRecordPress &&
                  test->records[1] == mui_pointerRecordCancel,
              "a lost focus cancels the press held");
        test->recordCount = 0;
        PostTouches(test, windows);
        break;
    case 3:
        Feed(test, windows);
        CheckTouches(test);
        test->recordCount = 0;
        PostMany(test, windows);
        break;
    case 4:
        Feed(test, windows);
        CheckMany(test);
        test->recordCount = 0;
        PostPen(test, windows);
        break;
    case 5:
        Feed(test, windows);
        CheckPen(test);
        PostHold(test, windows);
        break;
    case 6:
        Feed(test, windows);
        CheckHold(test);
        PostDpad(test, windows);
        break;
    case 7:
        Feed(test, windows);
        RepeatDpad(test, windows);
        break;
    case 8:
        Feed(test, windows);
        EaseStick(test, windows);
        break;
    case 9:
        Feed(test, windows);
        CheckPad(test);
        SetCaret(test);
        break;
    case 10:
        Feed(test, windows);
        StopCaret(test, windows);
        break;
    default:
        Feed(test, windows);
        CHECK(!AcceptsText(test, windows), "the window no longer accepts text");
        test->done = true;
        break;
    }
    return test->done || test->frame > 20 ? mwin_frameStop : mwin_frameContinue;
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
