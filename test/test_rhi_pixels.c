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
    return false;
}

// A texture of a side filled with RGBA8 texels in a frame of its own.
static bool MakeTexture(Gpu* gpu, uint32_t side, const uint8_t* texels, mrhiTextureId* textureOut)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8UnormSrgb;
    def.width = side;
    def.height = side;
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
                              .range = {.mipCount = 1, .layerCount = 1}};
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.passClass = mrhi_passTransfer;
    passDef.accesses = &write;
    passDef.accessCount = 1;
    passDef.neverCull = true;
    mrhiPassId pass = {0};
    const mrhiTextureCopy into = {.resource = resource};
    const mrhiTexelLayout layout = {.bytesPerRow = side * 4};
    const mrhiExtent3d extent = {side, side, 1};
    mrhiRequestId token = {0};
    bool recorded = mrhiAddPass(gpu->device, &passDef, &pass) == mrhi_success &&
                    mrhiCompileFrame(gpu->device) == mrhi_success &&
                    mrhiBeginPass(gpu->device, pass) == mrhi_success &&
                    mrhiWriteTexture(gpu->device, pass, &into, texels, (size_t)side * side * 4,
                                     &layout, &extent) == mrhi_success &&
                    mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    return mrhiSubmitFrame(gpu->device, &token) == mrhi_success &&
           mrhiWaitFrame(gpu->device, token, WAIT_NS) == mrhi_success;
}

static bool MakeTextures(Gpu* gpu, Textures* textures)
{
    // A red frame two texels wide around green; red on the left two
    // columns alone, which a mirror moves.
    uint8_t bordered[8 * 8 * 4];
    uint8_t sided[8 * 8 * 4];
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            bool border = x < 2 || x > 5 || y < 2 || y > 5;
            const uint8_t texel[4] = {border ? 255 : 0, border ? 0 : 255, 0, 255};
            memcpy(&bordered[(y * 8 + x) * 4], texel, 4);
            const uint8_t side[4] = {x < 2 ? 255 : 0, x < 2 ? 0 : 255, 0, 255};
            memcpy(&sided[(y * 8 + x) * 4], side, 4);
        }
    }
    const uint8_t blue[2 * 2 * 4] = {0, 0, 255, 255, 0, 0, 255, 255,
                                     0, 0, 255, 255, 0, 0, 255, 255};
    return MakeTexture(gpu, 8, bordered, &textures->bordered) &&
           MakeTexture(gpu, 2, blue, &textures->blue) &&
           MakeTexture(gpu, 8, sided, &textures->sided);
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

#if MUI_TEST_TEXT

#include "maul-ui/font.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"

#include "ahem.inc"

// A text service with Ahem, its key in fontOut.
static muiTextService* MakeText(uint64_t* fontOut)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    font.dataMode = mui_fontDataBorrow;
    muiFontId ahem = {0, 0};
    if (muiCreateTextService(&def, &service) != mui_success ||
        muiCreateFont(service, &font, &ahem) != mui_success)
    {
        muiDestroyTextService(service);
        return NULL;
    }
    *fontOut = muiFont_GetKey(ahem);
    return service;
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
            return 1;
        }
        printf("no adapter: skipped\n");
        return 77;
    }
    Textures textures = {0};
    CHECK(MakeTextures(&gpu, &textures), "the test's textures");
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = gpu.device;
    def.image = FindImage;
    def.imageContext = &textures;
#if MUI_TEST_TEXT
    uint64_t font = 0;
    def.text = MakeText(&font);
    CHECK(def.text != NULL, "a text service with Ahem");
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
#if MUI_TEST_TEXT
        TestGlyphs(&gpu, renderer, font, pixels, 1);
        TestGlyphs(&gpu, renderer, font, pixels, 2);
        TestFields(&gpu, renderer, font, pixels, 1);
        TestFields(&gpu, renderer, font, pixels, 2);
        TestFieldCorners(&gpu, renderer, font, pixels, 1);
        TestFieldCorners(&gpu, renderer, font, pixels, 2);
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
#if MUI_TEST_TEXT
    muiDestroyTextService(def.text);
#endif
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
