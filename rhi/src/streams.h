// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The renderer's streams (record mui-0005): a device buffer of records,
// the staging a frame packs them into, and the records the buffer holds
// as last written, so that a frame writes only the runs of records that
// differ from them. Runs fewer bytes apart than an upload block are
// merged; a buffer made anew holds nothing, and a frame writes it whole.

#ifndef MAUL_UI_RHI_STREAMS_H
#define MAUL_UI_RHI_STREAMS_H

#include "maul-rhi/frame.h"
#include "maul-ui/base.h"

#include <stddef.h>
#include <stdint.h>

// Maul RHI places each upload at a boundary of this many bytes.
#define MUI_RHI_UPLOAD_BLOCK 512u

// Records from first, count of them.
typedef struct muiRhiRun
{
    uint32_t first;
    uint32_t count;
} muiRhiRun;

typedef struct muiRhiStream
{
    mrhiBufferId buffer;
    mrhiResourceId resource;
    size_t stride;
    uint32_t capacity;
    // This frame's records.
    void* staging;
    uint32_t count;
    // The records the buffer holds, the first heldCount of them.
    void* held;
    uint32_t heldCount;
    // The runs this frame writes.
    muiRhiRun* runs;
    uint32_t runCount;
} muiRhiStream;

void muiRhiDropStream(mrhiDevice* device, const muiAllocator* allocator, muiRhiStream* stream);

// Room for a number of records, the capacity doubled, at least `first`
// the first time, until it holds them; a buffer made anew holds nothing.
muiResult muiRhiReserveStream(mrhiDevice* device, const muiAllocator* allocator,
                              muiRhiStream* stream, uint32_t count, uint32_t first);

// Finds the runs of this frame's records that differ from those held.
void muiRhiDiffStream(muiRhiStream* stream);

// The bytes the runs take of a frame's uploads.
uint64_t muiRhiStreamUploadBytes(const muiRhiStream* stream);

// Writes the runs in a pass that declares the buffer a copy destination.
bool muiRhiWriteStream(mrhiDevice* device, mrhiPassId pass, const muiRhiStream* stream);

// After the runs are written: the buffer holds this frame's records.
void muiRhiKeepStream(muiRhiStream* stream);

#endif // MAUL_UI_RHI_STREAMS_H
