// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The glue's state (record mui-0007), which its other parts read: the
// window, the context and the root it feeds.

#ifndef MAUL_UI_WINDOW_GLUE_STATE_H
#define MAUL_UI_WINDOW_GLUE_STATE_H

#include "gamepads.h"

#include "maul-ui-window/glue.h"

#include <stdbool.h>
#include <stdint.h>

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
    uint64_t touchIds[MUI_WINDOW_TOUCHES];
    bool touching[MUI_WINDOW_TOUCHES];
    float touchX[MUI_WINDOW_TOUCHES];
    float touchY[MUI_WINDOW_TOUCHES];
    // The pointers whose press was the UI's, by id, while they hold it.
    uint32_t held;
    // Whether it takes gamepads, and what they hold.
    bool gamepads;
    muiWindowPads pads;
};

#endif // MAUL_UI_WINDOW_GLUE_STATE_H
