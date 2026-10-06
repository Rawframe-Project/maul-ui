// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling by keys and steps easing (record mui-0007): whole detents
// and key steps easing out, retargeting, fractions at once, reduced
// motion, a full table; arrows and navigation directions by Android's
// ScrollView rule, page keys, and the scroll rule's steps.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"

#include <math.h>
#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

#define MS 1000000ull

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
    style.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    style.sizing.height = (muiDimension){0.0f, height, mui_dimensionValue};
    style.item.shrink = 0.0f;
    SetLayout(context, node, &style,
              MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight) |
                  MUI_PROPERTY_BIT(mui_propertyShrink));
    return node;
}

static void Column(muiContext* context, muiNodeId node, muiScrollAxes axes)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = axes;
    style.container.direction = mui_flexColumn;
    SetLayout(context, node, &style,
              MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                  MUI_PROPERTY_BIT(mui_propertyFlexDirection));
}

static void Focusable(muiContext* context, muiNodeId node)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focus mode");
}

static void Layout(muiContext* context, muiNodeId root, uint64_t timeNs)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, timeNs, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static muiContext* MakeContext(bool ease)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    if (!ease)
    {
        muiScrollRule rule = muiDefaultScrollRule();
        rule.easeNs = 0;
        CHECK(muiSetScrollRule(context, &rule) == mui_success, "no easing");
    }
    return context;
}

static float Y(const muiContext* context, muiNodeId node)
{
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_GetScroll(context, node, &x, &y) == mui_success, "read");
    return y;
}

static float X(const muiContext* context, muiNodeId node)
{
    float x = 0.0f;
    float y = 0.0f;
    CHECK(muiNode_GetScroll(context, node, &x, &y) == mui_success, "read");
    return x;
}

static bool Turn(muiContext* context, muiNodeId root, uint64_t timeNs, float deltaY)
{
    const muiWheelEvent event = {timeNs, 50.0f, 25.0f, 0.0f, deltaY, 0, 0};
    bool handled = false;
    CHECK(muiWheelInput(context, root, &event, &handled) == mui_success, "wheel");
    return handled;
}

static bool Key(muiContext* context, muiNodeId root, muiKeyCode code, muiModifiers modifiers)
{
    const muiKeyEvent event = {.timeNs = 0, .code = code, .modifiers = modifiers, .down = true};
    bool handled = false;
    CHECK(muiKeyInput(context, root, &event, &handled) == mui_success, "key");
    return handled;
}

// A column root 300 by 400: s (200 by 100, a vertical scroller, a column)
// holding count children of 200 by 50, then a node below s.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId s;
    muiNodeId below;
    muiNodeId items[9];
} Scene;

static void MakeScene(Scene* scene, bool ease, int count)
{
    *scene = (Scene){.context = MakeContext(ease)};
    muiContext* context = scene->context;
    scene->root = Sized(context, s_nullNode, 300.0f, 400.0f);
    Column(context, scene->root, mui_scrollNone);
    scene->s = Sized(context, scene->root, 200.0f, 100.0f);
    Column(context, scene->s, mui_scrollVertical);
    for (int i = 0; i < count; i++)
    {
        scene->items[i] = Sized(context, scene->s, 200.0f, 50.0f);
    }
    scene->below = Sized(context, scene->root, 200.0f, 50.0f);
    Layout(context, scene->root, 0);
}

static void TestEase(void)
{
    // Five children: s's limit is 150.
    Scene scene;
    MakeScene(&scene, true, 5);
    muiContext* context = scene.context;
    CHECK(Turn(context, scene.root, 0, -1.0f) && Y(context, scene.s) == 0.0f &&
              muiIsUpdatePending(context, scene.root),
          "a detent eases");
    Layout(context, scene.root, 75 * MS);
    CHECK(Y(context, scene.s) == 87.5f, "half the time: a cubic ease out");
    // Another detent from where it is, to where the first went plus one.
    CHECK(Turn(context, scene.root, 75 * MS, -1.0f), "again");
    Layout(context, scene.root, 150 * MS);
    CHECK(Y(context, scene.s) == 87.5f + 62.5f * 0.875f, "retargeted, within the limit");
    Layout(context, scene.root, 225 * MS);
    CHECK(Y(context, scene.s) == 150.0f && !muiIsUpdatePending(context, scene.root), "done");
    // A fraction applies at once and stops the step.
    CHECK(Turn(context, scene.root, 300 * MS, 1.0f) && Turn(context, scene.root, 310 * MS, 0.5f) &&
              Y(context, scene.s) == 100.0f && !muiIsUpdatePending(context, scene.root),
          "a fraction at once");
    // Setting the offset stops a step.
    CHECK(Turn(context, scene.root, 400 * MS, -1.0f) &&
              muiNode_SetScroll(context, scene.s, 0.0f, 10.0f) == mui_success,
          "set");
    Layout(context, scene.root, 600 * MS);
    CHECK(Y(context, scene.s) == 10.0f, "the step stopped");
    // A step from before a layout's time is where it ends at once; one
    // that a shorter list cuts ends at the new limit.
    CHECK(Turn(context, scene.root, 700 * MS, -1.0f), "down");
    Layout(context, scene.root, 600 * MS);
    CHECK(Y(context, scene.s) == 10.0f && muiIsUpdatePending(context, scene.root),
          "the time before the step: its start");
    CHECK(muiDestroyNode(context, scene.items[4]) == mui_success, "shorter");
    Layout(context, scene.root, 850 * MS);
    CHECK(Y(context, scene.s) == 100.0f, "at the new limit");
    // Reduced motion jumps.
    muiEnvironment environment = muiDefaultEnvironment();
    environment.reducedMotion = true;
    CHECK(muiSetContextEnvironment(context, &environment) == mui_success, "reduced motion set");
    Layout(context, scene.root, 880 * MS);
    CHECK(Turn(context, scene.root, 900 * MS, 1.0f) && Y(context, scene.s) == 0.0f &&
              !muiIsUpdatePending(context, scene.root),
          "reduced motion");
    muiDestroyContext(context);
}

static void TestEaseEnds(void)
{
    // A node easing that goes is dropped; a scroller that stops scrolling
    // too; a rule set to 0 mid-step ends it.
    Scene scene;
    MakeScene(&scene, true, 5);
    muiContext* context = scene.context;
    CHECK(Turn(context, scene.root, 0, -1.0f), "down");
    muiNodeId s = scene.s;
    CHECK(muiNode_Detach(context, s) == mui_success, "out of the tree");
    CHECK(muiDestroyNode(context, s) == mui_success, "gone");
    Layout(context, scene.root, 50 * MS);
    CHECK(!muiIsUpdatePending(context, scene.root), "dropped");
    muiDestroyContext(context);
    MakeScene(&scene, true, 5);
    context = scene.context;
    CHECK(Turn(context, scene.root, 0, -1.0f), "down");
    muiScrollRule rule = muiDefaultScrollRule();
    rule.easeNs = 0;
    CHECK(muiSetScrollRule(context, &rule) == mui_success, "no easing");
    Layout(context, scene.root, 1);
    CHECK(Y(context, scene.s) == 100.0f && !muiIsUpdatePending(context, scene.root),
          "ended at once");
    muiDestroyContext(context);
}

static void TestFull(void)
{
    // Nine scrollers stepping at once: the ninth jumps.
    muiContext* context = MakeContext(true);
    muiNodeId root = Sized(context, s_nullNode, 900.0f, 100.0f);
    muiNodeId lists[9];
    for (int i = 0; i < 9; i++)
    {
        lists[i] = Sized(context, root, 100.0f, 100.0f);
        Column(context, lists[i], mui_scrollVertical);
        (void)Sized(context, lists[i], 100.0f, 300.0f);
    }
    Layout(context, root, 0);
    for (int i = 0; i < 9; i++)
    {
        const muiWheelEvent event = {0, 100.0f * (float)i + 50.0f, 50.0f, 0.0f, -1.0f, 0, 0};
        bool handled = false;
        CHECK(muiWheelInput(context, root, &event, &handled) == mui_success && handled, "wheel");
    }
    CHECK(Y(context, lists[7]) == 0.0f && Y(context, lists[8]) == 100.0f, "the ninth jumps");
    // A second step for an easing one retargets it, not a new entry.
    const muiWheelEvent again = {1, 50.0f, 50.0f, 0.0f, -1.0f, 0, 0};
    bool handled = false;
    CHECK(muiWheelInput(context, root, &again, &handled) == mui_success && handled, "again");
    Layout(context, root, 500 * MS);
    CHECK(Y(context, lists[0]) == 200.0f && Y(context, lists[7]) == 100.0f, "all done");
    muiDestroyContext(context);
}

static void TestArrows(void)
{
    // Items of 50 at 0, 50, 100 (focusable), a gap of 300, then one more
    // at 450: s's limit is 400.
    muiContext* context = MakeContext(false);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 400.0f);
    Column(context, root, mui_scrollNone);
    muiNodeId s = Sized(context, root, 200.0f, 100.0f);
    Column(context, s, mui_scrollVertical);
    muiNodeId items[4];
    for (int i = 0; i < 3; i++)
    {
        items[i] = Sized(context, s, 200.0f, 50.0f);
        Focusable(context, items[i]);
    }
    (void)Sized(context, s, 200.0f, 300.0f);
    items[3] = Sized(context, s, 200.0f, 50.0f);
    Focusable(context, items[3]);
    muiNodeId below = Sized(context, root, 200.0f, 50.0f);
    Focusable(context, below);
    Layout(context, root, 0);
    CHECK(muiFocus_Set(context, 0, items[0], mui_focusByCode) == mui_success, "focus");
    CHECK(Key(context, root, mui_codeArrowDown, 0) && Same(muiFocus_Get(context, 0), items[1]) &&
              Y(context, s) == 0.0f,
          "near: the focus moves");
    CHECK(Key(context, root, mui_codeArrowDown, 0) && Same(muiFocus_Get(context, 0), items[2]) &&
              Y(context, s) == 50.0f,
          "near below: moved and into view");
    // items[3] spans 450 to 500: near once the offset is 300 (450 - 300 - 50
    // is 100, the scrollport's end).
    for (int i = 0; i < 7; i++)
    {
        CHECK(Key(context, root, mui_codeArrowDown, 0) && Same(muiFocus_Get(context, 0), items[2]),
              "far: a line");
    }
    CHECK(Y(context, s) == 330.0f, "seven lines, from 50 to 330");
    CHECK(Key(context, root, mui_codeArrowDown, 0) && Same(muiFocus_Get(context, 0), items[3]) &&
              Y(context, s) == 400.0f,
          "then the focus, into view");
    // At its end with nothing below inside, the focus leaves the list.
    CHECK(Key(context, root, mui_codeArrowDown, 0) && Same(muiFocus_Get(context, 0), below),
          "out of the list");
    // Up from below: the list is outside the focus's scrollers, so plain
    // navigation; then up inside, far again.
    CHECK(Key(context, root, mui_codeArrowUp, 0) && Same(muiFocus_Get(context, 0), items[3]),
          "back in");
    CHECK(Key(context, root, mui_codeArrowUp, 0) && Same(muiFocus_Get(context, 0), items[3]) &&
              Y(context, s) == 360.0f,
          "far above: a line up");
    // Across, a vertical list does not scroll: plain navigation, nothing
    // there.
    CHECK(!Key(context, root, mui_codeArrowRight, 0), "across: nothing");
    // A modifier leaves it to the game.
    CHECK(!Key(context, root, mui_codeArrowUp, mui_modControl) && Y(context, s) == 360.0f,
          "with Control");
    // A gamepad's directions follow the same rule.
    const muiNavigationEvent up = {0, mui_navigateUp, 0};
    bool handled = false;
    CHECK(muiNavigationInput(context, root, &up, &handled) == mui_success && handled &&
              Y(context, s) == 320.0f,
          "navigation up: a line");
    // A link out of the list is followed at once.
    CHECK(muiNode_SetNeighbor(context, items[3], mui_directionUp, below) == mui_success &&
              Key(context, root, mui_codeArrowUp, 0) && Same(muiFocus_Get(context, 0), below),
          "a link out");
    muiDestroyContext(context);
}

static void TestPages(void)
{
    // Nine children: s's limit is 350, a page 87.5.
    Scene scene;
    MakeScene(&scene, false, 9);
    muiContext* context = scene.context;
    muiNodeId root = scene.root;
    CHECK(!Key(context, root, mui_codePageDown, 0), "no focus, a root that does not scroll");
    Focusable(context, scene.items[0]);
    Layout(context, root, 0);
    CHECK(muiFocus_Set(context, 0, scene.items[0], mui_focusByCode) == mui_success, "focus");
    CHECK(Key(context, root, mui_codePageDown, 0) && Y(context, scene.s) == 87.5f, "page down");
    CHECK(Key(context, root, mui_codeSpace, 0) && Y(context, scene.s) == 175.0f, "space");
    CHECK(!Key(context, root, mui_codeSpace, mui_modAlt) && Y(context, scene.s) == 175.0f,
          "space with Alt: the game's");
    CHECK(Key(context, root, mui_codeSpace, mui_modShift) && Y(context, scene.s) == 87.5f,
          "shift space");
    CHECK(Key(context, root, mui_codePageUp, 0) && Y(context, scene.s) == 0.0f, "page up");
    CHECK(!Key(context, root, mui_codePageUp, 0) && !Key(context, root, mui_codeHome, 0),
          "at the top: not handled");
    CHECK(Key(context, root, mui_codeEnd, 0) && Y(context, scene.s) == 350.0f, "end");
    CHECK(!Key(context, root, mui_codeEnd, 0) && !Key(context, root, mui_codePageDown, 0) &&
              !Key(context, root, mui_codeSpace, 0),
          "at the end: not handled");
    CHECK(Key(context, root, mui_codeHome, 0) && Y(context, scene.s) == 0.0f, "home");
    CHECK(!Key(context, root, mui_codePageDown, mui_modControl) &&
              !Key(context, root, mui_codeSpace, mui_modAlt) &&
              !Key(context, root, mui_codeEnd, mui_modShift) && Y(context, scene.s) == 0.0f,
          "modifiers: the game's");
    // A rule's page.
    muiScrollRule rule = muiDefaultScrollRule();
    rule.easeNs = 0;
    rule.pageFraction = 0.5f;
    rule.lineStep = 7.0f;
    CHECK(muiSetScrollRule(context, &rule) == mui_success &&
              Key(context, root, mui_codePageDown, 0) && Y(context, scene.s) == 50.0f,
          "half a page");
    CHECK(X(context, scene.s) == 0.0f, "across unmoved");
    rule.pageFraction = 0.0f;
    CHECK(muiSetScrollRule(context, &rule) == mui_errorInvalid, "no page");
    rule.pageFraction = 1.5f;
    CHECK(muiSetScrollRule(context, &rule) == mui_errorInvalid, "more than a page");
    rule.pageFraction = 1.0f;
    rule.lineStep = -1.0f;
    CHECK(muiSetScrollRule(context, &rule) == mui_errorInvalid, "a negative line");
    muiDestroyContext(context);
}

static void TestEaseMore(void)
{
    // The list follows a step as it eases; scrolling into view stops it;
    // a jump after reduced motion stops it too.
    Scene scene;
    MakeScene(&scene, true, 5);
    muiContext* context = scene.context;
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, scene.root, &input) == mui_success, "drawn");
    CHECK(Turn(context, scene.root, 0, -1.0f), "down");
    Layout(context, scene.root, 75 * MS);
    CHECK(muiBuildDrawList(context, scene.root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.transforms[1].f == -88.0f,
          "drawn as it eases, at a whole pixel");
    CHECK(muiNode_ScrollIntoView(context, scene.items[0]) == mui_success &&
              Y(context, scene.s) == 0.0f,
          "into view");
    Layout(context, scene.root, 150 * MS);
    CHECK(Y(context, scene.s) == 0.0f && !muiIsUpdatePending(context, scene.root),
          "the step stopped");
    CHECK(Turn(context, scene.root, 200 * MS, -1.0f), "down again");
    muiEnvironment environment = muiDefaultEnvironment();
    environment.reducedMotion = true;
    CHECK(muiSetContextEnvironment(context, &environment) == mui_success &&
              Turn(context, scene.root, 201 * MS, -1.0f) && Y(context, scene.s) == 150.0f,
          "jumped past the step");
    Layout(context, scene.root, 202 * MS);
    CHECK(Y(context, scene.s) == 150.0f, "the step gone");
    muiDestroyContext(context);
    // A fraction across with a whole detent down applies at once.
    context = MakeContext(true);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiNodeId both = Sized(context, root, 100.0f, 100.0f);
    Column(context, both, mui_scrollBoth);
    (void)Sized(context, both, 300.0f, 300.0f);
    Layout(context, root, 0);
    const muiWheelEvent event = {0, 50.0f, 50.0f, 0.5f, -1.0f, 0, 0};
    bool handled = false;
    CHECK(muiWheelInput(context, root, &event, &handled) == mui_success && handled &&
              X(context, both) == 50.0f && Y(context, both) == 100.0f,
          "a fraction across: at once");
    muiDestroyContext(context);
}

static void TestTableReused(void)
{
    // Eight steps easing on lists that then go free their entries: a ninth
    // eases.
    muiContext* context = MakeContext(true);
    muiNodeId root = Sized(context, s_nullNode, 900.0f, 100.0f);
    muiNodeId lists[9];
    for (int i = 0; i < 9; i++)
    {
        lists[i] = Sized(context, root, 100.0f, 100.0f);
        Column(context, lists[i], mui_scrollVertical);
        (void)Sized(context, lists[i], 100.0f, 300.0f);
    }
    Layout(context, root, 0);
    for (int i = 0; i < 8; i++)
    {
        const muiWheelEvent event = {0, 100.0f * (float)i + 50.0f, 50.0f, 0.0f, -1.0f, 0, 0};
        bool handled = false;
        CHECK(muiWheelInput(context, root, &event, &handled) == mui_success && handled, "wheel");
        CHECK(muiDestroyNode(context, lists[i]) == mui_success, "gone");
    }
    Layout(context, root, 1);
    // The others gone, it is first in the row.
    const muiWheelEvent last = {1, 50.0f, 50.0f, 0.0f, -1.0f, 0, 0};
    bool handled = false;
    CHECK(muiWheelInput(context, root, &last, &handled) == mui_success && handled &&
              Y(context, lists[8]) == 0.0f && muiIsUpdatePending(context, root),
          "the ninth eases");
    muiDestroyContext(context);
}

static void TestBounds(void)
{
    // The input root and a layer root bound the scroll container a key
    // finds: under g, inside s, page keys find nothing.
    muiContext* context = MakeContext(false);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiNodeId s = Sized(context, root, 200.0f, 100.0f);
    Column(context, s, mui_scrollVertical);
    muiNodeId g = Sized(context, s, 200.0f, 400.0f);
    Column(context, g, mui_scrollNone);
    muiNodeId a = Sized(context, g, 200.0f, 50.0f);
    Focusable(context, a);
    muiNodeId b = Sized(context, g, 200.0f, 50.0f);
    Focusable(context, b);
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.layer = mui_layerActivation;
    CHECK(muiNode_SetInteractionValues(context, b, &values, MUI_PROPERTY_BIT(mui_propertyLayer)) ==
              mui_success,
          "a layer");
    Layout(context, root, 0);
    CHECK(muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success, "focus");
    CHECK(!Key(context, g, mui_codePageDown, 0) && Y(context, s) == 0.0f, "not past the root");
    CHECK(Key(context, root, mui_codePageDown, 0) && Y(context, s) == 87.5f, "from the root");
    CHECK(muiFocus_Set(context, 0, b, mui_focusByCode) == mui_success &&
              !Key(context, root, mui_codePageDown, 0) && Y(context, s) == 87.5f,
          "not past a layer");
    muiDestroyContext(context);
    // In a row scrolling across inside s, a page key finds s.
    context = MakeContext(false);
    root = Sized(context, s_nullNode, 300.0f, 300.0f);
    s = Sized(context, root, 200.0f, 100.0f);
    Column(context, s, mui_scrollVertical);
    muiNodeId row = Sized(context, s, 200.0f, 50.0f);
    muiLayoutStyle across = muiDefaultLayoutStyle();
    across.scrollAxes = mui_scrollHorizontal;
    SetLayout(context, row, &across, MUI_PROPERTY_BIT(mui_propertyScrollAxes));
    a = Sized(context, row, 400.0f, 50.0f);
    Focusable(context, a);
    (void)Sized(context, s, 200.0f, 400.0f);
    Layout(context, root, 0);
    CHECK(muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success &&
              Key(context, root, mui_codePageDown, 0) && Y(context, s) == 87.5f &&
              X(context, row) == 0.0f,
          "past the row");
    muiDestroyContext(context);
}

static void TestNearAndAcross(void)
{
    // A candidate 40 past the scrollport is near (half is 50); a row
    // steps left and right.
    muiContext* context = MakeContext(false);
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    Column(context, root, mui_scrollNone);
    muiNodeId s = Sized(context, root, 200.0f, 100.0f);
    Column(context, s, mui_scrollVertical);
    muiNodeId a = Sized(context, s, 200.0f, 50.0f);
    Focusable(context, a);
    (void)Sized(context, s, 200.0f, 90.0f);
    muiNodeId b = Sized(context, s, 200.0f, 50.0f);
    Focusable(context, b);
    muiNodeId row = Sized(context, root, 100.0f, 50.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollHorizontal;
    SetLayout(context, row, &style, MUI_PROPERTY_BIT(mui_propertyScrollAxes));
    muiNodeId left = Sized(context, row, 50.0f, 50.0f);
    Focusable(context, left);
    (void)Sized(context, row, 300.0f, 50.0f);
    muiNodeId right = Sized(context, row, 50.0f, 50.0f);
    Focusable(context, right);
    Layout(context, root, 0);
    CHECK(muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success &&
              Key(context, root, mui_codeArrowDown, 0) && Same(muiFocus_Get(context, 0), b),
          "near enough");
    CHECK(muiFocus_Set(context, 0, left, mui_focusByCode) == mui_success &&
              Key(context, root, mui_codeArrowRight, 0) && Same(muiFocus_Get(context, 0), left) &&
              X(context, row) == 40.0f && Y(context, row) == 0.0f,
          "a line right");
    CHECK(Key(context, root, mui_codeArrowLeft, 0) && X(context, row) == 0.0f, "a line left");
    muiDestroyContext(context);
}

static void TestPageMidStep(void)
{
    // Home while a page eases down goes to the top.
    Scene scene;
    MakeScene(&scene, true, 9);
    muiContext* context = scene.context;
    Focusable(context, scene.items[0]);
    Layout(context, scene.root, 0);
    CHECK(muiFocus_Set(context, 0, scene.items[0], mui_focusByCode) == mui_success &&
              Key(context, scene.root, mui_codePageDown, 0) && Y(context, scene.s) == 0.0f,
          "easing");
    CHECK(Key(context, scene.root, mui_codeHome, 0), "home before any layout");
    Layout(context, scene.root, 300 * MS);
    CHECK(Y(context, scene.s) == 0.0f, "at the top");
    muiScrollRule rule = muiDefaultScrollRule();
    rule.lineStep = NAN;
    CHECK(muiSetScrollRule(context, &rule) == mui_errorInvalid, "a line not finite");
    muiDestroyContext(context);
}

int main(void)
{
    TestEase();
    TestEaseEnds();
    TestFull();
    TestArrows();
    TestPages();
    TestEaseMore();
    TestTableReused();
    TestBounds();
    TestNearAndAcross();
    TestPageMidStep();
    return s_failures == 0 ? 0 : 1;
}
