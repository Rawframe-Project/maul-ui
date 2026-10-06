// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Focus (record mui-0007): what pointer input and edits tell it.

#ifndef MAUL_UI_SRC_FOCUS_H
#define MAUL_UI_SRC_FOCUS_H

#include "context.h"

#include <stdint.h>

// Focuses, for a player, the nearest node from slot up that takes focus,
// as a pointer press does; over nothing that does (slot 0 included), the
// player's focus goes.
void muiFocusPress(muiContext* context, uint8_t player, uint32_t slot);

// Lets the focus of nodes that no longer take it go: after a node's
// style or interaction values changed. Only a focused node has any.
void muiFocusRecheck(muiContext* context, uint32_t slot);

static inline void muiNoteFocus(muiContext* context, uint32_t slot)
{
    if (context->style.nodes[slot - 1].focusedBy != 0)
    {
        muiFocusRecheck(context, slot);
    }
}

void muiFocusForgetDestroyed(muiContext* context);

// Lets the focus of destroyed nodes go, after a destruction; only a
// player that focuses a node has any.
static inline void muiNoteDestroyed(muiContext* context)
{
    if (context->focus.holders != 0)
    {
        muiFocusForgetDestroyed(context);
    }
}

#endif // MAUL_UI_SRC_FOCUS_H
