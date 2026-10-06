// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer on Maul RHI's test driver, without a GPU: its
// def and arguments; not drawing until its pipeline is ready, the
// device's notification making it so, or a failed pipeline leaving it
// not; a frame's upload and draw passes, the upload a record of 144
// bytes a box and nothing for the kinds it does not draw yet; the
// instance buffer grown for a long list; an empty list.

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/test.h"
#include "maul-ui-rhi/renderer.h"

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

static muiRhiRenderer* Make(Gpu* gpu, uint32_t instances)
{
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu->device;
    def.instances = instances;
    muiRhiRenderer* renderer = NULL;
    return muiCreateRhiRenderer(&def, &renderer) == mui_success ? renderer : NULL;
}

// The staging bytes an upload of a number of instances takes, in
// Maul RHI's 256-byte blocks.
static uint64_t Staged(uint64_t instances)
{
    return (instances * 144 + 255) / 256 * 256;
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
    muiDrawCommand commands[3] = {Box(0, 0), Box(20, 20), {.kind = mui_drawShadow}};
    muiDrawList list = ListOf(commands, 3);
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_empty && s_log.frames == 0,
          "nothing drawn before its pipeline is ready");
    CHECK(muiRhiRenderer_IsReady(renderer), "the device's notification made it ready");
    // The upload, then the pipeline, the bindings, the root block and the
    // draw.
    CHECK(DrawFrame(&gpu, renderer, &list) == mui_success && s_log.passes == 3 &&
              s_log.stagingBytes == Staged(2) && s_log.commands == 5,
          "upload and draw passes, two boxes uploaded, the shadow not yet");
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
    CHECK(many != NULL && DrawFrame(&gpu, renderer, &longList) == mui_success &&
              s_log.stagingBytes == Staged(3000),
          "the buffer grown for 3000 boxes");
    free(many);
    muiDrawList none = ListOf(NULL, 0);
    CHECK(DrawFrame(&gpu, renderer, &none) == mui_success && s_log.passes == 3 &&
              s_log.stagingBytes == 0 && s_log.commands == 0,
          "an empty list still clears");
    muiDestroyRhiRenderer(renderer);
    Close(&gpu);
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

int main(void)
{
    TestContract();
    TestDraws();
    TestFailedPipeline();
    return s_failures == 0 ? 0 : 1;
}
