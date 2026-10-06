// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Transitions: their specs and checks, timed and spring motion against
// host time, reversal, layers, reduced motion, direct writes, limits
// (record mui-0004).

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/transition.h"

#include <math.h>

static const muiNodeId s_nullNode = {0, 0};
static const muiStyleId s_nullStyle = {0, 0};
static const muiTransitionId s_nullTransition = {0, 0};

#define WIDTH         MUI_PROPERTY_BIT(mui_propertyWidth)
#define PADDING_START MUI_PROPERTY_BIT(mui_propertyPaddingStart)
#define MS            1000000ull
// A start time well away from 0.
#define T0 (1000 * MS)

static muiContext* MakeContextWith(muiLimits limits)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits = limits;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create context");
    return context;
}

static muiContext* MakeContext(void)
{
    return MakeContextWith(muiDefaultContextDef().limits);
}

static muiNodeId MakeNode(muiContext* context)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "create node");
    return node;
}

static muiStyleId MakeStyle(muiContext* context)
{
    muiStyleId style = s_nullStyle;
    CHECK(muiCreateStyle(context, &style) == mui_success, "create style");
    return style;
}

static muiDimension Length(float offset)
{
    return (muiDimension){0.0f, offset, mui_dimensionValue};
}

static void SetWidth(muiContext* context, muiStyleId style, muiVariant variant, muiDimension width)
{
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.sizing.width = width;
    CHECK(muiStyle_SetLayoutValues(context, style, variant, &values, WIDTH) == mui_success,
          "width");
}

static muiTransitionId MakeTimed(muiContext* context, uint64_t durationNs, muiEasing easing,
                                 uint64_t delayNs)
{
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = durationNs;
    def.easing = easing;
    def.delayNs = delayNs;
    muiTransitionId transition = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &transition) == mui_success, "create transition");
    return transition;
}

static muiTransitionId MakeSpring(muiContext* context, float frequency, float dampingRatio)
{
    muiTransitionDef def = muiDefaultTransitionDef();
    def.kind = mui_transitionSpring;
    def.frequency = frequency;
    def.dampingRatio = dampingRatio;
    muiTransitionId transition = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &transition) == mui_success, "create spring");
    return transition;
}

static void Bind(muiContext* context, muiStyleId style, muiVariant variant,
                 muiTransitionId transition, muiPropertyMask mask)
{
    CHECK(muiStyle_SetTransition(context, style, variant, transition, mui_groupLayout, mask) ==
              mui_success,
          "bind");
}

// The node's width after laying it out at timeNs.
static float WidthAt(muiContext* context, muiNodeId node, uint64_t timeNs)
{
    muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, timeNs, NULL};
    CHECK(muiComputeLayout(context, node, &input) == mui_success, "compute");
    return muiNode_GetRect(context, node).width;
}

// A root with one class: width 100, 200 when hovered.
typedef struct Scene
{
    muiContext* context;
    muiNodeId node;
    muiStyleId style;
} Scene;

static Scene MakeScene(muiLimits limits)
{
    Scene scene = {.context = MakeContextWith(limits)};
    scene.node = MakeNode(scene.context);
    scene.style = MakeStyle(scene.context);
    SetWidth(scene.context, scene.style, mui_variantBase, Length(100.0f));
    SetWidth(scene.context, scene.style, mui_variantHovered, Length(200.0f));
    CHECK(muiNode_SetClasses(scene.context, scene.node, &scene.style, 1) == mui_success, "class");
    CHECK(WidthAt(scene.context, scene.node, T0) == 100.0f, "starts at 100");
    return scene;
}

static void Hover(const Scene* scene, bool on)
{
    CHECK(muiNode_SetStates(scene->context, scene->node, on ? mui_stateHovered : 0) == mui_success,
          "states");
}

static void TestDefaultsAndChecks(void)
{
    muiTransitionDef def = muiDefaultTransitionDef();
    CHECK(def.kind == mui_transitionTimed && def.durationNs == 250 * MS, "timed, 250 ms");
    CHECK(def.easing == mui_easingEase && def.delayNs == 0, "ease, no delay");
    CHECK(def.frequency == 2.0f && def.dampingRatio == 1.0f, "a critical 2 Hz spring");
    muiContext* context = MakeContextWith((muiLimits){.nodes = 1, .transitions = 1});
    muiTransitionDef bad[8];
    for (int i = 0; i < 8; i++)
    {
        bad[i] = muiDefaultTransitionDef();
    }
    bad[0].cookie = 0;
    bad[1].kind = 2;
    bad[2].easing = 6;
    bad[3].bezier[0] = 1.5f;
    bad[4].bezier[3] = NAN;
    bad[5].frequency = 0.0f;
    bad[6].dampingRatio = 0.0f;
    bad[7].frequency = INFINITY;
    muiTransitionId transition = {9, 9};
    for (int i = 0; i < 8; i++)
    {
        CHECK(muiCreateTransition(context, &bad[i], &transition) == mui_errorInvalid, "refused");
        CHECK(transition.index1 == 0, "null on failure");
    }
    CHECK(muiCreateTransition(context, NULL, &transition) == mui_errorInvalid, "no def");
    CHECK(muiCreateTransition(context, &def, NULL) == mui_errorInvalid, "no out");
    CHECK(muiCreateTransition(NULL, &def, &transition) == mui_errorInvalid, "no context");
    def.bezier[1] = -2.0f;
    def.bezier[3] = 3.0f;
    def.easing = mui_easingCubicBezier;
    CHECK(muiCreateTransition(context, &def, &transition) == mui_success, "y may leave [0, 1]");
    muiTransitionId second = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &second) == mui_errorCapacity, "the limit");
    CHECK(muiDestroyTransition(context, transition) == mui_success, "destroy");
    CHECK(muiDestroyTransition(context, transition) == mui_errorStale, "once");
    CHECK(muiDestroyTransition(context, s_nullTransition) == mui_errorInvalid, "null id");
    CHECK(muiDestroyTransition(NULL, transition) == mui_errorInvalid, "no context");
    muiDestroyContext(context);
}

static void TestBindingsAreCheckedAndRead(void)
{
    muiContext* context = MakeContext();
    muiStyleId style = MakeStyle(context);
    muiTransitionId specs[MUI_MAX_VARIANT_TRANSITIONS + 1];
    for (int i = 0; i <= MUI_MAX_VARIANT_TRANSITIONS; i++)
    {
        specs[i] = MakeTimed(context, (uint64_t)(i + 1) * MS, mui_easingLinear, 0);
    }
    for (int i = 0; i < MUI_MAX_VARIANT_TRANSITIONS; i++)
    {
        Bind(context, style, mui_variantPressed, specs[i], MUI_PROPERTY_BIT(i));
    }
    muiPropertyMask fifth = MUI_PROPERTY_BIT(MUI_MAX_VARIANT_TRANSITIONS);
    CHECK(muiStyle_SetTransition(context, style, mui_variantPressed,
                                 specs[MUI_MAX_VARIANT_TRANSITIONS], mui_groupLayout,
                                 fifth) == mui_errorCapacity,
          "a fifth spec");
    // Giving the first spec's only property to the second frees a place.
    Bind(context, style, mui_variantPressed, specs[1], MUI_PROPERTY_BIT(0));
    Bind(context, style, mui_variantPressed, specs[MUI_MAX_VARIANT_TRANSITIONS], fifth);
    muiTransitionId read = s_nullTransition;
    CHECK(muiStyle_GetTransition(context, style, mui_variantPressed, 0, &read) == mui_success &&
              read.index1 == specs[1].index1,
          "the property's spec now");
    CHECK(muiStyle_GetTransition(context, style, mui_variantBase, 0, &read) == mui_success &&
              read.index1 == 0,
          "a variant naming none");
    Bind(context, style, mui_variantPressed, s_nullTransition, MUI_PROPERTY_BIT(0));
    CHECK(muiStyle_GetTransition(context, style, mui_variantPressed, 0, &read) == mui_success &&
              read.index1 == 0,
          "taken away");
    CHECK(muiStyle_SetTransition(context, style, mui_variantCondition0, specs[0], mui_groupLayout,
                                 WIDTH) == mui_errorInvalid,
          "a condition the class does not have");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, specs[0], mui_groupLayout,
                                 MUI_PROPERTY_BIT(mui_propertyScrollAxes + 1)) == mui_errorInvalid,
          "an unknown property");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, specs[0], 4, WIDTH) ==
              mui_errorInvalid,
          "an unknown group");
    CHECK(muiStyle_SetTransition(context, s_nullStyle, mui_variantBase, specs[0], mui_groupLayout,
                                 WIDTH) == mui_errorInvalid,
          "null class");
    CHECK(muiStyle_SetTransition(NULL, style, mui_variantBase, specs[0], mui_groupLayout, WIDTH) ==
              mui_errorInvalid,
          "no context");
    CHECK(muiDestroyTransition(context, specs[0]) == mui_success, "destroy a spec");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, specs[0], mui_groupLayout,
                                 WIDTH) == mui_errorStale,
          "a gone spec");
    CHECK(muiStyle_GetTransition(context, style, mui_variantCondition0, 0, &read) ==
              mui_errorInvalid,
          "read a missing condition");
    CHECK(muiStyle_GetTransition(context, style, mui_variantBase,
                                 (muiProperty)(mui_propertyScrollAxes + 1),
                                 &read) == mui_errorInvalid,
          "read an unknown property");
    CHECK(muiStyle_GetTransition(context, style, mui_variantBase, 0, NULL) == mui_errorInvalid,
          "no out");
    CHECK(muiStyle_GetTransition(NULL, style, mui_variantBase, 0, &read) == mui_errorInvalid,
          "no context read");
    CHECK(muiDestroyStyle(context, style) == mui_success, "destroy the class");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, specs[1], mui_groupLayout,
                                 WIDTH) == mui_errorStale,
          "a gone class");
    CHECK(muiStyle_GetTransition(context, style, mui_variantBase, 0, &read) == mui_errorStale,
          "read a gone class");
    muiDestroyContext(context);
}

static void TestBindingsHoldSetsAlone(void)
{
    muiContext* context =
        MakeContextWith((muiLimits){.nodes = 1, .styles = 1, .propertySets = 1, .transitions = 1});
    muiStyleId style = MakeStyle(context);
    muiTransitionId spec = MakeTimed(context, MS, mui_easingLinear, 0);
    Bind(context, style, mui_variantHovered, spec, WIDTH);
    muiLayoutStyle values = muiDefaultLayoutStyle();
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, WIDTH) ==
              mui_errorCapacity,
          "a binding alone holds the only set");
    CHECK(muiStyle_SetTransition(context, style, mui_variantPressed, spec, mui_groupLayout,
                                 WIDTH) == mui_errorCapacity,
          "so another variant cannot bind");
    Bind(context, style, mui_variantHovered, s_nullTransition, WIDTH);
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, WIDTH) == mui_success,
          "unbinding the last gave the set back");
    Bind(context, style, mui_variantBase, s_nullTransition, WIDTH);
    CHECK(muiStyle_ResetProperties(context, style, mui_variantBase, mui_groupLayout, WIDTH) ==
              mui_success,
          "reset");
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantPressed, &values, WIDTH) ==
              mui_success,
          "no binding and no value gave it back");
    muiDestroyContext(context);
}

static void TestTimedTransitionFollowsItsCurve(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    muiTransitionId linear = MakeTimed(scene.context, 100 * MS, mui_easingLinear, 0);
    Bind(scene.context, scene.style, mui_variantHovered, linear, WIDTH);
    Hover(&scene, true);
    CHECK(WidthAt(scene.context, scene.node, T0) == 100.0f, "no time has passed");
    CHECK(muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth), "moving");
    CHECK(muiIsUpdatePending(scene.context, scene.node), "a run is owed while it moves");
    CHECK(WidthAt(scene.context, scene.node, T0 + 25 * MS) == 125.0f, "a quarter of the way");
    CHECK(WidthAt(scene.context, scene.node, T0 + 10 * MS) == 125.0f,
          "time going back counts as none");
    CHECK(WidthAt(scene.context, scene.node, T0 + 100 * MS) == 200.0f, "there");
    CHECK(!muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth), "done");
    CHECK(!muiIsUpdatePending(scene.context, scene.node), "nothing owed");
    // Leaving hover, the base names no spec: back at once.
    Hover(&scene, false);
    CHECK(WidthAt(scene.context, scene.node, T0 + 200 * MS) == 100.0f, "the after-change spec");
    CHECK(!muiNode_IsTransitioning(NULL, scene.node, mui_propertyWidth) &&
              !muiNode_IsTransitioning(scene.context, s_nullNode, mui_propertyWidth) &&
              !muiNode_IsTransitioning(scene.context, scene.node,
                                       (muiProperty)(mui_propertyScrollAxes + 1)),
          "no transition to read");
    muiDestroyContext(scene.context);
}

static void TestEasingsShapeTheWay(void)
{
    const struct
    {
        muiEasing easing;
        int sign;
    } cases[] = {
        {mui_easingEaseIn, -1},
        {mui_easingEaseOut, 1},
        {mui_easingEase, 1},
        {mui_easingEaseInOut, 0},
    };
    for (int i = 0; i < 4; i++)
    {
        Scene scene = MakeScene(muiDefaultContextDef().limits);
        Bind(scene.context, scene.style, mui_variantHovered,
             MakeTimed(scene.context, 100 * MS, cases[i].easing, 0), WIDTH);
        Hover(&scene, true);
        (void)WidthAt(scene.context, scene.node, T0);
        float half = WidthAt(scene.context, scene.node, T0 + 50 * MS) - 150.0f;
        CHECK(cases[i].sign == 0 ? fabsf(half) < 0.01f : half * (float)cases[i].sign > 1.0f,
              "behind, ahead or even with linear at half time");
        muiDestroyContext(scene.context);
    }
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = 100 * MS;
    def.easing = mui_easingCubicBezier;
    def.bezier[0] = 0.0f;
    def.bezier[1] = 1.0f;
    def.bezier[2] = 0.0f;
    def.bezier[3] = 1.0f;
    muiTransitionId custom = s_nullTransition;
    CHECK(muiCreateTransition(scene.context, &def, &custom) == mui_success, "custom curve");
    Bind(scene.context, scene.style, mui_variantHovered, custom, WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    CHECK(WidthAt(scene.context, scene.node, T0 + 10 * MS) > 150.0f, "its own control points");
    muiDestroyContext(scene.context);
}

static void TestDelayHoldsTheStart(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    Bind(scene.context, scene.style, mui_variantHovered,
         MakeTimed(scene.context, 100 * MS, mui_easingLinear, 50 * MS), WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    CHECK(WidthAt(scene.context, scene.node, T0 + 40 * MS) == 100.0f, "waiting");
    CHECK(WidthAt(scene.context, scene.node, T0 + 100 * MS) == 150.0f, "half after the delay");
    CHECK(WidthAt(scene.context, scene.node, T0 + 150 * MS) == 200.0f, "there");
    muiDestroyContext(scene.context);
}

static void TestReversalIsShortened(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    // In the base, so it is the after-change spec both ways.
    Bind(scene.context, scene.style, mui_variantBase,
         MakeTimed(scene.context, 100 * MS, mui_easingLinear, 0), WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    CHECK(WidthAt(scene.context, scene.node, T0 + 30 * MS) == 130.0f, "30% of the way");
    Hover(&scene, false);
    CHECK(WidthAt(scene.context, scene.node, T0 + 30 * MS) == 130.0f, "turns back from there");
    CHECK(WidthAt(scene.context, scene.node, T0 + 45 * MS) == 115.0f,
          "over 30 ms, the share it had covered");
    CHECK(WidthAt(scene.context, scene.node, T0 + 60 * MS) == 100.0f, "back");
    CHECK(!muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth), "done");
    // A change to a third value is not a reversal.
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0 + 100 * MS);
    CHECK(WidthAt(scene.context, scene.node, T0 + 150 * MS) == 150.0f, "half way up");
    SetWidth(scene.context, scene.style, mui_variantHovered, Length(250.0f));
    CHECK(WidthAt(scene.context, scene.node, T0 + 150 * MS) == 150.0f, "retargeted in place");
    CHECK(WidthAt(scene.context, scene.node, T0 + 200 * MS) == 200.0f,
          "a full duration from where it was");
    muiDestroyContext(scene.context);
}

static void TestSpringKeepsItsSpeed(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    Bind(scene.context, scene.style, mui_variantBase, MakeSpring(scene.context, 2.0f, 1.0f), WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    float before = WidthAt(scene.context, scene.node, T0 + 80 * MS);
    CHECK(before > 100.0f && before < 200.0f, "on its way up");
    Hover(&scene, false);
    CHECK(WidthAt(scene.context, scene.node, T0 + 80 * MS) == before, "retargeted in place");
    CHECK(WidthAt(scene.context, scene.node, T0 + 90 * MS) > before,
          "still rising: it keeps its speed");
    uint64_t t = T0 + 100 * MS;
    while (muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth) &&
           t < T0 + 10000 * MS)
    {
        (void)WidthAt(scene.context, scene.node, t);
        t += 16 * MS;
    }
    CHECK(t < T0 + 5000 * MS, "comes to rest");
    CHECK(muiNode_GetRect(scene.context, scene.node).width == 100.0f, "exactly on its target");
    muiDestroyContext(scene.context);
}

static void TestSpringsStayInRange(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.padding.start = 40.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, PADDING_START) ==
              mui_success,
          "padding");
    values.padding.start = 0.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantHovered, &values, PADDING_START) ==
              mui_success,
          "none when hovered");
    values.placement.anchorX = 1.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantHovered, &values,
                                   MUI_PROPERTY_BIT(mui_propertyAnchorX)) == mui_success,
          "anchor 1 when hovered");
    Bind(context, style, mui_variantBase, MakeSpring(context, 3.0f, 0.2f),
         PADDING_START | MUI_PROPERTY_BIT(mui_propertyAnchorX));
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    (void)WidthAt(context, node, T0);
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    bool negative = false;
    for (uint64_t t = T0; t < T0 + 3000 * MS; t += 5 * MS)
    {
        (void)WidthAt(context, node, t);
        muiLayoutStyle read;
        CHECK(muiNode_GetLayoutStyle(context, node, &read) == mui_success, "read");
        negative = negative || read.padding.start < 0.0f || read.placement.anchorX > 1.0f;
    }
    CHECK(!negative, "overshoots stop at the bounds: 0 for a length, 1 for an anchor");
    muiDestroyContext(context);
}

static void TestReducedMotionAndDirectWritesApplyAtOnce(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    Bind(scene.context, scene.style, mui_variantBase,
         MakeTimed(scene.context, 100 * MS, mui_easingLinear, 0), WIDTH);
    muiEnvironment environment = muiDefaultEnvironment();
    environment.reducedMotion = true;
    CHECK(muiSetContextEnvironment(scene.context, &environment) == mui_success, "reduce");
    Hover(&scene, true);
    CHECK(WidthAt(scene.context, scene.node, T0) == 200.0f, "at once");
    environment.reducedMotion = false;
    CHECK(muiSetContextEnvironment(scene.context, &environment) == mui_success, "full motion");
    Hover(&scene, false);
    CHECK(WidthAt(scene.context, scene.node, T0) == 200.0f, "moving again");
    environment.reducedMotion = true;
    CHECK(muiSetContextEnvironment(scene.context, &environment) == mui_success, "reduce again");
    CHECK(WidthAt(scene.context, scene.node, T0 + MS) == 100.0f, "a running one ends");
    environment.reducedMotion = false;
    CHECK(muiSetContextEnvironment(scene.context, &environment) == mui_success, "full again");
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0 + 10 * MS);
    CHECK(WidthAt(scene.context, scene.node, T0 + 60 * MS) == 150.0f, "moving");
    muiLayoutStyle direct = muiDefaultLayoutStyle();
    direct.sizing.width = Length(300.0f);
    CHECK(muiNode_SetLayoutValues(scene.context, scene.node, &direct, WIDTH) == mui_success,
          "direct");
    CHECK(!muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth),
          "a direct write stops it");
    CHECK(WidthAt(scene.context, scene.node, T0 + 70 * MS) == 300.0f, "and applies at once");
    muiDestroyContext(scene.context);
}

static void TestOnlyMovableValuesMove(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    SetWidth(context, style, mui_variantHovered, Length(200.0f));
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.container.justify = mui_justifyCenter;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantHovered, &values,
                                   MUI_PROPERTY_BIT(mui_propertyJustify)) == mui_success,
          "justify");
    Bind(context, style, mui_variantBase, MakeTimed(context, 100 * MS, mui_easingLinear, 0),
         WIDTH | MUI_PROPERTY_BIT(mui_propertyJustify));
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    (void)WidthAt(context, node, T0);
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    CHECK(WidthAt(context, node, T0) == 200.0f, "from automatic, at once");
    CHECK(!muiNode_IsTransitioning(context, node, mui_propertyJustify), "an enumerator jumps");
    muiLayoutStyle read;
    CHECK(muiNode_GetLayoutStyle(context, node, &read) == mui_success &&
              read.container.justify == mui_justifyCenter,
          "to its new value");
    CHECK(muiNode_SetStates(context, node, 0) == mui_success, "unhover");
    CHECK(WidthAt(context, node, T0 + 10 * MS) == 0.0f, "to automatic, at once");
    CHECK(!muiNode_IsTransitioning(context, node, mui_propertyWidth), "nothing moves");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover again");
    CHECK(WidthAt(context, node, T0 + 20 * MS) == 200.0f, "from automatic, at once");
    // Both sides Scale+Offset: scale and offset both move.
    SetWidth(context, style, mui_variantHovered, (muiDimension){0.5f, 0.0f, mui_dimensionValue});
    muiNodeId root = MakeNode(context);
    CHECK(muiNode_InsertChild(context, root, node, s_nullNode) == mui_success, "insert");
    muiLayoutStyle wide = muiDefaultLayoutStyle();
    wide.sizing.width = Length(600.0f);
    CHECK(muiNode_SetLayoutValues(context, root, &wide, WIDTH) == mui_success, "root width");
    muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, T0 + 200 * MS, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "start");
    input.timeNs = T0 + 250 * MS;
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "half way");
    CHECK(muiNode_GetRect(context, node).width == 250.0f,
          "(0, 200) to (0.5, 0) of 600 halfway: 0.25 of 600 and 100");
    muiDestroyContext(context);
}

static void TestSpecsResolveThroughLayers(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    Bind(scene.context, scene.style, mui_variantBase,
         MakeTimed(scene.context, 1000 * MS, mui_easingLinear, 0), WIDTH);
    Bind(scene.context, scene.style, mui_variantHovered,
         MakeTimed(scene.context, 100 * MS, mui_easingLinear, 0), WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    CHECK(WidthAt(scene.context, scene.node, T0 + 50 * MS) == 150.0f, "hovering is fast");
    CHECK(WidthAt(scene.context, scene.node, T0 + 100 * MS) == 200.0f, "there");
    Hover(&scene, false);
    (void)WidthAt(scene.context, scene.node, T0 + 100 * MS);
    CHECK(WidthAt(scene.context, scene.node, T0 + 600 * MS) == 150.0f, "leaving is slow");
    muiDestroyContext(scene.context);
}

static void TestRecordLimitAndFreedRecords(void)
{
    muiLimits limits = muiDefaultContextDef().limits;
    limits.animations = 1;
    Scene scene = MakeScene(limits);
    muiTransitionId spec = MakeTimed(scene.context, 100 * MS, mui_easingLinear, 0);
    Bind(scene.context, scene.style, mui_variantBase, spec, WIDTH);
    muiNodeId other = MakeNode(scene.context);
    CHECK(muiNode_SetClasses(scene.context, other, &scene.style, 1) == mui_success, "class");
    (void)WidthAt(scene.context, other, T0);
    Hover(&scene, true);
    CHECK(muiNode_SetStates(scene.context, other, mui_stateHovered) == mui_success, "hover");
    (void)WidthAt(scene.context, scene.node, T0);
    CHECK(WidthAt(scene.context, other, T0) == 200.0f, "no record left: at once");
    CHECK(muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth), "the first moves");
    CHECK(muiDestroyNode(scene.context, scene.node) == mui_success, "destroy it");
    CHECK(!muiIsUpdatePending(scene.context, other), "a gone node's record is not owed");
    CHECK(muiNode_SetStates(scene.context, other, 0) == mui_success, "unhover");
    (void)WidthAt(scene.context, other, T0);
    CHECK(muiNode_IsTransitioning(scene.context, other, mui_propertyWidth),
          "the gone node's record was freed");
    // A destroyed spec: what runs, runs on; later changes are at once.
    CHECK(muiDestroyTransition(scene.context, spec) == mui_success, "destroy the spec");
    CHECK(WidthAt(scene.context, other, T0 + 50 * MS) == 150.0f, "runs on");
    CHECK(muiNode_SetStates(scene.context, other, mui_stateHovered) == mui_success, "hover");
    CHECK(WidthAt(scene.context, other, T0 + 60 * MS) == 200.0f, "no spec: at once");
    muiDestroyContext(scene.context);
}

static void TestDelayBeforeNoLength(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    Bind(scene.context, scene.style, mui_variantHovered,
         MakeTimed(scene.context, 0, mui_easingLinear, 50 * MS), WIDTH);
    Hover(&scene, true);
    CHECK(WidthAt(scene.context, scene.node, T0) == 100.0f, "the change");
    CHECK(WidthAt(scene.context, scene.node, T0 + 25 * MS) == 100.0f, "held through the delay");
    CHECK(WidthAt(scene.context, scene.node, T0 + 50 * MS) == 200.0f, "then at once");
    muiDestroyContext(scene.context);
}

static float PaddingAt(muiContext* context, muiNodeId node, uint64_t timeNs)
{
    (void)WidthAt(context, node, timeNs);
    muiLayoutStyle read;
    CHECK(muiNode_GetLayoutStyle(context, node, &read) == mui_success, "read");
    return read.padding.start;
}

// A number has one channel, so its reversals compare it alone.
static void TestReversalsKeepShortening(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.padding.start = 100.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, PADDING_START) ==
              mui_success,
          "100");
    values.padding.start = 200.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantHovered, &values, PADDING_START) ==
              mui_success,
          "200 when hovered");
    Bind(context, style, mui_variantBase, MakeTimed(context, 100 * MS, mui_easingLinear, 0),
         PADDING_START);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    CHECK(PaddingAt(context, node, T0) == 100.0f, "a node's first styling is at once");
    // A node first styled by direct writes alone has a value to move from.
    muiNodeId direct = MakeNode(context);
    muiLayoutStyle all = muiDefaultLayoutStyle();
    CHECK(muiNode_SetLayoutStyle(context, direct, &all) == mui_success, "all direct");
    (void)PaddingAt(context, direct, T0);
    CHECK(muiNode_SetClasses(context, direct, &style, 1) == mui_success, "class");
    CHECK(muiNode_ResetProperties(context, direct, mui_groupLayout, PADDING_START) == mui_success,
          "reset");
    CHECK(PaddingAt(context, direct, T0) == 0.0f &&
              muiNode_IsTransitioning(context, direct, mui_propertyPaddingStart),
          "so it moves");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    (void)PaddingAt(context, node, T0);
    (void)PaddingAt(context, node, T0 + 30 * MS);
    CHECK(muiNode_SetStates(context, node, 0) == mui_success, "unhover");
    (void)PaddingAt(context, node, T0 + 30 * MS);
    CHECK(PaddingAt(context, node, T0 + 40 * MS) == 120.0f, "a third of the way back");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover again");
    (void)PaddingAt(context, node, T0 + 40 * MS);
    // The second reversal covers 0.3 + 0.7 of a third: 80 ms from 120.
    CHECK(PaddingAt(context, node, T0 + 80 * MS) == 160.0f, "shortened again");
    muiDestroyContext(context);
}

static void TestOvershootingReversalTakesNoLonger(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = 100 * MS;
    def.easing = mui_easingCubicBezier;
    def.bezier[0] = 0.3f;
    def.bezier[1] = 1.8f;
    def.bezier[2] = 0.7f;
    def.bezier[3] = 1.8f;
    muiTransitionId overshoot = s_nullTransition;
    CHECK(muiCreateTransition(scene.context, &def, &overshoot) == mui_success, "overshoot");
    Bind(scene.context, scene.style, mui_variantBase, overshoot, WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    CHECK(WidthAt(scene.context, scene.node, T0 + 50 * MS) > 200.0f, "past its target");
    Hover(&scene, false);
    (void)WidthAt(scene.context, scene.node, T0 + 50 * MS);
    (void)WidthAt(scene.context, scene.node, T0 + 150 * MS);
    CHECK(!muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth),
          "a reversal never takes longer than the full duration");
    muiDestroyContext(scene.context);
}

static void TestLongSpringsAreCut(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    Bind(scene.context, scene.style, mui_variantBase, MakeSpring(scene.context, 0.5f, 0.001f),
         WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    (void)WidthAt(scene.context, scene.node, T0 + 30000 * MS);
    CHECK(muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth),
          "still ringing after 30 s");
    CHECK(WidthAt(scene.context, scene.node, T0 + 61000 * MS) == 200.0f, "put there after 60 s");
    CHECK(!muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth), "done");
    muiDestroyContext(scene.context);
}

static void TestSpringRetargetedInPlaceRests(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    Bind(scene.context, scene.style, mui_variantBase, MakeSpring(scene.context, 2.0f, 1.0f), WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    float at = WidthAt(scene.context, scene.node, T0 + 80 * MS);
    // A new target where it is now: only its speed moves it.
    SetWidth(scene.context, scene.style, mui_variantHovered, (muiDimension){0.0f, at, 1});
    (void)WidthAt(scene.context, scene.node, T0 + 80 * MS);
    CHECK(muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth),
          "at its target, but its speed carries it on");
    uint64_t t = T0 + 80 * MS;
    while (muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth) &&
           t < T0 + 10000 * MS)
    {
        (void)WidthAt(scene.context, scene.node, t);
        t += 16 * MS;
    }
    CHECK(t < T0 + 5000 * MS, "comes to rest by its speed's measure");
    muiDestroyContext(scene.context);
}

// A running width whose target becomes automatic stops there at once.
static void TestRunningToAutomaticStops(void)
{
    Scene scene = MakeScene(muiDefaultContextDef().limits);
    Bind(scene.context, scene.style, mui_variantBase,
         MakeTimed(scene.context, 100 * MS, mui_easingLinear, 0), WIDTH);
    Hover(&scene, true);
    (void)WidthAt(scene.context, scene.node, T0);
    CHECK(WidthAt(scene.context, scene.node, T0 + 50 * MS) == 150.0f, "running");
    SetWidth(scene.context, scene.style, mui_variantHovered,
             (muiDimension){0.0f, 0.0f, mui_dimensionAuto});
    CHECK(WidthAt(scene.context, scene.node, T0 + 50 * MS) == 0.0f, "automatic, at once");
    CHECK(!muiNode_IsTransitioning(scene.context, scene.node, mui_propertyWidth), "stopped");
    muiDestroyContext(scene.context);
}

int main(void)
{
    TestDefaultsAndChecks();
    TestBindingsAreCheckedAndRead();
    TestBindingsHoldSetsAlone();
    TestTimedTransitionFollowsItsCurve();
    TestEasingsShapeTheWay();
    TestDelayHoldsTheStart();
    TestReversalIsShortened();
    TestSpringKeepsItsSpeed();
    TestSpringsStayInRange();
    TestReducedMotionAndDirectWritesApplyAtOnce();
    TestOnlyMovableValuesMove();
    TestSpecsResolveThroughLayers();
    TestRecordLimitAndFreedRecords();
    TestDelayBeforeNoLength();
    TestReversalsKeepShortening();
    TestOvershootingReversalTakesNoLonger();
    TestLongSpringsAreCut();
    TestSpringRetargetedInPlaceRests();
    TestRunningToAutomaticStops();
    return s_failures == 0 ? 0 : 1;
}
