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
//   at twice its place;
// - gradients, linear and radial, each probe the color the core's own
//   premultiplied Oklab mixing gives at its place along the gradient;
// - a hard outer shadow beside its box and not under it, a blurred one
//   against the exact blur of its square box (a product of two error
//   functions) within 4, and an inset one's ring inside its box only.
// Skips (77) without an adapter, unless MUI_RHI_REQUIRED is set.

#include "color.h"
#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-ui-rhi/renderer.h"

#include <math.h>
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

// A value in linear light encoded as an sRGB byte.
static int SrgbByte(double linear)
{
    double c = linear <= 0.0031308 ? 12.92 * linear : 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
    return (int)lround(fmin(fmax(c, 0.0), 1.0) * 255.0);
}

static bool Near(const uint8_t* pixels, uint32_t side, int x, int y, const int expected[4],
                 int tolerance)
{
    const uint8_t* pixel = &pixels[((size_t)y * side + (size_t)x) * 4];
    bool near = true;
    for (int c = 0; c < 4; c++)
    {
        int difference = pixel[c] - expected[c];
        near = near && difference >= -tolerance && difference <= tolerance;
    }
    if (!near)
    {
        printf("  at %d,%d: %u %u %u %u, expected %d %d %d %d\n", x, y, pixel[0], pixel[1],
               pixel[2], pixel[3], expected[0], expected[1], expected[2], expected[3]);
    }
    return near;
}

// Red to blue at a place along a gradient, mixed as the core mixes.
static void Mixed(double t, int out[4])
{
    float red[4];
    float blue[4];
    muiColorToChannels((muiColor){1.0f, 0.0f, 0.0f, 1.0f}, red);
    muiColorToChannels((muiColor){0.0f, 0.0f, 1.0f, 1.0f}, blue);
    float f = (float)fmin(fmax(t, 0.0), 1.0);
    float channels[4];
    for (int i = 0; i < 4; i++)
    {
        channels[i] = red[i] + (blue[i] - red[i]) * f;
    }
    muiColor color = muiColorFromChannels(channels);
    out[0] = (int)lround((double)color.r * 255.0);
    out[1] = (int)lround((double)color.g * 255.0);
    out[2] = (int)lround((double)color.b * 255.0);
    out[3] = 255;
}

// How much of a square box a Gaussian blur of sigma leaves at a point:
// the blur is separable, an error function each way.
static double BlurOf(muiRect box, double sigma, double x, double y)
{
    double s = sigma * sqrt(2.0);
    double left = (double)box.x;
    double top = (double)box.y;
    double right = left + (double)box.width;
    double bottom = top + (double)box.height;
    double across = 0.5 * (erf((right - x) / s) - erf((left - x) / s));
    double down = 0.5 * (erf((bottom - y) / s) - erf((top - y) / s));
    return across * down;
}

static void TestGradientsAndShadows(Gpu* gpu, muiRhiRenderer* renderer, uint8_t* pixels)
{
    const muiDrawGradient gradients[3] = {
        {0},
        {.kind = mui_gradientLinear,
         .stopCount = 2,
         .interpolation = mui_interpolateOklab,
         .angle = 90.0f,
         .colors = {{1, 0, 0, 1}, {0, 0, 1, 1}},
         .positions = {0.0f, 1.0f}},
        {.kind = mui_gradientRadial,
         .stopCount = 2,
         .interpolation = mui_interpolateOklab,
         .colors = {{1, 0, 0, 1}, {0, 0, 1, 1}},
         .positions = {0.0f, 1.0f}},
    };
    muiDrawCommand commands[6] = {
        Box(0, 0, 64, 12, (muiLinearColor){0, 0, 0, 0}),
        Box(0, 12, 32, 32, (muiLinearColor){0, 0, 0, 0}),
        {.kind = mui_drawShadow},
        {.kind = mui_drawShadow},
        {.kind = mui_drawShadow},
    };
    commands[0].box.gradient = 1;
    commands[1].box.gradient = 2;
    // A hard green shadow down and right of a box not drawn.
    commands[2].shadow = (muiDrawShadow){
        .rect = {40, 14, 14, 14}, .color = {0, 1, 0, 1}, .offsetX = 4, .offsetY = 4};
    // A white shadow blurred by 8 (sigma 4) around a box not drawn.
    const muiRect blurredBox = {44, 46, 12, 12};
    commands[3].shadow = (muiDrawShadow){.rect = blurredBox, .color = {1, 1, 1, 1}, .blur = 8};
    // A hard red inset shadow spread 2 inside a box.
    commands[4].shadow =
        (muiDrawShadow){.rect = {4, 48, 24, 12}, .color = {1, 0, 0, 1}, .spread = 2, .inset = 1};
    muiDrawList list = {.commands = commands, .commandCount = 5};
    list.gradients = gradients;
    list.gradientCount = 3;
    list.header.scale = 1.0f;
    if (!Render(gpu, renderer, &list, 64, pixels))
    {
        CHECK(false, "gradients and shadows drawn and read");
        return;
    }
    // Along the linear gradient, to the right: t at a pixel's middle.
    for (int x = 2; x < 64; x += 29)
    {
        int expected[4];
        Mixed((x + 0.5) / 64.0, expected);
        CHECK(Near(pixels, 64, x, 6, expected, 2), "the linear gradient");
    }
    // Outward from the radial gradient's middle, its ellipse's reach
    // 16 times the square root of 2.
    const int radial[3][2] = {{16, 28}, {4, 16}, {28, 40}};
    for (int i = 0; i < 3; i++)
    {
        double dx = radial[i][0] + 0.5 - 16.0;
        double dy = radial[i][1] + 0.5 - 28.0;
        int expected[4];
        Mixed(sqrt(dx * dx + dy * dy) / (16.0 * sqrt(2.0)), expected);
        CHECK(Near(pixels, 64, radial[i][0], radial[i][1], expected, 2), "the radial gradient");
    }
    const int green[4] = {0, 255, 0, 255};
    const int black[4] = {0, 0, 0, 255};
    const int red[4] = {255, 0, 0, 255};
    CHECK(Near(pixels, 64, 56, 30, green, 2) && Near(pixels, 64, 47, 21, black, 2) &&
              Near(pixels, 64, 38, 13, black, 2),
          "a hard shadow beside its box, not under it");
    for (int y = 42; y <= 45; y++)
    {
        int level = SrgbByte(BlurOf(blurredBox, 4.0, 50.5, y + 0.5));
        const int expected[4] = {level, level, level, 255};
        CHECK(Near(pixels, 64, 50, y, expected, 4), "a blurred shadow against the exact blur");
    }
    CHECK(Near(pixels, 64, 5, 54, red, 2) && Near(pixels, 64, 16, 54, black, 2) &&
              Near(pixels, 64, 2, 54, black, 2),
          "an inset shadow's ring inside its box only");
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
        TestGradientsAndShadows(&gpu, renderer, pixels);
    }
    free(pixels);
    muiDestroyRhiRenderer(renderer);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
