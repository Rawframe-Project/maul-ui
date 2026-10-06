// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads as navigation (record mui-0007). A direction is the last
// pressed of those held: a d-pad button, or the stick crossing its
// threshold; when it lets go, another still held takes over without
// navigating again.

#include "gamepads.h"

#include "maul-ui/focus.h"

#include <math.h>

#define NONE (-1)

static bool SameId(mwinGamepadId a, mwinGamepadId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// A gamepad's slot, taken for it when it has none and one is free; NULL
// when none is.
static muiWindowPad* Find(muiWindowPads* pads, mwinGamepadId id)
{
    muiWindowPad* open = nullptr;
    for (uint32_t i = 0; i < MUI_WINDOW_GAMEPADS; i++)
    {
        muiWindowPad* pad = &pads->pads[i];
        if (pad->used && SameId(pad->id, id))
        {
            return pad;
        }
        open = open == nullptr && !pad->used ? pad : open;
    }
    if (open != nullptr)
    {
        *open = (muiWindowPad){.id = id, .used = true, .stick = NONE, .held = NONE};
    }
    return open;
}

static muiWindowAsk Ask(const muiWindowPads* pads, const muiWindowPad* pad, uint64_t timeNs,
                        muiNavigation action)
{
    uint8_t player = pads->player != nullptr ? pads->player(pads->playerContext, pad->id) : 0;
    return (muiWindowAsk){
        .asks = player < MUI_MAX_PLAYERS,
        .navigation = {.timeNs = timeNs, .action = action, .player = player},
    };
}

// A direction newly held: it navigates now and repeats from the delay.
static muiWindowAsk Hold(const muiWindowPads* pads, muiWindowPad* pad, uint64_t timeNs,
                         int8_t direction)
{
    pad->held = direction;
    pad->nextNs = timeNs + pads->delayNs;
    return Ask(pads, pad, timeNs, (muiNavigation)direction);
}

// After a direction lets go: another still held takes over, quietly.
static void Release(muiWindowPad* pad, int8_t direction)
{
    if (pad->held != direction)
    {
        return;
    }
    pad->held = pad->stick;
    for (int8_t d = 0; d < 4 && pad->held == NONE; d++)
    {
        pad->held = (pad->dpad & (1u << d)) != 0 ? d : NONE;
    }
}

static muiWindowAsk Button(muiWindowPads* pads, muiWindowPad* pad, const mwinEvent* event)
{
    const muiWindowAsk none = {0};
    bool down = event->type == mwin_eventGamepadButtonDown;
    uint8_t button = event->data.gamepadButton.button;
    if (button <= mwin_padDpadRight)
    {
        // The d-pad's buttons are the directions, in muiNavigation's
        // order.
        int8_t direction = (int8_t)button;
        if (!down)
        {
            pad->dpad &= (uint8_t)~(1u << direction);
            Release(pad, direction);
            return none;
        }
        pad->dpad |= (uint8_t)(1u << direction);
        return Hold(pads, pad, event->timeNs, direction);
    }
    if (!down)
    {
        return none;
    }
    bool south = button == mwin_padFaceSouth;
    switch (button)
    {
    case mwin_padFaceSouth:
    case mwin_padFaceEast:
        return Ask(pads, pad, event->timeNs,
                   south != pads->confirmEast ? mui_navigateActivate : mui_navigateCancel);
    case mwin_padShoulderLeft:
        return Ask(pads, pad, event->timeNs, mui_navigatePrevious);
    case mwin_padShoulderRight:
        return Ask(pads, pad, event->timeNs, mui_navigateNext);
    default:
        return none;
    }
}

// The direction a stick's place holds: its larger axis past the
// threshold, or kept below it while past seven tenths of it.
static int8_t StickDirection(const muiWindowPads* pads, const muiWindowPad* pad)
{
    float x = pad->stickX;
    float y = pad->stickY;
    bool across = fabsf(x) >= fabsf(y);
    float reach = across ? fabsf(x) : fabsf(y);
    float needed = pad->stick == NONE ? pads->threshold : pads->threshold * 0.7f;
    if (!(reach >= needed))
    {
        return NONE;
    }
    if (across)
    {
        return x > 0.0f ? (int8_t)mui_navigateRight : (int8_t)mui_navigateLeft;
    }
    return y > 0.0f ? (int8_t)mui_navigateDown : (int8_t)mui_navigateUp;
}

static muiWindowAsk Axis(muiWindowPads* pads, muiWindowPad* pad, const mwinEvent* event)
{
    const muiWindowAsk none = {0};
    const mwinGamepadAxisEvent* axis = &event->data.gamepadAxis;
    if (axis->axis == mwin_padStickLeftX)
    {
        pad->stickX = axis->value;
    }
    else if (axis->axis == mwin_padStickLeftY)
    {
        pad->stickY = axis->value;
    }
    else
    {
        return none;
    }
    int8_t direction = StickDirection(pads, pad);
    if (direction == pad->stick)
    {
        return none;
    }
    int8_t was = pad->stick;
    pad->stick = direction;
    if (direction == NONE)
    {
        Release(pad, was);
        return none;
    }
    return Hold(pads, pad, event->timeNs, direction);
}

muiWindowAsk muiWindowPadRecord(muiWindowPads* pads, const mwinEvent* event)
{
    const muiWindowAsk none = {0};
    switch (event->type)
    {
    case mwin_eventGamepadButtonDown:
    case mwin_eventGamepadButtonUp:
    case mwin_eventGamepadAxisMoved:
        break;
    case mwin_eventGamepadRemoved:
    case mwin_eventInputStateReset:
        for (uint32_t i = 0; i < MUI_WINDOW_GAMEPADS; i++)
        {
            muiWindowPad* pad = &pads->pads[i];
            if (pad->used && SameId(pad->id, event->data.gamepad))
            {
                *pad = (muiWindowPad){0};
            }
        }
        return none;
    default:
        return none;
    }
    bool raw = event->type == mwin_eventGamepadAxisMoved ? event->data.gamepadAxis.raw
                                                         : event->data.gamepadButton.raw;
    mwinGamepadId id = event->type == mwin_eventGamepadAxisMoved
                           ? event->data.gamepadAxis.gamepad
                           : event->data.gamepadButton.gamepad;
    muiWindowPad* pad = raw ? nullptr : Find(pads, id);
    if (pad == nullptr)
    {
        return none;
    }
    return event->type == mwin_eventGamepadAxisMoved ? Axis(pads, pad, event)
                                                     : Button(pads, pad, event);
}

muiWindowAsk muiWindowPadTick(muiWindowPads* pads, uint64_t nowNs)
{
    for (uint32_t i = 0; i < MUI_WINDOW_GAMEPADS; i++)
    {
        muiWindowPad* pad = &pads->pads[i];
        if (pad->used && pad->held != NONE && nowNs >= pad->nextNs)
        {
            pad->nextNs = nowNs + pads->intervalNs;
            return Ask(pads, pad, nowNs, (muiNavigation)pad->held);
        }
    }
    return (muiWindowAsk){0};
}
