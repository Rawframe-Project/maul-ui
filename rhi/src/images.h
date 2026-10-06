// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The images a frame draws (record mui-0005): each key a list names found
// through the host's function once a frame, its texture imported into
// the open frame, and the frame's distinct textures, each once, for the
// draw pass's accesses.

#ifndef MAUL_UI_RHI_IMAGES_H
#define MAUL_UI_RHI_IMAGES_H

#include "maul-ui-rhi/renderer.h"

#include <stdint.h>

// An index that names no entry.
#define MUI_RHI_NO_IMAGE UINT32_MAX

typedef struct muiRhiImageEntry
{
    uint64_t key;
    muiRhiImage image;
    // The texture in the open frame.
    mrhiResourceId resource;
    bool found;
} muiRhiImageEntry;

typedef struct muiRhiImages
{
    muiAllocator allocator;
    mrhiDevice* device;
    muiRhiImageFunction find;
    void* context;
    muiRhiImageEntry* entries;
    uint32_t count;
    uint32_t capacity;
    // Open addressing over the entries, each bucket an entry's index plus
    // 1 or 0 when empty; twice the capacity, a power of 2.
    uint32_t* buckets;
    uint32_t bucketCount;
    // The frame's distinct textures, sorted, as muiRhiListTextures made.
    mrhiResourceId* textures;
    uint32_t textureCount;
} muiRhiImages;

// An empty table for a device and the host's function, which may be
// NULL; it allocates nothing until a frame names images.
muiRhiImages muiRhiMakeImages(const muiAllocator* allocator, mrhiDevice* device,
                              muiRhiImageFunction find, void* context);

void muiRhiFreeImages(muiRhiImages* images);

// Empties the table for a frame naming at most a number of keys, with room
// for them all.
muiResult muiRhiResetImages(muiRhiImages* images, uint32_t keys);

// The entry of a key: found through the host's function, and imported,
// the first time the frame names it; MUI_RHI_NO_IMAGE when the host has
// no image for it, its texture cannot be imported, or the table is full.
uint32_t muiRhiFindImage(muiRhiImages* images, uint64_t key);

// Lists the frame's distinct textures in images->textures.
void muiRhiListTextures(muiRhiImages* images);

#endif // MAUL_UI_RHI_IMAGES_H
