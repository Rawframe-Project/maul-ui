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
#include "maul-ui/draw.h"
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
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &scene->host,
                                  0,       NULL,    {0, 0, 0, 0}};
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
    // Alef, bet, gimel: gimel at 0, bet at 10, alef at 20; bet alone is
    // the middle, though alef after it on screen ends inside the range.
    ShowPlain(&scene, "\xD7\x90\xD7\x91\xD7\x92");
    CHECK(muiTextGetRangeRects(&scene.host, scene.node, 100.0f, 2, 4, rects, 4, &count) ==
                  mui_success &&
              count == 1 && rects[0].x == 10.0f && rects[0].width == 10.0f,
          "a right-to-left letter alone");
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
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &scene.host,
                                  0,       NULL,    {0, 0, 0, 0}};
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

// Whether moving from a position at a content width lands on another.
static bool Moves(Scene* scene, float width, uint32_t offset, muiTextAffinity affinity,
                  muiTextMovement movement, uint32_t toOffset, muiTextAffinity toAffinity)
{
    muiTextPosition to = {99, 9};
    return muiTextMove(&scene->host, scene->node, width, (muiTextPosition){offset, affinity},
                       movement, NAN, &to) == mui_success &&
           to.offset == toOffset && to.affinity == toAffinity;
}

static void TestMovingInText(void)
{
    // x and its acute are one cluster, bytes 1 to 4.
    Scene scene = MakeScene();
    ShowPlain(&scene, "ax\xCC\x81"
                      "b");
    CHECK(Moves(&scene, 100.0f, 0, DOWN, mui_moveNextCluster, 1, DOWN) &&
              Moves(&scene, 100.0f, 1, UP, mui_moveNextCluster, 4, DOWN) &&
              Moves(&scene, 100.0f, 2, DOWN, mui_moveNextCluster, 4, DOWN) &&
              Moves(&scene, 100.0f, 5, DOWN, mui_moveNextCluster, 5, DOWN) &&
              Moves(&scene, 100.0f, 99, DOWN, mui_moveNextCluster, 5, DOWN),
          "to the next cluster");
    CHECK(Moves(&scene, 100.0f, 4, DOWN, mui_movePreviousCluster, 1, DOWN) &&
              Moves(&scene, 100.0f, 2, DOWN, mui_movePreviousCluster, 1, DOWN) &&
              Moves(&scene, 100.0f, 1, DOWN, mui_movePreviousCluster, 0, DOWN) &&
              Moves(&scene, 100.0f, 0, DOWN, mui_movePreviousCluster, 0, DOWN) &&
              Moves(&scene, 100.0f, 99, DOWN, mui_movePreviousCluster, 4, DOWN),
          "to the one before");
    CHECK(Moves(&scene, 100.0f, 2, UP, mui_moveTextStart, 0, DOWN) &&
              Moves(&scene, 100.0f, 2, UP, mui_moveTextEnd, 5, DOWN),
          "to the text's ends");
    // Words: "hello," 0 to 6, "world" 7 to 12, "foo" 14 to 17.
    ShowPlain(&scene, "hello, world  foo");
    CHECK(Moves(&scene, 1000.0f, 0, DOWN, mui_moveNextWordStart, 7, DOWN) &&
              Moves(&scene, 1000.0f, 7, DOWN, mui_moveNextWordStart, 14, DOWN) &&
              Moves(&scene, 1000.0f, 14, DOWN, mui_moveNextWordStart, 17, DOWN),
          "to the next word's start");
    CHECK(Moves(&scene, 1000.0f, 0, DOWN, mui_moveNextWordEnd, 5, DOWN) &&
              Moves(&scene, 1000.0f, 3, DOWN, mui_moveNextWordEnd, 5, DOWN) &&
              Moves(&scene, 1000.0f, 5, DOWN, mui_moveNextWordEnd, 12, DOWN) &&
              Moves(&scene, 1000.0f, 17, DOWN, mui_moveNextWordEnd, 17, DOWN),
          "to a word's end");
    CHECK(Moves(&scene, 1000.0f, 17, DOWN, mui_movePreviousWordStart, 14, DOWN) &&
              Moves(&scene, 1000.0f, 14, DOWN, mui_movePreviousWordStart, 7, DOWN) &&
              Moves(&scene, 1000.0f, 9, DOWN, mui_movePreviousWordStart, 7, DOWN) &&
              Moves(&scene, 1000.0f, 7, DOWN, mui_movePreviousWordStart, 0, DOWN) &&
              Moves(&scene, 1000.0f, 0, DOWN, mui_movePreviousWordStart, 0, DOWN),
          "to a word's start");
    // Words of other letters and of numbers: "a," then a Hebrew word at
    // 3 to 11, then "42" at 12 to 14.
    ShowPlain(&scene, "a, \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D 42");
    CHECK(Moves(&scene, 1000.0f, 0, DOWN, mui_moveNextWordStart, 3, DOWN) &&
              Moves(&scene, 1000.0f, 3, DOWN, mui_moveNextWordStart, 12, DOWN) &&
              Moves(&scene, 1000.0f, 11, DOWN, mui_moveNextWordEnd, 14, DOWN),
          "words of any letters, and numbers");
    FreeScene(&scene);
}

static void TestMovingOnScreen(void)
{
    Scene scene = MakeScene();
    ShowPlain(&scene, "abc");
    CHECK(Moves(&scene, 100.0f, 0, DOWN, mui_moveRight, 1, UP) &&
              Moves(&scene, 100.0f, 1, UP, mui_moveRight, 2, UP) &&
              Moves(&scene, 100.0f, 2, UP, mui_moveLeft, 1, DOWN) &&
              Moves(&scene, 100.0f, 0, DOWN, mui_moveLeft, 0, DOWN) &&
              Moves(&scene, 100.0f, 3, UP, mui_moveRight, 3, UP),
          "left and right");
    // a at 0 to 10, bet at 10 to 20, alef at 20 to 30: right goes a, bet,
    // alef on screen, and stops at the right end, offset 1.
    ShowPlain(&scene, "a\xD7\x90\xD7\x91");
    CHECK(Moves(&scene, 100.0f, 0, DOWN, mui_moveRight, 1, UP) &&
              Moves(&scene, 100.0f, 1, UP, mui_moveRight, 3, DOWN) &&
              Moves(&scene, 100.0f, 3, DOWN, mui_moveRight, 1, DOWN) &&
              Moves(&scene, 100.0f, 1, DOWN, mui_moveRight, 1, DOWN) &&
              Moves(&scene, 100.0f, 1, DOWN, mui_moveLeft, 3, UP) &&
              CaretAt(&scene, 100.0f, 5, DOWN, 10.0f, 0.0f),
          "across a change of direction");
    // "ab " and "cd": across the wrap.
    ShowPlain(&scene, "ab cd");
    CHECK(Moves(&scene, 25.0f, 2, UP, mui_moveRight, 3, DOWN) &&
              Moves(&scene, 25.0f, 3, DOWN, mui_moveLeft, 2, UP),
          "across a wrapped line's end");
    CHECK(Moves(&scene, 25.0f, 4, DOWN, mui_moveLineStart, 3, DOWN) &&
              Moves(&scene, 25.0f, 4, DOWN, mui_moveLineEnd, 5, UP) &&
              Moves(&scene, 25.0f, 1, DOWN, mui_moveLineEnd, 2, UP) &&
              Moves(&scene, 25.0f, 2, UP, mui_moveLineStart, 0, DOWN),
          "a line's ends");
    CHECK(Moves(&scene, 25.0f, 4, DOWN, mui_moveLineUp, 1, DOWN) &&
              Moves(&scene, 25.0f, 5, UP, mui_moveLineUp, 2, UP) &&
              Moves(&scene, 25.0f, 1, DOWN, mui_moveLineUp, 0, DOWN) &&
              Moves(&scene, 25.0f, 1, DOWN, mui_moveLineDown, 4, DOWN) &&
              Moves(&scene, 25.0f, 4, DOWN, mui_moveLineDown, 5, DOWN),
          "up and down at the caret's x");
    muiTextPosition to = {0, 0};
    CHECK(muiTextMove(&scene.host, scene.node, 25.0f, (muiTextPosition){1, DOWN}, mui_moveLineDown,
                      19.0f, &to) == mui_success &&
              to.offset == 5 && to.affinity == UP,
          "down at the preferred x");
    // An empty line: its end is its start, downstream.
    ShowPlain(&scene, "a\n\nb");
    CHECK(Moves(&scene, 25.0f, 2, DOWN, mui_moveLineEnd, 2, DOWN) &&
              Moves(&scene, 25.0f, 0, DOWN, mui_moveLineEnd, 1, UP) &&
              Moves(&scene, 25.0f, 2, DOWN, mui_moveRight, 3, DOWN) &&
              Moves(&scene, 25.0f, 2, DOWN, mui_moveLeft, 1, UP),
          "an empty line");
    // Right to left, a line's right end is its start: right from it goes
    // back to the line before's end, left from the left end on to the
    // next line, and from the last line's left end nowhere.
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(scene.context, scene.node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "direction");
    ShowPlain(&scene, "ab cd");
    CHECK(Moves(&scene, 25.0f, 0, DOWN, mui_moveLeft, 3, DOWN) &&
              Moves(&scene, 25.0f, 2, UP, mui_moveRight, 2, UP) &&
              Moves(&scene, 25.0f, 3, DOWN, mui_moveLeft, 3, DOWN) &&
              Moves(&scene, 25.0f, 4, DOWN, mui_moveRight, 5, UP) &&
              Moves(&scene, 25.0f, 5, UP, mui_moveRight, 2, UP),
          "a right-to-left paragraph");
    ShowPlain(&scene, "a\n\nb");
    CHECK(CaretAt(&scene, 25.0f, 2, DOWN, 25.0f, 10.0f), "an empty line starts at the right");
    FreeScene(&scene);
}

// Whether a deletion from an offset of the block's text removes the
// bytes from start up to end.
static bool Deletes(Scene* scene, const char* text, uint32_t offset, muiTextDeletion deletion,
                    uint32_t start, uint32_t end)
{
    uint32_t from = 99;
    uint32_t to = 99;
    return muiTextBlock_SetText(scene->service, scene->block, text, strlen(text)) == mui_success &&
           muiTextBlock_FindDeletion(scene->service, scene->block, offset, deletion, &from, &to) ==
               mui_success &&
           from == start && to == end;
}

static void TestDeletion(void)
{
    Scene scene = MakeScene();
    muiTextDeletion back = mui_deleteBackward;
    muiTextDeletion forward = mui_deleteForward;
    CHECK(Deletes(&scene, "ab", 0, forward, 0, 1) && Deletes(&scene, "ab", 2, forward, 2, 2) &&
              Deletes(&scene, "ab", 99, forward, 2, 2) && Deletes(&scene, "ab", 2, back, 1, 2) &&
              Deletes(&scene, "ab", 0, back, 0, 0) && Deletes(&scene, "", 0, back, 0, 0),
          "a letter either way, nothing past the ends");
    // x and a combining acute: forward the cluster, back the acute alone.
    CHECK(Deletes(&scene, "x\xCC\x81", 0, forward, 0, 3) &&
              Deletes(&scene, "x\xCC\x81", 3, back, 1, 3) &&
              Deletes(&scene, "x\xCC\x81", 99, back, 1, 3) &&
              Deletes(&scene, "x\xCC\x81", 2, back, 0, 2),
          "a mark");
    // Two jamo make a syllable: back takes the vowel.
    CHECK(Deletes(&scene, "\xE1\x84\x80\xE1\x85\xA1", 6, back, 3, 6), "jamo");
    // A thumbs-up and a skin tone, a flag, a keycap: whole.
    CHECK(Deletes(&scene, "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", 8, back, 0, 8) &&
              Deletes(&scene, "\xF0\x9F\x87\xB9\xF0\x9F\x87\xB7", 8, back, 0, 8) &&
              Deletes(&scene, "1\xEF\xB8\x8F\xE2\x83\xA3", 7, back, 0, 7),
          "emoji, flags and keycaps whole");
    // A letter after an emoji: the letter alone, the emoji kept.
    CHECK(Deletes(&scene,
                  "\xF0\x9F\x98\x80"
                  "a",
                  5, back, 4, 5),
          "the cluster before, not the one before it");
    // A variation selector with what it selects; CR with its LF.
    CHECK(Deletes(&scene, "a\xEF\xB8\x8E", 4, back, 0, 4) &&
              Deletes(&scene, "a\xEF\xB8\x80", 4, back, 0, 4) &&
              Deletes(&scene, "a\xF3\xA0\x84\x80", 5, back, 0, 5) &&
              Deletes(&scene, "a\r\n", 3, back, 1, 3) && Deletes(&scene, "a\n", 2, back, 1, 2),
          "a variation selector, a CR LF");
    uint32_t start = 7;
    uint32_t end = 7;
    muiTextBlockId none = {0, 0};
    CHECK(muiTextBlock_FindDeletion(NULL, scene.block, 0, back, &start, &end) == mui_errorInvalid &&
              muiTextBlock_FindDeletion(scene.service, none, 0, back, &start, &end) ==
                  mui_errorInvalid &&
              muiTextBlock_FindDeletion(scene.service, scene.block, 0, 2, &start, &end) ==
                  mui_errorInvalid &&
              muiTextBlock_FindDeletion(scene.service, scene.block, 0, back, NULL, &end) ==
                  mui_errorInvalid &&
              muiTextBlock_FindDeletion(scene.service, scene.block, 0, back, &start, NULL) ==
                  mui_errorInvalid &&
              start == 7 && end == 7,
          "deletion outside the contract");
    FreeScene(&scene);
}

// Whether the block's text is the one given.
static bool TextIs(Scene* scene, const char* expected)
{
    const char* text = NULL;
    size_t length = 0;
    return muiTextBlock_GetText(scene->service, scene->block, &text, &length) == mui_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static void TestReplace(void)
{
    Scene scene = MakeScene();
    ShowPlain(&scene, "hello world");
    CHECK(muiTextBlock_Replace(scene.service, scene.block, 6, 11, "there", 5) == mui_success &&
              TextIs(&scene, "hello there"),
          "a word replaced");
    CHECK(muiTextBlock_Replace(scene.service, scene.block, 0, 0, "oh, ", 4) == mui_success &&
              TextIs(&scene, "oh, hello there") &&
              muiTextBlock_Replace(scene.service, scene.block, 15, 15, "!", 1) == mui_success &&
              TextIs(&scene, "oh, hello there!") &&
              muiTextBlock_Replace(scene.service, scene.block, 2, 10, NULL, 0) == mui_success &&
              TextIs(&scene, "ohthere!"),
          "inserted at the ends, deleted");
    // A part of its own text put back in.
    const char* own = NULL;
    size_t length = 0;
    CHECK(muiTextBlock_GetText(scene.service, scene.block, &own, &length) == mui_success &&
              muiTextBlock_Replace(scene.service, scene.block, 0, 2, own + 2, 5) == mui_success &&
              TextIs(&scene, "therethere!"),
          "its own text");
    // Measured anew once marked: Ahem's letters are 10 wide.
    CHECK(muiNode_MarkContentChanged(scene.context, scene.node) == mui_success, "marked");
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &scene.host,
                                  0,       NULL,    {0, 0, 0, 0}};
    CHECK(muiComputeLayout(scene.context, scene.node, &input) == mui_success &&
              muiNode_GetRect(scene.context, scene.node).width == 110.0f,
          "laid out anew");
    CHECK(muiTextBlock_Replace(scene.service, scene.block, 3, 2, "x", 1) == mui_errorInvalid &&
              muiTextBlock_Replace(scene.service, scene.block, 0, 12, "x", 1) == mui_errorInvalid &&
              muiTextBlock_Replace(scene.service, scene.block, 11, 12, "x", 1) ==
                  mui_errorInvalid &&
              muiTextBlock_Replace(scene.service, scene.block, 0, 0, "x", 0x7FFFFFFF) ==
                  mui_errorInvalid &&
              muiTextBlock_Replace(scene.service, scene.block, 0, 0, NULL, 1) == mui_errorInvalid &&
              muiTextBlock_Replace(NULL, scene.block, 0, 0, "x", 1) == mui_errorInvalid &&
              muiTextBlock_Replace(scene.service, (muiTextBlockId){0, 0}, 0, 0, "x", 1) ==
                  mui_errorInvalid &&
              TextIs(&scene, "therethere!"),
          "replacing outside the contract");
    CHECK(muiTextBlock_GetText(NULL, scene.block, &own, &length) == mui_errorInvalid &&
              muiTextBlock_GetText(scene.service, scene.block, NULL, &length) == mui_errorInvalid &&
              muiTextBlock_GetText(scene.service, scene.block, &own, NULL) == mui_errorInvalid,
          "reading outside the contract");
    muiTextBlockId gone = scene.block;
    uint32_t start = 0;
    uint32_t end = 0;
    CHECK(muiDestroyTextBlock(scene.service, gone) == mui_success &&
              muiTextBlock_Replace(scene.service, gone, 0, 0, "x", 1) == mui_errorStale &&
              muiTextBlock_GetText(scene.service, gone, &own, &length) == mui_errorStale &&
              muiTextBlock_FindDeletion(scene.service, gone, 0, mui_deleteForward, &start, &end) ==
                  mui_errorStale,
          "a block gone");
    FreeScene(&scene);
    // Memory running out keeps the old text.
    for (int failAt = 1; failAt < 8; failAt++)
    {
        FailingAllocator failing = {0, 0};
        Scene failingScene = MakeFailingScene(&failing);
        ShowPlain(&failingScene, "ab");
        failing = (FailingAllocator){0, failAt};
        muiResult result =
            muiTextBlock_Replace(failingScene.service, failingScene.block, 1, 1, "x y", 3);
        bool failed = failing.allocations >= failAt;
        CHECK(failed ? result == mui_errorCapacity && TextIs(&failingScene, "ab")
                     : result == mui_success && TextIs(&failingScene, "ax yb"),
              "the old text kept");
        FreeScene(&failingScene);
    }
}

// Lays the node out anew at 1000 wide, paints it at scale 10 and
// returns its list.
static muiDrawList PaintAtTen(Scene* scene)
{
    const muiLayoutInput layout = {1000.0f, 1000.0f, muiMeasureText, &scene->host,
                                   0,       NULL,    {0, 0, 0, 0}};
    CHECK(muiNode_MarkContentChanged(scene->context, scene->node) == mui_success &&
              muiComputeLayout(scene->context, scene->node, &layout) == mui_success,
          "laid out");
    const muiDrawInput input = {1, 10.0f, muiPaintText, &scene->host};
    CHECK(muiBuildDrawList(scene->context, scene->node, &input) == mui_success, "paint");
    muiDrawList list;
    CHECK(muiGetDrawList(scene->context, &list) == mui_success, "list");
    return list;
}

// Whether the list's boxes are underlines from x0 to x1, each thin (1) or
// thick (2): Ahem's underline is 0.02 em thick, its top 0.133 em below
// the baseline, here at 8; snapped to tenths.
static bool UnderlinesAre(const muiDrawList* list, const float* spans, uint32_t count)
{
    uint32_t found = 0;
    bool same = true;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        if (list->commands[i].kind != mui_drawBox)
        {
            continue;
        }
        const muiRect* rect = &list->commands[i].box.rect;
        same = same && found < count && fabsf(rect->x - spans[found * 3]) < 1e-4f &&
               fabsf(rect->x + rect->width - spans[found * 3 + 1]) < 1e-4f &&
               fabsf(rect->y - 9.3f) < 1e-4f &&
               fabsf(rect->height - 0.2f * spans[found * 3 + 2]) < 1e-4f;
        found++;
    }
    return same && found == count;
}

static bool CompositionIs(Scene* scene, uint32_t start, uint32_t length)
{
    uint32_t at = 99;
    uint32_t size = 99;
    return muiTextBlock_GetComposition(scene->service, scene->block, &at, &size) == mui_success &&
           at == start && size == length;
}

static void TestComposition(void)
{
    Scene scene = MakeScene();
    ShowPlain(&scene, "ab");
    const muiCompositionSegment two[2] = {{0, 1, mui_compositionTarget},
                                          {1, 1, mui_compositionUnderline}};
    CHECK(muiTextBlock_SetComposition(scene.service, scene.block, 1, "xy", 2, two, 2) ==
                  mui_success &&
              TextIs(&scene, "axyb") && CompositionIs(&scene, 1, 2),
          "a composition put in");
    muiDrawList list = PaintAtTen(&scene);
    CHECK(UnderlinesAre(&list, (const float[]){10.0f, 20.0f, 2.0f, 20.0f, 30.0f, 1.0f}, 2),
          "the target thick, the rest thin");
    // Updated: replaces the old, offset not read; no segments, all thin.
    CHECK(muiTextBlock_SetComposition(scene.service, scene.block, 0, "xyz", 3, NULL, 0) ==
                  mui_success &&
              TextIs(&scene, "axyzb") && CompositionIs(&scene, 1, 3),
          "updated");
    list = PaintAtTen(&scene);
    CHECK(UnderlinesAre(&list, (const float[]){10.0f, 40.0f, 1.0f}, 1), "underlined whole");
    const muiCompositionSegment plain = {0, 3, mui_compositionPlain};
    CHECK(muiTextBlock_SetComposition(scene.service, scene.block, 0, "xyz", 3, &plain, 1) ==
              mui_success,
          "plain");
    list = PaintAtTen(&scene);
    CHECK(UnderlinesAre(&list, NULL, 0), "a plain segment undrawn");
    // Edits before move it, after leave it, over it end it.
    CHECK(muiTextBlock_Replace(scene.service, scene.block, 0, 1, "AA", 2) == mui_success &&
              CompositionIs(&scene, 2, 3) &&
              muiTextBlock_Replace(scene.service, scene.block, 5, 6, NULL, 0) == mui_success &&
              CompositionIs(&scene, 2, 3) && TextIs(&scene, "AAxyz") &&
              muiTextBlock_Replace(scene.service, scene.block, 2, 2, "-", 1) == mui_success &&
              CompositionIs(&scene, 3, 3) &&
              muiTextBlock_Replace(scene.service, scene.block, 5, 6, NULL, 0) == mui_success &&
              CompositionIs(&scene, 3, 0) && TextIs(&scene, "AA-xy"),
          "edits around it");
    list = PaintAtTen(&scene);
    CHECK(UnderlinesAre(&list, NULL, 0), "ended: undrawn");
    // A new one at the end, then emptied: removed.
    CHECK(muiTextBlock_SetComposition(scene.service, scene.block, 99, "q", 1, NULL, 0) ==
                  mui_success &&
              TextIs(&scene, "AA-xyq") && CompositionIs(&scene, 5, 1) &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, NULL, 0, NULL, 0) ==
                  mui_success &&
              TextIs(&scene, "AA-xy") && CompositionIs(&scene, 5, 0),
          "an empty composition removed");
    // Ended as typed, or by new text.
    CHECK(muiTextBlock_SetComposition(scene.service, scene.block, 0, "k", 1, NULL, 0) ==
                  mui_success &&
              muiTextBlock_EndComposition(scene.service, scene.block) == mui_success &&
              TextIs(&scene, "kAA-xy") && CompositionIs(&scene, 0, 0) &&
              muiTextBlock_SetComposition(scene.service, scene.block, 1, "k", 1, NULL, 0) ==
                  mui_success &&
              muiTextBlock_SetText(scene.service, scene.block, "new", 3) == mui_success &&
              CompositionIs(&scene, 1, 0),
          "ended");
    // Over two lines at 25 wide: a thin underline on each.
    CHECK(muiTextBlock_SetText(scene.service, scene.block, "", 0) == mui_success &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, "ab cd", 5, NULL, 0) ==
                  mui_success &&
              muiNode_MarkContentChanged(scene.context, scene.node) == mui_success,
          "a long one");
    const muiLayoutInput input = {25.0f, 1000.0f, muiMeasureText, &scene.host,
                                  0,     NULL,    {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 10.0f, muiPaintText, &scene.host};
    CHECK(muiComputeLayout(scene.context, scene.node, &input) == mui_success &&
              muiBuildDrawList(scene.context, scene.node, &draw) == mui_success &&
              muiGetDrawList(scene.context, &list) == mui_success,
          "laid out at 25");
    uint32_t boxes = 0;
    for (uint32_t i = 0; i < list.commandCount; i++)
    {
        boxes += list.commands[i].kind == mui_drawBox ? 1u : 0u;
    }
    CHECK(boxes == 2, "an underline a line");
    FreeScene(&scene);
}

static void TestCompositionContract(void)
{
    Scene scene = MakeScene();
    ShowPlain(&scene, "ab");
    muiCompositionSegment many[MUI_MAX_COMPOSITION_SEGMENTS + 1] = {{0}};
    const muiCompositionSegment past = {1, 2, mui_compositionTarget};
    const muiCompositionSegment unknown = {0, 1, 4};
    const muiCompositionSegment start = {3, 0, mui_compositionTarget};
    muiTextBlockId none = {0, 0};
    CHECK(muiTextBlock_SetComposition(NULL, scene.block, 0, "x", 1, NULL, 0) == mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, none, 0, "x", 1, NULL, 0) ==
                  mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, NULL, 1, NULL, 0) ==
                  mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, "x", 1, NULL, 1) ==
                  mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, "xy", 2, many,
                                          MUI_MAX_COMPOSITION_SEGMENTS + 1) == mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, "xy", 2, &past, 1) ==
                  mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, "xy", 2, &unknown, 1) ==
                  mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, "xy", 2, &start, 1) ==
                  mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, "x", 0x7FFFFFFF, NULL,
                                          0) == mui_errorInvalid &&
              muiTextBlock_SetComposition(scene.service, scene.block, 0, "xy", 2, many,
                                          MUI_MAX_COMPOSITION_SEGMENTS) == mui_success &&
              TextIs(&scene, "xyab"),
          "compositions outside the contract");
    uint32_t at = 7;
    uint32_t size = 7;
    CHECK(muiTextBlock_EndComposition(NULL, scene.block) == mui_errorInvalid &&
              muiTextBlock_EndComposition(scene.service, none) == mui_errorInvalid &&
              muiTextBlock_GetComposition(NULL, scene.block, &at, &size) == mui_errorInvalid &&
              muiTextBlock_GetComposition(scene.service, none, &at, &size) == mui_errorInvalid &&
              muiTextBlock_GetComposition(scene.service, scene.block, NULL, &size) ==
                  mui_errorInvalid &&
              muiTextBlock_GetComposition(scene.service, scene.block, &at, NULL) ==
                  mui_errorInvalid &&
              at == 7 && size == 7,
          "ending and reading outside the contract");
    muiTextBlockId gone = scene.block;
    CHECK(muiDestroyTextBlock(scene.service, gone) == mui_success &&
              muiTextBlock_SetComposition(scene.service, gone, 0, "x", 1, NULL, 0) ==
                  mui_errorStale &&
              muiTextBlock_EndComposition(scene.service, gone) == mui_errorStale &&
              muiTextBlock_GetComposition(scene.service, gone, &at, &size) == mui_errorStale,
          "a block gone");
    FreeScene(&scene);
    // Memory running out keeps the old text and composition.
    for (int failAt = 1; failAt < 8; failAt++)
    {
        FailingAllocator failing = {0, 0};
        Scene failingScene = MakeFailingScene(&failing);
        ShowPlain(&failingScene, "ab");
        CHECK(muiTextBlock_SetComposition(failingScene.service, failingScene.block, 1, "x", 1, NULL,
                                          0) == mui_success,
              "first");
        failing = (FailingAllocator){0, failAt};
        muiResult result = muiTextBlock_SetComposition(
            failingScene.service, failingScene.block, 0, "xyz", 3,
            &(const muiCompositionSegment){0, 3, mui_compositionTarget}, 1);
        bool failed = failing.allocations >= failAt;
        CHECK(failed ? result == mui_errorCapacity && TextIs(&failingScene, "axb") &&
                           CompositionIs(&failingScene, 1, 1)
                     : result == mui_success && TextIs(&failingScene, "axyzb") &&
                           CompositionIs(&failingScene, 1, 3),
              "the old kept");
        FreeScene(&failingScene);
    }
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
    CHECK(muiTextMove(NULL, scene.node, 100.0f, position, mui_moveRight, NAN, &position) ==
                  mui_errorInvalid &&
              muiTextMove(&scene.host, scene.node, 100.0f, position, mui_moveRight, NAN, NULL) ==
                  mui_errorInvalid &&
              muiTextMove(&scene.host, scene.node, 100.0f, position, mui_moveTextEnd + 1, NAN,
                          &position) == mui_errorInvalid &&
              position.offset == 7,
          "moving outside the contract");
    ShowPlain(&scene, "");
    CHECK(muiTextMove(&scene.host, scene.node, 100.0f, position, mui_moveTextEnd, NAN, &position) ==
                  mui_success &&
              position.offset == 0,
          "no text: nowhere to move");
    muiNodeId stale = scene.node;
    CHECK(muiDestroyNode(scene.context, scene.node) == mui_success &&
              muiTextHitTest(&scene.host, stale, 100.0f, 0.0f, 0.0f, &position) == mui_errorStale &&
              muiTextGetCaret(&scene.host, stale, 100.0f, position, &caret) == mui_errorStale &&
              muiTextGetRangeRects(&scene.host, stale, 100.0f, 0, 1, rects, 2, &count) ==
                  mui_errorStale &&
              muiTextMove(&scene.host, stale, 100.0f, position, mui_moveLeft, NAN, &position) ==
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
    TestMovingInText();
    TestMovingOnScreen();
    TestDeletion();
    TestReplace();
    TestComposition();
    TestCompositionContract();
    TestLigature();
    TestMemory();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
