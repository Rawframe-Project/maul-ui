// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling (record mui-0007): what focus asks of it.

#ifndef MAUL_UI_SRC_SCROLL_H
#define MAUL_UI_SRC_SCROLL_H

#include "maul-ui/event.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiContext muiContext;

// The default of an unhandled wheel turn over hit (muiWheelInput), under
// root: whether a scroll container took it.
bool muiScrollWheel(muiContext* context, uint32_t root, muiNodeId hit, const muiWheelEvent* event);

// Scrolls a node's scrolling ancestors to bring it into view, as
// muiNode_ScrollIntoView does.
void muiScrollReveal(muiContext* context, uint32_t slot);

#endif // MAUL_UI_SRC_SCROLL_H
