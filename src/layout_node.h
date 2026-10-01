// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What layout keeps per node, in an array parallel to the node store's
// slots: the authored style, the published rectangle, the solver's
// cache, and the node's working values as a flex item of its parent.

#ifndef MAUL_UI_SRC_LAYOUT_NODE_H
#define MAUL_UI_SRC_LAYOUT_NODE_H

#include "maul-ui/layout.h"

#include <stdbool.h>

// What a node is sized under: a constraint per axis on its border box,
// and its parent's content extents, negative when indefinite, against
// which its Scale+Offset values resolve.
typedef struct muiSizingInput
{
    muiMeasureAxis width;
    muiMeasureAxis height;
    float parentWidth;
    float parentHeight;
} muiSizingInput;

typedef struct muiCacheEntry
{
    muiSizingInput input;
    muiSize size;
    bool valid;
} muiCacheEntry;

enum
{
    MUI_CACHE_ENTRIES = 4
};

// Sizing results by input, valid until the node or a descendant changes,
// and the size the node was last laid out at in full.
typedef struct muiLayoutCache
{
    muiCacheEntry entries[MUI_CACHE_ENTRIES];
    // The entry the next miss replaces.
    uint8_t next;
    bool finalValid;
    muiSize finalSize;
} muiLayoutCache;

// A node's values while its parent runs the flex algorithm over it, all
// along the parent's main axis unless named cross. Sizes are border box.
typedef struct muiFlexItemState
{
    float marginMain;
    float marginCross;
    float minMain;
    float maxMain;
    float minCross;
    float maxCross;
    float base;
    // The base without padding and border, which scales shrinking.
    float innerBase;
    float hypothetical;
    float target;
    float cross;
    // The last clamp's effect: 1 raised to the minimum, -1 lowered to the
    // maximum, 0 neither.
    int8_t violation;
    bool frozen;
} muiFlexItemState;

typedef struct muiLayoutNode
{
    muiLayoutStyle style;
    muiRect rect;
    muiLayoutCache cache;
    muiFlexItemState item;
} muiLayoutNode;

#endif // MAUL_UI_SRC_LAYOUT_NODE_H
