// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A frame's passes (record mui-0005): the draw pass's instances split
// into draws, a new draw only where an image's texture or a glyph's page
// differs from the one the draw samples; the resources the draw pass
// reads, and those the upload pass writes.

#ifndef MAUL_UI_RHI_PLAN_H
#define MAUL_UI_RHI_PLAN_H

#include "glyphs.h"
#include "images.h"
#include "pack.h"

#include "maul-rhi/frame.h"

#include <stdint.h>

// Instances drawn with one texture bound: an image's or a glyph page's,
// or the placeholder for a list that samples nothing.
typedef struct muiRhiDraw
{
    uint32_t first;
    uint32_t count;
    mrhiResourceId texture;
} muiRhiDraw;

// What a frame draws from: its buffers, images and glyph pages in the
// open frame, and the placeholder texture.
typedef struct muiRhiSources
{
    const mrhiResourceId* buffers;
    uint32_t bufferCount;
    const muiRhiImages* images;
    const muiRhiGlyphs* glyphs;
    mrhiResourceId placeholder;
} muiRhiSources;

typedef struct muiRhiPlan
{
    muiAllocator allocator;
    muiRhiDraw* draws;
    uint32_t drawCount;
    uint32_t drawCapacity;
    mrhiAccess* accesses;
    uint32_t accessCount;
    uint32_t accessCapacity;
    mrhiAccess* uploads;
    uint32_t uploadCount;
    uint32_t uploadCapacity;
} muiRhiPlan;

void muiRhiFreePlan(muiRhiPlan* plan);

// Plans a frame's passes: the instances' draws; the draw pass's
// accesses, the buffers' storage reads and each distinct texture and
// page sampled (the placeholder where a draw samples nothing); and the
// upload pass's, the buffers and the changed pages as copy destinations.
muiResult muiRhiMakePlan(muiRhiPlan* plan, const muiRhiInstance* instances, uint32_t count,
                         const muiRhiSources* sources);

#endif // MAUL_UI_RHI_PLAN_H
