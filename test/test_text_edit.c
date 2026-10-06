// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Editing primitives (record mui-0006): hit testing, carets and range
// rectangles over laid-out text, in left-to-right, right-to-left and
// mixed lines, across wrapped lines, inside a ligature, with alignment
// and letter spacing, and calls outside the contract refused. Ahem draws
// every character as a box an em wide; at size 10 each is 10 wide.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"
#include "break_test.inc"

#define DOWN mui_affinityDownstream
#define UP   mui_affinityUpstream

// An allocator that fails its allocation number failAt (from 1; 0
// never), counting from when failAt was set.
typedef struct FailingAllocator
{
    int allocations;
    int failAt;
} FailingAllocator;

static void* FailingAlloc(size_t size, size_t alignment, void* context)
{
    FailingAllocator* failing = context;
    failing->allocations++;
    if (failing->failAt != 0 && failing->allocations == failing->failAt)
    {
        return NULL;
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : NULL;
}

static void FailingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

typedef struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    muiFontId ahem;
    muiFontId ligatures;
    muiTextBlockId block;
    muiNodeId node;
} Scene;

static Scene MakeFailingScene(FailingAllocator* failing)
{
    Scene scene = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    if (failing != NULL)
    {
        def.allocator = (muiAllocator){FailingAlloc, FailingFree, failing};
    }
    CHECK(muiCreateTextService(&def, &scene.service) == mui_success, "service");
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    font.dataMode = mui_fontDataBorrow;
    CHECK(muiCreateFont(scene.service, &font, &scene.ahem) == mui_success &&
              muiSetDefaultFont(scene.service, scene.ahem) == mui_success,
          "Ahem");
    font.data = s_breakTest;
    font.size = sizeof s_breakTest;
    CHECK(muiCreateFont(scene.service, &font, &scene.ligatures) == mui_success, "ligatures");
    muiContextDef context = muiDefaultContextDef();
    CHECK(muiCreateContext(&context, &scene.context) == mui_success, "context");
    scene.host = (muiTextHost){scene.service, scene.context};
    CHECK(muiCreateTextBlock(scene.service, "", 0, &scene.block) == mui_success, "block");
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

static Scene MakeScene(void)
{
    return MakeFailingScene(NULL);
}

static void FreeScene(Scene* scene)
{
    muiDestroyContext(scene->context);
    muiDestroyTextService(scene->service);
}

// Sets the node's text and style (size 10 unless style says otherwise)
// and lays it out.
static void Show(Scene* scene, const char* text, muiTextStyle style, muiPropertyMask mask)
{
    CHECK(muiTextBlock_SetText(scene->service, scene->block, text, strlen(text)) == mui_success &&
              muiNode_MarkContentChanged(scene->context, scene->node) == mui_success,
          "text");
    style.size = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiNode_SetTextValues(scene->context, scene->node, &style,
                                mask | MUI_PROPERTY_BIT(mui_propertyFontSize)) == mui_success,
          "style");
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &scene->host, 0, NULL};
    CHECK(muiComputeLayout(scene->context, scene->node, &input) == mui_success, "layout");
}

static void ShowPlain(Scene* scene, const char* text)
{
    Show(scene, text, muiDefaultTextStyle(), 0);
}

// Whether a point at a content width hits a position.
static bool Hits(Scene* scene, float width, float x, float y, uint32_t offset,
                 muiTextAffinity affinity)
{
    muiTextPosition position = {99, 9};
    return muiTextHitTest(&scene->host, scene->node, width, x, y, &position) == mui_success &&
           position.offset == offset && position.affinity == affinity;
}

static muiTextCaret Caret(Scene* scene, float width, uint32_t offset, muiTextAffinity affinity)
{
    muiTextCaret caret = {-1.0f, -1.0f, -1.0f, false};
    CHECK(muiTextGetCaret(&scene->host, scene->node, width, (muiTextPosition){offset, affinity},
                          &caret) == mui_success,
          "caret");
    return caret;
}

static bool CaretAt(Scene* scene, float width, uint32_t offset, muiTextAffinity affinity, float x,
                    float y)
{
    muiTextCaret caret = Caret(scene, width, offset, affinity);
    return caret.x == x && caret.y == y && caret.height == 10.0f;
}

static void TestLeftToRight(void)
{
    Scene scene = MakeScene();
    ShowPlain(&scene, "abc");
    CHECK(Hits(&scene, 100.0f, 4.0f, 5.0f, 0, DOWN) && Hits(&scene, 100.0f, 6.0f, 5.0f, 1, UP) &&
              Hits(&scene, 100.0f, 14.0f, 5.0f, 1, DOWN) &&
              Hits(&scene, 100.0f, 15.0f, 5.0f, 2, UP),
          "the nearer edge of the cluster under the point");
    CHECK(Hits(&scene, 100.0f, 10.0f, 5.0f, 1, DOWN), "on a boundary: the cluster after it");
    CHECK(Hits(&scene, 100.0f, -5.0f, -50.0f, 0, DOWN) && Hits(&scene, 100.0f, 95.0f, 50.0f, 3, UP),
          "past the ends: the ends of the nearest line");
    CHECK(CaretAt(&scene, 100.0f, 0, DOWN, 0.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 1, DOWN, 10.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 1, UP, 10.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 3, UP, 30.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 3, DOWN, 30.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 9, DOWN, 30.0f, 0.0f),
          "carets between, at the ends and past the text");
    muiRect rects[4];
    uint32_t count = 0;
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 100.0f, 1, 3, rects, 4, &count) ==
                  mui_success &&
              count == 1 && rects[0].x == 10.0f && rects[0].width == 20.0f && rects[0].y == 0.0f &&
              rects[0].height == 10.0f,
          "a range as one rectangle");
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 100.0f, 2, 2, rects, 4, &count) ==
                  mui_success &&
              count == 0,
          "an empty range");
    // Centered in 100: the line starts at 35; spacing widens each box.
    muiTextStyle style = muiDefaultTextStyle();
    style.align = mui_textAlignCenter;
    Show(&scene, "abc", style, MUI_PROPERTY_BIT(mui_propertyTextAlign));
    CHECK(CaretAt(&scene, 100.0f, 1, DOWN, 45.0f, 0.0f) &&
              Hits(&scene, 100.0f, 36.0f, 5.0f, 0, DOWN),
          "aligned lines");
    style = muiDefaultTextStyle();
    style.letterSpacing = (muiDimension){0.0f, 2.0f, mui_dimensionValue};
    Show(&scene, "abc", style,
         MUI_PROPERTY_BIT(mui_propertyTextAlign) | MUI_PROPERTY_BIT(mui_propertyLetterSpacing));
    CHECK(CaretAt(&scene, 100.0f, 1, DOWN, 12.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 3, UP, 36.0f, 0.0f) &&
              Hits(&scene, 100.0f, 7.0f, 5.0f, 1, UP),
          "spacing after each cluster");
    FreeScene(&scene);
}

static void TestWrapped(void)
{
    // At 25 wide: "ab " and "cd", the space hanging.
    Scene scene = MakeScene();
    ShowPlain(&scene, "ab cd");
    CHECK(CaretAt(&scene, 25.0f, 3, UP, 20.0f, 0.0f) &&
              CaretAt(&scene, 25.0f, 3, DOWN, 0.0f, 10.0f) &&
              CaretAt(&scene, 25.0f, 2, UP, 20.0f, 0.0f) &&
              CaretAt(&scene, 25.0f, 5, UP, 20.0f, 10.0f),
          "the break's offset on either line, by affinity");
    CHECK(Hits(&scene, 25.0f, 4.0f, 50.0f, 3, DOWN) && Hits(&scene, 25.0f, 50.0f, 5.0f, 2, UP) &&
              Hits(&scene, 25.0f, 4.0f, 15.0f, 3, DOWN) &&
              Hits(&scene, 25.0f, 14.0f, 15.0f, 4, DOWN),
          "points on each line");
    muiRect rects[4];
    uint32_t count = 0;
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 25.0f, 1, 4, rects, 4, &count) ==
                  mui_success &&
              count == 2 && rects[0].x == 10.0f && rects[0].width == 10.0f && rects[1].x == 0.0f &&
              rects[1].y == 10.0f && rects[1].width == 10.0f,
          "a range over two lines");
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 25.0f, 1, 4, rects, 1, &count) ==
                  mui_errorCapacity &&
              count == 2 && rects[0].x == 10.0f,
          "too few rectangles: those that fit, and the count");
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 25.0f, 0, 5, rects, 1, &count) ==
                  mui_errorCapacity &&
              count == 2 && rects[0].x == 0.0f && rects[0].width == 20.0f,
          "a stretch of several clusters past the room counted once");
    // Right to left, the line starts at the right, at 25: "ab" from 5,
    // and an offset in the space hanging past its end at its left.
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(scene.context, scene.node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "direction");
    ShowPlain(&scene, "ab  cd");
    CHECK(CaretAt(&scene, 25.0f, 3, DOWN, 5.0f, 0.0f) &&
              CaretAt(&scene, 25.0f, 0, DOWN, 5.0f, 0.0f),
          "hanging white space at the line's end, on the left");
    // An empty line between two: a point on it is its start.
    ShowPlain(&scene, "a\n\nb");
    CHECK(Hits(&scene, 25.0f, 20.0f, 15.0f, 2, DOWN), "an empty line");
    FreeScene(&scene);
}

static void TestInsideClusters(void)
{
    // Ahem has no combining acute: x and its missing glyph, given no
    // width as a mark, are one cluster from 10 to 20; b from 20 to 30.
    Scene scene = MakeScene();
    ShowPlain(&scene, "ax\xCC\x81"
                      "b");
    CHECK(CaretAt(&scene, 100.0f, 2, DOWN, 10.0f, 0.0f), "an offset inside a cluster: its start");
    muiRect rects[2];
    uint32_t count = 0;
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 100.0f, 2, 5, rects, 2, &count) ==
                  mui_success &&
              count == 1 && rects[0].x == 20.0f && rects[0].width == 10.0f,
          "a cluster the range starts inside: not covered");
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 100.0f, 0, 2, rects, 2, &count) ==
                  mui_success &&
              count == 1 && rects[0].x == 0.0f && rects[0].width == 10.0f,
          "a cluster the range ends inside: not covered");
    FreeScene(&scene);
}

static void TestRightToLeft(void)
{
    // Two Hebrew letters, two bytes each, in a left-to-right paragraph:
    // the second at 0 to 10, the first at 10 to 20.
    Scene scene = MakeScene();
    ShowPlain(&scene, "\xD7\x90\xD7\x91");
    CHECK(Hits(&scene, 100.0f, 2.0f, 5.0f, 4, UP) && Hits(&scene, 100.0f, 18.0f, 5.0f, 0, DOWN) &&
              Hits(&scene, 100.0f, 12.0f, 5.0f, 2, UP) && Hits(&scene, 100.0f, 8.0f, 5.0f, 2, DOWN),
          "edges of right-to-left clusters");
    muiTextCaret caret = Caret(&scene, 100.0f, 0, DOWN);
    CHECK(caret.x == 20.0f && caret.rightToLeft, "the start at the right");
    CHECK(CaretAt(&scene, 100.0f, 2, DOWN, 10.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 2, UP, 10.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 4, UP, 0.0f, 0.0f),
          "carets between and at the end");
    // a, then the two letters: a at 0 to 10, the second at 10 to 20, the
    // first at 20 to 30. Offset 1 has two places.
    ShowPlain(&scene, "a\xD7\x90\xD7\x91");
    CHECK(CaretAt(&scene, 100.0f, 1, UP, 10.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 1, DOWN, 30.0f, 0.0f),
          "a change of direction: after a, or before the first letter");
    muiRect rects[4];
    uint32_t count = 0;
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 100.0f, 0, 3, rects, 4, &count) ==
                  mui_success &&
              count == 2 && rects[0].x == 0.0f && rects[0].width == 10.0f && rects[1].x == 20.0f &&
              rects[1].width == 10.0f,
          "a range across the change: two rectangles");
    // Hebrew then Arabic: two items, one right-to-left run, the Hebrew
    // on the right.
    ShowPlain(&scene, "\xD7\x90\xD8\xB3");
    CHECK(CaretAt(&scene, 100.0f, 0, DOWN, 20.0f, 0.0f) &&
              CaretAt(&scene, 100.0f, 4, UP, 0.0f, 0.0f),
          "items of a run right to left");
    // A right-to-left paragraph: its empty caret at the right.
    ShowPlain(&scene, "");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(scene.context, scene.node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "direction");
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &scene.host, 0, NULL};
    CHECK(muiComputeLayout(scene.context, scene.node, &input) == mui_success, "layout");
    caret = Caret(&scene, 100.0f, 0, DOWN);
    CHECK(caret.x == 100.0f && caret.rightToLeft && Hits(&scene, 100.0f, 50.0f, 5.0f, 0, DOWN),
          "an empty right-to-left paragraph");
    FreeScene(&scene);
}

static void TestLigature(void)
{
    // MaulBreakTest's space and x are one glyph: two clusters share it
    // equally.
    Scene scene = MakeScene();
    muiTextStyle style = muiDefaultTextStyle();
    style.font = muiFont_GetKey(scene.ligatures);
    Show(&scene, "A x", style, MUI_PROPERTY_BIT(mui_propertyFont));
    float a = Caret(&scene, 1000.0f, 1, UP).x;
    float end = Caret(&scene, 1000.0f, 3, UP).x;
    float middle = Caret(&scene, 1000.0f, 2, DOWN).x;
    CHECK(a > 0.0f && end > a && middle == a + (end - a) * 0.5f, "a caret inside the ligature");
    CHECK(Hits(&scene, 1000.0f, middle - 0.1f, 5.0f, 2, UP) &&
              Hits(&scene, 1000.0f, middle + 0.1f, 5.0f, 2, DOWN),
          "either part of it");
    // Alef and bet are one glyph 10 wide: bet, after alef, on the left.
    Show(&scene, "\xD7\x90\xD7\x91", style, MUI_PROPERTY_BIT(mui_propertyFont));
    CHECK(Caret(&scene, 1000.0f, 2, DOWN).x == 5.0f && Hits(&scene, 1000.0f, 1.0f, 5.0f, 4, UP) &&
              Hits(&scene, 1000.0f, 4.0f, 5.0f, 2, DOWN) &&
              Hits(&scene, 1000.0f, 9.0f, 5.0f, 0, DOWN),
          "a right-to-left ligature cut right to left");
    FreeScene(&scene);
}

static void TestContract(void)
{
    Scene scene = MakeScene();
    ShowPlain(&scene, "abc");
    muiTextPosition position = {7, 7};
    muiTextCaret caret = {7.0f, 7.0f, 7.0f, false};
    muiRect rects[2];
    uint32_t count = 0;
    muiTextHost noService = {NULL, scene.context};
    CHECK(muiTextHitTest(NULL, scene.node, 100.0f, 0.0f, 0.0f, &position) == mui_errorInvalid &&
              muiTextHitTest(&noService, scene.node, 100.0f, 0.0f, 0.0f, &position) ==
                  mui_errorInvalid &&
              muiTextHitTest(&scene.host, scene.node, 100.0f, 0.0f, 0.0f, NULL) ==
                  mui_errorInvalid &&
              muiTextHitTest(&scene.host, scene.node, 100.0f, NAN, 0.0f, &position) ==
                  mui_errorInvalid &&
              muiTextHitTest(&scene.host, scene.node, 100.0f, 0.0f, INFINITY, &position) ==
                  mui_errorInvalid &&
              position.offset == 7,
          "hit testing outside the contract");
    CHECK(muiTextGetCaret(NULL, scene.node, 100.0f, position, &caret) == mui_errorInvalid &&
              muiTextGetCaret(&scene.host, scene.node, 100.0f, position, NULL) ==
                  mui_errorInvalid &&
              muiTextGetRangeRects(&scene.host, scene.node, 100.0f, 0, 1, NULL, 1, &count) ==
                  mui_errorInvalid &&
              muiTextGetRangeRects(&scene.host, scene.node, 100.0f, 0, 1, rects, 2, NULL) ==
                  mui_errorInvalid &&
              caret.x == 7.0f,
          "carets and ranges outside the contract");
    muiNodeId stale = scene.node;
    CHECK(muiDestroyNode(scene.context, scene.node) == mui_success &&
              muiTextHitTest(&scene.host, stale, 100.0f, 0.0f, 0.0f, &position) == mui_errorStale &&
              muiTextGetCaret(&scene.host, stale, 100.0f, position, &caret) == mui_errorStale &&
              muiTextGetRangeRects(&scene.host, stale, 100.0f, 0, 1, rects, 2, &count) ==
                  mui_errorStale,
          "a node gone");
    FreeScene(&scene);
}

static void TestMemory(void)
{
    // Each allocation fails in turn, the block's text made longer so its
    // shaping needs more: every call reports memory, never a stale node.
    for (int failAt = 1; failAt < 40; failAt++)
    {
        FailingAllocator failing = {0, 0};
        Scene scene = MakeFailingScene(&failing);
        ShowPlain(&scene, "ab cd\xE2\x80\xAE"
                          "ef");
        const char* longer = "ab cd\xE2\x80\xAE"
                             "ef gh ij kl mn op qr";
        CHECK(muiTextBlock_SetText(scene.service, scene.block, longer, strlen(longer)) ==
                  mui_success,
              "longer text, shaped anew");
        failing = (FailingAllocator){0, failAt};
        muiTextPosition position = {0, 0};
        muiTextCaret caret = {0};
        muiRect rects[4];
        uint32_t count = 0;
        muiResult hit = muiTextHitTest(&scene.host, scene.node, 25.0f, 5.0f, 15.0f, &position);
        muiResult caretResult = muiTextGetCaret(&scene.host, scene.node, 25.0f, position, &caret);
        muiResult range =
            muiTextGetRangeRects(&scene.host, scene.node, 25.0f, 0, 9, rects, 4, &count);
        bool failed = failing.allocations >= failAt;
        CHECK(failAt < 39 || !failed, "every allocation tried");
        CHECK((hit == mui_success || hit == mui_errorCapacity) &&
                  (caretResult == mui_success || caretResult == mui_errorCapacity) &&
                  (range == mui_success || range == mui_errorCapacity) &&
                  failed ==
                      (hit != mui_success || caretResult != mui_success || range != mui_success),
              "memory, and only when an allocation failed");
        FreeScene(&scene);
    }
}

int main(void)
{
    TestLeftToRight();
    TestWrapped();
    TestRightToLeft();
    TestInsideClusters();
    TestLigature();
    TestMemory();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
