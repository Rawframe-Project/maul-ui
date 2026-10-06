// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Seeded fuzzing of the text component (record mui-0006), run with every
// test under the sanitizers: damaged fonts are refused or read without
// harm, and random UTF-8 in random styles and widths lays out with
// measuring and painting in agreement. The seeds are fixed, so a failure
// repeats.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"
#include "liberation_sans.inc"

enum
{
    FONT_ROUNDS = 200,
    TEXT_ROUNDS = 400,
    TEXT_LIMIT = 96
};

static uint32_t Next(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

// A copy of a font with bytes changed or its end cut, in the ways a
// damaged file is.
static size_t Damage(unsigned char* out, const unsigned char* font, size_t size, uint32_t* state)
{
    memcpy(out, font, size);
    switch (Next(state) % 3)
    {
    case 0:
        // A few bytes anywhere, often in the table directory.
        for (uint32_t i = 0, n = 1 + Next(state) % 8; i < n; i++)
        {
            size_t at = Next(state) % 4 == 0 ? Next(state) % 256 : Next(state) % size;
            out[at] = (unsigned char)Next(state);
        }
        return size;
    case 1:
        return 12 + Next(state) % (size - 12);
    default:
        // A table's offset or length word set to an extreme.
        out[12 + 16 * (Next(state) % 8) + 8 + Next(state) % 8] = 0xFF;
        return size;
    }
}

static unsigned char s_pixels[1 << 20];

// Renders a damaged font's first glyphs at random sizes and offsets:
// each is rendered, refused, or too large, and none harms anything.
// Returns how many were rendered.
static int RenderSome(muiTextService* service, muiFontId font, uint32_t* state)
{
    int rendered = 0;
    muiFontMetrics metrics;
    CHECK(muiFont_GetMetrics(service, font, &metrics) == mui_success, "metrics");
    for (uint32_t glyph = 0; glyph < 12 && glyph < metrics.glyphCount; glyph++)
    {
        float size = 1.0f + (float)(Next(state) % 2000) / 10.0f;
        float offset = (float)(Next(state) % 4) / 4.0f;
        muiGlyphImage image = {0, 0, 0, 0};
        muiResult result = muiRenderGlyph(service, muiFont_GetKey(font), glyph, size, offset,
                                          &image, s_pixels, sizeof s_pixels);
        CHECK(result == mui_success || result == mui_errorFormat ||
                  (result == mui_errorCapacity &&
                   (size_t)image.width * image.height > sizeof s_pixels),
              "a glyph rendered or refused");
        result = muiRenderGlyphField(service, muiFont_GetKey(font), glyph, size / 4.0f, 4, &image,
                                     s_pixels, sizeof s_pixels);
        CHECK(result == mui_success || result == mui_errorFormat ||
                  (result == mui_errorCapacity &&
                   (size_t)image.width * image.height > sizeof s_pixels),
              "a field rendered or refused");
        rendered += result == mui_success && image.width != 0 ? 1 : 0;
    }
    return rendered;
}

static void TestDamagedFonts(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    muiContextDef contextDef = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&contextDef, &context) == mui_success, "context");
    muiTextBlockId block = {0, 0};
    const char text[] = "AV fi \xD7\x90\xD7\x91 \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    CHECK(muiCreateTextBlock(service, text, sizeof text - 1, &block) == mui_success, "block");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    nodeDef.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &node) == mui_success, "node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, node, &layout, MUI_PROPERTY_BIT(mui_propertyContent)) ==
              mui_success,
          "host content");
    unsigned char* copy = malloc(sizeof s_liberationSans);
    CHECK(copy != NULL, "copy");
    uint32_t state = 2026;
    int read = 0;
    int rendered = 0;
    for (int round = 0; round < FONT_ROUNDS && copy != NULL; round++)
    {
        bool ahem = round % 2 == 0;
        size_t size = Damage(copy, ahem ? s_ahem : s_liberationSans,
                             ahem ? sizeof s_ahem : sizeof s_liberationSans, &state);
        muiFontDef fontDef = muiDefaultFontDef();
        fontDef.data = copy;
        fontDef.size = size;
        muiFontId font = {0, 0};
        muiResult result = muiCreateFont(service, &fontDef, &font);
        CHECK(result == mui_success || result == mui_errorFormat, "read or refused");
        if (result != mui_success)
        {
            continue;
        }
        // What was read lays text out and renders glyphs without harm.
        read++;
        rendered += RenderSome(service, font, &state);
        CHECK(muiSetDefaultFont(service, font) == mui_success &&
                  muiNode_MarkContentChanged(context, node) == mui_success,
              "the default");
        muiTextHost host = {service, context};
        const muiLayoutInput input = {200.0f, 1000.0f, muiMeasureText, &host, 0, NULL};
        const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
        CHECK(muiComputeLayout(context, node, &input) == mui_success &&
                  muiBuildDrawList(context, node, &draw) != mui_errorInvalid,
              "laid out");
        CHECK(muiSetDefaultFont(service, (muiFontId){0, 0}) == mui_success &&
                  muiDestroyFont(service, font) == mui_success,
              "destroyed");
    }
    CHECK(read > 0 && read < FONT_ROUNDS, "some damage is read, some refused");
    CHECK(rendered > read, "glyphs of damaged fonts rendered");
    free(copy);
    muiDestroyContext(context);
    muiDestroyTextService(service);
}

// Pieces random text is made of: ASCII, spaces and line breaks, Hebrew,
// Arabic, CJK, an emoji, bidi controls, a combining mark, and bytes that
// are not UTF-8.
static const char* const s_pieces[] = {
    "a",
    "Wo",
    " ",
    "  ",
    "\n",
    "\r\n",
    "\t",
    "-",
    "\xD7\x90\xD7\x91",
    "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85",
    "\xE4\xB8\xAD\xE6\x96\x87",
    "\xF0\x9F\x99\x82",
    "\xE2\x80\xAE",
    "\xE2\x80\xAC",
    "\xE2\x81\xA6",
    "\xCC\x81",
    "\xFF",
    "\xC3",
    "\xE2\x80\xA8",
    "1.5",
};

static size_t RandomText(char* out, uint32_t* state)
{
    size_t length = 0;
    for (uint32_t i = 0, n = Next(state) % 12; i < n; i++)
    {
        const char* piece = s_pieces[Next(state) % (sizeof s_pieces / sizeof s_pieces[0])];
        size_t size = strlen(piece);
        if (length + size > TEXT_LIMIT)
        {
            break;
        }
        memcpy(out + length, piece, size);
        length += size;
    }
    return length;
}

static void RandomStyle(muiContext* context, muiNodeId node, uint32_t* state)
{
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 4.0f + (float)(Next(state) % 40), mui_dimensionValue};
    style.letterSpacing = (muiDimension){0.0f, (float)(Next(state) % 7) - 3.0f, mui_dimensionValue};
    style.lineHeight = Next(state) % 2 == 0 ? (muiDimension){0.0f, 0.0f, mui_dimensionAuto}
                                            : (muiDimension){0.5f + (float)(Next(state) % 3), 0.0f,
                                                             mui_dimensionValue};
    style.align = (muiTextAlign)(Next(state) % 3);
    style.wrap = (muiTextWrap)(Next(state) % 2);
    CHECK(muiNode_SetTextValues(context, node, &style, MUI_TEXT_PROPERTIES) == mui_success,
          "style");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.textDirection = (muiTextDirection)(Next(state) % 3);
    CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "direction");
}

static void TestRandomText(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = s_liberationSans;
    fontDef.size = sizeof s_liberationSans;
    fontDef.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    muiFontMetrics metrics = {0};
    CHECK(muiCreateFont(service, &fontDef, &font) == mui_success &&
              muiSetDefaultFont(service, font) == mui_success &&
              muiFont_GetMetrics(service, font, &metrics) == mui_success,
          "font");
    muiContextDef contextDef = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&contextDef, &context) == mui_success, "context");
    muiTextHost host = {service, context};
    uint32_t state = 77;
    char text[TEXT_LIMIT];
    uint32_t drawn = 0;
    for (int round = 0; round < TEXT_ROUNDS; round++)
    {
        size_t length = RandomText(text, &state);
        muiTextBlockId block = {0, 0};
        CHECK(muiCreateTextBlock(service, text, length, &block) == mui_success, "block");
        muiNodeDef nodeDef = muiDefaultNodeDef();
        nodeDef.hostKey = muiTextBlock_GetKey(block);
        muiNodeId node = {0, 0};
        CHECK(muiCreateNode(context, &nodeDef, &node) == mui_success, "node");
        muiLayoutStyle layout = muiDefaultLayoutStyle();
        layout.content = mui_contentHost;
        CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                      MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
              "content");
        RandomStyle(context, node, &state);
        float available = 5.0f + (float)(Next(&state) % 300);
        const muiLayoutInput input = {available, 1000.0f, muiMeasureText, &host, 0, NULL};
        CHECK(muiComputeLayout(context, node, &input) == mui_success, "layout");
        const muiMeasureMode modes[4] = {mui_measureExact, mui_measureAtMost, mui_measureMaxContent,
                                         mui_measureMinContent};
        for (int m = 0; m < 4; m++)
        {
            muiSize size =
                muiMeasureText(&host, node, nodeDef.hostKey, (muiMeasureAxis){available, modes[m]},
                               (muiMeasureAxis){0.0f, mui_measureMaxContent});
            CHECK(isfinite(size.width) && isfinite(size.height) && size.height >= 0.0f,
                  "a finite size");
        }
        // Painting at the laid-out width draws no more lines than
        // measuring found, a line height apart: baselines never go up, and
        // the first and last are no further apart than the lines allow.
        muiRect rect = muiNode_GetRect(context, node);
        muiComputedTextStyle style;
        CHECK(muiNode_GetTextStyle(context, node, &style) == mui_success, "style read");
        float lineHeight =
            style.automaticLineHeight
                ? (metrics.ascent + metrics.descent) * style.size + metrics.lineGap * style.size
                : style.lineHeight;
        float lines = lineHeight > 0.0f ? roundf(rect.height / lineHeight) : 0.0f;
        const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
        CHECK(muiBuildDrawList(context, node, &draw) == mui_success, "paint");
        muiDrawList list;
        CHECK(muiGetDrawList(context, &list) == mui_success, "list");
        float baselines = 0.0f;
        for (uint32_t i = 0; i < list.commandCount; i++)
        {
            const muiDrawGlyphRun* run = &list.commands[i].glyphRun;
            const muiDrawGlyphRun* first = &list.commands[0].glyphRun;
            CHECK(list.commands[i].kind == mui_drawGlyphRun &&
                      run->firstGlyph + run->glyphCount <= list.glyphCount,
                  "a glyph span");
            CHECK(i == 0 || run->originY >= list.commands[i - 1].glyphRun.originY,
                  "baselines go down");
            CHECK(run->originY - first->originY <= (lines - 1.0f) * lineHeight + 1.0f,
                  "within the measured lines");
            baselines +=
                i == 0 || run->originY != list.commands[i - 1].glyphRun.originY ? 1.0f : 0.0f;
        }
        CHECK(baselines <= lines, "no more lines than measured");
        drawn += list.glyphCount;
        CHECK(muiDestroyNode(context, node) == mui_success &&
                  muiDestroyTextBlock(service, block) == mui_success,
              "destroyed");
    }
    CHECK(muiGetTextServiceFailures(service) == 0 && drawn > 1000, "glyphs drawn, no failures");
    muiDestroyContext(context);
    muiDestroyTextService(service);
}

int main(void)
{
    TestDamagedFonts();
    TestRandomText();
    return s_failures == 0 ? 0 : 1;
}
