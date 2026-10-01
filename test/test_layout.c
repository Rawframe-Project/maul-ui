// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The layout API around the solver: authored values, refusals, the
// measure function's contract, and work skipped when nothing changed.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

#include <math.h>

static const muiNodeId s_null = {0, 0};

// A host whose content is a fixed size, counting the measurements and
// trying an edit from inside one when asked.
typedef struct Host
{
    muiContext* context;
    muiSize content;
    int measured;
    bool tryEdit;
    muiResult editStatus;
} Host;

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)hostKey;
    (void)width;
    (void)height;
    Host* host = user;
    host->measured++;
    if (host->tryEdit)
    {
        muiLayoutStyle style = muiDefaultLayoutStyle();
        host->editStatus = muiNode_SetLayoutStyle(host->context, nodeId, &style);
    }
    return host->content;
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
        CHECK(muiNode_SetLayoutStyle(context, node, style) == mui_success, "style");
    }
    return node;
}

static muiDimension Length(float offset)
{
    return (muiDimension){0.0f, offset, mui_dimensionValue};
}

static muiLayoutInput Input(Host* host)
{
    return (muiLayoutInput){400.0f, 300.0f, Measure, host};
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
    muiLayoutStyle bad[6];
    for (int i = 0; i < 6; i++)
    {
        bad[i] = muiDefaultLayoutStyle();
    }
    bad[0].item.grow = -1.0f;
    bad[1].sizing.width = (muiDimension){NAN, 0.0f, mui_dimensionValue};
    bad[2].padding.top = -2.0f;
    bad[3].container.alignItems = mui_alignAuto;
    bad[4].container.direction = 9;
    bad[5].container.columnGap = INFINITY;
    for (int i = 0; i < 6; i++)
    {
        CHECK(muiNode_SetLayoutStyle(context, node, &bad[i]) == mui_errorInvalid, "refused");
    }
    CHECK(muiNode_SetLayoutStyle(context, node, NULL) == mui_errorInvalid, "NULL style");
    CHECK(muiGetContextMisuse(context) == 7, "each counted");
    muiLayoutStyle negativeMargin = muiDefaultLayoutStyle();
    negativeMargin.margin.start = -5.0f;
    CHECK(muiNode_SetLayoutStyle(context, node, &negativeMargin) == mui_success,
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
    CHECK(muiComputeLayout(context, child, &input) == mui_errorInvalid, "not a root");
    input.availableWidth = -1.0f;
    CHECK(muiComputeLayout(context, root, &input) == mui_errorInvalid, "negative space");
    input.availableWidth = INFINITY;
    CHECK(muiComputeLayout(context, root, &input) == mui_errorInvalid, "infinite space");
    CHECK(muiComputeLayout(context, root, NULL) == mui_errorInvalid, "NULL input");
    CHECK(muiDestroyNode(context, child) == mui_success, "destroy child");
    input = Input(&host);
    CHECK(muiComputeLayout(context, child, &input) == mui_errorStale, "stale root");
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
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(host.measured > 0, "measured");
    CHECK(host.editStatus == mui_errorInvalid, "edit refused inside measure");
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
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "first layout");
    CHECK(host.measured > 0, "measured once laid out");
    host.measured = 0;
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "second layout");
    CHECK(host.measured == 0, "a static frame measures nothing");
    muiRect rect = muiNode_GetRect(context, text);
    CHECK(rect.width == 30.0f && rect.height == 12.0f, "content size");

    host.content = (muiSize){50.0f, 12.0f};
    CHECK(muiNode_MarkContentChanged(context, text) == mui_success, "mark");
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "third layout");
    CHECK(host.measured > 0, "changed content is measured");
    CHECK(muiNode_GetRect(context, text).width == 50.0f, "new size");
    CHECK(muiNode_GetRect(context, root).width == 50.0f, "the root fits it");
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
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(muiNode_GetRect(context, second).x == 30.0f, "after the first");
    fixed.sizing.width = Length(45.0f);
    CHECK(muiNode_SetLayoutStyle(context, first, &fixed) == mui_success, "widen first");
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout again");
    CHECK(muiNode_GetRect(context, second).x == 45.0f, "the sibling moved");
    CHECK(muiNode_Detach(context, first) == mui_success, "detach");
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout once more");
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
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(muiNode_GetRect(context, child).width == 400.0f, "fills the space");
    input.availableWidth = 250.0f;
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout in less space");
    CHECK(muiNode_GetRect(context, root).width == 250.0f, "the root follows the space");
    CHECK(muiNode_GetRect(context, child).width == 250.0f, "and so does its child");
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
    TestStyleChangeRelaysTheParent();
    TestNewSpaceRelaysAnUnchangedTree();
    TestRectOfUnknownNodeIsZero();
    return s_failures == 0 ? 0 : 1;
}
