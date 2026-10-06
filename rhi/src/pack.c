// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's records (record mui-0005). A shadow's shape is
// its box offset and grown by its spread, as CSS's box-shadow, its radii
// grown with it by CSS's adjustment for small radii (or shrunk, inset);
// an outer shadow's quad reaches three sigmas past its shape, an inset
// one's is its box.

#include "pack.h"

#include <math.h>
#include <string.h>

static_assert(sizeof(muiRhiInstance) == 144, "an instance is the shader's 144 bytes");
static_assert(sizeof(muiRhiGradient) == 112, "a gradient is the shader's 112 bytes");

static bool IsDrawn(muiDrawKind kind)
{
    return kind == mui_drawBox || kind == mui_drawShadow;
}

uint32_t muiRhiCountInstances(const muiDrawList* list)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        count += IsDrawn(list->commands[i].kind) ? 1 : 0;
    }
    return count;
}

static muiRhiInstance BoxOf(const muiDrawCommand* command)
{
    const muiDrawBox* box = &command->box;
    muiRhiInstance instance = {
        .rect = box->rect,
        .radii = box->radii,
        .fill = box->fill,
        .widths = box->borderWidths,
        .kind = mui_drawBox,
        .clip = command->clip,
        .transform = command->transform,
        .gradient = box->gradient,
    };
    memcpy(instance.colors, box->borderColors, sizeof(instance.colors));
    return instance;
}

// A radius grown by an outer shadow's spread: by the spread where it is
// at least as large, less where it is smaller, and a square corner kept
// square (CSS Backgrounds 3, the spread distance's adjustment).
static float Spread(float radius, float spread)
{
    if (spread < 0.0f || radius >= spread)
    {
        return fmaxf(radius + spread, 0.0f);
    }
    if (radius <= 0.0f)
    {
        return 0.0f;
    }
    float ratio = radius / spread - 1.0f;
    return radius + spread * (1.0f + ratio * ratio * ratio);
}

static muiCorners SpreadAll(muiCorners radii, float spread)
{
    return (muiCorners){Spread(radii.topLeft, spread), Spread(radii.topRight, spread),
                        Spread(radii.bottomRight, spread), Spread(radii.bottomLeft, spread)};
}

static muiRhiInstance ShadowOf(const muiDrawCommand* command)
{
    const muiDrawShadow* shadow = &command->shadow;
    bool inset = shadow->inset != 0;
    float spread = inset ? -shadow->spread : shadow->spread;
    muiRect shape = {
        shadow->rect.x + shadow->offsetX - spread,
        shadow->rect.y + shadow->offsetY - spread,
        fmaxf(shadow->rect.width + 2.0f * spread, 0.0f),
        fmaxf(shadow->rect.height + 2.0f * spread, 0.0f),
    };
    float sigma = shadow->blur * 0.5f;
    float reach = 3.0f * sigma;
    muiRect quad = inset ? shadow->rect
                         : (muiRect){shape.x - reach, shape.y - reach, shape.width + 2.0f * reach,
                                     shape.height + 2.0f * reach};
    const muiCorners box = shadow->radii;
    return (muiRhiInstance){
        .rect = quad,
        .radii = SpreadAll(box, spread),
        .fill = shadow->color,
        .widths = {shape.x, shape.y, shape.width, shape.height},
        .colors =
            {
                {shadow->rect.x, shadow->rect.y, shadow->rect.width, shadow->rect.height},
                {box.topLeft, box.topRight, box.bottomRight, box.bottomLeft},
                {sigma, inset ? 1.0f : 0.0f, 0.0f, 0.0f},
            },
        .kind = mui_drawShadow,
        .clip = command->clip,
        .transform = command->transform,
    };
}

void muiRhiPackInstances(const muiDrawList* list, muiRhiInstance* instances)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        const muiDrawCommand* command = &list->commands[i];
        if (command->kind == mui_drawBox)
        {
            instances[count++] = BoxOf(command);
        }
        else if (command->kind == mui_drawShadow)
        {
            instances[count++] = ShadowOf(command);
        }
    }
}

uint32_t muiRhiPackGradients(const muiDrawList* list, muiRhiGradient* gradients)
{
    uint32_t count = list->gradientCount > 0 ? list->gradientCount : 1;
    if (gradients == nullptr)
    {
        return count;
    }
    memset(gradients, 0, (size_t)count * sizeof(muiRhiGradient));
    for (uint32_t i = 0; i < list->gradientCount; i++)
    {
        const muiDrawGradient* from = &list->gradients[i];
        muiRhiGradient* to = &gradients[i];
        to->kind = from->kind;
        to->stopCount = from->stopCount < MUI_MAX_DRAW_STOPS ? from->stopCount : MUI_MAX_DRAW_STOPS;
        to->interpolation = from->interpolation;
        to->angle = from->angle;
        memcpy(to->colors, from->colors, sizeof(to->colors));
        memcpy(to->positions, from->positions, sizeof(to->positions));
    }
    return count;
}
