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

#include "maul-ui/draw.h"

#include <stdint.h>

// An instance (std430): a box, or a shadow, whose fields then hold its
// quad (rect), its shape's radii, its color (fill), its shape (widths, as
// x, y, width and height), its box and the box's radii (colors 0 and 1),
// and its blur's sigma and whether it is inset (colors 2).
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
    uint32_t gradient;
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

// The instances a list's commands make.
uint32_t muiRhiCountInstances(const muiDrawList* list);

// A list's commands as instances, as many as muiRhiCountInstances says.
void muiRhiPackInstances(const muiDrawList* list, muiRhiInstance* instances);

// A list's gradient table, its placeholder entry 0 included, at least one
// entry: how many there are, and packed when gradients is not NULL.
uint32_t muiRhiPackGradients(const muiDrawList* list, muiRhiGradient* gradients);

// A list's transform table, as muiRhiPackGradients.
uint32_t muiRhiPackTransforms(const muiDrawList* list, muiRhiTransform* transforms);

// A list's clip table, as muiRhiPackGradients.
uint32_t muiRhiPackClips(const muiDrawList* list, muiRhiClip* clips);

#endif // MAUL_UI_RHI_PACK_H
