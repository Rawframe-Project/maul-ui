// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Touch scrolling (record mui-0007): a touch's drag pans the nearest
// scroll container, a release flings it by iOS's decay, a press stops
// a fling; mice do not pan, nearer drags win, and the rule's rate.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
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

// The clicks Feed has seen.
static int s_clicks;

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
        s_clicks += record.kind == mui_pointerRecordClick;
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

// The draw list's shift, down or across, of the scroll container whose
// transform is at index, drawn at scale.
static float DrawnAt(const Scene* scene, float scale, uint32_t index, bool down)
{
    const muiDrawInput input = {1, scale, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(scene->context, scene->root, &input) == mui_success &&
              muiGetDrawList(scene->context, &drawn) == mui_success && drawn.transformCount > index,
          "drawn");
    return down ? drawn.transforms[index].f : drawn.transforms[index].e;
}

// The first scroll container's.
static float Drawn(const Scene* scene, bool down)
{
    return DrawnAt(scene, 1.0f, 1, down);
}

// iOS's rubber band in a scrollport of port, of 100 by default; and the
// inverse.
static double BandIn(double past, double port)
{
    return port * (1.0 - 1.0 / (0.55 * past / port + 1.0));
}

static double Band(double past)
{
    return BandIn(past, 100.0);
}

static double Unband(double over)
{
    return 100.0 / 0.55 * over / (100.0 - over);
}

// What a bounce keeps of its overscroll after seconds.
static double Kept(double seconds)
{
    double wt = sqrt(200.0) * seconds;
    return (1.0 + wt) * exp(-wt);
}

static void Overscroll(muiContext* context, bool on)
{
    muiScrollRule rule = muiDefaultScrollRule();
    rule.overscroll = on;
    CHECK(muiSetScrollRule(context, &rule) == mui_success, "rule");
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
    Layout(context, scene.root, 230 * MS);
    expected = (float)(30.0 + 1000.0 * (1.0 - exp(-decay * 0.2)) / decay);
    CHECK(Near(Y(context, scene.s), expected), "after 200 ms");
    // A press stops it where it is. (A touch's hover and press restyle, so
    // a pending update says nothing here; a later layout leaves it.)
    CHECK(Touch(&scene, mui_pointerPress, 240 * MS, 50.0f) == 0, "stopped by a press");
    Layout(context, scene.root, 400 * MS);
    CHECK(Near(Y(context, scene.s), expected), "where it was");
    CHECK(Touch(&scene, mui_pointerRelease, 250 * MS, 50.0f) == 0, "a tap, no fling");
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
              Touch(&scene, mui_pointerMove, 1010 * MS, 40.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 1015 * MS, 30.0f) == 1 &&
              Touch(&scene, mui_pointerCancel, 1020 * MS, 30.0f) == 1 &&
              Y(context, scene.s) == 32.0f,
          "cancelled");
    Layout(context, scene.root, 1900 * MS);
    CHECK(Y(context, scene.s) == 32.0f, "cancelled, after a layout");
    // A mouse does not pan: no drag, so its release clicks.
    int clicks = s_clicks;
    CHECK(Feed(&scene, 1, mui_pointerMouse, mui_pointerPress, 2000 * MS, 50.0f, 50.0f) == 0 &&
              Feed(&scene, 1, mui_pointerMouse, mui_pointerMove, 2010 * MS, 50.0f, 10.0f) == 0 &&
              Feed(&scene, 1, mui_pointerMouse, mui_pointerRelease, 2020 * MS, 50.0f, 10.0f) == 0 &&
              Y(context, scene.s) == 32.0f && s_clicks == clicks + 1,
          "a mouse");
    // Reduced motion: a pan, no fling.
    muiEnvironment environment = muiDefaultEnvironment();
    environment.reducedMotion = true;
    CHECK(muiSetContextEnvironment(context, &environment) == mui_success, "reduced");
    Layout(context, scene.root, 2900 * MS);
    CHECK(Touch(&scene, mui_pointerPress, 3000 * MS, 90.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 3010 * MS, 60.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 3020 * MS, 30.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 3030 * MS, 30.0f) == 1 &&
              Y(context, scene.s) == 92.0f,
          "reduced motion");
    Layout(context, scene.root, 3500 * MS);
    CHECK(Y(context, scene.s) == 92.0f, "reduced motion, after a layout");
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

static void TestMore(void)
{
    Scene scene;
    MakeScene(&scene, 10);
    muiContext* context = scene.context;
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, scene.root, &input) == mui_success, "drawn");
    // Down at the top: nothing past it.
    CHECK(Touch(&scene, mui_pointerPress, 0, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 10 * MS, 70.0f) == 1 && Y(context, scene.s) == 0.0f,
          "not above the top");
    CHECK(Touch(&scene, mui_pointerMove, 20 * MS, 40.0f) == 1 && Y(context, scene.s) == 10.0f &&
              muiBuildDrawList(context, scene.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transforms[1].f == -10.0f,
          "drawn where it pans");
    CHECK(Touch(&scene, mui_pointerCancel, 30 * MS, 40.0f) == 1, "cancelled");
    // A fast move long before the release is out of the window.
    CHECK(Touch(&scene, mui_pointerPress, 1000 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 1010 * MS, 30.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 1015 * MS, -10.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 1300 * MS, -11.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 1350 * MS, -11.0f) == 1 &&
              Y(context, scene.s) == 71.0f,
          "held, then released");
    Layout(context, scene.root, 2000 * MS);
    CHECK(Y(context, scene.s) == 71.0f, "no fling");
    // Moves at one time give no velocity.
    CHECK(Touch(&scene, mui_pointerPress, 3000 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 3010 * MS, 30.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 3010 * MS, 30.0f) == 1,
          "at once");
    Layout(context, scene.root, 3500 * MS);
    CHECK(Y(context, scene.s) == 91.0f, "no fling at once");
    // Flung up past the top: stopped at 0, done.
    CHECK(Touch(&scene, mui_pointerPress, 4000 * MS, 20.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 4010 * MS, 30.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 4020 * MS, 50.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 4030 * MS, 50.0f) == 1 &&
              Y(context, scene.s) == 61.0f,
          "a flick up");
    Layout(context, scene.root, 4330 * MS);
    CHECK(Y(context, scene.s) == 0.0f && !muiIsUpdatePending(context, scene.root),
          "at the top, done");
    // A press does not stop a step, only flings; a pan does.
    muiScrollRule rule = muiDefaultScrollRule();
    const muiWheelEvent wheel = {5000 * MS, 50.0f, 50.0f, 0.0f, -1.0f, 0, 0};
    bool handled = false;
    CHECK(muiWheelInput(context, scene.root, &wheel, &handled) == mui_success && handled &&
              Touch(&scene, mui_pointerPress, 5010 * MS, 50.0f) == 0,
          "a step, a press");
    Layout(context, scene.root, 5200 * MS);
    CHECK(Y(context, scene.s) == 100.0f, "the step went on");
    CHECK(Touch(&scene, mui_pointerRelease, 5210 * MS, 50.0f) == 0, "released");
    const muiWheelEvent again = {6000 * MS, 50.0f, 50.0f, 0.0f, -1.0f, 0, 0};
    CHECK(muiWheelInput(context, scene.root, &again, &handled) == mui_success &&
              Touch(&scene, mui_pointerPress, 6010 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 6020 * MS, 30.0f) == 1 &&
              Y(context, scene.s) == 120.0f,
          "a pan from where the step was");
    Layout(context, scene.root, 6300 * MS);
    CHECK(Y(context, scene.s) == 120.0f, "the step stopped");
    CHECK(Touch(&scene, mui_pointerCancel, 6310 * MS, 30.0f) == 1, "cancelled");
    (void)rule;
    muiDestroyContext(context);
}

static void TestMouseDrags(void)
{
    // A scroll container that takes drags: a mouse drags it, no pan.
    Scene scene;
    MakeScene(&scene, 10);
    muiContext* context = scene.context;
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.drags = true;
    CHECK(muiNode_SetInteractionValues(context, scene.s, &values,
                                       MUI_PROPERTY_BIT(mui_propertyDrags)) == mui_success,
          "drags");
    Layout(context, scene.root, 0);
    CHECK(Feed(&scene, 1, mui_pointerMouse, mui_pointerPress, 0, 50.0f, 50.0f) == 0 &&
              Feed(&scene, 1, mui_pointerMouse, mui_pointerMove, 10 * MS, 50.0f, 10.0f) == 0 &&
              Y(context, scene.s) == 0.0f,
          "a mouse drag: no pan");
    muiDestroyContext(context);
}

static void TestTables(void)
{
    // Nine lists in a row, 100 wide, their content 300: limits of 200.
    muiContextDef def = muiDefaultContextDef();
    Scene scene = {0};
    CHECK(muiCreateContext(&def, &scene.context) == mui_success, "context");
    muiContext* context = scene.context;
    scene.root = Sized(context, s_nullNode, 900.0f, 100.0f);
    muiNodeId lists[9];
    for (int i = 0; i < 9; i++)
    {
        lists[i] = Sized(context, scene.root, 100.0f, 100.0f);
        Scrolls(context, lists[i], mui_scrollVertical, true, mui_textInherit);
        (void)Sized(context, lists[i], 100.0f, 300.0f);
    }
    Layout(context, scene.root, 0);
    // Five fingers at once: four pans.
    for (uint32_t i = 0; i < 5; i++)
    {
        float x = 100.0f * (float)i + 50.0f;
        CHECK(Feed(&scene, 10 + i, mui_pointerTouch, mui_pointerPress, 0, x, 50.0f) == 0, "down");
    }
    for (uint32_t i = 0; i < 5; i++)
    {
        float x = 100.0f * (float)i + 50.0f;
        int panned = Feed(&scene, 10 + i, mui_pointerTouch, mui_pointerMove, 10 * MS, x, 30.0f);
        CHECK(panned == (i < 4 ? 1 : 0) && Y(context, lists[i]) == (i < 4 ? 20.0f : 0.0f),
              "four pans");
        CHECK(Feed(&scene, 10 + i, mui_pointerTouch, mui_pointerMove, 20 * MS, x, 20.0f) ==
                  (i < 4 ? 1 : 0),
              "and their moves");
    }
    for (uint32_t i = 0; i < 5; i++)
    {
        float x = 100.0f * (float)i + 50.0f;
        (void)Feed(&scene, 10 + i, mui_pointerTouch, mui_pointerCancel, 30 * MS, x, 20.0f);
    }
    // One finger after another, each its own pointer: each pans.
    for (uint32_t i = 0; i < 6; i++)
    {
        uint32_t pointer = 20 + i;
        CHECK(Feed(&scene, pointer, mui_pointerTouch, mui_pointerPress, 0, 850.0f, 50.0f) == 0 &&
                  Feed(&scene, pointer, mui_pointerTouch, mui_pointerMove, 10 * MS, 850.0f,
                       40.0f) == 1 &&
                  Feed(&scene, pointer, mui_pointerTouch, mui_pointerCancel, 20 * MS, 850.0f,
                       40.0f) == 1,
              "in turn");
    }
    CHECK(Y(context, lists[8]) == 60.0f, "six pans of 10");
    // Eight lists easing a step: the table is full, so a flick does not
    // fling the ninth.
    for (int i = 0; i < 8; i++)
    {
        const muiWheelEvent wheel = {1000 * MS, 100.0f * (float)i + 50.0f, 50.0f, 0.0f, -1.0f, 0,
                                     0};
        bool handled = false;
        CHECK(muiWheelInput(context, scene.root, &wheel, &handled) == mui_success && handled,
              "a step");
    }
    CHECK(Feed(&scene, 40, mui_pointerTouch, mui_pointerPress, 1000 * MS, 850.0f, 90.0f) == 0 &&
              Feed(&scene, 40, mui_pointerTouch, mui_pointerMove, 1010 * MS, 850.0f, 60.0f) == 1 &&
              Feed(&scene, 40, mui_pointerTouch, mui_pointerMove, 1020 * MS, 850.0f, 20.0f) == 1 &&
              Feed(&scene, 40, mui_pointerTouch, mui_pointerRelease, 1030 * MS, 850.0f, 20.0f) ==
                  1 &&
              Y(context, lists[8]) == 130.0f,
          "a flick");
    Layout(context, scene.root, 2000 * MS);
    CHECK(Y(context, lists[8]) == 130.0f && Y(context, lists[0]) == 130.0f, "no room to fling");
    // Eight easing again: an overscroll released with no room to spring
    // back is gone at once.
    Overscroll(context, true);
    for (int i = 0; i < 8; i++)
    {
        const muiWheelEvent wheel = {3000 * MS, 100.0f * (float)i + 50.0f, 50.0f, 0.0f, -1.0f, 0,
                                     0};
        bool handled = false;
        CHECK(muiWheelInput(context, scene.root, &wheel, &handled) == mui_success && handled,
              "a step");
    }
    CHECK(Feed(&scene, 41, mui_pointerTouch, mui_pointerPress, 3000 * MS, 850.0f, 90.0f) == 0 &&
              Feed(&scene, 41, mui_pointerTouch, mui_pointerMove, 3010 * MS, 850.0f, 0.0f) == 1 &&
              DrawnAt(&scene, 1.0f, 9, true) == roundf(-200.0f - (float)Band(20.0)),
          "past the end");
    CHECK(Feed(&scene, 41, mui_pointerTouch, mui_pointerRelease, 3020 * MS, 850.0f, 0.0f) == 1 &&
              DrawnAt(&scene, 1.0f, 9, true) == -200.0f,
          "no room to spring back");
    muiDestroyContext(context);
}

static void TestAcrossFling(void)
{
    // A row right to left, 100 wide, its content 400: a flick right goes
    // on toward the end, and stops there; a flick left back to 0.
    muiContextDef def = muiDefaultContextDef();
    Scene scene = {0};
    CHECK(muiCreateContext(&def, &scene.context) == mui_success, "context");
    muiContext* context = scene.context;
    scene.root = Sized(context, s_nullNode, 300.0f, 300.0f);
    scene.s = Sized(context, scene.root, 100.0f, 50.0f);
    Scrolls(context, scene.s, mui_scrollHorizontal, false, mui_textRightToLeft);
    (void)Sized(context, scene.s, 400.0f, 50.0f);
    Layout(context, scene.root, 0);
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerPress, 0, 10.0f, 25.0f) == 0 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 100 * MS, 40.0f, 25.0f) == 1 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 110 * MS, 80.0f, 25.0f) == 1 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerRelease, 120 * MS, 80.0f, 25.0f) == 1 &&
              X(context, scene.s) == 70.0f,
          "a flick right");
    Layout(context, scene.root, 420 * MS);
    Layout(context, scene.root, 421 * MS);
    CHECK(X(context, scene.s) == 300.0f && !muiIsUpdatePending(context, scene.root),
          "at the end, done");
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerPress, 1000 * MS, 90.0f, 25.0f) == 0 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 1010 * MS, 60.0f, 25.0f) == 1 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 1020 * MS, 0.0f, 25.0f) == 1 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerRelease, 1030 * MS, 0.0f, 25.0f) == 1 &&
              X(context, scene.s) == 210.0f,
          "a flick left");
    Layout(context, scene.root, 2000 * MS);
    CHECK(X(context, scene.s) == 0.0f, "at the start");
    muiDestroyContext(context);
}

static void TestSlowEnd(void)
{
    // A fling of 1000 in a long list stops by slowing, short of a limit.
    Scene scene;
    MakeScene(&scene, 100);
    muiContext* context = scene.context;
    CHECK(Touch(&scene, mui_pointerPress, 0, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 10 * MS, 40.0f) == 1 &&
              Touch(&scene, mui_pointerMove, 20 * MS, 20.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 30 * MS, 20.0f) == 1,
          "a flick");
    Layout(context, scene.root, 6000 * MS);
    Layout(context, scene.root, 6001 * MS);
    CHECK(Y(context, scene.s) > 500.0f && Y(context, scene.s) < 530.0f &&
              !muiIsUpdatePending(context, scene.root),
          "slowed to a stop");
    muiDestroyContext(context);
}

static void TestPurge(void)
{
    // Lists destroyed mid-pan, each beside s at 200 in the row, leave their
    // pans; the next pan clears them.
    Scene scene;
    MakeScene(&scene, 10);
    muiContext* context = scene.context;
    for (uint32_t i = 0; i < 4; i++)
    {
        muiNodeId list = Sized(context, scene.root, 200.0f, 100.0f);
        Scrolls(context, list, mui_scrollVertical, true, mui_textInherit);
        (void)Sized(context, list, 200.0f, 300.0f);
        Layout(context, scene.root, 0);
        CHECK(Feed(&scene, 50 + i, mui_pointerTouch, mui_pointerPress, 0, 250.0f, 50.0f) == 0 &&
                  Feed(&scene, 50 + i, mui_pointerTouch, mui_pointerMove, 10 * MS, 250.0f, 30.0f) ==
                      1,
              "a pan");
        CHECK(muiDestroyNode(context, list) == mui_success, "gone mid-pan");
        (void)Feed(&scene, 50 + i, mui_pointerTouch, mui_pointerRelease, 20 * MS, 250.0f, 30.0f);
    }
    Layout(context, scene.root, 0);
    CHECK(Touch(&scene, mui_pointerPress, 100 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 110 * MS, 30.0f) == 1 && Y(context, scene.s) == 20.0f,
          "still pans");
    muiDestroyContext(context);
}

static void TestOverscroll(void)
{
    Scene scene;
    MakeScene(&scene, 10);
    muiContext* context = scene.context;
    // Off: a pull past the top shows nothing past it.
    CHECK(Touch(&scene, mui_pointerPress, 0, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 10 * MS, 70.0f) == 1 && Drawn(&scene, true) == 0.0f,
          "off");
    CHECK(Touch(&scene, mui_pointerCancel, 20 * MS, 70.0f) == 1, "cancelled");
    Overscroll(context, true);
    // On: the band, the offset at 0.
    CHECK(Touch(&scene, mui_pointerPress, 100 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 110 * MS, 70.0f) == 1 && Y(context, scene.s) == 0.0f &&
              Drawn(&scene, true) == roundf((float)Band(20.0)),
          "pulled 20 past the top");
    CHECK(Touch(&scene, mui_pointerMove, 120 * MS, 90.0f) == 1 &&
              Drawn(&scene, true) == roundf((float)Band(40.0)),
          "40 past");
    // Sideways along an axis it does not scroll: nothing.
    CHECK(Feed(&scene, 7, mui_pointerTouch, mui_pointerMove, 125 * MS, 150.0f, 90.0f) == 1 &&
              Drawn(&scene, false) == 0.0f,
          "not across");
    CHECK(Touch(&scene, mui_pointerMove, 130 * MS, 90.0f) == 1, "back");
    // Released: it springs back.
    CHECK(Touch(&scene, mui_pointerRelease, 130 * MS, 90.0f) == 1, "released");
    Layout(context, scene.root, 230 * MS);
    CHECK(Drawn(&scene, true) == roundf((float)(Band(40.0) * Kept(0.1))) &&
              Y(context, scene.s) == 0.0f,
          "springing back");
    // Within a tenth of a unit it rests at none, as fine a scale shows.
    Layout(context, scene.root, 660 * MS);
    CHECK(Band(40.0) * Kept(0.53) < 0.1 && DrawnAt(&scene, 64.0f, 1, true) == 0.0f, "at rest");
    Layout(context, scene.root, 1130 * MS);
    CHECK(Drawn(&scene, true) == 0.0f && !muiIsUpdatePending(context, scene.root), "back, done");
    // Past the end, the offset at the limit.
    CHECK(muiNode_SetScroll(context, scene.s, 0.0f, 400.0f) == mui_success, "to the end");
    CHECK(Touch(&scene, mui_pointerPress, 2000 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 2010 * MS, 10.0f) == 1 &&
              Y(context, scene.s) == 400.0f &&
              Drawn(&scene, true) == roundf(-400.0f - (float)Band(40.0)),
          "40 past the end");
    CHECK(Touch(&scene, mui_pointerRelease, 2020 * MS, 10.0f) == 1, "released");
    Layout(context, scene.root, 2120 * MS);
    // Caught while springing back: the pan goes on from the pan that put
    // it there.
    double over = Band(40.0) * Kept(0.1);
    double wanted = 400.0 + Unband(over) + 10.0;
    CHECK(Touch(&scene, mui_pointerPress, 2120 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 2130 * MS, 40.0f) == 1 &&
              Near(Drawn(&scene, true), roundf((float)(-400.0 - Band(wanted - 400.0)))),
          "caught");
    // Cancelled past it: it springs back too.
    CHECK(Touch(&scene, mui_pointerCancel, 2140 * MS, 40.0f) == 1, "cancelled past");
    Layout(context, scene.root, 4000 * MS);
    CHECK(Drawn(&scene, true) == -400.0f, "back after a cancel");
    // A wheel turn while it springs back takes it back at once.
    CHECK(Touch(&scene, mui_pointerPress, 5000 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 5010 * MS, 10.0f) == 1 &&
              Touch(&scene, mui_pointerRelease, 5020 * MS, 10.0f) == 1,
          "past again");
    const muiWheelEvent wheel = {5030 * MS, 50.0f, 50.0f, 0.0f, 1.0f, 0, 0};
    bool handled = false;
    CHECK(muiWheelInput(context, scene.root, &wheel, &handled) == mui_success && handled &&
              Drawn(&scene, true) == -400.0f,
          "a wheel turn");
    Layout(context, scene.root, 6000 * MS);
    // A wheel turn mid-pan takes it back too.
    CHECK(muiNode_SetScroll(context, scene.s, 0.0f, 400.0f) == mui_success &&
              Touch(&scene, mui_pointerPress, 6100 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 6110 * MS, 10.0f) == 1 &&
              Drawn(&scene, true) == roundf(-400.0f - (float)Band(40.0)),
          "past mid-pan");
    const muiWheelEvent midPan = {6120 * MS, 50.0f, 50.0f, 0.0f, 1.0f, 0, 0};
    CHECK(muiWheelInput(context, scene.root, &midPan, &handled) == mui_success && handled &&
              Drawn(&scene, true) == -400.0f,
          "a wheel turn mid-pan");
    CHECK(Touch(&scene, mui_pointerCancel, 6130 * MS, 10.0f) == 1, "cancelled");
    Layout(context, scene.root, 6900 * MS);
    // Setting the offset takes it back too; reduced motion, at once.
    CHECK(Touch(&scene, mui_pointerPress, 7000 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 7010 * MS, 10.0f) == 1 &&
              muiNode_SetScroll(context, scene.s, 0.0f, 300.0f) == mui_success &&
              Drawn(&scene, true) == -300.0f,
          "set mid-pan");
    CHECK(Touch(&scene, mui_pointerCancel, 7020 * MS, 10.0f) == 1, "cancelled");
    muiEnvironment environment = muiDefaultEnvironment();
    environment.reducedMotion = true;
    CHECK(muiSetContextEnvironment(context, &environment) == mui_success, "reduced");
    CHECK(muiNode_SetScroll(context, scene.s, 0.0f, 0.0f) == mui_success, "to the top");
    Layout(context, scene.root, 7900 * MS);
    CHECK(Touch(&scene, mui_pointerPress, 8000 * MS, 50.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 8010 * MS, 70.0f) == 1 &&
              Drawn(&scene, true) == roundf((float)Band(20.0)) &&
              Touch(&scene, mui_pointerRelease, 8020 * MS, 70.0f) == 1 &&
              Drawn(&scene, true) == 0.0f,
          "reduced motion: back at once");
    muiDestroyContext(context);
}

static void TestOverscrollAcross(void)
{
    // A row right to left: a finger moving left from the start pulls the
    // content left with it.
    muiContextDef def = muiDefaultContextDef();
    Scene scene = {0};
    CHECK(muiCreateContext(&def, &scene.context) == mui_success, "context");
    muiContext* context = scene.context;
    scene.root = Sized(context, s_nullNode, 300.0f, 300.0f);
    scene.s = Sized(context, scene.root, 100.0f, 50.0f);
    Scrolls(context, scene.s, mui_scrollHorizontal, false, mui_textRightToLeft);
    (void)Sized(context, scene.s, 400.0f, 50.0f);
    Overscroll(context, true);
    Layout(context, scene.root, 0);
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerPress, 0, 50.0f, 25.0f) == 0 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 10 * MS, 20.0f, 25.0f) == 1 &&
              X(context, scene.s) == 0.0f && Drawn(&scene, false) == -roundf((float)Band(30.0)) &&
              Drawn(&scene, true) == 0.0f,
          "pulled left past the start");
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerRelease, 20 * MS, 20.0f, 25.0f) == 1,
          "released");
    Layout(context, scene.root, 120 * MS);
    double over = Band(30.0) * Kept(0.1);
    CHECK(Drawn(&scene, false) == -roundf((float)over), "springing back");
    // Caught, and pulled 10 further.
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerPress, 120 * MS, 50.0f, 25.0f) == 0 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 130 * MS, 40.0f, 25.0f) == 1 &&
              Drawn(&scene, false) == -roundf((float)Band(Unband(over) + 10.0)),
          "caught");
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerCancel, 140 * MS, 40.0f, 25.0f) == 1,
          "cancelled");
    Layout(context, scene.root, 2000 * MS);
    CHECK(Drawn(&scene, false) == 0.0f && !muiIsUpdatePending(context, scene.root), "back");
    muiDestroyContext(context);
}

static void TestOverscrollPort(void)
{
    // Borders of 10 above and below leave a scrollport of 80.
    Scene scene;
    MakeScene(&scene, 10);
    muiContext* context = scene.context;
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.border.top = 10.0f;
    style.border.bottom = 10.0f;
    CHECK(muiNode_SetLayoutValues(context, scene.s, &style,
                                  MUI_PROPERTY_BIT(mui_propertyBorderTop) |
                                      MUI_PROPERTY_BIT(mui_propertyBorderBottom)) == mui_success,
          "borders");
    Overscroll(context, true);
    Layout(context, scene.root, 0);
    CHECK(Touch(&scene, mui_pointerPress, 0, 30.0f) == 0 &&
              Touch(&scene, mui_pointerMove, 10 * MS, 90.0f) == 1 &&
              Drawn(&scene, true) == roundf((float)BandIn(60.0, 80.0)),
          "the band of 80");
    // Scrolling turned off and on again mid-pan: the overscroll is gone.
    Scrolls(context, scene.s, mui_scrollNone, true, mui_textInherit);
    Layout(context, scene.root, 20 * MS);
    Scrolls(context, scene.s, mui_scrollVertical, true, mui_textInherit);
    Layout(context, scene.root, 30 * MS);
    CHECK(Drawn(&scene, true) == 0.0f, "gone with scrolling");
    CHECK(Touch(&scene, mui_pointerCancel, 40 * MS, 90.0f) == 1, "cancelled");
    muiDestroyContext(context);
    // Borders of 10 at the start and the end: across, a scrollport of 180.
    MakeScene(&scene, 0);
    context = scene.context;
    Scrolls(context, scene.s, mui_scrollHorizontal, false, mui_textInherit);
    (void)Sized(context, scene.s, 400.0f, 50.0f);
    style.border = (muiEdges){10.0f, 10.0f, 0.0f, 0.0f};
    CHECK(muiNode_SetLayoutValues(context, scene.s, &style,
                                  MUI_PROPERTY_BIT(mui_propertyBorderStart) |
                                      MUI_PROPERTY_BIT(mui_propertyBorderEnd)) == mui_success,
          "borders");
    Overscroll(context, true);
    Layout(context, scene.root, 0);
    CHECK(Feed(&scene, 3, mui_pointerTouch, mui_pointerPress, 0, 20.0f, 50.0f) == 0 &&
              Feed(&scene, 3, mui_pointerTouch, mui_pointerMove, 10 * MS, 180.0f, 50.0f) == 1 &&
              Drawn(&scene, false) == roundf((float)BandIn(160.0, 180.0)),
          "the band of 180 across");
    muiDestroyContext(context);
}

static void TestRule(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiScrollRule rule = muiDefaultScrollRule();
    CHECK(rule.decelerationRate == 0.998f && !rule.overscroll, "iOS's rate, no overscroll");
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
    TestMore();
    TestMouseDrags();
    TestTables();
    TestAcrossFling();
    TestSlowEnd();
    TestPurge();
    TestOverscroll();
    TestOverscrollAcross();
    TestOverscrollPort();
    TestRule();
    return s_failures == 0 ? 0 : 1;
}
