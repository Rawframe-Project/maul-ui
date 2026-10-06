// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wheel input (record mui-0007): routing, the default scroll of the
// nearest scroll container that can move, chaining outward, latching,
// Shift and right to left, layers, the scroll rule, scrollbar thumbs,
// and calls outside the contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"

#include <math.h>
#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

#define MS 1000000ull

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void SetLayout(muiContext* context, muiNodeId node, const muiLayoutStyle* style,
                      muiPropertyMask mask)
{
    CHECK(muiNode_SetLayoutValues(context, node, style, mask) == mui_success, "layout values");
}

// A node of a fixed size under parent that keeps it in a flex line.
static muiNodeId Sized(muiContext* context, muiNodeId parent, float width, float height)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.width = Length(width);
    style.sizing.height = Length(height);
    style.item.shrink = 0.0f;
    SetLayout(context, node, &style,
              MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight) |
                  MUI_PROPERTY_BIT(mui_propertyShrink));
    return node;
}

static void Scrolls(muiContext* context, muiNodeId node, muiScrollAxes axes, bool column)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = axes;
    style.container.direction = column ? mui_flexColumn : mui_flexRow;
    SetLayout(context, node, &style,
              MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                  MUI_PROPERTY_BIT(mui_propertyFlexDirection));
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static muiContext* MakeContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    return context;
}

static float X(const muiContext* context, muiNodeId node)
{
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_GetScroll(context, node, &x, &y) == mui_success, "read");
    return x;
}

static float Y(const muiContext* context, muiNodeId node)
{
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_GetScroll(context, node, &x, &y) == mui_success, "read");
    return y;
}

// A turn at a point: true when handled.
static bool Turn(muiContext* context, muiNodeId root, uint64_t timeNs, float x, float y,
                 float deltaX, float deltaY, muiModifiers modifiers)
{
    const muiWheelEvent event = {timeNs, x, y, deltaX, deltaY, modifiers, 0};
    bool handled = false;
    CHECK(muiWheelInput(context, root, &event, &handled) == mui_success, "wheel");
    return handled;
}

// A column root of 300 by 300 holding outer (200 by 100, scrolling
// vertically), which holds inner (200 by 50, scrolling vertically, its
// content 100 tall) and below it a filler of 200: outer's limit is 150,
// inner's 50. A plain node beside, after outer, 100 tall.
typedef struct Nest
{
    muiContext* context;
    muiNodeId root;
    muiNodeId outer;
    muiNodeId inner;
    muiNodeId content;
    muiNodeId filler;
    muiNodeId plain;
} Nest;

static void MakeNest(Nest* nest)
{
    *nest = (Nest){.context = MakeContext()};
    muiContext* context = nest->context;
    nest->root = Sized(context, s_nullNode, 300.0f, 300.0f);
    Scrolls(context, nest->root, mui_scrollNone, true);
    nest->outer = Sized(context, nest->root, 200.0f, 100.0f);
    Scrolls(context, nest->outer, mui_scrollVertical, true);
    nest->inner = Sized(context, nest->outer, 200.0f, 50.0f);
    Scrolls(context, nest->inner, mui_scrollVertical, true);
    nest->content = Sized(context, nest->inner, 200.0f, 100.0f);
    nest->filler = Sized(context, nest->outer, 200.0f, 200.0f);
    nest->plain = Sized(context, nest->root, 100.0f, 100.0f);
    Layout(context, nest->root);
}

static void TestChain(void)
{
    Nest nest;
    MakeNest(&nest);
    muiContext* context = nest.context;
    muiNodeId root = nest.root;
    // Down over inner (at 25): inner takes it, to its end.
    CHECK(Turn(context, root, 0, 50.0f, 25.0f, 0.0f, -1.0f, 0) && Y(context, nest.inner) == 50.0f &&
              Y(context, nest.outer) == 0.0f,
          "inner first");
    // Latched: inner keeps it at its end, handled.
    CHECK(Turn(context, root, 100 * MS, 50.0f, 25.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.inner) == 50.0f && Y(context, nest.outer) == 0.0f,
          "latched at the end");
    // At the latch time, the turn chains to outer, and the list follows.
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, root, &input) == mui_success, "drawn");
    CHECK(Turn(context, root, 600 * MS, 50.0f, 25.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.outer) == 100.0f &&
              muiBuildDrawList(context, root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transforms[1].f == -100.0f,
          "chained outward");
    // Over the filler while still latched to outer: outer.
    CHECK(Turn(context, root, 800 * MS, 50.0f, 80.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.outer) == 150.0f,
          "outer to its end");
    // Outer at its end, latched, over the plain node: the point left, and
    // nothing there scrolls.
    CHECK(!Turn(context, root, 900 * MS, 50.0f, 150.0f, 0.0f, -1.0f, 0),
          "over nothing that scrolls");
    // Up, much later: outer scrolled 150 shows the filler at 25, and
    // outer can move up.
    CHECK(Turn(context, root, 2000 * MS, 50.0f, 25.0f, 0.0f, 1.0f, 0) &&
              Y(context, nest.outer) == 50.0f,
          "up again");
    // A point over nothing at all.
    CHECK(!Turn(context, root, 3000 * MS, 500.0f, 500.0f, 0.0f, -1.0f, 0), "outside the root");
    // A latched container that is no longer one does not hold.
    CHECK(Turn(context, root, 4000 * MS, 50.0f, 25.0f, 0.0f, 1.0f, 0) &&
              Y(context, nest.outer) == 0.0f,
          "to the top");
    Scrolls(context, nest.outer, mui_scrollNone, true);
    Layout(context, root);
    CHECK(!Turn(context, root, 4001 * MS, 50.0f, 75.0f, 0.0f, 1.0f, 0), "latch dropped");
    muiDestroyContext(context);
}

static void TestTime(void)
{
    // Up at the top: nothing moves, nothing takes it.
    Nest nest;
    MakeNest(&nest);
    muiContext* context = nest.context;
    CHECK(!Turn(context, nest.root, 0, 50.0f, 25.0f, 0.0f, 1.0f, 0), "up at the top");
    // The latch runs from the last turn; a time before it does not hold
    // it, and a rule without a latch never holds one.
    CHECK(Turn(context, nest.root, 500 * MS, 50.0f, 25.0f, 0.0f, -0.25f, 0) &&
              Turn(context, nest.root, 900 * MS, 50.0f, 25.0f, 0.0f, -0.25f, 0) &&
              Y(context, nest.inner) == 50.0f,
          "inner");
    CHECK(Turn(context, nest.root, 1000 * MS, 50.0f, 25.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.outer) == 0.0f,
          "still latched from the later turn");
    CHECK(Turn(context, nest.root, 899 * MS, 50.0f, 25.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.outer) == 100.0f,
          "time went back: chained");
    muiScrollRule rule = muiDefaultScrollRule();
    CHECK(rule.wheelStep == 100.0f && rule.latchNs == 500 * MS, "the defaults");
    rule.wheelStep = 10.0f;
    rule.latchNs = 0;
    CHECK(muiSetScrollRule(context, &rule) == mui_success, "set the rule");
    CHECK(Turn(context, nest.root, 1000 * MS, 50.0f, 25.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.outer) == 110.0f,
          "a step of 10, not latched");
    muiDestroyContext(context);
}

static void TestAcross(void)
{
    // A row scrolling horizontally, 100 wide, its content 300: Shift turns
    // the wheel across; right to left, rightward is back to the start.
    muiContext* context = MakeContext();
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiNodeId s = Sized(context, root, 100.0f, 50.0f);
    Scrolls(context, s, mui_scrollBoth, false);
    (void)Sized(context, s, 300.0f, 100.0f);
    muiNodeId t = Sized(context, root, 100.0f, 50.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollHorizontal;
    style.textDirection = mui_textRightToLeft;
    SetLayout(context, t, &style,
              MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                  MUI_PROPERTY_BIT(mui_propertyTextDirection));
    (void)Sized(context, t, 300.0f, 50.0f);
    Layout(context, root);
    CHECK(Turn(context, root, 0, 50.0f, 25.0f, 0.0f, -1.0f, mui_modShift) &&
              X(context, s) == 100.0f && Y(context, s) == 0.0f,
          "Shift and toward the user: right only");
    CHECK(Turn(context, root, 0, 50.0f, 25.0f, 0.5f, -1.0f, mui_modShift) &&
              X(context, s) == 150.0f && Y(context, s) == 50.0f,
          "Shift with a turn across: as it is");
    CHECK(Turn(context, root, 0, 50.0f, 25.0f, -2.0f, 0.0f, 0) && X(context, s) == 0.0f,
          "left, at most to 0");
    CHECK(!Turn(context, root, 0, 150.0f, 25.0f, 1.0f, 0.0f, 0) && X(context, t) == 0.0f,
          "right to left at its start: rightward cannot");
    CHECK(Turn(context, root, 0, 150.0f, 25.0f, -1.0f, 0.0f, 0) && X(context, t) == 100.0f,
          "leftward scrolls on");
    muiDestroyContext(context);
}

typedef struct Handler
{
    muiContext* context;
    muiEvent last;
    int calls;
    bool take;
    bool destroy;
} Handler;

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    (void)nodeId;
    (void)phase;
    Handler* handler = user;
    handler->last = *event;
    handler->calls++;
    if (handler->destroy)
    {
        CHECK(muiDestroyNode(handler->context, event->target) == mui_success, "destroyed");
        handler->destroy = false;
    }
    return handler->take;
}

static void TestRouted(void)
{
    Nest nest;
    MakeNest(&nest);
    muiContext* context = nest.context;
    Handler handler = {.context = context, .take = true};
    CHECK(muiSetEventFunction(context, Hear, &handler) == mui_success, "function");
    CHECK(Turn(context, nest.root, 7, 50.0f, 25.0f, 0.0f, -1.0f, mui_modControl) &&
              Y(context, nest.inner) == 0.0f && handler.calls == 1 &&
              handler.last.kind == mui_eventWheel && handler.last.timeNs == 7 &&
              handler.last.modifiers == mui_modControl && handler.last.wheel != NULL &&
              Same(handler.last.target, nest.content),
          "a handler takes it");
    handler.take = false;
    CHECK(Turn(context, nest.root, 8, 50.0f, 25.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.inner) == 50.0f,
          "not taken: the default");
    // Latched to inner, the node under the point goes: nothing holds it.
    handler.destroy = true;
    CHECK(!Turn(context, nest.root, 9, 50.0f, 25.0f, 0.0f, 1.0f, 0) &&
              Y(context, nest.inner) == 50.0f,
          "the node gone");
    muiDestroyContext(context);
}

static void TestLayer(void)
{
    // A layer root inside a scroller stops the search.
    Nest nest;
    MakeNest(&nest);
    muiContext* context = nest.context;
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.layer = mui_layerActivation;
    CHECK(muiNode_SetInteractionValues(context, nest.filler, &values,
                                       MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "a layer");
    Layout(context, nest.root);
    CHECK(!Turn(context, nest.root, 0, 50.0f, 75.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.outer) == 0.0f,
          "not past the layer");
    // Nor past the root given: under inner, at its end, outer stays.
    CHECK(Turn(context, nest.inner, 0, 50.0f, 25.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.inner) == 50.0f,
          "inner as the root");
    CHECK(!Turn(context, nest.inner, 1000 * MS, 50.0f, 25.0f, 0.0f, -1.0f, 0) &&
              Y(context, nest.outer) == 0.0f,
          "not past the root");
    muiDestroyContext(context);
}

static void TestThumb(void)
{
    Nest nest;
    MakeNest(&nest);
    muiContext* context = nest.context;
    muiScrollThumb thumb = {0};
    // Outer: scrollport 100 of 250; limit 150.
    CHECK(muiNode_GetScrollThumb(context, nest.outer, false, 100.0f, 10.0f, &thumb) ==
                  mui_success &&
              thumb.start == 0.0f && thumb.length == 40.0f,
          "at the top");
    CHECK(muiNode_SetScroll(context, nest.outer, 0.0f, 75.0f) == mui_success &&
              muiNode_GetScrollThumb(context, nest.outer, false, 100.0f, 10.0f, &thumb) ==
                  mui_success &&
              thumb.start == 30.0f && thumb.length == 40.0f,
          "half way");
    CHECK(muiNode_GetScrollThumb(context, nest.outer, false, 100.0f, 50.0f, &thumb) ==
                  mui_success &&
              thumb.start == 25.0f && thumb.length == 50.0f,
          "a minimum");
    CHECK(muiNode_GetScrollThumb(context, nest.outer, false, 100.0f, 500.0f, &thumb) ==
                  mui_success &&
              thumb.start == 0.0f && thumb.length == 100.0f,
          "at most the track");
    CHECK(muiNode_GetScrollThumb(context, nest.outer, true, 80.0f, 10.0f, &thumb) == mui_success &&
              thumb.start == 0.0f && thumb.length == 80.0f,
          "an axis it does not scroll");
    CHECK(muiNode_GetScrollThumb(context, nest.plain, false, 80.0f, 10.0f, &thumb) == mui_success &&
              thumb.start == 0.0f && thumb.length == 80.0f,
          "a node that does not scroll");
    CHECK(muiNode_GetScrollThumb(NULL, nest.outer, false, 1.0f, 0.0f, &thumb) == mui_errorInvalid &&
              muiNode_GetScrollThumb(context, nest.outer, false, 1.0f, 0.0f, NULL) ==
                  mui_errorInvalid &&
              muiNode_GetScrollThumb(context, s_nullNode, false, 1.0f, 0.0f, &thumb) ==
                  mui_errorInvalid &&
              muiNode_GetScrollThumb(context, nest.outer, false, NAN, 0.0f, &thumb) ==
                  mui_errorInvalid &&
              muiNode_GetScrollThumb(context, nest.outer, false, -1.0f, 0.0f, &thumb) ==
                  mui_errorInvalid &&
              muiNode_GetScrollThumb(context, nest.outer, false, 1.0f, INFINITY, &thumb) ==
                  mui_errorInvalid &&
              muiNode_GetScrollThumb(context, nest.outer, false, 1.0f, -1.0f, &thumb) ==
                  mui_errorInvalid,
          "outside the contract");
    CHECK(muiDestroyNode(context, nest.plain) == mui_success &&
              muiNode_GetScrollThumb(context, nest.plain, false, 1.0f, 0.0f, &thumb) ==
                  mui_errorStale,
          "a node gone");
    muiDestroyContext(context);
}

static bool Refeed(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    (void)nodeId;
    (void)phase;
    muiContext* context = user;
    const muiWheelEvent again = *event->wheel;
    bool handled = false;
    CHECK(muiWheelInput(context, event->target, &again, &handled) == mui_errorInvalid,
          "no input while dispatching");
    return false;
}

static void TestContract(void)
{
    Nest nest;
    MakeNest(&nest);
    muiContext* context = nest.context;
    bool handled = false;
    const muiWheelEvent good = {0, 50.0f, 25.0f, 0.0f, -1.0f, 0, 0};
    muiWheelEvent bad = good;
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiWheelInput(NULL, nest.root, &good, &handled) == mui_errorInvalid &&
              muiWheelInput(context, nest.root, NULL, &handled) == mui_errorInvalid &&
              muiWheelInput(context, nest.root, &good, NULL) == mui_errorInvalid &&
              muiWheelInput(context, s_nullNode, &good, &handled) == mui_errorInvalid,
          "missing arguments");
    bad.x = NAN;
    CHECK(muiWheelInput(context, nest.root, &bad, &handled) == mui_errorInvalid, "a point");
    bad = good;
    bad.y = INFINITY;
    CHECK(muiWheelInput(context, nest.root, &bad, &handled) == mui_errorInvalid, "a point y");
    bad = good;
    bad.deltaX = NAN;
    CHECK(muiWheelInput(context, nest.root, &bad, &handled) == mui_errorInvalid, "a delta");
    bad = good;
    bad.deltaY = -INFINITY;
    CHECK(muiWheelInput(context, nest.root, &bad, &handled) == mui_errorInvalid, "a delta y");
    bad = good;
    bad.player = 8;
    CHECK(muiWheelInput(context, nest.root, &bad, &handled) == mui_errorInvalid, "a player");
    CHECK(muiGetContextMisuse(context) == misuse + 8, "counted");
    CHECK(muiSetEventFunction(context, Refeed, context) == mui_success, "function");
    CHECK(muiWheelInput(context, nest.root, &good, &handled) == mui_success && handled,
          "refused inside, default after");
    muiScrollRule rule = muiDefaultScrollRule();
    rule.wheelStep = NAN;
    CHECK(muiSetScrollRule(NULL, &rule) == mui_errorInvalid &&
              muiSetScrollRule(context, NULL) == mui_errorInvalid &&
              muiSetScrollRule(context, &rule) == mui_errorInvalid,
          "a rule outside the contract");
    rule.wheelStep = -1.0f;
    CHECK(muiSetScrollRule(context, &rule) == mui_errorInvalid, "a negative step");
    muiNodeId root = nest.root;
    CHECK(muiDestroyNode(context, root) == mui_success, "gone");
    CHECK(muiWheelInput(context, root, &good, &handled) == mui_errorStale, "a root gone");
    muiDestroyContext(context);
}

int main(void)
{
    TestChain();
    TestTime();
    TestAcross();
    TestRouted();
    TestLayer();
    TestThumb();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
