// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The samples' harness (record mui-0005).

#include "harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// How long a frame is waited for.
#define WAIT_NS 10000000000ull

int SampleOpen(Sample* sample)
{
    *sample = (Sample){0};
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    bool found = mrhiCreateInstance(&def, &sample->instance) == mrhi_success &&
                 mrhiRequestAdapters(sample->instance, &search, &request) == mrhi_success &&
                 mrhiNextInstanceNotification(sample->instance, &record) == mrhi_success &&
                 record.outcome == mrhi_success &&
                 mrhiGetAdapters(sample->instance, &adapter, 1, &count) == mrhi_success &&
                 count != 0;
    if (!found)
    {
        const char* required = getenv("MUI_RHI_REQUIRED");
        if (required != NULL && required[0] != '\0' && required[0] != '0')
        {
            printf("FAIL: no adapter, and MUI_RHI_REQUIRED is set\n");
            return 1;
        }
        printf("skipped: no adapter\n");
        return SAMPLE_SKIPPED;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    if (mrhiCreateDevice(sample->instance, &deviceDef, &sample->device, &request) != mrhi_success ||
        mrhiNextInstanceNotification(sample->instance, &record) != mrhi_success ||
        record.outcome != mrhi_success)
    {
        printf("FAIL: no device on the adapter\n");
        return 1;
    }
    return 0;
}

int SampleClose(Sample* sample)
{
    if (sample->device != NULL)
    {
        mrhiDestroyDevice(sample->device);
    }
    if (sample->instance != NULL)
    {
        mrhiDestroyInstance(sample->instance);
    }
    return sample->failures == 0 ? 0 : 1;
}

bool SampleCheck(Sample* sample, bool condition, const char* what)
{
    if (!condition)
    {
        printf("FAIL: %s\n", what);
        sample->failures++;
    }
    return condition;
}

static double Seconds(void)
{
    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

bool SampleAwaitReady(Sample* sample, muiRhiRenderer* renderer)
{
    double deadline = Seconds() + 10.0;
    while (!muiRhiRenderer_IsReady(renderer) && Seconds() < deadline)
    {
        mrhiDeviceNotification record;
        while (mrhiNextDeviceNotification(sample->device, &record) == mrhi_success)
        {
            (void)muiRhiRenderer_Notify(renderer, &record);
        }
    }
    return muiRhiRenderer_IsReady(renderer);
}

bool SampleTexture(Sample* sample, uint32_t width, uint32_t height, const uint8_t* texels,
                   mrhiTextureId* textureOut)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8UnormSrgb;
    def.width = width;
    def.height = height;
    def.usage = mrhi_textureSampled | mrhi_textureCopyDestination;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    if (mrhiCreateTexture(sample->device, &def, textureOut) != mrhi_success ||
        mrhiBeginFrame(sample->device, &frame) != mrhi_success ||
        mrhiImportTexture(sample->device, *textureOut, &resource) != mrhi_success)
    {
        return false;
    }
    const mrhiAccess write = {.resource = resource,
                              .kind = mrhi_accessCopyDestination,
                              .range = {.mipCount = 1, .layerCount = 1}};
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.passClass = mrhi_passTransfer;
    passDef.accesses = &write;
    passDef.accessCount = 1;
    passDef.neverCull = true;
    mrhiPassId pass = {0};
    const mrhiTextureCopy into = {.resource = resource};
    const mrhiTexelLayout layout = {.bytesPerRow = width * 4};
    const mrhiExtent3d extent = {width, height, 1};
    mrhiRequestId token = {0};
    bool recorded =
        mrhiAddPass(sample->device, &passDef, &pass) == mrhi_success &&
        mrhiCompileFrame(sample->device) == mrhi_success &&
        mrhiBeginPass(sample->device, pass) == mrhi_success &&
        mrhiWriteTexture(sample->device, pass, &into, texels, (size_t)width * height * 4, &layout,
                         &extent) == mrhi_success &&
        mrhiEndPass(sample->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(sample->device);
        return false;
    }
    return mrhiSubmitFrame(sample->device, &token) == mrhi_success &&
           mrhiWaitFrame(sample->device, token, WAIT_NS) == mrhi_success;
}

bool SampleRender(Sample* sample, muiRhiRenderer* renderer, const muiDrawList* list, uint32_t width,
                  uint32_t height, uint8_t* pixels)
{
    mrhiDevice* device = sample->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8UnormSrgb;
    targetDef.width = width;
    targetDef.height = height;
    mrhiResourceId target = {0};
    if (mrhiBeginFrame(device, &frame) != mrhi_success ||
        mrhiDeclareTexture(device, &targetDef, &target) != mrhi_success)
    {
        return false;
    }
    const muiRhiTarget into = {.resource = target,
                               .width = width,
                               .height = height,
                               .clear = true,
                               .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    const mrhiAccess read = {.resource = target,
                             .kind = mrhi_accessCopySource,
                             .range = {.mipCount = 1, .layerCount = 1}};
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    const mrhiTextureCopy source = {.resource = target};
    const mrhiExtent3d extent = {width, height, 1};
    mrhiRequestId readback = {0};
    mrhiRequestId token = {0};
    bool recorded = muiRhiRenderer_AddPasses(renderer, list, &into) == mui_success &&
                    mrhiAddPass(device, &readDef, &reading) == mrhi_success &&
                    mrhiCompileFrame(device) == mrhi_success &&
                    muiRhiRenderer_Record(renderer) == mui_success &&
                    mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &extent, &readback) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(device);
        return false;
    }
    if (mrhiSubmitFrame(device, &token) != mrhi_success ||
        mrhiWaitFrame(device, token, WAIT_NS) != mrhi_success)
    {
        return false;
    }
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
    size_t size = (size_t)width * height * 4;
    size_t taken = 0;
    return mrhiTakeReadback(device, readback, pixels, size, &taken) == mrhi_success &&
           taken == size;
}
