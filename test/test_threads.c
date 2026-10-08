// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Contexts on separate threads (record mui-0001): four threads at once,
// each with its own text service, fonts and context, build the same scene
// of styled text in several scripts, lay it out and paint it over frames
// of hovering and transitions, and render glyphs from every kind of font
// as images, fields and colour glyphs. Each thread's results, hashed,
// are those of one thread alone; under ThreadSanitizer, nothing races.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cff.inc"
#include "color.inc"
#include "liberation_sans.inc"
#include "variable.inc"

enum
{
    THREADS = 4,
    FRAMES = 40,
    GLYPHS = 40,
    MS = 1000000,
    PIXELS = 1 << 18
};

static const char* const s_texts[] = {
    "AVATAR Wavy, kerned: fi fl ffi",
    "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC: "
    "\xCE\xB3\xCF\x81\xCE\xAC\xCE\xBC\xCE\xBC\xCE\xB1\xCF\x84\xCE\xB1",
    "\xD0\x9A\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0 \xD0\xB8 0123",
    "\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA \xD7\xA2\xD7\x9D 123 "
    "\xD7\x95\xD7\x9E\xD7\xA1\xD7\xA4\xD7\xA8\xD7\x99\xD7\x9D",
    "Mixed abc \xD7\x90\xD7\x91\xD7\x92 def, and caf\xC3\xA9",
    "A long paragraph that wraps across the width of its row, with words of every length and some "
    "punctuation; it breaks, again and again.",
};

enum
{
    TEXTS = sizeof s_texts / sizeof s_texts[0]
};

// Set once every thread exists, so they run at once.
static atomic_bool s_go;

// What a run computed, as hashes.
typedef struct Run
{
    uint32_t layout;
    uint32_t list;
    uint32_t glyphs;
    uint32_t steps;
} Run;

typedef struct Fonts
{
    uint64_t sans;
    uint64_t cff;
    uint64_t variable;
    uint64_t color;
} Fonts;

typedef struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    Fonts fonts;
    muiNodeId root;
    uint64_t timeNs;
} Scene;

static uint32_t Mix(uint32_t hash, const void* bytes, size_t size)
{
    const unsigned char* p = bytes;
    for (size_t i = 0; i < size; i++)
    {
        hash = (hash ^ p[i]) * 16777619u;
    }
    return hash;
}

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

static bool AddFont(Scene* scene, const unsigned char* data, size_t size, uint64_t* keyOut)
{
    muiFontDef def = muiDefaultFontDef();
    def.data = data;
    def.size = size;
    def.dataMode = mui_fontDataBorrow;
    muiFontId id = {0, 0};
    if (muiCreateFont(scene->service, &def, &id) != mui_success)
    {
        return false;
    }
    *keyOut = muiFont_GetKey(id);
    return true;
}

// A class of text in a font at a size, with a background that lightens
// over 150 ms when hovered.
static bool MakeClass(Scene* scene, uint64_t font, float size, float weight, muiStyleId* classOut)
{
    muiContext* context = scene->context;
    const muiStyleDef def = muiDefaultStyleDef();
    muiTextStyle text = muiDefaultTextStyle();
    text.font = font;
    text.size = Length(size);
    text.weight = weight;
    text.color = (muiColor){0.1f, 0.2f, 0.3f, 1.0f};
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){0.9f, 0.9f, 0.85f, 1.0f};
    visual.radius.topStart = Length(6.0f);
    visual.radius.bottomEnd = Length(6.0f);
    visual.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.3f}, 1.0f, 2.0f, 4.0f, 0.0f};
    const muiPropertyMask visuals =
        MUI_PROPERTY_BIT(mui_propertyBackground) | MUI_PROPERTY_BIT(mui_propertyRadiusTopStart) |
        MUI_PROPERTY_BIT(mui_propertyRadiusBottomEnd) | MUI_PROPERTY_BIT(mui_propertyOuterShadow);
    muiVisualStyle hovered = visual;
    hovered.background = (muiColor){0.6f, 0.7f, 1.0f, 1.0f};
    muiTransitionDef fade = muiDefaultTransitionDef();
    fade.durationNs = 150 * (uint64_t)MS;
    muiTransitionId transition = {0, 0};
    return muiCreateStyle(context, &def, classOut) == mui_success &&
           muiStyle_SetTextValues(context, *classOut, mui_variantBase, &text,
                                  MUI_PROPERTY_BIT(mui_propertyFont) |
                                      MUI_PROPERTY_BIT(mui_propertyFontSize) |
                                      MUI_PROPERTY_BIT(mui_propertyFontWeight) |
                                      MUI_PROPERTY_BIT(mui_propertyTextColor)) == mui_success &&
           muiStyle_SetVisualValues(context, *classOut, mui_variantBase, &visual, visuals) ==
               mui_success &&
           muiStyle_SetVisualValues(context, *classOut, mui_variantHovered, &hovered,
                                    MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success &&
           muiCreateTransition(context, &fade, &transition) == mui_success &&
           muiStyle_SetTransition(context, *classOut, mui_variantBase, transition, mui_groupVisual,
                                  MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success;
}

// A node showing text in a class, the root's last child.
static bool AddLabel(Scene* scene, const char* text, muiStyleId style)
{
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = text;
    blockDef.length = strlen(text);
    muiTextBlockId block = {0, 0};
    if (muiCreateTextBlock(scene->service, &blockDef, &block) != mui_success)
    {
        return false;
    }
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = {0, 0};
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    return muiCreateNode(scene->context, &def, &node) == mui_success &&
           muiNode_InsertChild(scene->context, scene->root, node, (muiNodeId){0, 0}) ==
               mui_success &&
           muiNode_SetLayoutValues(scene->context, node, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success &&
           muiNode_SetClasses(scene->context, node, &style, 1) == mui_success;
}

static bool MakeScene(Scene* scene)
{
    const muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    const muiContextDef contextDef = muiDefaultContextDef();
    if (muiCreateTextService(&serviceDef, &scene->service) != mui_success ||
        muiCreateContext(&contextDef, &scene->context) != mui_success)
    {
        return false;
    }
    scene->host = (muiTextHost){scene->service, scene->context};
    Fonts* fonts = &scene->fonts;
    if (!AddFont(scene, s_liberationSans, sizeof s_liberationSans, &fonts->sans) ||
        !AddFont(scene, s_cff, sizeof s_cff, &fonts->cff) ||
        !AddFont(scene, s_variable, sizeof s_variable, &fonts->variable) ||
        !AddFont(scene, s_color, sizeof s_color, &fonts->color))
    {
        return false;
    }
    const muiNodeDef def = muiDefaultNodeDef();
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.sizing.width = Length(320.0f);
    muiStyleId plain = {0, 0};
    muiStyleId heading = {0, 0};
    muiStyleId bold = {0, 0};
    if (muiCreateNode(scene->context, &def, &scene->root) != mui_success ||
        muiNode_SetLayoutValues(scene->context, scene->root, &layout,
                                MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                                    MUI_PROPERTY_BIT(mui_propertyWidth)) != mui_success ||
        !MakeClass(scene, fonts->sans, 15.0f, 400.0f, &plain) ||
        !MakeClass(scene, fonts->cff, 23.5f, 400.0f, &heading) ||
        !MakeClass(scene, fonts->variable, 17.0f, 650.0f, &bold) ||
        !AddLabel(scene, "Settings", heading))
    {
        return false;
    }
    for (uint32_t i = 0; i < TEXTS; i++)
    {
        if (!AddLabel(scene, s_texts[i], i % 3 == 2 ? bold : plain))
        {
            return false;
        }
    }
    return true;
}

// A frame with the pointer over a row: its layout and its list, hashed.
static bool Frame(Scene* scene, uint32_t i, Run* run)
{
    scene->timeNs += 16 * (uint64_t)MS;
    const muiPointerEvent move = {.timeNs = scene->timeNs,
                                  .pointer = 1,
                                  .kind = mui_pointerMouse,
                                  .action = mui_pointerMove,
                                  .x = 40.0f + (float)(i % 3) * 50.0f,
                                  .y = 10.0f + (float)(i * 37 % 200)};
    const muiLayoutInput layoutInput = {320.0f,        600.0f,  muiMeasureText, &scene->host,
                                        scene->timeNs, nullptr, {0, 0, 0, 0}};
    const muiDrawInput drawInput = {1, 1.25f, muiPaintText, &scene->host};
    muiDrawList list;
    if (muiPointerInput(scene->context, scene->root, &move) != mui_success ||
        muiComputeLayout(scene->context, scene->root, &layoutInput) != mui_success ||
        muiBuildDrawList(scene->context, scene->root, &drawInput) != mui_success ||
        muiGetDrawList(scene->context, &list) != mui_success)
    {
        return false;
    }
    muiPointerRecord record;
    while (muiNextPointerRecord(scene->context, &record) == mui_success)
    {
    }
    for (muiNodeId node = muiNode_GetFirstChild(scene->context, scene->root); node.index1 != 0;
         node = muiNode_GetNextSibling(scene->context, node))
    {
        const muiRect rect = muiNode_GetRect(scene->context, node);
        run->layout = Mix(run->layout, &rect, sizeof rect);
    }
    run->list = Mix(run->list, list.commands, list.commandCount * sizeof list.commands[0]);
    run->list = Mix(run->list, list.clips, list.clipCount * sizeof list.clips[0]);
    run->list = Mix(run->list, list.transforms, list.transformCount * sizeof list.transforms[0]);
    run->list = Mix(run->list, list.gradients, list.gradientCount * sizeof list.gradients[0]);
    run->list = Mix(run->list, list.glyphs, list.glyphCount * sizeof list.glyphs[0]);
    return true;
}

// A rendering's outcome: its status and, when it drew, its bytes.
static uint32_t Outcome(uint32_t hash, muiResult result, const muiGlyphImage* image,
                        const unsigned char* pixels, size_t pixelSize)
{
    hash = Mix(hash, &result, sizeof result);
    if (result == mui_success)
    {
        hash = Mix(hash, image, sizeof *image);
        hash = Mix(hash, pixels, (size_t)image->width * image->height * pixelSize);
    }
    return hash;
}

// Glyphs of every kind of font as images, fields and colour glyphs.
static void Render(Scene* scene, unsigned char* pixels, Run* run)
{
    const uint64_t outlines[3] = {scene->fonts.sans, scene->fonts.cff, scene->fonts.variable};
    const muiLinearColor ink = {0.2f, 0.4f, 0.8f, 0.75f};
    for (uint32_t glyph = 0; glyph < GLYPHS; glyph++)
    {
        const float size = 9.5f + (float)(glyph % 5) * 6.25f;
        const float offset = (float)(glyph % 4) * 0.25f;
        muiGlyphImage image = {0};
        for (uint32_t k = 0; k < 3; k++)
        {
            muiResult result = muiRenderGlyph(scene->service, outlines[k], glyph, size, offset,
                                              &image, pixels, PIXELS);
            run->glyphs = Outcome(run->glyphs, result, &image, pixels, 1);
            result = muiRenderGlyphField(scene->service, outlines[k], glyph, 32.0f, 4, &image,
                                         pixels, PIXELS);
            run->glyphs = Outcome(run->glyphs, result, &image, pixels, 1);
        }
        muiResult result = muiRenderGlyphMultiField(scene->service, scene->fonts.sans, glyph, 28.0f,
                                                    4, &image, pixels, PIXELS);
        run->glyphs = Outcome(run->glyphs, result, &image, pixels, 4);
        result = muiRenderColorGlyph(scene->service, scene->fonts.color, glyph, size, offset, 0,
                                     ink, &image, pixels, PIXELS);
        run->glyphs = Outcome(run->glyphs, result, &image, pixels, 4);
    }
}

// One run, start to end: every step that succeeded is counted.
static void Work(Run* run)
{
    *run = (Run){2166136261u, 2166136261u, 2166136261u, 0};
    Scene scene = {0};
    unsigned char* pixels = malloc(PIXELS);
    if (pixels != nullptr && MakeScene(&scene))
    {
        run->steps++;
        for (uint32_t i = 0; i < FRAMES; i++)
        {
            run->steps += Frame(&scene, i, run) ? 1 : 0;
        }
        Render(&scene, pixels, run);
    }
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
    free(pixels);
}

static void* Start(void* argument)
{
    while (!atomic_load(&s_go))
    {
        sched_yield();
    }
    Work(argument);
    return nullptr;
}

int main(void)
{
    Run alone;
    Work(&alone);
    CHECK(alone.steps == 1 + FRAMES, "one thread alone, every step");
    pthread_t threads[THREADS];
    Run runs[THREADS];
    bool started[THREADS];
    for (uint32_t i = 0; i < THREADS; i++)
    {
        runs[i] = (Run){0};
        started[i] = pthread_create(&threads[i], nullptr, Start, &runs[i]) == 0;
        CHECK(started[i], "a thread");
    }
    atomic_store(&s_go, true);
    for (uint32_t i = 0; i < THREADS; i++)
    {
        if (started[i])
        {
            CHECK(pthread_join(threads[i], nullptr) == 0, "joined");
            CHECK(runs[i].steps == alone.steps && runs[i].layout == alone.layout,
                  "the same layout as alone");
            CHECK(runs[i].list == alone.list, "the same list bytes as alone");
            CHECK(runs[i].glyphs == alone.glyphs, "the same glyph bytes as alone");
        }
    }
    return s_failures == 0 ? 0 : 1;
}
