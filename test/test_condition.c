// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Style conditions: their checks, the static rejection of values a
// condition reads, the environment, and the sizes and directions they
// read from the last layout (record mui-0004).

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/exit.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <math.h>

static const muiNodeId s_nullNode = {0, 0};
static const muiStyleId s_nullStyle = {0, 0};

#define WIDTH         MUI_PROPERTY_BIT(mui_propertyWidth)
#define PADDING_START MUI_PROPERTY_BIT(mui_propertyPaddingStart)

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
    const muiStyleDef styleDef = muiDefaultStyleDef();
    CHECK(muiCreateStyle(context, &styleDef, &style) == mui_success, "create style");
    return style;
}

static muiDimension Length(float offset)
{
    return (muiDimension){0.0f, offset, mui_dimensionValue};
}

static void SetWidth(muiContext* context, muiNodeId node, float width)
{
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.sizing.width = Length(width);
    CHECK(muiNode_SetLayoutValues(context, node, &values, WIDTH) == mui_success, "width");
}

// Adds condition to style with a start padding as its value.
static muiVariant AddPadding(muiContext* context, muiStyleId style, const muiCondition* condition,
                             float padding)
{
    muiVariant variant = mui_variantBase;
    CHECK(muiStyle_AddCondition(context, style, condition, &variant) == mui_success, "add");
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.padding.start = padding;
    CHECK(muiStyle_SetLayoutValues(context, style, variant, &values, PADDING_START) == mui_success,
          "padding");
    return variant;
}

static void Compute(muiContext* context, muiNodeId root)
{
    muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "compute");
}

static float PaddingOf(const muiContext* context, muiNodeId node)
{
    muiLayoutStyle values;
    CHECK(muiNode_GetLayoutStyle(context, node, &values) == mui_success, "read");
    return values.padding.start;
}

// The padding after laying out root once.
static float PaddingAfterLayout(muiContext* context, muiNodeId root, muiNodeId node)
{
    Compute(context, root);
    return PaddingOf(context, node);
}

static void TestDefaults(void)
{
    muiCondition condition = muiDefaultCondition();
    CHECK(condition.width.min == 0.0f && condition.width.max == INFINITY, "every width");
    CHECK(condition.aspect.max == INFINITY && condition.textScale.max == INFINITY, "ranges");
    CHECK(condition.viewports == 7 && condition.inputs == 7, "every class and modality");
    CHECK(condition.motion == mui_motionAny && condition.direction == mui_directionAny, "any");
    muiEnvironment environment = muiDefaultEnvironment();
    CHECK(environment.viewport == mui_viewportMedium && environment.input == mui_inputPointer,
          "medium, pointer");
    CHECK(environment.textScale == 1.0f && !environment.reducedMotion, "scale 1, full motion");
    muiContext* context = MakeContext();
    muiEnvironment read = muiGetContextEnvironment(context);
    CHECK(read.viewport == mui_viewportMedium && read.textScale == 1.0f, "a new context's");
    read = muiGetContextEnvironment(NULL);
    CHECK(read.input == mui_inputPointer, "no context gives the default");
    muiDestroyContext(context);
}

static void TestEnvironmentIsChecked(void)
{
    muiContext* context = MakeContext();
    muiEnvironment bad[6];
    for (int i = 0; i < 6; i++)
    {
        bad[i] = muiDefaultEnvironment();
    }
    bad[0].viewport = mui_viewportSmall | mui_viewportLarge;
    bad[1].viewport = 0;
    bad[2].input = 8;
    bad[3].textScale = 0.0f;
    bad[4].textScale = NAN;
    bad[5].textScale = INFINITY;
    for (int i = 0; i < 6; i++)
    {
        CHECK(muiSetContextEnvironment(context, &bad[i]) == mui_errorInvalid, "refused");
    }
    CHECK(muiSetContextEnvironment(context, NULL) == mui_errorInvalid, "no environment");
    CHECK(muiGetContextMisuse(context) == 7, "each counted");
    muiEnvironment environment = muiDefaultEnvironment();
    CHECK(muiSetContextEnvironment(NULL, &environment) == mui_errorInvalid, "no context");
    environment.input = mui_inputGamepad;
    environment.textScale = 1.25f;
    CHECK(muiSetContextEnvironment(context, &environment) == mui_success, "set");
    muiEnvironment read = muiGetContextEnvironment(context);
    CHECK(read.input == mui_inputGamepad && read.textScale == 1.25f, "read back");
    muiDestroyContext(context);
}

static void TestConditionLifetime(void)
{
    muiContext* context = MakeContextWith((muiLimits){.nodes = 1, .styles = 1, .propertySets = 2});
    muiStyleId style = MakeStyle(context);
    muiCondition condition = muiDefaultCondition();
    muiVariant variants[MUI_MAX_CONDITIONS];
    for (uint32_t i = 0; i < MUI_MAX_CONDITIONS; i++)
    {
        condition.textScale.min = (float)i;
        CHECK(muiStyle_AddCondition(context, style, &condition, &variants[i]) == mui_success,
              "add");
        CHECK(variants[i] == mui_variantCondition0 + i, "variants in order");
    }
    muiVariant variant = mui_variantPressed;
    CHECK(muiStyle_AddCondition(context, style, &condition, &variant) == mui_errorCapacity,
          "the limit");
    CHECK(variant == mui_variantBase, "base on failure");
    muiCondition read;
    CHECK(muiStyle_GetCondition(context, style, variants[3], &read) == mui_success &&
              read.textScale.min == 3.0f,
          "read one");
    condition.textScale.min = 9.0f;
    CHECK(muiStyle_SetCondition(context, style, variants[3], &condition) == mui_success, "replace");
    CHECK(muiStyle_GetCondition(context, style, variants[3], &read) == mui_success &&
              read.textScale.min == 9.0f,
          "replaced");
    muiLayoutStyle values = muiDefaultLayoutStyle();
    CHECK(muiStyle_SetLayoutValues(context, style, variants[0], &values, PADDING_START) ==
              mui_success,
          "values of one");
    CHECK(muiStyle_SetLayoutValues(context, style, variants[1], &values, PADDING_START) ==
              mui_success,
          "values of another");
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, PADDING_START) ==
              mui_errorCapacity,
          "both sets in use");
    CHECK(muiStyle_ClearConditions(context, style) == mui_success, "clear");
    CHECK(muiStyle_GetCondition(context, style, variants[0], &read) == mui_errorInvalid, "gone");
    CHECK(muiStyle_SetLayoutValues(context, style, variants[0], &values, PADDING_START) ==
              mui_errorInvalid,
          "no values for a gone condition");
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, PADDING_START) ==
              mui_success,
          "clearing gave the sets back");
    CHECK(muiStyle_AddCondition(context, style, &condition, &variant) == mui_success &&
              variant == mui_variantCondition0,
          "numbering starts again");
    CHECK(muiStyle_SetLayoutValues(context, style, variant, &values, PADDING_START) == mui_success,
          "the second set");
    CHECK(muiDestroyStyle(context, style) == mui_success, "destroy");
    style = MakeStyle(context);
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, PADDING_START) ==
                  mui_success &&
              muiStyle_SetLayoutValues(context, style, mui_variantHovered, &values,
                                       PADDING_START) == mui_success,
          "destroying gave a condition's set back");
    muiDestroyContext(context);
}

static void TestConditionsAreChecked(void)
{
    muiContext* context = MakeContext();
    muiStyleId style = MakeStyle(context);
    muiCondition bad[8];
    for (int i = 0; i < 8; i++)
    {
        bad[i] = muiDefaultCondition();
    }
    bad[0].width.min = -1.0f;
    bad[1].height = (muiRange){5.0f, 4.0f};
    bad[2].aspect.max = NAN;
    bad[3].textScale.min = INFINITY;
    bad[4].viewports = 8;
    bad[5].inputs = 0x10;
    bad[6].motion = 3;
    bad[7].direction = 3;
    muiVariant variant = mui_variantBase;
    for (int i = 0; i < 8; i++)
    {
        CHECK(muiStyle_AddCondition(context, style, &bad[i], &variant) == mui_errorInvalid,
              "refused");
    }
    muiCondition good = muiDefaultCondition();
    good.width = (muiRange){3.0f, 3.0f};
    good.viewports = 0;
    CHECK(muiStyle_AddCondition(context, style, &good, &variant) == mui_success,
          "an empty range and no viewports are well formed");
    for (int i = 0; i < 8; i++)
    {
        CHECK(muiStyle_SetCondition(context, style, variant, &bad[i]) == mui_errorInvalid,
              "refused as a replacement");
    }
    CHECK(muiStyle_AddCondition(context, style, NULL, &variant) == mui_errorInvalid, "none");
    CHECK(muiStyle_AddCondition(context, style, &good, NULL) == mui_errorInvalid, "no out");
    CHECK(muiStyle_AddCondition(context, s_nullStyle, &good, &variant) == mui_errorInvalid,
          "null class");
    CHECK(muiStyle_AddCondition(NULL, style, &good, &variant) == mui_errorInvalid, "no context");
    CHECK(muiStyle_SetCondition(context, style, mui_variantHovered, &good) == mui_errorInvalid,
          "a state is not a condition");
    CHECK(muiStyle_SetCondition(context, style, mui_variantCondition0 + 1, &good) ==
              mui_errorInvalid,
          "a condition the class does not have");
    CHECK(muiStyle_SetCondition(context, style, variant, NULL) == mui_errorInvalid, "none");
    CHECK(muiStyle_SetCondition(NULL, style, variant, &good) == mui_errorInvalid, "no context");
    muiCondition read;
    CHECK(muiStyle_GetCondition(context, style, mui_variantBase, &read) == mui_errorInvalid,
          "the base is not a condition");
    CHECK(muiStyle_GetCondition(context, style, variant, NULL) == mui_errorInvalid, "no out");
    CHECK(muiStyle_GetCondition(context, s_nullStyle, variant, &read) == mui_errorInvalid,
          "null class read");
    CHECK(muiStyle_GetCondition(NULL, style, variant, &read) == mui_errorInvalid, "no context");
    CHECK(muiStyle_ClearConditions(context, s_nullStyle) == mui_errorInvalid, "null clear");
    CHECK(muiStyle_ClearConditions(NULL, style) == mui_errorInvalid, "no context clear");
    muiLayoutStyle values;
    muiPropertyMask mask = 0;
    CHECK(muiStyle_GetLayoutValues(context, style, mui_variantCondition0 + 1, &values, &mask) ==
              mui_errorInvalid,
          "values of a condition the class does not have");
    CHECK(muiStyle_ResetProperties(context, style, mui_variantCondition0 + 1, mui_groupLayout,
                                   WIDTH) == mui_errorInvalid,
          "reset on one");
    CHECK(muiDestroyStyle(context, style) == mui_success, "destroy");
    CHECK(muiStyle_AddCondition(context, style, &good, &variant) == mui_errorStale, "gone add");
    CHECK(muiStyle_SetCondition(context, style, variant, &good) == mui_errorStale, "gone set");
    CHECK(muiStyle_GetCondition(context, style, variant, &read) == mui_errorStale, "gone get");
    CHECK(muiStyle_ClearConditions(context, style) == mui_errorStale, "gone clear");
    muiDestroyContext(context);
}

static void TestValuesMayNotSetWhatTheirConditionReads(void)
{
    muiContext* context = MakeContext();
    muiStyleId style = MakeStyle(context);
    const struct
    {
        muiCondition condition;
        muiPropertyMask allowed;
        muiPropertyMask refused;
    } cases[] = {
        {{.width = {0, 100}, .height = {0, INFINITY}, .aspect = {0, INFINITY}},
         MUI_PROPERTY_BIT(mui_propertyHeight) | PADDING_START,
         WIDTH | MUI_PROPERTY_BIT(mui_propertyMinWidth) | MUI_PROPERTY_BIT(mui_propertyMaxWidth) |
             MUI_PROPERTY_BIT(mui_propertyAspectRatio)},
        {{.width = {0, INFINITY}, .height = {10, INFINITY}, .aspect = {0, INFINITY}},
         WIDTH,
         MUI_PROPERTY_BIT(mui_propertyHeight) | MUI_PROPERTY_BIT(mui_propertyMinHeight) |
             MUI_PROPERTY_BIT(mui_propertyMaxHeight) | MUI_PROPERTY_BIT(mui_propertyAspectRatio)},
        {{.width = {0, INFINITY}, .height = {0, INFINITY}, .aspect = {1, 2}},
         PADDING_START,
         WIDTH | MUI_PROPERTY_BIT(mui_propertyMaxHeight)},
        {{.width = {0, INFINITY},
          .height = {0, INFINITY},
          .aspect = {0, INFINITY},
          .direction = mui_directionRightToLeft},
         WIDTH | MUI_PROPERTY_BIT(mui_propertyAspectRatio),
         MUI_PROPERTY_BIT(mui_propertyTextDirection)},
    };
    muiLayoutStyle values = muiDefaultLayoutStyle();
    for (uint32_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        muiCondition condition = cases[i].condition;
        condition.textScale = (muiRange){0.0f, INFINITY};
        condition.viewports = 7;
        condition.inputs = 7;
        muiVariant variant = mui_variantBase;
        CHECK(muiStyle_AddCondition(context, style, &condition, &variant) == mui_success, "add");
        for (uint32_t bit = 0; bit <= mui_propertyContent; bit++)
        {
            muiPropertyMask mask = MUI_PROPERTY_BIT(bit);
            if ((cases[i].refused & mask) != 0)
            {
                CHECK(muiStyle_SetLayoutValues(context, style, variant, &values, mask) ==
                          mui_errorInvalid,
                      "what it reads is refused");
            }
            else if ((cases[i].allowed & mask) != 0)
            {
                CHECK(muiStyle_SetLayoutValues(context, style, variant, &values, mask) ==
                          mui_success,
                      "the rest is allowed");
            }
        }
    }
    // A replacement may not read what the values set already.
    muiCondition readsWidth = muiDefaultCondition();
    readsWidth.width.max = 50.0f;
    CHECK(muiStyle_SetCondition(context, style, mui_variantCondition0 + 1, &readsWidth) ==
              mui_errorInvalid,
          "values set width, so the condition may not read it");
    CHECK(muiStyle_SetCondition(context, style, mui_variantCondition0 + 2, &readsWidth) ==
              mui_success,
          "values set only padding");
    CHECK(muiStyle_SetCondition(context, style, mui_variantCondition0 + 3, &readsWidth) ==
              mui_errorInvalid,
          "values set width and the aspect ratio");
    muiDestroyContext(context);
}

static void TestEnvironmentClauses(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiCondition small = muiDefaultCondition();
    small.viewports = mui_viewportSmall;
    AddPadding(context, style, &small, 1.0f);
    muiCondition touch = muiDefaultCondition();
    touch.inputs = mui_inputTouch | mui_inputGamepad;
    AddPadding(context, style, &touch, 2.0f);
    muiCondition reduced = muiDefaultCondition();
    reduced.motion = mui_motionReduced;
    AddPadding(context, style, &reduced, 3.0f);
    muiCondition large = muiDefaultCondition();
    large.textScale = (muiRange){1.5f, 2.0f};
    AddPadding(context, style, &large, 4.0f);
    muiCondition full = muiDefaultCondition();
    full.motion = mui_motionFull;
    full.viewports = mui_viewportLarge;
    AddPadding(context, style, &full, 5.0f);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    CHECK(PaddingAfterLayout(context, node, node) == 0.0f, "none holds by default");
    muiEnvironment environment = muiDefaultEnvironment();
    const struct
    {
        muiViewportClass viewport;
        muiInputModality input;
        bool reducedMotion;
        float textScale;
        float padding;
    } steps[] = {
        {mui_viewportSmall, mui_inputPointer, false, 1.0f, 1.0f},
        {mui_viewportMedium, mui_inputTouch, false, 1.0f, 2.0f},
        {mui_viewportMedium, mui_inputGamepad, false, 1.0f, 2.0f},
        {mui_viewportSmall, mui_inputTouch, true, 1.0f, 3.0f},
        {mui_viewportMedium, mui_inputPointer, false, 1.5f, 4.0f},
        {mui_viewportMedium, mui_inputPointer, false, 2.0f, 0.0f},
        {mui_viewportLarge, mui_inputPointer, false, 1.0f, 5.0f},
        {mui_viewportLarge, mui_inputPointer, true, 1.0f, 3.0f},
    };
    for (uint32_t i = 0; i < sizeof steps / sizeof steps[0]; i++)
    {
        environment.viewport = steps[i].viewport;
        environment.input = steps[i].input;
        environment.reducedMotion = steps[i].reducedMotion;
        environment.textScale = steps[i].textScale;
        CHECK(muiSetContextEnvironment(context, &environment) == mui_success, "environment");
        CHECK(PaddingAfterLayout(context, node, node) == steps[i].padding,
              "the last condition that holds wins");
    }
    muiDestroyContext(context);
}

static void TestConditionsLayerAboveStates(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId first = MakeStyle(context);
    muiStyleId second = MakeStyle(context);
    muiCondition always = muiDefaultCondition();
    AddPadding(context, first, &always, 1.0f);
    muiLayoutStyle pressed = muiDefaultLayoutStyle();
    pressed.padding.start = 9.0f;
    CHECK(muiStyle_SetLayoutValues(context, second, mui_variantPressed, &pressed, PADDING_START) ==
              mui_success,
          "pressed");
    muiStyleId order[] = {first, second};
    CHECK(muiNode_SetClasses(context, node, order, 2) == mui_success, "classes");
    CHECK(muiNode_SetStates(context, node, mui_statePressed) == mui_success, "press");
    CHECK(PaddingAfterLayout(context, node, node) == 1.0f,
          "an earlier class's condition beats a later class's state");
    AddPadding(context, first, &always, 2.0f);
    CHECK(PaddingAfterLayout(context, node, node) == 2.0f, "a later condition of a class wins");
    AddPadding(context, second, &always, 3.0f);
    CHECK(PaddingAfterLayout(context, node, node) == 3.0f, "a later class's condition wins");
    muiLayoutStyle direct = muiDefaultLayoutStyle();
    direct.padding.start = 7.0f;
    CHECK(muiNode_SetLayoutValues(context, node, &direct, PADDING_START) == mui_success, "direct");
    CHECK(PaddingAfterLayout(context, node, node) == 7.0f, "a direct write beats them all");
    muiDestroyContext(context);
}

static void TestSizeClausesReadTheLastLayout(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = MakeNode(context);
    muiNodeId node = MakeNode(context);
    CHECK(muiNode_InsertChild(context, root, node, s_nullNode) == mui_success, "insert");
    SetWidth(context, node, 200.0f);
    muiStyleId style = MakeStyle(context);
    muiCondition narrow = muiDefaultCondition();
    narrow.width = (muiRange){100.0f, 250.0f};
    AddPadding(context, style, &narrow, 10.0f);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    CHECK(muiIsUpdatePending(context, root), "a new tree is pending");
    CHECK(PaddingAfterLayout(context, root, node) == 0.0f, "before any layout the width is 0");
    CHECK(muiIsUpdatePending(context, root), "the layout changed what the condition read");
    CHECK(PaddingAfterLayout(context, root, node) == 10.0f, "the next run reads 200");
    CHECK(!muiIsUpdatePending(context, root), "and nothing changed it since");
    SetWidth(context, node, 250.0f);
    CHECK(PaddingAfterLayout(context, root, node) == 10.0f, "the run that widens reads 200");
    CHECK(muiIsUpdatePending(context, root), "so another is owed");
    CHECK(PaddingAfterLayout(context, root, node) == 0.0f, "250 is past the range's end");
    CHECK(!muiIsUpdatePending(context, root), "settled");
    muiLayoutStyle tall = muiDefaultLayoutStyle();
    tall.sizing.height = Length(0.0f);
    CHECK(muiNode_SetLayoutValues(context, node, &tall, MUI_PROPERTY_BIT(mui_propertyHeight)) ==
              mui_success,
          "zero height");
    muiCondition wide = muiDefaultCondition();
    wide.aspect = (muiRange){2.0f, INFINITY};
    AddPadding(context, style, &wide, 20.0f);
    Compute(context, root);
    CHECK(PaddingAfterLayout(context, root, node) == 20.0f,
          "a zero height is an infinite aspect, which an unbounded range holds");
    CHECK(!muiIsUpdatePending(NULL, root) && !muiIsUpdatePending(context, s_nullNode),
          "nothing pending without a context or root");
    muiDestroyContext(context);
}

static void TestDirectionClauseReadsTheLastLayout(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = MakeNode(context);
    muiNodeId node = MakeNode(context);
    CHECK(muiNode_InsertChild(context, root, node, s_nullNode) == mui_success, "insert");
    muiStyleId style = MakeStyle(context);
    muiCondition rtl = muiDefaultCondition();
    rtl.direction = mui_directionRightToLeft;
    AddPadding(context, style, &rtl, 6.0f);
    muiCondition ltr = muiDefaultCondition();
    ltr.direction = mui_directionLeftToRight;
    AddPadding(context, style, &ltr, 3.0f);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    CHECK(PaddingAfterLayout(context, root, node) == 3.0f, "left to right at first");
    muiLayoutStyle flip = muiDefaultLayoutStyle();
    flip.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, root, &flip,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "the root turns right to left");
    CHECK(PaddingAfterLayout(context, root, node) == 3.0f, "the run that turns it reads ltr");
    CHECK(muiIsUpdatePending(context, root), "another is owed");
    CHECK(PaddingAfterLayout(context, root, node) == 6.0f, "the inherited direction");
    CHECK(!muiIsUpdatePending(context, root), "settled");
    muiDestroyContext(context);
}

static void SetSize(muiContext* context, muiNodeId node, float width, float height)
{
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.sizing.width = Length(width);
    values.sizing.height = Length(height);
    CHECK(muiNode_SetLayoutValues(context, node, &values,
                                  WIDTH | MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "size");
}

// Lays root out until nothing is pending, at most three times.
static void Settle(muiContext* context, muiNodeId root)
{
    for (int i = 0; i < 3 && muiIsUpdatePending(context, root); i++)
    {
        Compute(context, root);
    }
    CHECK(!muiIsUpdatePending(context, root), "settles");
}

static void TestAspectAndHeightClausesFollowLayout(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    SetSize(context, node, 100.0f, 100.0f);
    muiStyleId style = MakeStyle(context);
    muiCondition wide = muiDefaultCondition();
    wide.aspect = (muiRange){2.0f, INFINITY};
    AddPadding(context, style, &wide, 5.0f);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Settle(context, node);
    CHECK(PaddingOf(context, node) == 0.0f, "square");
    SetSize(context, node, 300.0f, 100.0f);
    Settle(context, node);
    CHECK(PaddingOf(context, node) == 5.0f, "an aspect clause alone follows a width change");
    muiCondition low = muiDefaultCondition();
    low.height = (muiRange){0.0f, 50.0f};
    AddPadding(context, style, &low, 7.0f);
    Settle(context, node);
    CHECK(PaddingOf(context, node) == 5.0f, "tall");
    SetSize(context, node, 300.0f, 40.0f);
    Settle(context, node);
    CHECK(PaddingOf(context, node) == 7.0f, "a height change alone is followed");
    muiDestroyContext(context);
}

static void TestOnlyConditionsThatApplyAreWatched(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    SetSize(context, node, 100.0f, 100.0f);
    muiStyleId style = MakeStyle(context);
    muiCondition narrow = muiDefaultCondition();
    narrow.width = (muiRange){0.0f, 150.0f};
    muiVariant variant = mui_variantBase;
    CHECK(muiStyle_AddCondition(context, style, &narrow, &variant) == mui_success,
          "a condition with no values");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Settle(context, node);
    SetSize(context, node, 200.0f, 100.0f);
    Compute(context, node);
    CHECK(!muiIsUpdatePending(context, node), "a condition without values reads nothing");
    // A node whose every property is direct reads nothing either, once
    // it is styled again.
    muiStyleId watched = MakeStyle(context);
    AddPadding(context, watched, &narrow, 4.0f);
    CHECK(muiNode_SetClasses(context, node, &watched, 1) == mui_success, "watched class");
    Settle(context, node);
    muiLayoutStyle all = muiDefaultLayoutStyle();
    all.sizing.width = Length(120.0f);
    CHECK(muiNode_SetLayoutStyle(context, node, &all) == mui_success, "every property direct");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "restyle");
    Settle(context, node);
    SetSize(context, node, 130.0f, 0.0f);
    Compute(context, node);
    CHECK(!muiIsUpdatePending(context, node), "an all-direct node is not watched");
    // Nor when its class leaves visual values to resolve: the condition's
    // values reach only properties it writes.
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.opacity = 0.5f;
    CHECK(muiStyle_SetVisualValues(context, watched, mui_variantBase, &visual,
                                   MUI_PROPERTY_BIT(mui_propertyOpacity)) == mui_success,
          "a visual value");
    Settle(context, node);
    SetSize(context, node, 140.0f, 0.0f);
    Compute(context, node);
    CHECK(!muiIsUpdatePending(context, node), "still not watched");
    muiDestroyContext(context);
}

static void TestDirectValuesSurviveAConditionWrite(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiVariant variant = mui_variantBase;
    muiCondition always = muiDefaultCondition();
    CHECK(muiStyle_AddCondition(context, style, &always, &variant) == mui_success, "add");
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.sizing.width = Length(25.0f);
    values.padding.start = 2.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, variant, &values, WIDTH | PADDING_START) ==
              mui_success,
          "width and padding");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    SetWidth(context, node, 50.0f);
    Compute(context, node);
    CHECK(PaddingOf(context, node) == 2.0f, "the condition's padding");
    CHECK(muiNode_GetRect(context, node).width == 50.0f, "beside the direct width");
    muiDestroyContext(context);
}

// A node whose start padding, given while it is narrower than 100,
// widens it past 100: its child is 80 wide and it fits its content.
static muiNodeId MakeOscillator(muiContext* context, muiNodeId parent, muiStyleId style)
{
    muiNodeId node = MakeNode(context);
    muiNodeId child = MakeNode(context);
    SetWidth(context, child, 80.0f);
    CHECK(muiNode_InsertChild(context, node, child, s_nullNode) == mui_success, "child");
    CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    return node;
}

static muiStyleId MakeWidening(muiContext* context)
{
    muiStyleId style = MakeStyle(context);
    muiCondition narrow = muiDefaultCondition();
    narrow.width = (muiRange){0.0f, 100.0f};
    AddPadding(context, style, &narrow, 50.0f);
    return style;
}

// Lays root out while work is pending, at most ten times, and returns how
// many runs it took.
static int RunWhilePending(muiContext* context, muiNodeId root)
{
    int runs = 0;
    while (runs < 10 && muiIsUpdatePending(context, root))
    {
        Compute(context, root);
        runs++;
    }
    return runs;
}

static void ExpectOscillation(muiContext* context, muiNodeId node)
{
    muiNotification record = {0};
    CHECK(muiNextNotification(context, &record) == mui_success, "a record");
    CHECK(record.kind == mui_notificationOscillation, "an oscillation");
    CHECK(record.nodeId.index1 == node.index1 && record.nodeId.generation == node.generation,
          "of the node");
    CHECK(muiNextNotification(context, &record) == mui_empty, "only one");
}

static void TestOscillationIsReportedAndHeld(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = MakeNode(context);
    muiStyleId style = MakeWidening(context);
    muiNodeId node = MakeOscillator(context, root, style);
    CHECK(RunWhilePending(context, root) == 5, "four runs flip it, the fifth reports");
    ExpectOscillation(context, node);
    float width = muiNode_GetRect(context, node).width;
    Compute(context, root);
    CHECK(!muiIsUpdatePending(context, root), "held");
    CHECK(muiNode_GetRect(context, node).width == width, "at one of its two widths");
    // A host edit of the node releases it.
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "edit");
    CHECK(RunWhilePending(context, root) == 4,
          "it oscillates again, its first styling reading one of the two widths");
    ExpectOscillation(context, node);
    // So does an exit begun and cancelled.
    Compute(context, root);
    CHECK(!muiIsUpdatePending(context, root), "held again");
    CHECK(muiNode_BeginExit(context, node) == mui_success &&
              muiNode_CancelExit(context, node) == mui_success,
          "an exit begun and cancelled");
    CHECK(RunWhilePending(context, root) == 4, "released");
    ExpectOscillation(context, node);
    // So does a size other than the two, here from its child.
    muiNodeId child = muiNode_GetFirstChild(context, node);
    SetWidth(context, child, 85.0f);
    CHECK(RunWhilePending(context, root) > 1, "released and laid out again");
    ExpectOscillation(context, node);
    muiDestroyContext(context);
}

static void TestHostEditsAreNotOscillation(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiCondition narrow = muiDefaultCondition();
    narrow.width = (muiRange){0.0f, 100.0f};
    AddPadding(context, style, &narrow, 5.0f);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    for (int i = 0; i < 8; i++)
    {
        SetWidth(context, node, i % 2 == 0 ? 90.0f : 110.0f);
        Compute(context, node);
        Compute(context, node);
    }
    muiNotification record;
    CHECK(muiNextNotification(context, &record) == mui_empty,
          "widths the host writes in turn are its own doing");
    muiDestroyContext(context);
}

static void TestNotificationsPastTheLimitAreCounted(void)
{
    muiLimits limits = muiDefaultContextDef().limits;
    limits.notifications = 1;
    muiContext* context = MakeContextWith(limits);
    muiNodeId root = MakeNode(context);
    muiStyleId style = MakeWidening(context);
    muiNodeId first = MakeOscillator(context, root, style);
    MakeOscillator(context, root, style);
    MakeOscillator(context, root, style);
    RunWhilePending(context, root);
    muiNotification record = {0};
    CHECK(muiNextNotification(context, &record) == mui_success &&
              record.kind == mui_notificationOscillation && record.nodeId.index1 == first.index1,
          "the first, in tree order");
    CHECK(muiNextNotification(context, &record) == mui_success &&
              record.kind == mui_notificationDropped && record.count == 2 &&
              record.nodeId.index1 == 0,
          "then a count of the rest");
    CHECK(muiNextNotification(context, &record) == mui_empty, "then nothing");
    CHECK(muiNextNotification(context, NULL) == mui_errorInvalid, "no out pointer");
    CHECK(muiNextNotification(NULL, &record) == mui_errorInvalid, "no context");
    CHECK(muiResultName(mui_empty)[4] == 'e', "mui_empty has a name");
    muiDestroyContext(context);
    limits.notifications = 0;
    context = MakeContextWith(limits);
    root = MakeNode(context);
    style = MakeWidening(context);
    MakeOscillator(context, root, style);
    RunWhilePending(context, root);
    CHECK(muiNextNotification(context, &record) == mui_success &&
              record.kind == mui_notificationDropped && record.count == 1,
          "with no room, only the count");
    muiDestroyContext(context);
}

static void TestNotificationsKeepTheirOrder(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = MakeNode(context);
    muiStyleId style = MakeWidening(context);
    muiNodeId first = MakeOscillator(context, root, style);
    muiNodeId second = MakeOscillator(context, root, style);
    RunWhilePending(context, root);
    muiNotification record = {0};
    CHECK(muiNextNotification(context, &record) == mui_success &&
              record.nodeId.index1 == first.index1,
          "the first");
    CHECK(muiNextNotification(context, &record) == mui_success &&
              record.nodeId.index1 == second.index1,
          "then the second");
    muiDestroyContext(context);
    // With room for one: a record lost keeps later ones from overtaking
    // its count.
    muiLimits limits = muiDefaultContextDef().limits;
    limits.notifications = 1;
    context = MakeContextWith(limits);
    root = MakeNode(context);
    style = MakeWidening(context);
    first = MakeOscillator(context, root, style);
    RunWhilePending(context, root);
    MakeOscillator(context, root, style);
    RunWhilePending(context, root);
    CHECK(muiNextNotification(context, &record) == mui_success &&
              record.nodeId.index1 == first.index1,
          "the one kept");
    MakeOscillator(context, root, style);
    RunWhilePending(context, root);
    CHECK(muiNextNotification(context, &record) == mui_success &&
              record.kind == mui_notificationDropped && record.count == 2,
          "the later one counts with the lost one, though there was room");
    CHECK(muiNextNotification(context, &record) == mui_empty, "nothing after");
    muiDestroyContext(context);
}

static void TestAlternatingSizesWithOneOutcomeAreNotOscillation(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = MakeNode(context);
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.container.direction = mui_flexColumn;
    CHECK(muiNode_SetLayoutValues(context, root, &column,
                                  MUI_PROPERTY_BIT(mui_propertyFlexDirection)) == mui_success,
          "column");
    muiNodeId node = MakeNode(context);
    CHECK(muiNode_InsertChild(context, root, node, s_nullNode) == mui_success, "insert");
    muiStyleId style = MakeStyle(context);
    muiCondition any = muiDefaultCondition();
    any.width = (muiRange){0.0f, 200.0f};
    AddPadding(context, style, &any, 5.0f);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    for (int i = 0; i < 8; i++)
    {
        // The parent's width, not the node's, so its stylings are its
        // layout's doing.
        SetWidth(context, root, i % 2 == 0 ? 90.0f : 110.0f);
        Compute(context, root);
        Compute(context, root);
    }
    muiNotification record;
    CHECK(muiNextNotification(context, &record) == mui_empty,
          "sizes in turn with no flip of the outcome");
    muiDestroyContext(context);
}

static void TestHostEditsStartTheHistoryAgain(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = MakeNode(context);
    muiStyleId style = MakeWidening(context);
    muiNodeId node = MakeOscillator(context, root, style);
    for (int i = 0; i < 3; i++)
    {
        Compute(context, root);
    }
    muiStyleId unrelated = MakeStyle(context);
    muiLayoutStyle values = muiDefaultLayoutStyle();
    CHECK(muiStyle_SetLayoutValues(context, unrelated, mui_variantBase, &values, WIDTH) ==
              mui_success,
          "edit another class");
    CHECK(RunWhilePending(context, root) == 4, "a class edit is the host's");
    ExpectOscillation(context, node);
    CHECK(muiNode_SetStates(context, node, mui_stateFocused) == mui_success, "release");
    for (int i = 0; i < 3; i++)
    {
        Compute(context, root);
    }
    CHECK(muiNode_Detach(context, node) == mui_success, "detach");
    CHECK(muiNode_InsertChild(context, root, node, s_nullNode) == mui_success, "insert again");
    CHECK(RunWhilePending(context, root) == 4, "so is inserting the node");
    ExpectOscillation(context, node);
    muiDestroyContext(context);
}

static void TestFlipsBetweenConditionsAreOscillation(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = MakeNode(context);
    muiStyleId style = MakeWidening(context);
    muiCondition wide = muiDefaultCondition();
    wide.width = (muiRange){100.0f, INFINITY};
    AddPadding(context, style, &wide, 0.0f);
    muiNodeId node = MakeOscillator(context, root, style);
    RunWhilePending(context, root);
    ExpectOscillation(context, node);
    muiDestroyContext(context);
}

int main(void)
{
    TestDefaults();
    TestEnvironmentIsChecked();
    TestConditionLifetime();
    TestConditionsAreChecked();
    TestValuesMayNotSetWhatTheirConditionReads();
    TestEnvironmentClauses();
    TestConditionsLayerAboveStates();
    TestSizeClausesReadTheLastLayout();
    TestDirectionClauseReadsTheLastLayout();
    TestAspectAndHeightClausesFollowLayout();
    TestOnlyConditionsThatApplyAreWatched();
    TestDirectValuesSurviveAConditionWrite();
    TestOscillationIsReportedAndHeld();
    TestHostEditsAreNotOscillation();
    TestNotificationsPastTheLimitAreCounted();
    TestNotificationsKeepTheirOrder();
    TestAlternatingSizesWithOneOutcomeAreNotOscillation();
    TestHostEditsStartTheHistoryAgain();
    TestFlipsBetweenConditionsAreOscillation();
    return s_failures == 0 ? 0 : 1;
}
