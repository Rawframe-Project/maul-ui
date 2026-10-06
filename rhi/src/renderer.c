// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer (record mui-0005): a draw list's commands
// packed into instance records and its gradient, transform and clip
// tables beside them (pack.c), each a stream bound at its own slot,
// uploaded in a pass of their own each frame, and drawn as six vertices
// an instance by one pipeline whose shaders evaluate each
// (rhi/shaders/quad.*), in draws split where an image's texture changes
// (plan.c), the texture and a linear sampler bound in table 1. It
// allocates through the public allocator alone, as Maul UI's internals
// stay inside a shared build.

#include "maul-ui-rhi/renderer.h"

#include "allocator.h"
#include "images.h"
#include "pack.h"
#include "plan.h"

#include "../shaders/quad_container.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"

#include <stdalign.h>
#include <stddef.h>
#include <string.h>

#define DEF_COOKIE 0x6D757268u // "murh"

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

// The streams, each bound at the slot of its index.
enum
{
    kInstances,
    kGradients,
    kTransforms,
    kClips,
    kStreamCount
};

struct muiRhiRenderer
{
    muiAllocator allocator;
    mrhiDevice* device;
    mrhiShaderId shader;
    mrhiGraphicsPipelineId pipeline;
    mrhiRequestId request;
    bool ready;
    Stream streams[kStreamCount];
    muiRhiImages images;
    muiRhiPlan plan;
    mrhiSamplerId sampler;
    // Bound where a list has no images, never sampled.
    mrhiTextureId placeholder;
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
    return def->cookie == DEF_COOKIE && muiRhiIsAllocatorValid(&def->allocator) &&
           def->device != nullptr && def->instances != 0 && def->instances <= (1u << 24);
}

static void DropStream(muiRhiRenderer* renderer, Stream* stream)
{
    if (stream->staging != nullptr)
    {
        (void)mrhiDestroyBuffer(renderer->device, stream->buffer);
        muiRhiRelease(&renderer->allocator, stream->staging,
                      (size_t)stream->capacity * stream->stride, alignof(max_align_t));
        stream->staging = nullptr;
    }
}

// A stream's buffer and staging at a capacity, replacing the old.
static muiResult GrowStream(muiRhiRenderer* renderer, Stream* stream, uint32_t capacity)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = (uint64_t)capacity * stream->stride;
    def.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    void* staging = muiRhiAllocate(&renderer->allocator, (size_t)capacity * stream->stride,
                                   alignof(max_align_t));
    if (staging == nullptr)
    {
        return mui_errorCapacity;
    }
    mrhiBufferId buffer = {0};
    if (mrhiCreateBuffer(renderer->device, &def, &buffer) != mrhi_success)
    {
        muiRhiRelease(&renderer->allocator, staging, (size_t)capacity * stream->stride,
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

// The sampler of images and the placeholder texture.
static bool MakeTextures(muiRhiRenderer* renderer)
{
    mrhiSamplerDef samplerDef = mrhiDefaultSamplerDef();
    samplerDef.magFilter = mrhi_filterLinear;
    samplerDef.minFilter = mrhi_filterLinear;
    samplerDef.mipFilter = mrhi_filterLinear;
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 1;
    textureDef.height = 1;
    textureDef.usage = mrhi_textureSampled;
    return mrhiCreateSampler(renderer->device, &samplerDef, &renderer->sampler) == mrhi_success &&
           mrhiCreateTexture(renderer->device, &textureDef, &renderer->placeholder) == mrhi_success;
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
        muiRhiAllocate(&def->allocator, sizeof(muiRhiRenderer), alignof(muiRhiRenderer));
    if (renderer == nullptr)
    {
        return mui_errorCapacity;
    }
    *renderer = (muiRhiRenderer){
        .allocator = def->allocator,
        .device = def->device,
        .streams =
            {
                [kInstances] = {.stride = sizeof(muiRhiInstance)},
                [kGradients] = {.stride = sizeof(muiRhiGradient)},
                [kTransforms] = {.stride = sizeof(muiRhiTransform)},
                [kClips] = {.stride = sizeof(muiRhiClip)},
            },
        .images = muiRhiMakeImages(&def->allocator, def->device, def->image, def->imageContext),
        .plan = {.allocator = def->allocator},
    };
    muiResult status = mui_success;
    for (uint32_t i = 0; i < kStreamCount && status == mui_success; i++)
    {
        status = GrowStream(renderer, &renderer->streams[i], i == kInstances ? def->instances : 16);
    }
    if (status == mui_success &&
        (!MakeTextures(renderer) || !MakePipeline(renderer, def->targetFormat)))
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
    if (renderer->placeholder.index1 != 0)
    {
        (void)mrhiDestroyTexture(device, renderer->placeholder);
    }
    if (renderer->sampler.index1 != 0)
    {
        (void)mrhiDestroySampler(device, renderer->sampler);
    }
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        DropStream(renderer, &renderer->streams[i]);
    }
    muiRhiFreeImages(&renderer->images);
    muiRhiFreePlan(&renderer->plan);
    const muiAllocator allocator = renderer->allocator;
    muiRhiRelease(&allocator, renderer, sizeof(muiRhiRenderer), alignof(muiRhiRenderer));
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

// The list's instances and tables into the streams' staging, its images
// found and imported into the frame.
static muiResult Pack(muiRhiRenderer* renderer, const muiDrawList* list)
{
    Stream* streams = renderer->streams;
    muiResult reset = muiRhiResetImages(&renderer->images, muiRhiCountImages(list));
    if (reset != mui_success)
    {
        return reset;
    }
    const uint32_t counts[kStreamCount] = {
        [kInstances] = muiRhiCountInstances(list),
        [kGradients] = muiRhiPackGradients(list, nullptr),
        [kTransforms] = muiRhiPackTransforms(list, nullptr),
        [kClips] = muiRhiPackClips(list, nullptr),
    };
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        muiResult status = Reserve(renderer, &streams[i], counts[i]);
        if (status != mui_success)
        {
            return status;
        }
    }
    streams[kInstances].count =
        muiRhiPackInstances(list, &renderer->images, streams[kInstances].staging);
    muiRhiListTextures(&renderer->images);
    (void)muiRhiPackGradients(list, streams[kGradients].staging);
    (void)muiRhiPackTransforms(list, streams[kTransforms].staging);
    (void)muiRhiPackClips(list, streams[kClips].staging);
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
    mrhiAccess writes[kStreamCount];
    mrhiResourceId buffers[kStreamCount];
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        Stream* stream = &renderer->streams[i];
        if (mrhiImportBuffer(device, stream->buffer, &stream->resource) != mrhi_success)
        {
            return mui_errorPlatform;
        }
        writes[i] = Whole(stream->resource, mrhi_accessCopyDestination);
        buffers[i] = stream->resource;
    }
    const Stream* instances = &renderer->streams[kInstances];
    mrhiResourceId placeholder = {0};
    if (instances->count > 0 && renderer->images.textureCount == 0 &&
        mrhiImportTexture(device, renderer->placeholder, &placeholder) != mrhi_success)
    {
        return mui_errorPlatform;
    }
    status = muiRhiMakePlan(&renderer->plan, instances->staging, instances->count,
                            &renderer->images, buffers, kStreamCount, placeholder);
    if (status != mui_success)
    {
        return status;
    }
    mrhiPassDef uploadDef = mrhiDefaultPassDef();
    uploadDef.passClass = mrhi_passTransfer;
    uploadDef.accesses = writes;
    uploadDef.accessCount = kStreamCount;
    const muiLinearColor clear = target->clearColor;
    mrhiPassDef drawDef = mrhiDefaultPassDef();
    drawDef.colorTargets[0] = (mrhiColorTarget){
        .resource = target->resource,
        .load = target->clear ? mrhi_loadClear : mrhi_loadKeep,
        .store = mrhi_storeKeep,
        .clear = {clear.r, clear.g, clear.b, clear.a},
    };
    drawDef.colorTargetCount = 1;
    drawDef.accesses = renderer->plan.accesses;
    drawDef.accessCount = renderer->plan.accessCount;
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

// The streams' records, none where no instance reads them.
static bool Upload(const muiRhiRenderer* renderer)
{
    uint32_t streams = renderer->streams[kInstances].count > 0 ? kStreamCount : 0;
    for (uint32_t i = 0; i < streams; i++)
    {
        const Stream* stream = &renderer->streams[i];
        uint64_t bytes = (uint64_t)stream->count * stream->stride;
        if (bytes != 0 && mrhiWriteBuffer(renderer->device, renderer->upload, stream->resource, 0,
                                          stream->staging, bytes) != mrhi_success)
        {
            return false;
        }
    }
    return true;
}

// Binds a draw's texture and the sampler in table 1, and draws it.
static bool DrawOne(const muiRhiRenderer* renderer, const muiRhiDraw* draw)
{
    const mrhiBinding bindings[2] = {
        {.slot = 0,
         .resource = draw->texture,
         .viewKind = mrhi_texture2d,
         .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING}},
        {.slot = 1, .sampler = renderer->sampler},
    };
    return mrhiSetBindings(renderer->device, renderer->draw, 1, bindings, 2) == mrhi_success &&
           mrhiDraw(renderer->device, renderer->draw, 6, draw->count, 0, draw->first) ==
               mrhi_success;
}

static bool Draw(muiRhiRenderer* renderer)
{
    mrhiDevice* device = renderer->device;
    mrhiPassId pass = renderer->draw;
    mrhiBinding bindings[kStreamCount];
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        bindings[i] = (mrhiBinding){
            .slot = i, .resource = renderer->streams[i].resource, .size = MRHI_WHOLE_SIZE};
    }
    if (mrhiSetGraphicsPipeline(device, pass, renderer->pipeline) != mrhi_success ||
        mrhiSetBindings(device, pass, 0, bindings, kStreamCount) != mrhi_success ||
        mrhiSetRootBlock(device, pass, 0, renderer->frame, sizeof(renderer->frame)) != mrhi_success)
    {
        return false;
    }
    for (uint32_t i = 0; i < renderer->plan.drawCount; i++)
    {
        if (!DrawOne(renderer, &renderer->plan.draws[i]))
        {
            return false;
        }
    }
    return true;
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
    bool recorded = mrhiBeginPass(device, renderer->upload) == mrhi_success && Upload(renderer) &&
                    mrhiEndPass(device, renderer->upload) == mrhi_success &&
                    mrhiBeginPass(device, renderer->draw) == mrhi_success &&
                    (renderer->streams[kInstances].count == 0 || Draw(renderer)) &&
                    mrhiEndPass(device, renderer->draw) == mrhi_success;
    return recorded ? mui_success : mui_errorPlatform;
}
