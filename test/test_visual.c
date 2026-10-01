// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Visual values: their defaults and checks, classes, variants,
// conditions, direct writes and transitions, and that a change to one
// marks paint, never layout (record mui-0004). White-box: it reads the
// tree's paint marks.

#include "context.h"
#include "test_harness.h"
#include "tree.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <math.h>

static const muiNodeId s_nullNode = {0, 0};
static const muiStyleId s_nullStyle = {0, 0};
static const muiTransitionId s_nullTransition = {0, 0};

#define BACKGROUND MUI_PROPERTY_BIT(mui_propertyBackground)
#define OPACITY    MUI_PROPERTY_BIT(mui_propertyOpacity)
#define RADIUS     MUI_PROPERTY_BIT(mui_propertyRadiusTopStart)
#define GRADIENT   MUI_PROPERTY_BIT(mui_propertyGradient)
#define WIDTH      MUI_PROPERTY_BIT(mui_propertyWidth)
#define MS         1000000ull
#define T0         (1000 * MS)

static const muiColor s_red = {1.0f, 0.0f, 0.0f, 1.0f};
static const muiColor s_blue = {0.0f, 0.0f, 1.0f, 1.0f};

static int s_measured = 0;

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)user;
    (void)nodeId;
    (void)hostKey;
    (void)width;
    (void)height;
    s_measured += 1;
    return (muiSize){40.0f, 10.0f};
}

static muiContext* MakeContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create context");
    return context;
}

// A node of host content, so that laying it out again measures it.
static muiNodeId MakeNode(muiContext* context)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "create node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, node, &layout, MUI_PROPERTY_BIT(mui_propertyContent)) ==
              mui_success,
          "host content");
    return node;
}

static muiStyleId MakeStyle(muiContext* context)
{
    muiStyleId style = s_nullStyle;
    CHECK(muiCreateStyle(context, &style) == mui_success, "create style");
    return style;
}

static void Layout(muiContext* context, muiNodeId node, uint64_t timeNs)
{
    const muiLayoutInput input = {400.0f, 300.0f, Measure, NULL, timeNs};
    CHECK(muiComputeLayout(context, node, &input) == mui_success, "layout");
}

static muiVisualStyle Read(const muiContext* context, muiNodeId node)
{
    muiVisualStyle values = muiDefaultVisualStyle();
    CHECK(muiNode_GetVisualStyle(context, node, &values) == mui_success, "read");
    return values;
}

static bool IsPaintMarked(muiContext* context, muiNodeId node)
{
    uint32_t slot = muiTreeResolve(&context->tree, node);
    return (muiTreeAt(&context->tree, slot)->dirty.request & mui_stagePaint) != 0;
}

// Drawing clears paint marks, and records the rectangles painted.
static void ClearPaint(muiContext* context, muiNodeId root)
{
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    CHECK(muiBuildDrawList(context, root, &input) == mui_success, "drawn");
}

static bool SameColor(muiColor a, muiColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void SetBackground(muiContext* context, muiStyleId style, muiVariant variant, muiColor color)
{
    muiVisualStyle values = muiDefaultVisualStyle();
    values.background = color;
    CHECK(muiStyle_SetVisualValues(context, style, variant, &values, BACKGROUND) == mui_success,
          "background");
}

static void TestDefaults(void)
{
    muiVisualStyle values = muiDefaultVisualStyle();
    const muiColor clear = {0.0f, 0.0f, 0.0f, 0.0f};
    const muiColor black = {0.0f, 0.0f, 0.0f, 1.0f};
    const muiColor white = {1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(SameColor(values.background, clear) && values.gradient.kind == mui_gradientNone &&
              values.gradient.stopCount == 0,
          "clear, no gradient");
    CHECK(values.radius.topStart.kind == mui_dimensionValue &&
              values.radius.bottomStart.offset == 0.0f && values.radius.topEnd.scale == 0.0f,
          "square corners, as values");
    CHECK(SameColor(values.borderColor.start, black) && SameColor(values.borderColor.bottom, black),
          "black borders");
    CHECK(SameColor(values.outerShadow.color, clear) && SameColor(values.innerShadow.color, clear),
          "no shadows");
    CHECK(values.image == 0 && SameColor(values.imageTint, white), "no image, white tint");
    CHECK(values.opacity == 1.0f && !values.clip, "opaque, not clipping");

    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiVisualStyle read = Read(context, node);
    CHECK(read.opacity == 1.0f && SameColor(read.imageTint, white) &&
              read.radius.bottomEnd.kind == mui_dimensionValue,
          "a new node has them");
    muiVisualStyle out;
    CHECK(muiNode_GetVisualStyle(NULL, node, &out) == mui_errorInvalid &&
              muiNode_GetVisualStyle(context, node, NULL) == mui_errorInvalid &&
              muiNode_GetVisualStyle(context, s_nullNode, &out) == mui_errorInvalid,
          "null arguments");
    CHECK(muiDestroyNode(context, node) == mui_success, "destroy");
    CHECK(muiNode_GetVisualStyle(context, node, &out) == mui_errorStale, "stale");
    muiDestroyContext(context);
}

static void TestClassValuesMarkPaintOnly(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    int measured = s_measured;
    ClearPaint(context, node);

    SetBackground(context, style, mui_variantBase, s_red);
    CHECK(muiIsUpdatePending(context, node), "a class edit restyles");
    Layout(context, node, T0);
    CHECK(SameColor(Read(context, node).background, s_red), "the class's background");
    CHECK(IsPaintMarked(context, node), "paint marked");
    CHECK(s_measured == measured, "not laid out again");

    ClearPaint(context, node);
    SetBackground(context, style, mui_variantBase, s_red);
    Layout(context, node, T0);
    CHECK(!IsPaintMarked(context, node), "the same value marks nothing");

    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, 100.0f, mui_dimensionValue};
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &layout, WIDTH) == mui_success,
          "width");
    Layout(context, node, T0);
    CHECK(s_measured > measured, "a layout value lays it out again");
    muiDestroyContext(context);
}

static void TestVariantsAndConditions(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    SetBackground(context, style, mui_variantBase, s_red);
    SetBackground(context, style, mui_variantHovered, s_blue);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    CHECK(SameColor(Read(context, node).background, s_red), "base");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    Layout(context, node, T0);
    CHECK(SameColor(Read(context, node).background, s_blue), "hovered");
    CHECK(muiNode_SetStates(context, node, 0) == mui_success, "unhover");
    Layout(context, node, T0);
    CHECK(SameColor(Read(context, node).background, s_red), "base again");

    // Narrow nodes are rounder: a condition on width, which a visual value
    // cannot change, may set one.
    muiCondition narrow = muiDefaultCondition();
    narrow.width = (muiRange){0.0f, 100.0f};
    muiVariant variant = mui_variantBase;
    CHECK(muiStyle_AddCondition(context, style, &narrow, &variant) == mui_success, "condition");
    muiVisualStyle values = muiDefaultVisualStyle();
    values.radius.topStart = (muiDimension){0.5f, 0.0f, mui_dimensionValue};
    CHECK(muiStyle_SetVisualValues(context, style, variant, &values, RADIUS) == mui_success,
          "conditional radius");
    Layout(context, node, T0);
    Layout(context, node, T0);
    CHECK(Read(context, node).radius.topStart.scale == 0.5f,
          "the root, 40 wide as its content, is rounder");
    muiDestroyContext(context);
}

static void CheckRefused(muiContext* context, muiStyleId style, muiNodeId node,
                         const muiVisualStyle* values, muiPropertyMask mask, const char* what)
{
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, values, mask) ==
              mui_errorInvalid,
          what);
    CHECK(muiNode_SetVisualValues(context, node, values, mask) == mui_errorInvalid, what);
}

static void TestChecks(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    const muiVisualStyle defaults = muiDefaultVisualStyle();
    muiVisualStyle v = defaults;

    v.background.r = 1.5f;
    CheckRefused(context, style, node, &v, BACKGROUND, "a component above 1");
    v.background.r = NAN;
    CheckRefused(context, style, node, &v, BACKGROUND, "a NaN component");
    v = defaults;
    v.background.a = 1.01f;
    CheckRefused(context, style, node, &v, BACKGROUND, "alpha above 1");
    v = defaults;
    v.opacity = -0.1f;
    CheckRefused(context, style, node, &v, OPACITY, "opacity below 0");
    v = defaults;
    v.radius.topStart = (muiDimension){0.0f, -1.0f, mui_dimensionValue};
    CheckRefused(context, style, node, &v, RADIUS, "a negative radius");
    v.radius.topStart = (muiDimension){-0.5f, 0.0f, mui_dimensionValue};
    CheckRefused(context, style, node, &v, RADIUS, "a negative scale");
    v.radius.topStart = (muiDimension){0.0f, 0.0f, mui_dimensionAuto};
    CheckRefused(context, style, node, &v, RADIUS, "an automatic radius");
    v = defaults;
    v.outerShadow.blur = -1.0f;
    CheckRefused(context, style, node, &v, MUI_PROPERTY_BIT(mui_propertyOuterShadow),
                 "a negative blur");
    v = defaults;
    v.innerShadow.offsetX = INFINITY;
    CheckRefused(context, style, node, &v, MUI_PROPERTY_BIT(mui_propertyInnerShadow),
                 "an infinite offset");
    v = defaults;
    v.innerShadow.offsetY = NAN;
    CheckRefused(context, style, node, &v, MUI_PROPERTY_BIT(mui_propertyInnerShadow),
                 "a NaN offset");
    v = defaults;
    v.outerShadow.spread = -INFINITY;
    CheckRefused(context, style, node, &v, MUI_PROPERTY_BIT(mui_propertyOuterShadow),
                 "an infinite spread");
    v = defaults;
    v.imageSlice.top = -2.0f;
    CheckRefused(context, style, node, &v, MUI_PROPERTY_BIT(mui_propertyImageSlice),
                 "a negative slice");
    v = defaults;
    v.imageSlice.bottom = NAN;
    CheckRefused(context, style, node, &v, MUI_PROPERTY_BIT(mui_propertyImageSlice),
                 "a NaN bottom slice");
    v = defaults;
    v.clip = true;
    unsigned char* flag = (unsigned char*)&v.clip;
    *flag = 2;
    CheckRefused(context, style, node, &v, MUI_PROPERTY_BIT(mui_propertyClip), "a flag of 2");

    const muiGradientStop black = {{0.0f, 0.0f, 0.0f, 1.0f}, 0.0f};
    const muiGradientStop white = {{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f};
    v = defaults;
    v.gradient = (muiGradient){mui_gradientLinear, 1, 90.0f, {black}};
    CheckRefused(context, style, node, &v, GRADIENT, "one stop");
    // Four stops in order and a fifth the struct has no room for; the
    // radius that follows it in memory would read as a stop in order.
    const muiGradientStop gray = {{0.5f, 0.5f, 0.5f, 1.0f}, 0.5f};
    v.gradient = (muiGradient){mui_gradientLinear, 5, 90.0f, {black, gray, gray, white}};
    v.radius.topEnd = (muiDimension){0.0f, 1.0f, mui_dimensionValue};
    CheckRefused(context, style, node, &v, GRADIENT, "five stops");
    v.radius = defaults.radius;
    const muiGradientStop bad = {{0.5f, 2.0f, 0.5f, 1.0f}, 0.5f};
    v.gradient = (muiGradient){mui_gradientLinear, 3, 90.0f, {black, bad, white}};
    CheckRefused(context, style, node, &v, GRADIENT, "a stop's color");
    v.gradient = (muiGradient){mui_gradientLinear, 2, 90.0f, {white, black}};
    CheckRefused(context, style, node, &v, GRADIENT, "stops out of order");
    v.gradient = (muiGradient){3, 2, 90.0f, {black, white}};
    CheckRefused(context, style, node, &v, GRADIENT, "an unknown kind");
    v.gradient = (muiGradient){mui_gradientNone, 2, 90.0f, {black, white}};
    CheckRefused(context, style, node, &v, GRADIENT, "none with stops");
    v.gradient = (muiGradient){mui_gradientLinear, 2, NAN, {black, white}};
    CheckRefused(context, style, node, &v, GRADIENT, "a NaN angle");

    CheckRefused(context, style, node, &defaults, MUI_PROPERTY_BIT(mui_propertyClip + 1),
                 "a bit past the visual group's properties");
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, NULL, BACKGROUND) ==
                  mui_errorInvalid &&
              muiNode_SetVisualValues(context, node, NULL, BACKGROUND) == mui_errorInvalid &&
              muiStyle_SetVisualValues(NULL, style, mui_variantBase, &defaults, BACKGROUND) ==
                  mui_errorInvalid &&
              muiNode_SetVisualValues(NULL, node, &defaults, BACKGROUND) == mui_errorInvalid,
          "null arguments");
    const muiLayoutStyle layout = muiDefaultLayoutStyle();
    const muiPropertyMask pastLayout = MUI_PROPERTY_BIT(mui_propertyContent + 1);
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &layout, pastLayout) ==
                  mui_errorInvalid &&
              muiNode_SetLayoutValues(context, node, &layout, pastLayout) == mui_errorInvalid,
          "the layout setters take only the layout group's bits");

    muiPropertyMask mask = 1;
    muiVisualStyle out;
    CHECK(muiStyle_GetVisualValues(context, style, mui_variantBase, &out, &mask) == mui_success &&
              mask == 0,
          "nothing set");
    CHECK(muiNode_GetDirectProperties(context, node, mui_groupLayout) ==
                  MUI_PROPERTY_BIT(mui_propertyContent) &&
              muiNode_GetDirectProperties(context, node, mui_groupVisual) == 0,
          "no direct visual write");

    // At the edges, allowed.
    v = defaults;
    v.gradient = (muiGradient){mui_gradientRadial, 2, 0.0f, {black, black}};
    v.opacity = 0.0f;
    v.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.5f}, -2.0f, 3.0f, 0.0f, -4.0f};
    v.radius.topStart = (muiDimension){100.0f, 0.0f, mui_dimensionValue};
    v.image = UINT64_MAX;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &v, MUI_VISUAL_PROPERTIES) ==
              mui_success,
          "stops at one place, opacity 0, an inward spread, a huge radius, any key");
    muiDestroyContext(context);
}

static void TestReadingValues(void)
{
    muiContext* context = MakeContext();
    muiStyleId style = MakeStyle(context);
    muiVisualStyle values = muiDefaultVisualStyle();
    values.background = s_red;
    values.opacity = 0.25f;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values,
                                   BACKGROUND | OPACITY) == mui_success,
          "visual");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &layout, WIDTH) == mui_success,
          "layout");

    muiVisualStyle out;
    muiPropertyMask mask = 0;
    CHECK(muiStyle_GetVisualValues(context, style, mui_variantBase, &out, &mask) == mui_success,
          "read visual");
    CHECK(mask == (BACKGROUND | OPACITY) && SameColor(out.background, s_red) &&
              out.opacity == 0.25f && SameColor(out.imageTint, muiDefaultVisualStyle().imageTint),
          "its values and defaults");
    muiLayoutStyle layoutOut;
    CHECK(muiStyle_GetLayoutValues(context, style, mui_variantBase, &layoutOut, &mask) ==
                  mui_success &&
              mask == WIDTH,
          "each getter names its own kind");
    CHECK(muiStyle_GetVisualValues(context, style, mui_variantHovered, &out, &mask) ==
                  mui_success &&
              mask == 0 && out.opacity == 1.0f,
          "an empty variant");
    CHECK(muiStyle_GetVisualValues(context, style, (muiVariant)99, &out, &mask) ==
                  mui_errorInvalid &&
              muiStyle_GetVisualValues(context, style, mui_variantBase, NULL, &mask) ==
                  mui_errorInvalid &&
              muiStyle_GetVisualValues(context, style, mui_variantBase, &out, NULL) ==
                  mui_errorInvalid &&
              muiStyle_GetVisualValues(context, s_nullStyle, mui_variantBase, &out, &mask) ==
                  mui_errorInvalid,
          "bad arguments");
    CHECK(muiDestroyStyle(context, style) == mui_success, "destroy");
    CHECK(muiStyle_GetVisualValues(context, style, mui_variantBase, &out, &mask) == mui_errorStale,
          "stale");
    muiDestroyContext(context);
}

static void TestDirectWrites(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    SetBackground(context, style, mui_variantBase, s_red);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    int measured = s_measured;
    ClearPaint(context, node);

    muiVisualStyle values = muiDefaultVisualStyle();
    values.background = s_blue;
    CHECK(muiNode_SetVisualValues(context, node, &values, BACKGROUND) == mui_success, "direct");
    CHECK(SameColor(Read(context, node).background, s_blue) && IsPaintMarked(context, node),
          "at once, marking paint");
    CHECK(!muiIsUpdatePending(context, node), "nothing to lay out");
    Layout(context, node, T0);
    CHECK(SameColor(Read(context, node).background, s_blue), "over the class");
    CHECK(s_measured == measured, "not laid out again");
    CHECK((muiNode_GetDirectProperties(context, node, mui_groupVisual) & BACKGROUND) != 0,
          "named direct");

    // A class edit restyles the node, and its direct write stays.
    muiVisualStyle classValues = muiDefaultVisualStyle();
    classValues.opacity = 0.75f;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &classValues, OPACITY) ==
              mui_success,
          "class opacity");
    Layout(context, node, T0);
    CHECK(SameColor(Read(context, node).background, s_blue) && Read(context, node).opacity == 0.75f,
          "the direct write kept beside the class's change");
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, OPACITY) ==
              mui_success,
          "class opacity back to 1");

    CHECK(muiNode_ResetProperties(context, node, mui_groupVisual, BACKGROUND) == mui_success,
          "reset");
    Layout(context, node, T0);
    CHECK(SameColor(Read(context, node).background, s_red), "the class's again");

    // No class names clipping: a reset still goes back to the default.
    values.clip = true;
    CHECK(muiNode_SetVisualValues(context, node, &values, MUI_PROPERTY_BIT(mui_propertyClip)) ==
              mui_success,
          "clip");
    Layout(context, node, T0);
    CHECK(muiNode_ResetProperties(context, node, mui_groupVisual,
                                  MUI_PROPERTY_BIT(mui_propertyClip)) == mui_success,
          "reset clip");
    Layout(context, node, T0);
    CHECK(!Read(context, node).clip, "the default again");
    values.opacity = 0.5f;

    // A slot used again starts from the defaults.
    CHECK(muiNode_SetVisualValues(context, node, &values, OPACITY) == mui_success, "opacity");
    CHECK(muiDestroyNode(context, node) == mui_success, "destroy");
    muiNodeId again = MakeNode(context);
    CHECK(Read(context, again).opacity == 1.0f && Read(context, again).background.a == 0.0f,
          "a new node in the old slot");
    muiDestroyContext(context);
}

static void TestTransitions(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiVisualStyle values = muiDefaultVisualStyle();
    values.opacity = 0.0f;
    values.radius.topStart = (muiDimension){0.0f, 8.0f, mui_dimensionValue};
    values.background = s_blue;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantHovered, &values,
                                   OPACITY | RADIUS | BACKGROUND) == mui_success,
          "hovered");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = 100 * MS;
    def.easing = mui_easingLinear;
    muiTransitionId linear = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &linear) == mui_success, "transition");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, linear, mui_groupVisual,
                                 OPACITY | RADIUS | BACKGROUND) == mui_success,
          "named");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    int measured = s_measured;

    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    Layout(context, node, T0);
    ClearPaint(context, node);
    Layout(context, node, T0 + 50 * MS);
    muiVisualStyle read = Read(context, node);
    CHECK(read.opacity == 0.5f, "opacity halfway");
    CHECK(read.radius.topStart.offset == 4.0f, "radius halfway");
    // From clear, premultiplied: blue at half alpha, not a darker blue.
    CHECK(fabsf(read.background.r) < 1e-5f && fabsf(read.background.g) < 1e-5f &&
              fabsf(read.background.b - 1.0f) < 1e-5f && read.background.a == 0.5f,
          "a color from clear, halfway");
    CHECK(muiNode_IsTransitioning(context, node, mui_propertyOpacity) &&
              muiNode_IsTransitioning(context, node, mui_propertyBackground),
          "which move");
    CHECK(IsPaintMarked(context, node) && s_measured == measured, "moving marks paint only");
    CHECK(muiIsUpdatePending(context, node), "pending while moving");
    Layout(context, node, T0 + 100 * MS);
    CHECK(Read(context, node).opacity == 0.0f && !muiIsUpdatePending(context, node), "done");
    CHECK(SameColor(Read(context, node).background, s_blue), "exactly on the target");
    muiDestroyContext(context);
}

// Red to blue in Oklab, and a shadow growing, as a hovered card's.
static void TestColorsAndShadowsMove(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiVisualStyle values = muiDefaultVisualStyle();
    values.background = s_red;
    values.imageSlice = (muiEdges){2.0f, 2.0f, 2.0f, 2.0f};
    const muiPropertyMask moved = BACKGROUND | MUI_PROPERTY_BIT(mui_propertyOuterShadow) |
                                  MUI_PROPERTY_BIT(mui_propertyImageSlice) |
                                  MUI_PROPERTY_BIT(mui_propertyClip) | GRADIENT;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, moved) == mui_success,
          "base");
    values.background = s_blue;
    values.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f, 4.0f, 8.0f, -2.0f};
    values.imageSlice = (muiEdges){6.0f, 6.0f, 6.0f, 6.0f};
    values.clip = true;
    values.gradient = (muiGradient){mui_gradientLinear, 2, 0.0f, {{s_red, 0.0f}, {s_blue, 1.0f}}};
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantHovered, &values, moved) ==
              mui_success,
          "hovered");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = 100 * MS;
    def.easing = mui_easingLinear;
    muiTransitionId linear = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &linear) == mui_success, "transition");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, linear, mui_groupVisual, moved) ==
              mui_success,
          "named");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    Layout(context, node, T0);
    Layout(context, node, T0 + 50 * MS);
    muiVisualStyle read = Read(context, node);
    // The Oklab midpoint, against an independent double-precision
    // conversion; sRGB's own midpoint would be (0.5, 0, 0.5).
    CHECK(fabsf(read.background.r - 0.5504411f) < 1e-5f &&
              fabsf(read.background.g - 0.3256207f) < 1e-5f &&
              fabsf(read.background.b - 0.6365007f) < 1e-5f && read.background.a == 1.0f,
          "red to blue through Oklab");
    CHECK(read.outerShadow.offsetY == 2.0f && read.outerShadow.blur == 4.0f &&
              read.outerShadow.spread == -1.0f && read.outerShadow.color.a == 0.25f &&
              read.outerShadow.color.r == 0.0f,
          "the shadow halfway, its color from clear");
    CHECK(read.imageSlice.start == 4.0f && read.imageSlice.bottom == 4.0f, "slices halfway");
    CHECK(read.clip && read.gradient.kind == mui_gradientLinear, "a flag and a gradient, at once");
    Layout(context, node, T0 + 100 * MS);
    read = Read(context, node);
    CHECK(SameColor(read.background, s_blue) && read.outerShadow.blur == 8.0f &&
              read.outerShadow.color.a == 0.5f && read.imageSlice.top == 6.0f,
          "all on their targets");
    muiDestroyContext(context);
}

static void TestRadiusSpringStaysAtZero(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiVisualStyle values = muiDefaultVisualStyle();
    values.radius.topStart = (muiDimension){0.0f, 8.0f, mui_dimensionValue};
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, RADIUS) == mui_success,
          "8");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.kind = mui_transitionSpring;
    def.frequency = 3.0f;
    def.dampingRatio = 0.2f;
    muiTransitionId spring = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &spring) == mui_success, "spring");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, spring, mui_groupVisual,
                                 RADIUS) == mui_success,
          "named");
    values.radius.topStart.offset = 0.0f;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantHovered, &values, RADIUS) ==
              mui_success,
          "0 when hovered");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    bool negative = false;
    for (uint64_t t = 0; t <= 1000 * MS; t += 8 * MS)
    {
        Layout(context, node, T0 + t);
        negative = negative || Read(context, node).radius.topStart.offset < 0.0f;
    }
    CHECK(!negative, "an overshoot stops at 0");
    muiDestroyContext(context);
}

// A spring on a shadow and on slice insets overshoots, and stops at 0.
static void TestShadowAndSliceSpringsStayAtZero(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    const muiPropertyMask moved =
        MUI_PROPERTY_BIT(mui_propertyOuterShadow) | MUI_PROPERTY_BIT(mui_propertyImageSlice);
    muiVisualStyle values = muiDefaultVisualStyle();
    values.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f, 0.0f, 8.0f, 0.0f};
    values.imageSlice = (muiEdges){8.0f, 8.0f, 8.0f, 8.0f};
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, moved) == mui_success,
          "base");
    values.outerShadow.blur = 0.0f;
    values.imageSlice = (muiEdges){0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantHovered, &values, moved) ==
              mui_success,
          "hovered");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.kind = mui_transitionSpring;
    def.frequency = 3.0f;
    def.dampingRatio = 0.2f;
    muiTransitionId spring = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &spring) == mui_success, "spring");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, spring, mui_groupVisual, moved) ==
              mui_success,
          "named");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    bool negative = false;
    for (uint64_t t = 0; t <= 1000 * MS; t += 8 * MS)
    {
        Layout(context, node, T0 + t);
        muiVisualStyle read = Read(context, node);
        negative = negative || read.outerShadow.blur < 0.0f || read.imageSlice.start < 0.0f ||
                   read.imageSlice.bottom < 0.0f;
    }
    CHECK(!negative, "a blur and insets stop at 0");
    muiDestroyContext(context);
}

// A running shadow given a new blur alone, its seventh channel, takes it.
static void TestRetargetOnALaterChannel(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    const muiPropertyMask shadow = MUI_PROPERTY_BIT(mui_propertyOuterShadow);
    muiVisualStyle values = muiDefaultVisualStyle();
    values.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f, 4.0f, 8.0f, 0.0f};
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantHovered, &values, shadow) ==
              mui_success,
          "hovered");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = 100 * MS;
    def.easing = mui_easingLinear;
    muiTransitionId linear = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &linear) == mui_success, "transition");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, linear, mui_groupVisual,
                                 shadow) == mui_success,
          "named");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    Layout(context, node, T0);
    Layout(context, node, T0 + 50 * MS);
    values.outerShadow.blur = 16.0f;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantHovered, &values, shadow) ==
              mui_success,
          "a larger blur");
    Layout(context, node, T0 + 50 * MS);
    Layout(context, node, T0 + 200 * MS);
    CHECK(Read(context, node).outerShadow.blur == 16.0f, "ends on the new blur");
    muiDestroyContext(context);
}

static void TestGradientsCompareUsedStops(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    muiVisualStyle values = muiDefaultVisualStyle();
    values.gradient = (muiGradient){mui_gradientLinear,
                                    2,
                                    45.0f,
                                    {{s_red, 0.0f}, {s_blue, 1.0f}, {s_red, 0.5f}, {s_red, 0.7f}}};
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, GRADIENT) ==
              mui_success,
          "gradient");
    Layout(context, node, T0);
    ClearPaint(context, node);
    values.gradient.stops[2] = (muiGradientStop){s_red, 1.0f};
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, GRADIENT) ==
              mui_success,
          "an unused stop");
    Layout(context, node, T0);
    CHECK(!IsPaintMarked(context, node), "changes nothing");
    values.gradient.stops[1].color.g = 0.5f;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, GRADIENT) ==
              mui_success,
          "a used stop");
    Layout(context, node, T0);
    CHECK(IsPaintMarked(context, node), "a change");
    ClearPaint(context, node);
    values.gradient.angle = 90.0f;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, GRADIENT) ==
              mui_success,
          "the angle");
    Layout(context, node, T0);
    CHECK(IsPaintMarked(context, node), "a change too");
    ClearPaint(context, node);
    // The unused stop it takes in is already there: only the count moves.
    values.gradient.stopCount = 3;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &values, GRADIENT) ==
              mui_success,
          "a stop more");
    Layout(context, node, T0);
    CHECK(IsPaintMarked(context, node), "a change as well");
    muiDestroyContext(context);
}

static void TestDrawingClearsPaint(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    Layout(context, node, T0);
    muiVisualStyle values = muiDefaultVisualStyle();
    values.background = s_red;
    CHECK(muiNode_SetVisualValues(context, node, &values, BACKGROUND) == mui_success, "red");
    CHECK(IsPaintMarked(context, node), "paint marked");
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    CHECK(muiBuildDrawList(context, node, &input) == mui_success, "drawn");
    CHECK(!IsPaintMarked(context, node), "and cleared");
    muiDestroyContext(context);
}

int main(void)
{
    TestDefaults();
    TestClassValuesMarkPaintOnly();
    TestVariantsAndConditions();
    TestChecks();
    TestReadingValues();
    TestDirectWrites();
    TestTransitions();
    TestGradientsCompareUsedStops();
    TestRadiusSpringStaysAtZero();
    TestColorsAndShadowsMove();
    TestDrawingClearsPaint();
    TestShadowAndSliceSpringsStayAtZero();
    TestRetargetOnALaterChannel();
    return s_failures == 0 ? 0 : 1;
}
