// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's floating-point environment against a host's: a scene of
// styled text in several fonts, laid out and painted over frames of
// pointer input and transitions, hit-tested, read for accessibility, and
// rendered as glyph images, fields and colour glyphs, run in the default
// environment and on a thread playing each host test_fenv.h names (every
// rounding mode, with flush-to-zero and denormals-are-zero off and on).
// Every run gives the same bytes. Every host function the library calls
// (measure, baseline, paint, access text, event and allocator) sees the
// host's own setting, and every public call leaves it as it found it.
// WebAssembly has a single environment, so this suite is not built there.

#include "test_fenv.h"
#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/range.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <stdalign.h>
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
    FRAMES = 30,
    GLYPHS = 30,
    MS = 1000000,
    PIXELS = 1 << 18
};

static const char* const s_texts[] = {
    "AVATAR Wavy, kerned: fi fl ffi",
    "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC: "
    "\xCE\xB3\xCF\x81\xCE\xAC\xCE\xBC\xCE\xBC\xCE\xB1\xCF\x84\xCE\xB1",
    "\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA \xD7\xA2\xD7\x9D 123 "
    "\xD7\x95\xD7\x9E\xD7\xA1\xD7\xA4\xD7\xA8\xD7\x99\xD7\x9D",
    "A long paragraph that wraps across the width of its row, with words of every length and some "
    "punctuation; it breaks, again and again.",
};

enum
{
    TEXTS = sizeof s_texts / sizeof s_texts[0]
};

// The inputs that take arithmetic, computed once in the default
// environment: the test's own arithmetic in the host's setting would give
// the library other inputs.
typedef struct Inputs
{
    float moveX[FRAMES];
    float moveY[FRAMES];
    float hitX[8];
    float hitY[8];
    float sizes[GLYPHS];
    float offsets[GLYPHS];
    float width;
    float basis;
    float field;
} Inputs;

static Inputs s_inputs;

static void MakeInputs(void)
{
    for (uint32_t i = 0; i < FRAMES; i++)
    {
        s_inputs.moveX[i] = 40.0f / 3.0f + (float)(i % 3) * 50.0f;
        s_inputs.moveY[i] = 10.0f + (float)(i * 37 % 200) / 7.0f;
    }
    for (uint32_t k = 0; k < 8; k++)
    {
        s_inputs.hitX[k] = 1.0f / 3.0f + (float)k * 41.7f;
        s_inputs.hitY[k] = 2.0f / 3.0f + (float)(k * k) * 9.1f;
    }
    for (uint32_t glyph = 0; glyph < GLYPHS; glyph++)
    {
        s_inputs.sizes[glyph] = 9.5f + (float)(glyph % 5) * 6.25f / 3.0f;
        s_inputs.offsets[glyph] = (float)(glyph % 4) / 3.0f;
    }
    s_inputs.width = 1000.0f / 3.0f;
    s_inputs.basis = 100.0f / 3.0f;
    s_inputs.field = 32.0f / 3.0f;
}

// What a run computed, as hashes, and how the host's functions found the
// register.
typedef struct Run
{
    uint32_t layout;
    uint32_t list;
    uint32_t hits;
    uint32_t access;
    uint32_t glyphs;
    uint32_t steps;
} Run;

// The register the host has set, and what the host's functions and the
// public calls found.
typedef struct Watch
{
    TestFloatSetting host;
    uint32_t calls;
    uint32_t wrongCalls;
    uint32_t returns;
    uint32_t wrongReturns;
} Watch;

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
    Watch* watch;
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

// A host function's look at the thread's setting.
static void Called(Watch* watch)
{
    watch->calls++;
    watch->wrongCalls += TestFloatSame(TestFloatGet(), watch->host) ? 0 : 1;
}

// A public call's result, with the register it left.
static bool Returned(Watch* watch, bool done)
{
    watch->returns++;
    watch->wrongReturns += TestFloatSame(TestFloatGet(), watch->host) ? 0 : 1;
    return done;
}

static void* Alloc(size_t size, size_t alignment, void* context)
{
    Called(context);
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    Called(context);
    free(memory);
}

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    Scene* scene = user;
    Called(scene->watch);
    return muiMeasureText(&scene->host, nodeId, hostKey, width, height);
}

static float Baseline(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height)
{
    Scene* scene = user;
    Called(scene->watch);
    return muiTextBaseline(&scene->host, nodeId, hostKey, width, height);
}

static void Paint(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                  muiDrawSink* sink)
{
    Scene* scene = user;
    Called(scene->watch);
    muiPaintText(&scene->host, nodeId, hostKey, width, height, sink);
}

static bool AccessText(void* user, muiNodeId nodeId, uint64_t hostKey, const muiRect* shown,
                       muiAccessContent* contentOut)
{
    Scene* scene = user;
    Called(scene->watch);
    return muiAccessTextOf(&scene->host, nodeId, hostKey, shown, contentOut);
}

static bool Event(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    (void)nodeId;
    (void)phase;
    (void)event;
    Called(user);
    return false;
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
    visual.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.3f}, 1.0f, 2.0f, 4.0f, 0.0f};
    const muiPropertyMask visuals = MUI_PROPERTY_BIT(mui_propertyBackground) |
                                    MUI_PROPERTY_BIT(mui_propertyRadiusTopStart) |
                                    MUI_PROPERTY_BIT(mui_propertyOuterShadow);
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

// A node showing text in a class, the root's last child, a third wide.
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
    layout.item.grow = 1.0f;
    layout.item.basis = Length(s_inputs.basis);
    return muiCreateNode(scene->context, &def, &node) == mui_success &&
           muiNode_InsertChild(scene->context, scene->root, node, (muiNodeId){0, 0}) ==
               mui_success &&
           muiNode_SetLayoutValues(scene->context, node, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyContent) |
                                       MUI_PROPERTY_BIT(mui_propertyGrow) |
                                       MUI_PROPERTY_BIT(mui_propertyBasis)) == mui_success &&
           muiNode_SetClasses(scene->context, node, &style, 1) == mui_success &&
           muiNode_SetAccessRole(scene->context, node, mui_roleLabel) == mui_success;
}

static bool MakeScene(Scene* scene)
{
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    serviceDef.allocator = (muiAllocator){Alloc, Free, scene->watch};
    muiContextDef contextDef = muiDefaultContextDef();
    contextDef.allocator = (muiAllocator){Alloc, Free, scene->watch};
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
    layout.container.wrap = mui_wrapWrap;
    layout.container.alignItems = mui_alignBaseline;
    layout.sizing.width = Length(s_inputs.width);
    muiStyleId plain = {0, 0};
    muiStyleId heading = {0, 0};
    muiStyleId bold = {0, 0};
    if (muiCreateNode(scene->context, &def, &scene->root) != mui_success ||
        muiNode_SetLayoutValues(scene->context, scene->root, &layout,
                                MUI_PROPERTY_BIT(mui_propertyFlexWrap) |
                                    MUI_PROPERTY_BIT(mui_propertyAlignItems) |
                                    MUI_PROPERTY_BIT(mui_propertyWidth)) != mui_success ||
        muiSetEventFunction(scene->context, Event, scene->watch) != mui_success ||
        muiSetAccessTextFunction(scene->context, AccessText, scene) != mui_success ||
        !MakeClass(scene, fonts->sans, 15.0f, 400.0f, &plain) ||
        !MakeClass(scene, fonts->cff, 23.5f, 400.0f, &heading) ||
        !MakeClass(scene, fonts->variable, 17.0f, 650.0f, &bold) ||
        !AddLabel(scene, "Settings", heading) ||
        muiAccess_Enable(scene->context, scene->root) != mui_success)
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

// The parts of an accessibility node that hold no pointer.
static uint32_t MixAccessNode(uint32_t hash, const muiAccessNode* node)
{
    hash = Mix(hash, &node->id, sizeof node->id);
    hash = Mix(hash, &node->role, sizeof node->role);
    hash = Mix(hash, &node->flags, sizeof node->flags);
    hash = Mix(hash, &node->bounds, sizeof node->bounds);
    hash = Mix(hash, &node->transform, sizeof node->transform);
    const float numbers[] = {node->value,   node->minimum, node->maximum,    node->step,
                             node->scrollX, node->scrollY, node->scrollXMax, node->scrollYMax};
    hash = Mix(hash, numbers, sizeof numbers);
    for (uint32_t i = 0; i < MUI_ACCESS_TEXTS; i++)
    {
        if (node->text[i] != nullptr)
        {
            hash = Mix(hash, node->text[i], node->textLength[i]);
        }
    }
    return hash;
}

// A frame with the pointer over a row: its layout, its list, the hits at
// a few points and its accessibility update, hashed.
static bool Frame(Scene* scene, uint32_t i, Run* run)
{
    Watch* watch = scene->watch;
    muiContext* context = scene->context;
    scene->timeNs += 16 * (uint64_t)MS + MS / 3;
    const muiPointerEvent move = {.timeNs = scene->timeNs,
                                  .pointer = 1,
                                  .kind = mui_pointerMouse,
                                  .action = mui_pointerMove,
                                  .x = s_inputs.moveX[i],
                                  .y = s_inputs.moveY[i]};
    const muiLayoutInput layoutInput = {s_inputs.width, 600.0f,   Measure,     scene,
                                        scene->timeNs,  Baseline, {0, 0, 0, 0}};
    const muiDrawInput drawInput = {1, 1.25f, Paint, scene};
    muiDrawList list;
    if (!Returned(watch, muiPointerInput(context, scene->root, &move) == mui_success) ||
        !Returned(watch, muiComputeLayout(context, scene->root, &layoutInput) == mui_success) ||
        !Returned(watch, muiBuildDrawList(context, scene->root, &drawInput) == mui_success) ||
        !Returned(watch, muiGetDrawList(context, &list) == mui_success))
    {
        return false;
    }
    muiPointerRecord record;
    while (muiNextPointerRecord(context, &record) == mui_success)
    {
        bool handled = false;
        (void)Returned(watch, muiDispatchPointerRecord(context, &record, &handled) == mui_success);
    }
    for (muiNodeId node = muiNode_GetFirstChild(context, scene->root); node.index1 != 0;
         node = muiNode_GetNextSibling(context, node))
    {
        const muiRect rect = muiNode_GetRect(context, node);
        run->layout = Mix(run->layout, &rect, sizeof rect);
    }
    run->list = Mix(run->list, list.commands, list.commandCount * sizeof list.commands[0]);
    run->list = Mix(run->list, list.clips, list.clipCount * sizeof list.clips[0]);
    run->list = Mix(run->list, list.transforms, list.transformCount * sizeof list.transforms[0]);
    run->list = Mix(run->list, list.gradients, list.gradientCount * sizeof list.gradients[0]);
    run->list = Mix(run->list, list.glyphs, list.glyphCount * sizeof list.glyphs[0]);
    for (uint32_t k = 0; k < 8; k++)
    {
        muiHit hit = {0};
        if (Returned(watch, muiHitTest(context, scene->root, s_inputs.hitX[k], s_inputs.hitY[k],
                                       &hit) == mui_success))
        {
            run->hits = Mix(run->hits, &hit.node, sizeof hit.node);
            run->hits = Mix(run->hits, &hit.x, sizeof hit.x);
            run->hits = Mix(run->hits, &hit.y, sizeof hit.y);
        }
    }
    muiAccessUpdate update = {0};
    if (!Returned(watch, muiBuildAccessUpdate(context, scene->root, &update) == mui_success))
    {
        return false;
    }
    for (uint32_t k = 0; k < update.nodeCount; k++)
    {
        run->access = MixAccessNode(run->access, update.nodes[k]);
    }
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
    Watch* watch = scene->watch;
    const uint64_t outlines[3] = {scene->fonts.sans, scene->fonts.cff, scene->fonts.variable};
    const muiLinearColor ink = {0.2f, 0.4f, 0.8f, 0.75f};
    for (uint32_t glyph = 0; glyph < GLYPHS; glyph++)
    {
        const float size = s_inputs.sizes[glyph];
        const float offset = s_inputs.offsets[glyph];
        muiGlyphImage image = {0};
        for (uint32_t k = 0; k < 3; k++)
        {
            muiResult result = muiRenderGlyph(scene->service, outlines[k], glyph, size, offset,
                                              &image, pixels, PIXELS);
            run->glyphs = Outcome(run->glyphs, result, &image, pixels, 1);
            result = muiRenderGlyphField(scene->service, outlines[k], glyph, s_inputs.field, 4,
                                         &image, pixels, PIXELS);
            run->glyphs = Outcome(run->glyphs, result, &image, pixels, 1);
            (void)Returned(watch, true);
        }
        muiResult result = muiRenderGlyphMultiField(scene->service, scene->fonts.sans, glyph, 28.0f,
                                                    4, &image, pixels, PIXELS);
        run->glyphs = Outcome(run->glyphs, result, &image, pixels, 4);
        result = muiRenderColorGlyph(scene->service, scene->fonts.color, glyph, size, offset, 0,
                                     ink, &image, pixels, PIXELS);
        run->glyphs = Outcome(run->glyphs, result, &image, pixels, 4);
        (void)Returned(watch, true);
    }
}

// One run, start to end, in the register the thread has: every step that
// succeeded is counted.
static void Work(Run* run, Watch* watch)
{
    *run = (Run){2166136261u, 2166136261u, 2166136261u, 2166136261u, 2166136261u, 0};
    Scene scene = {.watch = watch};
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
    (void)Returned(watch, true);
    free(pixels);
}

// The scene in the default environment and in each host's: the same
// bytes, the host's functions in the host's setting.
static void TestHostSettings(void)
{
    const TestFloatSetting original = TestFloatGet();
    Watch plainWatch = {.host = original};
    Run plain;
    Work(&plain, &plainWatch);
    CHECK(plain.steps == 1 + FRAMES, "every step in the default environment");
    CHECK(plainWatch.wrongCalls == 0 && plainWatch.wrongReturns == 0, "the default setting kept");
    for (int i = 0; i < TestFloatHostileCount(); i++)
    {
        TestFloatSet(TestFloatHostileAt(i));
        Watch watch = {.host = TestFloatGet()};
        Run hosted;
        Work(&hosted, &watch);
        TestFloatSet(original);
        CHECK(hosted.steps == plain.steps, "every step in the host's setting");
        CHECK(hosted.layout == plain.layout, "the same layout and baselines");
        CHECK(hosted.list == plain.list, "the same draw list bytes");
        CHECK(hosted.hits == plain.hits, "the same hits");
        CHECK(hosted.access == plain.access, "the same accessibility updates");
        CHECK(hosted.glyphs == plain.glyphs, "the same glyph images");
        CHECK(watch.calls > FRAMES * 4, "the host's functions called");
        CHECK(watch.wrongCalls == 0, "every host function in the host's setting");
        CHECK(watch.wrongReturns == 0, "every call leaves the host's setting");
    }
}

// A range checked by comparing its ends: a minimum of a positive
// subnormal above a maximum of 0 is refused in the host's
// denormals-are-zero, which would read it as 0, as in the default
// environment.
static void TestSubnormalRange(void)
{
    const TestFloatSetting original = TestFloatGet();
    const muiContextDef def = muiDefaultContextDef();
    const muiNodeDef nodeDef = muiDefaultNodeDef();
    muiContext* context = nullptr;
    muiNodeId node = {0, 0};
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    CHECK(muiCreateNode(context, &nodeDef, &node) == mui_success, "node");
    muiValueRange range = muiDefaultValueRange();
    range.minimum = 0x1p-140f;
    range.maximum = 0.0f;
    range.value = 0.0f;
    const muiResult plain = muiNode_SetValueRange(context, node, &range);
    TestFloatSet(TestFloatHostile());
    const muiResult hosted = muiNode_SetValueRange(context, node, &range);
    TestFloatSet(original);
    CHECK(plain == mui_errorInvalid, "a minimum above the maximum refused");
    CHECK(hosted == plain, "the same answer in the host's setting");
    muiDestroyContext(context);
}

int main(void)
{
    MakeInputs();
    TestHostSettings();
    TestSubnormalRange();
    return s_failures == 0 ? 0 : 1;
}
