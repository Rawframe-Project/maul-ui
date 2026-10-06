// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A frame's draw pass (record mui-0005): its instances split into draws,
// a new draw only where an image's texture differs from the one the draw
// samples, and the resources the pass reads.

#ifndef MAUL_UI_RHI_PLAN_H
#define MAUL_UI_RHI_PLAN_H

#include "images.h"
#include "pack.h"

#include "maul-rhi/frame.h"

#include <stdint.h>

// Instances drawn with one texture bound: an image's, or none, for a
// list without images, when the placeholder is bound.
typedef struct muiRhiDraw
{
    uint32_t first;
    uint32_t count;
    mrhiResourceId texture;
} muiRhiDraw;

typedef struct muiRhiPlan
{
    muiAllocator allocator;
    muiRhiDraw* draws;
    uint32_t drawCount;
    uint32_t drawCapacity;
    mrhiAccess* accesses;
    uint32_t accessCount;
    uint32_t accessCapacity;
} muiRhiPlan;

void muiRhiFreePlan(muiRhiPlan* plan);

// Plans a frame's draw pass: the instances' draws, and the pass's
// accesses, the buffers' storage reads and each distinct texture sampled
// (the placeholder where a draw has no image).
muiResult muiRhiMakePlan(muiRhiPlan* plan, const muiRhiInstance* instances, uint32_t count,
                         const muiRhiImages* images, const mrhiResourceId* buffers,
                         uint32_t bufferCount, mrhiResourceId placeholder);

#endif // MAUL_UI_RHI_PLAN_H
