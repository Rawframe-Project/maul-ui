// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font instances (record mui-0006): a text style's weight, slant and size
// setting a variable font's wght, ital, slnt and opsz axes, in shaping and
// in glyph images alike; bold and oblique made for faces without them;
// and the keys glyph runs carry for them. MaulVariable.ttf's A is a box
// 400 wide with an advance of 500 that each axis moves by a known amount
// (make_variable_font.py); Ahem's glyphs are boxes an em wide.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ahem.inc"
#include "italic.inc"
#include "liberation_sans.inc"
#include "variable.inc"
#include "variable_italic.inc"
#include "variable_slant.inc"

#define SIZE   MUI_PROPERTY_BIT(mui_propertyFontSize)
#define WEIGHT MUI_PROPERTY_BIT(mui_propertyFontWeight)
#define SLANT  MUI_PROPERTY_BIT(mui_propertyFontSlant)

enum
{
    // The A of the Maul fonts and Ahem's X.
    GLYPH_A = 1,
    GLYPH_X = 58
};

typedef struct Scene
{
    muiTextService* service;
    muiFontId font;
    muiContext* context;
    muiTextHost host;
    muiNodeId node;
} Scene;

// A node showing text in a font, the service's default.
static Scene MakeScene(const unsigned char* data, size_t size, const char* text)
{
    Scene scene = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    CHECK(muiCreateTextService(&def, &scene.service) == mui_success, "service");
    muiFontDef font = muiDefaultFontDef();
    font.data = data;
    font.size = size;
    font.dataMode = mui_fontDataBorrow;
    CHECK(muiCreateFont(scene.service, &font, &scene.font) == mui_success &&
              muiSetDefaultFont(scene.service, scene.font) == mui_success,
          "the font");
    muiContextDef context = muiDefaultContextDef();
    CHECK(muiCreateContext(&context, &scene.context) == mui_success, "context");
    scene.host = (muiTextHost){scene.service, scene.context};
    muiTextBlockId block = {0, 0};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = text;
    blockDef.length = strlen(text);
    CHECK(muiCreateTextBlock(scene.service, &blockDef, &block) == mui_success, "block");
    muiNodeDef node = muiDefaultNodeDef();
    node.hostKey = muiTextBlock_GetKey(block);
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

// Styles the node, lays it out and returns its one line's width.
static float Width(Scene* scene, float size, float weight, muiFontSlant slant)
{
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, size, mui_dimensionValue};
    style.weight = weight;
    style.slant = slant;
    CHECK(muiNode_SetTextValues(scene->context, scene->node, &style, SIZE | WEIGHT | SLANT) ==
              mui_success,
          "style");
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &scene->host,
                                  0,       NULL,    {0, 0, 0, 0}};
    CHECK(muiComputeLayout(scene->context, scene->node, &input) == mui_success, "layout");
    return muiMeasureText(&scene->host, scene->node,
                          muiNode_GetHostKey(scene->context, scene->node),
                          (muiMeasureAxis){0.0f, mui_measureMaxContent},
                          (muiMeasureAxis){0.0f, mui_measureMaxContent})
        .width;
}

// The font key of the node's first glyph run, as laid out last.
static uint64_t RunFont(Scene* scene)
{
    const muiDrawInput input = {1, 1.0f, muiPaintText, &scene->host};
    CHECK(muiBuildDrawList(scene->context, scene->node, &input) == mui_success, "paint");
    muiDrawList list;
    CHECK(muiGetDrawList(scene->context, &list) == mui_success && list.commandCount == 1,
          "one run");
    return list.commands[0].glyphRun.font;
}

// The image of a glyph in a key's instance at a size in pixels.
// The last glyph image's coverage, rows from the top.
static unsigned char s_pixels[128 * 128];

static muiGlyphImage ImageAt(Scene* scene, uint64_t font, uint32_t glyph, float size)
{
    muiGlyphImage image = {0, 0, 0, 0};
    CHECK(muiRenderGlyph(scene->service, font, glyph, size, 0.0f, &image, s_pixels,
                         sizeof s_pixels) == mui_success,
          "image");
    return image;
}

static muiGlyphImage Image(Scene* scene, uint64_t font, uint32_t glyph)
{
    return ImageAt(scene, font, glyph, 10.0f);
}

static bool Near(float value, float expected)
{
    return fabsf(value - expected) <= 0.001f;
}

static void TestAxes(void)
{
    Scene scene = MakeScene(s_variable, sizeof s_variable, "A");
    // At 16 units opsz is at its default.
    CHECK(Width(&scene, 16.0f, 400.0f, mui_slantNormal) == 8.0f, "the default instance");
    uint64_t regular = RunFont(&scene);
    CHECK(regular != muiFont_GetKey(scene.font) && Image(&scene, regular, GLYPH_A).width == 4,
          "an instance of its own, drawn as the default");
    CHECK(Near(Width(&scene, 16.0f, 900.0f, mui_slantNormal), 14.4f) &&
              Image(&scene, RunFont(&scene), GLYPH_A).width == 8,
          "wght 900: 400 units wider, shaped and drawn");
    CHECK(Near(Width(&scene, 16.0f, 100.0f, mui_slantNormal), 5.6f), "wght 100");
    CHECK(Near(Width(&scene, 16.0f, 700.0f, mui_slantNormal), 11.84f),
          "wght 700, between the masters");
    (void)Width(&scene, 16.0f, 900.0f, mui_slantNormal);
    uint64_t heaviest = RunFont(&scene);
    CHECK(Near(Width(&scene, 16.0f, 1000.0f, mui_slantNormal), 14.4f) &&
              RunFont(&scene) == heaviest,
          "a weight past the axis is held to it, in one key");
    CHECK(Near(Width(&scene, 72.0f, 400.0f, mui_slantNormal), 28.8f) &&
              Near(Width(&scene, 8.0f, 400.0f, mui_slantNormal), 4.4f),
          "opsz follows the size");
    (void)Width(&scene, 100.0f, 400.0f, mui_slantNormal);
    uint64_t largest = RunFont(&scene);
    CHECK(Near(Width(&scene, 200.0f, 400.0f, mui_slantNormal), 80.0f) && RunFont(&scene) == largest,
          "a size past the axis is held to it, in one key");
    // ital moves the top right by 100 units, slnt at -14 by 175.
    CHECK(Width(&scene, 16.0f, 400.0f, mui_slantItalic) == 8.0f &&
              ImageAt(&scene, RunFont(&scene), GLYPH_A, 20.0f).width == 10,
          "italic sets ital to 1");
    CHECK(Width(&scene, 16.0f, 400.0f, mui_slantOblique) == 8.0f &&
              Image(&scene, RunFont(&scene), GLYPH_A).width == 6,
          "oblique sets slnt");
    CHECK(Image(&scene, regular, GLYPH_A).width == 4, "back to the regular instance");
    // More instances than are kept for shaping, twice over.
    bool held = true;
    for (int round = 0; round < 2; round++)
    {
        for (int weight = 100; weight <= 900; weight += 100)
        {
            float units = 500.0f + (weight >= 400 ? (float)(weight - 400) * 0.8f
                                                  : (float)(weight - 400) * 0.5f);
            held = held && fabsf(Width(&scene, 16.0f, (float)weight, mui_slantNormal) -
                                 units * 0.016f) <= 0.017f;
        }
    }
    CHECK(held, "instances made again as they are needed");
    FreeScene(&scene);
}

static void TestSlantAxis(void)
{
    Scene scene = MakeScene(s_variableSlant, sizeof s_variableSlant, "A");
    CHECK(Width(&scene, 16.0f, 400.0f, mui_slantItalic) == 8.0f &&
              Image(&scene, RunFont(&scene), GLYPH_A).width == 6,
          "italic without ital sets slnt");
    // wght stops at 500 (an advance of 600), so bold is made: an em/24
    // wider.
    float bold = Width(&scene, 16.0f, 700.0f, mui_slantNormal);
    CHECK(fabsf(bold - 642.0f * 0.016f) <= 0.017f, "bold past the axis is made");
    CHECK(Near(Width(&scene, 16.0f, 500.0f, mui_slantNormal), 9.6f), "within the axis, not made");
    FreeScene(&scene);
}

static void TestItalicAxisAlone(void)
{
    Scene scene = MakeScene(s_variableItalic, sizeof s_variableItalic, "A");
    CHECK(Width(&scene, 16.0f, 400.0f, mui_slantOblique) == 8.0f &&
              ImageAt(&scene, RunFont(&scene), GLYPH_A, 20.0f).width == 10,
          "oblique without slnt sets ital");
    FreeScene(&scene);
}

static void TestItalicFace(void)
{
    Scene scene = MakeScene(s_italic, sizeof s_italic, "A");
    CHECK(Width(&scene, 16.0f, 400.0f, mui_slantItalic) == 8.0f &&
              RunFont(&scene) == muiFont_GetKey(scene.font) &&
              Image(&scene, RunFont(&scene), GLYPH_A).width == 4,
          "an italic face is not sheared again");
    FreeScene(&scene);
}

static void TestMarksSlanted(void)
{
    // x with a combining acute, which Liberation Sans places above it: a
    // made oblique moves the mark right by a quarter of how far it rises.
    Scene scene = MakeScene(s_liberationSans, sizeof s_liberationSans, "x\xCC\x81");
    (void)Width(&scene, 20.0f, 400.0f, mui_slantNormal);
    const muiDrawInput input = {1, 1.0f, muiPaintText, &scene.host};
    muiDrawList list;
    CHECK(muiBuildDrawList(scene.context, scene.node, &input) == mui_success &&
              muiGetDrawList(scene.context, &list) == mui_success && list.glyphCount == 2,
          "x and its mark");
    float upright = list.glyphs[1].x;
    float rise = list.glyphs[0].y - list.glyphs[1].y;
    (void)Width(&scene, 20.0f, 400.0f, mui_slantItalic);
    CHECK(muiBuildDrawList(scene.context, scene.node, &input) == mui_success &&
              muiGetDrawList(scene.context, &list) == mui_success && list.glyphCount == 2,
          "slanted");
    CHECK(rise != 0.0f && fabsf(list.glyphs[1].x - upright - rise * 0.25f) <= 0.05f,
          "the mark moved along the slant");
    FreeScene(&scene);
}

static void TestSynthesis(void)
{
    Scene scene = MakeScene(s_ahem, sizeof s_ahem, "X");
    CHECK(Width(&scene, 10.0f, 400.0f, mui_slantNormal) == 10.0f &&
              RunFont(&scene) == muiFont_GetKey(scene.font),
          "a regular upright face as it is: the font's own key");
    CHECK(Width(&scene, 10.0f, 599.0f, mui_slantNormal) == 10.0f &&
              RunFont(&scene) == muiFont_GetKey(scene.font),
          "below 600, not bold");
    float bold = Width(&scene, 10.0f, 600.0f, mui_slantNormal);
    uint64_t boldKey = RunFont(&scene);
    muiGlyphImage image = Image(&scene, boldKey, GLYPH_X);
    CHECK(fabsf(bold - 10.42f) <= 0.011f && image.left == 0 && image.width == 11 &&
              image.top == 9 && image.height == 11,
          "bold: an em/24 wider and taller, the left side kept");
    // At 48 pixels the strength is 2 pixels: the box grows right and up.
    image = ImageAt(&scene, boldKey, GLYPH_X, 48.0f);
    CHECK(image.left == 0 && image.width == 50 && image.top == 41 && image.height == 51,
          "bold grows right and up from the left side");
    // Grown by whole pixels, its right column and top row are covered
    // whole (found by a mutant growing it by an em/25).
    CHECK(s_pixels[25 * 50 + 49] == 255 && s_pixels[1 * 50 + 25] == 255, "by exactly 2 pixels");
    CHECK(Width(&scene, 10.0f, 400.0f, mui_slantItalic) == 10.0f, "oblique keeps the advance");
    uint64_t italicKey = RunFont(&scene);
    image = Image(&scene, italicKey, GLYPH_X);
    CHECK(image.left == -1 && image.width == 13 && image.top == 8 && image.height == 10,
          "italic sheared by a quarter: the top 2 right, the bottom half left");
    CHECK(Width(&scene, 10.0f, 400.0f, mui_slantOblique) == 10.0f && RunFont(&scene) == italicKey,
          "oblique as italic");
    static unsigned char field[64 * 64];
    muiGlyphImage fieldImage = {0, 0, 0, 0};
    CHECK(muiRenderGlyphField(scene.service, boldKey, GLYPH_X, 10.0f, 2, &fieldImage, field,
                              sizeof field) == mui_success &&
              fieldImage.width == 15,
          "fields of a made bold");
    // The atlas keeps each instance apart.
    muiGlyphAtlas* atlas = NULL;
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    CHECK(muiCreateGlyphAtlas(scene.service, &def, &atlas) == mui_success, "atlas");
    muiAtlasGlyph plain = {0};
    muiAtlasGlyph heavy = {0};
    CHECK(
        muiGlyphAtlas_Get(atlas, muiFont_GetKey(scene.font), GLYPH_X, 10.0f, 0.0f, 0.0f, &plain) ==
                mui_success &&
            muiGlyphAtlas_Get(atlas, boldKey, GLYPH_X, 10.0f, 0.0f, 0.0f, &heavy) == mui_success &&
            plain.width == 10 && heavy.width == 11 && heavy.u != plain.u,
        "instances apart in an atlas");
    muiDestroyGlyphAtlas(atlas);
    FreeScene(&scene);
}

static void TestKeys(void)
{
    Scene scene = MakeScene(s_ahem, sizeof s_ahem, "X");
    (void)Width(&scene, 10.0f, 700.0f, mui_slantNormal);
    uint64_t bold = RunFont(&scene);
    static unsigned char pixels[64 * 64];
    muiGlyphImage image = {0, 0, 0, 0};
    CHECK(muiRenderGlyph(scene.service, bold | 1ull << 63, GLYPH_X, 10.0f, 0.0f, &image, pixels,
                         sizeof pixels) == mui_errorStale,
          "the top bit names no font");
    CHECK(muiRenderGlyph(scene.service, bold & ~0xFFFFFFFFFFull, GLYPH_X, 10.0f, 0.0f, &image,
                         pixels, sizeof pixels) == mui_errorStale,
          "an instance alone names no font, not the default");
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    muiFontId other = {0, 0};
    CHECK(muiCreateFont(scene.service, &font, &other) == mui_success, "another font");
    uint64_t otherBold = muiFont_GetKey(other) | (bold & ~0xFFFFFFFFFFull);
    CHECK(muiRenderGlyph(scene.service, otherBold, GLYPH_X, 10.0f, 0.0f, &image, pixels,
                         sizeof pixels) == mui_success &&
              image.width == 11,
          "an instance's bits with another font");
    CHECK(muiDestroyFont(scene.service, other) == mui_success &&
              muiRenderGlyph(scene.service, otherBold, GLYPH_X, 10.0f, 0.0f, &image, pixels,
                             sizeof pixels) == mui_errorStale,
          "an instance of a font gone");
    FreeScene(&scene);
}

int main(void)
{
    TestAxes();
    TestSlantAxis();
    TestItalicAxisAlone();
    TestItalicFace();
    TestMarksSlanted();
    TestSynthesis();
    TestKeys();
    return s_failures == 0 ? 0 : 1;
}
