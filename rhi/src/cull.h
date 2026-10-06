// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Culling as a list is packed (record mui-0005): each clip's bounds on
// the screen, in logical units, worked out once a frame, and an instance
// whose quad through its transform misses its clip's bounds is not
// drawn. Bounds only ever hold more than a clip keeps: an inverted clip
// bounds nothing, and a clip whose parent comes after it in the table
// is bounded by the target alone.

#ifndef MAUL_UI_RHI_CULL_H
#define MAUL_UI_RHI_CULL_H

#include "maul-ui/base.h"
#include "maul-ui/draw.h"

#include <stdint.h>

// An axis-aligned box from (x0, y0) to (x1, y1).
typedef struct muiRhiBounds
{
    float x0;
    float y0;
    float x1;
    float y1;
} muiRhiBounds;

typedef struct muiRhiCull
{
    muiAllocator allocator;
    // Each clip's bounds, entry 0 (no clip) the target's.
    muiRhiBounds* bounds;
    uint32_t count;
    uint32_t capacity;
    // How far past its quad an instance's edge may cover, in units.
    float margin;
} muiRhiCull;

void muiRhiFreeCull(muiRhiCull* cull);

// Works out a list's clips' bounds for a target of a size in device
// pixels.
muiResult muiRhiPrepareCull(muiRhiCull* cull, const muiDrawList* list, uint32_t width,
                            uint32_t height);

// Whether a quad, in the units of a transform of the list, misses the
// bounds of a clip; both indices are the list's own, checked.
bool muiRhiIsCulled(const muiRhiCull* cull, const muiDrawList* list, muiRect quad, uint32_t clip,
                    uint32_t transform);

#endif // MAUL_UI_RHI_CULL_H
