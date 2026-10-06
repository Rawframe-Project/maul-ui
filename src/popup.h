// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Popups (record mui-0007), within the library.

#ifndef MAUL_UI_SRC_POPUP_H
#define MAUL_UI_SRC_POPUP_H

#include "context.h"

#include <stdint.h>

// Places the popups under the root at slot root beside their anchors,
// after layout and scrolling; a popup anchored inside another after it.
void muiPlacePopups(muiContext* context, uint32_t root);

#endif // MAUL_UI_SRC_POPUP_H
