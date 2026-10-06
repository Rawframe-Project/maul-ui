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
#include "maul-ui/text_edit.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"
#include "liberation_sans.inc"
#include "variable.inc"

#define INSTANCE                                                                                   \
    (MUI_PROPERTY_BIT(mui_propertyFontWeight) | MUI_PROPERTY_BIT(mui_propertyFontSlant))

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
static int RenderSome(muiTextService* service, muiFontId font, uint64_t key, uint32_t* state)
{
    int rendered = 0;
    muiFontMetrics metrics;
    CHECK(muiFont_GetMetrics(service, font, &metrics) == mui_success, "metrics");
    for (uint32_t glyph = 0; glyph < 12 && glyph < metrics.glyphCount; glyph++)
    {
        float size = 1.0f + (float)(Next(state) % 2000) / 10.0f;
        float offset = (float)(Next(state) % 4) / 4.0f;
        muiGlyphImage image = {0, 0, 0, 0};
        muiResult result =
            muiRenderGlyph(service, key, glyph, size, offset, &image, s_pixels, sizeof s_pixels);
        CHECK(result == mui_success || result == mui_errorFormat ||
                  (result == mui_errorCapacity &&
                   (size_t)image.width * image.height > sizeof s_pixels),
              "a glyph rendered or refused");
        result = muiRenderGlyphField(service, key, glyph, size / 4.0f, 4, &image, s_pixels,
                                     sizeof s_pixels);
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
        // Ahem, Liberation Sans and a variable font in turn.
        const unsigned char* fonts[3] = {s_ahem, s_liberationSans, s_variable};
        const size_t sizes[3] = {sizeof s_ahem, sizeof s_liberationSans, sizeof s_variable};
        size_t size = Damage(copy, fonts[round % 3], sizes[round % 3], &state);
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
        // What was read lays text out in an instance of a random weight
        // and slant, and renders glyphs in it, without harm.
        read++;
        muiTextStyle style = muiDefaultTextStyle();
        style.weight = 1.0f + (float)(Next(&state) % 1000);
        style.slant = (muiFontSlant)(Next(&state) % 3);
        CHECK(muiSetDefaultFont(service, font) == mui_success &&
                  muiNode_SetTextValues(context, node, &style, INSTANCE) == mui_success &&
                  muiNode_MarkContentChanged(context, node) == mui_success,
              "the default");
        muiTextHost host = {service, context};
        const muiLayoutInput input = {200.0f, 1000.0f, muiMeasureText, &host, 0, NULL};
        const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
        CHECK(muiComputeLayout(context, node, &input) == mui_success &&
                  muiBuildDrawList(context, node, &draw) != mui_errorInvalid,
              "laid out");
        muiDrawList list;
        uint64_t key = muiGetDrawList(context, &list) == mui_success && list.commandCount != 0
                           ? list.commands[0].glyphRun.font
                           : muiFont_GetKey(font);
        rendered += RenderSome(service, font, key, &state);
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
    style.weight = 1.0f + (float)(Next(state) % 1000);
    style.slant = (muiFontSlant)(Next(state) % 3);
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

// Moving right and back left along a line comes back to the same x.
static void CheckRightLeft(const muiTextHost* host, muiNodeId node, float width,
                           muiTextPosition position, muiTextCaret caret)
{
    muiTextPosition right = {0, 0};
    muiTextPosition back = {0, 0};
    muiTextCaret there = {0};
    muiTextCaret again = {0};
    CHECK(muiTextMove(host, node, width, position, mui_moveRight, NAN, &right) == mui_success &&
              muiTextGetCaret(host, node, width, right, &there) == mui_success,
          "right");
    if (there.y != caret.y || there.x == caret.x)
    {
        return;
    }
    CHECK(muiTextMove(host, node, width, right, mui_moveLeft, NAN, &back) == mui_success &&
              muiTextGetCaret(host, node, width, back, &again) == mui_success &&
              fabsf(again.x - caret.x) <= 0.01f && again.y == caret.y,
          "right, then left, back to the same x");
}

// Points hit positions within the text; unless letter spacing is
// negative, which can draw a cluster before the one ahead of it, their
// carets lie in the content box and hit again where they are drawn.
static void CheckHits(const muiTextHost* host, muiNodeId node, float width, size_t length,
                      bool overlapping, uint32_t* state)
{
    for (int k = 0; k < 8; k++)
    {
        float x = (float)(Next(state) % 400) - 50.0f;
        float y = (float)(Next(state) % 200) - 20.0f;
        muiTextPosition position = {0, 0};
        muiTextCaret caret = {0};
        muiTextCaret again = {0};
        CHECK(muiTextHitTest(host, node, width, x, y, &position) == mui_success &&
                  position.offset <= length &&
                  muiTextGetCaret(host, node, width, position, &caret) == mui_success,
              "a hit and its caret");
        muiTextPosition moved = {0, 0};
        muiTextMovement movement = (muiTextMovement)(Next(state) % (mui_moveTextEnd + 1));
        CHECK(muiTextMove(host, node, width, position, movement, NAN, &moved) == mui_success &&
                  moved.offset <= length,
              "a move within the text");
        CHECK(movement != mui_moveNextCluster || moved.offset > position.offset ||
                  moved.offset == length,
              "the next cluster after");
        if (overlapping)
        {
            continue;
        }
        CheckRightLeft(host, node, width, position, caret);
        CHECK(caret.x >= -0.01f && caret.x <= fmaxf(width, 0.0f) + 0.01f && caret.height > 0.0f,
              "a caret in the box");
        muiTextPosition second = {0, 0};
        CHECK(muiTextHitTest(host, node, width, caret.x, caret.y + caret.height * 0.5f, &second) ==
                      mui_success &&
                  muiTextGetCaret(host, node, width, second, &again) == mui_success &&
                  fabsf(again.x - caret.x) <= 0.01f && again.y == caret.y,
              "a caret hit where it is drawn");
    }
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
    // Liberation Sans and Ahem, each the other's fallback in turn, so
    // lines mix fonts of different units per em.
    muiFontId fonts[2] = {{0, 0}, {0, 0}};
    muiFontMetrics metrics[2] = {{0}, {0}};
    CHECK(muiCreateFont(service, &fontDef, &fonts[0]) == mui_success &&
              muiFont_GetMetrics(service, fonts[0], &metrics[0]) == mui_success,
          "Liberation Sans");
    fontDef.data = s_ahem;
    fontDef.size = sizeof s_ahem;
    CHECK(muiCreateFont(service, &fontDef, &fonts[1]) == mui_success &&
              muiFont_GetMetrics(service, fonts[1], &metrics[1]) == mui_success,
          "Ahem");
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
        int primary = round % 2;
        uint64_t other = muiFont_GetKey(fonts[1 - primary]);
        CHECK(muiSetDefaultFont(service, fonts[primary]) == mui_success &&
                  muiSetFallbackFonts(service, &other, 1) == mui_success,
              "fonts");
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
        float lineHeight = style.automaticLineHeight
                               ? (metrics[primary].ascent + metrics[primary].descent) * style.size +
                                     metrics[primary].lineGap * style.size
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
        CheckHits(&host, node, rect.width, length, style.letterSpacing < 0.0f, &state);
        CHECK(muiDestroyNode(context, node) == mui_success &&
                  muiDestroyTextBlock(service, block) == mui_success,
              "destroyed");
    }
    CHECK(muiGetTextServiceFailures(service) == 0 && drawn > 1000, "glyphs drawn, no failures");
    muiDestroyContext(context);
    muiDestroyTextService(service);
}

// Sets a random composition of up to three segments, or ends it.
static void ComposeAtRandom(muiTextService* service, muiTextBlockId block, uint32_t offset,
                            uint32_t* state)
{
    if (Next(state) % 5 == 0)
    {
        CHECK(muiTextBlock_EndComposition(service, block) == mui_success, "ended");
        return;
    }
    char text[TEXT_LIMIT];
    size_t length = RandomText(text, state) % 24;
    muiCompositionSegment segments[3];
    uint32_t count = length == 0 ? 0 : Next(state) % 4;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t start = Next(state) % ((uint32_t)length + 1u);
        segments[i] = (muiCompositionSegment){start, Next(state) % ((uint32_t)length - start + 1u),
                                              (muiCompositionStyle)(Next(state) % 4)};
    }
    CHECK(muiTextBlock_SetComposition(service, block, offset, text, length, segments, count) ==
              mui_success,
          "composed");
}

// Random deletions, insertions and compositions at random offsets: each deletion within
// the text and on the offset's side, each replacement the length it
// should be, and the text laid out and painted after each.
static void TestRandomEdits(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = s_liberationSans;
    fontDef.size = sizeof s_liberationSans;
    fontDef.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    CHECK(muiCreateFont(service, &fontDef, &font) == mui_success &&
              muiSetDefaultFont(service, font) == mui_success,
          "font");
    muiContextDef contextDef = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&contextDef, &context) == mui_success, "context");
    muiTextHost host = {service, context};
    uint32_t state = 91;
    char text[TEXT_LIMIT];
    size_t length = RandomText(text, &state);
    muiTextBlockId block = {0, 0};
    CHECK(muiCreateTextBlock(service, text, length, &block) == mui_success, "block");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    nodeDef.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &node) == mui_success, "node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, node, &layout, MUI_PROPERTY_BIT(mui_propertyContent)) ==
              mui_success,
          "content");
    for (int round = 0; round < TEXT_ROUNDS; round++)
    {
        uint32_t offset = Next(&state) % ((uint32_t)length + 2u);
        uint32_t start = 0;
        uint32_t end = 0;
        size_t inserted = 0;
        if (Next(&state) % 3 == 0 && length < 400)
        {
            inserted = RandomText(text, &state);
            start = end = offset < length ? offset : (uint32_t)length;
        }
        else
        {
            muiTextDeletion deletion = (muiTextDeletion)(Next(&state) % 2);
            CHECK(muiTextBlock_FindDeletion(service, block, offset, deletion, &start, &end) ==
                          mui_success &&
                      start <= end && end <= length &&
                      (deletion == mui_deleteForward ? start : end) ==
                          (offset < length ? offset : length),
                  "a deletion on the offset's side");
        }
        bool composing = Next(&state) % 4 == 0;
        if (composing)
        {
            ComposeAtRandom(service, block, offset, &state);
        }
        else
        {
            CHECK(muiTextBlock_Replace(service, block, start, end, text, inserted) == mui_success,
                  "replaced");
        }
        const char* now = NULL;
        size_t nowLength = 0;
        CHECK(muiTextBlock_GetText(service, block, &now, &nowLength) == mui_success &&
                  (composing || nowLength == length - (end - start) + inserted),
              "the length after");
        length = nowLength;
        uint32_t composed = 0;
        uint32_t composedLength = 0;
        CHECK(muiTextBlock_GetComposition(service, block, &composed, &composedLength) ==
                      mui_success &&
                  composed + composedLength <= length,
              "a composition within the text");
        CHECK(muiNode_MarkContentChanged(context, node) == mui_success, "marked");
        const muiLayoutInput input = {
            50.0f + (float)(Next(&state) % 200), 1000.0f, muiMeasureText, &host, 0, NULL};
        const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
        CHECK(muiComputeLayout(context, node, &input) == mui_success &&
                  muiBuildDrawList(context, node, &draw) == mui_success,
              "laid out and painted");
    }
    CHECK(muiGetTextServiceFailures(service) == 0, "no failures");
    muiDestroyContext(context);
    muiDestroyTextService(service);
}

int main(void)
{
    TestDamagedFonts();
    TestRandomText();
    TestRandomEdits();
    return s_failures == 0 ? 0 : 1;
}
