// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The samples' application frame (record mui-0005): what every sample
// does around its own tree, in the order a host does it. It opens a
// window through Maul Window, makes the context, the glue, the access
// and the renderer, and runs frames: the window's records through the
// glue, layout, the accessibility tree's changes, the list painted and
// drawn. Headless (--headless) it runs on Maul Window's test backend,
// draws into a texture it reads back each frame, and hands the sample
// each frame to check and to post the next input; windowed it presents
// onto the window's surface until the window is closed, or for --frames
// N frames, the last read back where the surface allows and handed to
// the sample's still check. Text is Liberation Sans.

#ifndef MAUL_UI_SAMPLES_APP_H
#define MAUL_UI_SAMPLES_APP_H

#include "harness.h"

#include "maul-ui-rhi/renderer.h"
#include "maul-ui-window/access.h"
#include "maul-ui-window/glue.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text_block.h"
#include "maul-ui/visual.h"
#include "maul-window/context.h"
#include "maul-window/event.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct SampleApp SampleApp;

// What a sample gives the frame. Every function takes the user pointer.
typedef struct SampleAppDef
{
    // The window's size, and the root's, in logical units.
    uint32_t width;
    uint32_t height;
    // Builds the tree under the root; the device is open.
    void (*build)(void* user, SampleApp* app);
    // Headless, after each frame is drawn and read back: checks it and
    // posts the next input; false when the run is done.
    bool (*script)(void* user, SampleApp* app, int frame);
    // Windowed with --frames, on the last frame read back: checks what
    // the first headless frame checks. NULL for none.
    void (*still)(void* user, SampleApp* app);
    // Finds the image a key names, or NULL for none.
    muiRhiImageFunction image;
    // Lets go of what the sample made on the device, before it goes; NULL
    // for nothing.
    void (*finish)(void* user, SampleApp* app);
    void* user;
} SampleAppDef;

struct SampleApp
{
    const SampleAppDef* def;
    Sample sample;
    muiTextService* text;
    muiTextHost host;
    muiContext* context;
    muiNodeId root;
    mwinContext* windows;
    mwinWindowId window;
    muiWindowGlue* glue;
    muiWindowAccess* access;
    muiRhiRenderer* renderer;
    // Headless, or windowed for frames frames (0 until closed).
    bool headless;
    int frames;
    bool closing;
    SampleSurface surface;
    // The frame's pixels when read back, their size and scale.
    uint8_t* pixels;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    float scale;
    bool reading;
    // Now on the records' clock, and the frames run.
    uint64_t now;
    int frame;
};

// Runs a sample from its arguments ([--headless] [--frames N]): its exit
// status, 77 without an adapter unless MUI_RHI_REQUIRED is set.
int SampleRunApp(const SampleAppDef* def, int count, char** arguments);

// Records a check, as SampleCheck.
bool SampleAppCheck(SampleApp* app, bool condition, const char* what);

// sRGB bytes as a colour.
muiColor SampleColor(const uint8_t rgb[3]);

// A length in logical units.
muiDimension SampleLength(float value);

// A node under a parent (the null id for none), of a size where one is
// given (0 for automatic), which does not shrink along a column.
muiNodeId SampleNode(SampleApp* app, muiNodeId parent, float width, float height);

// A node's background.
void SampleFill(SampleApp* app, muiNodeId node, const uint8_t rgb[3]);

// A node holding text in a block of its own, at a size and colour.
muiNodeId SampleLabel(SampleApp* app, muiNodeId parent, const char* text, float size,
                      const uint8_t rgb[3]);

// A layout value set alone on a node, by its property.
void SampleSetLayout(SampleApp* app, muiNodeId node, const muiLayoutStyle* layout,
                     muiPropertyMask mask);

// The pixel at a point of a node's border box in the last frame read
// back; all zero outside it.
const uint8_t* SamplePixelOf(const SampleApp* app, muiNodeId node, float x, float y);

// Whether a pixel is within 3 of sRGB bytes in each channel.
bool SampleNear(const uint8_t* pixel, const uint8_t rgb[3]);

// Posts a cursor record at a node's centre, as a platform reports it:
// a move (no button), or a button going down or up with the buttons
// held after it.
void SamplePost(SampleApp* app, mwinEventType type, muiNodeId node, uint8_t buttons);

// Posts a wheel turn of detents over a node, the cursor moved there.
void SamplePostWheel(SampleApp* app, muiNodeId node, float detents);

// Posts a key going down and up, by its code and its meaning
// (MWIN_KEY_NAMED with the code for a key that types nothing), with the
// text it types, or NULL.
void SamplePostKey(SampleApp* app, mwinKeyCode code, mwinKey key, const char* text);

#endif // MAUL_UI_SAMPLES_APP_H
