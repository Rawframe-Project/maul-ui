// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer (record mui-0005): a draw list's commands
// packed into instance records and its gradient, transform and clip
// tables beside them (pack.c), each a stream bound at its own slot,
// uploaded in a pass of their own each frame, and drawn as six vertices
// an instance by one pipeline whose shaders evaluate each
// (rhi/shaders/quad.*), in draws split where the texture an image or a
// glyph samples changes (plan.c), the texture and a linear sampler bound
// in table 1; glyphs come from an atlas of the renderer's own (glyphs.c). It
// allocates through the public allocator alone, as Maul UI's internals
// stay inside a shared build.

#include "maul-ui-rhi/renderer.h"

#include "allocator.h"
#include "cull.h"
#include "glyphs.h"
#include "images.h"
#include "pack.h"
#include "plan.h"
#include "streams.h"

#include "../shaders/quad_container.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"

#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <string.h>

#define DEF_COOKIE 0x6D757268u // "murh"

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
    muiRhiStream streams[kStreamCount];
    muiRhiImages images;
    muiRhiGlyphs glyphs;
    muiRhiCull cull;
    muiRhiPlan plan;
    mrhiSamplerId sampler;
    // Bound where a list has no images, never sampled.
    mrhiTextureId placeholder;
    // The passes added this frame, and what the draw needs.
    bool added;
    mrhiPassId upload;
    mrhiPassId draw;
    float frame[4];
    // The transform record the frame's projection starts at; 0 for none.
    uint32_t view;
    // The device's frameUploadBytes, as the def gave it.
    uint64_t uploadBytes;
};

muiRhiRendererDef muiDefaultRhiRendererDef(void)
{
    return (muiRhiRendererDef){
        .cookie = DEF_COOKIE,
        .targetFormat = mrhi_formatRgba8UnormSrgb,
        .instances = 1024,
        .uploadBytes = 1u << 20,
    };
}

static bool IsValid(const muiRhiRendererDef* def)
{
    return def->cookie == DEF_COOKIE && muiRhiIsAllocatorValid(&def->allocator) &&
           def->device != nullptr && def->instances != 0 && def->instances <= (1u << 24) &&
           def->uploadBytes != 0;
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
        .cull = {.allocator = def->allocator},
        .uploadBytes = def->uploadBytes,
        .plan = {.allocator = def->allocator},
    };
    muiResult status = muiRhiMakeGlyphs(&renderer->glyphs, &def->allocator, def->device, def->text);
    for (uint32_t i = 0; i < kStreamCount && status == mui_success; i++)
    {
        status = muiRhiReserveStream(renderer->device, &renderer->allocator, &renderer->streams[i],
                                     0, i == kInstances ? def->instances : 16);
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
        muiRhiDropStream(device, &renderer->allocator, &renderer->streams[i]);
    }
    muiRhiFreeImages(&renderer->images);
    muiRhiFreeGlyphs(&renderer->glyphs);
    muiRhiFreeCull(&renderer->cull);
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
// found and imported into the frame, its glyphs packed and their pages
// imported.
static muiResult Pack(muiRhiRenderer* renderer, const muiDrawList* list, const muiRhiTarget* target)
{
    muiRhiStream* streams = renderer->streams;
    muiResult reset = muiRhiResetImages(&renderer->images, muiRhiCountImages(list));
    // A projected list's bounds on the target are its clips' alone.
    const muiRhiBounds unbounded = {-INFINITY, -INFINITY, INFINITY, INFINITY};
    reset = reset != mui_success ? reset
            : target->projected
                ? muiRhiPrepareCullWithin(&renderer->cull, list, unbounded)
                : muiRhiPrepareCull(&renderer->cull, list, target->width, target->height);
    if (reset != mui_success)
    {
        return reset;
    }
    const uint32_t counts[kStreamCount] = {
        [kInstances] = muiRhiCountInstances(list),
        [kGradients] = muiRhiPackGradients(list, nullptr),
        [kTransforms] = muiRhiPackTransforms(list, nullptr) + (target->projected ? 2u : 0u),
        [kClips] = muiRhiPackClips(list, nullptr),
    };
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        muiResult status =
            muiRhiReserveStream(renderer->device, &renderer->allocator, &streams[i], counts[i], 16);
        if (status != mui_success)
        {
            return status;
        }
    }
    muiRhiNextGlyphFrame(&renderer->glyphs);
    const muiRhiPacking packing = {&renderer->images, &renderer->glyphs, &renderer->cull,
                                   target->projected};
    streams[kInstances].count = muiRhiPackInstances(list, &packing, streams[kInstances].staging);
    muiRhiListTextures(&renderer->images);
    muiResult prepared = muiRhiPrepareGlyphs(&renderer->glyphs);
    if (prepared != mui_success)
    {
        return prepared;
    }
    (void)muiRhiPackGradients(list, streams[kGradients].staging);
    uint32_t transforms = muiRhiPackTransforms(list, streams[kTransforms].staging);
    if (target->projected)
    {
        muiRhiPackProjection(target->projection,
                             (muiRhiTransform*)streams[kTransforms].staging + transforms);
    }
    renderer->view = target->projected ? transforms : 0u;
    (void)muiRhiPackClips(list, streams[kClips].staging);
    return mui_success;
}

// The runs of the streams' records that differ from those their buffers
// hold, none where no instance reads them, and the bytes they and the
// glyphs' changed rectangles take of the frame's uploads.
static uint64_t Diff(muiRhiRenderer* renderer)
{
    bool drawn = renderer->streams[kInstances].count > 0;
    uint64_t bytes = muiRhiGlyphUploadBytes(&renderer->glyphs);
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        muiRhiStream* stream = &renderer->streams[i];
        stream->runCount = 0;
        if (drawn)
        {
            muiRhiDiffStream(stream);
            bytes += muiRhiStreamUploadBytes(stream);
        }
    }
    return bytes;
}

static bool Upload(const muiRhiRenderer* renderer)
{
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        if (!muiRhiWriteStream(renderer->device, renderer->upload, &renderer->streams[i]))
        {
            return false;
        }
    }
    return muiRhiWriteGlyphs(&renderer->glyphs, renderer->upload);
}

// After the uploads are recorded: the buffers hold the frame's records
// and the glyphs' rectangles are written.
static void Keep(muiRhiRenderer* renderer)
{
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        if (renderer->streams[i].runCount > 0)
        {
            muiRhiKeepStream(&renderer->streams[i]);
        }
    }
    muiRhiGlyphsWritten(&renderer->glyphs);
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
    muiResult status = Pack(renderer, list, target);
    if (status != mui_success)
    {
        return status;
    }
    mrhiDevice* device = renderer->device;
    mrhiResourceId buffers[kStreamCount];
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        muiRhiStream* stream = &renderer->streams[i];
        if (mrhiImportBuffer(device, stream->buffer, &stream->resource) != mrhi_success)
        {
            return mui_errorPlatform;
        }
        buffers[i] = stream->resource;
    }
    const muiRhiStream* instances = &renderer->streams[kInstances];
    mrhiResourceId placeholder = {0};
    if (instances->count > 0 && renderer->images.textureCount == 0 &&
        renderer->glyphs.pageCount == 0 &&
        mrhiImportTexture(device, renderer->placeholder, &placeholder) != mrhi_success)
    {
        return mui_errorPlatform;
    }
    // A frame past the device's uploads is refused before any pass.
    if (Diff(renderer) > renderer->uploadBytes)
    {
        return mui_errorCapacity;
    }
    const muiRhiSources sources = {buffers, kStreamCount, &renderer->images, &renderer->glyphs,
                                   placeholder};
    status = muiRhiMakePlan(&renderer->plan, instances->staging, instances->count, &sources);
    if (status != mui_success)
    {
        return status;
    }
    mrhiPassDef uploadDef = mrhiDefaultPassDef();
    uploadDef.passClass = mrhi_passTransfer;
    uploadDef.accesses = renderer->plan.uploads;
    uploadDef.accessCount = renderer->plan.uploadCount;
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
    // The projection's first record, 0 for none (record 0 is the
    // identity transform).
    renderer->frame[3] = (float)renderer->view;
    renderer->added = true;
    return mui_success;
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
    if (!recorded)
    {
        return mui_errorPlatform;
    }
    Keep(renderer);
    return mui_success;
}

void muiRhiRenderer_Forget(muiRhiRenderer* renderer)
{
    if (renderer == nullptr)
    {
        return;
    }
    for (uint32_t i = 0; i < kStreamCount; i++)
    {
        renderer->streams[i].heldCount = 0;
    }
    muiRhiForgetGlyphs(&renderer->glyphs);
}
