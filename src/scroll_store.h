// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scroll state (record mui-0007): per node, its offset and the extent its
// children reach, logical (x from the inline start), which layout
// measures and src/scroll.c moves.

#ifndef MAUL_UI_SRC_SCROLL_STORE_H
#define MAUL_UI_SRC_SCROLL_STORE_H

#include "layout_node.h"

#include "maul-ui/scroll.h"

typedef struct muiScrollState
{
    float x;
    float y;
    // From the padding box's start, at least the padding box.
    float extentWidth;
    float extentHeight;
} muiScrollState;

// The default rule (muiDefaultScrollRule).
#define MUI_SCROLL_RULE                                                                            \
    ((muiScrollRule){.wheelStep = 100.0f,                                                          \
                     .lineStep = 40.0f,                                                            \
                     .pageFraction = 0.875f,                                                       \
                     .latchNs = 500000000ull,                                                      \
                     .easeNs = 150000000ull})

// The most scroll containers easing a step at once; a step past them
// jumps.
#define MUI_SCROLL_EASES 8

// A step easing out: offsets from where they were when it began to where
// it goes.
typedef struct muiScrollEase
{
    muiNodeId node;
    float fromX;
    float fromY;
    float toX;
    float toY;
    uint64_t startNs;
} muiScrollEase;

// The rule, the scroll container the wheel last scrolled with when, and
// the steps easing.
typedef struct muiScrollStore
{
    muiScrollRule rule;
    muiNodeId latched;
    uint64_t latchedNs;
    uint32_t easeCount;
    muiScrollEase eases[MUI_SCROLL_EASES];
} muiScrollStore;

static inline void muiScrollInit(muiScrollStore* store)
{
    *store = (muiScrollStore){.rule = MUI_SCROLL_RULE};
}

// The furthest an offset goes along an axis: the extent less the padding
// box, of a node of a style and border box size, for an axis it scrolls,
// else 0.
static inline float muiScrollLimit(const muiLayoutStyle* style, muiSize size,
                                   const muiScrollState* scroll, bool horizontal)
{
    muiScrollAxes axis = horizontal ? mui_scrollHorizontal : mui_scrollVertical;
    if ((style->scrollAxes & axis) == 0)
    {
        return 0.0f;
    }
    float box = horizontal ? size.width - style->border.start - style->border.end
                           : size.height - style->border.top - style->border.bottom;
    float extent = horizontal ? scroll->extentWidth : scroll->extentHeight;
    return extent > box ? extent - box : 0.0f;
}

// Drops a node's offset and extent when it stops scrolling: they live
// with the scroll container, as a browser's do. (Along an axis it still
// scrolls but no longer along, layout's limit of 0 drops the offset.) The
// layout style change repaints the node, which sets the transforms anew.
static inline void muiSyncScroll(muiScrollState* scroll, muiScrollAxes axes)
{
    if (axes == mui_scrollNone)
    {
        *scroll = (muiScrollState){0};
    }
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
