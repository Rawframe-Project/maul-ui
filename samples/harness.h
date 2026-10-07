// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The samples' harness (record mui-0005): each sample is a program that
// checks its own result, so the samples double as tests, as Maul RHI's
// do. The harness opens a Maul RHI device on the first adapter, software
// ones included, makes the textures a sample shows, draws a list into a
// texture and reads it back, and turns the checks into an exit status:
// 0 when every check passed, 77 when there is no adapter and none is
// required (MUI_RHI_REQUIRED), 1 otherwise.

#ifndef MAUL_UI_SAMPLES_HARNESS_H
#define MAUL_UI_SAMPLES_HARNESS_H

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-ui-rhi/renderer.h"
#include "maul-ui/draw.h"

#include <stdbool.h>
#include <stdint.h>

// The exit status of a sample skipped for want of an adapter.
#define SAMPLE_SKIPPED 77

typedef struct Sample
{
    mrhiInstance* instance;
    mrhiDevice* device;
    // The checks that failed.
    int failures;
} Sample;

// Opens the sample's device: 0, SAMPLE_SKIPPED, or 1 after printing why.
int SampleOpen(Sample* sample);

// Destroys the device and the instance and returns the exit status.
int SampleClose(Sample* sample);

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

#endif // MAUL_UI_SAMPLES_HARNESS_H
