// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads as navigation (record mui-0007): a mapped gamepad's d-pad and
// left stick give the four directions, its faces activate and cancel,
// its shoulders previous and next. A direction held repeats after a
// delay and then at an interval, timed by the records' clock; the
// stick holds a direction past a threshold and lets it go below seven
// tenths of it, so it does not flicker at the edge.

#ifndef MAUL_UI_WINDOW_GAMEPADS_H
#define MAUL_UI_WINDOW_GAMEPADS_H

#include "maul-ui-window/glue.h"
#include "maul-ui/event.h"

#include <stdbool.h>
#include <stdint.h>

// The gamepads followed at once, as Maul Window's default limit.
#define MUI_WINDOW_GAMEPADS 8u

// What a gamepad holds.
typedef struct muiWindowPad
{
    mwinGamepadId id;
    bool used;
    // The d-pad's directions held, a bit a muiNavigation direction.
    uint8_t dpad;
    // The stick's place, and the direction it holds, or none.
    float stickX;
    float stickY;
    int8_t stick;
    // The direction repeating, or none, and when it next repeats.
    int8_t held;
    uint64_t nextNs;
} muiWindowPad;

// What navigation a glue's gamepads give, and how.
typedef struct muiWindowPads
{
    muiWindowPad pads[MUI_WINDOW_GAMEPADS];
    muiWindowPlayerFunction player;
    void* playerContext;
    bool confirmEast;
    float threshold;
    uint64_t delayNs;
    uint64_t intervalNs;
} muiWindowPads;

// What a gamepad record asks: a navigation for a player, or nothing.
typedef struct muiWindowAsk
{
    bool asks;
    muiNavigationEvent navigation;
} muiWindowAsk;

// Takes a gamepad record: what it asks.
muiWindowAsk muiWindowPadRecord(muiWindowPads* pads, const mwinEvent* event);

// What repeats are due at a time, one a call: for the first gamepad due.
muiWindowAsk muiWindowPadTick(muiWindowPads* pads, uint64_t nowNs);

#endif // MAUL_UI_WINDOW_GAMEPADS_H
