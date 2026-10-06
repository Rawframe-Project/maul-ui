// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The images a frame draws (record mui-0005). Keys hash by a 64-bit mix
// into buckets probed linearly; the table holds twice the buckets of the
// keys a frame may name, so a probe ends at an empty bucket.

#include "images.h"

#include "allocator.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

muiRhiImages muiRhiMakeImages(const muiAllocator* allocator, mrhiDevice* device,
                              muiRhiImageFunction find, void* context)
{
    return (muiRhiImages){
        .allocator = *allocator, .device = device, .find = find, .context = context};
}

static void Drop(muiRhiImages* images)
{
    muiRhiRelease(&images->allocator, images->entries,
                  (size_t)images->capacity * sizeof(muiRhiImageEntry), alignof(muiRhiImageEntry));
    muiRhiRelease(&images->allocator, images->buckets,
                  (size_t)images->bucketCount * sizeof(uint32_t), alignof(uint32_t));
    muiRhiRelease(&images->allocator, images->textures,
                  (size_t)images->capacity * sizeof(mrhiResourceId), alignof(mrhiResourceId));
    images->entries = nullptr;
    images->buckets = nullptr;
    images->textures = nullptr;
    images->capacity = 0;
    images->bucketCount = 0;
}

void muiRhiFreeImages(muiRhiImages* images)
{
    Drop(images);
}

// Room for a number of keys, the old room given back.
static muiResult Grow(muiRhiImages* images, uint32_t keys)
{
    uint32_t capacity = 16;
    while (capacity < keys)
    {
        capacity *= 2;
    }
    const muiAllocator* allocator = &images->allocator;
    muiRhiImageEntry* entries = muiRhiAllocate(
        allocator, (size_t)capacity * sizeof(muiRhiImageEntry), alignof(muiRhiImageEntry));
    uint32_t* buckets =
        muiRhiAllocate(allocator, (size_t)capacity * 2 * sizeof(uint32_t), alignof(uint32_t));
    mrhiResourceId* textures = muiRhiAllocate(allocator, (size_t)capacity * sizeof(mrhiResourceId),
                                              alignof(mrhiResourceId));
    if (entries == nullptr || buckets == nullptr || textures == nullptr)
    {
        muiRhiRelease(allocator, entries, (size_t)capacity * sizeof(muiRhiImageEntry),
                      alignof(muiRhiImageEntry));
        muiRhiRelease(allocator, buckets, (size_t)capacity * 2 * sizeof(uint32_t),
                      alignof(uint32_t));
        muiRhiRelease(allocator, textures, (size_t)capacity * sizeof(mrhiResourceId),
                      alignof(mrhiResourceId));
        return mui_errorCapacity;
    }
    Drop(images);
    images->entries = entries;
    images->buckets = buckets;
    images->textures = textures;
    images->capacity = capacity;
    images->bucketCount = capacity * 2;
    return mui_success;
}

muiResult muiRhiResetImages(muiRhiImages* images, uint32_t keys)
{
    images->count = 0;
    images->textureCount = 0;
    if (keys == 0)
    {
        return mui_success;
    }
    if (keys > (1u << 24))
    {
        return mui_errorCapacity;
    }
    if (keys > images->capacity)
    {
        muiResult status = Grow(images, keys);
        if (status != mui_success)
        {
            return status;
        }
    }
    memset(images->buckets, 0, (size_t)images->bucketCount * sizeof(uint32_t));
    return mui_success;
}

// A key's bits mixed, as SplitMix64 finishes.
static uint64_t Mix(uint64_t key)
{
    key ^= key >> 30;
    key *= 0xBF58476D1CE4E5B9u;
    key ^= key >> 27;
    key *= 0x94D049BB133111EBu;
    return key ^ (key >> 31);
}

static muiRhiImageEntry Look(const muiRhiImages* images, uint64_t key)
{
    muiRhiImageEntry entry = {.key = key};
    if (images->find == nullptr || !images->find(images->context, key, &entry.image))
    {
        return entry;
    }
    entry.found =
        entry.image.width > 0 && entry.image.height > 0 &&
        mrhiImportTexture(images->device, entry.image.texture, &entry.resource) == mrhi_success;
    return entry;
}

uint32_t muiRhiFindImage(muiRhiImages* images, uint64_t key)
{
    if (images->bucketCount == 0)
    {
        return MUI_RHI_NO_IMAGE;
    }
    uint32_t mask = images->bucketCount - 1;
    uint32_t at = (uint32_t)Mix(key) & mask;
    while (images->buckets[at] != 0)
    {
        uint32_t index = images->buckets[at] - 1;
        if (images->entries[index].key == key)
        {
            return images->entries[index].found ? index : MUI_RHI_NO_IMAGE;
        }
        at = (at + 1) & mask;
    }
    if (images->count == images->capacity)
    {
        return MUI_RHI_NO_IMAGE;
    }
    uint32_t index = images->count++;
    images->entries[index] = Look(images, key);
    images->buckets[at] = index + 1;
    return images->entries[index].found ? index : MUI_RHI_NO_IMAGE;
}

static int Compare(const void* a, const void* b)
{
    const mrhiResourceId* left = a;
    const mrhiResourceId* right = b;
    uint64_t x = ((uint64_t)left->index1 << 32) | left->generation;
    uint64_t y = ((uint64_t)right->index1 << 32) | right->generation;
    return (x > y) - (x < y);
}

void muiRhiListTextures(muiRhiImages* images)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < images->count; i++)
    {
        if (images->entries[i].found)
        {
            images->textures[count++] = images->entries[i].resource;
        }
    }
    qsort(images->textures, count, sizeof(mrhiResourceId), Compare);
    uint32_t distinct = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        if (distinct == 0 || Compare(&images->textures[distinct - 1], &images->textures[i]) != 0)
        {
            images->textures[distinct++] = images->textures[i];
        }
    }
    images->textureCount = distinct;
}
