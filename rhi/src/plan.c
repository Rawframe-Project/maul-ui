// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A frame's draw pass (record mui-0005). Boxes and shadows sample
// nothing, so they join any draw; a draw takes the texture of its first
// image, and the next image of another texture starts a new one.

#include "plan.h"

#include "allocator.h"

#include <stdalign.h>

void muiRhiFreePlan(muiRhiPlan* plan)
{
    muiRhiRelease(&plan->allocator, plan->draws, (size_t)plan->drawCapacity * sizeof(muiRhiDraw),
                  alignof(muiRhiDraw));
    muiRhiRelease(&plan->allocator, plan->accesses,
                  (size_t)plan->accessCapacity * sizeof(mrhiAccess), alignof(mrhiAccess));
    plan->draws = nullptr;
    plan->accesses = nullptr;
    plan->drawCapacity = 0;
    plan->accessCapacity = 0;
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
                  const muiRhiImages* images)
{
    plan->drawCount = 0;
    muiRhiDraw draw = {0};
    bool textured = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (instances[i].kind == mui_drawImage)
        {
            mrhiResourceId texture = images->entries[instances[i].index].resource;
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

muiResult muiRhiMakePlan(muiRhiPlan* plan, const muiRhiInstance* instances, uint32_t count,
                         const muiRhiImages* images, const mrhiResourceId* buffers,
                         uint32_t bufferCount, mrhiResourceId placeholder)
{
    uint32_t accesses = bufferCount + images->textureCount + 1;
    if (!Reserve(&plan->allocator, (void**)&plan->draws, &plan->drawCapacity, count + 1,
                 sizeof(muiRhiDraw), alignof(muiRhiDraw)) ||
        !Reserve(&plan->allocator, (void**)&plan->accesses, &plan->accessCapacity, accesses,
                 sizeof(mrhiAccess), alignof(mrhiAccess)))
    {
        return mui_errorCapacity;
    }
    Split(plan, instances, count, images);
    plan->accessCount = 0;
    for (uint32_t i = 0; i < bufferCount; i++)
    {
        plan->accesses[plan->accessCount++] = Reading(buffers[i], mrhi_accessStorageRead);
    }
    for (uint32_t i = 0; i < images->textureCount; i++)
    {
        plan->accesses[plan->accessCount++] = Reading(images->textures[i], mrhi_accessSampled);
    }
    // A list without images draws with the placeholder bound.
    if (images->textureCount == 0 && plan->drawCount > 0)
    {
        plan->draws[0].texture = placeholder;
        plan->accesses[plan->accessCount++] = Reading(placeholder, mrhi_accessSampled);
    }
    return mui_success;
}
