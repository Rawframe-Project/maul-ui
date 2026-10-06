// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtualization (record mui-0007), within the library.

#ifndef MAUL_UI_SRC_VIRTUAL_H
#define MAUL_UI_SRC_VIRTUAL_H

#include "context.h"

#include <stdint.h>

// After layout, before scrolling moves: measures the bound items of the
// lists under root into their extents, places them at their offsets, and
// sizes each list's content to all its items.
void muiVirtualPlace(muiContext* context, uint32_t root);

// After scrolling moves: finds each list's window under root, reporting
// those that changed.
void muiVirtualWindows(muiContext* context, uint32_t root);

#endif // MAUL_UI_SRC_VIRTUAL_H
