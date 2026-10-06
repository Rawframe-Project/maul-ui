// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer (record mui-0005): a draw list's commands packed into
// instance records, uploaded in a pass of their own each frame and drawn
// as six vertices an instance by one pipeline whose fragment shader
// evaluates each (rhi/shaders/quad.*). It allocates through the public
// allocator alone, as Maul UI's internals stay inside a shared build. The records hold what the
// shader reads, in its layout.

#include "maul-ui-rhi/renderer.h"

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

// An instance as shaders/quad.vert declares it (std430).
typedef struct Instance
{
    muiRect rect;
    muiCorners radii;
    muiLinearColor fill;
    muiSides widths;
    muiLinearColor colors[4];
    uint32_t kind;
    uint32_t clip;
    uint32_t transform;
    uint32_t gradient;
} Instance;

static_assert(sizeof(Instance) == 144, "an instance is the shader's 144 bytes");

struct muiRhiRenderer
{
    muiAllocator allocator;
    mrhiDevice* device;
    mrhiShaderId shader;
    mrhiGraphicsPipelineId pipeline;
    mrhiRequestId request;
    bool ready;
    // The instance buffer and the records packed for it.
    mrhiBufferId buffer;
    Instance* instances;
    uint32_t capacity;
    uint32_t count;
    // The passes added this frame, and what the draw needs.
    bool added;
    mrhiPassId upload;
    mrhiPassId draw;
    mrhiResourceId bufferResource;
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

// The instance buffer and its staging at a capacity, replacing the old.
static muiResult MakeBuffer(muiRhiRenderer* renderer, uint32_t capacity)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = (uint64_t)capacity * sizeof(Instance);
    def.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    Instance* instances =
        Allocate(&renderer->allocator, (size_t)capacity * sizeof(Instance), alignof(Instance));
    if (instances == nullptr)
    {
        return mui_errorCapacity;
    }
    mrhiBufferId buffer = {0};
    if (mrhiCreateBuffer(renderer->device, &def, &buffer) != mrhi_success)
    {
        Release(&renderer->allocator, instances, (size_t)capacity * sizeof(Instance),
                alignof(Instance));
        return mui_errorPlatform;
    }
    if (renderer->instances != nullptr)
    {
        (void)mrhiDestroyBuffer(renderer->device, renderer->buffer);
        Release(&renderer->allocator, renderer->instances,
                (size_t)renderer->capacity * sizeof(Instance), alignof(Instance));
    }
    renderer->buffer = buffer;
    renderer->instances = instances;
    renderer->capacity = capacity;
    return mui_success;
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
    *renderer = (muiRhiRenderer){.allocator = def->allocator, .device = def->device};
    muiResult status = MakeBuffer(renderer, def->instances);
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
    if (renderer->instances != nullptr)
    {
        (void)mrhiDestroyBuffer(device, renderer->buffer);
        Release(&renderer->allocator, renderer->instances,
                (size_t)renderer->capacity * sizeof(Instance), alignof(Instance));
    }
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

// A box command as an instance.
static Instance BoxOf(const muiDrawCommand* command)
{
    const muiDrawBox* box = &command->box;
    Instance instance = {
        .rect = box->rect,
        .radii = box->radii,
        .fill = box->fill,
        .widths = box->borderWidths,
        .kind = mui_drawBox,
        .clip = command->clip,
        .transform = command->transform,
        .gradient = box->gradient,
    };
    memcpy(instance.colors, box->borderColors, sizeof(instance.colors));
    return instance;
}

// The list's commands as instances, the buffer grown to hold them.
static muiResult Pack(muiRhiRenderer* renderer, const muiDrawList* list)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        count += list->commands[i].kind == mui_drawBox ? 1 : 0;
    }
    uint32_t capacity = renderer->capacity;
    while (capacity < count && capacity <= (1u << 23))
    {
        capacity *= 2;
    }
    if (capacity < count)
    {
        return mui_errorCapacity;
    }
    muiResult status =
        capacity != renderer->capacity ? MakeBuffer(renderer, capacity) : mui_success;
    if (status != mui_success)
    {
        return status;
    }
    renderer->count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        if (list->commands[i].kind == mui_drawBox)
        {
            renderer->instances[renderer->count++] = BoxOf(&list->commands[i]);
        }
    }
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
    if (mrhiImportBuffer(device, renderer->buffer, &renderer->bufferResource) != mrhi_success)
    {
        return mui_errorPlatform;
    }
    const mrhiAccess write = Whole(renderer->bufferResource, mrhi_accessCopyDestination);
    mrhiPassDef uploadDef = mrhiDefaultPassDef();
    uploadDef.passClass = mrhi_passTransfer;
    uploadDef.accesses = &write;
    uploadDef.accessCount = 1;
    const mrhiAccess read = Whole(renderer->bufferResource, mrhi_accessStorageRead);
    const muiLinearColor clear = target->clearColor;
    mrhiPassDef drawDef = mrhiDefaultPassDef();
    drawDef.colorTargets[0] = (mrhiColorTarget){
        .resource = target->resource,
        .load = target->clear ? mrhi_loadClear : mrhi_loadKeep,
        .store = mrhi_storeKeep,
        .clear = {clear.r, clear.g, clear.b, clear.a},
    };
    drawDef.colorTargetCount = 1;
    drawDef.accesses = &read;
    drawDef.accessCount = 1;
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
    uint64_t bytes = (uint64_t)renderer->count * sizeof(Instance);
    const mrhiBinding binding = {
        .slot = 0, .resource = renderer->bufferResource, .size = MRHI_WHOLE_SIZE};
    bool recorded =
        mrhiBeginPass(device, renderer->upload) == mrhi_success &&
        (bytes == 0 || mrhiWriteBuffer(device, renderer->upload, renderer->bufferResource, 0,
                                       renderer->instances, bytes) == mrhi_success) &&
        mrhiEndPass(device, renderer->upload) == mrhi_success &&
        mrhiBeginPass(device, renderer->draw) == mrhi_success &&
        (renderer->count == 0 ||
         (mrhiSetGraphicsPipeline(device, renderer->draw, renderer->pipeline) == mrhi_success &&
          mrhiSetBindings(device, renderer->draw, 0, &binding, 1) == mrhi_success &&
          mrhiSetRootBlock(device, renderer->draw, 0, renderer->frame, sizeof(renderer->frame)) ==
              mrhi_success &&
          mrhiDraw(device, renderer->draw, 6, renderer->count, 0, 0) == mrhi_success)) &&
        mrhiEndPass(device, renderer->draw) == mrhi_success;
    return recorded ? mui_success : mui_errorPlatform;
}
