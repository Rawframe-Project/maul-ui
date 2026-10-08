// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Culling as a list is packed (record mui-0005).

#include "cull.h"

#include "allocator.h"

#include <math.h>
#include <stdalign.h>

void muiRhiFreeCull(muiRhiCull* cull)
{
    muiRhiRelease(&cull->allocator, cull->bounds, (size_t)cull->capacity * sizeof(muiRhiBounds),
                  alignof(muiRhiBounds));
    cull->bounds = nullptr;
    cull->capacity = 0;
    cull->count = 0;
}

static muiDrawTransform TransformOf(const muiDrawList* list, uint32_t transform)
{
    return transform < list->transformCount ? list->transforms[transform]
                                            : (muiDrawTransform){1, 0, 0, 1, 0, 0};
}

// A rect's corners through a transform, boxed.
static muiRhiBounds Through(muiRect rect, muiDrawTransform t)
{
    const float xs[2] = {rect.x, rect.x + rect.width};
    const float ys[2] = {rect.y, rect.y + rect.height};
    muiRhiBounds box = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    for (int i = 0; i < 4; i++)
    {
        float x = t.a * xs[i & 1] + t.c * ys[i >> 1] + t.e;
        float y = t.b * xs[i & 1] + t.d * ys[i >> 1] + t.f;
        box =
            (muiRhiBounds){fminf(box.x0, x), fminf(box.y0, y), fmaxf(box.x1, x), fmaxf(box.y1, y)};
    }
    return box;
}

static muiRhiBounds Meet(muiRhiBounds a, muiRhiBounds b)
{
    return (muiRhiBounds){fmaxf(a.x0, b.x0), fmaxf(a.y0, b.y0), fminf(a.x1, b.x1),
                          fminf(a.y1, b.y1)};
}

muiResult muiRhiPrepareCull(muiRhiCull* cull, const muiDrawList* list, uint32_t width,
                            uint32_t height)
{
    float scale = list->header.scale > 0.0f ? list->header.scale : 1.0f;
    const muiRhiBounds target = {0.0f, 0.0f, (float)width / scale, (float)height / scale};
    return muiRhiPrepareCullWithin(cull, list, target);
}

muiResult muiRhiPrepareCullWithin(muiRhiCull* cull, const muiDrawList* list, muiRhiBounds target)
{
    uint32_t count = list->clipCount > 0 ? list->clipCount : 1;
    if (count > cull->capacity)
    {
        muiRhiBounds* bounds = muiRhiAllocate(
            &cull->allocator, (size_t)count * sizeof(muiRhiBounds), alignof(muiRhiBounds));
        if (bounds == nullptr)
        {
            return mui_errorCapacity;
        }
        muiRhiFreeCull(cull);
        cull->bounds = bounds;
        cull->capacity = count;
    }
    float scale = list->header.scale > 0.0f ? list->header.scale : 1.0f;
    // An edge covers half a device pixel past its quad, grown by one.
    cull->margin = 1.0f / scale;
    cull->count = count;
    cull->bounds[0] = target;
    for (uint32_t i = 1; i < count; i++)
    {
        const muiDrawClip* clip = &list->clips[i];
        muiRhiBounds own =
            clip->invert != 0
                ? target
                : Meet(target, Through(clip->rect, TransformOf(list, clip->transform)));
        cull->bounds[i] = clip->parent < i ? Meet(own, cull->bounds[clip->parent]) : own;
    }
    return mui_success;
}

bool muiRhiIsCulled(const muiRhiCull* cull, const muiDrawList* list, muiRect quad, uint32_t clip,
                    uint32_t transform)
{
    const muiRhiBounds* bounds = &cull->bounds[clip < cull->count ? clip : 0];
    muiRhiBounds box = Through(quad, TransformOf(list, transform));
    float margin = cull->margin;
    // Comparisons with NaN keep the quad.
    return box.x1 + margin < bounds->x0 || box.x0 - margin > bounds->x1 ||
           box.y1 + margin < bounds->y0 || box.y0 - margin > bounds->y1;
}
