// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The samples' harness (record mui-0005): each sample is a program that
// checks its own result, so the samples double as tests, as Maul RHI's
// do. The harness opens a Maul RHI device on the first adapter, software
// ones included, makes the textures a sample shows, draws a list into a
// texture and reads it back, and turns the checks into an exit status:
// 0 when every check passed, 77 when there is no adapter and none is
// required (MUI_RHI_REQUIRED), 1 otherwise. A sample with a window draws
// onto a surface made from Maul Window's native handles instead, its
// images sRGB, and reads the image it presents back where the surface
// allows copies.

#ifndef MAUL_UI_SAMPLES_HARNESS_H
#define MAUL_UI_SAMPLES_HARNESS_H

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/surface.h"
#include "maul-ui-rhi/renderer.h"
#include "maul-ui/draw.h"
#include "maul-window/native.h"

#include <stdbool.h>
#include <stdint.h>

// The exit status of a sample skipped for want of an adapter.
#define SAMPLE_SKIPPED 77

typedef struct Sample
{
    mrhiInstance* instance;
    mrhiAdapterId adapter;
    mrhiDevice* device;
    // The checks that failed.
    int failures;
} Sample;

// Opens the sample's device: 0, SAMPLE_SKIPPED, or 1 after printing why.
int SampleOpen(Sample* sample);

// Destroys the device and the instance and returns the exit status.
int SampleClose(Sample* sample);

// Returns a sample's exit status, printed on the web for the runner.
int SampleExit(int status);

// Records a check, printing what failed; returns the condition.
bool SampleCheck(Sample* sample, bool condition, const char* what);

// Polls the device, ten seconds at most, until a renderer's pipeline is
// ready: whether it is.
bool SampleAwaitReady(Sample* sample, muiRhiRenderer* renderer);

// Makes a texture of RGBA8 sRGB texels, uploaded in a frame of its own.
bool SampleTexture(Sample* sample, uint32_t width, uint32_t height, const uint8_t* texels,
                   mrhiTextureId* textureOut);

// Draws a list into a texture of a size, cleared to black, and reads it
// back as RGBA8 sRGB bytes, width * height * 4 of them.
bool SampleRender(Sample* sample, muiRhiRenderer* renderer, const muiDrawList* list, uint32_t width,
                  uint32_t height, uint8_t* pixels);

// A window's surface configured on the sample's device.
typedef struct SampleSurface
{
    mrhiSurfaceId surface;
    mrhiSurfaceConfig config;
    // Whether its images can be read back.
    bool copies;
    // Whether its images are BGRA, which a readback swaps to RGBA.
    bool bgra;
} SampleSurface;

// What presenting a frame came to.
typedef enum SamplePresented
{
    // Drawn and presented.
    sample_presented,
    // Not drawn: the window is hidden or has no size.
    sample_occluded,
    // Not drawn: the surface was configured again for the window's size.
    sample_resized,
    sample_failed,
} SamplePresented;

// Makes a surface from a window's handles and configures it at a size,
// its images in the sRGB twin of the colour the platform prefers: true,
// or false after printing why.
bool SampleSurfaceOpen(Sample* sample, const mwinNativeHandles* handles, uint32_t width,
                       uint32_t height, SampleSurface* surfaceOut);

// Configures a surface again at a size.
bool SampleSurfaceResize(Sample* sample, SampleSurface* surface, uint32_t width, uint32_t height);

// Ends a surface's configuration and destroys it.
void SampleSurfaceClose(Sample* sample, SampleSurface* surface);

// Draws a list onto the surface's next image, cleared to black, and
// presents it; with pixels and a surface that allows copies, reads the
// image back first as RGBA8 sRGB bytes.
SamplePresented SamplePresent(Sample* sample, SampleSurface* surface, muiRhiRenderer* renderer,
                              const muiDrawList* list, uint8_t* pixels);

#endif // MAUL_UI_SAMPLES_HARNESS_H
