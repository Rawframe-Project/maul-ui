// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A frame's passes (record mui-0005). Boxes and shadows sample nothing,
// so they join any draw; a draw takes the texture of its first image or
// glyph, and the next of another texture starts a new one.

#include "plan.h"

#include "allocator.h"

#include <stdalign.h>

void muiRhiFreePlan(muiRhiPlan* plan)
{
    muiRhiRelease(&plan->allocator, plan->draws, (size_t)plan->drawCapacity * sizeof(muiRhiDraw),
                  alignof(muiRhiDraw));
    muiRhiRelease(&plan->allocator, plan->accesses,
                  (size_t)plan->accessCapacity * sizeof(mrhiAccess), alignof(mrhiAccess));
    muiRhiRelease(&plan->allocator, plan->uploads,
                  (size_t)plan->uploadCapacity * sizeof(mrhiAccess), alignof(mrhiAccess));
    plan->draws = nullptr;
    plan->accesses = nullptr;
    plan->uploads = nullptr;
    plan->drawCapacity = 0;
    plan->accessCapacity = 0;
    plan->uploadCapacity = 0;
}

// Room for a number of items of a size in an array, grown to the next
// power of 2 and its contents dropped.
static bool Reserve(const muiAllocator* allocator, void** items, uint32_t* capacity,
                    uint32_t needed, size_t size, size_t alignment)
{
    if (needed <= *capacity)
    {
        return true;
    }
    uint32_t grown = 16;
    while (grown < needed && grown <= (1u << 24))
    {
        grown *= 2;
    }
    void* memory =
        grown >= needed ? muiRhiAllocate(allocator, (size_t)grown * size, alignment) : nullptr;
    if (memory == nullptr)
    {
        return false;
    }
    muiRhiRelease(allocator, *items, (size_t)*capacity * size, alignment);
    *items = memory;
    *capacity = grown;
    return true;
}

static bool Same(mrhiResourceId a, mrhiResourceId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void Split(muiRhiPlan* plan, const muiRhiInstance* instances, uint32_t count,
                  const muiRhiSources* sources)
{
    plan->drawCount = 0;
    muiRhiDraw draw = {0};
    bool textured = false;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t kind = instances[i].kind;
        if (kind == mui_drawImage || kind == mui_drawGlyphRun)
        {
            mrhiResourceId texture = kind == mui_drawImage
                                         ? sources->images->entries[instances[i].index].resource
                                         : sources->glyphs->resources[instances[i].index];
            if (textured && !Same(texture, draw.texture))
            {
                plan->draws[plan->drawCount++] = draw;
                draw = (muiRhiDraw){.first = i};
            }
            draw.texture = texture;
            textured = true;
        }
        draw.count++;
    }
    if (draw.count > 0)
    {
        plan->draws[plan->drawCount++] = draw;
    }
}

static mrhiAccess Reading(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){.resource = resource,
                        .kind = kind,
                        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING}};
}

// The pages a frame's changed rectangles write, each once.
static void AddWrites(muiRhiPlan* plan, const muiRhiGlyphs* glyphs)
{
    bool written[MUI_RHI_MAX_PAGES] = {0};
    for (uint32_t i = 0; i < glyphs->updateCount; i++)
    {
        written[muiRhiUpdatedPage(glyphs, i)] = true;
    }
    for (uint32_t page = 0; page < glyphs->pageCount; page++)
    {
        if (written[page])
        {
            plan->uploads[plan->uploadCount++] =
                Reading(glyphs->resources[page], mrhi_accessCopyDestination);
        }
    }
}

muiResult muiRhiMakePlan(muiRhiPlan* plan, const muiRhiInstance* instances, uint32_t count,
                         const muiRhiSources* sources)
{
    const muiRhiImages* images = sources->images;
    const muiRhiGlyphs* glyphs = sources->glyphs;
    uint32_t accesses = sources->bufferCount + images->textureCount + glyphs->pageCount + 1;
    uint32_t uploads = sources->bufferCount + glyphs->pageCount;
    const muiAllocator* allocator = &plan->allocator;
    if (!Reserve(allocator, (void**)&plan->draws, &plan->drawCapacity, count + 1,
                 sizeof(muiRhiDraw), alignof(muiRhiDraw)) ||
        !Reserve(allocator, (void**)&plan->accesses, &plan->accessCapacity, accesses,
                 sizeof(mrhiAccess), alignof(mrhiAccess)) ||
        !Reserve(allocator, (void**)&plan->uploads, &plan->uploadCapacity, uploads,
                 sizeof(mrhiAccess), alignof(mrhiAccess)))
    {
        return mui_errorCapacity;
    }
    Split(plan, instances, count, sources);
    plan->accessCount = 0;
    plan->uploadCount = 0;
    for (uint32_t i = 0; i < sources->bufferCount; i++)
    {
        plan->accesses[plan->accessCount++] = Reading(sources->buffers[i], mrhi_accessStorageRead);
        plan->uploads[plan->uploadCount++] =
            Reading(sources->buffers[i], mrhi_accessCopyDestination);
    }
    for (uint32_t i = 0; i < images->textureCount; i++)
    {
        plan->accesses[plan->accessCount++] = Reading(images->textures[i], mrhi_accessSampled);
    }
    for (uint32_t i = 0; i < glyphs->pageCount; i++)
    {
        plan->accesses[plan->accessCount++] = Reading(glyphs->resources[i], mrhi_accessSampled);
    }
    AddWrites(plan, glyphs);
    // A list that samples nothing draws with the placeholder bound.
    if (images->textureCount == 0 && glyphs->pageCount == 0 && plan->drawCount > 0)
    {
        plan->draws[0].texture = sources->placeholder;
        plan->accesses[plan->accessCount++] = Reading(sources->placeholder, mrhi_accessSampled);
    }
    return mui_success;
}
