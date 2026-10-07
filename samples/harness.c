// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The samples' harness (record mui-0005). On the web a browser answers
// only between the page's tasks, so the harness sleeps there while it
// waits (JSPI), as Maul RHI's samples do.

#include "harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// How long an answer is waited for: 10 s natively, 10,000 sleeps of a
// millisecond or more on the web.
#define WAIT_NS     10000000000ull
#define WAIT_SLEEPS 10000

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

int SampleOpen(Sample* sample)
{
    *sample = (Sample){0};
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    size_t count = 0;
    bool found = mrhiCreateInstance(&def, &sample->instance) == mrhi_success &&
                 mrhiRequestAdapters(sample->instance, &search, &request) == mrhi_success &&
                 NextInstance(sample->instance, &record) == mrhi_success &&
                 record.outcome == mrhi_success &&
                 mrhiGetAdapters(sample->instance, &sample->adapter, 1, &count) == mrhi_success &&
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
    deviceDef.adapter = sample->adapter;
    if (mrhiCreateDevice(sample->instance, &deviceDef, &sample->device, &request) != mrhi_success ||
        NextInstance(sample->instance, &record) != mrhi_success || record.outcome != mrhi_success)
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
#ifdef __EMSCRIPTEN__
        emscripten_sleep(1);
#endif
    }
    return muiRhiRenderer_IsReady(renderer);
}

bool SamplePump(Sample* sample, muiRhiRenderer* renderer)
{
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(sample->device, &record) == mrhi_success)
    {
        (void)muiRhiRenderer_Notify(renderer, &record);
    }
    return muiRhiRenderer_IsReady(renderer);
}

int SampleExit(int status)
{
#ifdef __EMSCRIPTEN__
    // The web runner ends the page's run on this line.
    printf("mui-test: exit %d\n", status);
#endif
    return status;
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
           WaitFrame(sample->device, token) == mrhi_success;
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
    if (mrhiSubmitFrame(device, &token) != mrhi_success || WaitFrame(device, token) != mrhi_success)
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

// The source chained on a surface def, by the window's platform.
typedef union Source
{
    mrhiChain chain;
    mrhiSurfaceSourceWin32 win32;
    mrhiSurfaceSourceWayland wayland;
    mrhiSurfaceSourceXcb xcb;
    mrhiSurfaceSourceAndroid android;
    mrhiSurfaceSourceMetalLayer metal;
    mrhiSurfaceSourceCanvas canvas;
} Source;

static bool SourceOf(const mwinNativeHandles* handles, Source* source)
{
    switch (handles->platform)
    {
    case mwin_platformWin32:
        source->win32 = (mrhiSurfaceSourceWin32){{NULL, mrhi_structSurfaceSourceWin32},
                                                 handles->handles.win32.hinstance,
                                                 handles->handles.win32.hwnd};
        return true;
    case mwin_platformWayland:
        source->wayland = (mrhiSurfaceSourceWayland){{NULL, mrhi_structSurfaceSourceWayland},
                                                     handles->handles.wayland.display,
                                                     handles->handles.wayland.surface};
        return true;
    case mwin_platformX11:
        source->xcb = (mrhiSurfaceSourceXcb){{NULL, mrhi_structSurfaceSourceXcb},
                                             handles->handles.x11.connection,
                                             handles->handles.x11.window};
        return true;
    case mwin_platformAndroid:
        source->android = (mrhiSurfaceSourceAndroid){{NULL, mrhi_structSurfaceSourceAndroid},
                                                     handles->handles.android.window};
        return true;
    case mwin_platformMacOS:
    case mwin_platformIOS:
        source->metal = (mrhiSurfaceSourceMetalLayer){{NULL, mrhi_structSurfaceSourceMetalLayer},
                                                      handles->handles.apple.layer};
        return true;
    case mwin_platformWeb:
        source->canvas = (mrhiSurfaceSourceCanvas){{NULL, mrhi_structSurfaceSourceCanvas},
                                                   handles->handles.web.selector,
                                                   handles->handles.web.selectorLength};
        return true;
    default:
        return false;
    }
}

// The sRGB twin of an 8-bit unorm format, or none.
static mrhiFormat TwinOf(mrhiFormat format)
{
    switch (format)
    {
    case mrhi_formatRgba8Unorm:
        return mrhi_formatRgba8UnormSrgb;
    case mrhi_formatBgra8Unorm:
        return mrhi_formatBgra8UnormSrgb;
    default:
        return mrhi_formatNone;
    }
}

// The first colour the surface reports whose sRGB twin the images may
// take, which the renderer encodes into.
// The surface's colour, and the format frames draw in: the sRGB twin of
// a reported 8-bit colour, as the images' own format where the surface
// allows it; else, where its images take copies (a WebGPU canvas, whose
// twin is a view only), the colour itself, frames drawn into a staging
// texture in the twin whose bytes are copied over.
static bool ColorOf(const mrhiSurfaceCaps* caps, mrhiSurfaceColor* colorOut,
                    mrhiFormat* drawFormatOut)
{
    bool stages = (caps->usages & mrhi_textureCopyDestination) != 0;
    for (uint32_t i = 0; (caps->twinImages || stages) && i < caps->colorCount; i++)
    {
        mrhiFormat twin = TwinOf(caps->colors[i].format);
        if (twin != mrhi_formatNone && caps->colors[i].primaries == mrhi_primariesBt709)
        {
            *colorOut = caps->colors[i];
            colorOut->format = caps->twinImages ? twin : caps->colors[i].format;
            *drawFormatOut = twin;
            return true;
        }
    }
    return false;
}

bool SampleSurfaceOpen(Sample* sample, const mwinNativeHandles* handles, uint32_t width,
                       uint32_t height, SampleSurface* surfaceOut)
{
    *surfaceOut = (SampleSurface){0};
    Source source;
    if (!SourceOf(handles, &source))
    {
        printf("FAIL: no surface source for the window's platform\n");
        return false;
    }
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &source.chain;
    mrhiSurfaceCaps caps;
    if (mrhiCreateSurface(sample->instance, &def, &surfaceOut->surface) != mrhi_success ||
        mrhiGetSurfaceCaps(sample->instance, surfaceOut->surface, sample->adapter, &caps) !=
            mrhi_success ||
        !caps.presentable)
    {
        printf("FAIL: the adapter cannot present to the window\n");
        return false;
    }
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = surfaceOut->surface;
    if (!ColorOf(&caps, &config.color, &surfaceOut->drawFormat))
    {
        printf("FAIL: the surface offers no 8-bit colour with sRGB images\n");
        return false;
    }
    surfaceOut->copies = (caps.usages & mrhi_textureCopySource) != 0;
    surfaceOut->staged = config.color.format != surfaceOut->drawFormat;
    surfaceOut->bgra = surfaceOut->drawFormat == mrhi_formatBgra8UnormSrgb;
    config.usage = (surfaceOut->staged ? mrhi_textureCopyDestination : mrhi_textureRenderTarget) |
                   (surfaceOut->copies ? mrhi_textureCopySource : 0u);
    surfaceOut->config = config;
    return SampleSurfaceResize(sample, surfaceOut, width, height);
}

bool SampleSurfaceResize(Sample* sample, SampleSurface* surface, uint32_t width, uint32_t height)
{
    surface->config.width = width;
    surface->config.height = height;
    mrhiResult result = mrhiConfigureSurface(sample->device, &surface->config);
    if (result != mrhi_success && result != mrhi_errorOutOfDate)
    {
        printf("FAIL: the surface not configured (%d)\n", (int)result);
        return false;
    }
    return true;
}

void SampleSurfaceClose(Sample* sample, SampleSurface* surface)
{
    if (surface->surface.index1 == 0)
    {
        return;
    }
    (void)mrhiUnconfigureSurface(sample->device, surface->surface);
    (void)mrhiDestroySurface(sample->instance, surface->surface);
    *surface = (SampleSurface){0};
}

// Swaps a readback's BGRA texels to RGBA.
static void Swap(uint8_t* pixels, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        uint8_t blue = pixels[i * 4];
        pixels[i * 4] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = blue;
    }
}

SamplePresented SamplePresent(Sample* sample, SampleSurface* surface, muiRhiRenderer* renderer,
                              const muiDrawList* list, bool readBack)
{
    mrhiDevice* device = sample->device;
    uint32_t width = surface->config.width;
    uint32_t height = surface->config.height;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId image = {0};
    mrhiResult begun = mrhiBeginFrame(device, &frame);
    if (begun != mrhi_success)
    {
        if (begun == mrhi_errorDeviceLost)
        {
            mrhiDeviceLossReport report = {0};
            (void)mrhiGetDeviceLossReport(device, &report);
            printf("FAIL: the device was lost (reason %d): %.*s\n", (int)report.reason,
                   (int)report.messageLength, report.message);
            sample->failures++;
            return sample_lost_device;
        }
        return begun == mrhi_errorCapacity ? sample_busy : sample_failed;
    }
    mrhiResult acquired = mrhiAcquireSurfaceImage(device, surface->surface, &image);
    if (acquired != mrhi_success && acquired != mrhi_suboptimal)
    {
        (void)mrhiDropFrame(device);
        return acquired == mrhi_occluded ? sample_occluded
               : acquired == mrhi_errorOutOfDate &&
                       SampleSurfaceResize(sample, surface, width, height)
                   ? sample_resized
                   : sample_failed;
    }
    // Staged, the list is drawn into a texture in the image's sRGB twin,
    // whose bytes a transfer pass copies onto the image.
    mrhiResourceId target = image;
    mrhiTextureDef stageDef = mrhiDefaultTextureDef();
    stageDef.format = surface->drawFormat;
    stageDef.width = width;
    stageDef.height = height;
    if (surface->staged && mrhiDeclareTexture(device, &stageDef, &target) != mrhi_success)
    {
        (void)mrhiDropFrame(device);
        return sample_failed;
    }
    const muiRhiTarget into = {.resource = target,
                               .width = width,
                               .height = height,
                               .clear = true,
                               .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    const mrhiAccess copies[2] = {
        {.resource = target,
         .kind = mrhi_accessCopySource,
         .range = {.mipCount = 1, .layerCount = 1}},
        {.resource = image,
         .kind = mrhi_accessCopyDestination,
         .range = {.mipCount = 1, .layerCount = 1}},
    };
    mrhiPassDef copyDef = mrhiDefaultPassDef();
    copyDef.passClass = mrhi_passTransfer;
    copyDef.accesses = copies;
    copyDef.accessCount = 2;
    copyDef.neverCull = true;
    mrhiPassId copying = {0};
    const mrhiTextureCopy from = {.resource = target};
    const mrhiTextureCopy onto = {.resource = image};
    bool reads = readBack && surface->copies;
    const mrhiAccess read = {.resource = image,
                             .kind = mrhi_accessCopySource,
                             .range = {.mipCount = 1, .layerCount = 1}};
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    const mrhiTextureCopy source = {.resource = image};
    const mrhiExtent3d extent = {width, height, 1};
    mrhiRequestId readback = {0};
    bool recorded =
        muiRhiRenderer_AddPasses(renderer, list, &into) == mui_success &&
        (!surface->staged || mrhiAddPass(device, &copyDef, &copying) == mrhi_success) &&
        (!reads || mrhiAddPass(device, &readDef, &reading) == mrhi_success) &&
        mrhiCompileFrame(device) == mrhi_success &&
        muiRhiRenderer_Record(renderer) == mui_success &&
        (!surface->staged ||
         (mrhiBeginPass(device, copying) == mrhi_success &&
          mrhiCopyTexture(device, copying, &from, &onto, &extent) == mrhi_success &&
          mrhiEndPass(device, copying) == mrhi_success)) &&
        (!reads || (mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &extent, &readback) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success));
    mrhiRequestId token = {0};
    if (!recorded)
    {
        (void)mrhiDropFrame(device);
        return sample_failed;
    }
    if (mrhiSubmitFrame(device, &token) != mrhi_success)
    {
        return sample_failed;
    }
    if (reads)
    {
        surface->reading = true;
        surface->readback = readback;
        surface->frame = token;
        surface->readWidth = width;
        surface->readHeight = height;
    }
    return sample_presented;
}

SampleTaken SampleTakeFrame(Sample* sample, SampleSurface* surface, uint8_t* pixels,
                            size_t capacity)
{
    mrhiDevice* device = sample->device;
    if (!surface->reading)
    {
        return sample_lost;
    }
    mrhiResult done = mrhiWaitFrame(device, surface->frame, 0);
    if (done == mrhi_timeout)
    {
        return sample_waiting;
    }
    size_t size = (size_t)surface->readWidth * surface->readHeight * 4;
    size_t taken = 0;
    mrhiResult took = done == mrhi_success && size <= capacity
                          ? mrhiTakeReadback(device, surface->readback, pixels, size, &taken)
                          : mrhi_errorInvalid;
    // Answered once the device's notifications are taken.
    if (took == mrhi_errorState)
    {
        return sample_waiting;
    }
    surface->reading = false;
    if (took != mrhi_success || taken != size)
    {
        return sample_lost;
    }
    if (surface->bgra)
    {
        Swap(pixels, (size_t)surface->readWidth * surface->readHeight);
    }
    return sample_taken;
}
