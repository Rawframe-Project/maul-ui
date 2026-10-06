// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's pixels on a real device (lavapipe on CI): a
// list drawn into a 64-pixel sRGB target cleared to black and read back,
// each probe what the list says there, within 2 of 255:
// - a square's fill inside it, the clear outside;
// - a circle (radii half its side) at its middle, not at its box's
//   corner;
// - a bordered box: its border's color in the border, its fill inside;
// - a half-covering premultiplied red over black, linear 0.5 encoded
//   as sRGB 188;
// - the same list at a scale of 2 into a 128-pixel target, each probe
//   at twice its place.
// Skips (77) without an adapter, unless MUI_RHI_REQUIRED is set.

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-ui-rhi/renderer.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WAIT_NS 10000000000ull

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// A device on the first adapter, software ones included.
static bool Open(Gpu* gpu)
{
    *gpu = (Gpu){0};
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &gpu->instance) != mrhi_success ||
        mrhiRequestAdapters(gpu->instance, &search, &request) != mrhi_success ||
        mrhiNextInstanceNotification(gpu->instance, &record) != mrhi_success ||
        record.outcome != mrhi_success ||
        mrhiGetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count == 0)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success &&
           mrhiNextInstanceNotification(gpu->instance, &record) == mrhi_success &&
           record.outcome == mrhi_success;
}

static void Close(Gpu* gpu)
{
    if (gpu->device != NULL)
    {
        mrhiDestroyDevice(gpu->device);
    }
    if (gpu->instance != NULL)
    {
        mrhiDestroyInstance(gpu->instance);
    }
}

static double Seconds(void)
{
    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Polls the device, ten seconds at most, for the renderer's pipeline.
static bool AwaitReady(Gpu* gpu, muiRhiRenderer* renderer)
{
    double deadline = Seconds() + 10.0;
    while (!muiRhiRenderer_IsReady(renderer) && Seconds() < deadline)
    {
        mrhiDeviceNotification record;
        while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
        {
            (void)muiRhiRenderer_Notify(renderer, &record);
        }
    }
    return muiRhiRenderer_IsReady(renderer);
}

// Draws a list into a square target of a side and reads it back as RGBA8.
static bool Render(Gpu* gpu, muiRhiRenderer* renderer, const muiDrawList* list, uint32_t side,
                   uint8_t* pixels)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8UnormSrgb;
    targetDef.width = side;
    targetDef.height = side;
    mrhiResourceId target = {0};
    if (mrhiBeginFrame(gpu->device, &frame) != mrhi_success ||
        mrhiDeclareTexture(gpu->device, &targetDef, &target) != mrhi_success)
    {
        return false;
    }
    const muiRhiTarget into = {.resource = target,
                               .width = side,
                               .height = side,
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
    const mrhiExtent3d extent = {side, side, 1};
    mrhiRequestId readback = {0};
    mrhiRequestId token = {0};
    bool recorded =
        muiRhiRenderer_AddPasses(renderer, list, &into) == mui_success &&
        mrhiAddPass(gpu->device, &readDef, &reading) == mrhi_success &&
        mrhiCompileFrame(gpu->device) == mrhi_success &&
        muiRhiRenderer_Record(renderer) == mui_success &&
        mrhiBeginPass(gpu->device, reading) == mrhi_success &&
        mrhiReadTexture(gpu->device, reading, &source, &extent, &readback) == mrhi_success &&
        mrhiEndPass(gpu->device, reading) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    if (mrhiSubmitFrame(gpu->device, &token) != mrhi_success ||
        mrhiWaitFrame(gpu->device, token, WAIT_NS) != mrhi_success)
    {
        return false;
    }
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
    {
    }
    size_t size = (size_t)side * side * 4;
    size_t taken = 0;
    return mrhiTakeReadback(gpu->device, readback, pixels, size, &taken) == mrhi_success &&
           taken == size;
}

static muiDrawCommand Box(float x, float y, float width, float height, muiLinearColor fill)
{
    muiDrawCommand command = {.kind = mui_drawBox};
    command.box.rect = (muiRect){x, y, width, height};
    command.box.fill = fill;
    return command;
}

typedef struct Probe
{
    int x;
    int y;
    uint8_t color[4];
    const char* what;
} Probe;

static const Probe s_probes[] = {
    {18, 18, {255, 0, 0, 255}, "inside the square"},
    {4, 4, {0, 0, 0, 255}, "outside everything"},
    {46, 18, {0, 255, 0, 255}, "the circle's middle"},
    {37, 9, {0, 0, 0, 255}, "outside the circle, inside its box"},
    {10, 46, {255, 255, 255, 255}, "the border"},
    {32, 46, {0, 0, 255, 255}, "inside the border"},
    {50, 60, {188, 0, 0, 255}, "half red over black"},
};

static void CheckProbes(const uint8_t* pixels, uint32_t side, int scale)
{
    for (size_t i = 0; i < sizeof(s_probes) / sizeof(s_probes[0]); i++)
    {
        const Probe* probe = &s_probes[i];
        // The middle of the probe's pixel at the scale.
        int x = probe->x * scale + (scale - 1) / 2;
        int y = probe->y * scale + (scale - 1) / 2;
        const uint8_t* pixel = &pixels[((size_t)y * side + (size_t)x) * 4];
        bool near = true;
        for (int c = 0; c < 4; c++)
        {
            int difference = pixel[c] - probe->color[c];
            near = near && difference >= -2 && difference <= 2;
        }
        if (!near)
        {
            printf("  at %d,%d (scale %d): %u %u %u %u\n", x, y, scale, pixel[0], pixel[1],
                   pixel[2], pixel[3]);
        }
        CHECK(near, probe->what);
    }
}

int main(void)
{
    Gpu gpu;
    if (!Open(&gpu))
    {
        Close(&gpu);
        const char* required = getenv("MUI_RHI_REQUIRED");
        if (required != NULL && required[0] != '\0')
        {
            printf("FAIL: no adapter, and MUI_RHI_REQUIRED is set\n");
            return 1;
        }
        printf("no adapter: skipped\n");
        return 77;
    }
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    muiRhiRenderer* renderer = NULL;
    CHECK(muiCreateRhiRenderer(&def, &renderer) == mui_success && AwaitReady(&gpu, renderer),
          "a ready renderer");
    muiDrawCommand commands[4] = {
        Box(8, 8, 20, 20, (muiLinearColor){1, 0, 0, 1}),
        Box(36, 8, 20, 20, (muiLinearColor){0, 1, 0, 1}),
        Box(8, 36, 48, 20, (muiLinearColor){0, 0, 1, 1}),
        Box(44, 58, 12, 4, (muiLinearColor){0.5f, 0, 0, 0.5f}),
    };
    commands[1].box.radii = (muiCorners){10, 10, 10, 10};
    commands[2].box.borderWidths = (muiSides){4, 4, 4, 4};
    for (int side = 0; side < 4; side++)
    {
        commands[2].box.borderColors[side] = (muiLinearColor){1, 1, 1, 1};
    }
    muiDrawList list = {.commands = commands, .commandCount = 4};
    list.header.scale = 1.0f;
    uint8_t* pixels = malloc(128 * 128 * 4);
    if (renderer != NULL && muiRhiRenderer_IsReady(renderer) && pixels != NULL)
    {
        CHECK(Render(&gpu, renderer, &list, 64, pixels), "drawn and read at a scale of 1");
        CheckProbes(pixels, 64, 1);
        list.header.scale = 2.0f;
        CHECK(Render(&gpu, renderer, &list, 128, pixels), "drawn and read at a scale of 2");
        CheckProbes(pixels, 128, 2);
    }
    free(pixels);
    muiDestroyRhiRenderer(renderer);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
