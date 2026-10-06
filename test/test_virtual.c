// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtualization (record mui-0007): windows from offsets, fixed and
// estimated extents, measured items, placing across and along lists, the
// content extent, and the table's contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/virtual.h"

#include <math.h>
#include <stddef.h>

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

// Sets a node's width and height; a negative one stays automatic.
static void Size(muiContext* context, muiNodeId node, float width, float height)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    muiPropertyMask mask = MUI_PROPERTY_BIT(mui_propertyShrink);
    style.item.shrink = 0.0f;
    if (width >= 0.0f)
    {
        style.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
        mask |= MUI_PROPERTY_BIT(mui_propertyWidth);
    }
    if (height >= 0.0f)
    {
        style.sizing.height = (muiDimension){0.0f, height, mui_dimensionValue};
        mask |= MUI_PROPERTY_BIT(mui_propertyHeight);
    }
    CHECK(muiNode_SetLayoutValues(context, node, &style, mask) == mui_success, "size");
}

// Makes a node scroll along an axis, its children in a column or a row,
// with padding all round and a direction.
static void Scrolls(muiContext* context, muiNodeId node, bool horizontal, float padding,
                    muiTextDirection direction)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = horizontal ? mui_scrollHorizontal : mui_scrollVertical;
    style.container.direction = horizontal ? mui_flexRow : mui_flexColumn;
    style.padding = (muiEdges){padding, padding, padding, padding};
    style.textDirection = direction;
    CHECK(muiNode_SetLayoutValues(context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                                      MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingBottom) |
                                      MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "scrolls");
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

// How many windows changed since last asked.
static int Windows(muiContext* context)
{
    int count = 0;
    muiNotification record = {0};
    while (muiNextNotification(context, &record) == mui_success)
    {
        count += record.kind == mui_notificationWindowChanged;
    }
    return count;
}

static bool WindowIs(const muiContext* context, muiNodeId list, uint32_t first, uint32_t end)
{
    uint32_t a = 0;
    uint32_t b = 0;
    return muiNode_GetVirtualWindow(context, list, &a, &b) == mui_success && a == first && b == end;
}

static float ExtentOf(const muiContext* context, muiNodeId node, bool horizontal)
{
    muiSize extent = {0};
    CHECK(muiNode_GetScrollExtent(context, node, &extent) == mui_success, "extent");
    return horizontal ? extent.width : extent.height;
}

// A root of 400 by 300 and a list in it, 200 by 100, vertical unless
// horizontal, with padding.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId list;
} Scene;

static void MakeScene(Scene* scene, bool horizontal, float padding, muiTextDirection direction,
                      const muiVirtualList* list)
{
    muiContextDef def = muiDefaultContextDef();
    *scene = (Scene){0};
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiContext* context = scene->context;
    scene->root = Node(context, s_nullNode);
    Size(context, scene->root, 400.0f, 300.0f);
    scene->list = Node(context, scene->root);
    Size(context, scene->list, 200.0f, 100.0f);
    Scrolls(context, scene->list, horizontal, padding, direction);
    CHECK(muiNode_SetVirtualList(context, scene->list, list) == mui_success, "list");
}

static muiVirtualList ListOf(uint32_t count, float extent, bool fixed, float gap, float overscan)
{
    muiVirtualList list = muiDefaultVirtualList();
    list.count = count;
    list.extent = extent;
    list.fixed = fixed;
    list.gap = gap;
    list.overscan = overscan;
    return list;
}

// Realizes items first to end as children of the list, each sized as
// given (negative for automatic).
static void Realize(const Scene* scene, uint32_t first, uint32_t end, float width, float height,
                    muiNodeId* out)
{
    for (uint32_t i = first; i < end; i++)
    {
        muiNodeId item = Node(scene->context, scene->list);
        Size(scene->context, item, width, height);
        CHECK(muiNode_SetItem(scene->context, item, i) == mui_success, "bind");
        out[i - first] = item;
    }
}

static void TestEstimated(void)
{
    Scene scene;
    muiVirtualList list = ListOf(1000, 40.0f, false, 0.0f, 0.0f);
    MakeScene(&scene, false, 0.0f, mui_textInherit, &list);
    muiContext* context = scene.context;
    uint32_t first = 0;
    uint32_t end = 0;
    CHECK(muiNode_GetVirtualWindow(context, scene.list, &first, &end) == mui_empty,
          "no window before layout");
    Layout(context, scene.root);
    CHECK(WindowIs(context, scene.list, 0, 3) && Windows(context) == 1 &&
              ExtentOf(context, scene.list, false) == 40000.0f,
          "three items, all of them in the extent");
    // Realized at 50 each: measured, placed one after another.
    muiNodeId items[3];
    Realize(&scene, 0, 3, -1.0f, 50.0f, items);
    Layout(context, scene.root);
    CHECK(muiNode_GetRect(context, items[0]).y == 0.0f &&
              muiNode_GetRect(context, items[1]).y == 50.0f &&
              muiNode_GetRect(context, items[2]).y == 100.0f &&
              muiNode_GetRect(context, items[1]).width == 200.0f,
          "measured and placed, across the list");
    CHECK(ExtentOf(context, scene.list, false) == 40030.0f && WindowIs(context, scene.list, 0, 3) &&
              Windows(context) == 0,
          "the extent follows; the window stays");
    float offset = 0.0f;
    float extent = 0.0f;
    CHECK(muiNode_GetVirtualItem(context, scene.list, 5, &offset, &extent) == mui_success &&
              offset == 230.0f && extent == 40.0f,
          "an item after them, estimated");
    // Scrolled to 500: items from 3 on start at 150 + 40 per item.
    CHECK(muiNode_SetScroll(context, scene.list, 0.0f, 500.0f) == mui_success, "scrolled");
    Layout(context, scene.root);
    CHECK(WindowIs(context, scene.list, 11, 15) && Windows(context) == 1, "the window moves");
    // Far down, beyond anything realized.
    CHECK(muiNode_SetScroll(context, scene.list, 0.0f, 39900.0f) == mui_success, "the end");
    Layout(context, scene.root);
    CHECK(WindowIs(context, scene.list, 996, 1000), "the last items");
    muiDestroyContext(context);
}

static void TestFixed(void)
{
    Scene scene;
    muiVirtualList list = ListOf(100000, 20.0f, true, 0.0f, 0.0f);
    MakeScene(&scene, false, 0.0f, mui_textInherit, &list);
    muiContext* context = scene.context;
    Layout(context, scene.root);
    CHECK(ExtentOf(context, scene.list, false) == 2000000.0f && WindowIs(context, scene.list, 0, 6),
          "a hundred thousand rows");
    CHECK(muiNode_SetScroll(context, scene.list, 0.0f, 1000000.0f) == mui_success, "halfway");
    Layout(context, scene.root);
    CHECK(WindowIs(context, scene.list, 50000, 50006), "the window there");
    // A realized row of another height is placed but not measured.
    muiNodeId row[1];
    Realize(&scene, 50000, 50001, -1.0f, 35.0f, row);
    Layout(context, scene.root);
    float offset = 0.0f;
    float extent = 0.0f;
    CHECK(muiNode_GetRect(context, row[0]).y == 1000000.0f &&
              muiNode_GetVirtualItem(context, scene.list, 50001, &offset, &extent) == mui_success &&
              offset == 1000020.0f && extent == 20.0f,
          "fixed stays fixed");
    muiDestroyContext(context);
}

static void TestGapAndOverscan(void)
{
    Scene scene;
    muiVirtualList list = ListOf(100, 40.0f, false, 10.0f, 30.0f);
    MakeScene(&scene, false, 10.0f, mui_textInherit, &list);
    muiContext* context = scene.context;
    Layout(context, scene.root);
    // The viewport from -10 to 90 in the items' space, widened to 120.
    CHECK(WindowIs(context, scene.list, 0, 3) && ExtentOf(context, scene.list, false) == 5010.0f,
          "items at 0, 50 and 100; 100 lengths and 99 gaps between paddings");
    muiNodeId items[3];
    Realize(&scene, 0, 3, -1.0f, -1.0f, items);
    Layout(context, scene.root);
    // Measured as 0 at once: items at 0, 10 and 20, inside the padding.
    CHECK(muiNode_GetRect(context, items[1]).y == 20.0f &&
              muiNode_GetRect(context, items[1]).x == 10.0f &&
              muiNode_GetRect(context, items[1]).width == 180.0f &&
              muiNode_GetRect(context, items[1]).height == 0.0f,
          "inside the padding, empty items measured as 0");
    CHECK(muiNode_SetScroll(context, scene.list, 0.0f, 300.0f) == mui_success, "scrolled");
    Layout(context, scene.root);
    // Items 0 to 2 are 0 long now: 0, 10, 20; then 30 + 50 per item; the
    // viewport from 290, widened to 260 and 420.
    CHECK(WindowIs(context, scene.list, 7, 11), "the window over measured and estimated items");
    muiDestroyContext(context);
}

static void TestHorizontal(void)
{
    Scene scene;
    muiVirtualList list = ListOf(10, 60.0f, true, 0.0f, 0.0f);
    list.axis = mui_listHorizontal;
    MakeScene(&scene, true, 0.0f, mui_textInherit, &list);
    muiContext* context = scene.context;
    Layout(context, scene.root);
    muiNodeId items[4];
    Realize(&scene, 0, 4, 60.0f, -1.0f, items);
    Layout(context, scene.root);
    CHECK(WindowIs(context, scene.list, 0, 4) && ExtentOf(context, scene.list, true) == 600.0f &&
              muiNode_GetRect(context, items[1]).x == 60.0f &&
              muiNode_GetRect(context, items[1]).height == 100.0f,
          "left to right, stretched down");
    muiDestroyContext(context);
    MakeScene(&scene, true, 0.0f, mui_textRightToLeft, &list);
    context = scene.context;
    Layout(context, scene.root);
    Realize(&scene, 0, 4, 60.0f, -1.0f, items);
    Layout(context, scene.root);
    CHECK(muiNode_GetRect(context, items[0]).x == 140.0f &&
              muiNode_GetRect(context, items[1]).x == 80.0f,
          "right to left from the right");
    muiDestroyContext(context);
}

static void TestBinding(void)
{
    Scene scene;
    muiVirtualList list = ListOf(10, 40.0f, false, 0.0f, 0.0f);
    MakeScene(&scene, false, 0.0f, mui_textInherit, &list);
    muiContext* context = scene.context;
    muiNodeId item = Node(context, scene.list);
    Size(context, item, -1.0f, 30.0f);
    muiNodeId flow = Node(context, scene.list);
    Size(context, flow, -1.0f, 30.0f);
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiNode_SetItem(context, item, 10) == mui_errorInvalid &&
              muiNode_SetItem(context, scene.root, 0) == mui_errorInvalid &&
              muiNode_SetItem(context, s_nullNode, 0) == mui_errorInvalid &&
              muiNode_SetItem(NULL, item, 0) == mui_errorInvalid &&
              muiNode_ClearItem(NULL, item) == mui_errorInvalid &&
              muiNode_ClearItem(context, s_nullNode) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 4,
          "refused");
    CHECK(muiNode_SetItem(context, item, 4) == mui_success, "bound");
    Layout(context, scene.root);
    CHECK(muiNode_GetRect(context, item).y == 160.0f && muiNode_GetRect(context, flow).y == 0.0f,
          "out of the flow, at its offset");
    CHECK(muiNode_ClearItem(context, item) == mui_success &&
              muiNode_ClearItem(context, item) == mui_success,
          "unbound, twice");
    Layout(context, scene.root);
    CHECK(muiNode_GetRect(context, item).y == 0.0f && muiNode_GetRect(context, flow).y == 30.0f,
          "back in the flow");
    // A node reusing a destroyed one's slot is not bound.
    CHECK(muiNode_SetItem(context, item, 2) == mui_success &&
              muiDestroyNode(context, item) == mui_success,
          "destroyed bound");
    muiNodeId fresh = Node(context, scene.list);
    Size(context, fresh, -1.0f, 30.0f);
    Layout(context, scene.root);
    CHECK(fresh.index1 == item.index1 && muiNode_GetRect(context, fresh).y == 30.0f,
          "a new node in the flow");
    muiDestroyContext(context);
}

static void TestEdges(void)
{
    // Four items: measuring the first updates the tree's root.
    Scene scene;
    muiVirtualList list = ListOf(4, 40.0f, false, 0.0f, 0.0f);
    MakeScene(&scene, false, 0.0f, mui_textInherit, &list);
    muiContext* context = scene.context;
    Layout(context, scene.root);
    muiNodeId items[3];
    Realize(&scene, 0, 1, -1.0f, 50.0f, items);
    Layout(context, scene.root);
    CHECK(ExtentOf(context, scene.list, false) == 170.0f, "four items, one of 50");
    // Set anew smaller: the extent shrinks; an item past the count is
    // left alone, nothing read or written past the list.
    CHECK(muiNode_SetItem(context, items[0], 3) == mui_success, "item 3");
    list.count = 2;
    CHECK(muiNode_SetVirtualList(context, scene.list, &list) == mui_success, "two");
    muiNodeId other = Node(context, scene.root);
    muiVirtualList others = ListOf(10, 40.0f, false, 0.0f, 0.0f);
    CHECK(muiNode_SetVirtualList(context, other, &others) == mui_success, "another list after");
    Layout(context, scene.root);
    float offset = 0.0f;
    float extent = 0.0f;
    CHECK(ExtentOf(context, scene.list, false) == 100.0f &&
              muiNode_GetRect(context, items[0]).y == 0.0f &&
              muiNode_GetVirtualItem(context, other, 1, &offset, &extent) == mui_success &&
              extent == 40.0f && offset == 40.0f,
          "shrunk, the stale item where layout put it, the other list untouched");
    // Cleared: back to its own content.
    CHECK(muiNode_ClearVirtualList(context, scene.list) == mui_success, "cleared");
    Layout(context, scene.root);
    CHECK(ExtentOf(context, scene.list, false) == 100.0f, "the padding box");
    // No items: an empty window, still reported.
    list.count = 0;
    (void)Windows(context);
    CHECK(muiNode_SetVirtualList(context, scene.list, &list) == mui_success, "empty");
    Layout(context, scene.root);
    CHECK(Windows(context) >= 1 && WindowIs(context, scene.list, 0, 0), "reported empty");
    muiDestroyContext(context);
}

static void TestFixedEdges(void)
{
    // Fixed with a gap and overscan: 20 long, 5 between, 30 beyond.
    Scene scene;
    muiVirtualList list = ListOf(100, 20.0f, true, 5.0f, 30.0f);
    MakeScene(&scene, false, 0.0f, mui_textInherit, &list);
    muiContext* context = scene.context;
    Layout(context, scene.root);
    float offset = 0.0f;
    float extent = 0.0f;
    CHECK(WindowIs(context, scene.list, 0, 6) && ExtentOf(context, scene.list, false) == 2495.0f &&
              muiNode_GetVirtualItem(context, scene.list, 4, &offset, &extent) == mui_success &&
              offset == 100.0f,
          "places of 25");
    // An overscan beyond everything: the whole list.
    list.overscan = 1e30f;
    CHECK(muiNode_SetVirtualList(context, scene.list, &list) == mui_success, "huge overscan");
    Layout(context, scene.root);
    CHECK(WindowIs(context, scene.list, 0, 100), "every item");
    muiDestroyContext(context);
}

static void TestLargeEstimated(void)
{
    // 1024 items, a power of two: the tree's full depth.
    Scene scene;
    muiVirtualList list = ListOf(1024, 40.0f, false, 0.0f, 0.0f);
    MakeScene(&scene, false, 0.0f, mui_textInherit, &list);
    muiContext* context = scene.context;
    Layout(context, scene.root);
    CHECK(muiNode_SetScroll(context, scene.list, 0.0f, 40860.0f) == mui_success, "the end");
    Layout(context, scene.root);
    CHECK(WindowIs(context, scene.list, 1021, 1024), "the last items");
    // Scrolled far, then laid out anew: the offset holds.
    CHECK(muiNode_SetScroll(context, scene.list, 0.0f, 20000.0f) == mui_success, "the middle");
    Size(context, scene.list, 210.0f, 100.0f);
    Layout(context, scene.root);
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_GetScroll(context, scene.list, &x, &y) == mui_success && y == 20000.0f &&
              WindowIs(context, scene.list, 500, 503),
          "not clamped to what exists");
    muiDestroyContext(context);
}

static void TestStorageBack(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.virtualItems = 100;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId a = Node(context, s_nullNode);
    muiNodeId b = Node(context, s_nullNode);
    muiVirtualList list = ListOf(60, 40.0f, false, 0.0f, 0.0f);
    CHECK(muiNode_SetVirtualList(context, a, &list) == mui_success &&
              muiDestroyNode(context, a) == mui_success &&
              muiNode_SetVirtualList(context, b, &list) == mui_success,
          "a destroyed list's items taken back, its entry not needed");
    muiDestroyContext(context);
}

static void TestContract(void)
{
    muiContextDef def = muiDefaultContextDef();
    CHECK(def.limits.virtualLists == 8 && def.limits.virtualItems == 16384, "the defaults");
    def.limits.virtualLists = 2;
    def.limits.virtualItems = 100;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId a = Node(context, s_nullNode);
    muiNodeId b = Node(context, s_nullNode);
    muiNodeId c = Node(context, s_nullNode);
    muiVirtualList list = muiDefaultVirtualList();
    CHECK(list.count == 0 && list.axis == mui_listVertical && list.extent == 40.0f && !list.fixed &&
              list.gap == 0.0f && list.overscan == 200.0f,
          "default");
    uint64_t misuse = muiGetContextMisuse(context);
    muiVirtualList bad = list;
    bad.extent = 0.0f;
    CHECK(muiNode_SetVirtualList(context, a, &bad) == mui_errorInvalid, "no extent");
    bad.extent = NAN;
    CHECK(muiNode_SetVirtualList(context, a, &bad) == mui_errorInvalid, "a NaN");
    bad = list;
    bad.gap = -1.0f;
    CHECK(muiNode_SetVirtualList(context, a, &bad) == mui_errorInvalid, "a gap");
    bad = list;
    bad.overscan = INFINITY;
    CHECK(muiNode_SetVirtualList(context, a, &bad) == mui_errorInvalid, "an overscan");
    bad = list;
    bad.axis = 2;
    CHECK(muiNode_SetVirtualList(context, a, &bad) == mui_errorInvalid, "an axis");
    CHECK(muiNode_SetVirtualList(context, a, NULL) == mui_errorInvalid &&
              muiNode_SetVirtualList(context, s_nullNode, &list) == mui_errorInvalid &&
              muiNode_SetVirtualList(NULL, a, &list) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 7,
          "counted");
    // Items: 60 and 50 do not fit 100 together; fixed lists take none.
    list.count = 60;
    CHECK(muiNode_SetVirtualList(context, a, &list) == mui_success, "60");
    list.count = 50;
    CHECK(muiNode_SetVirtualList(context, b, &list) == mui_errorCapacity, "50 more");
    list.count = 40;
    CHECK(muiNode_SetVirtualList(context, b, &list) == mui_success, "40 more");
    muiVirtualList fixed = ListOf(1000000, 10.0f, true, 0.0f, 0.0f);
    CHECK(muiNode_SetVirtualList(context, c, &fixed) == mui_errorCapacity, "lists full");
    CHECK(muiNode_ClearVirtualList(context, a) == mui_success &&
              muiNode_SetVirtualList(context, c, &fixed) == mui_success,
          "fixed takes no items");
    list.count = 60;
    CHECK(muiNode_ClearVirtualList(context, c) == mui_success &&
              muiNode_SetVirtualList(context, a, &list) == mui_success,
          "the room a cleared list left");
    // Set anew larger: it moves where it fits.
    list.count = 50;
    CHECK(muiNode_SetVirtualList(context, b, &list) == mui_errorCapacity, "b cannot grow");
    list.count = 30;
    CHECK(muiNode_SetVirtualList(context, b, &list) == mui_success, "b shrinks");
    // A destroyed list's room and entry are taken back.
    CHECK(muiDestroyNode(context, a) == mui_success, "a destroyed");
    list.count = 70;
    CHECK(muiNode_SetVirtualList(context, c, &list) == mui_errorCapacity,
          "70 does not fit around b");
    list.count = 60;
    CHECK(muiNode_SetVirtualList(context, c, &list) == mui_success, "c takes a's room");
    float offset = 0.0f;
    float extent = 0.0f;
    uint32_t first = 0;
    uint32_t end = 0;
    CHECK(muiNode_GetVirtualItem(context, c, 60, &offset, &extent) == mui_errorInvalid &&
              muiNode_GetVirtualItem(context, c, 59, &offset, &extent) == mui_success &&
              offset == 2360.0f &&
              muiNode_GetVirtualItem(context, a, 0, &offset, &extent) == mui_errorStale,
          "items read");
    CHECK(muiNode_GetVirtualWindow(context, b, &first, NULL) == mui_errorInvalid &&
              muiNode_GetVirtualWindow(NULL, b, &first, &end) == mui_errorInvalid &&
              muiNode_GetVirtualWindow(context, s_nullNode, &first, &end) == mui_errorInvalid &&
              muiNode_ClearVirtualList(NULL, b) == mui_errorInvalid &&
              muiNode_ClearVirtualList(context, s_nullNode) == mui_errorInvalid &&
              muiNode_ClearVirtualList(context, a) == mui_errorStale,
          "contract");
    muiNodeId none = Node(context, s_nullNode);
    CHECK(muiNode_GetVirtualWindow(context, none, &first, &end) == mui_empty &&
              muiNode_ClearVirtualList(context, none) == mui_success,
          "not a list");
    muiDestroyContext(context);
}

int main(void)
{
    TestEstimated();
    TestFixed();
    TestGapAndOverscan();
    TestHorizontal();
    TestBinding();
    TestEdges();
    TestFixedEdges();
    TestLargeEstimated();
    TestStorageBack();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
