// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The layout API around the solver: authored values, refusals, the
// measure function's contract, and work skipped when nothing changed.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"

#include <math.h>

static const muiNodeId s_null = {0, 0};

// A host whose content is a fixed size, counting the measurements and
// trying an edit from inside one when asked.
typedef struct Host
{
    muiContext* context;
    muiSize content;
    int measured;
    int decided;
    bool tryEdit;
    muiResult editStatus;
} Host;

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)hostKey;
    Host* host = user;
    host->measured++;
    host->decided += width.mode == mui_measureExact && height.mode == mui_measureExact;
    if (host->tryEdit)
    {
        muiLayoutStyle style = muiDefaultLayoutStyle();
        host->editStatus =
            muiNode_SetLayoutValues(host->context, nodeId, &style, MUI_LAYOUT_PROPERTIES);
    }
    return host->content;
}

// Text that wraps: hostKey packs its natural width (high half) and its
// longest word (low half); lines are 10 high.
static muiSize MeasureText(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                           muiMeasureAxis height)
{
    (void)user;
    (void)nodeId;
    (void)height;
    float natural = (float)(hostKey >> 32);
    float word = (float)(hostKey & 0xFFFFFFFFu);
    float space = natural;
    if (width.mode == mui_measureExact || width.mode == mui_measureAtMost)
    {
        space = width.size;
    }
    else if (width.mode == mui_measureMinContent)
    {
        space = word;
    }
    float line = fmaxf(fminf(space, natural), word);
    float lines = ceilf(natural / line);
    float used = width.mode == mui_measureExact ? width.size : line;
    return (muiSize){used, lines * 10.0f};
}

static uint64_t TextKey(uint32_t natural, uint32_t word)
{
    return ((uint64_t)natural << 32) | word;
}

static muiContext* MakeContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create context");
    return context;
}

static muiNodeId MakeNode(muiContext* context, const muiLayoutStyle* style)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_null;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "create node");
    if (style != NULL)
    {
        CHECK(muiNode_SetLayoutValues(context, node, style, MUI_LAYOUT_PROPERTIES) == mui_success,
              "style");
    }
    return node;
}

static muiDimension Length(float offset)
{
    return (muiDimension){0.0f, offset, mui_dimensionValue};
}

static muiLayoutInput Input(Host* host)
{
    return (muiLayoutInput){400.0f, 300.0f, Measure, host, 0, NULL, {0, 0, 0, 0}};
}

static void TestDefaultsAreCssInitialValues(void)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    CHECK(style.container.direction == mui_flexRow, "row");
    CHECK(style.item.grow == 0.0f && style.item.shrink == 1.0f, "grow 0, shrink 1");
    CHECK(style.item.basis.kind == mui_dimensionAuto, "basis auto");
    CHECK(style.container.alignItems == mui_alignStretch, "stretch");
    CHECK(style.item.alignSelf == mui_alignAuto, "self auto");
    CHECK(style.sizing.minWidth.kind == mui_dimensionAuto, "automatic minimum");
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context, NULL);
    muiLayoutStyle read;
    CHECK(muiNode_GetLayoutStyle(context, node, &read) == mui_success, "read");
    CHECK(read.item.shrink == 1.0f && read.container.alignItems == mui_alignStretch,
          "a new node has the defaults");
    muiDestroyContext(context);
}

static void TestBadStylesAreMisuse(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context, NULL);
    muiLayoutStyle bad[8];
    for (int i = 0; i < 8; i++)
    {
        bad[i] = muiDefaultLayoutStyle();
    }
    bad[0].item.grow = -1.0f;
    bad[1].sizing.width = (muiDimension){NAN, 0.0f, mui_dimensionValue};
    bad[2].padding.top = -2.0f;
    bad[3].container.alignItems = mui_alignAuto;
    bad[4].container.direction = 9;
    bad[5].container.columnGap = INFINITY;
    bad[6].container.alignItems = mui_alignBaseline + 1;
    bad[7].item.alignSelf = mui_alignBaseline + 1;
    for (int i = 0; i < 8; i++)
    {
        CHECK(muiNode_SetLayoutValues(context, node, &bad[i], MUI_LAYOUT_PROPERTIES) ==
                  mui_errorInvalid,
              "refused");
    }
    CHECK(muiNode_SetLayoutValues(context, node, NULL, MUI_LAYOUT_PROPERTIES) == mui_errorInvalid,
          "NULL style");
    CHECK(muiContext_GetMisuse(context) == 9, "each counted");
    muiLayoutStyle baseline = muiDefaultLayoutStyle();
    baseline.container.alignItems = mui_alignBaseline;
    baseline.item.alignSelf = mui_alignBaseline;
    CHECK(muiNode_SetLayoutValues(context, node, &baseline, MUI_LAYOUT_PROPERTIES) == mui_success,
          "baseline is valid");
    muiLayoutStyle negativeMargin = muiDefaultLayoutStyle();
    negativeMargin.margin.start = -5.0f;
    CHECK(muiNode_SetLayoutValues(context, node, &negativeMargin, MUI_LAYOUT_PROPERTIES) ==
              mui_success,
          "negative margins are allowed");
    muiDestroyContext(context);
}

static void TestComputeRefusesNonRootsAndBadSpace(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiNodeId root = MakeNode(context, NULL);
    muiNodeId child = MakeNode(context, NULL);
    CHECK(muiNode_InsertChild(context, root, child, s_null) == mui_success, "insert");
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, child, &input) == mui_errorInvalid, "not a root");
    input.availableWidth = -1.0f;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_errorInvalid, "negative space");
    input.availableWidth = INFINITY;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_errorInvalid, "infinite space");
    CHECK(muiNode_ComputeLayout(context, root, NULL) == mui_errorInvalid, "NULL input");
    CHECK(muiDestroyNode(context, child) == mui_success, "destroy child");
    input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, child, &input) == mui_errorStale, "stale root");
    muiDestroyContext(context);
}

static void TestEditsFromMeasureAreRefused(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context, .content = {10.0f, 10.0f}, .tryEdit = true};
    muiLayoutStyle leaf = muiDefaultLayoutStyle();
    leaf.content = mui_contentHost;
    muiNodeId root = MakeNode(context, &leaf);
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(host.measured > 0, "measured");
    CHECK(host.editStatus == mui_errorBusy, "edit refused inside measure");
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_null;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "edits work again after");
    muiDestroyContext(context);
}

static void TestUnchangedTreeIsNotMeasuredAgain(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context, .content = {30.0f, 12.0f}};
    muiNodeId root = MakeNode(context, NULL);
    muiLayoutStyle leaf = muiDefaultLayoutStyle();
    leaf.content = mui_contentHost;
    muiNodeId text = MakeNode(context, &leaf);
    CHECK(muiNode_InsertChild(context, root, text, s_null) == mui_success, "insert");
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "first layout");
    CHECK(host.measured > 0, "measured once laid out");
    host.measured = 0;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "second layout");
    CHECK(host.measured == 0, "a static frame measures nothing");
    muiRect rect = muiNode_GetRect(context, text);
    CHECK(rect.width == 30.0f && rect.height == 12.0f, "content size");

    host.content = (muiSize){50.0f, 12.0f};
    CHECK(muiNode_MarkContentChanged(context, text) == mui_success, "mark");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "third layout");
    CHECK(host.measured > 0, "changed content is measured");
    CHECK(muiNode_GetRect(context, text).width == 50.0f, "new size");
    CHECK(muiNode_GetRect(context, root).width == 50.0f, "the root fits it");
    muiDestroyContext(context);
}

static void TestDecidedSizesAreNotMeasured(void)
{
    // A row of text beside a leaf of 40 by 20: the text stretches to 20.
    // Its sizes are asked for, but no query with both exact, as the final
    // pass gives: the host lays out at the rectangle.
    muiContext* context = MakeContext();
    Host host = {.context = context, .content = {30.0f, 12.0f}};
    muiNodeId root = MakeNode(context, NULL);
    muiLayoutStyle leaf = muiDefaultLayoutStyle();
    leaf.content = mui_contentHost;
    muiNodeId text = MakeNode(context, &leaf);
    leaf.sizing.width = Length(40.0f);
    leaf.sizing.height = Length(20.0f);
    muiNodeId sized = MakeNode(context, &leaf);
    CHECK(muiNode_InsertChild(context, root, text, s_null) == mui_success &&
              muiNode_InsertChild(context, root, sized, s_null) == mui_success,
          "insert");
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(host.measured > 0 && host.decided == 0, "nothing decided asked");
    muiRect rect = muiNode_GetRect(context, text);
    muiRect fixed = muiNode_GetRect(context, sized);
    CHECK(rect.width == 30.0f && rect.height == 20.0f && fixed.width == 40.0f &&
              fixed.height == 20.0f,
          "sizes");
    muiDestroyContext(context);
}

static void TestLooseRowLabelIsMeasuredOnce(void)
{
    // A row of its content's height inside a row: its label is asked the
    // same once, whether its height is loose being no key of a leaf's.
    muiContext* context = MakeContext();
    Host host = {.context = context, .content = {30.0f, 12.0f}};
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.width = Length(200.0f);
    style.sizing.height = Length(200.0f);
    muiNodeId root = MakeNode(context, &style);
    style = muiDefaultLayoutStyle();
    style.item.alignSelf = mui_alignStart;
    muiNodeId row = MakeNode(context, &style);
    style = muiDefaultLayoutStyle();
    style.content = mui_contentHost;
    muiNodeId label = MakeNode(context, &style);
    CHECK(muiNode_InsertChild(context, root, row, s_null) == mui_success &&
              muiNode_InsertChild(context, row, label, s_null) == mui_success,
          "insert");
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(host.measured == 1, "the label measured once");
    muiRect rect = muiNode_GetRect(context, label);
    CHECK(rect.width == 30.0f && rect.height == 12.0f, "its size");

    muiDestroyContext(context);

    // A column there reads no loose height either: asked with and
    // without it, it keeps one answer, and the four nodes are sized
    // eight times (nine when the flag split the column's entries).
    context = MakeContext();
    host = (Host){.context = context, .content = {30.0f, 12.0f}};
    style = muiDefaultLayoutStyle();
    style.sizing.width = Length(200.0f);
    style.sizing.height = Length(200.0f);
    root = MakeNode(context, &style);
    style = muiDefaultLayoutStyle();
    style.item.alignSelf = mui_alignStart;
    row = MakeNode(context, &style);
    style = muiDefaultLayoutStyle();
    style.container.direction = mui_flexColumn;
    muiNodeId column = MakeNode(context, &style);
    style = muiDefaultLayoutStyle();
    style.content = mui_contentHost;
    label = MakeNode(context, &style);
    CHECK(muiNode_InsertChild(context, root, row, s_null) == mui_success &&
              muiNode_InsertChild(context, row, column, s_null) == mui_success &&
              muiNode_InsertChild(context, column, label, s_null) == mui_success,
          "insert the column");
    input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout the column");
    CHECK(muiContext_GetWorkCounts(context).sized == 8, "the column sized once a query");
    muiDestroyContext(context);
}

static void TestStyleChangeRelaysTheParent(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.sizing.width = Length(100.0f);
    row.sizing.height = Length(20.0f);
    muiNodeId root = MakeNode(context, &row);
    muiLayoutStyle fixed = muiDefaultLayoutStyle();
    fixed.sizing.width = Length(30.0f);
    muiNodeId first = MakeNode(context, &fixed);
    muiNodeId second = MakeNode(context, &fixed);
    CHECK(muiNode_InsertChild(context, root, first, s_null) == mui_success, "first");
    CHECK(muiNode_InsertChild(context, root, second, s_null) == mui_success, "second");
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(muiNode_GetRect(context, second).x == 30.0f, "after the first");
    fixed.sizing.width = Length(45.0f);
    CHECK(muiNode_SetLayoutValues(context, first, &fixed, MUI_LAYOUT_PROPERTIES) == mui_success,
          "widen first");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout again");
    CHECK(muiNode_GetRect(context, second).x == 45.0f, "the sibling moved");
    CHECK(muiNode_Detach(context, first) == mui_success, "detach");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout once more");
    CHECK(muiNode_GetRect(context, second).x == 0.0f, "the sibling took its place");
    muiDestroyContext(context);
}

static void TestNewSpaceRelaysAnUnchangedTree(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiLayoutStyle full = muiDefaultLayoutStyle();
    full.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    full.sizing.height = Length(20.0f);
    muiNodeId root = MakeNode(context, &full);
    muiLayoutStyle grow = muiDefaultLayoutStyle();
    grow.item.grow = 1.0f;
    muiNodeId child = MakeNode(context, &grow);
    CHECK(muiNode_InsertChild(context, root, child, s_null) == mui_success, "insert");
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(muiNode_GetRect(context, child).width == 400.0f, "fills the space");
    input.availableWidth = 250.0f;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout in less space");
    CHECK(muiNode_GetRect(context, root).width == 250.0f, "the root follows the space");
    CHECK(muiNode_GetRect(context, child).width == 250.0f, "and so does its child");
    muiDestroyContext(context);
}

static void TestScaledLimitFollowsTheParent(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context, .content = {300.0f, 10.0f}};
    muiLayoutStyle full = muiDefaultLayoutStyle();
    full.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    muiNodeId root = MakeNode(context, &full);
    muiLayoutStyle half = muiDefaultLayoutStyle();
    half.sizing.maxWidth = (muiDimension){0.5f, 0.0f, mui_dimensionValue};
    muiNodeId box = MakeNode(context, &half);
    muiLayoutStyle leaf = muiDefaultLayoutStyle();
    leaf.content = mui_contentHost;
    muiNodeId text = MakeNode(context, &leaf);
    CHECK(muiNode_InsertChild(context, root, box, s_null) == mui_success, "box");
    CHECK(muiNode_InsertChild(context, box, text, s_null) == mui_success, "text");
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(muiNode_GetRect(context, box).width == 200.0f, "half of 400");
    input.availableWidth = 800.0f;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "wider");
    CHECK(muiNode_GetRect(context, box).width == 300.0f, "its content, under half of 800");
    muiDestroyContext(context);
}

static muiNodeId MakeText(muiContext* context, muiNodeId parent, uint64_t key)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = key;
    muiNodeId node = s_null;
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.content = mui_contentHost;
    style.sizing.minWidth = Length(0.0f);
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "text");
    CHECK(muiNode_SetLayoutValues(context, node, &style, MUI_LAYOUT_PROPERTIES) == mui_success,
          "text style");
    CHECK(muiNode_InsertChild(context, parent, node, s_null) == mui_success, "text insert");
    return node;
}

// Lays out a row of two wrapping texts with too little space, as the
// root or nested in an automatic root, and checks the row is as tall as
// its tallest text at the widths it gave them, not as its min-content
// layout was.
static void CheckShrunkTexts(bool nested)
{
    muiContext* context = MakeContext();
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.alignItems = mui_alignStart;
    muiNodeId root = MakeNode(context, &row);
    muiNodeId line = root;
    if (nested)
    {
        line = MakeNode(context, &row);
        CHECK(muiNode_InsertChild(context, root, line, s_null) == mui_success, "nest");
    }
    muiNodeId wide = MakeText(context, line, TextKey(100, 30));
    muiNodeId narrow = MakeText(context, line, TextKey(40, 20));
    // The row takes its min-content width, 50, and shrinks the texts to
    // it by their natural widths.
    muiLayoutInput input = {10.0f, 300.0f, MeasureText, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    muiRect a = muiNode_GetRect(context, wide);
    muiRect b = muiNode_GetRect(context, narrow);
    CHECK(muiNode_GetRect(context, line).width == 50.0f, "min-content width");
    CHECK(a.width + b.width == 50.0f && a.width > 30.0f, "shrunk by natural width");
    CHECK(a.height == ceilf(100.0f / a.width) * 10.0f, "wide text wraps at its width");
    CHECK(muiNode_GetRect(context, line).height == fmaxf(a.height, b.height),
          "the row is as tall as its tallest text");
    muiDestroyContext(context);
}

static void TestShrunkTextIsNotTakenFromItsMinContentSize(void)
{
    CheckShrunkTexts(false);
    CheckShrunkTexts(true);
}

// A column as wide as the space with text aligned to the start: the
// text's width is fit-content in that space.
static void CheckTextFollowsSpace(float first, float second)
{
    muiContext* context = MakeContext();
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.container.direction = mui_flexColumn;
    column.container.alignItems = mui_alignStart;
    column.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    muiNodeId root = MakeNode(context, &column);
    muiNodeId text = MakeText(context, root, TextKey(100, 10));
    muiLayoutInput input = {first, 300.0f, MeasureText, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(muiNode_GetRect(context, text).width == fminf(first, 100.0f), "first space");
    input.availableWidth = second;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout again");
    CHECK(muiNode_GetRect(context, text).width == fminf(second, 100.0f), "second space");
    muiDestroyContext(context);
}

static void TestTextFollowsTheSpaceBothWays(void)
{
    CheckTextFollowsSpace(100.0f, 60.0f);
    CheckTextFollowsSpace(60.0f, 100.0f);
}

// A box as wide as its content holding text half its width: the text
// is as wide as it would be at its own size to give the box that width
// (CSS's percentages in an intrinsic size), then half of it, wrapping
// to two lines, and the box is as tall as they are, not as its
// max-content height at the same width.
static void TestScaledChildWrapsInItsBox(void)
{
    muiContext* context = MakeContext();
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.alignItems = mui_alignStart;
    muiNodeId root = MakeNode(context, &row);
    muiNodeId box = MakeNode(context, &row);
    CHECK(muiNode_InsertChild(context, root, box, s_null) == mui_success, "box");
    muiNodeId text = MakeText(context, box, TextKey(100, 10));
    muiLayoutStyle half = muiDefaultLayoutStyle();
    half.content = mui_contentHost;
    half.sizing.minWidth = Length(0.0f);
    half.sizing.width = (muiDimension){0.5f, 0.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, text, &half, MUI_LAYOUT_PROPERTIES) == mui_success,
          "half");
    muiLayoutInput input = {400.0f, 300.0f, MeasureText, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    muiRect inner = muiNode_GetRect(context, text);
    CHECK(muiNode_GetRect(context, box).width == 100.0f && inner.width == 50.0f &&
              inner.height == 20.0f,
          "its content's width; the text half of it, on two lines");
    CHECK(muiNode_GetRect(context, box).height == 20.0f, "the box as tall as the text");
    muiDestroyContext(context);
}

// A root as wide as its content holding a box a tenth of its width with
// text in it: laid out in a limited space, the box is a tenth of the
// root's width and its text wraps to its height. A change that leaves
// the root's cache, as one in another text's wrapper, keeps that, not
// the root's max-content size, where the tenth counted as automatic.
static void TestScaledChildKeepsItsRootsHeight(void)
{
    muiContext* context = MakeContext();
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.alignItems = mui_alignStart;
    muiNodeId root = MakeNode(context, &row);
    muiLayoutStyle tenth = muiDefaultLayoutStyle();
    tenth.container.alignItems = mui_alignStart;
    tenth.sizing.width = (muiDimension){0.1f, 0.0f, mui_dimensionValue};
    muiNodeId box = MakeNode(context, &tenth);
    CHECK(muiNode_InsertChild(context, root, box, s_null) == mui_success, "box");
    muiNodeId text = MakeText(context, box, TextKey(100, 10));
    muiNodeId wrapper = MakeNode(context, &row);
    CHECK(muiNode_InsertChild(context, root, wrapper, s_null) == mui_success, "wrapper");
    muiNodeId other = MakeText(context, wrapper, TextKey(20, 10));
    muiLayoutInput input = {400.0f, 300.0f, MeasureText, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    muiRect first = muiNode_GetRect(context, root);
    CHECK(first.width == 120.0f && muiNode_GetRect(context, box).width == 12.0f &&
              muiNode_GetRect(context, text).height == 90.0f && first.height == 90.0f,
          "a tenth of 120, the text on nine lines");
    CHECK(muiNode_MarkContentChanged(context, other) == mui_success, "the other text");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "again");
    muiRect second = muiNode_GetRect(context, root);
    CHECK(second.width == 120.0f && second.height == 90.0f, "the same root");
    muiDestroyContext(context);
}

// Text held to a maximum width narrower than its line is one line long
// at max-content, clamped to that width, and wraps at it when laid out
// there: two lines, and its row as tall.
static void TestLimitedTextWrapsAtItsLimit(void)
{
    muiContext* context = MakeContext();
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.alignItems = mui_alignStart;
    muiNodeId root = MakeNode(context, &row);
    muiNodeId text = MakeText(context, root, TextKey(100, 10));
    muiLayoutStyle limited = muiDefaultLayoutStyle();
    limited.content = mui_contentHost;
    limited.sizing.minWidth = Length(0.0f);
    limited.sizing.maxWidth = Length(50.0f);
    CHECK(muiNode_SetLayoutValues(context, text, &limited, MUI_LAYOUT_PROPERTIES) == mui_success,
          "limited");
    muiLayoutInput input = {400.0f, 300.0f, MeasureText, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    muiRect rect = muiNode_GetRect(context, text);
    CHECK(rect.width == 50.0f && rect.height == 20.0f, "two lines at its limit");
    CHECK(muiNode_GetRect(context, root).height == 20.0f, "the row as tall");
    muiDestroyContext(context);
}

static void TestDirectionChangeReachesInheritingDescendants(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.sizing.width = Length(200.0f);
    row.sizing.height = Length(20.0f);
    muiNodeId root = MakeNode(context, &row);
    muiLayoutStyle box = muiDefaultLayoutStyle();
    box.sizing.width = Length(100.0f);
    muiNodeId inner = MakeNode(context, &box);
    muiLayoutStyle leaf = muiDefaultLayoutStyle();
    leaf.sizing.width = Length(30.0f);
    muiNodeId first = MakeNode(context, &leaf);
    CHECK(muiNode_InsertChild(context, root, inner, s_null) == mui_success, "inner");
    CHECK(muiNode_InsertChild(context, inner, first, s_null) == mui_success, "first");
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "left to right");
    CHECK(muiNode_GetRect(context, first).x == 0.0f, "at the left of inner");
    row.textDirection = mui_inlineRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, root, &row, MUI_LAYOUT_PROPERTIES) == mui_success,
          "right to left");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "again");
    CHECK(muiNode_GetRect(context, inner).x == 100.0f, "inner at the right of root");
    CHECK(muiNode_GetRect(context, first).x == 70.0f, "and its child at its right, inherited");
    muiDestroyContext(context);
}

static bool RectIs(muiRect rect, float x, float y, float width, float height)
{
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

// The safe area: a node pads by the inset of each edge it names, on the
// physical side the edge falls on, at least its own padding; new insets
// lay it out again, the same ones nothing.
static void TestSafeArea(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.width = Length(400.0f);
    style.sizing.height = Length(300.0f);
    style.padding = (muiEdges){10.0f, 0.0f, 10.0f, 10.0f};
    style.safeArea = mui_edgeStart | mui_edgeEnd | mui_edgeTop | mui_edgeBottom;
    muiNodeId root = MakeNode(context, &style);
    muiLayoutInput input = Input(&host);
    input.safeArea = (muiSides){20.0f, 30.0f, 5.0f, 0.0f};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "laid out");
    CHECK(RectIs(muiNode_GetContentRect(context, root), 10.0f, 20.0f, 360.0f, 270.0f),
          "start 10 over a left of 0, end 30 from the right, top 20, bottom its own 10");
    // Right to left, start is the right: 30 there, the end's 0 on the left.
    style.textDirection = mui_inlineRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, root, &style, MUI_LAYOUT_PROPERTIES) == mui_success,
          "right to left");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "laid out");
    CHECK(RectIs(muiNode_GetContentRect(context, root), 0.0f, 20.0f, 370.0f, 270.0f),
          "the insets fall on the physical sides");
    // The bottom alone.
    style.textDirection = mui_inlineInherit;
    style.safeArea = mui_edgeBottom;
    input.safeArea.bottom = 40.0f;
    CHECK(muiNode_SetLayoutValues(context, root, &style, MUI_LAYOUT_PROPERTIES) == mui_success,
          "the bottom");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "laid out");
    CHECK(RectIs(muiNode_GetContentRect(context, root), 10.0f, 10.0f, 390.0f, 250.0f),
          "the bottom's 40, the rest its own");
    // The same insets cost nothing; new ones lay the node out again.
    muiWorkCounts before = muiContext_GetWorkCounts(context);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success &&
              muiContext_GetWorkCounts(context).sized == before.sized,
          "the same insets: no work");
    input.safeArea.bottom = 50.0f;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success &&
              RectIs(muiNode_GetContentRect(context, root), 10.0f, 10.0f, 390.0f, 240.0f),
          "new insets: laid out again");
    // An absolute child across the root's padding box, padded at its top
    // and start, sized by its insets.
    muiLayoutStyle over = muiDefaultLayoutStyle();
    over.placement.position = mui_positionAbsolute;
    over.placement.inset = (muiInsets){Length(0.0f), Length(0.0f), Length(0.0f), Length(0.0f)};
    over.safeArea = mui_edgeTop | mui_edgeStart;
    muiNodeId overlay = MakeNode(context, &over);
    input.safeArea.left = 15.0f;
    CHECK(muiNode_InsertChild(context, root, overlay, s_null) == mui_success &&
              muiNode_ComputeLayout(context, root, &input) == mui_success &&
              RectIs(muiNode_GetRect(context, overlay), 0.0f, 0.0f, 400.0f, 300.0f) &&
              RectIs(muiNode_GetContentRect(context, overlay), 15.0f, 20.0f, 385.0f, 280.0f),
          "an absolute child pads by the top and the left");
    input.safeArea.left = -1.0f;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_errorInvalid, "a negative inset");
    input.safeArea.left = NAN;
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_errorInvalid, "a NaN inset");
    muiDestroyContext(context);
}

// A size that depends on direction, through a safe area on a start edge,
// is not taken from a size cached in the other direction.
static void TestSafeAreaSizeFollowsDirection(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.sizing.width = Length(400.0f);
    row.sizing.height = Length(100.0f);
    row.container.alignItems = mui_alignStart;
    muiNodeId root = MakeNode(context, &row);
    muiLayoutStyle box = muiDefaultLayoutStyle();
    muiNodeId inner = MakeNode(context, &box);
    muiLayoutStyle leaf = muiDefaultLayoutStyle();
    leaf.sizing.height = Length(10.0f);
    leaf.safeArea = mui_edgeStart;
    muiNodeId padded = MakeNode(context, &leaf);
    CHECK(muiNode_InsertChild(context, root, inner, s_null) == mui_success &&
              muiNode_InsertChild(context, inner, padded, s_null) == mui_success,
          "a box holding a padded leaf");
    muiLayoutInput input = Input(&host);
    input.safeArea = (muiSides){0.0f, 30.0f, 0.0f, 0.0f};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success &&
              muiNode_GetRect(context, inner).width == 0.0f,
          "left to right, start is the left, of 0");
    row.textDirection = mui_inlineRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, root, &row, MUI_LAYOUT_PROPERTIES) == mui_success,
          "right to left");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success &&
              muiNode_GetRect(context, inner).width == 30.0f &&
              muiNode_GetRect(context, inner).x == 370.0f,
          "right to left, start is the right, of 30: the box grows");
    muiDestroyContext(context);
}

// A row 40 wide with a ratio of 2, stretched in a row, holding a child
// 50% high and one 60 high: its content grows it to 60, and the line
// stretches it there, so the 50% is 30, as in Firefox (Chrome reads it
// against the ratio's 20). Not stretched, it is 10 in both.
static void TestStretchedRatioRowResolvesAgainstItsHeight(void)
{
    for (int stretched = 0; stretched < 2; stretched++)
    {
        muiContext* context = MakeContext();
        muiLayoutStyle outer = muiDefaultLayoutStyle();
        outer.container.alignItems = stretched ? mui_alignStretch : mui_alignStart;
        muiNodeId root = MakeNode(context, &outer);
        muiLayoutStyle ratio = muiDefaultLayoutStyle();
        ratio.sizing.width = Length(40.0f);
        ratio.sizing.aspectRatio = 2.0f;
        muiNodeId box = MakeNode(context, &ratio);
        CHECK(muiNode_InsertChild(context, root, box, s_null) == mui_success, "box");
        muiLayoutStyle half = muiDefaultLayoutStyle();
        half.sizing.height = (muiDimension){0.5f, 0.0f, mui_dimensionValue};
        muiNodeId child = MakeNode(context, &half);
        CHECK(muiNode_InsertChild(context, box, child, s_null) == mui_success, "child");
        muiLayoutStyle tall = muiDefaultLayoutStyle();
        tall.sizing.height = Length(60.0f);
        muiNodeId sibling = MakeNode(context, &tall);
        CHECK(muiNode_InsertChild(context, box, sibling, s_null) == mui_success, "sibling");
        Host host = {0};
        muiLayoutInput input = Input(&host);
        CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
        CHECK(muiNode_GetRect(context, box).height == 60.0f, "grown to its content");
        CHECK(muiNode_GetRect(context, child).height == (stretched ? 30.0f : 10.0f),
              "against its height stretched, else its ratio's");
        muiDestroyContext(context);
    }
}

static void TestEarlyStretchLaysOutAgainWhenLate(void)
{
    // A ratio column stretched through a row its single-line parent of
    // given height stretches lays out at that height from the start: its
    // empty last item stays on the first line. Wrapping the parent makes
    // the row's height known only after its lines, the same size: the
    // clean row lays out again, and the column keeps its ratio's lines.
    muiContext* context = MakeContext();
    muiLayoutStyle outer = muiDefaultLayoutStyle();
    outer.sizing.height = Length(100.0f);
    muiNodeId root = MakeNode(context, &outer);
    muiLayoutStyle plain = muiDefaultLayoutStyle();
    muiNodeId row = MakeNode(context, &plain);
    muiLayoutStyle ratio = muiDefaultLayoutStyle();
    ratio.sizing.width = Length(80.0f);
    ratio.sizing.aspectRatio = 1.5f;
    ratio.container.direction = mui_flexColumn;
    ratio.container.wrap = mui_wrapWrap;
    muiNodeId column = MakeNode(context, &ratio);
    muiLayoutStyle tall = muiDefaultLayoutStyle();
    tall.sizing.height = Length(100.0f);
    tall.item.shrink = 0.0f;
    muiNodeId first = MakeNode(context, &tall);
    muiLayoutStyle narrow = muiDefaultLayoutStyle();
    narrow.sizing.width = Length(20.0f);
    muiNodeId last = MakeNode(context, &narrow);
    CHECK(muiNode_InsertChild(context, root, row, s_null) == mui_success &&
              muiNode_InsertChild(context, row, column, s_null) == mui_success &&
              muiNode_InsertChild(context, column, first, s_null) == mui_success &&
              muiNode_InsertChild(context, column, last, s_null) == mui_success,
          "insert");
    Host host = {0};
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    muiRect rect = muiNode_GetRect(context, last);
    CHECK(rect.x == 0.0f && rect.y == 100.0f, "on the first line");
    // Laid out again from within, it stays there.
    narrow.sizing.width = Length(25.0f);
    CHECK(muiNode_SetLayoutValues(context, last, &narrow, MUI_LAYOUT_PROPERTIES) == mui_success,
          "widen the last");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout from within");
    rect = muiNode_GetRect(context, last);
    CHECK(rect.x == 0.0f && rect.y == 100.0f && rect.width == 25.0f, "still on the first line");
    outer.container.wrap = mui_wrapWrap;
    CHECK(muiNode_SetLayoutValues(context, root, &outer, MUI_LAYOUT_PROPERTIES) == mui_success,
          "wrap the parent");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout again");
    CHECK(muiNode_GetRect(context, row).height == 100.0f, "the row's size kept");
    rect = muiNode_GetRect(context, last);
    CHECK(rect.x > 0.0f && rect.y == 0.0f, "on a line of its own");
    muiDestroyContext(context);
}

static void TestBaselineItemFittedToItsLineFromItsLineLeft(void)
{
    // In a column that wraps, an item aligned by baseline is fitted to
    // its line as one aligned at its start, its line's 14 for the 10 its
    // container's width of 0 gave. Right to left its line-left edge,
    // where a column synthesizes its baseline, is not its start: it keeps
    // its width and its place (as Firefox), where Chrome widens it past
    // its line's start.
    muiContext* context = MakeContext();
    muiLayoutStyle outer = muiDefaultLayoutStyle();
    outer.sizing.width = Length(0.0f);
    outer.container.direction = mui_flexColumn;
    outer.container.wrap = mui_wrapWrap;
    muiNodeId root = MakeNode(context, &outer);
    muiLayoutStyle wide = muiDefaultLayoutStyle();
    wide.sizing.width = Length(14.0f);
    wide.sizing.height = Length(10.0f);
    muiNodeId first = MakeNode(context, &wide);
    muiLayoutStyle words = muiDefaultLayoutStyle();
    words.item.alignSelf = mui_alignBaseline;
    words.container.wrap = mui_wrapWrap;
    muiNodeId item = MakeNode(context, &words);
    CHECK(muiNode_InsertChild(context, root, first, s_null) == mui_success &&
              muiNode_InsertChild(context, root, item, s_null) == mui_success,
          "insert");
    muiLayoutStyle word = muiDefaultLayoutStyle();
    word.sizing.width = Length(10.0f);
    word.sizing.height = Length(20.0f);
    for (int i = 0; i < 3; i++)
    {
        CHECK(muiNode_InsertChild(context, item, MakeNode(context, &word), s_null) == mui_success,
              "insert a word");
    }
    Host host = {0};
    muiLayoutInput input = Input(&host);
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    muiRect rect = muiNode_GetRect(context, item);
    CHECK(rect.x == 0.0f && rect.width == 14.0f, "fitted to its line");
    outer.textDirection = mui_inlineRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, root, &outer, MUI_LAYOUT_PROPERTIES) == mui_success,
          "right to left");
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout right to left");
    rect = muiNode_GetRect(context, item);
    CHECK(rect.x == -10.0f && rect.width == 10.0f, "its width and place kept");
    muiDestroyContext(context);
}

// A fuzz finding (fuzz_api): a child far wider than its row, shrunk onto
// its minimum. At 2^38 a float steps by 32768, so its shrunk size is a
// rounding, and the clamp to the minimum moved it by more than the row's
// slack but less than its own: no pass froze it, and the loop of CSS
// Flexbox 9.7 ran forever. Each pass now freezes a child.
static void TestShrinkOntoAMinimumFarBelowTheBaseEnds(void)
{
    muiContext* context = MakeContext();
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.sizing.width = Length(200.0f);
    muiNodeId root = MakeNode(context, &row);
    muiLayoutStyle wide = muiDefaultLayoutStyle();
    wide.sizing.width = Length(0x1.9p+38f);
    wide.sizing.minWidth = Length(0x1.e001fep+5f);
    muiNodeId child = MakeNode(context, &wide);
    CHECK(muiNode_InsertChild(context, root, child, s_null) == mui_success, "child");
    const muiLayoutInput input = {184.0f, 160.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "laid out");
    muiRect rect = muiNode_GetRect(context, child);
    CHECK(isfinite(rect.width) && rect.width >= 0x1.e001fep+5f, "finite, at its minimum or more");
    muiDestroyContext(context);
}

static void TestRectOfUnknownNodeIsZero(void)
{
    muiContext* context = MakeContext();
    muiRect rect = muiNode_GetRect(context, (muiNodeId){5, 1});
    CHECK(rect.x == 0.0f && rect.width == 0.0f, "stale");
    rect = muiNode_GetRect(NULL, (muiNodeId){1, 1});
    CHECK(rect.height == 0.0f, "NULL context");
    muiDestroyContext(context);
}

int main(void)
{
    TestDefaultsAreCssInitialValues();
    TestBadStylesAreMisuse();
    TestComputeRefusesNonRootsAndBadSpace();
    TestEditsFromMeasureAreRefused();
    TestUnchangedTreeIsNotMeasuredAgain();
    TestDecidedSizesAreNotMeasured();
    TestLooseRowLabelIsMeasuredOnce();
    TestEarlyStretchLaysOutAgainWhenLate();
    TestBaselineItemFittedToItsLineFromItsLineLeft();
    TestStyleChangeRelaysTheParent();
    TestNewSpaceRelaysAnUnchangedTree();
    TestScaledLimitFollowsTheParent();
    TestShrunkTextIsNotTakenFromItsMinContentSize();
    TestTextFollowsTheSpaceBothWays();
    TestScaledChildWrapsInItsBox();
    TestScaledChildKeepsItsRootsHeight();
    TestLimitedTextWrapsAtItsLimit();
    TestDirectionChangeReachesInheritingDescendants();
    TestSafeArea();
    TestSafeAreaSizeFollowsDirection();
    TestStretchedRatioRowResolvesAgainstItsHeight();
    TestShrinkOntoAMinimumFarBelowTheBaseEnds();
    TestRectOfUnknownNodeIsZero();
    return s_failures == 0 ? 0 : 1;
}
