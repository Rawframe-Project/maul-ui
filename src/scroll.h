// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling (record mui-0007): what focus asks of it.

#ifndef MAUL_UI_SRC_SCROLL_H
#define MAUL_UI_SRC_SCROLL_H

#include <stdint.h>

typedef struct muiContext muiContext;

// Scrolls a node's scrolling ancestors to bring it into view, as
// muiNode_ScrollIntoView does.
void muiScrollReveal(muiContext* context, uint32_t slot);

#endif // MAUL_UI_SRC_SCROLL_H
