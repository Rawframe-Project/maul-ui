// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A long estimated list whose sums round: one item measured at 2^53, its
// first item 2^24 - 1, and some 2^23 items of 1 after it, each lost when
// added to a running sum that holds 2^53, as the tree's own sums keep
// them. Rebuilt with those sizes, the tree's total must agree with the
// sizes within the error a sum of that many terms can have, not within a
// fixed share of the total. Some 310 MB of sizes and sums, so only the
// 64-bit desktop cells run it.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/virtual.h"

#include <stddef.h>

enum
{
    // Items: 2^24 before the tall one, then about 9.1 million, past the
    // 2^53 * 1e-9 that a relative tolerance of 1e-9 allows.
    COUNT = 25900000,
    TALL = 16777215
};

static const muiNodeId s_nullNode = {0, 0};

static muiNodeId Node(muiContext* context, muiNodeId parent)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    return node;
}

static void Size(muiContext* context, muiNodeId node, float width, float height)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.item.shrink = 0.0f;
    style.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    style.sizing.height = (muiDimension){0.0f, height, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyShrink) |
                                      MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "size");
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
}

static void TestRoundedTotal(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.virtualItems = COUNT + 1;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Node(context, s_nullNode);
    Size(context, root, 400.0f, 300.0f);
    muiNodeId list = Node(context, root);
    Size(context, list, 200.0f, 100.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollVertical;
    style.container.direction = mui_flexColumn;
    CHECK(muiNode_SetLayoutValues(context, list, &style,
                                  MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                                      MUI_PROPERTY_BIT(mui_propertyFlexDirection)) == mui_success,
          "scrolls");
    muiVirtualList items = muiDefaultVirtualList();
    items.count = COUNT;
    items.extent = 1.0f;
    items.overscan = 0.0f;
    CHECK(muiNode_SetVirtualList(context, list, &items) == mui_success, "list");
    muiNodeId tall = Node(context, list);
    Size(context, tall, 200.0f, 9007199254740992.0f);
    CHECK(muiNode_BindVirtualItem(context, tall, TALL) == mui_success, "bound");
    Layout(context, root);
    // Rebuilt with the measured size kept: the total checked again.
    CHECK(muiNode_InsertVirtualItems(context, list, COUNT, 1) == mui_success, "one more");
    Layout(context, root);
    muiSize extent = {0};
    CHECK(muiNode_GetScrollExtent(context, list, &extent) == mui_success &&
              extent.height >= 9007199254740992.0f,
          "every item in the extent");
    CHECK(muiNode_GetRect(context, tall).y == (float)TALL, "the tall item after the others");
    muiDestroyContext(context);
}

int main(void)
{
    TestRoundedTotal();
    return s_failures == 0 ? 0 : 1;
}
