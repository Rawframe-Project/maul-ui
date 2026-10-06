// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scroll state (record mui-0007): per node, its offset and the extent its
// children reach, logical (x from the inline start), which layout
// measures and src/scroll.c moves.

#ifndef MAUL_UI_SRC_SCROLL_STORE_H
#define MAUL_UI_SRC_SCROLL_STORE_H

#include "layout_node.h"

typedef struct muiScrollState
{
    float x;
    float y;
    // From the padding box's start, at least the padding box.
    float extentWidth;
    float extentHeight;
} muiScrollState;

// The furthest an offset goes along an axis: the extent less the padding
// box, for an axis the node scrolls, else 0.
static inline float muiScrollLimit(const muiLayoutNode* node, const muiScrollState* scroll,
                                   bool horizontal)
{
    const muiLayoutStyle* style = &node->style;
    muiScrollAxes axis = horizontal ? mui_scrollHorizontal : mui_scrollVertical;
    if ((style->scrollAxes & axis) == 0)
    {
        return 0.0f;
    }
    float box = horizontal ? node->rect.width - style->border.start - style->border.end
                           : node->rect.height - style->border.top - style->border.bottom;
    float extent = horizontal ? scroll->extentWidth : scroll->extentHeight;
    return extent > box ? extent - box : 0.0f;
}

// How far a node moves its children on the surface: by its offsets, the
// logical x leftward under right to left; nothing for a node that does
// not scroll.
static inline float muiScrollShiftX(const muiLayoutNode* node, const muiScrollState* scroll)
{
    if (node->style.scrollAxes == mui_scrollNone)
    {
        return 0.0f;
    }
    return node->rtl ? scroll->x : -scroll->x;
}

static inline float muiScrollShiftY(const muiLayoutNode* node, const muiScrollState* scroll)
{
    return node->style.scrollAxes == mui_scrollNone ? 0.0f : -scroll->y;
}

#endif // MAUL_UI_SRC_SCROLL_STORE_H
