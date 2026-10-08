// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font fallback (record mui-0006): each grapheme cluster drawn in the
// first font of the chain (the style's font or family, the family's
// fallbacks, the service's) that has all its characters, characters of
// no one script staying in the font before them, glyph runs split where
// the font changes, widths added across fonts of different units per em,
// and calls outside the contract refused. MaulItalic.ttf has only an A,
// half an em wide; Ahem has boxes an em wide for most characters, and no
// combining acute, which Liberation Sans has.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ahem.inc"
#include "coverage.inc"
#include "italic.inc"
#include "liberation_sans.inc"

#define FONT MUI_PROPERTY_BIT(mui_propertyFont)
#define SIZE MUI_PROPERTY_BIT(mui_propertyFontSize)

typedef struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    muiFontId maul;
    muiFontId ahem;
    muiFontId liberation;
    muiFontId coverage;
    muiTextBlockId block;
    muiNodeId node;
} Scene;

static muiFontId MakeFont(muiTextService* service, const unsigned char* data, size_t size)
{
    muiFontDef def = muiDefaultFontDef();
    def.data = data;
    def.size = size;
    def.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    CHECK(muiCreateFont(service, &def, &font) == mui_success, "font");
    return font;
}

// A node at size 10 whose text is in the font of key.
static Scene MakeScene(void)
{
    Scene scene = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    CHECK(muiCreateTextService(&def, &scene.service) == mui_success, "service");
    scene.maul = MakeFont(scene.service, s_italic, sizeof s_italic);
    scene.ahem = MakeFont(scene.service, s_ahem, sizeof s_ahem);
    scene.liberation = MakeFont(scene.service, s_liberationSans, sizeof s_liberationSans);
    scene.coverage = MakeFont(scene.service, s_coverage, sizeof s_coverage);
    muiContextDef context = muiDefaultContextDef();
    CHECK(muiCreateContext(&context, &scene.context) == mui_success, "context");
    scene.host = (muiTextHost){scene.service, scene.context};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = "";
    blockDef.length = 0;
    CHECK(muiCreateTextBlock(scene.service, &blockDef, &scene.block) == mui_success, "block");
    muiNodeDef node = muiDefaultNodeDef();
    node.hostKey = muiTextBlock_GetKey(scene.block);
    CHECK(muiCreateNode(scene.context, &node, &scene.node) == mui_success, "node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(scene.context, scene.node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
          "host content");
    return scene;
}

static void FreeScene(Scene* scene)
{
    muiDestroyContext(scene->context);
    muiDestroyTextService(scene->service);
}

// Lays out text in the font of key at size 10 and returns its width.
static float Show(Scene* scene, uint64_t font, const char* text)
{
    CHECK(muiTextBlock_SetText(scene->service, scene->block, text, strlen(text)) == mui_success &&
              muiNode_MarkContentChanged(scene->context, scene->node) == mui_success,
          "text");
    muiTextStyle style = muiDefaultTextStyle();
    style.font = font;
    style.size = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiNode_SetTextValues(scene->context, scene->node, &style, FONT | SIZE) == mui_success,
          "style");
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &scene->host,
                                  0,       NULL,    {0, 0, 0, 0}};
    CHECK(muiComputeLayout(scene->context, scene->node, &input) == mui_success, "layout");
    return muiMeasureText(&scene->host, scene->node, muiTextBlock_GetKey(scene->block),
                          (muiMeasureAxis){0.0f, mui_measureMaxContent},
                          (muiMeasureAxis){0.0f, mui_measureMaxContent})
        .width;
}

static muiDrawList Paint(Scene* scene)
{
    const muiDrawInput input = {1, 1.0f, muiPaintText, &scene->host};
    CHECK(muiBuildDrawList(scene->context, scene->node, &input) == mui_success, "paint");
    muiDrawList list;
    CHECK(muiGetDrawList(scene->context, &list) == mui_success, "list");
    return list;
}

// Whether the list's runs are in the fonts given, in order.
static bool RunsIn(const muiDrawList* list, const muiFontId* fonts, uint32_t count)
{
    bool same = list->commandCount == count;
    for (uint32_t i = 0; same && i < count; i++)
    {
        same = list->commands[i].glyphRun.font == muiFont_GetKey(fonts[i]);
    }
    return same;
}

static void TestServiceFallbacks(void)
{
    Scene scene = MakeScene();
    uint64_t maul = muiFont_GetKey(scene.maul);
    uint64_t ahem = muiFont_GetKey(scene.ahem);
    CHECK(Show(&scene, maul, "A") == 5.0f, "the first font alone");
    CHECK(muiSetFallbackFonts(scene.service, &ahem, 1) == mui_success, "fallbacks");
    CHECK(Show(&scene, maul, "AB") == 15.0f, "B from the fallback, the width of both");
    muiDrawList list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.maul, scene.ahem}, 2) && list.glyphs[1].x == 5.0f,
          "a run for each font");
    // The space has no one script: it stays in Ahem after the B; the A
    // goes back to the first font.
    CHECK(Show(&scene, maul, "AB A") == 30.0f, "a space after a fallback");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.maul, scene.ahem, scene.maul}, 3) &&
              list.commands[1].glyphRun.glyphCount == 2,
          "the space kept with the B");
    // After an A it takes the first font with it: Ahem again.
    CHECK(Show(&scene, maul, "A A") == 20.0f, "a space the first font lacks");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.maul, scene.ahem, scene.maul}, 3), "three runs");
    // A joiner draws nothing and asks for no font.
    CHECK(Show(&scene, maul,
               "A\xE2\x80\x8D"
               "A") == 10.0f,
          "a joiner in the cluster");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.maul}, 1), "one run");
    // A character no font has: the first font's missing glyph.
    (void)Show(&scene, maul, "\xE0\xB8\x81");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.maul}, 1) && list.glyphs[0].id == 0,
          "none has it: the first font's missing glyph");
    CHECK(muiSetFallbackFonts(scene.service, NULL, 0) == mui_success &&
              Show(&scene, maul, "AB") == 10.0f,
          "fallbacks gone: the block shaped again, B missing");
    FreeScene(&scene);
}

static void TestClusters(void)
{
    // x and a combining acute: Ahem has x but not the acute, so the
    // cluster goes whole to Liberation Sans, whose units per em differ.
    Scene scene = MakeScene();
    uint64_t ahem = muiFont_GetKey(scene.ahem);
    uint64_t liberation = muiFont_GetKey(scene.liberation);
    CHECK(muiSetFallbackFonts(scene.service, &liberation, 1) == mui_success, "fallbacks");
    float alone = Show(&scene, liberation, "x\xCC\x81");
    float mixed = Show(&scene, ahem, "Ax\xCC\x81");
    muiDrawList list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.ahem, scene.liberation}, 2) &&
              list.commands[1].glyphRun.glyphCount == 2 && list.glyphs[1].x == 10.0f,
          "the cluster whole in the font with both");
    CHECK(fabsf(mixed - (10.0f + alone)) <= 1e-4f, "widths of 1000 and 2048 units per em added");
    float acute = list.glyphs[2].x;
    (void)Show(&scene, liberation, "x\xCC\x81");
    list = Paint(&scene);
    CHECK(list.glyphCount == 2 && fabsf(acute - (10.0f + list.glyphs[1].x)) <= 1e-4f,
          "the mark placed in its own font's units");
    // Liberation Sans's x is 1024 of 2048 units wide: half an em.
    (void)Show(&scene, liberation, "xx");
    list = Paint(&scene);
    CHECK(list.glyphs[1].x == 5.0f, "Liberation Sans's x, half an em");
    FreeScene(&scene);
}

static void TestChoosing(void)
{
    Scene scene = MakeScene();
    uint64_t maul = muiFont_GetKey(scene.maul);
    uint64_t ahem = muiFont_GetKey(scene.ahem);
    uint64_t liberation = muiFont_GetKey(scene.liberation);
    uint64_t coverage = muiFont_GetKey(scene.coverage);
    muiDrawList list;
    // Liberation Sans lacks the ideograph; the comma after it, of no one
    // script, stays in the coverage font though Liberation Sans has it.
    CHECK(muiSetFallbackFonts(scene.service, &coverage, 1) == mui_success &&
              Show(&scene, liberation, "\xE4\xB8\x80,") == 10.0f,
          "a comma after a fallback");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.coverage}, 1), "one run");
    // A mark that starts a paragraph is chosen within its own paragraph,
    // as paragraphs are shaped apart: the first font that has it.
    (void)Show(&scene, liberation, "\xE4\xB8\x80\n\xCC\x81");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.coverage, scene.liberation}, 2),
          "a mark alone, in its own paragraph");
    // A tab is no character a font is chosen for.
    (void)Show(&scene, liberation, "A\tA");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.liberation}, 1), "a tab in the font around it");
    // No font has x and the acute: the first with the x.
    CHECK(muiSetFallbackFonts(scene.service, &ahem, 1) == mui_success, "fallbacks");
    (void)Show(&scene, maul, "x\xCC\x81");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.ahem}, 1), "the font with the base");
    // An emoji no font has, of no one script: the font before it.
    (void)Show(&scene, maul, "B\xF0\x9F\x98\x80");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.ahem}, 1), "none has it: the font before");
    // A cluster that draws nothing stays in the font before it.
    (void)Show(&scene, maul,
               "B\xE2\x80\x8B"
               "B");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.ahem}, 1) && list.glyphCount == 2,
          "a zero width space between");
    // Fallbacks take the style's instance: Ahem made bold.
    muiTextStyle style = muiDefaultTextStyle();
    style.weight = 700.0f;
    CHECK(muiNode_SetTextValues(scene.context, scene.node, &style,
                                MUI_PROPERTY_BIT(mui_propertyFontWeight)) == mui_success,
          "bold");
    (void)Show(&scene, maul, "AB");
    list = Paint(&scene);
    CHECK(list.commandCount == 2 && (list.commands[1].glyphRun.font & 0xFFFFFFFFFFull) == ahem &&
              list.commands[1].glyphRun.font != ahem,
          "a fallback in the style's instance");
    FreeScene(&scene);
}

static void TestFamilyFallbacks(void)
{
    Scene scene = MakeScene();
    uint64_t ahem = muiFont_GetKey(scene.ahem);
    uint64_t liberation = muiFont_GetKey(scene.liberation);
    muiFontFamilyDef def = muiDefaultFontFamilyDef();
    def.faces = &scene.ahem;
    def.faceCount = 1;
    muiFontFamilyId ahemFamily = {0, 0};
    CHECK(muiCreateFontFamily(scene.service, &def, &ahemFamily) == mui_success, "Ahem's family");
    uint64_t ahemFamilyKey = muiFontFamily_GetKey(ahemFamily);
    def.faces = &scene.maul;
    def.fallbacks = &ahemFamilyKey;
    def.fallbackCount = 1;
    muiFontFamilyId family = {0, 0};
    CHECK(muiCreateFontFamily(scene.service, &def, &family) == mui_success, "a family");
    // The family's fallbacks come before the service's.
    CHECK(muiSetFallbackFonts(scene.service, &liberation, 1) == mui_success, "fallbacks");
    CHECK(Show(&scene, muiFontFamily_GetKey(family), "AB") == 15.0f, "B from the family's");
    muiDrawList list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.maul, scene.ahem}, 2), "Ahem through its family");
    // A font's own key has no family fallbacks: the service's then.
    (void)Show(&scene, muiFont_GetKey(scene.maul), "AB");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.maul, scene.liberation}, 2), "the service's alone");
    // The first font among the fallbacks again is tried once.
    CHECK(muiSetFallbackFonts(scene.service, &ahem, 1) == mui_success &&
              Show(&scene, ahem, "AB") == 20.0f,
          "a fallback that is the first font");
    list = Paint(&scene);
    CHECK(RunsIn(&list, (muiFontId[]){scene.ahem}, 1), "one run");
    FreeScene(&scene);
}

static void TestContract(void)
{
    Scene scene = MakeScene();
    uint64_t ahem = muiFont_GetKey(scene.ahem);
    uint64_t nine[9] = {ahem, ahem, ahem, ahem, ahem, ahem, ahem, ahem, ahem};
    CHECK(muiSetFallbackFonts(NULL, &ahem, 1) == mui_errorInvalid &&
              muiSetFallbackFonts(scene.service, NULL, 1) == mui_errorInvalid &&
              muiSetFallbackFonts(scene.service, nine, 9) == mui_errorInvalid &&
              muiSetFallbackFonts(scene.service, (uint64_t[]){0}, 1) == mui_errorInvalid &&
              muiSetFallbackFonts(scene.service, (uint64_t[]){ahem | 1ull << 40}, 1) ==
                  mui_errorInvalid &&
              muiSetFallbackFonts(scene.service, nine, 8) == mui_success,
          "keys outside the contract");
    muiFontId gone = MakeFont(scene.service, s_ahem, sizeof s_ahem);
    uint64_t goneKey = muiFont_GetKey(gone);
    CHECK(muiDestroyFont(scene.service, gone) == mui_success &&
              muiSetFallbackFonts(scene.service, (uint64_t[]){ahem, goneKey}, 2) ==
                  mui_errorStale &&
              muiSetFallbackFonts(scene.service,
                                  (uint64_t[]){muiFontFamily_GetKey((muiFontFamilyId){1, 1})},
                                  1) == mui_errorStale,
          "keys gone");
    // The fallbacks stay as they were after a failure: Ahem still.
    CHECK(Show(&scene, muiFont_GetKey(scene.maul), "AB") == 15.0f, "kept");
    muiFontFamilyDef def = muiDefaultFontFamilyDef();
    def.faces = &scene.maul;
    def.faceCount = 1;
    def.fallbackCount = 1;
    muiFontFamilyId family = {0, 0};
    CHECK(muiCreateFontFamily(scene.service, &def, &family) == mui_errorInvalid, "NULL fallbacks");
    def.fallbacks = nine;
    def.fallbackCount = 9;
    CHECK(muiCreateFontFamily(scene.service, &def, &family) == mui_errorInvalid, "too many");
    def.fallbacks = &goneKey;
    def.fallbackCount = 1;
    CHECK(muiCreateFontFamily(scene.service, &def, &family) == mui_errorStale && family.index1 == 0,
          "a fallback gone");
    // A fallback destroyed later is passed over.
    muiFontId later = MakeFont(scene.service, s_ahem, sizeof s_ahem);
    uint64_t laterKey = muiFont_GetKey(later);
    CHECK(muiSetFallbackFonts(scene.service, &laterKey, 1) == mui_success &&
              muiDestroyFont(scene.service, later) == mui_success &&
              Show(&scene, muiFont_GetKey(scene.maul), "AB") == 10.0f,
          "a fallback gone later");
    FreeScene(&scene);
}

int main(void)
{
    TestServiceFallbacks();
    TestClusters();
    TestChoosing();
    TestFamilyFallbacks();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
