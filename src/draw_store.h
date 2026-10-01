// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's draw list: its tables, reserved by the context's limits,
// and the state painting keeps per node (record mui-0005).

#ifndef MAUL_UI_SRC_DRAW_STORE_H
#define MAUL_UI_SRC_DRAW_STORE_H

#include "maul-ui/draw.h"

#include <stdint.h>

// What a node passes to its children while it is painted: its border
// box's origin on the surface, the clip they are drawn in, and the
// opacity they are multiplied by.
typedef struct muiPaintState
{
    float x;
    float y;
    uint32_t clip;
    float opacity;
} muiPaintState;

typedef struct muiDrawStore
{
    muiDrawHeader header;
    muiDrawCommand* commands;
    uint32_t commandCount;
    uint32_t commandCapacity;
    // Entry 0 of the clips and gradients is the placeholder for none, so
    // their capacities count it.
    muiDrawClip* clips;
    uint32_t clipCount;
    uint32_t clipCapacity;
    muiDrawGradient* gradients;
    uint32_t gradientCount;
    uint32_t gradientCapacity;
    muiDrawTransform identity;
    // Per node, parallel to the tree's slots.
    muiPaintState* states;
} muiDrawStore;

#endif // MAUL_UI_SRC_DRAW_STORE_H
