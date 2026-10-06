// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Maul Window glue (record mui-0007). Maul Window's button b is bit
// b - 1 of its held mask and Maul UI's index b - 1 is bit b - 1 of its
// own, so the masks pass as they are and an index is the button less
// one.

#include "maul-ui-window/glue.h"

#include "allocator.h"

#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"

#include <stdalign.h>

#define DEF_COOKIE 0x6D757767u // "muwg"

// The pointer id the window's mouse has.
#define MOUSE 0u

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
};

muiWindowGlueDef muiDefaultWindowGlueDef(void)
{
    return (muiWindowGlueDef){.cookie = DEF_COOKIE};
}

static bool IsValid(const muiWindowGlueDef* def)
{
    return def->cookie == DEF_COOKIE && muiWindowIsAllocatorValid(&def->allocator) &&
           def->windows != nullptr && def->context != nullptr && def->root.index1 != 0 &&
           def->player < MUI_MAX_PLAYERS;
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

// Dispatches every pointer record waiting: whether one was handled.
static muiResult DispatchRecords(muiContext* context, bool* handledOut)
{
    muiPointerRecord record;
    while (muiNextPointerRecord(context, &record) == mui_success)
    {
        bool handled = false;
        muiResult status = muiDispatchPointerRecord(context, &record, &handled);
        if (status != mui_success)
        {
            return status;
        }
        *handledOut = *handledOut || handled;
    }
    return mui_success;
}

// Whether the UI keeps the mouse at its place: it holds it, or the point
// hits a node that does not pass input through.
static bool KeepsMouse(const muiWindowGlue* glue)
{
    muiPointerState state;
    if (muiPointer_GetState(glue->context, MOUSE, &state) == mui_success &&
        (state.pressed.index1 != 0 || state.captured.index1 != 0))
    {
        return true;
    }
    muiHit hit;
    return muiHitTest(glue->context, glue->root, glue->x, glue->y, &hit) == mui_success &&
           hit.node.index1 != 0 && !hit.passThrough;
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
    muiResult status = muiPointerInput(glue->context, glue->root, &pointer);
    status = status == mui_success ? DispatchRecords(glue->context, handledOut) : status;
    if (status == mui_success && !*handledOut && action != mui_pointerLeave &&
        action != mui_pointerCancel)
    {
        *handledOut = KeepsMouse(glue);
    }
    return status;
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

// Forgets what is held: the mouse's buttons, cancelled, and the
// modifiers.
static muiResult Reset(muiWindowGlue* glue, uint64_t timeNs)
{
    glue->modifiers = 0;
    if (glue->buttons == 0)
    {
        return mui_success;
    }
    glue->buttons = 0;
    bool handled = false;
    return Mouse(glue, timeNs, mui_pointerCancel, 0, &handled);
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
    case mwin_eventInputStateReset:
    case mwin_eventFocusLost:
        return Reset(glue, event->timeNs);
    default:
        return mui_success;
    }
}
