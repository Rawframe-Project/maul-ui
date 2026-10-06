// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's records (record mui-0005): a draw list's
// commands and its gradient, transform and clip tables in the layouts
// rhi/shaders/quad.* read. Every table has its entry 0, the identity
// transform and no clip where the list has none, and every index into a
// table is one of its entries, 0 where the list's is not, so the shaders
// never read past a table whatever list they are given.

#ifndef MAUL_UI_RHI_PACK_H
#define MAUL_UI_RHI_PACK_H

#include "cull.h"
#include "glyphs.h"
#include "images.h"

#include "maul-ui/draw.h"

#include <stdint.h>

// An instance (std430): a box; a shadow, whose fields then hold its quad
// (rect), its shape's radii, its color (fill), its shape (widths, as x,
// y, width and height), its box and the box's radii (colors 0 and 1), and
// its blur's sigma and whether it is inset (colors 2); or an image, whose
// fields hold its rect, its tint (fill), its uv rect as two corners
// (colors 0), and its slice insets, top, right, bottom and left, in
// logical units as drawn (colors 1) and in uv (colors 2); or a glyph,
// whose fields hold its quad, its color (fill), its uv rect in its atlas
// page (colors 0) and the uv its samples stay within, half a texel into
// its gutter (colors 1). index is a box's gradient, an image's entry of
// the frame's images, or a glyph's page.
typedef struct muiRhiInstance
{
    muiRect rect;
    muiCorners radii;
    muiLinearColor fill;
    muiSides widths;
    muiLinearColor colors[4];
    uint32_t kind;
    uint32_t clip;
    uint32_t transform;
    uint32_t index;
} muiRhiInstance;

// A gradient (std430).
typedef struct muiRhiGradient
{
    uint32_t kind;
    uint32_t stopCount;
    uint32_t interpolation;
    uint32_t reserved;
    float angle;
    float unused[3];
    muiLinearColor colors[4];
    float positions[4];
} muiRhiGradient;

// A transform (std430): a, b, c and d, then e and f.
typedef struct muiRhiTransform
{
    float linear[4];
    float offset[4];
} muiRhiTransform;

// A clip (std430).
typedef struct muiRhiClip
{
    muiRect rect;
    muiCorners radii;
    uint32_t parent;
    uint32_t transform;
    uint32_t invert;
    uint32_t reserved;
} muiRhiClip;

// The instances a list's commands make at most.
uint32_t muiRhiCountInstances(const muiDrawList* list);

// The image commands of a list.
uint32_t muiRhiCountImages(const muiDrawList* list);

// What a frame's commands are packed with: its images, glyphs and
// clips' bounds.
typedef struct muiRhiPacking
{
    muiRhiImages* images;
    muiRhiGlyphs* glyphs;
    const muiRhiCull* cull;
} muiRhiPacking;

// A list's commands as instances, at most as many as muiRhiCountInstances
// says: an image's found in the frame's images and a glyph's in the
// atlas, or not drawn, and an instance that misses its clip's bounds not
// drawn either. How many there are.
uint32_t muiRhiPackInstances(const muiDrawList* list, const muiRhiPacking* packing,
                             muiRhiInstance* instances);

// A list's gradient table, its placeholder entry 0 included, at least one
// entry: how many there are, and packed when gradients is not NULL.
uint32_t muiRhiPackGradients(const muiDrawList* list, muiRhiGradient* gradients);

// A list's transform table, as muiRhiPackGradients.
uint32_t muiRhiPackTransforms(const muiDrawList* list, muiRhiTransform* transforms);

// A list's clip table, as muiRhiPackGradients.
uint32_t muiRhiPackClips(const muiDrawList* list, muiRhiClip* clips);

#endif // MAUL_UI_RHI_PACK_H
