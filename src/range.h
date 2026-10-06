// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Range values (record mui-0007): the defaults input reaches them by.

#ifndef MAUL_UI_SRC_RANGE_H
#define MAUL_UI_SRC_RANGE_H

#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/pointer.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiContext muiContext;

// An unmodified key's default on the range at slot, holding the focus:
// arrows along its axis a step, Page Up and Down a page, Home and End
// the ends. Whether the range takes the key.
bool muiRangeKey(muiContext* context, uint32_t slot, muiKeyCode code);

// A navigation direction's default on the range at slot, as its arrow's.
bool muiRangeDirection(muiContext* context, uint32_t slot, muiDirection direction);

// A pointer record's default: a press on a range or inside it, its
// thumb's grab or a page toward the point; a drag of a range, its value
// from the pointer, or back where it began when cancelled. Whether a
// range took it.
bool muiRangePointer(muiContext* context, const muiPointerRecord* record);

#endif // MAUL_UI_SRC_RANGE_H
