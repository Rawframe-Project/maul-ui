// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer (record mui-0005): a draw list's commands
// packed into instance records and its gradient table beside them
// (pack.c), uploaded in a pass of their own each frame, and drawn as six
// vertices an instance by one pipeline whose fragment shader evaluates
// each (rhi/shaders/quad.*). It allocates through the public allocator
// alone, as Maul UI's internals stay inside a shared build.

#include "maul-ui-rhi/renderer.h"

#include "pack.h"

#include "../shaders/quad_container.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define DEF_COOKIE 0x6D757268u // "murh"

static bool IsAllocatorValid(const muiAllocator* allocator)
{
    return (allocator->alloc == nullptr) == (allocator->free == nullptr);
}

// The allocator's memory, or the C library's for a zeroed one.
static void* Allocate(const muiAllocator* allocator, size_t size, size_t alignment)
{
    if (allocator->alloc != nullptr)
    {
        return allocator->alloc(size, alignment, allocator->context);
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

static void Release(const muiAllocator* allocator, void* memory, size_t size, size_t alignment)
{
    if (memory == nullptr)
    {
        return;
    }
    if (allocator->free != nullptr)
    {
        allocator->free(memory, size, alignment, allocator->context);
        return;
    }
    free(memory);
}

// A device buffer of records and their staging, grown as lists need.
typedef struct Stream
{
    mrhiBufferId buffer;
    void* staging;
    size_t stride;
    uint32_t capacity;
    uint32_t count;
    mrhiResourceId resource;
} Stream;

struct muiRhiRenderer
{
    muiAllocator allocator;
    mrhiDevice* device;
    mrhiShaderId shader;
    mrhiGraphicsPipelineId pipeline;
    mrhiRequestId request;
    bool ready;
    Stream instances;
    Stream gradients;
    // The passes added this frame, and what the draw needs.
    bool added;
    mrhiPassId upload;
    mrhiPassId draw;
    float frame[4];
};

muiRhiRendererDef muiDefaultRhiRendererDef(void)
{
    return (muiRhiRendererDef){
        .cookie = DEF_COOKIE,
        .targetFormat = mrhi_formatRgba8UnormSrgb,
        .instances = 1024,
    };
}

static bool IsValid(const muiRhiRendererDef* def)
{
    return def->cookie == DEF_COOKIE && IsAllocatorValid(&def->allocator) &&
           def->device != nullptr && def->instances != 0 && def->instances <= (1u << 24);
}

static void DropStream(muiRhiRenderer* renderer, Stream* stream)
{
    if (stream->staging != nullptr)
    {
        (void)mrhiDestroyBuffer(renderer->device, stream->buffer);
        Release(&renderer->allocator, stream->staging, (size_t)stream->capacity * stream->stride,
                alignof(max_align_t));
        stream->staging = nullptr;
    }
}

// A stream's buffer and staging at a capacity, replacing the old.
static muiResult GrowStream(muiRhiRenderer* renderer, Stream* stream, uint32_t capacity)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = (uint64_t)capacity * stream->stride;
    def.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    void* staging =
        Allocate(&renderer->allocator, (size_t)capacity * stream->stride, alignof(max_align_t));
    if (staging == nullptr)
    {
        return mui_errorCapacity;
    }
    mrhiBufferId buffer = {0};
    if (mrhiCreateBuffer(renderer->device, &def, &buffer) != mrhi_success)
    {
        Release(&renderer->allocator, staging, (size_t)capacity * stream->stride,
                alignof(max_align_t));
        return mui_errorPlatform;
    }
    DropStream(renderer, stream);
    stream->buffer = buffer;
    stream->staging = staging;
    stream->capacity = capacity;
    return mui_success;
}

// Room for a number of records, doubling the capacity until it holds them.
static muiResult Reserve(muiRhiRenderer* renderer, Stream* stream, uint32_t count)
{
    uint32_t capacity = stream->capacity;
    while (capacity < count && capacity <= (1u << 23))
    {
        capacity *= 2;
    }
    if (capacity < count)
    {
        return mui_errorCapacity;
    }
    stream->count = count;
    return capacity != stream->capacity ? GrowStream(renderer, stream, capacity) : mui_success;
}

static bool MakePipeline(muiRhiRenderer* renderer, mrhiFormat format)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_quadContainer;
    shaderDef.byteCount = sizeof(s_quadContainer);
    if (mrhiCreateShader(renderer->device, &shaderDef, &renderer->shader) != mrhi_success)
    {
        return false;
    }
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = renderer->shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargetCount = 1;
    def.colorTargets[0].format = format;
    // Premultiplied colors over what is there.
    def.colorTargets[0].blend = true;
    const mrhiBlendComponent over = {mrhi_blendOne, mrhi_blendOneMinusSrcAlpha, mrhi_blendAdd};
    def.colorTargets[0].color = over;
    def.colorTargets[0].alpha = over;
    return mrhiCreateGraphicsPipeline(renderer->device, &def, &renderer->pipeline,
                                      &renderer->request) == mrhi_success;
}

muiResult muiCreateRhiRenderer(const muiRhiRendererDef* def, muiRhiRenderer** rendererOut)
{
    if (rendererOut != nullptr)
    {
        *rendererOut = nullptr;
    }
    if (def == nullptr || rendererOut == nullptr || !IsValid(def))
    {
        return mui_errorInvalid;
    }
    muiRhiRenderer* renderer =
        Allocate(&def->allocator, sizeof(muiRhiRenderer), alignof(muiRhiRenderer));
    if (renderer == nullptr)
    {
        return mui_errorCapacity;
    }
    *renderer = (muiRhiRenderer){
        .allocator = def->allocator,
        .device = def->device,
        .instances = {.stride = sizeof(muiRhiInstance)},
        .gradients = {.stride = sizeof(muiRhiGradient)},
    };
    muiResult status = GrowStream(renderer, &renderer->instances, def->instances);
    status = status == mui_success ? GrowStream(renderer, &renderer->gradients, 16) : status;
    if (status == mui_success && !MakePipeline(renderer, def->targetFormat))
    {
        status = mui_errorPlatform;
    }
    if (status != mui_success)
    {
        muiDestroyRhiRenderer(renderer);
        return status;
    }
    *rendererOut = renderer;
    return mui_success;
}

void muiDestroyRhiRenderer(muiRhiRenderer* renderer)
{
    if (renderer == nullptr)
    {
        return;
    }
    mrhiDevice* device = renderer->device;
    if (renderer->pipeline.index1 != 0)
    {
        (void)mrhiDestroyGraphicsPipeline(device, renderer->pipeline);
    }
    if (renderer->shader.index1 != 0)
    {
        (void)mrhiDestroyShader(device, renderer->shader);
    }
    DropStream(renderer, &renderer->instances);
    DropStream(renderer, &renderer->gradients);
    const muiAllocator allocator = renderer->allocator;
    Release(&allocator, renderer, sizeof(muiRhiRenderer), alignof(muiRhiRenderer));
}

mrhiRequestId muiRhiRenderer_GetPipelineRequest(const muiRhiRenderer* renderer)
{
    return renderer != nullptr ? renderer->request : (mrhiRequestId){0};
}

bool muiRhiRenderer_Notify(muiRhiRenderer* renderer, const mrhiDeviceNotification* notification)
{
    if (renderer == nullptr || notification == nullptr ||
        notification->kind != mrhi_devicePipelineReady ||
        notification->requestId.index1 != renderer->request.index1 ||
        notification->requestId.generation != renderer->request.generation)
    {
        return false;
    }
    renderer->ready = notification->outcome == mrhi_success;
    return true;
}

bool muiRhiRenderer_IsReady(const muiRhiRenderer* renderer)
{
    return renderer != nullptr && renderer->ready;
}

// The list's instances and gradients into the streams' staging.
static muiResult Pack(muiRhiRenderer* renderer, const muiDrawList* list)
{
    muiResult status = Reserve(renderer, &renderer->instances, muiRhiCountInstances(list));
    status = status == mui_success
                 ? Reserve(renderer, &renderer->gradients, muiRhiPackGradients(list, nullptr))
                 : status;
    if (status != mui_success)
    {
        return status;
    }
    muiRhiPackInstances(list, renderer->instances.staging);
    (void)muiRhiPackGradients(list, renderer->gradients.staging);
    return mui_success;
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource, .kind = kind, .range = {.mipCount = 1, .layerCount = 1}};
}

muiResult muiRhiRenderer_AddPasses(muiRhiRenderer* renderer, const muiDrawList* list,
                                   const muiRhiTarget* target)
{
    if (renderer == nullptr || list == nullptr || target == nullptr || target->width == 0 ||
        target->height == 0)
    {
        return mui_errorInvalid;
    }
    renderer->added = false;
    if (!renderer->ready)
    {
        return mui_empty;
    }
    muiResult status = Pack(renderer, list);
    if (status != mui_success)
    {
        return status;
    }
    mrhiDevice* device = renderer->device;
    Stream* instances = &renderer->instances;
    Stream* gradients = &renderer->gradients;
    if (mrhiImportBuffer(device, instances->buffer, &instances->resource) != mrhi_success ||
        mrhiImportBuffer(device, gradients->buffer, &gradients->resource) != mrhi_success)
    {
        return mui_errorPlatform;
    }
    const mrhiAccess writes[2] = {Whole(instances->resource, mrhi_accessCopyDestination),
                                  Whole(gradients->resource, mrhi_accessCopyDestination)};
    mrhiPassDef uploadDef = mrhiDefaultPassDef();
    uploadDef.passClass = mrhi_passTransfer;
    uploadDef.accesses = writes;
    uploadDef.accessCount = 2;
    const mrhiAccess reads[2] = {Whole(instances->resource, mrhi_accessStorageRead),
                                 Whole(gradients->resource, mrhi_accessStorageRead)};
    const muiLinearColor clear = target->clearColor;
    mrhiPassDef drawDef = mrhiDefaultPassDef();
    drawDef.colorTargets[0] = (mrhiColorTarget){
        .resource = target->resource,
        .load = target->clear ? mrhi_loadClear : mrhi_loadKeep,
        .store = mrhi_storeKeep,
        .clear = {clear.r, clear.g, clear.b, clear.a},
    };
    drawDef.colorTargetCount = 1;
    drawDef.accesses = reads;
    drawDef.accessCount = 2;
    if (mrhiAddPass(device, &uploadDef, &renderer->upload) != mrhi_success ||
        mrhiAddPass(device, &drawDef, &renderer->draw) != mrhi_success)
    {
        return mui_errorPlatform;
    }
    renderer->frame[0] = 2.0f / (float)target->width;
    renderer->frame[1] = 2.0f / (float)target->height;
    renderer->frame[2] = list->header.scale;
    renderer->frame[3] = 0.0f;
    renderer->added = true;
    return mui_success;
}

static bool Upload(mrhiDevice* device, mrhiPassId pass, const Stream* stream)
{
    uint64_t bytes = (uint64_t)stream->count * stream->stride;
    return bytes == 0 || mrhiWriteBuffer(device, pass, stream->resource, 0, stream->staging,
                                         bytes) == mrhi_success;
}

static bool Draw(muiRhiRenderer* renderer)
{
    mrhiDevice* device = renderer->device;
    mrhiPassId pass = renderer->draw;
    const mrhiBinding bindings[2] = {
        {.slot = 0, .resource = renderer->instances.resource, .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = renderer->gradients.resource, .size = MRHI_WHOLE_SIZE},
    };
    return mrhiSetGraphicsPipeline(device, pass, renderer->pipeline) == mrhi_success &&
           mrhiSetBindings(device, pass, 0, bindings, 2) == mrhi_success &&
           mrhiSetRootBlock(device, pass, 0, renderer->frame, sizeof(renderer->frame)) ==
               mrhi_success &&
           mrhiDraw(device, pass, 6, renderer->instances.count, 0, 0) == mrhi_success;
}

muiResult muiRhiRenderer_Record(muiRhiRenderer* renderer)
{
    if (renderer == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!renderer->added)
    {
        return mui_success;
    }
    renderer->added = false;
    mrhiDevice* device = renderer->device;
    bool recorded = mrhiBeginPass(device, renderer->upload) == mrhi_success &&
                    Upload(device, renderer->upload, &renderer->instances) &&
                    Upload(device, renderer->upload, &renderer->gradients) &&
                    mrhiEndPass(device, renderer->upload) == mrhi_success &&
                    mrhiBeginPass(device, renderer->draw) == mrhi_success &&
                    (renderer->instances.count == 0 || Draw(renderer)) &&
                    mrhiEndPass(device, renderer->draw) == mrhi_success;
    return recorded ? mui_success : mui_errorPlatform;
}
