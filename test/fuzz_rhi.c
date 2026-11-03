// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the reference renderer (rhi/src) with draw lists a host builds
// by hand, drawn on Maul RHI's test driver. The input is read as:
//
// - a byte of flags: bit 0 draws through a projection;
// - the target's width and height less one, a byte each;
// - the list's scale, 4 bytes;
// - the counts of clips (mod 17), transforms (mod 9), gradients (mod 5),
//   glyphs (mod 65) and commands (mod 65), a byte each;
// - with a projection, its 16 floats;
// - then each table's records as their bytes: clips, transforms,
//   gradients, glyphs, commands. Missing bytes read as zeros.
//
// A command's kind is its byte mod 6, so unknown kinds come too. A
// glyph run whose font key is even draws in Liberation Sans, and one
// whose size times the scale is between 256 and the largest glyph size
// the atlas takes is drawn at 16, keeping a run quick. Images are found
// for keys 1 to 3; key 4 answers no size. The target is cleared. Each
// input gets a renderer of its own: a first frame readies it, then the
// list is drawn twice, the second time against the records the first
// left. Every frame is drawn or refused for its room
// (mui_errorCapacity); a frame drawn records, and nothing is read or
// written past a buffer.

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/test.h"
#include "maul-ui-rhi/renderer.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "liberation_sans.inc"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    MAX_CLIPS = 17,
    MAX_TRANSFORMS = 9,
    MAX_GRADIENTS = 5,
    MAX_GLYPHS = 65,
    MAX_COMMANDS = 65,
    // The command kinds and one past them.
    KINDS = 6
};

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// The input, read front to back.
typedef struct Reader
{
    const uint8_t* data;
    size_t size;
} Reader;

// The next count bytes into out, zeros past the input.
static void Read(Reader* reader, void* out, size_t count)
{
    size_t taken = count < reader->size ? count : reader->size;
    memset(out, 0, count);
    if (taken > 0)
    {
        memcpy(out, reader->data, taken);
    }
    reader->data += taken;
    reader->size -= taken;
}

static uint8_t Byte(Reader* reader)
{
    uint8_t value = 0;
    Read(reader, &value, 1);
    return value;
}

// The device, a font and two images, made once.
typedef struct World
{
    mrhiTestFrameLog log;
    mrhiTestAdapter adapter;
    mrhiTestDriverDef driver;
    mrhiInstance* instance;
    mrhiDevice* device;
    muiTextService* service;
    uint64_t font;
    mrhiTextureId images[2];
} World;

static World s_world;

static bool OpenDevice(World* world)
{
    world->adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
        .pipelineOutcome = mrhi_success,
        .frameLog = &world->log,
    };
    world->driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &world->adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &world->driver.chain;
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &world->instance) != mrhi_success ||
        mrhiInstance_RequestAdapters(world->instance, &search, &request) != mrhi_success ||
        mrhiInstance_TakeNotification(world->instance, &record) != mrhi_success ||
        mrhiInstance_GetAdapters(world->instance, &adapter, 1, &count) != mrhi_success ||
        count != 1)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    return mrhiCreateDevice(world->instance, &deviceDef, &world->device, &request) ==
               mrhi_success &&
           mrhiInstance_TakeNotification(world->instance, &record) == mrhi_success &&
           record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success;
}

static void MakeWorld(World* world)
{
    Expect(OpenDevice(world));
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = s_liberationSans;
    fontDef.size = sizeof s_liberationSans;
    fontDef.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    Expect(muiCreateTextService(&serviceDef, &world->service) == mui_success &&
           muiCreateFont(world->service, &fontDef, &font) == mui_success);
    world->font = muiFont_GetKey(font);
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8UnormSrgb;
    textureDef.width = 8;
    textureDef.height = 8;
    textureDef.usage = mrhi_textureSampled;
    for (uint32_t i = 0; i < 2; i++)
    {
        Expect(mrhiCreateTexture(world->device, &textureDef, &world->images[i]) == mrhi_success);
    }
}

static bool FindImage(void* context, uint64_t key, muiRhiImage* imageOut)
{
    const World* world = context;
    if (key < 1 || key > 4)
    {
        return false;
    }
    *imageOut =
        (muiRhiImage){.texture = world->images[key & 1], .width = key == 4 ? 0 : 8, .height = 8};
    return true;
}

static void Pump(World* world, muiRhiRenderer* renderer)
{
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(world->device, &record) == mrhi_success)
    {
        (void)muiRhiRenderer_Notify(renderer, &record);
    }
}

// A frame drawing the list into a target the frame makes, which a pass
// reads so that the drawing is kept.
static muiResult DrawFrame(World* world, muiRhiRenderer* renderer, const muiDrawList* list,
                           muiRhiTarget into)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8UnormSrgb;
    targetDef.width = into.width;
    targetDef.height = into.height;
    Expect(mrhiDevice_BeginFrame(world->device, &frame) == mrhi_success &&
           mrhiDeclareTexture(world->device, &targetDef, &into.resource) == mrhi_success);
    muiResult added = muiRhiRenderer_AddPasses(renderer, list, &into);
    if (added != mui_success)
    {
        (void)mrhiDevice_DropFrame(world->device);
        Pump(world, renderer);
        return added;
    }
    const mrhiAccess read = {.resource = into.resource,
                             .kind = mrhi_accessCopySource,
                             .range = {.mipCount = 1, .layerCount = 1}};
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    mrhiRequestId token = {0};
    Expect(mrhiAddPass(world->device, &readDef, &reading) == mrhi_success &&
           mrhiDevice_CompileFrame(world->device) == mrhi_success &&
           muiRhiRenderer_Record(renderer) == mui_success &&
           mrhiPass_Begin(world->device, reading) == mrhi_success &&
           mrhiPass_End(world->device, reading) == mrhi_success &&
           mrhiDevice_SubmitFrame(world->device, &token) == mrhi_success);
    Pump(world, renderer);
    return added;
}

// The run's font and size kept to what draws quickly (see above).
static void TameRun(muiDrawGlyphRun* run, float scale, uint64_t font)
{
    if ((run->font & 1u) == 0)
    {
        run->font = font;
    }
    float pixels = fabsf(run->size * scale);
    if (pixels > 256.0f && pixels <= MUI_MAX_GLYPH_PIXEL_SIZE)
    {
        run->size = 16.0f;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    World* world = &s_world;
    if (world->device == nullptr)
    {
        MakeWorld(world);
    }
    Reader reader = {data, size};
    uint8_t flags = Byte(&reader);
    muiRhiTarget into = {.width = 1u + Byte(&reader),
                         .height = 1u + Byte(&reader),
                         // A target the frame declares has nothing to keep.
                         .clear = true,
                         .clearColor = {0, 0, 0, 1},
                         .projected = (flags & 1u) != 0};
    muiDrawClip clips[MAX_CLIPS];
    muiDrawTransform transforms[MAX_TRANSFORMS];
    muiDrawGradient gradients[MAX_GRADIENTS];
    muiGlyph glyphs[MAX_GLYPHS];
    muiDrawCommand commands[MAX_COMMANDS];
    muiDrawList list = {.clips = clips,
                        .transforms = transforms,
                        .gradients = gradients,
                        .glyphs = glyphs,
                        .commands = commands};
    Read(&reader, &list.header.scale, sizeof list.header.scale);
    list.clipCount = Byte(&reader) % MAX_CLIPS;
    list.transformCount = Byte(&reader) % MAX_TRANSFORMS;
    list.gradientCount = Byte(&reader) % MAX_GRADIENTS;
    list.glyphCount = Byte(&reader) % MAX_GLYPHS;
    list.commandCount = Byte(&reader) % MAX_COMMANDS;
    if (into.projected)
    {
        Read(&reader, into.projection, sizeof into.projection);
    }
    Read(&reader, clips, list.clipCount * sizeof clips[0]);
    Read(&reader, transforms, list.transformCount * sizeof transforms[0]);
    Read(&reader, gradients, list.gradientCount * sizeof gradients[0]);
    Read(&reader, glyphs, list.glyphCount * sizeof glyphs[0]);
    Read(&reader, commands, list.commandCount * sizeof commands[0]);
    for (uint32_t i = 0; i < list.commandCount; i++)
    {
        commands[i].kind = (muiDrawKind)((uint32_t)commands[i].kind % KINDS);
        if (commands[i].kind == mui_drawGlyphRun)
        {
            TameRun(&commands[i].glyphRun, list.header.scale, world->font);
        }
    }

    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = world->device;
    def.image = FindImage;
    def.imageUser = world;
    def.text = world->service;
    muiRhiRenderer* renderer = nullptr;
    Expect(muiCreateRhiRenderer(&def, &renderer) == mui_success);
    // The first frame finds the pipelines not ready; the device's
    // notification readies them.
    const muiDrawList empty = {.header = {.scale = 1.0f}};
    Expect(DrawFrame(world, renderer, &empty, into) == mui_empty &&
           muiRhiRenderer_IsReady(renderer));
    for (int frame = 0; frame < 2; frame++)
    {
        muiResult drawn = DrawFrame(world, renderer, &list, into);
        Expect(drawn == mui_success || drawn == mui_errorCapacity);
    }
    muiDestroyRhiRenderer(renderer);
    return 0;
}
