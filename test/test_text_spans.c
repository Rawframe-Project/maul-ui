// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spans and decorations (record mui-0006): a block's spans checked, moved
// by edits and read back; painting splits glyph runs where a span's ink
// changes, in visual order, and draws underlines and overlines before the
// glyphs, line-through after, from the font's metrics.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <string.h>

#include "ahem.inc"

static const muiNodeId s_nullNode = {0, 0};

#define COLOR      MUI_PROPERTY_BIT(mui_propertyTextColor)
#define DECORATION MUI_PROPERTY_BIT(mui_propertyTextDecoration)
#define LINE_COLOR MUI_PROPERTY_BIT(mui_propertyTextDecorationColor)

static const muiColor s_red = {1.0f, 0.0f, 0.0f, 1.0f};
static const muiColor s_green = {0.0f, 1.0f, 0.0f, 1.0f};
static const muiColor s_blue = {0.0f, 0.0f, 1.0f, 1.0f};

typedef struct Scene
{
    muiTextService* service;
    muiFontId font;
    muiContext* context;
    muiTextHost host;
    muiFontMetrics metrics;
} Scene;

static Scene MakeScene(void)
{
    Scene scene = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    CHECK(muiCreateTextService(&def, &scene.service) == mui_success, "service");
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    CHECK(muiCreateFont(scene.service, &font, &scene.font) == mui_success &&
              muiSetDefaultFont(scene.service, scene.font) == mui_success &&
              muiFont_GetMetrics(scene.service, scene.font, &scene.metrics) == mui_success,
          "the default font");
    muiContextDef context = muiDefaultContextDef();
    CHECK(muiCreateContext(&context, &scene.context) == mui_success, "context");
    scene.host = (muiTextHost){scene.service, scene.context};
    return scene;
}

static void FreeScene(Scene* scene)
{
    muiDestroyContext(scene->context);
    muiDestroyTextService(scene->service);
}

// A root showing a block's text at size 10.
static muiNodeId AddText(Scene* scene, const char* text, muiTextBlockId* blockOut)
{
    CHECK(muiCreateTextBlock(scene->service, text, strlen(text), blockOut) == mui_success, "block");
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(*blockOut);
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(scene->context, &def, &node) == mui_success, "node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(scene->context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success &&
              muiNode_SetTextValues(scene->context, node, &style,
                                    MUI_PROPERTY_BIT(mui_propertyFontSize)) == mui_success,
          "host content");
    return node;
}

static muiDrawList Paint(Scene* scene, muiNodeId root)
{
    CHECK(muiNode_MarkContentChanged(scene->context, root) == mui_success, "marked");
    const muiLayoutInput layout = {1000.0f, 1000.0f, muiMeasureText, &scene->host,
                                   0,       NULL,    {0, 0, 0, 0}};
    const muiDrawInput input = {1, 1.0f, muiPaintText, &scene->host};
    muiDrawList list = {0};
    CHECK(muiComputeLayout(scene->context, root, &layout) == mui_success &&
              muiBuildDrawList(scene->context, root, &input) == mui_success &&
              muiGetDrawList(scene->context, &list) == mui_success,
          "painted");
    return list;
}

static muiTextSpan Span(uint32_t start, uint32_t length, muiPropertyMask mask)
{
    return (muiTextSpan){start, length, mask, muiDefaultTextStyle()};
}

static bool SameLinear(muiLinearColor a, muiColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Whether a command is a glyph run of one glyph at x in a color.
static bool IsGlyph(const muiDrawList* list, uint32_t index, float x, muiColor color)
{
    if (index >= list->commandCount || list->commands[index].kind != mui_drawGlyphRun)
    {
        return false;
    }
    const muiDrawGlyphRun* run = &list->commands[index].glyphRun;
    return run->glyphCount >= 1 && list->glyphs[run->firstGlyph].x == x &&
           SameLinear(run->color, color);
}

// Whether a command is a decoration from x0 to x1, its top and thickness
// those given in ems at size 10 on a baseline at 8, snapped to pixels.
static bool IsLine(const muiDrawList* list, uint32_t index, float x0, float x1, float top,
                   float thickness, muiColor color)
{
    if (index >= list->commandCount || list->commands[index].kind != mui_drawBox)
    {
        return false;
    }
    const muiDrawBox* box = &list->commands[index].box;
    return box->rect.x == x0 && box->rect.width == x1 - x0 && fabsf(box->rect.y - top) <= 0.5f &&
           box->rect.height >= 1.0f && fabsf(box->rect.height - thickness) <= 1.0f &&
           SameLinear(box->fill, color);
}

static void TestChecks(void)
{
    Scene scene = MakeScene();
    muiTextBlockId block = {0, 0};
    (void)AddText(&scene, "a\xC3\xA9z", &block);
    muiTextSpan span = Span(0, 1, COLOR);
    CHECK(muiTextBlock_SetSpans(scene.service, block, &span, 1) == mui_success, "a span");
    const muiTextSpan bad[] = {
        Span(0, 0, COLOR),
        Span(3, 2, COLOR),
        Span(2, 1, COLOR),
        Span(1, 1, COLOR),
        Span(0, 1, MUI_PROPERTY_BIT(mui_propertyFont)),
        Span(0, 1, MUI_PROPERTY_BIT(mui_propertyTextDecorationColor + 1)),
    };
    const char* what[] = {"empty",     "past the text",      "from inside a sequence",
                          "to inside", "a shaping property", "past the group"};
    for (int i = 0; i < 6; i++)
    {
        CHECK(muiTextBlock_SetSpans(scene.service, block, &bad[i], 1) == mui_errorInvalid, what[i]);
    }
    muiTextSpan red = Span(1, 2, COLOR);
    red.style.color.r = 1.5f;
    muiTextSpan lines = Span(0, 1, DECORATION);
    lines.style.decoration = 8;
    CHECK(muiTextBlock_SetSpans(scene.service, block, &red, 1) == mui_errorInvalid &&
              muiTextBlock_SetSpans(scene.service, block, &lines, 1) == mui_errorInvalid &&
              muiTextBlock_SetSpans(scene.service, block, NULL, 1) == mui_errorInvalid &&
              muiTextBlock_SetSpans(scene.service, block, &span, MUI_MAX_TEXT_SPANS + 1) ==
                  mui_errorInvalid &&
              muiTextBlock_SetSpans(NULL, block, &span, 1) == mui_errorInvalid &&
              muiTextBlock_SetSpans(scene.service, (muiTextBlockId){0, 0}, &span, 1) ==
                  mui_errorInvalid,
          "values and arguments");
    const muiTextSpan* read = NULL;
    uint32_t count = 0;
    CHECK(muiTextBlock_GetSpans(scene.service, block, &read, &count) == mui_success && count == 1 &&
              read[0].start == 0 && read[0].length == 1,
          "the refused calls kept the span");
    CHECK(muiTextBlock_GetSpans(scene.service, block, NULL, &count) == mui_errorInvalid &&
              muiTextBlock_GetSpans(scene.service, block, &read, NULL) == mui_errorInvalid,
          "reading's arguments");
    CHECK(muiTextBlock_SetSpans(scene.service, block, NULL, 0) == mui_success &&
              muiTextBlock_GetSpans(scene.service, block, &read, &count) == mui_success &&
              count == 0 && read == NULL,
          "cleared");
    FreeScene(&scene);
}

// Where three spans go as "abcdef" is edited: one on "b", one on "cd",
// one on "ef".
static void TestEdits(void)
{
    Scene scene = MakeScene();
    muiTextBlockId block = {0, 0};
    (void)AddText(&scene, "abcdef", &block);
    const muiTextSpan spans[3] = {Span(1, 1, COLOR), Span(2, 2, COLOR), Span(4, 2, COLOR)};
    const muiTextSpan* read = NULL;
    uint32_t count = 0;
    CHECK(muiTextBlock_SetSpans(scene.service, block, spans, 3) == mui_success, "set");
    // "c" to "XY": cd's start stays at the range's start and it grows.
    CHECK(muiTextBlock_Replace(scene.service, block, 2, 3, "XY", 2) == mui_success &&
              muiTextBlock_GetSpans(scene.service, block, &read, &count) == mui_success &&
              count == 3 && read[0].start == 1 && read[0].length == 1 && read[1].start == 2 &&
              read[1].length == 3 && read[2].start == 5 && read[2].length == 2,
          "a replaced start: the span keeps it; those after move");
    // Typing at the end of "b" is outside it; inside "XYd" is in it.
    CHECK(muiTextBlock_Replace(scene.service, block, 2, 2, "-", 1) == mui_success &&
              muiTextBlock_Replace(scene.service, block, 4, 4, "+", 1) == mui_success &&
              muiTextBlock_GetSpans(scene.service, block, &read, &count) == mui_success &&
              count == 3 && read[0].length == 1 && read[1].start == 3 && read[1].length == 4,
          "insertions: at an end outside, strictly inside within");
    // Deleting across the end of "X+Yd" and the start of "ef": both trim.
    CHECK(muiTextBlock_Replace(scene.service, block, 5, 8, "", 0) == mui_success &&
              muiTextBlock_GetSpans(scene.service, block, &read, &count) == mui_success &&
              count == 3 && read[1].start == 3 && read[1].length == 2 && read[2].start == 5 &&
              read[2].length == 1,
          "a range across two spans trims both");
    // Deleting all of "b" drops its span.
    CHECK(muiTextBlock_Replace(scene.service, block, 1, 2, "", 0) == mui_success &&
              muiTextBlock_GetSpans(scene.service, block, &read, &count) == mui_success &&
              count == 2 && read[0].start == 2,
          "an emptied span is dropped");
    // A composition moves them as a replacement does; new text drops them.
    CHECK(muiTextBlock_SetComposition(scene.service, block, 0, "ka", 2, NULL, 0) == mui_success &&
              muiTextBlock_GetSpans(scene.service, block, &read, &count) == mui_success &&
              count == 2 && read[0].start == 4,
          "a composition moves them");
    CHECK(muiTextBlock_SetText(scene.service, block, "new", 3) == mui_success &&
              muiTextBlock_GetSpans(scene.service, block, &read, &count) == mui_success &&
              count == 0,
          "new text drops them");
    FreeScene(&scene);
}

static void TestPainting(void)
{
    Scene scene = MakeScene();
    const muiFontMetrics* m = &scene.metrics;
    float under = 8.0f + m->underlineOffset * 10.0f;
    float thin = m->underlineThickness * 10.0f;
    float strike = 8.0f - m->strikeoutOffset * 10.0f;
    float strikeThin = m->strikeoutThickness * 10.0f;
    CHECK(thin > 0.0f && strikeThin > 0.0f, "Ahem has an underline and a strikeout");
    muiTextBlockId block = {0, 0};
    muiNodeId node = AddText(&scene, "abcd", &block);
    // b red; c and d blue, underlined and struck in blue; d green over
    // that.
    muiTextSpan spans[3] = {Span(1, 1, COLOR), Span(2, 2, COLOR | DECORATION | LINE_COLOR),
                            Span(3, 1, COLOR)};
    spans[0].style.color = s_red;
    spans[1].style.decoration = mui_decorationUnderline | mui_decorationLineThrough;
    spans[1].style.decorationColor = s_blue;
    spans[1].style.color = s_blue;
    spans[2].style.color = s_green;
    CHECK(muiTextBlock_SetSpans(scene.service, block, spans, 3) == mui_success, "spans");
    const muiColor black = {0.0f, 0.0f, 0.0f, 1.0f};
    muiDrawList list = Paint(&scene, node);
    CHECK(list.commandCount == 8, "a, b, then c and d each underlined, drawn and struck");
    CHECK(IsGlyph(&list, 0, 0.0f, black) && IsGlyph(&list, 1, 10.0f, s_red), "a, then b in red");
    CHECK(IsLine(&list, 2, 20.0f, 30.0f, under, thin, s_blue) && IsGlyph(&list, 3, 20.0f, s_blue) &&
              IsLine(&list, 4, 20.0f, 30.0f, strike, strikeThin, s_blue),
          "c: the underline before it, the line-through after");
    CHECK(IsLine(&list, 5, 30.0f, 40.0f, under, thin, s_blue) &&
              IsGlyph(&list, 6, 30.0f, s_green) &&
              IsLine(&list, 7, 30.0f, 40.0f, strike, strikeThin, s_blue),
          "d in green, a later span winning");

    // The node's own decoration, overline, in the text's color.
    CHECK(muiTextBlock_SetSpans(scene.service, block, NULL, 0) == mui_success, "no spans");
    muiTextStyle style = muiDefaultTextStyle();
    style.decoration = mui_decorationOverline;
    CHECK(muiNode_SetTextValues(scene.context, node, &style, DECORATION) == mui_success,
          "overlined");
    list = Paint(&scene, node);
    CHECK(list.commandCount == 2 &&
              IsLine(&list, 0, 0.0f, 40.0f, 8.0f - m->ascent * 10.0f, thin, black) &&
              IsGlyph(&list, 1, 0.0f, black),
          "one run overlined at the ascent, in the text's color");

    // Right to left (an override), stretches go in visual order: d c, b
    // in red, a.
    muiTextBlockId mirrored = {0, 0};
    muiNodeId other = AddText(&scene,
                              "\xE2\x80\xAE"
                              "abcd",
                              &mirrored);
    muiTextSpan b = Span(4, 1, COLOR);
    b.style.color = s_red;
    CHECK(muiTextBlock_SetSpans(scene.service, mirrored, &b, 1) == mui_success, "b");
    list = Paint(&scene, other);
    CHECK(list.commandCount == 3 && IsGlyph(&list, 0, 0.0f, black) &&
              list.commands[0].glyphRun.glyphCount == 2 && IsGlyph(&list, 1, 20.0f, s_red) &&
              IsGlyph(&list, 2, 30.0f, black),
          "right to left: d c, b in red, a");
    FreeScene(&scene);
}

int main(void)
{
    TestChecks();
    TestEdits();
    TestPainting();
    return s_failures == 0 ? 0 : 1;
}
