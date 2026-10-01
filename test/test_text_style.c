// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text style (record mui-0004): its defaults and checks, inheritance down
// the tree with relative sizes, line heights inherited as written,
// direct writes, moves between parents, tokens and themes, transitions
// reaching inheriting children, and what a change marks on host content.
// White-box: it reads the tree's marks.

#include "context.h"
#include "test_harness.h"
#include "tree.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/text_style.h"
#include "maul-ui/theme.h"
#include "maul-ui/token.h"
#include "maul-ui/transition.h"

#include <math.h>

static const muiNodeId s_nullNode = {0, 0};
static const muiStyleId s_nullStyle = {0, 0};
static const muiTransitionId s_nullTransition = {0, 0};

#define COLOR   MUI_PROPERTY_BIT(mui_propertyTextColor)
#define FONT    MUI_PROPERTY_BIT(mui_propertyFont)
#define SIZE    MUI_PROPERTY_BIT(mui_propertyFontSize)
#define LINE    MUI_PROPERTY_BIT(mui_propertyLineHeight)
#define SPACING MUI_PROPERTY_BIT(mui_propertyLetterSpacing)
#define WEIGHT  MUI_PROPERTY_BIT(mui_propertyFontWeight)
#define SLANT   MUI_PROPERTY_BIT(mui_propertyFontSlant)
#define ALIGN   MUI_PROPERTY_BIT(mui_propertyTextAlign)
#define WRAP    MUI_PROPERTY_BIT(mui_propertyTextWrap)
#define MS      1000000ull
#define T0      (1000 * MS)

static const muiColor s_black = {0.0f, 0.0f, 0.0f, 1.0f};
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

static muiNodeId Add(muiContext* context, muiNodeId parent, bool content)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "create node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    if (content)
    {
        muiLayoutStyle layout = muiDefaultLayoutStyle();
        layout.content = mui_contentHost;
        CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                      MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
              "host content");
    }
    return node;
}

static muiStyleId MakeStyle(muiContext* context)
{
    muiStyleId style = s_nullStyle;
    CHECK(muiCreateStyle(context, &style) == mui_success, "create style");
    return style;
}

static void Layout(muiContext* context, muiNodeId root, uint64_t timeNs)
{
    const muiLayoutInput input = {400.0f, 400.0f, Measure, NULL, timeNs, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static bool IsMarked(muiContext* context, muiNodeId node, muiStages stage)
{
    uint32_t slot = muiTreeResolve(&context->tree, node);
    return (muiTreeAt(&context->tree, slot)->dirty.request & stage) != 0;
}

// Drawing clears paint marks.
static void ClearPaint(muiContext* context, muiNodeId root)
{
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    CHECK(muiBuildDrawList(context, root, &input) == mui_success, "drawn");
}

static muiComputedTextStyle Computed(const muiContext* context, muiNodeId node)
{
    muiComputedTextStyle style = {0};
    CHECK(muiNode_GetTextStyle(context, node, &style) == mui_success, "computed");
    return style;
}

static bool SameColor(muiColor a, muiColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static muiDimension Value(float scale, float offset)
{
    return (muiDimension){scale, offset, mui_dimensionValue};
}

static void TestDefaults(void)
{
    const muiTextStyle defaults = muiDefaultTextStyle();
    CHECK(SameColor(defaults.color, s_black) && defaults.font == 0 &&
              defaults.size.offset == 16.0f && defaults.size.scale == 0.0f &&
              defaults.lineHeight.kind == mui_dimensionAuto && defaults.weight == 400.0f &&
              defaults.slant == mui_slantNormal && defaults.align == mui_textAlignStart &&
              defaults.wrap == mui_textWrap,
          "the defaults");
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, false);
    muiNodeId child = Add(context, root, true);
    // Before any layout, a new node reads the defaults too.
    muiComputedTextStyle before = Computed(context, child);
    CHECK(before.size == 16.0f && before.automaticLineHeight && before.weight == 400.0f,
          "a new node");
    Layout(context, root, T0);
    muiComputedTextStyle computed = Computed(context, child);
    CHECK(SameColor(computed.color, s_black) && computed.size == 16.0f &&
              computed.lineHeight == 0.0f && computed.automaticLineHeight &&
              computed.letterSpacing == 0.0f && computed.weight == 400.0f,
          "a child of a root with nothing");
    muiComputedTextStyle out;
    CHECK(muiNode_GetTextStyle(NULL, child, &out) == mui_errorInvalid &&
              muiNode_GetTextStyle(context, child, NULL) == mui_errorInvalid &&
              muiNode_GetTextStyle(context, s_nullNode, &out) == mui_errorInvalid,
          "arguments");
    CHECK(muiDestroyNode(context, child) == mui_success &&
              muiNode_GetTextStyle(context, child, &out) == mui_errorStale,
          "a gone node");
    muiDestroyContext(context);
}

static void TestChecks(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = Add(context, s_nullNode, false);
    muiStyleId style = MakeStyle(context);
    const muiTextStyle good = muiDefaultTextStyle();
    struct
    {
        muiPropertyMask mask;
        muiTextStyle values;
        bool valid;
        const char* what;
    } cases[] = {
        {WEIGHT, good, true, "the default weight"},
        {WEIGHT, good, false, "weight 0"},
        {WEIGHT, good, false, "weight 1001"},
        {WEIGHT, good, false, "weight NaN"},
        {SIZE, good, false, "a negative size"},
        {SIZE, good, false, "an automatic size"},
        {LINE, good, false, "a negative line height"},
        {LINE, good, false, "an automatic line height with a scale"},
        {LINE, good, true, "a line height of 1.5"},
        {SPACING, good, true, "a negative letter spacing"},
        {SPACING, good, false, "an automatic letter spacing"},
        {SPACING, good, false, "an infinite letter spacing"},
        {SLANT, good, false, "an unknown slant"},
        {ALIGN, good, false, "an unknown alignment"},
        {WRAP, good, false, "an unknown wrap"},
        {COLOR, good, false, "a color past 1"},
        {FONT, good, true, "any font key"},
        {MUI_PROPERTY_BIT(mui_propertyTextWrap + 1), good, false, "a bit past the group"},
    };
    cases[1].values.weight = 0.0f;
    cases[2].values.weight = 1001.0f;
    cases[3].values.weight = NAN;
    cases[4].values.size = Value(0.0f, -1.0f);
    cases[5].values.size = (muiDimension){0.0f, 0.0f, mui_dimensionAuto};
    cases[6].values.lineHeight = Value(-1.0f, 0.0f);
    cases[7].values.lineHeight = (muiDimension){1.0f, 0.0f, mui_dimensionAuto};
    cases[8].values.lineHeight = Value(1.5f, 0.0f);
    cases[9].values.letterSpacing = Value(-0.05f, -1.0f);
    cases[10].values.letterSpacing = (muiDimension){0.0f, 0.0f, mui_dimensionAuto};
    cases[11].values.letterSpacing = Value(INFINITY, 0.0f);
    cases[12].values.slant = 3;
    cases[13].values.align = 3;
    cases[14].values.wrap = 2;
    cases[15].values.color = (muiColor){2.0f, 0.0f, 0.0f, 1.0f};
    cases[16].values.font = UINT64_MAX;
    for (uint32_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        muiResult expected = cases[i].valid ? mui_success : mui_errorInvalid;
        CHECK(muiStyle_SetTextValues(context, style, mui_variantBase, &cases[i].values,
                                     cases[i].mask) == expected,
              cases[i].what);
        CHECK(muiNode_SetTextValues(context, node, &cases[i].values, cases[i].mask) == expected,
              cases[i].what);
    }
    CHECK(muiStyle_SetTextValues(context, style, mui_variantBase, NULL, COLOR) ==
                  mui_errorInvalid &&
              muiNode_SetTextValues(context, node, NULL, COLOR) == mui_errorInvalid &&
              muiStyle_SetTextValues(NULL, style, mui_variantBase, &good, COLOR) ==
                  mui_errorInvalid &&
              muiNode_SetTextValues(NULL, node, &good, COLOR) == mui_errorInvalid,
          "null arguments");
    // What a class sets reads back, the rest as the defaults.
    muiTextStyle set = muiDefaultTextStyle();
    set.color = s_red;
    set.size = Value(1.25f, 2.0f);
    CHECK(muiStyle_SetTextValues(context, style, mui_variantHovered, &set, COLOR | SIZE) ==
              mui_success,
          "hovered");
    muiTextStyle read;
    muiPropertyMask mask = 0;
    CHECK(muiStyle_GetTextValues(context, style, mui_variantHovered, &read, &mask) == mui_success &&
              mask == (COLOR | SIZE) && SameColor(read.color, s_red) && read.size.scale == 1.25f &&
              read.weight == 400.0f,
          "read back");
    CHECK(muiStyle_GetTextValues(context, style, mui_variantHovered, NULL, &mask) ==
              mui_errorInvalid,
          "no values out");
    muiDestroyContext(context);
}

static void TestInheritance(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, false);
    muiNodeId middle = Add(context, root, false);
    muiNodeId leaf = Add(context, middle, true);
    muiStyleId heading = MakeStyle(context);
    muiTextStyle values = muiDefaultTextStyle();
    values.color = s_red;
    values.font = 7;
    values.size = Value(0.0f, 20.0f);
    values.lineHeight = Value(1.5f, 0.0f);
    values.letterSpacing = Value(0.1f, 0.0f);
    values.weight = 700.0f;
    values.slant = mui_slantItalic;
    values.align = mui_textAlignCenter;
    values.wrap = mui_textNoWrap;
    CHECK(muiStyle_SetTextValues(context, heading, mui_variantBase, &values, MUI_TEXT_PROPERTIES) ==
              mui_success,
          "heading");
    CHECK(muiNode_SetClasses(context, root, &heading, 1) == mui_success, "class");
    Layout(context, root, T0);
    muiComputedTextStyle leafStyle = Computed(context, leaf);
    CHECK(SameColor(leafStyle.color, s_red) && leafStyle.font == 7 && leafStyle.size == 20.0f &&
              leafStyle.lineHeight == 30.0f && !leafStyle.automaticLineHeight &&
              leafStyle.letterSpacing == 2.0f && leafStyle.weight == 700.0f &&
              leafStyle.slant == mui_slantItalic && leafStyle.align == mui_textAlignCenter &&
              leafStyle.wrap == mui_textNoWrap,
          "a grandchild inherits every property");

    // The middle node scales the size by 1.5; the line height and letter
    // spacing, inherited as written, scale with it below.
    muiStyleId larger = MakeStyle(context);
    muiTextStyle scaled = muiDefaultTextStyle();
    scaled.size = Value(1.5f, 0.0f);
    scaled.color = s_blue;
    CHECK(muiStyle_SetTextValues(context, larger, mui_variantBase, &scaled, SIZE | COLOR) ==
                  mui_success &&
              muiNode_SetClasses(context, middle, &larger, 1) == mui_success,
          "larger");
    Layout(context, root, T0);
    leafStyle = Computed(context, leaf);
    CHECK(leafStyle.size == 30.0f && leafStyle.lineHeight == 45.0f &&
              leafStyle.letterSpacing == 3.0f && SameColor(leafStyle.color, s_blue) &&
              leafStyle.weight == 700.0f,
          "relative to the parent's size, ratios kept");
    CHECK(Computed(context, root).size == 20.0f && SameColor(Computed(context, root).color, s_red),
          "the root keeps its own");

    // A direct write over the class, and its reset back to it.
    muiTextStyle direct = muiDefaultTextStyle();
    direct.color = s_black;
    CHECK(muiNode_SetTextValues(context, middle, &direct, COLOR) == mui_success, "direct");
    Layout(context, root, T0);
    CHECK(SameColor(Computed(context, leaf).color, s_black), "a direct write reaches below");
    CHECK(muiNode_ResetProperties(context, middle, mui_groupText, COLOR) == mui_success, "reset");
    Layout(context, root, T0);
    CHECK(SameColor(Computed(context, leaf).color, s_blue), "back to the class");

    // A class change at the root reaches every node below.
    values.size = Value(0.0f, 10.0f);
    CHECK(muiStyle_SetTextValues(context, heading, mui_variantBase, &values, SIZE) == mui_success,
          "smaller root");
    Layout(context, root, T0);
    CHECK(Computed(context, leaf).size == 15.0f, "1.5 times 10");

    // Moved under a root with nothing, the leaf takes the defaults.
    muiNodeId other = Add(context, s_nullNode, false);
    CHECK(muiNode_Detach(context, leaf) == mui_success &&
              muiNode_InsertChild(context, other, leaf, s_nullNode) == mui_success,
          "moved");
    Layout(context, other, T0);
    leafStyle = Computed(context, leaf);
    CHECK(leafStyle.size == 16.0f && SameColor(leafStyle.color, s_black) &&
              leafStyle.automaticLineHeight,
          "the new parent's");
    muiDestroyContext(context);
}

static void TestHostContentMarks(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, false);
    muiNodeId label = Add(context, root, true);
    muiStyleId style = MakeStyle(context);
    muiTextStyle values = muiDefaultTextStyle();
    CHECK(muiNode_SetClasses(context, root, &style, 1) == mui_success, "class");
    Layout(context, root, T0);
    int measured = s_measured;
    Layout(context, root, T0);
    CHECK(s_measured == measured, "nothing changed, nothing measured");
    values.color = s_red;
    CHECK(muiStyle_SetTextValues(context, style, mui_variantBase, &values, COLOR) == mui_success,
          "color");
    Layout(context, root, T0);
    CHECK(s_measured == measured && SameColor(Computed(context, label).color, s_red),
          "color measures nothing");
    values.size = Value(0.0f, 24.0f);
    CHECK(muiStyle_SetTextValues(context, style, mui_variantBase, &values, SIZE) == mui_success,
          "size");
    Layout(context, root, T0);
    CHECK(s_measured > measured, "a size change measures the label again");
    measured = s_measured;
    values.weight = 600.0f;
    CHECK(muiStyle_SetTextValues(context, style, mui_variantBase, &values, WEIGHT) == mui_success,
          "weight");
    Layout(context, root, T0);
    CHECK(s_measured > measured, "so does a weight change");
    muiDestroyContext(context);
}

static void TestTokensAndThemes(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, false);
    muiNodeId themed = Add(context, root, false);
    muiNodeId label = Add(context, themed, true);
    muiTokenValue ink = {.type = mui_tokenColor};
    ink.color = s_red;
    muiTokenId token = {0, 0};
    CHECK(muiCreateToken(context, &ink, &token) == mui_success, "token");
    muiStyleId style = MakeStyle(context);
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyTextColor, token) ==
                  mui_success &&
              muiNode_SetClasses(context, root, &style, 1) == mui_success,
          "named");
    muiTokenValue body = {.type = mui_tokenDimension};
    body.dimension = Value(0.0f, 18.0f);
    muiTokenId size = {0, 0};
    CHECK(muiCreateToken(context, &body, &size) == mui_success &&
              muiStyle_SetToken(context, style, mui_variantBase, mui_propertyFontSize, size) ==
                  mui_success,
          "a size token");
    Layout(context, root, T0);
    CHECK(SameColor(Computed(context, label).color, s_red) &&
              Computed(context, label).size == 18.0f,
          "token values inherited");
    // A theme on the middle node, whose class names the same token.
    muiThemeId theme = {0, 0};
    muiTokenValue dark = {.type = mui_tokenColor};
    dark.color = s_blue;
    CHECK(muiCreateTheme(context, &theme) == mui_success &&
              muiTheme_SetTokenValue(context, theme, token, &dark) == mui_success &&
              muiNode_SetTheme(context, themed, theme) == mui_success &&
              muiNode_SetClasses(context, themed, &style, 1) == mui_success,
          "themed");
    Layout(context, root, T0);
    CHECK(SameColor(Computed(context, label).color, s_blue) &&
              SameColor(Computed(context, root).color, s_red),
          "the theme's value, inherited below it");
    muiDestroyContext(context);
}

static void TestTransitionsReachChildren(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, false);
    muiNodeId label = Add(context, root, true);
    muiStyleId style = MakeStyle(context);
    muiTextStyle values = muiDefaultTextStyle();
    values.size = Value(0.0f, 20.0f);
    CHECK(muiStyle_SetTextValues(context, style, mui_variantHovered, &values, SIZE) == mui_success,
          "hovered size");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = 100 * MS;
    def.easing = mui_easingLinear;
    muiTransitionId linear = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &linear) == mui_success &&
              muiStyle_SetTransition(context, style, mui_variantBase, linear, mui_groupText,
                                     SIZE) == mui_success &&
              muiNode_SetClasses(context, root, &style, 1) == mui_success,
          "a transition");
    Layout(context, root, T0);
    CHECK(Computed(context, label).size == 16.0f, "before");
    CHECK(muiNode_SetStates(context, root, mui_stateHovered) == mui_success, "hover");
    Layout(context, root, T0);
    int measured = s_measured;
    Layout(context, root, T0 + 50 * MS);
    CHECK(Computed(context, root).size == 18.0f && Computed(context, label).size == 18.0f,
          "halfway, at the root and below");
    CHECK(s_measured > measured, "a moving size measures the label");
    CHECK(muiNode_IsTransitioning(context, root, mui_propertyFontSize), "moving");
    Layout(context, root, T0 + 100 * MS);
    CHECK(Computed(context, label).size == 20.0f && !muiIsUpdatePending(context, root), "done");
    muiDestroyContext(context);
}

static void TestWhatChangesMark(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, false);
    muiNodeId box = Add(context, root, false);
    muiNodeId label = Add(context, box, true);
    Layout(context, root, T0);
    ClearPaint(context, root);
    muiTextStyle values = muiDefaultTextStyle();
    values.color = s_red;
    CHECK(muiNode_SetTextValues(context, root, &values, COLOR) == mui_success, "color");
    CHECK(IsMarked(context, label, mui_stagePaint) && !IsMarked(context, label, mui_stageLayout),
          "a color paints the label, no more");
    Layout(context, root, T0);
    ClearPaint(context, root);
    values.size = Value(0.0f, 30.0f);
    CHECK(muiNode_SetTextValues(context, root, &values, SIZE) == mui_success, "size");
    CHECK(IsMarked(context, label, mui_stageLayout) && IsMarked(context, box, mui_stageLayout) &&
              !IsMarked(context, root, mui_stageLayout),
          "a size lays out the label and its parent, not a root with no content");
    Layout(context, root, T0);
    int measured = s_measured;
    values.font = 9;
    CHECK(muiNode_SetTextValues(context, root, &values, FONT) == mui_success, "font");
    Layout(context, root, T0);
    CHECK(s_measured > measured && Computed(context, label).font == 9,
          "a font measures the label again");
    muiDestroyContext(context);
}

static void TestMovedWithoutClasses(void)
{
    // No class sets anything, so the style pass has no layer to apply;
    // a node moved still takes its new parent's text.
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, false);
    muiTextStyle values = muiDefaultTextStyle();
    values.size = Value(0.0f, 20.0f);
    CHECK(muiNode_SetTextValues(context, root, &values, SIZE) == mui_success, "size");
    muiNodeId label = Add(context, s_nullNode, true);
    Layout(context, label, T0);
    CHECK(Computed(context, label).size == 16.0f, "a root of its own");
    CHECK(muiNode_InsertChild(context, root, label, s_nullNode) == mui_success, "moved");
    Layout(context, root, T0);
    CHECK(Computed(context, label).size == 20.0f, "its new parent's size");
    muiDestroyContext(context);
}

static void TestSpringWeightStaysInRange(void)
{
    muiContext* context = MakeContext();
    muiNodeId label = Add(context, s_nullNode, true);
    muiStyleId style = MakeStyle(context);
    muiTextStyle values = muiDefaultTextStyle();
    values.weight = 1.0f;
    CHECK(muiStyle_SetTextValues(context, style, mui_variantBase, &values, WEIGHT) == mui_success,
          "the lightest");
    values.weight = 1000.0f;
    CHECK(muiStyle_SetTextValues(context, style, mui_variantHovered, &values, WEIGHT) ==
              mui_success,
          "the heaviest when hovered");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.kind = mui_transitionSpring;
    def.frequency = 3.0f;
    def.dampingRatio = 0.2f;
    muiTransitionId spring = s_nullTransition;
    CHECK(muiCreateTransition(context, &def, &spring) == mui_success &&
              muiStyle_SetTransition(context, style, mui_variantBase, spring, mui_groupText,
                                     WEIGHT) == mui_success &&
              muiNode_SetClasses(context, label, &style, 1) == mui_success,
          "a spring");
    Layout(context, label, T0);
    bool outside = false;
    for (int hovered = 1; hovered >= 0; hovered--)
    {
        CHECK(muiNode_SetStates(context, label, hovered ? mui_stateHovered : 0) == mui_success,
              "hover or not");
        uint64_t start = T0 + (hovered ? 0 : 3000 * MS);
        for (uint64_t t = start; t < start + 3000 * MS; t += 5 * MS)
        {
            Layout(context, label, t);
            float weight = Computed(context, label).weight;
            outside = outside || weight < 1.0f || weight > 1000.0f;
        }
    }
    CHECK(!outside && Computed(context, label).weight == 1.0f, "overshoots stop at 1 and at 1000");
    muiDestroyContext(context);
}

int main(void)
{
    TestDefaults();
    TestChecks();
    TestInheritance();
    TestHostContentMarks();
    TestTokensAndThemes();
    TestTransitionsReachChildren();
    TestWhatChangesMark();
    TestMovedWithoutClasses();
    TestSpringWeightStaysInRange();
    return s_failures == 0 ? 0 : 1;
}
