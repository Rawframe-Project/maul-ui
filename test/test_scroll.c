// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling (record mui-0007): scroll containers' automatic minimum and
// extents, offsets clamped and kept within, children painted through a
// transform that scrolling alone moves, hit testing, pointer points and
// navigation through offsets, scrolling into view, and calls outside the
// contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static const muiNodeId s_nullNode = {0, 0};

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static muiNodeId Make(muiContext* context, muiNodeId parent)
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

static void SetLayout(muiContext* context, muiNodeId node, const muiLayoutStyle* style,
                      muiPropertyMask mask)
{
    CHECK(muiNode_SetLayoutValues(context, node, style, mask) == mui_success, "layout values");
}

#define WIDTH     MUI_PROPERTY_BIT(mui_propertyWidth)
#define HEIGHT    MUI_PROPERTY_BIT(mui_propertyHeight)
#define DIRECTION MUI_PROPERTY_BIT(mui_propertyFlexDirection)
#define SCROLL    MUI_PROPERTY_BIT(mui_propertyScrollAxes)
#define PADDING                                                                                    \
    (MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |       \
     MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom))
#define BORDER                                                                                     \
    (MUI_PROPERTY_BIT(mui_propertyBorderStart) | MUI_PROPERTY_BIT(mui_propertyBorderEnd) |         \
     MUI_PROPERTY_BIT(mui_propertyBorderTop) | MUI_PROPERTY_BIT(mui_propertyBorderBottom))

// A node of a fixed size under parent.
static muiNodeId Sized(muiContext* context, muiNodeId parent, float width, float height)
{
    muiNodeId node = Make(context, parent);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.width = Length(width);
    style.sizing.height = Length(height);
    SetLayout(context, node, &style, WIDTH | HEIGHT);
    return node;
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static muiContext* MakeContext(uint32_t transforms)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.drawTransforms = transforms;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    return context;
}

// A column list: root (300 wide) holds a scroll container s (200 by 100,
// a column, scrolling vertically, padding 10, border 0) with five items
// of 100 by 50, each painting a box, the second taking focus.
typedef struct List
{
    muiContext* context;
    muiNodeId root;
    muiNodeId s;
    muiNodeId items[5];
} List;

static void MakeList(List* list, uint32_t transforms)
{
    *list = (List){.context = MakeContext(transforms)};
    muiContext* context = list->context;
    list->root = Sized(context, s_nullNode, 300.0f, 300.0f);
    list->s = Sized(context, list->root, 200.0f, 100.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.container.direction = mui_flexColumn;
    style.scrollAxes = mui_scrollVertical;
    style.padding = (muiEdges){10.0f, 10.0f, 10.0f, 10.0f};
    SetLayout(context, list->s, &style, DIRECTION | SCROLL | PADDING);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){1.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 5; i++)
    {
        list->items[i] = Sized(context, list->s, 100.0f, 50.0f);
        // Items keep their height in the column.
        muiLayoutStyle item = muiDefaultLayoutStyle();
        item.item.shrink = 0.0f;
        SetLayout(context, list->items[i], &item, MUI_PROPERTY_BIT(mui_propertyShrink));
        CHECK(muiNode_SetVisualValues(context, list->items[i], &visual,
                                      MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
              "background");
        muiInteractionStyle values = muiDefaultInteractionStyle();
        values.focusMode = mui_focusAll;
        CHECK(muiNode_SetInteractionValues(context, list->items[i], &values,
                                           MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
              "focus mode");
    }
    Layout(context, list->root);
}

static void TestExtent(void)
{
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    muiSize extent = {0};
    float x = -1.0f;
    float y = -1.0f;
    // Five items of 50 and padding 10 at each end.
    CHECK(muiNode_GetScrollExtent(context, list.s, &extent) == mui_success &&
              extent.width == 200.0f && extent.height == 270.0f &&
              muiNode_GetScroll(context, list.s, &x, &y) == mui_success && x == 0.0f && y == 0.0f,
          "the extent");
    CHECK(muiNode_SetScroll(context, list.s, 50.0f, 40.0f) == mui_success &&
              muiNode_GetScroll(context, list.s, &x, &y) == mui_success && x == 0.0f && y == 40.0f,
          "x does not scroll");
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 500.0f) == mui_success &&
              muiNode_GetScroll(context, list.s, &x, &y) == mui_success && y == 170.0f &&
              muiNode_SetScroll(context, list.s, 0.0f, -5.0f) == mui_success &&
              muiNode_GetScroll(context, list.s, &x, &y) == mui_success && y == 0.0f,
          "clamped to 0 and the extent less the padding box");
    // Taller, the container leaves less to scroll: layout brings it back.
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 170.0f) == mui_success, "to the end");
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.height = Length(200.0f);
    SetLayout(context, list.s, &style, HEIGHT);
    Layout(context, list.root);
    CHECK(muiNode_GetScroll(context, list.s, &x, &y) == mui_success && y == 70.0f,
          "kept within a shorter range");
    // A node that does not scroll has no extent.
    CHECK(muiNode_GetScrollExtent(context, list.items[0], &extent) == mui_success &&
              extent.width == 0.0f && extent.height == 0.0f &&
              muiNode_SetScroll(context, list.items[0], 5.0f, 5.0f) == mui_success &&
              muiNode_GetScroll(context, list.items[0], &x, &y) == mui_success && x == 0.0f &&
              y == 0.0f,
          "a node that does not scroll");
    muiDestroyContext(context);
}

static void TestMinimum(void)
{
    // A column of 200 holds a container whose content is 500 tall: as a
    // scroll container it shrinks to 200, else it stays 500.
    muiContext* context = MakeContext(64);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 200.0f);
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.container.direction = mui_flexColumn;
    SetLayout(context, root, &column, DIRECTION);
    muiNodeId s = Make(context, root);
    SetLayout(context, s, &column, DIRECTION);
    muiNodeId tall = Sized(context, s, 100.0f, 500.0f);
    muiLayoutStyle keep = muiDefaultLayoutStyle();
    keep.item.shrink = 0.0f;
    SetLayout(context, tall, &keep, MUI_PROPERTY_BIT(mui_propertyShrink));
    Layout(context, root);
    CHECK(muiNode_GetRect(context, s).height == 500.0f, "content keeps it tall");
    muiLayoutStyle scroll = muiDefaultLayoutStyle();
    scroll.scrollAxes = mui_scrollVertical;
    SetLayout(context, s, &scroll, SCROLL);
    Layout(context, root);
    muiSize extent = {0};
    CHECK(muiNode_GetRect(context, s).height == 200.0f &&
              muiNode_GetScrollExtent(context, s, &extent) == mui_success &&
              extent.height == 500.0f,
          "a scroll container shrinks, its extent kept");
    muiDestroyContext(context);
}

static void TestRightToLeft(void)
{
    // A row scrolling horizontally, right to left: items run leftward and
    // the extent is measured from the start, on the right.
    muiContext* context = MakeContext(64);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiNodeId s = Sized(context, root, 100.0f, 50.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollHorizontal;
    style.textDirection = mui_textRightToLeft;
    SetLayout(context, s, &style, SCROLL | MUI_PROPERTY_BIT(mui_propertyTextDirection));
    muiNodeId a = Sized(context, s, 80.0f, 50.0f);
    muiNodeId b = Sized(context, s, 80.0f, 50.0f);
    muiLayoutStyle keep = muiDefaultLayoutStyle();
    keep.item.shrink = 0.0f;
    SetLayout(context, a, &keep, MUI_PROPERTY_BIT(mui_propertyShrink));
    SetLayout(context, b, &keep, MUI_PROPERTY_BIT(mui_propertyShrink));
    Layout(context, root);
    muiSize extent = {0};
    CHECK(muiNode_GetScrollExtent(context, s, &extent) == mui_success && extent.width == 160.0f &&
              muiNode_GetRect(context, b).x == -60.0f,
          "measured from the right");
    float x = 1.0f;
    float y = 1.0f;
    CHECK(muiNode_SetScroll(context, s, -5.0f, 0.0f) == mui_success &&
              muiNode_GetScroll(context, s, &x, &y) == mui_success && x == 0.0f,
          "clamped at 0");
    // Scrolled 60, b's start shows at the left edge: hit at 5, 25 is b's 5.
    CHECK(muiNode_SetScroll(context, s, 60.0f, 0.0f) == mui_success, "scrolled");
    muiHit hit = {0};
    CHECK(muiHitTest(context, root, 5.0f, 25.0f, &hit) == mui_success && Same(hit.node, b) &&
              hit.x == 5.0f,
          "hit leftward");
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transformCount == 2 &&
              drawn.transforms[1].e == 60.0f && drawn.transforms[1].f == 0.0f,
          "a transform rightward");
    (void)a;
    muiDestroyContext(context);
}

// Whether every command of the list's items goes through transform.
static bool ItemsThrough(const muiDrawList* list, uint32_t transform)
{
    bool through = list->commandCount == 5;
    for (uint32_t i = 0; through && i < list->commandCount; i++)
    {
        through = list->commands[i].transform == transform;
    }
    return through;
}

static void TestPainting(void)
{
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    const muiDrawInput input = {1, 2.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, list.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transformCount == 2 &&
              ItemsThrough(&drawn, 1) && drawn.transforms[1].f == 0.0f &&
              drawn.transforms[1].a == 1.0f && drawn.transforms[1].d == 1.0f,
          "items through the container's transform");
    // The container clips at its padding box (no border: its border box,
    // the padding scrolling under it), through the identity.
    CHECK(drawn.clipCount == 2 && drawn.clips[1].rect.x == 0.0f && drawn.clips[1].rect.y == 0.0f &&
              drawn.clips[1].rect.width == 200.0f && drawn.clips[1].rect.height == 100.0f &&
              drawn.clips[1].transform == 0 && drawn.commands[0].clip == 1 &&
              drawn.commands[0].box.rect.y == 10.0f,
          "the padding box clip; items unscrolled");
    // Scrolling alone: a new list, the same commands, the transform moved
    // and rounded to device pixels (10.3 at scale 2 is 10.5).
    muiDrawCommand kept[5];
    memcpy(kept, drawn.commands, sizeof kept);
    uint64_t generation = drawn.header.generation;
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 10.3f) == mui_success &&
              muiBuildDrawList(context, list.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success &&
              drawn.header.generation == generation + 1 &&
              memcmp(kept, drawn.commands, sizeof kept) == 0 && drawn.transforms[1].f == -10.5f,
          "scrolling moves the transform alone");
    CHECK(muiBuildDrawList(context, list.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success &&
              drawn.header.generation == generation + 1,
          "then nothing to do");
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 10.3f) == mui_success &&
              muiBuildDrawList(context, list.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success &&
              drawn.header.generation == generation + 1,
          "the same offset: nothing to do");
    CHECK(muiNode_ScrollIntoView(context, list.items[4]) == mui_success &&
              muiBuildDrawList(context, list.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success &&
              drawn.header.generation == generation + 2 && drawn.transforms[1].f == -160.0f,
          "into view, drawn");
    // Repainting an item copies the rest, their transform renumbered.
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){0.0f, 1.0f, 0.0f, 1.0f};
    CHECK(muiNode_SetVisualValues(context, list.items[3], &visual,
                                  MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "repaint one");
    Layout(context, list.root);
    CHECK(muiBuildDrawList(context, list.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && ItemsThrough(&drawn, 1) &&
              drawn.transforms[1].f == -160.0f && drawn.transformCount == 2,
          "copied and repainted alike");
    muiDestroyContext(context);
}

static void TestNested(void)
{
    // An outer vertical scroller holds an inner horizontal one holding a
    // box: its transform follows the outer's.
    muiContext* context = MakeContext(64);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiNodeId outer = Sized(context, root, 200.0f, 100.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollVertical;
    style.container.direction = mui_flexColumn;
    SetLayout(context, outer, &style, SCROLL | DIRECTION);
    muiNodeId inner = Sized(context, outer, 100.0f, 300.0f);
    style.scrollAxes = mui_scrollHorizontal;
    style.container.direction = mui_flexRow;
    SetLayout(context, inner, &style, SCROLL | DIRECTION);
    muiLayoutStyle keep = muiDefaultLayoutStyle();
    keep.item.shrink = 0.0f;
    SetLayout(context, inner, &keep, MUI_PROPERTY_BIT(mui_propertyShrink));
    muiNodeId box = Sized(context, inner, 400.0f, 100.0f);
    SetLayout(context, box, &keep, MUI_PROPERTY_BIT(mui_propertyShrink));
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(muiNode_SetVisualValues(context, box, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "background");
    Layout(context, root);
    CHECK(muiNode_SetScroll(context, outer, 0.0f, 30.0f) == mui_success &&
              muiNode_SetScroll(context, inner, 50.0f, 0.0f) == mui_success,
          "both scrolled");
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transformCount == 3 &&
              drawn.commandCount == 1 && drawn.commands[0].transform == 2 &&
              drawn.transforms[2].e == -50.0f && drawn.transforms[2].f == -30.0f &&
              drawn.transforms[1].e == 0.0f && drawn.transforms[1].f == -30.0f &&
              drawn.clips[2].transform == 1,
          "composed");
    // A hit at 10, 10 lands on the box at 60, 40.
    muiHit hit = {0};
    CHECK(muiHitTest(context, root, 10.0f, 10.0f, &hit) == mui_success && Same(hit.node, box) &&
              hit.x == 60.0f && hit.y == 40.0f,
          "hit through both");
    // Too few transforms: the list does not fit.
    muiDestroyContext(context);
    context = MakeContext(1);
    root = Sized(context, s_nullNode, 300.0f, 300.0f);
    outer = Sized(context, root, 200.0f, 100.0f);
    style.scrollAxes = mui_scrollVertical;
    SetLayout(context, outer, &style, SCROLL);
    inner = Sized(context, outer, 100.0f, 300.0f);
    SetLayout(context, inner, &style, SCROLL);
    Layout(context, root);
    CHECK(muiBuildDrawList(context, root, &input) == mui_errorCapacity, "a transform too many");
    muiDestroyContext(context);
}

static void TestHits(void)
{
    // Scrolled 60, items i span 50 i - 50 to 50 i on the surface: at
    // 50, 25 item 1 shows at its 40, 25; beside the items, the container.
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 60.0f) == mui_success, "scrolled");
    muiHit hit = {0};
    CHECK(muiHitTest(context, list.root, 50.0f, 25.0f, &hit) == mui_success &&
              Same(hit.node, list.items[1]) && hit.x == 40.0f && hit.y == 25.0f,
          "a scrolled item");
    CHECK(muiHitTest(context, list.root, 150.0f, 25.0f, &hit) == mui_success &&
              Same(hit.node, list.s) && hit.y == 25.0f,
          "beside the items: the container");
    CHECK(muiHitTest(context, list.root, 50.0f, 150.0f, &hit) == mui_success &&
              Same(hit.node, list.root),
          "below the container: item 3 is clipped there");
    // A press there records the item's point.
    const muiPointerEvent press = {0, 1, mui_pointerMouse, mui_pointerPress, 0, 1, 50.0f, 25.0f, 0};
    muiPointerRecord record = {0};
    CHECK(muiPointerInput(context, list.root, &press) == mui_success &&
              muiNextPointerRecord(context, &record) == mui_success &&
              Same(record.node, list.items[1]) && record.x == 40.0f && record.y == 25.0f,
          "a pointer record");
    // A captured move: the point in item 0, through the offset.
    CHECK(muiPointer_SetCapture(context, 1, list.items[0]) == mui_success, "captured");
    const muiPointerEvent move = {1, 1, mui_pointerMouse, mui_pointerMove, 0, 1, 50.0f, 25.0f, 0};
    CHECK(muiPointerInput(context, list.root, &move) == mui_success &&
              muiNextPointerRecord(context, &record) == mui_success &&
              record.kind == mui_pointerRecordMove && Same(record.node, list.items[0]) &&
              record.x == 40.0f && record.y == 75.0f,
          "a captured move");
    muiDestroyContext(context);
}

// The list of TestHits scaled twice about its top left (record mui-0005):
// every point on the surface is twice the one TestHits uses.
static void TestScaled(void)
{
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.scale = (muiLocalScale){2.0f, 2.0f, 0.0f, 0.0f};
    CHECK(muiNode_SetVisualValues(context, list.s, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyScaleX) |
                                      MUI_PROPERTY_BIT(mui_propertyScaleY) |
                                      MUI_PROPERTY_BIT(mui_propertyScaleOriginX) |
                                      MUI_PROPERTY_BIT(mui_propertyScaleOriginY)) == mui_success &&
              muiNode_SetScroll(context, list.s, 0.0f, 60.0f) == mui_success,
          "scaled and scrolled");
    Layout(context, list.root);
    CHECK(muiNode_GetRect(context, list.s).width == 200.0f, "layout keeps the laid-out box");
    muiHit hit = {0};
    CHECK(muiHitTest(context, list.root, 100.0f, 50.0f, &hit) == mui_success &&
              Same(hit.node, list.items[1]) && hit.x == 40.0f && hit.y == 25.0f,
          "a scaled, scrolled item");
    CHECK(muiHitTest(context, list.root, 250.0f, 150.0f, &hit) == mui_success &&
              Same(hit.node, list.s) && hit.x == 125.0f && hit.y == 75.0f,
          "outside its laid-out box, inside its scaled one: the container");
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_MapToRoot(context, list.items[1], 40.0f, 25.0f, &x, &y) == mui_success &&
              x == 100.0f && y == 50.0f,
          "mapped through the scale");
    const muiPointerEvent press = {0,     1, mui_pointerMouse, mui_pointerPress, 0, 1, 100.0f,
                                   50.0f, 0};
    muiPointerRecord record = {0};
    CHECK(muiPointerInput(context, list.root, &press) == mui_success &&
              muiNextPointerRecord(context, &record) == mui_success &&
              Same(record.node, list.items[1]) && record.x == 40.0f && record.y == 25.0f,
          "a pointer record");
    CHECK(muiPointer_SetCapture(context, 1, list.items[0]) == mui_success, "captured");
    const muiPointerEvent move = {1, 1, mui_pointerMouse, mui_pointerMove, 0, 1, 100.0f, 50.0f, 0};
    CHECK(muiPointerInput(context, list.root, &move) == mui_success &&
              muiNextPointerRecord(context, &record) == mui_success &&
              record.kind == mui_pointerRecordMove && Same(record.node, list.items[0]) &&
              record.x == 40.0f && record.y == 75.0f,
          "a captured move, its point divided by the scale");
    muiDestroyContext(context);
}

static void TestMapToRoot(void)
{
    // Scrolled 60, the points TestHits finds at 50, 25 map back to it:
    // item 1's 40, 25 and item 0's 40, 75.
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 60.0f) == mui_success, "scrolled");
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_MapToRoot(context, list.items[1], 40.0f, 25.0f, &x, &y) == mui_success &&
              x == 50.0f && y == 25.0f,
          "item 1's point, through the offset");
    CHECK(muiNode_MapToRoot(context, list.items[0], 40.0f, 75.0f, &x, &y) == mui_success &&
              x == 50.0f && y == 25.0f,
          "item 0's point, through the offset");
    CHECK(muiNode_MapToRoot(context, list.s, 5.0f, 5.0f, &x, &y) == mui_success && x == 5.0f &&
              y == 5.0f,
          "the container's own point: its scroll moves its children only");
    // The container's content box: inside its padding of 10.
    muiRect content = muiNode_GetContentRect(context, list.s);
    CHECK(content.x == 10.0f && content.y == 10.0f && content.width == 180.0f &&
              content.height == 80.0f,
          "the content box inside the padding");
    x = 7.0f;
    CHECK(muiNode_MapToRoot(context, list.items[0], NAN, 0.0f, &x, &y) == mui_errorInvalid &&
              x == 7.0f,
          "a point not finite refused, the outputs kept");
    CHECK(muiNode_MapToRoot(context, list.items[0], 0.0f, 0.0f, NULL, &y) == mui_errorInvalid,
          "a NULL output refused");
    muiNodeId gone = list.items[4];
    CHECK(muiDestroyNode(context, gone) == mui_success &&
              muiNode_MapToRoot(context, gone, 0.0f, 0.0f, &x, &y) == mui_errorStale,
          "a node that is gone");
    content = muiNode_GetContentRect(context, gone);
    CHECK(content.width == 0.0f && content.height == 0.0f, "a node that is gone has none");
    muiDestroyContext(context);

    // Right to left, the start border of 20 on the right: the content
    // box is from 0 to 80.
    context = MakeContext(64);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiNodeId s = Sized(context, root, 100.0f, 50.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.textDirection = mui_textRightToLeft;
    style.border = (muiEdges){20.0f, 0.0f, 0.0f, 0.0f};
    SetLayout(context, s, &style, BORDER | MUI_PROPERTY_BIT(mui_propertyTextDirection));
    Layout(context, root);
    content = muiNode_GetContentRect(context, s);
    CHECK(content.x == 0.0f && content.y == 0.0f && content.width == 80.0f &&
              content.height == 50.0f,
          "right to left, the start border on the right");
    muiDestroyContext(context);
}

static float ScrollY(const muiContext* context, muiNodeId node)
{
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_GetScroll(context, node, &x, &y) == mui_success, "read");
    return y;
}

static void TestIntoView(void)
{
    // The scrollport is the padding box, 0 to 100; item i spans 10 + 50 i
    // to 60 + 50 i; the offset goes to 170.
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    CHECK(muiNode_ScrollIntoView(context, list.items[0]) == mui_success &&
              ScrollY(context, list.s) == 0.0f,
          "inside: stays");
    CHECK(muiNode_ScrollIntoView(context, list.items[3]) == mui_success &&
              ScrollY(context, list.s) == 110.0f,
          "below: its end at the end");
    CHECK(muiNode_ScrollIntoView(context, list.items[1]) == mui_success &&
              ScrollY(context, list.s) == 60.0f,
          "above: its start at the start");
    // Items taller than the scrollport (30 high): past the end, align the
    // start; past the start, the end; past both, stay.
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.height = Length(30.0f);
    SetLayout(context, list.s, &style, HEIGHT);
    Layout(context, list.root);
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 0.0f) == mui_success &&
              muiNode_ScrollIntoView(context, list.items[1]) == mui_success &&
              ScrollY(context, list.s) == 60.0f,
          "larger, past the end: its start");
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 100.0f) == mui_success &&
              muiNode_ScrollIntoView(context, list.items[1]) == mui_success &&
              ScrollY(context, list.s) == 80.0f,
          "larger, past the start: its end");
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 70.0f) == mui_success &&
              muiNode_ScrollIntoView(context, list.items[1]) == mui_success &&
              ScrollY(context, list.s) == 70.0f,
          "larger, past both: stays");
    muiDestroyContext(context);
    // Navigation reveals what it focuses.
    MakeList(&list, 64);
    context = list.context;
    CHECK(muiFocus_Set(context, 0, list.items[1], mui_focusByCode) == mui_success &&
              muiFocus_MoveToward(context, list.root, 0, mui_directionDown) == mui_success &&
              Same(muiFocus_Get(context, 0), list.items[2]) && ScrollY(context, list.s) == 60.0f,
          "moved down and into view");
    CHECK(muiFocus_Move(context, list.root, 0, false) == mui_success &&
              Same(muiFocus_Get(context, 0), list.items[3]) && ScrollY(context, list.s) == 110.0f,
          "Tab into view");
    // Code focus does not scroll.
    CHECK(muiFocus_Set(context, 0, list.items[0], mui_focusByCode) == mui_success &&
              ScrollY(context, list.s) == 110.0f,
          "code focus stays");
    CHECK(muiFocus_MoveToward(context, list.root, 0, mui_directionUp) == mui_empty,
          "nothing above the first");
    muiDestroyContext(context);
}

static void TestLayer(void)
{
    // A layer inside the scrolled content moves with it.
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.layer = mui_layerActivation;
    CHECK(muiNode_SetInteractionValues(context, list.items[2], &values,
                                       MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "a layer");
    Layout(context, list.root);
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 40.0f) == mui_success, "scrolled");
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, list.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.commandCount == 5 &&
              drawn.commands[4].transform == 1 && drawn.commands[4].clip == 0 &&
              drawn.commands[4].box.rect.y == 110.0f,
          "through the transform, outside the clip");
    muiHit hit = {0};
    CHECK(muiHitTest(context, list.root, 50.0f, 75.0f, &hit) == mui_success &&
              Same(hit.node, list.items[2]) && hit.y == 5.0f,
          "hit where it shows");
    muiDestroyContext(context);
}

static void TestContract(void)
{
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    float x = 0.0f;
    float y = 0.0f;
    muiSize extent = {0};
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiNode_SetScroll(NULL, list.s, 0.0f, 0.0f) == mui_errorInvalid &&
              muiNode_SetScroll(context, s_nullNode, 0.0f, 0.0f) == mui_errorInvalid &&
              muiNode_SetScroll(context, list.s, NAN, 0.0f) == mui_errorInvalid &&
              muiNode_SetScroll(context, list.s, 0.0f, INFINITY) == mui_errorInvalid &&
              muiNode_ScrollIntoView(NULL, list.s) == mui_errorInvalid &&
              muiNode_ScrollIntoView(context, s_nullNode) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 4,
          "edits outside the contract");
    CHECK(muiNode_GetScroll(NULL, list.s, &x, &y) == mui_errorInvalid &&
              muiNode_GetScroll(context, list.s, NULL, &y) == mui_errorInvalid &&
              muiNode_GetScroll(context, list.s, &x, NULL) == mui_errorInvalid &&
              muiNode_GetScroll(context, s_nullNode, &x, &y) == mui_errorInvalid &&
              muiNode_GetScrollExtent(NULL, list.s, &extent) == mui_errorInvalid &&
              muiNode_GetScrollExtent(context, list.s, NULL) == mui_errorInvalid &&
              muiNode_GetScrollExtent(context, s_nullNode, &extent) == mui_errorInvalid,
          "reads outside the contract");
    muiNodeId gone = list.items[4];
    CHECK(muiDestroyNode(context, gone) == mui_success &&
              muiNode_SetScroll(context, gone, 0.0f, 0.0f) == mui_errorStale &&
              muiNode_GetScroll(context, gone, &x, &y) == mui_errorStale &&
              muiNode_GetScrollExtent(context, gone, &extent) == mui_errorStale &&
              muiNode_ScrollIntoView(context, gone) == mui_errorStale,
          "a node gone");
    muiDestroyContext(context);
}

static void Paint(muiContext* context, muiNodeId node)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(muiNode_SetVisualValues(context, node, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "background");
}

static void Keep(muiContext* context, muiNodeId node)
{
    muiLayoutStyle keep = muiDefaultLayoutStyle();
    keep.item.shrink = 0.0f;
    SetLayout(context, node, &keep, MUI_PROPERTY_BIT(mui_propertyShrink));
}

static void Scrolls(muiContext* context, muiNodeId node, muiScrollAxes axes)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = axes;
    SetLayout(context, node, &style, SCROLL);
}

static bool ExtentIs(const muiContext* context, muiNodeId node, float width, float height)
{
    muiSize extent = {0};
    return muiNode_GetScrollExtent(context, node, &extent) == mui_success &&
           extent.width == width && extent.height == height;
}

static void TestExtentEdges(void)
{
    muiContext* context = MakeContext(64);
    muiNodeId root = Sized(context, s_nullNode, 600.0f, 600.0f);
    // Short content: the padding box, the end padding counted once.
    muiNodeId shortList = Sized(context, root, 200.0f, 100.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollBoth;
    style.padding = (muiEdges){10.0f, 10.0f, 10.0f, 10.0f};
    SetLayout(context, shortList, &style, SCROLL | PADDING);
    (void)Sized(context, shortList, 50.0f, 20.0f);
    // Margins reach past an item's border box.
    muiNodeId margined = Sized(context, root, 200.0f, 100.0f);
    Scrolls(context, margined, mui_scrollBoth);
    muiNodeId item = Sized(context, margined, 300.0f, 150.0f);
    Keep(context, item);
    muiLayoutStyle margin = muiDefaultLayoutStyle();
    margin.margin = (muiEdges){0.0f, 40.0f, 0.0f, 30.0f};
    SetLayout(context, item, &margin,
              MUI_PROPERTY_BIT(mui_propertyMarginEnd) | MUI_PROPERTY_BIT(mui_propertyMarginBottom));
    // An absolute child's margins do not.
    muiNodeId placed = Sized(context, root, 100.0f, 100.0f);
    Scrolls(context, placed, mui_scrollVertical);
    muiNodeId floating = Sized(context, placed, 150.0f, 250.0f);
    muiLayoutStyle absolute = muiDefaultLayoutStyle();
    absolute.placement.position = mui_positionAbsolute;
    absolute.placement.inset.start = Length(0.0f);
    absolute.placement.inset.top = Length(0.0f);
    absolute.margin = (muiEdges){0.0f, 0.0f, 0.0f, 30.0f};
    SetLayout(context, floating, &absolute,
              MUI_PROPERTY_BIT(mui_propertyPosition) | MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                  MUI_PROPERTY_BIT(mui_propertyInsetTop) |
                  MUI_PROPERTY_BIT(mui_propertyMarginBottom));
    Layout(context, root);
    CHECK(ExtentIs(context, shortList, 200.0f, 100.0f), "short content: the padding box");
    CHECK(ExtentIs(context, margined, 340.0f, 180.0f), "margins");
    CHECK(ExtentIs(context, placed, 150.0f, 250.0f), "an absolute child's border box");
    float x = 1.0f;
    float y = 1.0f;
    CHECK(muiNode_SetScroll(context, placed, 30.0f, 0.0f) == mui_success &&
              muiNode_GetScroll(context, placed, &x, &y) == mui_success && x == 0.0f,
          "wider, but not scrolling across");
    // The last margin gone, the offset comes back within, and the list
    // follows.
    CHECK(muiNode_SetScroll(context, margined, 0.0f, 80.0f) == mui_success, "to the end");
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    CHECK(muiBuildDrawList(context, root, &input) == mui_success, "drawn");
    margin.margin.bottom = 0.0f;
    SetLayout(context, item, &margin, MUI_PROPERTY_BIT(mui_propertyMarginBottom));
    Layout(context, root);
    muiDrawList drawn;
    CHECK(muiNode_GetScroll(context, margined, &x, &y) == mui_success && y == 50.0f &&
              muiBuildDrawList(context, root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transformCount == 4 &&
              drawn.transforms[2].f == -50.0f,
          "clamped and drawn");
    // A node that stops scrolling drops its offset and extent; scrolling
    // again starts at 0.
    Scrolls(context, margined, mui_scrollNone);
    Layout(context, root);
    CHECK(muiNode_GetScroll(context, margined, &x, &y) == mui_success && x == 0.0f && y == 0.0f &&
              ExtentIs(context, margined, 0.0f, 0.0f),
          "stopped");
    CHECK(muiBuildDrawList(context, root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transformCount == 3,
          "its transform gone");
    Scrolls(context, margined, mui_scrollBoth);
    Layout(context, root);
    CHECK(muiNode_GetScroll(context, margined, &x, &y) == mui_success && x == 0.0f && y == 0.0f &&
              ExtentIs(context, margined, 340.0f, 150.0f),
          "again from 0");
    muiDestroyContext(context);
}

static void TestRightToLeftBorders(void)
{
    // A row of three 40 wide, right to left, its start border 20 on the
    // right: the padding box is 0 to 80; at offset 0, a is at 40, b at
    // 0 and c at -40.
    muiContext* context = MakeContext(64);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiNodeId s = Sized(context, root, 100.0f, 50.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollHorizontal;
    style.textDirection = mui_textRightToLeft;
    style.border = (muiEdges){20.0f, 0.0f, 0.0f, 0.0f};
    SetLayout(context, s, &style, SCROLL | BORDER | MUI_PROPERTY_BIT(mui_propertyTextDirection));
    muiNodeId items[3];
    for (int i = 0; i < 3; i++)
    {
        items[i] = Sized(context, s, 40.0f, 50.0f);
        Keep(context, items[i]);
    }
    Layout(context, root);
    float x = 0.0f;
    float y = 0.0f;
    muiHit hit = {0};
    CHECK(muiHitTest(context, root, 5.0f, 25.0f, &hit) == mui_success && Same(hit.node, items[1]) &&
              hit.x == 5.0f,
          "the padding box on the left");
    CHECK(muiHitTest(context, root, 90.0f, 25.0f, &hit) == mui_success && Same(hit.node, s),
          "the start border on the right");
    CHECK(muiNode_ScrollIntoView(context, items[1]) == mui_success &&
              muiNode_GetScroll(context, s, &x, &y) == mui_success && x == 0.0f,
          "b in view: stays");
    CHECK(muiNode_ScrollIntoView(context, items[2]) == mui_success &&
              muiNode_GetScroll(context, s, &x, &y) == mui_success && x == 40.0f,
          "c into view: leftward");
    CHECK(muiNode_ScrollIntoView(context, items[0]) == mui_success &&
              muiNode_GetScroll(context, s, &x, &y) == mui_success && x == 0.0f,
          "a into view: back");
    muiDestroyContext(context);
}

// A column: a plain node p, then an outer vertical scroller holding an
// inner horizontal one holding a painted box.
typedef struct Nest
{
    muiContext* context;
    muiNodeId root;
    muiNodeId p;
    muiNodeId outer;
    muiNodeId inner;
} Nest;

static void MakeNest(Nest* nest, uint32_t transforms)
{
    *nest = (Nest){.context = MakeContext(transforms)};
    muiContext* context = nest->context;
    nest->root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.container.direction = mui_flexColumn;
    SetLayout(context, nest->root, &column, DIRECTION);
    nest->p = Sized(context, nest->root, 100.0f, 20.0f);
    Paint(context, nest->p);
    nest->outer = Sized(context, nest->root, 200.0f, 100.0f);
    Scrolls(context, nest->outer, mui_scrollVertical);
    nest->inner = Sized(context, nest->outer, 100.0f, 300.0f);
    Scrolls(context, nest->inner, mui_scrollHorizontal);
    Keep(context, nest->inner);
    muiNodeId box = Sized(context, nest->inner, 400.0f, 100.0f);
    Keep(context, box);
    Paint(context, box);
    Layout(context, nest->root);
    CHECK(muiNode_SetScroll(context, nest->outer, 0.0f, 30.0f) == mui_success &&
              muiNode_SetScroll(context, nest->inner, 50.0f, 0.0f) == mui_success,
          "scrolled");
}

static void TestCopiedNest(void)
{
    // p turns into a scroll container: the nest is copied, its transforms
    // one on, the inner one still after the outer.
    Nest nest;
    MakeNest(&nest, 64);
    muiContext* context = nest.context;
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, nest.root, &input) == mui_success, "first");
    Scrolls(context, nest.p, mui_scrollVertical);
    Layout(context, nest.root);
    CHECK(muiBuildDrawList(context, nest.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transformCount == 4 &&
              drawn.transforms[1].f == 0.0f && drawn.transforms[2].f == -30.0f &&
              drawn.transforms[3].e == -50.0f && drawn.transforms[3].f == -30.0f &&
              drawn.commands[drawn.commandCount - 1].transform == 3,
          "renumbered");
    muiDestroyContext(context);
    // With room for two, the copy does not fit.
    MakeNest(&nest, 2);
    context = nest.context;
    CHECK(muiBuildDrawList(context, nest.root, &input) == mui_success, "two fit");
    Scrolls(context, nest.p, mui_scrollVertical);
    Layout(context, nest.root);
    CHECK(muiBuildDrawList(context, nest.root, &input) == mui_errorCapacity, "three do not");
    muiDestroyContext(context);
}

static void TestOverlay(void)
{
    // An absolute node over a scrolled list is hit after the walk leaves
    // the list, from the list's parent's origin.
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    muiNodeId over = Sized(context, list.root, 50.0f, 50.0f);
    muiLayoutStyle absolute = muiDefaultLayoutStyle();
    absolute.placement.position = mui_positionAbsolute;
    absolute.placement.inset.start = Length(0.0f);
    absolute.placement.inset.top = Length(0.0f);
    SetLayout(context, over, &absolute,
              MUI_PROPERTY_BIT(mui_propertyPosition) | MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                  MUI_PROPERTY_BIT(mui_propertyInsetTop));
    Layout(context, list.root);
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 60.0f) == mui_success, "scrolled");
    muiHit hit = {0};
    CHECK(muiHitTest(context, list.root, 25.0f, 25.0f, &hit) == mui_success &&
              Same(hit.node, over) && hit.y == 25.0f,
          "the overlay");
    muiDestroyContext(context);
}

static void TestBeside(void)
{
    // Beside the list, a column with b1 at 50 to 100 and b2 at 160 to 210.
    // Scrolled 110, item 3 shows at 50 to 100: right of it is b1.
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    muiNodeId side = Sized(context, list.root, 100.0f, 300.0f);
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.container.direction = mui_flexColumn;
    SetLayout(context, side, &column, DIRECTION);
    muiNodeId b[2];
    const float gaps[2] = {50.0f, 60.0f};
    for (int i = 0; i < 2; i++)
    {
        (void)Sized(context, side, 10.0f, gaps[i]);
        b[i] = Sized(context, side, 50.0f, 50.0f);
        muiInteractionStyle values = muiDefaultInteractionStyle();
        values.focusMode = mui_focusAll;
        CHECK(muiNode_SetInteractionValues(context, b[i], &values,
                                           MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
              "focus mode");
    }
    Layout(context, list.root);
    CHECK(muiNode_SetScroll(context, list.s, 0.0f, 110.0f) == mui_success &&
              muiFocus_Set(context, 0, list.items[3], mui_focusByCode) == mui_success &&
              muiFocus_MoveToward(context, list.root, 0, mui_directionRight) == mui_success &&
              Same(muiFocus_Get(context, 0), b[0]),
          "where it shows");
    muiDestroyContext(context);
}

static void TestStyled(void)
{
    // A class makes the list scroll; without it, the offset goes.
    List list;
    MakeList(&list, 64);
    muiContext* context = list.context;
    muiNodeId s = Sized(context, list.root, 100.0f, 50.0f);
    muiNodeId item = Sized(context, s, 100.0f, 200.0f);
    Keep(context, item);
    muiStyleId scrolling = {0, 0};
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollVertical;
    const muiStyleDef styleDef = muiDefaultStyleDef();
    CHECK(muiCreateStyle(context, &styleDef, &scrolling) == mui_success &&
              muiStyle_SetLayoutValues(context, scrolling, mui_variantBase, &style, SCROLL) ==
                  mui_success &&
              muiNode_SetClasses(context, s, &scrolling, 1) == mui_success,
          "a scrolling class");
    Layout(context, list.root);
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_SetScroll(context, s, 0.0f, 40.0f) == mui_success &&
              muiNode_GetScroll(context, s, &x, &y) == mui_success && y == 40.0f,
          "scrolled by class");
    CHECK(muiNode_SetClasses(context, s, NULL, 0) == mui_success, "class gone");
    Layout(context, list.root);
    CHECK(muiNode_GetScroll(context, s, &x, &y) == mui_success && y == 0.0f &&
              ExtentIs(context, s, 0.0f, 0.0f),
          "the offset gone with it");
    muiDestroyContext(context);
}

int main(void)
{
    TestExtent();
    TestMinimum();
    TestRightToLeft();
    TestPainting();
    TestNested();
    TestHits();
    TestScaled();
    TestMapToRoot();
    TestIntoView();
    TestLayer();
    TestExtentEdges();
    TestRightToLeftBorders();
    TestCopiedNest();
    TestOverlay();
    TestBeside();
    TestStyled();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
