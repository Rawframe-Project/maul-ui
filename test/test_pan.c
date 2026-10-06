// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Touch scrolling (record mui-0007): a touch's drag pans the nearest
// scroll container, a release flings it by iOS's decay, a press stops
// a fling; mice do not pan, nearer drags win, and the rule's rate.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"

#include <math.h>
#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

#define MS 1000000ull

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
    style.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    style.sizing.height = (muiDimension){0.0f, height, mui_dimensionValue};
    style.item.shrink = 0.0f;
    CHECK(muiNode_SetLayoutValues(context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight) |
                                      MUI_PROPERTY_BIT(mui_propertyShrink)) == mui_success,
          "size");
    return node;
}

static void Scrolls(muiContext* context, muiNodeId node, muiScrollAxes axes, bool column,
                    muiTextDirection direction)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = axes;
    style.container.direction = column ? mui_flexColumn : mui_flexRow;
    style.textDirection = direction;
    CHECK(muiNode_SetLayoutValues(context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                                      MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                                      MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "scrolls");
}

static void Layout(muiContext* context, muiNodeId root, uint64_t timeNs)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, timeNs, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
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

// A list: root (300 by 400) holding s (200 by 100, scrolling
// vertically) with count items of 50: s's limit is 50 count less 100.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId s;
    muiNodeId first;
} Scene;

static void MakeScene(Scene* scene, int count)
{
    muiContextDef def = muiDefaultContextDef();
    *scene = (Scene){0};
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiContext* context = scene->context;
    scene->root = Sized(context, s_nullNode, 300.0f, 400.0f);
    scene->s = Sized(context, scene->root, 200.0f, 100.0f);
    Scrolls(context, scene->s, mui_scrollVertical, true, mui_textInherit);
    for (int i = 0; i < count; i++)
    {
        muiNodeId item = Sized(context, scene->s, 200.0f, 50.0f);
        scene->first = i == 0 ? item : scene->first;
    }
    Layout(context, scene->root, 0);
}

// Feeds a pointer event and dispatches every record it left; how many
// were handled.
static int Feed(const Scene* scene, uint32_t pointer, muiPointerKind kind, muiPointerAction action,
                uint64_t timeNs, float x, float y)
{
    muiPointerButtons buttons = action == mui_pointerRelease || action == mui_pointerCancel ? 0 : 1;
    const muiPointerEvent event = {timeNs, pointer, kind, action, 0, buttons, x, y, 0};
    CHECK(muiPointerInput(scene->context, scene->root, &event) == mui_success, "input");
    int handled = 0;
    muiPointerRecord record = {0};
    while (muiNextPointerRecord(scene->context, &record) == mui_success)
    {
        bool taken = false;
        CHECK(muiDispatchPointerRecord(scene->context, &record, &taken) == mui_success, "dispatch");
        handled += taken;
    }
    return handled;
}

static int Touch(const Scene* scene, muiPointerAction action, uint64_t timeNs, float y)
{
    return Feed(scene, 7, mui_pointerTouch, action, timeNs, 50.0f, y);
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.05f;
}

static void TestPanAndFling(void)
{
    Scene scene;
    MakeScene(&scene, 10);
    muiContext* context = scene.context;
    CHECK(Touch(&scene, mui_pointerPress, 0, 50.0f) == 0 && Y(context, scene.s) == 0.0f, "down");
    CHECK(Touch(&scene, mui_pointerMove, 10 * MS, 46.0f) == 0 && Y(context, scene.s) == 0.0f,
          "within the slop");
    CHECK(Touch(&scene, mui_pointerMove, 10 * MS, 40.0f) == 1 && Y(context, scene.s) == 10.0f,
          "past it: the list follows the finger");
    CHECK(Touch(&scene, mui_pointerMove, 20 * MS, 20.0f) == 1 && Y(context, scene.s) == 30.0f,
          "on");
    // Up 10 at 10 ms, 20 more by 20 ms, still at 30 ms: the line through
    // them climbs 1000 a second, so the list flings down at 1000.
    CHECK(Touch(&scene, mui_pointerRelease, 30 * MS, 20.0f) == 1 && Y(context, scene.s) == 30.0f,
          "released: a fling");
    double decay = -log(0.998) * 1000.0;
    Layout(context, scene.root, 130 * MS);
    float expected = (float)(30.0 + 1000.0 * (1.0 - exp(-decay * 0.1)) / decay);
    CHECK(Near(Y(context, scene.s), expected), "after 100 ms");
    // A press stops it where it is. (A touch's hover and press restyle, so
    // a pending update says nothing here; a later layout leaves it.)
    CHECK(Touch(&scene, mui_pointerPress, 140 * MS, 50.0f) == 0, "stopped by a press");
    Layout(context, scene.root, 400 * MS);
    CHECK(Near(Y(context, scene.s), expected), "where it was");
    CHECK(Touch(&scene, mui_pointerRelease, 150 * MS, 50.0f) == 0, "a tap, no fling");
    // A fling to the end stops there.
    CHECK(Touch(&scene, mui_pointerPress, 1000 * MS, 90.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 1010 * MS, 60.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 1020 * MS, 10.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 1030 * MS, 10.0f) == 1,
          "a flick");
    Layout(context, scene.root, 3000 * MS);
    CHECK(Y(context, scene.s) == 400.0f && !muiIsUpdatePending(context, scene.root),
          "at the limit, done");
    muiDestroyContext(context);
}

static void TestNoFling(void)
{
    Scene scene;
    MakeScene(&scene, 10);
    muiContext* context = scene.context;
    // Slow: under 50 a second.
    CHECK(Touch(&scene, mui_pointerPress, 0, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 100 * MS, 40.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 400 * MS, 39.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 450 * MS, 38.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 500 * MS, 38.0f) == 1 &&
              Y(context, scene.s) == 12.0f,
          "slow: no fling");
    Layout(context, scene.root, 900 * MS);
    CHECK(Y(context, scene.s) == 12.0f, "slow: no fling, after a layout");
    // Cancelled: the list stays, no fling.
    CHECK(Touch(&scene, mui_pointerPress, 1000 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 1010 * MS, 30.0f) == 1 &&
              Touch(&scene, mui_pointerCancel, 1020 * MS, 30.0f) == 1 &&
              Y(context, scene.s) == 32.0f,
          "cancelled");
    Layout(context, scene.root, 1900 * MS);
    CHECK(Y(context, scene.s) == 32.0f, "cancelled, after a layout");
    // A mouse does not pan.
    CHECK(Feed(&scene, 1, mui_pointerMouse, mui_pointerPress, 2000 * MS, 50.0f, 50.0f) == 0 &&
              Feed(&scene, 1, mui_pointerMouse, mui_pointerMove, 2010 * MS, 50.0f, 10.0f) == 0 &&
              Feed(&scene, 1, mui_pointerMouse, mui_pointerRelease, 2020 * MS, 50.0f, 10.0f) == 0 &&
              Y(context, scene.s) == 32.0f,
          "a mouse");
    // Reduced motion: a pan, no fling.
    muiEnvironment environment = muiDefaultEnvironment();
    environment.reducedMotion = true;
    CHECK(muiSetContextEnvironment(context, &environment) == mui_success, "reduced");
    Layout(context, scene.root, 2900 * MS);
    CHECK(Touch(&scene, mui_pointerPress, 3000 * MS, 90.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 3010 * MS, 60.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 3020 * MS, 60.0f) == 1 &&
              Y(context, scene.s) == 62.0f,
          "reduced motion");
    Layout(context, scene.root, 3500 * MS);
    CHECK(Y(context, scene.s) == 62.0f, "reduced motion, after a layout");
    muiDestroyContext(context);
}

static void TestNearer(void)
{
    // A node taking drags inside the list drags; the list does not pan.
    Scene scene;
    MakeScene(&scene, 10);
    muiContext* context = scene.context;
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.drags = true;
    CHECK(muiNode_SetInteractionValues(context, scene.first, &values,
                                       MUI_PROPERTY_BIT(mui_propertyDrags)) == mui_success,
          "drags");
    Layout(context, scene.root, 0);
    CHECK(Touch(&scene, mui_pointerPress, 0, 25.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 10 * MS, 5.0f) == 0 && Y(context, scene.s) == 0.0f,
          "the item's drag");
    muiDestroyContext(context);
}

static void TestAcross(void)
{
    // A row right to left: a finger moving right scrolls toward the end.
    muiContextDef def = muiDefaultContextDef();
    Scene scene = {0};
    CHECK(muiCreateContext(&def, &scene.context) == mui_success, "context");
    muiContext* context = scene.context;
    scene.root = Sized(context, s_nullNode, 300.0f, 300.0f);
    scene.s = Sized(context, scene.root, 100.0f, 50.0f);
    Scrolls(context, scene.s, mui_scrollHorizontal, false, mui_textRightToLeft);
    (void)Sized(context, scene.s, 400.0f, 50.0f);
    Layout(context, scene.root, 0);
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerPress, 0, 50.0f, 25.0f) == 0 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 100 * MS, 80.0f, 25.0f) == 1 &&
              X(context, scene.s) == 30.0f && Y(context, scene.s) == 0.0f,
          "rightward: on");
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 200 * MS, 0.0f, 25.0f) == 1 &&
              X(context, scene.s) == 0.0f,
          "leftward past the start: 0");
    muiDestroyContext(context);
}

static void TestFlingThenStep(void)
{
    // A wheel turn during a fling takes its place, from where it is.
    Scene scene;
    MakeScene(&scene, 100);
    muiContext* context = scene.context;
    muiScrollRule rule = muiDefaultScrollRule();
    rule.easeNs = 0;
    CHECK(muiSetScrollRule(context, &rule) == mui_success, "rule");
    // Very fast: capped at 8000.
    CHECK(Touch(&scene, mui_pointerPress, 0, 90.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 1 * MS, 70.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 2 * MS, 10.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 3 * MS, 10.0f) == 1,
          "a flick");
    double decay = -log(0.998) * 1000.0;
    Layout(context, scene.root, 103 * MS);
    float expected = (float)(80.0 + 8000.0 * (1.0 - exp(-decay * 0.1)) / decay);
    CHECK(Near(Y(context, scene.s), expected), "capped at 8000");
    const muiWheelEvent wheel = {103 * MS, 50.0f, 50.0f, 0.0f, 1.0f, 0, 0};
    bool handled = false;
    CHECK(muiWheelInput(context, scene.root, &wheel, &handled) == mui_success && handled &&
              Near(Y(context, scene.s), expected - 100.0f) &&
              !muiIsUpdatePending(context, scene.root),
          "the wheel took over");
    muiDestroyContext(context);
}

static void TestRule(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiScrollRule rule = muiDefaultScrollRule();
    CHECK(rule.decelerationRate == 0.998f, "iOS's rate");
    const float bad[] = {0.0f, 1.0f, -0.5f, NAN};
    for (int i = 0; i < 4; i++)
    {
        rule.decelerationRate = bad[i];
        CHECK(muiSetScrollRule(context, &rule) == mui_errorInvalid, "a rate outside");
    }
    rule.decelerationRate = 0.99f;
    CHECK(muiSetScrollRule(context, &rule) == mui_success, "fast");
    muiDestroyContext(context);
}

int main(void)
{
    TestPanAndFling();
    TestNoFling();
    TestNearer();
    TestAcross();
    TestFlingThenStep();
    TestRule();
    return s_failures == 0 ? 0 : 1;
}
