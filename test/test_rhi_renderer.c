// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer on Maul RHI's test driver, without a GPU: its
// def and arguments; not drawing until its pipeline is ready, the
// device's notification making it so, or a failed pipeline leaving it
// not; a frame's upload and draw passes, the upload a record of 144
// bytes a box, shadow or image and nothing for the kinds it does not
// draw yet, and the gradient, transform and clip tables beside them; the
// instance buffer grown for a long list; an empty list; a frame
// uploading only the records that changed, everything after it is
// forgotten, and a frame past the upload budget refused; images, the
// host asked once a key a frame, a key it has no image for not drawn,
// and a draw a texture, keys of one texture drawn together; glyphs, their
// texels uploaded the frame they are packed and not again.

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/test.h"
#include "maul-ui-rhi/renderer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SIZE 64

static mrhiTestFrameLog s_log;
static mrhiTestAdapter s_adapter;
static mrhiTestDriverDef s_driver;

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// A ready device on the test driver, its pipelines answered with an
// outcome.
// The upload budget a frame of the next device Open makes; 0 for Maul
// RHI's default.
static uint32_t s_uploadBudget = 0;

static bool Open(Gpu* gpu, mrhiResult pipelines)
{
    *gpu = (Gpu){0};
    s_log = (mrhiTestFrameLog){0};
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
        .pipelineOutcome = pipelines,
        .frameLog = &s_log,
    };
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = NULL, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &gpu->instance) != mrhi_success ||
        mrhiRequestAdapters(gpu->instance, &search, &request) != mrhi_success ||
        mrhiNextInstanceNotification(gpu->instance, &record) != mrhi_success ||
        mrhiGetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count != 1)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    if (s_uploadBudget != 0)
    {
        deviceDef.deviceLimits.frameUploadBytes = s_uploadBudget;
    }
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success &&
           mrhiNextInstanceNotification(gpu->instance, &record) == mrhi_success &&
           record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success;
}

static void Close(Gpu* gpu)
{
    mrhiDestroyDevice(gpu->device);
    mrhiDestroyInstance(gpu->instance);
}

// Hands the renderer every notification the device has; how many were
// its own.
static int Pump(Gpu* gpu, muiRhiRenderer* renderer)
{
    int its = 0;
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
    {
        its += muiRhiRenderer_Notify(renderer, &record) ? 1 : 0;
    }
    return its;
}

// The renderer's memory: allocation failAt fails (from 1, 0 for none),
// and the bytes live are counted. Blocks are aligned by hand, as not
// every C library has aligned_alloc.
typedef struct Memory
{
    uint32_t allocations;
    uint32_t failAt;
    size_t live;
} Memory;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    Memory* memory = context;
    if (++memory->allocations == memory->failAt)
    {
        return NULL;
    }
    size_t room = alignment > sizeof(void*) ? alignment : sizeof(void*);
    unsigned char* raw = malloc(size + room + sizeof(void*));
    if (raw == NULL)
    {
        return NULL;
    }
    uintptr_t start = (uintptr_t)(raw + sizeof(void*));
    unsigned char* block = raw + sizeof(void*) + (room - start % room) % room;
    memcpy(block - sizeof(void*), &raw, sizeof raw);
    memory->live += size;
    return block;
}

static void Release(void* block, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    Memory* memory = context;
    void* raw = NULL;
    memcpy(&raw, (unsigned char*)block - sizeof(void*), sizeof raw);
    memory->live -= size;
    free(raw);
}

static muiRhiRenderer* Make(Gpu* gpu, uint32_t instances)
{
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu->device;
    def.instances = instances;
    muiRhiRenderer* renderer = NULL;
    return muiCreateRhiRenderer(&def, &renderer) == mui_success ? renderer : NULL;
}

static uint64_t Blocks(uint64_t bytes)
{
    return (bytes + 511) / 512 * 512;
}

// The staging bytes a frame's uploads take, each in whole 512-byte
// blocks of Maul RHI's staging: its instances, then its gradient,
// transform and clip tables, each of one entry for these lists.
static uint64_t Staged(uint64_t instances)
{
    return Blocks(instances * 144) + Blocks(112) + Blocks(32) + Blocks(48);
}

static muiDrawCommand Box(float x, float y)
{
    muiDrawCommand command = {.kind = mui_drawBox};
    command.box.rect = (muiRect){x, y, 10.0f, 10.0f};
    command.box.fill = (muiLinearColor){0.5f, 0.0f, 0.0f, 0.5f};
    return command;
}

static muiDrawList ListOf(const muiDrawCommand* commands, uint32_t count)
{
    muiDrawList list = {.commands = commands, .commandCount = count};
    list.header.scale = 1.0f;
    return list;
}

// A frame drawing a list into a target the frame makes, which a pass of
// the test reads so that the drawing is kept.
static muiResult DrawFrame(Gpu* gpu, muiRhiRenderer* renderer, const muiDrawList* list)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8UnormSrgb;
    targetDef.width = SIZE;
    targetDef.height = SIZE;
    mrhiResourceId target = {0};
    if (mrhiBeginFrame(gpu->device, &frame) != mrhi_success ||
        mrhiDeclareTexture(gpu->device, &targetDef, &target) != mrhi_success)
    {
        return mui_errorPlatform;
    }
    const muiRhiTarget into = {.resource = target, .width = SIZE, .height = SIZE, .clear = true};
    muiResult added = muiRhiRenderer_AddPasses(renderer, list, &into);
    if (added != mui_success)
    {
        // Nothing writes the target: the frame is dropped.
        (void)mrhiDropFrame(gpu->device);
        (void)Pump(gpu, renderer);
        return added;
    }
    const mrhiAccess read = {.resource = target,
                             .kind = mrhi_accessCopySource,
                             .range = {.mipCount = 1, .layerCount = 1}};
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    mrhiRequestId token = {0};
    bool done = mrhiAddPass(gpu->device, &readDef, &reading) == mrhi_success &&
                mrhiCompileFrame(gpu->device) == mrhi_success &&
                muiRhiRenderer_Record(renderer) == mui_success &&
                mrhiBeginPass(gpu->device, reading) == mrhi_success &&
                mrhiEndPass(gpu->device, reading) == mrhi_success &&
                mrhiSubmitFrame(gpu->device, &token) == mrhi_success;
    if (!done)
    {
        (void)mrhiDropFrame(gpu->device);
        return mui_errorPlatform;
    }
    (void)Pump(gpu, renderer);
    return added;
}

static void TestContract(void)
{
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    muiRhiRenderer* renderer = NULL;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_errorInvalid && renderer == NULL,
          "no device");
    def.cookie = 0;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_errorInvalid, "a def not from the default");
    CHECK(muiCreateRhiRenderer(NULL, &renderer) == mui_errorInvalid, "no def");
    CHECK(muiRhiRenderer_AddPasses(NULL, NULL, NULL) == mui_errorInvalid &&
              muiRhiRenderer_Record(NULL) == mui_errorInvalid && !muiRhiRenderer_IsReady(NULL) &&
              !muiRhiRenderer_Notify(NULL, NULL) &&
              muiRhiRenderer_GetPipelineRequest(NULL).index1 == 0,
          "NULL arguments");
    muiDestroyRhiRenderer(NULL);
}

static void TestDraws(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success), "a device");
    muiRhiRenderer* renderer = Make(&gpu, 4);
    CHECK(renderer != NULL && !muiRhiRenderer_IsReady(renderer) &&
              muiRhiRenderer_GetPipelineRequest(renderer).index1 != 0,
          "made, its pipeline pending");
    if (renderer == NULL)
    {
        Close(&gpu);
        return;
    }
    muiDrawCommand commands[3] = {Box(0, 0), Box(20, 20), {.kind = mui_drawImage}};
    muiDrawList list = ListOf(commands, 3);
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_empty && s_log.frames == 0,
          "nothing drawn before its pipeline is ready");
    CHECK(muiRhiRenderer_IsReady(renderer), "the device's notification made it ready");
    // The four uploads, then the pipeline, table 0, the root block, and
    // table 1 (the placeholder) and the draw.
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.passes == 3 &&
              s_log.stagingBytes == Staged(2) && s_log.commands == 9,
          "upload and draw passes, two boxes uploaded, an image with no host not drawn");
    const muiRhiTarget empty = {.width = 0, .height = SIZE};
    CHECK(muiRhiRenderer_AddPasses(renderer, &list, &empty) == mui_errorInvalid,
          "a target of no size");
    // A list longer than the buffer holds: the buffer grows.
    muiDrawCommand* many = malloc(3000 * sizeof(muiDrawCommand));
    for (uint32_t i = 0; many != NULL && i < 3000; i++)
    {
        many[i] = Box((float)(i % 60), (float)(i / 60));
    }
    muiDrawList longList = ListOf(many, many != NULL ? 3000 : 0);
    // The buffer made anew holds nothing; the tables did not change.
    CHECK(many != NULL && DrawFrame(&gpu, renderer, &longList) == mui_success &&
              s_log.stagingBytes == Blocks(3000 * 144),
          "the buffer grown for 3000 boxes, uploaded whole");
    free(many);
    muiDrawList none = ListOf(NULL, 0);
    CHECK(DrawFrame(&gpu, renderer, &none) == mui_success && s_log.passes == 3 &&
              s_log.stagingBytes == 0 && s_log.commands == 0,
          "an empty list still clears");
    muiDestroyRhiRenderer(renderer);
    Close(&gpu);
}

// The host's images: keys 1 and 2 name texture A, 3 names B, 5 names A
// with no size, others nothing; it counts its calls.
typedef struct Host
{
    mrhiTextureId a;
    mrhiTextureId b;
    int calls;
} Host;

static bool FindImage(void* context, uint64_t key, muiRhiImage* imageOut)
{
    Host* host = context;
    host->calls++;
    if (key < 1 || key > 5 || key == 4)
    {
        return false;
    }
    uint32_t width = key == 5 ? 0 : 8;
    *imageOut = (muiRhiImage){.texture = key == 3 ? host->b : host->a, .width = width, .height = 8};
    return true;
}

static muiDrawCommand Image(uint64_t key)
{
    muiDrawCommand command = {.kind = mui_drawImage};
    command.image.rect = (muiRect){0, 0, 8, 8};
    command.image.image = key;
    command.image.uv = (muiRect){0, 0, 1, 1};
    command.image.tint = (muiLinearColor){1, 1, 1, 1};
    return command;
}

static void TestImages(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success), "a device");
    Host host = {0};
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8UnormSrgb;
    textureDef.width = 8;
    textureDef.height = 8;
    textureDef.usage = mrhi_textureSampled;
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    def.image = FindImage;
    def.imageContext = &host;
    muiRhiRenderer* renderer = NULL;
    CHECK(mrhiCreateTexture(gpu.device, &textureDef, &host.a) == mrhi_success &&
              mrhiCreateTexture(gpu.device, &textureDef, &host.b) == mrhi_success &&
              muiCreateRhiRenderer(&def, &renderer) == mui_success,
          "two textures and a renderer");
    muiDrawCommand box = Box(0, 0);
    muiDrawList one = ListOf(&box, 1);
    CHECK(DrawFrame(&gpu, renderer, &one) == mui_empty && muiRhiRenderer_IsReady(renderer),
          "ready");
    const muiDrawCommand commands[8] = {Box(0, 0), Image(1), Image(2), Box(0, 0),
                                        Image(3),  Image(1), Image(4), Image(4)};
    muiDrawList list = ListOf(commands, 8);
    // Six instances in three draws, A's, B's and A's: the uploads, the
    // pipeline, table 0, the root block, and table 1 and a draw each.
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == Staged(6) &&
              s_log.commands == 4 + 3 + 3 * 2 && host.calls == 4,
          "images in a draw a texture, the host asked once a key, key 4 not drawn");
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && host.calls == 8,
          "asked again the next frame");
    // An image of no size is not drawn: three boxes fill 432 of a
    // staging block, which a fourth instance would pass.
    const muiDrawCommand empty[4] = {Box(0, 0), Box(0, 0), Box(0, 0), Image(5)};
    muiDrawList none = ListOf(empty, 4);
    muiRhiRenderer_Forget(renderer);
    CHECK(DrawFrame(&gpu, renderer, &none) == mui_success && s_log.stagingBytes == Staged(3),
          "an image of no size not drawn");
    muiDestroyRhiRenderer(renderer);
    (void)mrhiDestroyTexture(gpu.device, host.a);
    (void)mrhiDestroyTexture(gpu.device, host.b);
    Close(&gpu);
}

// Fourteen host textures, keys 1 to 14.
typedef struct Many
{
    mrhiTextureId textures[14];
} Many;

static bool FindMany(void* context, uint64_t key, muiRhiImage* imageOut)
{
    const Many* many = context;
    if (key < 1 || key > 14)
    {
        return false;
    }
    *imageOut = (muiRhiImage){.texture = many->textures[key - 1], .width = 4, .height = 4};
    return true;
}

// Frames sampling more textures each, 8 up to 14, on one renderer: the
// plan's table of accesses (the streams, each texture, the target) grows
// past 16 entries on the way, and holds every one (the sanitizers check).
static void TestManyTextures(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success), "a device");
    Many many = {0};
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8UnormSrgb;
    textureDef.width = 4;
    textureDef.height = 4;
    textureDef.usage = mrhi_textureSampled;
    bool made = true;
    for (uint32_t i = 0; i < 14; i++)
    {
        made =
            made && mrhiCreateTexture(gpu.device, &textureDef, &many.textures[i]) == mrhi_success;
    }
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    def.image = FindMany;
    def.imageContext = &many;
    muiRhiRenderer* renderer = NULL;
    CHECK(made && muiCreateRhiRenderer(&def, &renderer) == mui_success,
          "fourteen textures and a renderer");
    muiDrawCommand box = Box(0, 0);
    muiDrawList one = ListOf(&box, 1);
    CHECK(DrawFrame(&gpu, renderer, &one) == mui_empty && muiRhiRenderer_IsReady(renderer),
          "ready");
    muiDrawCommand commands[14];
    bool drawn = true;
    for (uint32_t count = 8; count <= 14; count++)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            commands[i] = Image(i + 1);
        }
        muiDrawList list = ListOf(commands, count);
        drawn = drawn && DrawFrame(&gpu, renderer, &list) == mui_success;
    }
    CHECK(drawn, "8 to 14 textures a frame drawn");
    muiDestroyRhiRenderer(renderer);
    for (uint32_t i = 0; i < 14; i++)
    {
        (void)mrhiDestroyTexture(gpu.device, many.textures[i]);
    }
    Close(&gpu);
}

#if MUI_TEST_TEXT

#include "maul-ui/font.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"

#include "ahem.inc"

static void TestGlyphs(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success), "a device");
    // The service's memory counted: the renderer's atlases are its.
    Memory memory = {0};
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    serviceDef.allocator = (muiAllocator){Allocate, Release, &memory};
    muiTextService* service = NULL;
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = s_ahem;
    fontDef.size = sizeof s_ahem;
    fontDef.dataMode = mui_fontDataBorrow;
    muiFontId ahem = {0, 0};
    CHECK(muiCreateTextService(&serviceDef, &service) == mui_success &&
              muiCreateFont(service, &fontDef, &ahem) == mui_success,
          "a text service with Ahem");
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    def.text = service;
    muiRhiRenderer* renderer = NULL;
    // A renderer made and destroyed gives its atlases back.
    const size_t before = memory.live;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_success && memory.live > before,
          "a renderer's atlases from the service");
    muiDestroyRhiRenderer(renderer);
    CHECK(memory.live == before, "the renderer's atlases given back to the service");
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_success, "a renderer with text");
    muiDrawCommand box = Box(0, 0);
    muiDrawList one = ListOf(&box, 1);
    CHECK(DrawFrame(&gpu, renderer, &one) == mui_empty && muiRhiRenderer_IsReady(renderer),
          "ready");
    const muiGlyph glyphs[2] = {{4, 0, 0}, {5, 12, 0}};
    muiDrawCommand run = {.kind = mui_drawGlyphRun};
    run.glyphRun = (muiDrawGlyphRun){.font = muiFont_GetKey(ahem),
                                     .originX = 4,
                                     .originY = 20,
                                     .size = 10,
                                     .glyphCount = 2,
                                     .color = {1, 1, 1, 1}};
    muiDrawList list = ListOf(&run, 1);
    list.glyphs = glyphs;
    list.glyphCount = 2;
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes > Staged(2),
          "the glyphs' texels uploaded with their instances");
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == 0,
          "and not again the next frame");
    // A frame refused for its uploads keeps its new glyph's image to
    // upload: Ahem's glyph 6 with 8,000 boxes passes the default 1 MiB.
    const muiGlyph six = {6, 0, 0};
    muiDrawCommand* many = malloc(8001 * sizeof(muiDrawCommand));
    for (uint32_t i = 1; many != NULL && i < 8001; i++)
    {
        many[i] = Box((float)(i % 50), (float)(i / 200));
    }
    if (many != NULL)
    {
        many[0] = run;
        many[0].glyphRun.glyphCount = 1;
    }
    muiDrawList crowded = ListOf(many, many != NULL ? 8001 : 0);
    crowded.glyphs = &six;
    crowded.glyphCount = 1;
    muiDrawList alone = ListOf(many, many != NULL ? 1 : 0);
    alone.glyphs = &six;
    alone.glyphCount = 1;
    // The next frame: its one record in a block, and the glyph's 12
    // rows with their gutter at a 256-byte pitch.
    CHECK(many != NULL && DrawFrame(&gpu, renderer, &crowded) == mui_errorCapacity &&
              DrawFrame(&gpu, renderer, &alone) == mui_success &&
              s_log.stagingBytes == 512 + 12 * 256,
          "a glyph packed in a refused frame uploaded in the next");
    free(many);
    // A run past the glyph table is not drawn.
    run.glyphRun.firstGlyph = 1;
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == 0,
          "a run past the glyph table not drawn");
    muiDestroyRhiRenderer(renderer);
    muiDestroyTextService(service);
    CHECK(memory.live == 0, "the service's memory all given back");
    Close(&gpu);
}

#else

// Without Maul UI's text component, a text service is refused.
static void TestNoText(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success), "a device");
    int notService = 0;
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    def.text = (muiTextService*)&notService;
    muiRhiRenderer* renderer = NULL;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_errorInvalid && renderer == NULL,
          "a text service refused without text");
    Close(&gpu);
}

#endif

static void TestChanges(void)
{
    Gpu gpu;
    // A device whose frames upload four 512-byte blocks: the tables'
    // three and one of records. The renderer reads the budget from it.
    s_uploadBudget = 4 * 512;
    CHECK(Open(&gpu, mrhi_success), "a device");
    s_uploadBudget = 0;
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    muiRhiRenderer* renderer = NULL;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_success, "a renderer");
    muiDrawCommand commands[8];
    for (uint32_t i = 0; i < 8; i++)
    {
        commands[i] = Box((float)i, 0);
    }
    muiDrawList list = ListOf(commands, 3);
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_empty && muiRhiRenderer_IsReady(renderer),
          "ready");
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == Staged(3),
          "the first frame uploads all");
    // No writes: the pipeline, two tables, the root block and the draw.
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == 0 &&
              s_log.commands == 5,
          "the same list again uploads nothing and draws");
    commands[2].box.fill.r = 1.0f;
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == 512,
          "one box changed: one block");
    commands[0].box.fill.g = 1.0f;
    commands[2].box.fill.g = 1.0f;
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == 512,
          "two boxes changed a record apart: one run, one block");
    muiRhiRenderer_Forget(renderer);
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == Staged(3),
          "forgotten: all again");
    // Eight records take three blocks: past the budget with nothing
    // held of them.
    muiRhiRenderer_Forget(renderer);
    muiDrawList longer = ListOf(commands, 8);
    s_log = (mrhiTestFrameLog){0};
    CHECK(DrawFrame(&gpu, renderer, &longer) == mui_errorCapacity && s_log.frames == 0,
          "a frame past the upload budget refused, nothing added");
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == Staged(3),
          "the next frame within it drawn");
    muiDestroyRhiRenderer(renderer);
    Close(&gpu);
}

// A host sharing the frame's uploads gives the renderer a share of the
// device's budget, no more than the whole.
static void TestUploadShare(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success), "a device of 1 MiB uploads a frame");
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    def.uploadBytes = (1u << 20) + 1;
    muiRhiRenderer* renderer = NULL;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_errorInvalid && renderer == NULL,
          "a share above the device's budget refused");
    def.uploadBytes = 4 * 512;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_success, "a share of four blocks");
    muiDrawCommand commands[8];
    for (uint32_t i = 0; i < 8; i++)
    {
        commands[i] = Box((float)i, 0);
    }
    muiDrawList longer = ListOf(commands, 8);
    muiDrawList list = ListOf(commands, 3);
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_empty && muiRhiRenderer_IsReady(renderer),
          "ready");
    s_log = (mrhiTestFrameLog){0};
    CHECK(DrawFrame(&gpu, renderer, &longer) == mui_errorCapacity && s_log.frames == 0,
          "a frame past the share refused, though the device's budget holds it");
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.stagingBytes == Staged(3),
          "a frame within the share drawn");
    muiDestroyRhiRenderer(renderer);
    Close(&gpu);
}

// Every allocation of the renderer failing in turn, as it is made and
// draws a list that grows its streams: each call succeeds or reports
// capacity, and every byte comes back.
static void TestMemory(void)
{
    muiDrawCommand commands[40];
    for (uint32_t i = 0; i < 40; i++)
    {
        commands[i] = Box((float)(i % 6), (float)(i / 6));
    }
    muiDrawList list = ListOf(commands, 40);
    bool held = true;
    uint32_t succeeded = 0;
    for (uint32_t failAt = 1; failAt <= 60 && held; failAt++)
    {
        Gpu gpu;
        CHECK(Open(&gpu, mrhi_success), "a device");
        Memory memory = {.failAt = failAt};
        muiRhiRendererDef def = muiDefaultRhiRendererDef();
        def.device = gpu.device;
        def.instances = 4;
        def.allocator = (muiAllocator){Allocate, Release, &memory};
        muiRhiRenderer* renderer = NULL;
        muiResult made = muiCreateRhiRenderer(&def, &renderer);
        held = made == mui_success || (made == mui_errorCapacity && renderer == NULL);
        if (made == mui_success)
        {
            muiResult first = DrawFrame(&gpu, renderer, &list);
            muiResult drawn = DrawFrame(&gpu, renderer, &list);
            held = held && (first == mui_empty || first == mui_errorCapacity) &&
                   (drawn == mui_success || drawn == mui_errorCapacity);
            succeeded += drawn == mui_success ? 1 : 0;
            muiDestroyRhiRenderer(renderer);
        }
        held = held && memory.live == 0;
        Close(&gpu);
    }
    CHECK(held, "every allocation failing in turn: success or capacity, nothing kept");
    CHECK(succeeded > 0, "some drawn, past the allocations that fail");
}

static void TestFailedPipeline(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_errorPlatform), "a device whose pipelines fail");
    muiRhiRenderer* renderer = Make(&gpu, 4);
    muiDrawCommand box = Box(0, 0);
    muiDrawList list = ListOf(&box, 1);
    CHECK(renderer != NULL && DrawFrame(&gpu, renderer, &list) == mui_empty &&
              Pump(&gpu, renderer) == 0 && !muiRhiRenderer_IsReady(renderer) &&
              DrawFrame(&gpu, renderer, &list) == mui_empty,
          "a failed pipeline never draws");
    muiDestroyRhiRenderer(renderer);
    Close(&gpu);
}

// A renderer made with a depth format makes two pipelines and is ready
// only when both are: the first pipeline's notification (the request
// muiRhiRenderer_GetPipelineRequest names) handed over before the other.
static void TestDepthPipelines(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success), "a device");
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    def.depthFormat = mrhi_formatDepth32Float;
    muiRhiRenderer* renderer = NULL;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_success, "made with a depth format");
    mrhiDeviceNotification records[8];
    uint32_t count = 0;
    while (count < 8 && mrhiNextDeviceNotification(gpu.device, &records[count]) == mrhi_success)
    {
        count++;
    }
    const mrhiRequestId first = muiRhiRenderer_GetPipelineRequest(renderer);
    bool handedFirst = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (records[i].request.index1 == first.index1 &&
            records[i].request.generation == first.generation)
        {
            handedFirst = muiRhiRenderer_Notify(renderer, &records[i]);
        }
    }
    bool readyAfterFirst = muiRhiRenderer_IsReady(renderer);
    int others = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        others += records[i].request.index1 != first.index1 &&
                          muiRhiRenderer_Notify(renderer, &records[i])
                      ? 1
                      : 0;
    }
    CHECK(handedFirst && !readyAfterFirst && others == 1 && muiRhiRenderer_IsReady(renderer),
          "ready after both pipelines, not after the first");
    muiDestroyRhiRenderer(renderer);
    Close(&gpu);
}

int main(void)
{
    TestContract();
    TestDraws();
    TestImages();
    TestManyTextures();
    TestChanges();
    TestUploadShare();
    TestMemory();
#if MUI_TEST_TEXT
    TestGlyphs();
#else
    TestNoText();
#endif
    TestFailedPipeline();
    TestDepthPipelines();
    return s_failures == 0 ? 0 : 1;
}
