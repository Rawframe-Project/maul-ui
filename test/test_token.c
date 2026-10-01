// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Tokens: their checks, values and aliases, classes that name them,
// resolution through them, theme switches, and limits (record mui-0004).

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/token.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <math.h>

static const muiNodeId s_nullNode = {0, 0};
static const muiStyleId s_nullStyle = {0, 0};
static const muiTokenId s_nullToken = {0, 0};

#define MS 1000000ull
#define T0 (1000 * MS)

static const muiColor s_red = {1.0f, 0.0f, 0.0f, 1.0f};
static const muiColor s_blue = {0.0f, 0.0f, 1.0f, 1.0f};

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

static muiTokenValue Color(muiColor color)
{
    muiTokenValue value = {.type = mui_tokenColor};
    value.color = color;
    return value;
}

static muiTokenValue Number(float number)
{
    muiTokenValue value = {.type = mui_tokenNumber};
    value.number = number;
    return value;
}

static muiTokenId MakeToken(muiContext* context, muiTokenValue value)
{
    muiTokenId token = s_nullToken;
    CHECK(muiCreateToken(context, &value, &token) == mui_success, "create token");
    return token;
}

static bool SameId(muiTokenId a, muiTokenId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static bool SameColor(muiColor a, muiColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void Layout(muiContext* context, muiNodeId node, uint64_t timeNs)
{
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, timeNs};
    CHECK(muiComputeLayout(context, node, &input) == mui_success, "layout");
}

static muiColor Background(const muiContext* context, muiNodeId node)
{
    muiVisualStyle values = muiDefaultVisualStyle();
    CHECK(muiNode_GetVisualStyle(context, node, &values) == mui_success, "read");
    return values.background;
}

static float PaddingStart(const muiContext* context, muiNodeId node)
{
    muiLayoutStyle values = muiDefaultLayoutStyle();
    CHECK(muiNode_GetLayoutStyle(context, node, &values) == mui_success, "read");
    return values.padding.start;
}

static void TestTokensAreChecked(void)
{
    muiContext* context = MakeContext();
    muiTokenId token = s_nullToken;
    muiTokenValue value = Color(s_red);
    CHECK(muiCreateToken(NULL, &value, &token) == mui_errorInvalid &&
              muiCreateToken(context, NULL, &token) == mui_errorInvalid &&
              muiCreateToken(context, &value, NULL) == mui_errorInvalid,
          "null arguments");
    value.color.g = 1.5f;
    CHECK(muiCreateToken(context, &value, &token) == mui_errorInvalid && token.index1 == 0,
          "a color component above 1");
    value = Number(NAN);
    CHECK(muiCreateToken(context, &value, &token) == mui_errorInvalid, "a NaN number");
    value = (muiTokenValue){.type = mui_tokenDimension};
    value.dimension = (muiDimension){0.0f, 1.0f, 7};
    CHECK(muiCreateToken(context, &value, &token) == mui_errorInvalid, "an unknown kind");
    value = (muiTokenValue){.type = mui_tokenShadow};
    value.shadow.blur = -1.0f;
    CHECK(muiCreateToken(context, &value, &token) == mui_errorInvalid, "a negative blur");
    value = (muiTokenValue){.type = mui_tokenGradient};
    value.gradient = (muiGradient){mui_gradientLinear, 1, 0.0f, {{s_red, 0.0f}}};
    CHECK(muiCreateToken(context, &value, &token) == mui_errorInvalid, "one stop");
    value = (muiTokenValue){.type = 0};
    CHECK(muiCreateToken(context, &value, &token) == mui_errorInvalid, "no type");
    value = (muiTokenValue){.type = 6};
    CHECK(muiCreateToken(context, &value, &token) == mui_errorInvalid, "an unknown type");

    // Each type, at its edges.
    value = Number(-3.0f);
    CHECK(muiCreateToken(context, &value, &token) == mui_success, "a negative number");
    value = (muiTokenValue){.type = mui_tokenDimension};
    value.dimension = (muiDimension){0.5f, -2.0f, mui_dimensionValue};
    CHECK(muiCreateToken(context, &value, &token) == mui_success, "a dimension");
    value = (muiTokenValue){.type = mui_tokenShadow};
    value.shadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.5f}, 1.0f, 2.0f, 3.0f, -1.0f};
    CHECK(muiCreateToken(context, &value, &token) == mui_success, "a shadow");
    value = (muiTokenValue){.type = mui_tokenGradient};
    value.gradient = (muiGradient){mui_gradientRadial, 2, 0.0f, {{s_red, 0.0f}, {s_blue, 1.0f}}};
    CHECK(muiCreateToken(context, &value, &token) == mui_success, "a gradient");
    muiTokenValue read;
    CHECK(muiGetTokenValue(context, token, &read, NULL) == mui_success &&
              read.type == mui_tokenGradient && read.gradient.kind == mui_gradientRadial &&
              SameColor(read.gradient.stops[1].color, s_blue),
          "read back");
    CHECK(muiGetTokenValue(NULL, token, &read, NULL) == mui_errorInvalid &&
              muiGetTokenValue(context, token, NULL, NULL) == mui_errorInvalid &&
              muiGetTokenValue(context, s_nullToken, &read, NULL) == mui_errorInvalid,
          "bad reads");
    CHECK(muiDestroyToken(context, token) == mui_success, "destroy");
    CHECK(muiDestroyToken(context, token) == mui_errorStale &&
              muiGetTokenValue(context, token, &read, NULL) == mui_errorStale,
          "stale");
    CHECK(muiDestroyToken(NULL, token) == mui_errorInvalid &&
              muiDestroyToken(context, s_nullToken) == mui_errorInvalid,
          "bad destroys");
    muiDestroyContext(context);

    muiLimits limits = muiDefaultContextDef().limits;
    limits.tokens = 1;
    context = MakeContextWith(limits);
    (void)MakeToken(context, Number(1.0f));
    value = Number(2.0f);
    CHECK(muiCreateToken(context, &value, &token) == mui_errorCapacity && token.index1 == 0,
          "the token limit");
    muiDestroyContext(context);
}

static void TestValuesAndAliases(void)
{
    muiContext* context = MakeContext();
    muiTokenId red = MakeToken(context, Color(s_red));
    muiTokenId blue = MakeToken(context, Color(s_blue));
    muiTokenId accent = MakeToken(context, Color(s_red));
    muiTokenId gap = MakeToken(context, Number(4.0f));

    muiTokenValue value = Number(8.0f);
    CHECK(muiSetTokenValue(context, accent, &value) == mui_errorInvalid, "another type");
    value = Color((muiColor){2.0f, 0.0f, 0.0f, 1.0f});
    CHECK(muiSetTokenValue(context, accent, &value) == mui_errorInvalid, "an invalid value");
    CHECK(muiSetTokenValue(NULL, accent, &value) == mui_errorInvalid &&
              muiSetTokenValue(context, accent, NULL) == mui_errorInvalid &&
              muiSetTokenValue(context, s_nullToken, &value) == mui_errorInvalid,
          "bad sets");

    CHECK(muiSetTokenAlias(context, accent, gap) == mui_errorInvalid, "an alias of another type");
    CHECK(muiSetTokenAlias(context, accent, accent) == mui_errorInvalid, "an alias of itself");
    CHECK(muiSetTokenAlias(context, accent, s_nullToken) == mui_errorInvalid &&
              muiSetTokenAlias(NULL, accent, blue) == mui_errorInvalid &&
              muiSetTokenAlias(context, s_nullToken, blue) == mui_errorInvalid,
          "bad aliases");
    CHECK(muiSetTokenAlias(context, accent, blue) == mui_success, "accent is blue");
    muiTokenValue read;
    muiTokenId alias = s_nullToken;
    CHECK(muiGetTokenValue(context, accent, &read, &alias) == mui_success &&
              SameColor(read.color, s_blue) && SameId(alias, blue),
          "through the alias");
    CHECK(muiSetTokenAlias(context, blue, accent) == mui_errorInvalid, "a cycle of two");
    muiTokenId link = MakeToken(context, Color(s_red));
    CHECK(muiSetTokenAlias(context, link, accent) == mui_success, "a chain");
    CHECK(muiSetTokenAlias(context, blue, link) == mui_errorInvalid, "a cycle of three");
    CHECK(muiGetTokenValue(context, link, &read, NULL) == mui_success &&
              SameColor(read.color, s_blue),
          "down the chain");

    CHECK(muiDestroyToken(context, blue) == mui_success, "the end of the chain goes");
    CHECK(muiGetTokenValue(context, link, &read, &alias) == mui_empty &&
              read.type == mui_tokenColor && read.color.a == 0.0f && SameId(alias, accent),
          "no value through it");
    CHECK(muiSetTokenAlias(context, link, blue) == mui_errorStale, "a gone target");
    value = Color(s_red);
    CHECK(muiSetTokenValue(context, accent, &value) == mui_success, "a value again");
    CHECK(muiGetTokenValue(context, accent, &read, &alias) == mui_success &&
              SameColor(read.color, s_red) && alias.index1 == 0,
          "ends the alias");
    CHECK(muiGetTokenValue(context, link, &read, NULL) == mui_success &&
              SameColor(read.color, s_red),
          "and the chain gives it");
    (void)red;
    muiDestroyContext(context);
}

static void TestClassesNameTokens(void)
{
    muiContext* context = MakeContext();
    muiStyleId style = MakeStyle(context);
    muiTokenId accent = MakeToken(context, Color(s_red));
    muiTokenId gap = MakeToken(context, Number(4.0f));
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyJustify, gap) ==
              mui_errorInvalid,
          "an enumerator takes no token");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyPaddingStart, accent) ==
              mui_errorInvalid,
          "a color for a number");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyImageSlice, gap) ==
              mui_errorInvalid,
          "insets take no token");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, (muiProperty)(mui_propertyContent + 1),
                            gap) == mui_errorInvalid &&
              muiStyle_SetToken(context, style, (muiVariant)99, mui_propertyPaddingStart, gap) ==
                  mui_errorInvalid &&
              muiStyle_SetToken(context, s_nullStyle, mui_variantBase, mui_propertyPaddingStart,
                                gap) == mui_errorInvalid &&
              muiStyle_SetToken(NULL, style, mui_variantBase, mui_propertyPaddingStart, gap) ==
                  mui_errorInvalid,
          "bad arguments");

    // A literal and a name end each other.
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding.start = 2.0f;
    const muiPropertyMask padding = MUI_PROPERTY_BIT(mui_propertyPaddingStart);
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &layout, padding) ==
              mui_success,
          "a literal");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyPaddingStart, gap) ==
              mui_success,
          "a name");
    muiTokenId named = s_nullToken;
    muiPropertyMask mask = 0;
    CHECK(muiStyle_GetToken(context, style, mui_variantBase, mui_propertyPaddingStart, &named) ==
                  mui_success &&
              SameId(named, gap),
          "named");
    CHECK(muiStyle_GetLayoutValues(context, style, mui_variantBase, &layout, &mask) ==
                  mui_success &&
              mask == 0,
          "the literal is gone");
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &layout, padding) ==
              mui_success,
          "a literal again");
    CHECK(muiStyle_GetToken(context, style, mui_variantBase, mui_propertyPaddingStart, &named) ==
                  mui_success &&
              named.index1 == 0,
          "the name is gone");
    CHECK(muiStyle_SetToken(context, style, mui_variantHovered, mui_propertyBackground, accent) ==
              mui_success,
          "a hovered name");
    CHECK(muiStyle_SetToken(context, style, mui_variantHovered, mui_propertyBackground,
                            s_nullToken) == mui_success,
          "unnamed");
    CHECK(muiStyle_GetToken(context, style, mui_variantHovered, mui_propertyBackground, &named) ==
                  mui_success &&
              named.index1 == 0,
          "no name");
    CHECK(muiStyle_SetToken(context, style, mui_variantHovered, mui_propertyBackground, accent) ==
                  mui_success &&
              muiStyle_ResetProperties(context, style, mui_variantHovered, mui_groupVisual,
                                       MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "named and reset");
    CHECK(muiStyle_GetToken(context, style, mui_variantHovered, mui_propertyBackground, &named) ==
                  mui_success &&
              named.index1 == 0,
          "a reset ends the name");

    // A condition's variant may not name what the condition reads.
    muiTokenValue width = {.type = mui_tokenDimension};
    width.dimension = (muiDimension){0.0f, 80.0f, mui_dimensionValue};
    muiTokenId narrowWidth = MakeToken(context, width);
    muiCondition narrow = muiDefaultCondition();
    narrow.width = (muiRange){0.0f, 100.0f};
    muiVariant variant = mui_variantBase;
    CHECK(muiStyle_AddCondition(context, style, &narrow, &variant) == mui_success, "condition");
    CHECK(muiStyle_SetToken(context, style, variant, mui_propertyWidth, narrowWidth) ==
              mui_errorInvalid,
          "a width for a width condition");
    muiCondition tall = muiDefaultCondition();
    tall.height = (muiRange){100.0f, INFINITY};
    CHECK(muiStyle_SetCondition(context, style, variant, &tall) == mui_success &&
              muiStyle_SetToken(context, style, variant, mui_propertyWidth, narrowWidth) ==
                  mui_success,
          "on a height condition");
    CHECK(muiStyle_SetCondition(context, style, variant, &narrow) == mui_errorInvalid,
          "which then may not read width");

    CHECK(muiDestroyToken(context, gap) == mui_success, "destroy");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyPaddingStart, gap) ==
              mui_errorStale,
          "a gone token");
    CHECK(muiStyle_GetToken(NULL, style, mui_variantBase, mui_propertyWidth, &named) ==
                  mui_errorInvalid &&
              muiStyle_GetToken(context, style, mui_variantBase, mui_propertyWidth, NULL) ==
                  mui_errorInvalid &&
              muiStyle_GetToken(context, style, (muiVariant)99, mui_propertyWidth, &named) ==
                  mui_errorInvalid &&
              muiStyle_GetToken(context, style, mui_variantBase,
                                (muiProperty)(mui_propertyContent + 1), &named) == mui_errorInvalid,
          "bad reads");
    CHECK(muiDestroyStyle(context, style) == mui_success &&
              muiStyle_GetToken(context, style, mui_variantBase, mui_propertyWidth, &named) ==
                  mui_errorStale,
          "a gone class");
    muiDestroyContext(context);
}

static void TestResolutionReadsTokens(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiTokenId red = MakeToken(context, Color(s_red));
    muiTokenId blue = MakeToken(context, Color(s_blue));
    muiTokenId surface = MakeToken(context, Color(s_red));
    CHECK(muiSetTokenAlias(context, surface, red) == mui_success, "surface is red");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyBackground, surface) ==
              mui_success,
          "named");
    muiTokenValue width = {.type = mui_tokenDimension};
    width.dimension = (muiDimension){0.0f, 120.0f, mui_dimensionValue};
    muiTokenId wide = MakeToken(context, width);
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyWidth, wide) ==
              mui_success,
          "a width");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    CHECK(SameColor(Background(context, node), s_red), "through the alias");
    CHECK(muiNode_GetRect(context, node).width == 120.0f, "a token lays out");

    // A theme switch: the semantic token points elsewhere.
    CHECK(muiSetTokenAlias(context, surface, blue) == mui_success, "surface is blue");
    CHECK(muiIsUpdatePending(context, node), "every node restyles");
    Layout(context, node, T0);
    CHECK(SameColor(Background(context, node), s_blue), "switched");
    muiTokenValue value = Color(s_red);
    CHECK(muiSetTokenValue(context, blue, &value) == mui_success, "blue made red");
    Layout(context, node, T0);
    CHECK(SameColor(Background(context, node), s_red), "a value change reaches it");
    width.dimension.offset = 140.0f;
    CHECK(muiSetTokenValue(context, wide, &width) == mui_success, "wider");
    Layout(context, node, T0);
    CHECK(muiNode_GetRect(context, node).width == 140.0f, "laid out again");

    // A layer whose token gives nothing the property allows is silent.
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding.start = 4.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyPaddingStart)) == mui_success,
          "base padding");
    muiTokenId inset = MakeToken(context, Number(-5.0f));
    CHECK(muiStyle_SetToken(context, style, mui_variantHovered, mui_propertyPaddingStart, inset) ==
              mui_success,
          "hovered padding named");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    Layout(context, node, T0);
    CHECK(PaddingStart(context, node) == 4.0f, "a negative padding leaves the base");
    value = Number(9.0f);
    CHECK(muiSetTokenValue(context, inset, &value) == mui_success, "9");
    Layout(context, node, T0);
    CHECK(PaddingStart(context, node) == 9.0f, "a valid one applies");
    CHECK(muiDestroyToken(context, inset) == mui_success, "gone");
    Layout(context, node, T0);
    CHECK(PaddingStart(context, node) == 4.0f, "a gone token leaves the base");
    CHECK(muiDestroyToken(context, surface) == mui_success, "the background's token goes");
    Layout(context, node, T0);
    CHECK(Background(context, node).a == 0.0f, "the default background");
    muiDestroyContext(context);
}

static void TestThemeSwitchesMove(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiTokenId light = MakeToken(context, Color((muiColor){1.0f, 1.0f, 1.0f, 1.0f}));
    muiTokenId dark = MakeToken(context, Color((muiColor){0.0f, 0.0f, 0.0f, 1.0f}));
    muiTokenId surface = MakeToken(context, Color(s_red));
    CHECK(muiSetTokenAlias(context, surface, light) == mui_success, "light");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyBackground, surface) ==
              mui_success,
          "named");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = 100 * MS;
    def.easing = mui_easingLinear;
    muiTransitionId linear = {0, 0};
    CHECK(muiCreateTransition(context, &def, &linear) == mui_success, "transition");
    CHECK(muiStyle_SetTransition(context, style, mui_variantBase, linear, mui_groupVisual,
                                 MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "transition named");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    CHECK(muiSetTokenAlias(context, surface, dark) == mui_success, "dark");
    Layout(context, node, T0);
    Layout(context, node, T0 + 50 * MS);
    muiColor halfway = Background(context, node);
    CHECK(halfway.r > 0.05f && halfway.r < 0.95f && halfway.r == halfway.g, "a gray halfway");
    Layout(context, node, T0 + 100 * MS);
    CHECK(SameColor(Background(context, node), (muiColor){0.0f, 0.0f, 0.0f, 1.0f}), "dark");
    muiDestroyContext(context);
}

static void TestNameLimit(void)
{
    muiLimits limits = muiDefaultContextDef().limits;
    limits.tokenNames = 1;
    limits.propertySets = 2;
    muiContext* context = MakeContextWith(limits);
    muiStyleId style = MakeStyle(context);
    muiTokenId gap = MakeToken(context, Number(4.0f));
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyPaddingStart, gap) ==
              mui_success,
          "one name");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyPaddingStart, gap) ==
              mui_success,
          "the same property again takes no name");
    CHECK(muiStyle_SetToken(context, style, mui_variantHovered, mui_propertyPaddingEnd, gap) ==
              mui_errorCapacity,
          "the name limit");
    // The hovered variant took no set: one is left for another class.
    muiStyleId other = MakeStyle(context);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    CHECK(muiStyle_SetLayoutValues(context, other, mui_variantBase, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "a set left");
    // A set whose last name goes is given back.
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyPaddingStart,
                            s_nullToken) == mui_success,
          "unnamed");
    muiStyleId third = MakeStyle(context);
    CHECK(muiStyle_SetLayoutValues(context, third, mui_variantBase, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "its set taken by another class");
    CHECK(muiDestroyStyle(context, third) == mui_success, "and given back");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyPaddingStart, gap) ==
              mui_success,
          "named again");
    // A destroyed class gives its names back.
    CHECK(muiDestroyStyle(context, style) == mui_success, "destroy the class");
    CHECK(muiStyle_SetToken(context, other, mui_variantBase, mui_propertyPaddingEnd, gap) ==
              mui_success,
          "the name is free again");
    // So do cleared conditions.
    muiVariant variant = mui_variantBase;
    muiCondition any = muiDefaultCondition();
    any.textScale = (muiRange){2.0f, INFINITY};
    CHECK(muiStyle_ResetProperties(context, other, mui_variantBase, mui_groupLayout,
                                   MUI_LAYOUT_PROPERTIES) == mui_success &&
              muiStyle_ResetProperties(context, other, mui_variantBase, mui_groupVisual,
                                       MUI_VISUAL_PROPERTIES) == mui_success &&
              muiStyle_AddCondition(context, other, &any, &variant) == mui_success &&
              muiStyle_SetToken(context, other, variant, mui_propertyPaddingEnd, gap) ==
                  mui_success,
          "a condition's name");
    CHECK(muiStyle_ClearConditions(context, other) == mui_success &&
              muiStyle_SetToken(context, other, mui_variantBase, mui_propertyPaddingEnd, gap) ==
                  mui_success,
          "free once the conditions are cleared");
    muiDestroyContext(context);
}

// Edge cases of names beside the rest of the style system.
static void TestNamesAmongTheRest(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiTokenId gap = MakeToken(context, Number(4.0f));
    muiTokenId half = MakeToken(context, Number(0.5f));
    muiTokenId blue = MakeToken(context, Color(s_blue));
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyPaddingStart, gap) ==
                  mui_success &&
              muiStyle_SetToken(context, style, mui_variantBase, mui_propertyOpacity, half) ==
                  mui_success,
          "padding and opacity");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    Layout(context, node, T0);
    muiVisualStyle visual = muiDefaultVisualStyle();
    CHECK(muiNode_GetVisualStyle(context, node, &visual) == mui_success && visual.opacity == 0.5f,
          "an opacity token");
    muiTokenValue value = Number(1.5f);
    CHECK(muiSetTokenValue(context, half, &value) == mui_success, "1.5");
    Layout(context, node, T0);
    CHECK(muiNode_GetVisualStyle(context, node, &visual) == mui_success && visual.opacity == 1.0f,
          "too much for an opacity: silent");

    // A direct write wins over a name.
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding.start = 7.0f;
    CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyPaddingStart)) == mui_success,
          "direct");
    // Hovered changes another layout value, so the node's layout values
    // are written as a whole.
    layout.padding.end = 3.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantHovered, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyPaddingEnd)) == mui_success,
          "hovered padding end");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "restyle");
    Layout(context, node, T0);
    CHECK(PaddingStart(context, node) == 7.0f, "the direct write stands");

    // A variant holding only a name keeps its set when its transitions go.
    CHECK(muiStyle_SetToken(context, style, mui_variantHovered, mui_propertyBackground, blue) ==
              mui_success,
          "hovered background");
    muiTransitionDef def = muiDefaultTransitionDef();
    muiTransitionId transition = {0, 0};
    CHECK(muiCreateTransition(context, &def, &transition) == mui_success, "transition");
    const muiTransitionId none = {0, 0};
    CHECK(muiStyle_SetTransition(context, style, mui_variantHovered, transition, mui_groupLayout,
                                 MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success &&
              muiStyle_SetTransition(context, style, mui_variantHovered, none, mui_groupLayout,
                                     MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "a transition named and taken away");
    Layout(context, node, T0);
    CHECK(SameColor(Background(context, node), s_blue), "the name stays");

    // A condition's variant with names alone applies them.
    muiStyleId scaled = MakeStyle(context);
    muiCondition large = muiDefaultCondition();
    large.textScale = (muiRange){2.0f, INFINITY};
    muiVariant variant = mui_variantBase;
    CHECK(muiStyle_AddCondition(context, scaled, &large, &variant) == mui_success &&
              muiStyle_SetToken(context, scaled, variant, mui_propertyBorderColorTop, blue) ==
                  mui_success,
          "a conditional name");
    muiNodeId other = MakeNode(context);
    CHECK(muiNode_SetClasses(context, other, &scaled, 1) == mui_success, "class");
    muiEnvironment environment = muiDefaultEnvironment();
    environment.textScale = 2.0f;
    CHECK(muiSetContextEnvironment(context, &environment) == mui_success, "large text");
    Layout(context, other, T0);
    CHECK(muiNode_GetVisualStyle(context, other, &visual) == mui_success &&
              SameColor(visual.borderColor.top, s_blue),
          "applied");
    muiDestroyContext(context);
}

int main(void)
{
    TestTokensAreChecked();
    TestValuesAndAliases();
    TestClassesNameTokens();
    TestResolutionReadsTokens();
    TestThemeSwitchesMove();
    TestNameLimit();
    TestNamesAmongTheRest();
    return s_failures == 0 ? 0 : 1;
}
