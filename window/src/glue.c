// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Maul Window glue (record mui-0007). Maul Window's button b is bit
// b - 1 of its held mask and Maul UI's index b - 1 is bit b - 1 of its
// own, so the masks pass as they are and an index is the button less
// one. A touch's 64-bit id takes one of the touch slots while it lasts,
// its pointer id the slot's; the pen's tip is the primary button, or
// button 5 while it erases, and its barrel the secondary, as the W3C's
// Pointer Events number them.

#include "maul-ui-window/glue.h"

#include "allocator.h"
#include "gamepads.h"

#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"

#include <stdalign.h>

#define DEF_COOKIE 0x6D757767u // "muwg"

// The pointer ids: the window's mouse, its pen, then its touches, as
// many as there are slots.
#define MOUSE       0u
#define PEN         1u
#define FIRST_TOUCH 2u
#define TOUCHES     MUI_WINDOW_TOUCHES

// The pen's eraser, as the W3C's Pointer Events number it.
#define ERASER 5u

struct muiWindowGlue
{
    muiAllocator allocator;
    mwinContext* windows;
    mwinWindowId window;
    muiContext* context;
    muiNodeId root;
    uint8_t player;
    // The cursor's last place, the buttons it holds and the modifiers
    // last reported.
    float x;
    float y;
    muiPointerButtons buttons;
    muiModifiers modifiers;
    // The pen's last place and the buttons it holds.
    float penX;
    float penY;
    muiPointerButtons penButtons;
    // The touches in contact, by slot.
    uint64_t touchIds[TOUCHES];
    bool touching[TOUCHES];
    float touchX[TOUCHES];
    float touchY[TOUCHES];
    // The pointers whose press was the UI's, by id, while they hold it.
    uint32_t held;
    // Whether it takes gamepads, and what they hold.
    bool gamepads;
    muiWindowPads pads;
};

muiWindowGlueDef muiDefaultWindowGlueDef(void)
{
    return (muiWindowGlueDef){
        .cookie = DEF_COOKIE,
        .stickThreshold = 0.5f,
        .repeatDelayNs = 400000000u,
        .repeatIntervalNs = 100000000u,
    };
}

static bool IsValid(const muiWindowGlueDef* def)
{
    return def->cookie == DEF_COOKIE && muiWindowIsAllocatorValid(&def->allocator) &&
           def->windows != nullptr && def->context != nullptr && def->root.index1 != 0 &&
           def->player < MUI_MAX_PLAYERS && def->stickThreshold >= 0.1f &&
           def->stickThreshold <= 1.0f && def->repeatIntervalNs > 0;
}

muiResult muiCreateWindowGlue(const muiWindowGlueDef* def, muiWindowGlue** glueOut)
{
    if (glueOut != nullptr)
    {
        *glueOut = nullptr;
    }
    if (def == nullptr || glueOut == nullptr || !IsValid(def))
    {
        return mui_errorInvalid;
    }
    muiWindowGlue* glue =
        muiWindowAllocate(&def->allocator, sizeof(muiWindowGlue), alignof(muiWindowGlue));
    if (glue == nullptr)
    {
        return mui_errorCapacity;
    }
    *glue = (muiWindowGlue){
        .allocator = def->allocator,
        .windows = def->windows,
        .window = def->window,
        .context = def->context,
        .root = def->root,
        .player = def->player,
        .gamepads = def->gamepads,
        .pads =
            {
                .player = def->gamepadPlayer,
                .playerContext = def->gamepadContext,
                .confirmEast = def->confirmEast,
                .threshold = def->stickThreshold,
                .delayNs = def->repeatDelayNs,
                .intervalNs = def->repeatIntervalNs,
            },
    };
    *glueOut = glue;
    return mui_success;
}

void muiDestroyWindowGlue(muiWindowGlue* glue)
{
    if (glue == nullptr)
    {
        return;
    }
    const muiAllocator allocator = glue->allocator;
    muiWindowRelease(&allocator, glue, sizeof(muiWindowGlue), alignof(muiWindowGlue));
}

static bool IsMine(const muiWindowGlue* glue, mwinWindowId window)
{
    return window.index1 == glue->window.index1 && window.generation == glue->window.generation;
}

// Whether a record is the UI's: handled, or at a node that does not
// pass input through (the host hands the game only what is neither).
static bool IsTheUis(const muiPointerRecord* record, bool handled)
{
    return handled || !record->passThrough;
}

// Dispatches every pointer record waiting: whether one was the UI's. A
// press that was the UI's holds its pointer for the UI until it lets go.
static muiResult DispatchRecords(muiWindowGlue* glue, bool* handledOut)
{
    muiPointerRecord record;
    while (muiNextPointerRecord(glue->context, &record) == mui_success)
    {
        bool handled = false;
        muiResult status = muiDispatchPointerRecord(glue->context, &record, &handled);
        if (status != mui_success)
        {
            return status;
        }
        bool theUis = record.kind != mui_pointerRecordDropped && IsTheUis(&record, handled);
        if (theUis && record.kind == mui_pointerRecordPress && record.pointer < 32)
        {
            glue->held |= 1u << record.pointer;
        }
        *handledOut = *handledOut || theUis;
    }
    return mui_success;
}

// Whether a point hits a node that does not pass input through.
static bool Covers(const muiWindowGlue* glue, float x, float y)
{
    muiHit hit;
    return muiHitTest(glue->context, glue->root, x, y, &hit) == mui_success &&
           hit.node.index1 != 0 && !hit.passThrough;
}

// Feeds a pointer event and dispatches its records: whether it is the
// UI's, by a record, by a press the UI holds, or by where it is.
static muiResult Feed(muiWindowGlue* glue, const muiPointerEvent* pointer, bool* handledOut)
{
    muiResult status = muiPointerInput(glue->context, glue->root, pointer);
    status = status == mui_success ? DispatchRecords(glue, handledOut) : status;
    if (status != mui_success)
    {
        return status;
    }
    uint32_t bit = 1u << pointer->pointer;
    bool ends = pointer->action == mui_pointerLeave || pointer->action == mui_pointerCancel;
    *handledOut =
        *handledOut || (glue->held & bit) != 0 || (!ends && Covers(glue, pointer->x, pointer->y));
    if (pointer->buttons == 0 || pointer->action == mui_pointerCancel)
    {
        glue->held &= ~bit;
    }
    return mui_success;
}

static muiResult Mouse(muiWindowGlue* glue, uint64_t timeNs, muiPointerAction action,
                       uint8_t button, bool* handledOut)
{
    const muiPointerEvent pointer = {
        .timeNs = timeNs,
        .pointer = MOUSE,
        .kind = mui_pointerMouse,
        .action = action,
        .button = button,
        .buttons = glue->buttons,
        .x = glue->x,
        .y = glue->y,
        .player = glue->player,
    };
    return Feed(glue, &pointer, handledOut);
}

// The slot of a touch in contact, or TOUCHES.
static uint32_t FindTouch(const muiWindowGlue* glue, uint64_t id)
{
    for (uint32_t i = 0; i < TOUCHES; i++)
    {
        if (glue->touching[i] && glue->touchIds[i] == id)
        {
            return i;
        }
    }
    return TOUCHES;
}

static muiResult TouchAt(muiWindowGlue* glue, uint32_t slot, uint64_t timeNs,
                         muiPointerAction action, bool* handledOut)
{
    bool holds = action == mui_pointerPress || action == mui_pointerMove;
    const muiPointerEvent pointer = {
        .timeNs = timeNs,
        .pointer = FIRST_TOUCH + slot,
        .kind = mui_pointerTouch,
        .action = action,
        .button = mui_buttonPrimary,
        .buttons = holds ? 1u << mui_buttonPrimary : 0u,
        .x = glue->touchX[slot],
        .y = glue->touchY[slot],
        .player = glue->player,
    };
    if (!holds)
    {
        glue->touching[slot] = false;
    }
    return Feed(glue, &pointer, handledOut);
}

// A touch: one that begins takes a free slot, or is left when none is.
static muiResult Touch(muiWindowGlue* glue, const mwinEvent* event, bool* handledOut)
{
    const mwinTouchEvent* touch = &event->data.touch;
    uint32_t slot = FindTouch(glue, touch->id);
    if (event->type == mwin_eventTouchDown && slot == TOUCHES)
    {
        for (slot = 0; slot < TOUCHES && glue->touching[slot]; slot++)
        {
        }
        if (slot == TOUCHES)
        {
            return mui_success;
        }
        glue->touching[slot] = true;
        glue->touchIds[slot] = touch->id;
    }
    if (slot == TOUCHES)
    {
        return mui_success;
    }
    glue->touchX[slot] = touch->position.x;
    glue->touchY[slot] = touch->position.y;
    muiPointerAction action = event->type == mwin_eventTouchDown    ? mui_pointerPress
                              : event->type == mwin_eventTouchMoved ? mui_pointerMove
                              : event->type == mwin_eventTouchUp    ? mui_pointerRelease
                                                                    : mui_pointerCancel;
    return TouchAt(glue, slot, event->timeNs, action, handledOut);
}

static muiResult PenAt(muiWindowGlue* glue, uint64_t timeNs, muiPointerAction action,
                       uint8_t button, bool* handledOut)
{
    const muiPointerEvent pointer = {
        .timeNs = timeNs,
        .pointer = PEN,
        .kind = mui_pointerPen,
        .action = action,
        .button = button,
        .buttons = glue->penButtons,
        .x = glue->penX,
        .y = glue->penY,
        .player = glue->player,
    };
    return Feed(glue, &pointer, handledOut);
}

// The pen: its tip in contact holds the primary button, or the eraser
// while the eraser end is in use, and its barrel the secondary.
static muiResult Pen(muiWindowGlue* glue, const mwinEvent* event, bool* handledOut)
{
    const mwinPenEvent* pen = &event->data.pen;
    glue->penX = pen->position.x;
    glue->penY = pen->position.y;
    uint8_t tip = (pen->flags & mwin_penEraser) != 0 ? ERASER : mui_buttonPrimary;
    glue->penButtons =
        (muiPointerButtons)(((pen->flags & mwin_penContact) != 0 ? 1u << tip : 0u) |
                            ((pen->flags & mwin_penBarrel) != 0 ? 1u << mui_buttonSecondary : 0u));
    switch (event->type)
    {
    case mwin_eventPenDown:
        return PenAt(glue, event->timeNs, mui_pointerPress, tip, handledOut);
    case mwin_eventPenUp:
        return PenAt(glue, event->timeNs, mui_pointerRelease, tip, handledOut);
    case mwin_eventPenButtonDown:
    case mwin_eventPenButtonUp:
        if (pen->button != 1)
        {
            return mui_success;
        }
        return PenAt(glue, event->timeNs,
                     event->type == mwin_eventPenButtonDown ? mui_pointerPress : mui_pointerRelease,
                     mui_buttonSecondary, handledOut);
    default:
        return PenAt(glue, event->timeNs, mui_pointerMove, 0, handledOut);
    }
}

static muiResult Cursor(muiWindowGlue* glue, const mwinEvent* event, bool* handledOut)
{
    const mwinPointerEvent* cursor = &event->data.pointer;
    glue->x = cursor->position.x;
    glue->y = cursor->position.y;
    glue->modifiers = cursor->modifiers;
    if (event->type == mwin_eventCursorLeft)
    {
        return Mouse(glue, event->timeNs, mui_pointerLeave, 0, handledOut);
    }
    if (event->type == mwin_eventCursorMoved || event->type == mwin_eventCursorEntered)
    {
        glue->buttons = cursor->buttons;
        return Mouse(glue, event->timeNs, mui_pointerMove, 0, handledOut);
    }
    if (cursor->button < 1 || cursor->button > 8)
    {
        return mui_success;
    }
    glue->buttons = cursor->buttons;
    muiPointerAction action =
        event->type == mwin_eventButtonDown ? mui_pointerPress : mui_pointerRelease;
    return Mouse(glue, event->timeNs, action, (uint8_t)(cursor->button - 1), handledOut);
}

// Forgets what is held: the mouse's and the pen's buttons and every
// touch, cancelled, and the modifiers.
static muiResult Reset(muiWindowGlue* glue, uint64_t timeNs)
{
    glue->modifiers = 0;
    bool handled = false;
    muiResult status = mui_success;
    if (glue->buttons != 0)
    {
        glue->buttons = 0;
        status = Mouse(glue, timeNs, mui_pointerCancel, 0, &handled);
    }
    if (status == mui_success && glue->penButtons != 0)
    {
        glue->penButtons = 0;
        status = PenAt(glue, timeNs, mui_pointerCancel, 0, &handled);
    }
    for (uint32_t i = 0; i < TOUCHES && status == mui_success; i++)
    {
        if (glue->touching[i])
        {
            status = TouchAt(glue, i, timeNs, mui_pointerCancel, &handled);
        }
    }
    return status;
}

static muiResult Key(muiWindowGlue* glue, const mwinEvent* event, bool* handledOut)
{
    const mwinKeyEvent* key = &event->data.key;
    glue->modifiers = key->modifiers;
    const muiKeyEvent input = {
        .timeNs = event->timeNs,
        .key = key->key,
        .code = key->code,
        .modifiers = key->modifiers,
        .down = event->type == mwin_eventKeyDown,
        .repeat = key->repeat,
        .player = glue->player,
    };
    return muiKeyInput(glue->context, glue->root, &input, handledOut);
}

static muiResult Text(muiWindowGlue* glue, const mwinEvent* event, bool* handledOut)
{
    const muiTextEvent input = {
        .timeNs = event->timeNs,
        .text = event->data.text.text,
        .length = event->data.text.length,
        .player = glue->player,
    };
    return muiTextInput(glue->context, glue->root, &input, handledOut);
}

static muiResult Wheel(muiWindowGlue* glue, const mwinEvent* event, bool* handledOut)
{
    const muiWheelEvent input = {
        .timeNs = event->timeNs,
        .x = glue->x,
        .y = glue->y,
        .deltaX = event->data.wheel.x,
        .deltaY = event->data.wheel.y,
        .modifiers = glue->modifiers,
        .player = glue->player,
    };
    return muiWheelInput(glue->context, glue->root, &input, handledOut);
}

static muiResult Navigate(const muiWindowGlue* glue, muiWindowAsk ask, bool* handledOut)
{
    return ask.asks ? muiNavigationInput(glue->context, glue->root, &ask.navigation, handledOut)
                    : mui_success;
}

// A record of the context's own: a gamepad's, for a glue that takes
// them.
static muiResult Gamepad(muiWindowGlue* glue, const mwinEvent* event, bool* handledOut)
{
    return glue->gamepads ? Navigate(glue, muiWindowPadRecord(&glue->pads, event), handledOut)
                          : mui_success;
}

muiResult muiWindowGlue_Tick(muiWindowGlue* glue, uint64_t nowNs, bool* handledOut)
{
    if (handledOut != nullptr)
    {
        *handledOut = false;
    }
    if (glue == nullptr || handledOut == nullptr)
    {
        return mui_errorInvalid;
    }
    return glue->gamepads ? Navigate(glue, muiWindowPadTick(&glue->pads, nowNs), handledOut)
                          : mui_success;
}

muiResult muiWindowGlue_SetCaret(muiWindowGlue* glue, muiNodeId nodeId, muiRect caret,
                                 mwinRect* placedOut)
{
    if (glue == nullptr)
    {
        return mui_errorInvalid;
    }
    bool enabled = nodeId.index1 != 0;
    mwinRect rect = {0.0f, 0.0f, 0.0f, 0.0f};
    if (enabled)
    {
        // Maul Window refuses a size not finite or negative.
        muiResult status =
            muiNode_MapToRoot(glue->context, nodeId, caret.x, caret.y, &rect.x, &rect.y);
        if (status != mui_success)
        {
            return status;
        }
        rect.width = caret.width;
        rect.height = caret.height;
    }
    if (mwinRequestTextInput(glue->windows, glue->window, enabled, rect, nullptr) != mwin_success)
    {
        return mui_errorInvalid;
    }
    if (placedOut != nullptr)
    {
        *placedOut = rect;
    }
    return mui_success;
}

muiResult muiWindowGlue_HandleEvent(muiWindowGlue* glue, const mwinEvent* event, bool* handledOut)
{
    if (handledOut != nullptr)
    {
        *handledOut = false;
    }
    if (glue == nullptr || event == nullptr || handledOut == nullptr)
    {
        return mui_errorInvalid;
    }
    if (event->window.index1 == 0)
    {
        return Gamepad(glue, event, handledOut);
    }
    if (!IsMine(glue, event->window))
    {
        return mui_success;
    }
    switch (event->type)
    {
    case mwin_eventKeyDown:
    case mwin_eventKeyUp:
        return Key(glue, event, handledOut);
    case mwin_eventTextInput:
        return Text(glue, event, handledOut);
    case mwin_eventCursorMoved:
    case mwin_eventCursorEntered:
    case mwin_eventCursorLeft:
    case mwin_eventButtonDown:
    case mwin_eventButtonUp:
        return Cursor(glue, event, handledOut);
    case mwin_eventWheel:
        return Wheel(glue, event, handledOut);
    case mwin_eventTouchDown:
    case mwin_eventTouchMoved:
    case mwin_eventTouchUp:
    case mwin_eventTouchCancelled:
        return Touch(glue, event, handledOut);
    case mwin_eventPenMoved:
    case mwin_eventPenDown:
    case mwin_eventPenUp:
    case mwin_eventPenButtonDown:
    case mwin_eventPenButtonUp:
        return Pen(glue, event, handledOut);
    case mwin_eventInputStateReset:
    case mwin_eventFocusLost:
        return Reset(glue, event->timeNs);
    default:
        return mui_success;
    }
}
