// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's records (record mui-0005). A shadow's shape is
// its box offset and grown by its spread, as CSS's box-shadow, its radii
// grown with it by CSS's adjustment for small radii (or shrunk, inset);
// an outer shadow's quad reaches three sigmas past its shape, an inset
// one's is its box. An image's slice insets are one logical unit a texel
// as drawn, all four shrunk by one factor where two facing ones would
// not fit, as CSS's border-image shrinks them.

#include "pack.h"

#include <math.h>
#include <string.h>

static_assert(sizeof(muiRhiInstance) == 144, "an instance is the shader's 144 bytes");
static_assert(sizeof(muiRhiGradient) == 112, "a gradient is the shader's 112 bytes");
static_assert(sizeof(muiRhiTransform) == 32, "a transform is the shader's 32 bytes");
static_assert(sizeof(muiRhiClip) == 48, "a clip is the shader's 48 bytes");

// An index into a table of a number of entries, 0 where it is past them.
static uint32_t Index(uint32_t index, uint32_t count)
{
    return index < count ? index : 0;
}

static bool IsDrawn(muiDrawKind kind)
{
    return kind == mui_drawBox || kind == mui_drawShadow || kind == mui_drawImage;
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

uint32_t muiRhiCountImages(const muiDrawList* list)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        count += list->commands[i].kind == mui_drawImage ? 1 : 0;
    }
    return count;
}

static muiRhiInstance BoxOf(const muiDrawList* list, const muiDrawCommand* command)
{
    const muiDrawBox* box = &command->box;
    muiRhiInstance instance = {
        .rect = box->rect,
        .radii = box->radii,
        .fill = box->fill,
        .widths = box->borderWidths,
        .kind = mui_drawBox,
        .clip = Index(command->clip, list->clipCount),
        .transform = Index(command->transform, list->transformCount),
        .index = Index(box->gradient, list->gradientCount),
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

static muiRhiInstance ShadowOf(const muiDrawList* list, const muiDrawCommand* command)
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
        .clip = Index(command->clip, list->clipCount),
        .transform = Index(command->transform, list->transformCount),
    };
}

// The factor that fits two facing insets into a length.
static float Fit(float length, float low, float high)
{
    return low + high > length && low + high > 0.0f ? length / (low + high) : 1.0f;
}

static muiRhiInstance ImageOf(const muiDrawList* list, const muiDrawCommand* command,
                              const muiRhiImageEntry* entry, uint32_t index)
{
    const muiDrawImage* image = &command->image;
    const muiSides slice = {fmaxf(image->slice.top, 0.0f), fmaxf(image->slice.right, 0.0f),
                            fmaxf(image->slice.bottom, 0.0f), fmaxf(image->slice.left, 0.0f)};
    float fit = fminf(Fit(image->rect.width, slice.left, slice.right),
                      fminf(Fit(image->rect.height, slice.top, slice.bottom), 1.0f));
    float width = (float)entry->image.width;
    float height = (float)entry->image.height;
    return (muiRhiInstance){
        .rect = image->rect,
        .fill = image->tint,
        .colors =
            {
                {image->uv.x, image->uv.y, image->uv.x + image->uv.width,
                 image->uv.y + image->uv.height},
                {slice.top * fit, slice.right * fit, slice.bottom * fit, slice.left * fit},
                {slice.top / height, slice.right / width, slice.bottom / height,
                 slice.left / width},
            },
        .kind = mui_drawImage,
        .clip = Index(command->clip, list->clipCount),
        .transform = Index(command->transform, list->transformCount),
        .index = index,
    };
}

uint32_t muiRhiPackInstances(const muiDrawList* list, muiRhiImages* images,
                             muiRhiInstance* instances)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        const muiDrawCommand* command = &list->commands[i];
        if (command->kind == mui_drawBox)
        {
            instances[count++] = BoxOf(list, command);
        }
        else if (command->kind == mui_drawShadow)
        {
            instances[count++] = ShadowOf(list, command);
        }
        else if (command->kind == mui_drawImage)
        {
            uint32_t index = muiRhiFindImage(images, command->image.image);
            if (index != MUI_RHI_NO_IMAGE)
            {
                instances[count++] = ImageOf(list, command, &images->entries[index], index);
            }
        }
    }
    return count;
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

uint32_t muiRhiPackTransforms(const muiDrawList* list, muiRhiTransform* transforms)
{
    uint32_t count = list->transformCount > 0 ? list->transformCount : 1;
    if (transforms == nullptr)
    {
        return count;
    }
    transforms[0] = (muiRhiTransform){{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}};
    for (uint32_t i = 0; i < list->transformCount; i++)
    {
        const muiDrawTransform* from = &list->transforms[i];
        transforms[i] =
            (muiRhiTransform){{from->a, from->b, from->c, from->d}, {from->e, from->f, 0.0f, 0.0f}};
    }
    return count;
}

uint32_t muiRhiPackClips(const muiDrawList* list, muiRhiClip* clips)
{
    uint32_t count = list->clipCount > 0 ? list->clipCount : 1;
    if (clips == nullptr)
    {
        return count;
    }
    clips[0] = (muiRhiClip){0};
    for (uint32_t i = 0; i < list->clipCount; i++)
    {
        const muiDrawClip* from = &list->clips[i];
        clips[i] = (muiRhiClip){
            .rect = from->rect,
            .radii = from->radii,
            .parent = Index(from->parent, list->clipCount),
            .transform = Index(from->transform, list->transformCount),
            .invert = from->invert != 0 ? 1u : 0u,
        };
    }
    return count;
}
