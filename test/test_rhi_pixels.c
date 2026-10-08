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
//   functions) within 4, and an inset one's ring inside its box only;
// - clips and transforms: a box in a round clip with an inverted clip
//   inside it, a box scaled and moved, a bar turned 45 degrees clockwise
//   (probed where a bar not turned, or turned the other way, differs),
//   and a box in a clip that a transform scales and moves;
// - images from textures the test fills, at scales 1 and 2: a nine
//   slice, its 2-texel red border kept 2 units wide around its stretched
//   green middle (probed where stretching it whole differs), an image
//   tinted to half, another texture's between two of the first (each
//   drawn with its own), and a uv rect of the middle alone;
// - glyph runs in Ahem, whose glyphs are its em square 8 tenths above
//   the baseline, at scales 1 and 2: two squares and the gap between
//   them, and a run a transform moves; runs a transform scales by 2 or
//   turns a quarter, drawn from distance fields.
// - tiling: each repeat mode across and up, tiles at 9-slice edges and
//   in a seam on a mipmapped image, and with the slices' fit;
// - projection: the screen's own matrix drawing as none, one scaling by
//   2 as a scale of 2, a panel turned in perspective, its glyphs from
//   fields, and depth tested against the host's.
// Skips (77) without an adapter, unless MUI_RHI_REQUIRED is set. On the
// web it runs in headless Chrome's WebGPU through the web runner, its
// waits sleeping through JSPI, and ends with the runner's exit line.

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

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// How long an answer is waited for: 10 s natively, 10,000 sleeps of a
// millisecond or more on the web, where the browser answers between them.
#define WAIT_NS     10000000000ull
#define WAIT_SLEEPS 10000

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// A device on the first adapter, software ones included.
static mrhiResult NextInstance(mrhiInstance* instance, mrhiInstanceNotification* recordOut)
{
    mrhiResult status = mrhiNextInstanceNotification(instance, recordOut);
#ifdef __EMSCRIPTEN__
    for (int slept = 0; status == mrhi_empty && slept < WAIT_SLEEPS; slept++)
    {
        emscripten_sleep(1);
        status = mrhiNextInstanceNotification(instance, recordOut);
    }
#endif
    return status;
}

// Waits for a frame to finish.
static mrhiResult WaitFrame(mrhiDevice* device, mrhiRequestId token)
{
    mrhiResult status = mrhiWaitFrame(device, token, WAIT_NS);
#ifdef __EMSCRIPTEN__
    for (int slept = 0; status == mrhi_timeout && slept < WAIT_SLEEPS; slept++)
    {
        emscripten_sleep(1);
        status = mrhiWaitFrame(device, token, 0);
    }
#endif
    return status;
}

// The status, printed on the web for the runner, which ends the run on it.
static int Exit(int status)
{
#ifdef __EMSCRIPTEN__
    printf("mui-test: exit %d\n", status);
#endif
    return status;
}

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
        NextInstance(gpu->instance, &record) != mrhi_success || record.outcome != mrhi_success ||
        mrhiGetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count == 0)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success &&
           NextInstance(gpu->instance, &record) == mrhi_success && record.outcome == mrhi_success;
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
#ifdef __EMSCRIPTEN__
        emscripten_sleep(1);
#endif
    }
    return muiRhiRenderer_IsReady(renderer);
}

// Draws a list into a square target of a side, through a projection
// when one is given, over a depth texture cleared to a depth in a pass
// of its own when the depth is 0 or more, and reads it back as RGBA8.
static bool RenderInDepth(Gpu* gpu, muiRhiRenderer* renderer, const muiDrawList* list,
                          uint32_t side, const float* projection, float depth, uint8_t* pixels)
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
    muiRhiTarget into = {.resource = target,
                         .width = side,
                         .height = side,
                         .clear = true,
                         .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    if (projection != NULL)
    {
        into.projected = true;
        memcpy(into.projection, projection, sizeof into.projection);
    }
    mrhiPassDef clearDef = mrhiDefaultPassDef();
    mrhiPassId clearing = {0};
    if (depth >= 0.0f)
    {
        mrhiTextureDef depthDef = targetDef;
        depthDef.format = mrhi_formatDepth32Float;
        if (mrhiDeclareTexture(gpu->device, &depthDef, &into.depth) != mrhi_success)
        {
            (void)mrhiDropFrame(gpu->device);
            return false;
        }
        clearDef.depthTarget = (mrhiDepthTarget){.resource = into.depth,
                                                 .depthLoad = mrhi_loadClear,
                                                 .depthStore = mrhi_storeKeep,
                                                 .clearDepth = depth};
        clearDef.neverCull = true;
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
    const mrhiTextureCopy source = {.resource = target};
    const mrhiExtent3d extent = {side, side, 1};
    mrhiRequestId readback = {0};
    mrhiRequestId token = {0};
    bool cleared = depth < 0.0f || mrhiAddPass(gpu->device, &clearDef, &clearing) == mrhi_success;
    bool recorded =
        cleared && muiRhiRenderer_AddPasses(renderer, list, &into) == mui_success &&
        mrhiAddPass(gpu->device, &readDef, &reading) == mrhi_success &&
        mrhiCompileFrame(gpu->device) == mrhi_success &&
        (depth < 0.0f || (mrhiBeginPass(gpu->device, clearing) == mrhi_success &&
                          mrhiEndPass(gpu->device, clearing) == mrhi_success)) &&
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
        WaitFrame(gpu->device, token) != mrhi_success)
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

static bool RenderThrough(Gpu* gpu, muiRhiRenderer* renderer, const muiDrawList* list,
                          uint32_t side, const float* projection, uint8_t* pixels)
{
    return RenderInDepth(gpu, renderer, list, side, projection, -1.0f, pixels);
}

static bool Render(Gpu* gpu, muiRhiRenderer* renderer, const muiDrawList* list, uint32_t side,
                   uint8_t* pixels)
{
    return RenderThrough(gpu, renderer, list, side, NULL, pixels);
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

// A conic gradient from 90 degrees, red to blue over the turn, filling
// the target: at a pixel's middle, its turn clockwise from the right.
static void TestConic(Gpu* gpu, muiRhiRenderer* renderer, uint8_t* pixels)
{
    const muiDrawGradient gradients[2] = {
        {0},
        {.kind = mui_gradientConic,
         .stopCount = 2,
         .interpolation = mui_interpolateOklab,
         .angle = 90.0f,
         .colors = {{1, 0, 0, 1}, {0, 0, 1, 1}},
         .positions = {0.0f, 1.0f}},
    };
    muiDrawCommand commands[1] = {Box(0, 0, 64, 64, (muiLinearColor){0, 0, 0, 0})};
    commands[0].box.gradient = 1;
    muiDrawList list = {.commands = commands, .commandCount = 1};
    list.gradients = gradients;
    list.gradientCount = 2;
    list.header.scale = 1.0f;
    if (!Render(gpu, renderer, &list, 64, pixels))
    {
        CHECK(false, "a conic gradient drawn and read");
        return;
    }
    const int points[4][2] = {{56, 33}, {33, 56}, {8, 33}, {33, 8}};
    for (int i = 0; i < 4; i++)
    {
        double dx = points[i][0] + 0.5 - 32.0;
        double dy = points[i][1] + 0.5 - 32.0;
        double turn = (atan2(dx, -dy) - 0.5 * 3.14159265358979) / (2.0 * 3.14159265358979);
        int expected[4];
        Mixed(turn - floor(turn), expected);
        CHECK(Near(pixels, 64, points[i][0], points[i][1], expected, 2), "the conic gradient");
    }
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

// At a scale of 1 or 2 into a target of 64 pixels a unit, each probe at
// its place times the scale.
static void TestClipsAndTransforms(Gpu* gpu, muiRhiRenderer* renderer, uint8_t* pixels, int scale)
{
    const uint32_t side = 64u * (uint32_t)scale;
    const float turn = 0.70710678f;
    const muiDrawTransform transforms[4] = {
        {1, 0, 0, 1, 0, 0},
        {2, 0, 0, 2, 32, 0},
        {turn, turn, -turn, turn, 48, 48},
        {2, 0, 0, 2, 0, 40},
    };
    const muiDrawClip clips[4] = {
        {0},
        // A circle, and a hole in it.
        {.rect = {0, 0, 32, 32}, .radii = {16, 16, 16, 16}},
        {.rect = {12, 12, 8, 8}, .parent = 1, .invert = 1},
        // A square the transform makes 16 pixels wide at 0, 40.
        {.rect = {0, 0, 8, 8}, .transform = 3},
    };
    muiDrawCommand commands[4] = {
        Box(0, 0, 32, 32, (muiLinearColor){1, 0, 0, 1}),
        Box(0, 0, 8, 8, (muiLinearColor){0, 1, 0, 1}),
        Box(-8, -2, 16, 4, (muiLinearColor){0, 0, 1, 1}),
        Box(0, 36, 32, 24, (muiLinearColor){1, 1, 1, 1}),
    };
    commands[0].clip = 2;
    commands[1].transform = 1;
    commands[2].transform = 2;
    commands[3].clip = 3;
    muiDrawList list = {.commands = commands, .commandCount = 4};
    list.clips = clips;
    list.clipCount = 4;
    list.transforms = transforms;
    list.transformCount = 4;
    list.header.scale = (float)scale;
    if (!Render(gpu, renderer, &list, side, pixels))
    {
        CHECK(false, "clips and transforms drawn and read");
        return;
    }
    const int black[4] = {0, 0, 0, 255};
    const int red[4] = {255, 0, 0, 255};
    const int green[4] = {0, 255, 0, 255};
    const int blue[4] = {0, 0, 255, 255};
    const int white[4] = {255, 255, 255, 255};
    CHECK(Near(pixels, side, 6 * scale, 16 * scale, red, 2) &&
              Near(pixels, side, 16 * scale, 16 * scale, black, 2) &&
              Near(pixels, side, 1 * scale, 1 * scale, black, 2),
          "a round clip with an inverted clip inside it");
    CHECK(Near(pixels, side, 46 * scale, 14 * scale, green, 2) &&
              Near(pixels, side, 50 * scale, 8 * scale, black, 2) &&
              Near(pixels, side, 40 * scale, 17 * scale, black, 2),
          "a box scaled by 2 and moved");
    CHECK(Near(pixels, side, 48 * scale, 48 * scale, blue, 2) &&
              Near(pixels, side, 52 * scale, 52 * scale, blue, 2) &&
              Near(pixels, side, 43 * scale, 52 * scale, black, 2) &&
              Near(pixels, side, 55 * scale, 48 * scale, black, 2),
          "a bar turned 45 degrees clockwise");
    CHECK(Near(pixels, side, 8 * scale, 48 * scale, white, 2) &&
              Near(pixels, side, 20 * scale, 48 * scale, black, 2) &&
              Near(pixels, side, 8 * scale, 58 * scale, black, 2) &&
              Near(pixels, side, 8 * scale, 38 * scale, black, 2),
          "a box in a clip a transform scales and moves");
}

// The test's textures: 1 an 8-texel square, a 2-texel red border
// around green; 2 a 2-texel blue square.
typedef struct Textures
{
    mrhiTextureId bordered;
    mrhiTextureId blue;
    mrhiTextureId sided;
    mrhiTextureId quads;
    mrhiTextureId mipped;
} Textures;

static bool FindImage(void* context, uint64_t key, muiRhiImage* imageOut)
{
    const Textures* textures = context;
    if (key == 1)
    {
        *imageOut = (muiRhiImage){textures->bordered, 8, 8};
        return true;
    }
    if (key == 2)
    {
        *imageOut = (muiRhiImage){textures->blue, 2, 2};
        return true;
    }
    if (key == 3)
    {
        *imageOut = (muiRhiImage){textures->sided, 8, 8};
        return true;
    }
    if (key == 4)
    {
        *imageOut = (muiRhiImage){textures->quads, 8, 8};
        return true;
    }
    if (key == 5)
    {
        *imageOut = (muiRhiImage){textures->mipped, 8, 8};
        return true;
    }
    return false;
}

// A texture of a side and its mips, each level's RGBA8 texels given,
// filled in a frame of its own.
static bool MakeLevels(Gpu* gpu, uint32_t side, const uint8_t* const* levels, uint32_t levelCount,
                       mrhiTextureId* textureOut)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8UnormSrgb;
    def.width = side;
    def.height = side;
    def.mipLevels = levelCount;
    def.usage = mrhi_textureSampled | mrhi_textureCopyDestination;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    if (mrhiCreateTexture(gpu->device, &def, textureOut) != mrhi_success ||
        mrhiBeginFrame(gpu->device, &frame) != mrhi_success ||
        mrhiImportTexture(gpu->device, *textureOut, &resource) != mrhi_success)
    {
        return false;
    }
    const mrhiAccess write = {.resource = resource,
                              .kind = mrhi_accessCopyDestination,
                              .range = {.mipCount = levelCount, .layerCount = 1}};
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.passClass = mrhi_passTransfer;
    passDef.accesses = &write;
    passDef.accessCount = 1;
    passDef.neverCull = true;
    mrhiPassId pass = {0};
    mrhiRequestId token = {0};
    bool recorded = mrhiAddPass(gpu->device, &passDef, &pass) == mrhi_success &&
                    mrhiCompileFrame(gpu->device) == mrhi_success &&
                    mrhiBeginPass(gpu->device, pass) == mrhi_success;
    for (uint32_t mip = 0; recorded && mip < levelCount; mip++)
    {
        const uint32_t at = side >> mip;
        const mrhiTextureCopy into = {.resource = resource, .mip = mip};
        const mrhiTexelLayout layout = {.bytesPerRow = at * 4};
        const mrhiExtent3d extent = {at, at, 1};
        recorded = mrhiWriteTexture(gpu->device, pass, &into, levels[mip], (size_t)at * at * 4,
                                    &layout, &extent) == mrhi_success;
    }
    recorded = recorded && mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    return mrhiSubmitFrame(gpu->device, &token) == mrhi_success &&
           WaitFrame(gpu->device, token) == mrhi_success;
}

static bool MakeTexture(Gpu* gpu, uint32_t side, const uint8_t* texels, mrhiTextureId* textureOut)
{
    return MakeLevels(gpu, side, &texels, 1, textureOut);
}

static bool MakeTextures(Gpu* gpu, Textures* textures)
{
    // A red frame two texels wide around green; red on the left two
    // columns alone, which a mirror moves.
    uint8_t bordered[8 * 8 * 4];
    uint8_t sided[8 * 8 * 4];
    // Quarters: red, green above; blue, white below.
    uint8_t quads[8 * 8 * 4];
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            bool border = x < 2 || x > 5 || y < 2 || y > 5;
            const uint8_t texel[4] = {border ? 255 : 0, border ? 0 : 255, 0, 255};
            memcpy(&bordered[(y * 8 + x) * 4], texel, 4);
            const uint8_t side[4] = {x < 2 ? 255 : 0, x < 2 ? 0 : 255, 0, 255};
            memcpy(&sided[(y * 8 + x) * 4], side, 4);
            bool right = x >= 4;
            bool below = y >= 4;
            const uint8_t quad[4] = {(!right && !below) || (right && below) ? 255 : 0,
                                     right ? 255 : 0, below ? 255 : 0, 255};
            memcpy(&quads[(y * 8 + x) * 4], quad, 4);
        }
    }
    // The quarters again over a second level of magenta, which only a
    // coarser level than the first shows.
    uint8_t magenta[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++)
    {
        const uint8_t texel[4] = {255, 0, 255, 255};
        memcpy(&magenta[i * 4], texel, 4);
    }
    const uint8_t* const levels[2] = {quads, magenta};
    const uint8_t blue[2 * 2 * 4] = {0, 0, 255, 255, 0, 0, 255, 255,
                                     0, 0, 255, 255, 0, 0, 255, 255};
    return MakeTexture(gpu, 8, bordered, &textures->bordered) &&
           MakeTexture(gpu, 2, blue, &textures->blue) &&
           MakeTexture(gpu, 8, sided, &textures->sided) &&
           MakeTexture(gpu, 8, quads, &textures->quads) &&
           MakeLevels(gpu, 8, levels, 2, &textures->mipped);
}

static muiDrawCommand Image(uint64_t key, muiRect rect)
{
    muiDrawCommand command = {.kind = mui_drawImage};
    command.image.rect = rect;
    command.image.image = key;
    command.image.uv = (muiRect){0, 0, 1, 1};
    command.image.tint = (muiLinearColor){1, 1, 1, 1};
    return command;
}

static void TestImages(Gpu* gpu, muiRhiRenderer* renderer, uint8_t* pixels, int scale)
{
    const uint32_t side = 64u * (uint32_t)scale;
    muiDrawCommand commands[6] = {
        Image(1, (muiRect){0, 0, 40, 24}),   Image(2, (muiRect){0, 28, 16, 16}),
        Image(1, (muiRect){20, 28, 16, 16}), Image(1, (muiRect){44, 0, 16, 16}),
        Image(3, (muiRect){0, 48, 40, 12}),  Image(3, (muiRect){44, 48, 16, 12}),
    };
    commands[0].image.slice = (muiSides){2, 2, 2, 2};
    commands[2].image.uv = (muiRect){0.25f, 0.25f, 0.5f, 0.5f};
    commands[3].image.tint = (muiLinearColor){0.5f, 0.5f, 0.5f, 0.5f};
    // The red-sided image sliced at its red: mirrored, then as it is.
    commands[4].image.uv = (muiRect){1, 0, -1, 1};
    commands[4].image.slice = (muiSides){0, 2, 0, 0};
    commands[5].image.slice = (muiSides){0, 0, 0, 2};
    muiDrawList list = {.commands = commands, .commandCount = 6};
    list.header.scale = (float)scale;
    if (!Render(gpu, renderer, &list, side, pixels))
    {
        CHECK(false, "images drawn and read");
        return;
    }
    const int black[4] = {0, 0, 0, 255};
    const int red[4] = {255, 0, 0, 255};
    const int green[4] = {0, 255, 0, 255};
    const int blue[4] = {0, 0, 255, 255};
    const int half[4] = {0, 188, 0, 255};
    const int s = scale;
    CHECK(Near(pixels, side, 1 * s, 12 * s, red, 2) && Near(pixels, side, 39 * s, 12 * s, red, 2) &&
              Near(pixels, side, 20 * s, 1 * s, red, 2) &&
              Near(pixels, side, 20 * s, 23 * s, red, 2) &&
              Near(pixels, side, 8 * s, 12 * s, green, 2) &&
              Near(pixels, side, 20 * s, 5 * s, green, 2) &&
              Near(pixels, side, 41 * s, 12 * s, black, 2),
          "a nine slice's border kept 2 units wide, its middle stretched");
    CHECK(Near(pixels, side, 52 * s, 8 * s, half, 2), "an image tinted to half");
    CHECK(Near(pixels, side, 8 * s, 36 * s, blue, 2), "another texture's image between");
    CHECK(Near(pixels, side, 23 * s, 31 * s, green, 2) &&
              Near(pixels, side, 32 * s, 40 * s, green, 2),
          "a uv rect of the middle alone");
    CHECK(Near(pixels, side, 39 * s, 54 * s, red, 2) &&
              Near(pixels, side, 1 * s, 54 * s, green, 2) &&
              Near(pixels, side, 20 * s, 54 * s, green, 2) &&
              Near(pixels, side, 30 * s, 54 * s, green, 2),
          "mirrored: the red slice 2 units wide on the right");
    CHECK(Near(pixels, side, 45 * s, 54 * s, red, 2) &&
              Near(pixels, side, 58 * s, 54 * s, green, 2),
          "as it is: on the left");
}

// Tiling, an image's middle filled as CSS's border-image-repeat:
// - the red-sided image (red in its first 2 of 8 texels across)
//   repeated over 20, its tiles centred, so starting at -2, 6 and 14;
//   rounded, three tiles of 20 / 3; spaced, two with gaps of 4 / 3, and
//   none in a width of 6; and a repeat the renderer does not know,
//   stretched;
// - the quarters image repeated across 16 (tiles from -4, 4 and 12) and
//   rounded up 24 (three of 8);
// - the bordered image sliced at 2 over 15 by 12, its middle of 4
//   texels spaced across 11: tiles at 3 and 8, gaps of 1 in its top edge
//   and middle alike, its corners whole, its tiles' edge pixels green,
//   sampled half a texel inside the middle;
// - the mipmapped quarters repeated from 1.375 across 20, a seam at
//   7.375 within a 2 by 2 block of pixels at either scale, at no pixel's
//   centre: drawn from the first level, as gradients ignore the seam;
// - the quarters sliced at 2 in a rect 2 wide, all insets halved, its
//   left edge's middle repeated up 20 in tiles of 4 halved, and in a
//   rect 2 tall its top edge's across.
static void TestTiling(Gpu* gpu, muiRhiRenderer* renderer, uint8_t* pixels, int scale)
{
    const uint32_t side = 64u * (uint32_t)scale;
    muiDrawCommand commands[10] = {
        Image(3, (muiRect){0, 0, 20, 8}),       Image(3, (muiRect){0, 10, 20, 8}),
        Image(3, (muiRect){0, 20, 20, 8}),      Image(3, (muiRect){24, 0, 6, 8}),
        Image(4, (muiRect){40, 10, 16, 24}),    Image(1, (muiRect){0, 32, 15, 12}),
        Image(5, (muiRect){1.375f, 48, 20, 8}), Image(4, (muiRect){32, 10, 2, 22}),
        Image(3, (muiRect){24, 10, 6, 8}),      Image(4, (muiRect){24, 40, 22, 2}),
    };
    commands[0].image.repeatX = mui_imageRepeat;
    commands[1].image.repeatX = mui_imageRound;
    commands[2].image.repeatX = mui_imageSpace;
    commands[3].image.repeatX = mui_imageSpace;
    commands[4].image.repeatX = mui_imageRepeat;
    commands[4].image.repeatY = mui_imageRound;
    commands[5].image.slice = (muiSides){2, 2, 2, 2};
    commands[5].image.repeatX = mui_imageSpace;
    commands[6].image.repeatX = mui_imageRepeat;
    commands[7].image.slice = (muiSides){2, 2, 2, 2};
    commands[7].image.repeatY = mui_imageRepeat;
    commands[8].image.repeatX = 200;
    commands[9].image.slice = (muiSides){2, 2, 2, 2};
    commands[9].image.repeatX = mui_imageRepeat;
    muiDrawList list = {.commands = commands, .commandCount = 10};
    list.header.scale = (float)scale;
    if (!Render(gpu, renderer, &list, side, pixels))
    {
        CHECK(false, "tiled images drawn and read");
        return;
    }
    const int black[4] = {0, 0, 0, 255};
    const int red[4] = {255, 0, 0, 255};
    const int green[4] = {0, 255, 0, 255};
    const int blue[4] = {0, 0, 255, 255};
    const int white[4] = {255, 255, 255, 255};
    const int s = scale;
    CHECK(Near(pixels, side, 1 * s, 4 * s, green, 2) && Near(pixels, side, 7 * s, 4 * s, red, 2) &&
              Near(pixels, side, 10 * s, 4 * s, green, 2) &&
              Near(pixels, side, 15 * s, 4 * s, red, 2) &&
              Near(pixels, side, 18 * s, 4 * s, green, 2),
          "repeated, tiles centred");
    CHECK(Near(pixels, side, 0, 14 * s, red, 2) && Near(pixels, side, 4 * s, 14 * s, green, 2) &&
              Near(pixels, side, 7 * s, 14 * s, red, 2) &&
              Near(pixels, side, 11 * s, 14 * s, green, 2) &&
              Near(pixels, side, 14 * s, 14 * s, red, 2),
          "rounded, three tiles to the width");
    CHECK(Near(pixels, side, 0, 24 * s, black, 2) && Near(pixels, side, 2 * s, 24 * s, red, 2) &&
              Near(pixels, side, 10 * s, 24 * s, black, 2) &&
              Near(pixels, side, 11 * s, 24 * s, red, 2) &&
              Near(pixels, side, 15 * s, 24 * s, green, 2) &&
              Near(pixels, side, 19 * s, 24 * s, black, 2),
          "spaced, gaps before, between and after");
    CHECK(Near(pixels, side, 27 * s, 4 * s, black, 2), "spaced, no tile fits");
    CHECK(Near(pixels, side, 41 * s, 11 * s, green, 2) &&
              Near(pixels, side, 45 * s, 11 * s, red, 2) &&
              Near(pixels, side, 49 * s, 15 * s, white, 2) &&
              Near(pixels, side, 53 * s, 23 * s, blue, 2),
          "repeated across, rounded up");
    CHECK(Near(pixels, side, 0, 33 * s, red, 2) && Near(pixels, side, 4 * s, 33 * s, red, 2) &&
              Near(pixels, side, 7 * s, 33 * s, black, 2) &&
              Near(pixels, side, 14 * s, 33 * s, red, 2) &&
              Near(pixels, side, 4 * s, 37 * s, green, 2) &&
              Near(pixels, side, 7 * s, 37 * s, black, 2) &&
              Near(pixels, side, 12 * s, 37 * s, black, 2),
          "a 9-slice's middle spaced, its edge with it, its corners whole");
    CHECK(Near(pixels, side, 3 * s, 37 * s, green, 2) &&
              Near(pixels, side, 12 * s - 1, 37 * s, green, 2),
          "spaced tiles sample half a texel inside the middle");
    const int seam = (int)(7.5f * (float)s);
    CHECK(Near(pixels, side, seam - 1, 49 * s, green, 2) &&
              Near(pixels, side, seam, 49 * s, red, 2),
          "a seam samples the first level");
    CHECK(Near(pixels, side, 32 * s, 14 * s, red, 2) &&
              Near(pixels, side, 32 * s, 15 * s, blue, 2) &&
              Near(pixels, side, 28 * s, 40 * s, red, 2) &&
              Near(pixels, side, 29 * s, 40 * s, green, 2),
          "tiles shrink with the slices' fit");
    CHECK(Near(pixels, side, 24 * s, 14 * s, red, 2) &&
              Near(pixels, side, 28 * s, 14 * s, green, 2),
          "an unknown repeat stretches");
}

// Where a point of a list lands in a square target of a side, through a
// column-major projection: clip space's x right and y up, from -1 to 1.
static void Project(const float m[16], float x, float y, uint32_t side, int* px, int* py)
{
    float cx = m[0] * x + m[4] * y + m[12];
    float cy = m[1] * x + m[5] * y + m[13];
    float cw = m[3] * x + m[7] * y + m[15];
    *px = (int)floorf((cx / cw + 1.0f) * 0.5f * (float)side);
    *py = (int)floorf((1.0f - cy / cw) * 0.5f * (float)side);
}

// The projection that draws a list as the target's pixels at a scale do.
static void ScreenProjection(float m[16], float scale, uint32_t side)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f * scale / (float)side;
    m[5] = -2.0f * scale / (float)side;
    m[10] = 1.0f;
    m[12] = -1.0f;
    m[13] = 1.0f;
    m[15] = 1.0f;
}

// A 64-unit panel turned 60 degrees about its vertical middle, its right
// half toward the eye, seen in perspective from 48 units: a point x, y
// of it is at X = cos (x - 32), Y = 32 - y, Z = sin (x - 32), w = 48 - Z.
static void TurnedProjection(float m[16])
{
    const float c = 0.5f;
    const float s = 0.8660254f;
    memset(m, 0, 16 * sizeof(float));
    m[0] = c;
    m[12] = -32.0f * c;
    m[5] = -1.0f;
    m[13] = 32.0f;
    m[3] = -s;
    m[15] = 48.0f + 32.0f * s;
    m[2] = -0.5f * s;
    m[14] = 0.5f * (48.0f + 32.0f * s);
}

// A list of boxes, one rounded, and one in a round clip.
static muiDrawList PanelList(muiDrawCommand commands[4], const muiDrawClip clips[2], float scale)
{
    commands[0] = Box(4, 4, 24, 24, (muiLinearColor){1, 0, 0, 1});
    commands[0].box.radii = (muiCorners){6, 6, 6, 6};
    commands[1] = Box(36, 4, 24, 24, (muiLinearColor){0, 1, 0, 1});
    commands[2] = Box(4, 36, 24, 24, (muiLinearColor){0, 0, 1, 1});
    commands[3] = Box(36, 36, 24, 24, (muiLinearColor){1, 1, 1, 1});
    commands[3].clip = 1;
    muiDrawList list = {.commands = commands, .commandCount = 4};
    list.clips = clips;
    list.clipCount = 2;
    list.header.scale = scale;
    list.header.width = 64.0f;
    list.header.height = 64.0f;
    return list;
}

// Projection (record mui-0005): a projection equal to the target's own
// mapping draws what no projection draws, within 2, edges and clips
// included; and a panel turned in perspective puts each box, and its
// outer edges, where the matrix takes them.
static void TestProjection(Gpu* gpu, muiRhiRenderer* renderer, uint8_t* pixels, int scale)
{
    const uint32_t side = 64u * (uint32_t)scale;
    const size_t size = (size_t)side * side * 4;
    muiDrawCommand commands[4];
    const muiDrawClip clips[2] = {{0}, {.rect = {36, 36, 24, 24}, .radii = {12, 12, 12, 12}}};
    muiDrawList list = PanelList(commands, clips, (float)scale);
    uint8_t* plain = malloc(size);
    float m[16];
    ScreenProjection(m, (float)scale, side);
    if (plain == NULL || !Render(gpu, renderer, &list, side, plain) ||
        !RenderThrough(gpu, renderer, &list, side, m, pixels))
    {
        free(plain);
        CHECK(false, "a list drawn with and without a projection");
        return;
    }
    int worst = 0;
    for (size_t i = 0; i < size; i++)
    {
        int d = abs((int)plain[i] - (int)pixels[i]);
        worst = d > worst ? d : worst;
    }
    free(plain);
    CHECK(worst <= 2, "the screen's own projection draws the same pixels");
    list.header.scale = 1.0f;
    TurnedProjection(m);
    if (!RenderThrough(gpu, renderer, &list, side, m, pixels))
    {
        CHECK(false, "a turned panel drawn");
        return;
    }
    const int black[4] = {0, 0, 0, 255};
    const int red[4] = {255, 0, 0, 255};
    const int green[4] = {0, 255, 0, 255};
    const int blue[4] = {0, 0, 255, 255};
    const int white[4] = {255, 255, 255, 255};
    int x[4];
    int y[4];
    const float centres[4][2] = {{16, 16}, {48, 16}, {16, 48}, {48, 48}};
    for (int i = 0; i < 4; i++)
    {
        Project(m, centres[i][0], centres[i][1], side, &x[i], &y[i]);
    }
    CHECK(Near(pixels, side, x[0], y[0], red, 2) && Near(pixels, side, x[1], y[1], green, 2) &&
              Near(pixels, side, x[2], y[2], blue, 2) && Near(pixels, side, x[3], y[3], white, 2),
          "each box where the projection takes its centre");
    int gapX = 0;
    int gapY = 0;
    Project(m, 32, 16, side, &gapX, &gapY);
    int cornerX = 0;
    int cornerY = 0;
    Project(m, 37.5f, 37.5f, side, &cornerX, &cornerY);
    CHECK(Near(pixels, side, gapX, gapY, black, 2) &&
              Near(pixels, side, cornerX, cornerY, black, 2),
          "the gap between boxes, and the clip's cut corner, clear");
    // The outer edges where the matrix takes them, probed 2 pixels in
    // and out: a far unit covers a fifth of a pixel, a near one more.
    int left = 0;
    int leftY = 0;
    int right = 0;
    int rightY = 0;
    Project(m, 4, 16, side, &left, &leftY);
    Project(m, 60, 16, side, &right, &rightY);
    CHECK(Near(pixels, side, left + 2, leftY, red, 2) &&
              Near(pixels, side, left - 2, leftY, black, 2) &&
              Near(pixels, side, right - 2, rightY, green, 2) &&
              Near(pixels, side, right + 2, rightY, black, 2),
          "the outer edges where perspective puts them");
}

// Projection that scales: a matrix scaling by 2 draws a list of scale 1
// as a scale of 2 draws it, within 2, edge and clip widths included
// (their pixels a unit come from the fragment, not the list); and one
// scaling by a half brings a box past the target's own size into view,
// as culling by the target alone would not.
static void TestProjectionScale(Gpu* gpu, muiRhiRenderer* renderer, uint8_t* pixels)
{
    const uint32_t side = 128u;
    const size_t size = (size_t)side * side * 4;
    muiDrawCommand commands[5];
    const muiDrawClip clips[2] = {{0}, {.rect = {36, 36, 24, 24}, .radii = {12, 12, 12, 12}}};
    muiDrawList list = PanelList(commands, clips, 2.0f);
    uint8_t* plain = malloc(size);
    float m[16];
    ScreenProjection(m, 2.0f, side);
    bool drawn = plain != NULL && Render(gpu, renderer, &list, side, plain);
    list.header.scale = 1.0f;
    drawn = drawn && RenderThrough(gpu, renderer, &list, side, m, pixels);
    int worst = drawn ? 0 : 255;
    for (size_t i = 0; drawn && i < size; i++)
    {
        int d = abs((int)plain[i] - (int)pixels[i]);
        worst = d > worst ? d : worst;
    }
    free(plain);
    CHECK(worst <= 2, "a projection scaling by 2 draws as a scale of 2");
    commands[4] = Box(80, 80, 20, 20, (muiLinearColor){1, 1, 0, 1});
    list.commandCount = 5;
    ScreenProjection(m, 0.5f, 64);
    const int yellow[4] = {255, 255, 0, 255};
    CHECK(RenderThrough(gpu, renderer, &list, 64, m, pixels) && Near(pixels, 64, 45, 45, yellow, 2),
          "a box past the target's size brought into view");
}

// Whether a renderer refuses a target, in a frame then dropped.
static bool Refuses(Gpu* gpu, muiRhiRenderer* renderer, const muiDrawList* list, bool projected,
                    bool depth)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8UnormSrgb;
    def.width = 64;
    def.height = 64;
    muiRhiTarget into = {.width = 64, .height = 64, .projected = projected};
    ScreenProjection(into.projection, 1.0f, 64);
    if (mrhiBeginFrame(gpu->device, &frame) != mrhi_success ||
        mrhiDeclareTexture(gpu->device, &def, &into.resource) != mrhi_success)
    {
        return false;
    }
    def.format = mrhi_formatDepth32Float;
    bool declared = !depth || mrhiDeclareTexture(gpu->device, &def, &into.depth) == mrhi_success;
    bool refused = declared && muiRhiRenderer_AddPasses(renderer, list, &into) == mui_errorInvalid;
    (void)mrhiDropFrame(gpu->device);
    return refused;
}

// Depth (record mui-0005): a renderer made with a depth format tests a
// projected panel against the host's depth, never writing it: the turned
// panel, at depth 0.5 throughout (its matrix's z is half its w), drawn
// over a depth of 0.6 and hidden by one of 0.4 under less-or-equal. A
// depth texture without a projection, or given a renderer made without a
// depth format, is refused.
static void TestDepth(Gpu* gpu, muiRhiRenderer* plain, uint8_t* pixels)
{
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu->device;
    def.depthFormat = mrhi_formatDepth32Float;
    muiRhiRenderer* renderer = NULL;
    if (muiCreateRhiRenderer(&def, &renderer) != mui_success || !AwaitReady(gpu, renderer))
    {
        muiDestroyRhiRenderer(renderer);
        CHECK(false, "a renderer testing depth, ready");
        return;
    }
    muiDrawCommand commands[4];
    const muiDrawClip clips[2] = {{0}, {.rect = {36, 36, 24, 24}, .radii = {12, 12, 12, 12}}};
    muiDrawList list = PanelList(commands, clips, 1.0f);
    float m[16];
    TurnedProjection(m);
    int x = 0;
    int y = 0;
    Project(m, 48, 16, 64, &x, &y);
    const int black[4] = {0, 0, 0, 255};
    const int green[4] = {0, 255, 0, 255};
    CHECK(RenderInDepth(gpu, renderer, &list, 64, m, 0.6f, pixels) &&
              Near(pixels, 64, x, y, green, 2),
          "a panel in front of the scene's depth drawn");
    CHECK(RenderInDepth(gpu, renderer, &list, 64, m, 0.4f, pixels) &&
              Near(pixels, 64, x, y, black, 2),
          "a panel behind it hidden");
    CHECK(Refuses(gpu, renderer, &list, false, true) && Refuses(gpu, plain, &list, true, true) &&
              !Refuses(gpu, renderer, &list, true, false),
          "depth without a projection or a depth format refused");
    muiDestroyRhiRenderer(renderer);
}

#if MUI_TEST_TEXT

#include "maul-ui/font.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"

#include "ahem.inc"
#include "color.inc"

// A text service with Ahem, its key in fontOut.
// A text service with Ahem and Maul Color, their keys in fontOut and
// colorOut.
static muiTextService* MakeText(uint64_t* fontOut, uint64_t* colorOut)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    font.dataMode = mui_fontDataBorrow;
    muiFontDef color = font;
    color.data = s_color;
    color.size = sizeof s_color;
    muiFontId ahem = {0, 0};
    muiFontId colorId = {0, 0};
    if (muiCreateTextService(&def, &service) != mui_success ||
        muiCreateFont(service, &font, &ahem) != mui_success ||
        muiCreateFont(service, &color, &colorId) != mui_success)
    {
        muiDestroyTextService(service);
        return NULL;
    }
    *fontOut = muiFont_GetKey(ahem);
    *colorOut = muiFont_GetKey(colorId);
    return service;
}

// A glyph run on the turned panel's near half, drawn from its distance
// field: the square of glyph 4 at an em of 30 (34 to 64 across, 26 to 50
// down) where the matrix takes its middle, and clear above it.
static void TestProjectedGlyphs(Gpu* gpu, muiRhiRenderer* renderer, uint64_t font, uint8_t* pixels)
{
    const uint32_t side = 64u;
    const muiGlyph glyphs[1] = {{4, 0, 0}};
    muiDrawCommand command = {.kind = mui_drawGlyphRun};
    command.glyphRun = (muiDrawGlyphRun){.font = font,
                                         .originX = 34,
                                         .originY = 50,
                                         .size = 30,
                                         .glyphCount = 1,
                                         .color = {0, 1, 0, 1}};
    muiDrawList list = {.commands = &command, .commandCount = 1};
    list.glyphs = glyphs;
    list.glyphCount = 1;
    list.header.scale = 1.0f;
    float m[16];
    TurnedProjection(m);
    if (!RenderThrough(gpu, renderer, &list, side, m, pixels))
    {
        CHECK(false, "a projected run drawn");
        return;
    }
    const int black[4] = {0, 0, 0, 255};
    const int green[4] = {0, 255, 0, 255};
    int x = 0;
    int y = 0;
    int aboveX = 0;
    int aboveY = 0;
    Project(m, 49, 38, side, &x, &y);
    Project(m, 49, 18, side, &aboveX, &aboveY);
    CHECK(Near(pixels, side, x, y, green, 2) && Near(pixels, side, aboveX, aboveY, black, 2),
          "a projected glyph from its field");
    // Magnified 2 times, its square (4 to 14 across, 12 to 22 down at an
    // em of 10) stays solid to a pixel inside its edge, as a field draws
    // it and coverage drawn at the list's pixels, magnified, would not.
    command.glyphRun.originX = 4;
    command.glyphRun.originY = 20;
    command.glyphRun.size = 10;
    ScreenProjection(m, 2.0f, 128);
    CHECK(RenderThrough(gpu, renderer, &list, 128, m, pixels) &&
              Near(pixels, 128, 8, 34, green, 2) && Near(pixels, 128, 27, 34, green, 2) &&
              Near(pixels, 128, 6, 34, black, 2),
          "a magnified projected glyph sharp to its edge");
}

static void TestGlyphs(Gpu* gpu, muiRhiRenderer* renderer, uint64_t font, uint8_t* pixels,
                       int scale)
{
    const uint32_t side = 64u * (uint32_t)scale;
    // Glyph 4 is a square: two at 0 and 15 from the origin.
    const muiGlyph glyphs[2] = {{4, 0, 0}, {4, 15, 0}};
    const muiDrawTransform transforms[2] = {{1, 0, 0, 1, 0, 0}, {1, 0, 0, 1, 30, 30}};
    muiDrawCommand commands[2] = {{.kind = mui_drawGlyphRun}, {.kind = mui_drawGlyphRun}};
    commands[0].glyphRun = (muiDrawGlyphRun){.font = font,
                                             .originX = 4,
                                             .originY = 20,
                                             .size = 10,
                                             .glyphCount = 2,
                                             .color = {0, 1, 0, 1}};
    commands[1].glyphRun = (muiDrawGlyphRun){.font = font,
                                             .originX = 4,
                                             .originY = 20,
                                             .size = 10,
                                             .glyphCount = 1,
                                             .color = {0, 0, 1, 1}};
    commands[1].transform = 1;
    muiDrawList list = {.commands = commands, .commandCount = 2};
    list.glyphs = glyphs;
    list.glyphCount = 2;
    list.transforms = transforms;
    list.transformCount = 2;
    list.header.scale = (float)scale;
    if (!Render(gpu, renderer, &list, side, pixels))
    {
        CHECK(false, "glyphs drawn and read");
        return;
    }
    const int black[4] = {0, 0, 0, 255};
    const int green[4] = {0, 255, 0, 255};
    const int blue[4] = {0, 0, 255, 255};
    const int s = scale;
    // The squares span 4 to 14 and 19 to 29 across, 12 to 22 down.
    CHECK(Near(pixels, side, 4 * s, 12 * s, green, 2) &&
              Near(pixels, side, 13 * s, 21 * s, green, 2) &&
              Near(pixels, side, 3 * s, 16 * s, black, 2) &&
              Near(pixels, side, 8 * s, 11 * s, black, 2) &&
              Near(pixels, side, 8 * s, 22 * s, black, 2) &&
              Near(pixels, side, 16 * s, 16 * s, black, 2) &&
              Near(pixels, side, 22 * s, 16 * s, green, 2),
          "two squares of Ahem and the gap between them");
    CHECK(Near(pixels, side, 38 * s, 46 * s, blue, 2) &&
              Near(pixels, side, 33 * s, 46 * s, black, 2) &&
              Near(pixels, side, 8 * s, 16 * s, green, 2),
          "a run a transform moves");
}

static void TestFields(Gpu* gpu, muiRhiRenderer* renderer, uint64_t font, uint8_t* pixels,
                       int scale)
{
    const uint32_t side = 64u * (uint32_t)scale;
    const muiGlyph square = {4, 0, 0};
    // Scaled by 2 and moved down 30; turned clockwise a quarter and
    // moved right 60.
    const muiDrawTransform transforms[3] = {
        {1, 0, 0, 1, 0, 0}, {2, 0, 0, 2, 0, 30}, {0, 1, -1, 0, 60, 0}};
    muiDrawCommand commands[2] = {{.kind = mui_drawGlyphRun}, {.kind = mui_drawGlyphRun}};
    for (int i = 0; i < 2; i++)
    {
        commands[i].glyphRun = (muiDrawGlyphRun){
            .font = font, .originX = 4, .originY = 8, .size = 10, .glyphCount = 1};
        commands[i].transform = (uint32_t)i + 1;
    }
    commands[0].glyphRun.color = (muiLinearColor){1, 0, 0, 1};
    commands[1].glyphRun.color = (muiLinearColor){0, 0, 1, 1};
    muiDrawList list = {.commands = commands, .commandCount = 2};
    list.glyphs = &square;
    list.glyphCount = 1;
    list.transforms = transforms;
    list.transformCount = 3;
    list.header.scale = (float)scale;
    if (!Render(gpu, renderer, &list, side, pixels))
    {
        CHECK(false, "fields drawn and read");
        return;
    }
    const int black[4] = {0, 0, 0, 255};
    const int red[4] = {255, 0, 0, 255};
    const int blue[4] = {0, 0, 255, 255};
    const int s = scale;
    // The square, 4 to 14 across and 0 to 10 down, scaled: 8 to 28 and
    // 30 to 50.
    CHECK(Near(pixels, side, 18 * s, 40 * s, red, 2) && Near(pixels, side, 9 * s, 31 * s, red, 2) &&
              Near(pixels, side, 6 * s, 40 * s, black, 2) &&
              Near(pixels, side, 30 * s, 40 * s, black, 2) &&
              Near(pixels, side, 18 * s, 28 * s, black, 2) &&
              Near(pixels, side, 18 * s, 51 * s, black, 2),
          "a run scaled by 2, from its field");
    // Turned: 50 to 60 across, 4 to 14 down.
    CHECK(Near(pixels, side, 55 * s, 9 * s, blue, 2) &&
              Near(pixels, side, 51 * s, 5 * s, blue, 2) &&
              Near(pixels, side, 47 * s, 9 * s, black, 2) &&
              Near(pixels, side, 55 * s, 16 * s, black, 2) &&
              Near(pixels, side, 55 * s, 2 * s, black, 2),
          "a run turned a quarter, from its field");
}

// The red of a pixel.
static int Red(const uint8_t* pixels, uint32_t side, int x, int y)
{
    return pixels[((size_t)y * side + (size_t)x) * 4];
}

// Each corner of the square magnified: scaled by 40, its em drawn at 400
// pixels from a field of 128, so a field pixel spans 3 device pixels and
// more, and moved so the corner is at 32, 32. Coverage grows with the
// distance read, and a sharp corner's is the lesser of its two sides',
// so the pixels at the corner, inside and out, are covered as the lesser
// of the pixels beside the two sides in their column and row; a
// one-channel field, rounding the corner by a fifth of a field pixel, or
// a channel holding one side's colour alone, covers them otherwise.
static void TestFieldCorners(Gpu* gpu, muiRhiRenderer* renderer, uint64_t font, uint8_t* pixels,
                             int scale)
{
    const uint32_t side = 64u * (uint32_t)scale;
    const muiGlyph square = {4, 0, 0};
    const int red[4] = {255, 0, 0, 255};
    const int c = 32 * scale;
    for (int corner = 0; corner < 4; corner++)
    {
        // Which way the square lies from the corner, and its corner in
        // the run's units scaled.
        int inX = corner % 2 == 0 ? 1 : -1;
        int inY = corner < 2 ? 1 : -1;
        float x = inX > 0 ? 0.0f : 400.0f;
        float y = inY > 0 ? 0.0f : 400.0f;
        const muiDrawTransform transforms[2] = {{1, 0, 0, 1, 0, 0},
                                                {40, 0, 0, 40, 32.0f - x, 32.0f - y}};
        muiDrawCommand command = {.kind = mui_drawGlyphRun, .transform = 1};
        command.glyphRun = (muiDrawGlyphRun){.font = font,
                                             .originX = 0,
                                             .originY = 8,
                                             .size = 10,
                                             .glyphCount = 1,
                                             .color = {1, 0, 0, 1}};
        muiDrawList list = {.commands = &command, .commandCount = 1};
        list.glyphs = &square;
        list.glyphCount = 1;
        list.transforms = transforms;
        list.transformCount = 2;
        list.header.scale = (float)scale;
        if (!Render(gpu, renderer, &list, side, pixels))
        {
            CHECK(false, "a corner drawn and read");
            return;
        }
        // The pixels inside the corner and outside it, and pixels well
        // along each side.
        int inside = inX > 0 ? c : c - 1;
        int insideY = inY > 0 ? c : c - 1;
        int farX = c + inX * 10 * scale;
        int farY = c + inY * 10 * scale;
        bool sharp = true;
        for (int k = 0; k < 2; k++)
        {
            int px = k == 0 ? inside : inside - inX;
            int py = k == 0 ? insideY : insideY - inY;
            int column = Red(pixels, side, px, farY);
            int row = Red(pixels, side, farX, py);
            int expected = column < row ? column : row;
            int got = Red(pixels, side, px, py);
            if (got < expected - 3 || got > expected + 3)
            {
                printf("  corner %d at %d,%d: red %d, expected %d\n", corner, px, py, got,
                       expected);
                sharp = false;
            }
        }
        CHECK(sharp && Red(pixels, side, inside, farY) > 128 &&
                  Near(pixels, side, farX, farY, red, 2),
              "a magnified corner kept");
        // At device scale 1 each side falls on a pixel edge: the pixels
        // either side of it are covered and not, to a byte step of the
        // field, a 32nd of a field pixel, 3 device pixels and more.
        bool placed = scale != 1 || (Red(pixels, side, inside, farY) >= 250 &&
                                     Red(pixels, side, inside - inX, farY) <= 40 &&
                                     Red(pixels, side, farX, insideY) >= 250 &&
                                     Red(pixels, side, farX, insideY - inY) <= 40);
        CHECK(placed, "a magnified side where the outline is");
    }
}

// Maul Color's A (layers of red, half clear blue over its right half, and
// a small box of the text's colour): moved, at origin 4, 18, its box 5 to
// 13 across and 10 to 18 down, the small box 7 to 9 and 14 to 16; again
// at half alpha, at origin 4, 38; and scaled by 2 and moved right 30, at
// origin 0, 8, its box 32 to 48 and 0 to 16, drawn from an image at the
// em drawn. The text is green, so its box is.
static void TestColorGlyphs(Gpu* gpu, muiRhiRenderer* renderer, uint64_t font, uint8_t* pixels,
                            int scale)
{
    const uint32_t side = 64u * (uint32_t)scale;
    const muiGlyph glyph = {1, 0, 0};
    const muiDrawTransform transforms[2] = {{1, 0, 0, 1, 0, 0}, {2, 0, 0, 2, 30, 0}};
    muiDrawCommand commands[3];
    const float origins[3][2] = {{4, 18}, {4, 38}, {0, 8}};
    const muiLinearColor colors[3] = {{0, 1, 0, 1}, {0, 0.5f, 0, 0.5f}, {0, 1, 0, 1}};
    for (int i = 0; i < 3; i++)
    {
        commands[i] = (muiDrawCommand){.kind = mui_drawGlyphRun, .transform = i == 2 ? 1u : 0u};
        commands[i].glyphRun = (muiDrawGlyphRun){.font = font,
                                                 .originX = origins[i][0],
                                                 .originY = origins[i][1],
                                                 .size = 10,
                                                 .glyphCount = 1,
                                                 .color = colors[i]};
    }
    muiDrawList list = {.commands = commands, .commandCount = 3};
    list.glyphs = &glyph;
    list.glyphCount = 1;
    list.transforms = transforms;
    list.transformCount = 2;
    list.header.scale = (float)scale;
    if (!Render(gpu, renderer, &list, side, pixels))
    {
        CHECK(false, "colour glyphs drawn and read");
        return;
    }
    const int black[4] = {0, 0, 0, 255};
    const int red[4] = {255, 0, 0, 255};
    const int mixed[4] = {187, 0, 188, 255};
    const int green[4] = {0, 255, 0, 255};
    // Linear 0.5 over black: 188.
    const int halfRed[4] = {188, 0, 0, 255};
    const int halfGreen[4] = {0, 188, 0, 255};
    const int s = scale;
    CHECK(Near(pixels, side, 6 * s, 11 * s, red, 3) &&
              Near(pixels, side, 11 * s, 11 * s, mixed, 3) &&
              Near(pixels, side, 8 * s, 15 * s, green, 3) &&
              Near(pixels, side, 3 * s, 11 * s, black, 2) &&
              Near(pixels, side, 14 * s, 11 * s, black, 2),
          "a colour glyph's layers and the text's colour");
    CHECK(Near(pixels, side, 6 * s, 31 * s, halfRed, 4) &&
              Near(pixels, side, 8 * s, 35 * s, halfGreen, 4),
          "a colour glyph at the run's alpha");
    CHECK(Near(pixels, side, 34 * s, 4 * s, red, 3) &&
              Near(pixels, side, 44 * s, 4 * s, mixed, 3) &&
              Near(pixels, side, 38 * s, 10 * s, green, 3) &&
              Near(pixels, side, 30 * s, 8 * s, black, 2),
          "a colour glyph scaled, from an image at the em drawn");
}

#endif

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
            return Exit(1);
        }
        printf("no adapter: skipped\n");
        return Exit(77);
    }
    Textures textures = {0};
    CHECK(MakeTextures(&gpu, &textures), "the test's textures");
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    def.image = FindImage;
    def.imageContext = &textures;
#if MUI_TEST_TEXT
    uint64_t font = 0;
    uint64_t color = 0;
    def.text = MakeText(&font, &color);
    CHECK(def.text != NULL, "a text service with Ahem and Maul Color");
#endif
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
        TestConic(&gpu, renderer, pixels);
        TestClipsAndTransforms(&gpu, renderer, pixels, 1);
        TestClipsAndTransforms(&gpu, renderer, pixels, 2);
        TestImages(&gpu, renderer, pixels, 1);
        TestImages(&gpu, renderer, pixels, 2);
        TestTiling(&gpu, renderer, pixels, 1);
        TestTiling(&gpu, renderer, pixels, 2);
        TestProjection(&gpu, renderer, pixels, 1);
        TestProjection(&gpu, renderer, pixels, 2);
        TestProjectionScale(&gpu, renderer, pixels);
        TestDepth(&gpu, renderer, pixels);
#if MUI_TEST_TEXT
        TestGlyphs(&gpu, renderer, font, pixels, 1);
        TestGlyphs(&gpu, renderer, font, pixels, 2);
        TestProjectedGlyphs(&gpu, renderer, font, pixels);
        TestFields(&gpu, renderer, font, pixels, 1);
        TestFields(&gpu, renderer, font, pixels, 2);
        TestFieldCorners(&gpu, renderer, font, pixels, 1);
        TestFieldCorners(&gpu, renderer, font, pixels, 2);
        TestColorGlyphs(&gpu, renderer, color, pixels, 1);
        TestColorGlyphs(&gpu, renderer, color, pixels, 2);
#endif
    }
    free(pixels);
    muiDestroyRhiRenderer(renderer);
    if (textures.bordered.index1 != 0)
    {
        (void)mrhiDestroyTexture(gpu.device, textures.bordered);
    }
    if (textures.blue.index1 != 0)
    {
        (void)mrhiDestroyTexture(gpu.device, textures.blue);
    }
    if (textures.sided.index1 != 0)
    {
        (void)mrhiDestroyTexture(gpu.device, textures.sided);
    }
    if (textures.quads.index1 != 0)
    {
        (void)mrhiDestroyTexture(gpu.device, textures.quads);
    }
    if (textures.mipped.index1 != 0)
    {
        (void)mrhiDestroyTexture(gpu.device, textures.mipped);
    }
#if MUI_TEST_TEXT
    muiDestroyTextService(def.text);
#endif
    Close(&gpu);
    return Exit(s_failures == 0 ? 0 : 1);
}
