// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The parts of the flex algorithm that only arrange numbers already
// known: resolving flexible lengths (CSS Flexbox section 9.7) and
// spacing a line along its main axis.

#ifndef MAUL_UI_SRC_FLEX_RESOLVE_H
#define MAUL_UI_SRC_FLEX_RESOLVE_H

#include "layout_node.h"
#include "tree.h"

// Sets each child's target main size from its base, hypothetical size,
// limits and flex factors, so the line fills innerMain where it can.
// gaps is the space between the children.
void muiResolveFlexibleLengths(const muiTree* tree, muiLayoutNode* nodes, uint32_t container,
                               float innerMain, float gaps);

// The space before the first of count items and the extra space between
// two of them, for free space left on the line (negative when the items
// overflow).
void muiJustifySpacing(muiJustify justify, float freeSpace, uint32_t count, float* leadOut,
                       float* betweenOut);

#endif // MAUL_UI_SRC_FLEX_RESOLVE_H
