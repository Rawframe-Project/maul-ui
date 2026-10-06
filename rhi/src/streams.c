// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The renderer's streams (record mui-0005).

#include "streams.h"

#include "allocator.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"

#include <stdalign.h>
#include <string.h>

static size_t BytesOf(const muiRhiStream* stream, uint32_t records)
{
    return (size_t)records * stream->stride;
}

void muiRhiDropStream(mrhiDevice* device, const muiAllocator* allocator, muiRhiStream* stream)
{
    if (stream->staging == nullptr)
    {
        return;
    }
    (void)mrhiDestroyBuffer(device, stream->buffer);
    muiRhiRelease(allocator, stream->staging, BytesOf(stream, stream->capacity),
                  alignof(max_align_t));
    muiRhiRelease(allocator, stream->held, BytesOf(stream, stream->capacity), alignof(max_align_t));
    muiRhiRelease(allocator, stream->runs, (size_t)stream->capacity * sizeof(muiRhiRun),
                  alignof(muiRhiRun));
    stream->staging = nullptr;
    stream->held = nullptr;
    stream->runs = nullptr;
}

// The stream's buffer, staging, held records and runs at a capacity,
// replacing the old.
static muiResult Grow(mrhiDevice* device, const muiAllocator* allocator, muiRhiStream* stream,
                      uint32_t capacity)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = (uint64_t)capacity * stream->stride;
    def.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    size_t bytes = (size_t)capacity * stream->stride;
    void* staging = muiRhiAllocate(allocator, bytes, alignof(max_align_t));
    void* held = muiRhiAllocate(allocator, bytes, alignof(max_align_t));
    muiRhiRun* runs =
        muiRhiAllocate(allocator, (size_t)capacity * sizeof(muiRhiRun), alignof(muiRhiRun));
    mrhiBufferId buffer = {0};
    muiResult status =
        staging == nullptr || held == nullptr || runs == nullptr
            ? mui_errorCapacity
            : (mrhiCreateBuffer(device, &def, &buffer) == mrhi_success ? mui_success
                                                                       : mui_errorPlatform);
    if (status != mui_success)
    {
        muiRhiRelease(allocator, staging, bytes, alignof(max_align_t));
        muiRhiRelease(allocator, held, bytes, alignof(max_align_t));
        muiRhiRelease(allocator, runs, (size_t)capacity * sizeof(muiRhiRun), alignof(muiRhiRun));
        return status;
    }
    muiRhiDropStream(device, allocator, stream);
    stream->buffer = buffer;
    stream->staging = staging;
    stream->held = held;
    stream->runs = runs;
    stream->capacity = capacity;
    stream->heldCount = 0;
    return mui_success;
}

muiResult muiRhiReserveStream(mrhiDevice* device, const muiAllocator* allocator,
                              muiRhiStream* stream, uint32_t count, uint32_t first)
{
    uint32_t capacity = stream->capacity > 0 ? stream->capacity : first;
    while (capacity < count && capacity <= (1u << 23))
    {
        capacity *= 2;
    }
    if (capacity < count)
    {
        return mui_errorCapacity;
    }
    stream->count = count;
    return capacity != stream->capacity ? Grow(device, allocator, stream, capacity) : mui_success;
}

static bool Differs(const muiRhiStream* stream, uint32_t record)
{
    size_t at = BytesOf(stream, record);
    return record >= stream->heldCount ||
           memcmp((const unsigned char*)stream->staging + at,
                  (const unsigned char*)stream->held + at, stream->stride) != 0;
}

void muiRhiDiffStream(muiRhiStream* stream)
{
    stream->runCount = 0;
    // Records closer than a block's bytes join the run before them.
    uint32_t reach = (uint32_t)((MUI_RHI_UPLOAD_BLOCK + stream->stride - 1) / stream->stride);
    for (uint32_t i = 0; i < stream->count; i++)
    {
        if (!Differs(stream, i))
        {
            continue;
        }
        muiRhiRun* last = stream->runCount > 0 ? &stream->runs[stream->runCount - 1] : nullptr;
        if (last != nullptr && i - (last->first + last->count) < reach)
        {
            last->count = i + 1 - last->first;
        }
        else
        {
            stream->runs[stream->runCount++] = (muiRhiRun){i, 1};
        }
    }
}

uint64_t muiRhiStreamUploadBytes(const muiRhiStream* stream)
{
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < stream->runCount; i++)
    {
        uint64_t run = BytesOf(stream, stream->runs[i].count);
        bytes += (run + MUI_RHI_UPLOAD_BLOCK - 1) / MUI_RHI_UPLOAD_BLOCK * MUI_RHI_UPLOAD_BLOCK;
    }
    return bytes;
}

bool muiRhiWriteStream(mrhiDevice* device, mrhiPassId pass, const muiRhiStream* stream)
{
    for (uint32_t i = 0; i < stream->runCount; i++)
    {
        const muiRhiRun* run = &stream->runs[i];
        size_t at = BytesOf(stream, run->first);
        if (mrhiWriteBuffer(device, pass, stream->resource, at,
                            (const unsigned char*)stream->staging + at,
                            BytesOf(stream, run->count)) != mrhi_success)
        {
            return false;
        }
    }
    return true;
}

void muiRhiKeepStream(muiRhiStream* stream)
{
    for (uint32_t i = 0; i < stream->runCount; i++)
    {
        size_t at = BytesOf(stream, stream->runs[i].first);
        memcpy((unsigned char*)stream->held + at, (const unsigned char*)stream->staging + at,
               BytesOf(stream, stream->runs[i].count));
    }
    stream->heldCount = stream->count;
    stream->runCount = 0;
}
