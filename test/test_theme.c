// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Themes: their checks and overrides, the subtrees that read them, nested
// themes, moves, limits and cycles (record mui-0004).

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/theme.h"
#include "maul-ui/token.h"
#include "maul-ui/visual.h"

static const muiNodeId s_nullNode = {0, 0};
static const muiThemeId s_nullTheme = {0, 0};
static const muiTokenId s_nullToken = {0, 0};

static const muiColor s_light = {1.0f, 1.0f, 1.0f, 1.0f};
static const muiColor s_dark = {0.1f, 0.1f, 0.1f, 1.0f};
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

static muiNodeId MakeChild(muiContext* context, muiNodeId parent)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "create node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    return node;
}

static muiTokenValue Color(muiColor color)
{
    muiTokenValue value = {.type = mui_tokenColor};
    value.color = color;
    return value;
}

static muiTokenId MakeToken(muiContext* context, muiColor color)
{
    muiTokenValue value = Color(color);
    muiTokenId token = s_nullToken;
    CHECK(muiCreateToken(context, &value, &token) == mui_success, "create token");
    return token;
}

static muiThemeId MakeTheme(muiContext* context)
{
    muiThemeId theme = s_nullTheme;
    CHECK(muiCreateTheme(context, &theme) == mui_success, "create theme");
    return theme;
}

static void Override(muiContext* context, muiThemeId theme, muiTokenId token, muiColor color)
{
    muiTokenValue value = Color(color);
    CHECK(muiTheme_SetTokenValue(context, theme, token, &value) == mui_success, "override");
}

static bool SameColor(muiColor a, muiColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static muiColor Background(const muiContext* context, muiNodeId node)
{
    muiVisualStyle values = muiDefaultVisualStyle();
    CHECK(muiNode_GetVisualStyle(context, node, &values) == mui_success, "read");
    return values.background;
}

static muiColor Read(const muiContext* context, muiNodeId node, muiTokenId token)
{
    muiTokenValue value = Color(s_red);
    CHECK(muiNode_GetTokenValue(context, node, token, &value) == mui_success, "token at node");
    return value.color;
}

// A class whose background is the token, on every node given.
static void Paint(muiContext* context, muiTokenId token, const muiNodeId* nodes, uint32_t count)
{
    muiStyleId style = {0, 0};
    CHECK(muiCreateStyle(context, &style) == mui_success, "style");
    CHECK(muiStyle_SetToken(context, style, mui_variantBase, mui_propertyBackground, token) ==
              mui_success,
          "named");
    for (uint32_t i = 0; i < count; i++)
    {
        CHECK(muiNode_SetClasses(context, nodes[i], &style, 1) == mui_success, "class");
    }
}

static void TestThemesAreChecked(void)
{
    muiContext* context = MakeContext();
    muiThemeId theme = s_nullTheme;
    CHECK(muiCreateTheme(NULL, &theme) == mui_errorInvalid &&
              muiCreateTheme(context, NULL) == mui_errorInvalid,
          "null arguments");
    theme = MakeTheme(context);
    muiTokenId surface = MakeToken(context, s_light);
    muiTokenValue number = {.type = mui_tokenNumber};
    number.number = 2.0f;
    muiTokenId gap = s_nullToken;
    CHECK(muiCreateToken(context, &number, &gap) == mui_success, "a number");

    CHECK(muiTheme_SetTokenValue(context, theme, surface, &number) == mui_errorInvalid,
          "a value of another type");
    muiTokenValue bad = Color((muiColor){1.0f, 2.0f, 0.0f, 1.0f});
    CHECK(muiTheme_SetTokenValue(context, theme, surface, &bad) == mui_errorInvalid,
          "an invalid value");
    muiTokenValue value = Color(s_dark);
    CHECK(muiTheme_SetTokenValue(NULL, theme, surface, &value) == mui_errorInvalid &&
              muiTheme_SetTokenValue(context, theme, surface, NULL) == mui_errorInvalid &&
              muiTheme_SetTokenValue(context, s_nullTheme, surface, &value) == mui_errorInvalid &&
              muiTheme_SetTokenValue(context, theme, s_nullToken, &value) == mui_errorInvalid,
          "bad sets");
    muiTokenValue read;
    muiTokenId alias = surface;
    CHECK(muiTheme_GetToken(context, theme, surface, &read, &alias) == mui_empty &&
              read.type == mui_tokenColor && alias.index1 == 0,
          "not overridden");
    Override(context, theme, surface, s_dark);
    CHECK(muiTheme_GetToken(context, theme, surface, &read, &alias) == mui_success &&
              SameColor(read.color, s_dark) && alias.index1 == 0,
          "overridden");
    CHECK(muiTheme_SetTokenAlias(context, theme, surface, gap) == mui_errorInvalid,
          "an alias of another type");
    muiTokenId brand = MakeToken(context, s_blue);
    CHECK(muiTheme_SetTokenAlias(context, theme, surface, brand) == mui_success &&
              muiTheme_GetToken(context, theme, surface, &read, &alias) == mui_success &&
              alias.index1 == brand.index1,
          "an alias override");
    // brand aliases surface in the context; in the theme surface aliases
    // brand: a cycle through the theme.
    CHECK(muiSetTokenAlias(context, brand, surface) == mui_success, "context alias");
    muiThemeId other = MakeTheme(context);
    CHECK(muiTheme_SetTokenAlias(context, other, surface, brand) == mui_errorInvalid,
          "a cycle through a theme");
    // A cycle through the theme's own overrides: the accent is the
    // surface there, and the surface may not then be the accent.
    muiTokenId accent = MakeToken(context, s_red);
    muiThemeId third = MakeTheme(context);
    CHECK(muiTheme_SetTokenAlias(context, third, accent, surface) == mui_success,
          "the accent is the surface");
    CHECK(muiTheme_SetTokenAlias(context, third, surface, accent) == mui_errorInvalid,
          "a cycle through the theme's own overrides");
    CHECK(muiTheme_SetTokenAlias(context, other, surface, s_nullToken) == mui_errorInvalid &&
              muiTheme_SetTokenAlias(NULL, other, surface, brand) == mui_errorInvalid,
          "bad aliases");
    CHECK(muiTheme_ResetToken(context, theme, surface) == mui_success &&
              muiTheme_GetToken(context, theme, surface, &read, NULL) == mui_empty,
          "reset");
    CHECK(muiTheme_ResetToken(context, theme, surface) == mui_success, "reset again");
    CHECK(muiTheme_ResetToken(context, theme, s_nullToken) == mui_errorInvalid &&
              muiTheme_ResetToken(NULL, theme, surface) == mui_errorInvalid,
          "bad resets");
    CHECK(muiTheme_GetToken(NULL, theme, surface, &read, NULL) == mui_errorInvalid &&
              muiTheme_GetToken(context, theme, surface, NULL, NULL) == mui_errorInvalid,
          "bad reads");
    CHECK(muiDestroyTheme(context, theme) == mui_success, "destroy");
    CHECK(muiDestroyTheme(context, theme) == mui_errorStale &&
              muiTheme_SetTokenValue(context, theme, surface, &value) == mui_errorStale &&
              muiTheme_SetTokenAlias(context, theme, surface, brand) == mui_errorStale &&
              muiTheme_ResetToken(context, theme, surface) == mui_errorStale &&
              muiTheme_GetToken(context, theme, surface, &read, NULL) == mui_errorStale,
          "stale theme");
    CHECK(muiDestroyToken(context, gap) == mui_success, "a token goes");
    CHECK(muiTheme_SetTokenValue(context, other, gap, &number) == mui_errorStale &&
              muiTheme_GetToken(context, other, gap, &read, NULL) == mui_errorStale,
          "stale token");
    muiDestroyContext(context);
}

// A cycle is found through the first token made (found by a mutant that
// stopped every walk there): x is the first token, the first is y, so y
// may not be x.
static void TestCycleThroughTheFirstToken(void)
{
    muiContext* context = MakeContext();
    muiTokenId first = MakeToken(context, s_light);
    muiTokenId x = MakeToken(context, s_blue);
    muiTokenId y = MakeToken(context, s_red);
    CHECK(muiSetTokenAlias(context, x, first) == mui_success &&
              muiSetTokenAlias(context, first, y) == mui_success,
          "x is the first, the first is y");
    muiThemeId theme = MakeTheme(context);
    CHECK(muiTheme_SetTokenAlias(context, theme, y, x) == mui_errorInvalid, "y may not be x");
    muiDestroyContext(context);
}

// Every theme at the limit overrides every token, each its own colour,
// and each reads back as set (found by a mutant placing the themes'
// tables a row too far, the last past the end).
static void TestEveryOverride(void)
{
    enum
    {
        THEMES = 3,
        TOKENS = 16
    };
    muiLimits limits = muiDefaultContextDef().limits;
    limits.themes = THEMES;
    limits.tokens = TOKENS;
    limits.themeOverrides = THEMES * TOKENS;
    muiContext* context = MakeContextWith(limits);
    muiThemeId themes[THEMES];
    muiTokenId tokens[TOKENS];
    for (uint32_t t = 0; t < THEMES; t++)
    {
        themes[t] = MakeTheme(context);
    }
    for (uint32_t k = 0; k < TOKENS; k++)
    {
        tokens[k] = MakeToken(context, s_light);
    }
    for (uint32_t t = 0; t < THEMES; t++)
    {
        for (uint32_t k = 0; k < TOKENS; k++)
        {
            Override(context, themes[t], tokens[k],
                     (muiColor){(float)t / 4.0f, (float)k / 16.0f, 0.0f, 1.0f});
        }
    }
    bool held = true;
    for (uint32_t t = 0; t < THEMES; t++)
    {
        for (uint32_t k = 0; k < TOKENS; k++)
        {
            muiTokenValue read;
            held = held &&
                   muiTheme_GetToken(context, themes[t], tokens[k], &read, NULL) == mui_success &&
                   SameColor(read.color, (muiColor){(float)t / 4.0f, (float)k / 16.0f, 0.0f, 1.0f});
        }
    }
    CHECK(held, "every override as set");
    muiDestroyContext(context);
}

static void TestLimits(void)
{
    muiLimits limits = muiDefaultContextDef().limits;
    limits.themes = 1;
    limits.themeOverrides = 1;
    limits.tokens = 2;
    muiContext* context = MakeContextWith(limits);
    muiThemeId theme = MakeTheme(context);
    muiThemeId second = s_nullTheme;
    CHECK(muiCreateTheme(context, &second) == mui_errorCapacity && second.index1 == 0,
          "the theme limit");
    muiTokenId first = MakeToken(context, s_light);
    muiTokenId next = MakeToken(context, s_light);
    Override(context, theme, first, s_dark);
    Override(context, theme, first, s_red);
    muiTokenValue value = Color(s_dark);
    CHECK(muiTheme_SetTokenValue(context, theme, next, &value) == mui_errorCapacity,
          "the override limit");
    // A token in a slot used again is not the one overridden; the record
    // is taken over.
    CHECK(muiDestroyToken(context, first) == mui_success, "the overridden token goes");
    muiTokenId reused = MakeToken(context, s_blue);
    CHECK(reused.index1 == first.index1, "its slot is used again");
    muiTokenValue read;
    CHECK(muiTheme_GetToken(context, theme, reused, &read, NULL) == mui_empty,
          "the new token is not overridden");
    Override(context, theme, reused, s_dark);
    // A reset gives the record back.
    CHECK(muiTheme_ResetToken(context, theme, reused) == mui_success, "reset");
    Override(context, theme, next, s_dark);
    CHECK(muiDestroyTheme(context, theme) == mui_success, "destroy");
    theme = MakeTheme(context);
    Override(context, theme, next, s_dark);
    muiDestroyContext(context);

    // A table per theme and token slot past size_t.
    limits = muiDefaultContextDef().limits;
    limits.themes = 0x7FFFFFFF;
    limits.tokens = 0x7FFFFFFF;
    muiContextDef def = muiDefaultContextDef();
    def.limits = limits;
    muiContext* none = NULL;
    CHECK(muiCreateContext(&def, &none) == mui_errorCapacity && none == NULL,
          "too large a context");
}

static void TestSubtreesReadTheirTheme(void)
{
    muiContext* context = MakeContext();
    muiTokenId surface = MakeToken(context, s_light);
    muiThemeId dark = MakeTheme(context);
    Override(context, dark, surface, s_dark);
    muiNodeId root = MakeChild(context, s_nullNode);
    muiNodeId sidebar = MakeChild(context, root);
    muiNodeId item = MakeChild(context, sidebar);
    muiNodeId content = MakeChild(context, root);
    muiNodeId text = MakeChild(context, content);
    const muiNodeId painted[] = {root, sidebar, item, content, text};
    Paint(context, surface, painted, 5);
    CHECK(muiNode_SetTheme(context, sidebar, dark) == mui_success, "a dark sidebar");
    Layout(context, root);
    CHECK(SameColor(Background(context, root), s_light), "the root reads the context");
    CHECK(SameColor(Background(context, sidebar), s_dark), "the themed node");
    CHECK(SameColor(Background(context, item), s_dark), "its subtree");
    CHECK(SameColor(Background(context, text), s_light), "beside it, the context");
    CHECK(SameColor(Read(context, item, surface), s_dark) &&
              SameColor(Read(context, text, surface), s_light),
          "as the host reads them");
    muiThemeId read = s_nullTheme;
    CHECK(muiNode_GetTheme(context, sidebar, &read) == mui_success && read.index1 == dark.index1 &&
              muiNode_GetTheme(context, item, &read) == mui_success && read.index1 == 0,
          "set on the sidebar alone");

    // An edit of the theme restyles.
    Override(context, dark, surface, s_blue);
    Layout(context, root);
    CHECK(SameColor(Background(context, item), s_blue), "edited");
    // A reset restyles too.
    CHECK(muiTheme_ResetToken(context, dark, surface) == mui_success, "reset");
    Layout(context, root);
    CHECK(SameColor(Background(context, item), s_light), "reset to the context");
    Override(context, dark, surface, s_blue);
    // The content moves into the sidebar, and back out.
    CHECK(muiNode_Detach(context, content) == mui_success &&
              muiNode_InsertChild(context, sidebar, content, s_nullNode) == mui_success,
          "moved in");
    Layout(context, root);
    CHECK(SameColor(Background(context, text), s_blue), "a moved subtree reads its new theme");
    CHECK(muiNode_Detach(context, content) == mui_success, "detached");
    Layout(context, content);
    CHECK(SameColor(Background(context, text), s_light), "a detached subtree reads none");
    // The theme comes off the sidebar.
    CHECK(muiNode_SetTheme(context, sidebar, s_nullTheme) == mui_success, "no theme");
    Layout(context, root);
    CHECK(SameColor(Background(context, item), s_light), "the subtree reads the context again");
    // A theme that goes leaves its nodes to the context.
    CHECK(muiNode_SetTheme(context, sidebar, dark) == mui_success, "dark again");
    Layout(context, root);
    CHECK(SameColor(Background(context, item), s_blue), "dark");
    CHECK(muiDestroyTheme(context, dark) == mui_success, "the theme goes");
    CHECK(SameColor(Read(context, item, surface), s_light), "read before the next layout");
    Layout(context, root);
    CHECK(SameColor(Background(context, item), s_light), "the context");

    CHECK(muiNode_SetTheme(context, sidebar, dark) == mui_errorStale, "a gone theme");
    CHECK(muiNode_SetTheme(NULL, sidebar, dark) == mui_errorInvalid &&
              muiNode_SetTheme(context, s_nullNode, dark) == mui_errorInvalid &&
              muiNode_GetTheme(context, sidebar, NULL) == mui_errorInvalid &&
              muiNode_GetTheme(NULL, sidebar, &read) == mui_errorInvalid,
          "bad arguments");
    muiTokenValue value;
    CHECK(muiNode_GetTokenValue(NULL, item, surface, &value) == mui_errorInvalid &&
              muiNode_GetTokenValue(context, item, surface, NULL) == mui_errorInvalid &&
              muiNode_GetTokenValue(context, item, s_nullToken, &value) == mui_errorInvalid,
          "bad token reads");
    CHECK(muiDestroyNode(context, text) == mui_success &&
              muiNode_GetTheme(context, text, &read) == mui_errorStale &&
              muiNode_GetTokenValue(context, text, surface, &value) == mui_errorStale &&
              muiNode_SetTheme(context, text, s_nullTheme) == mui_errorStale,
          "a gone node");
    muiDestroyContext(context);
}

static void TestNestedThemes(void)
{
    muiContext* context = MakeContext();
    muiTokenId surface = MakeToken(context, s_light);
    muiTokenId accent = MakeToken(context, s_red);
    muiTokenId brand = MakeToken(context, s_red);
    muiThemeId outer = MakeTheme(context);
    muiThemeId inner = MakeTheme(context);
    Override(context, outer, surface, s_dark);
    Override(context, inner, accent, s_blue);
    // In the outer theme the accent is the brand, which the inner theme
    // makes light: read in the inner subtree, the alias reads it there.
    CHECK(muiTheme_SetTokenAlias(context, outer, accent, brand) == mui_success, "outer accent");
    muiNodeId root = MakeChild(context, s_nullNode);
    muiNodeId middle = MakeChild(context, root);
    muiNodeId leaf = MakeChild(context, middle);
    CHECK(muiNode_SetTheme(context, root, outer) == mui_success &&
              muiNode_SetTheme(context, middle, inner) == mui_success,
          "nested");
    const muiNodeId painted[] = {leaf};
    Paint(context, accent, painted, 1);
    Layout(context, root);
    CHECK(SameColor(Read(context, leaf, surface), s_dark), "the outer theme's surface");
    CHECK(SameColor(Read(context, leaf, accent), s_blue), "the inner theme's accent");
    CHECK(SameColor(Read(context, root, accent), s_red), "the outer accent is the brand");
    CHECK(SameColor(Background(context, leaf), s_blue), "resolved in the inner theme");
    CHECK(muiTheme_ResetToken(context, inner, accent) == mui_success, "the inner accent goes");
    Override(context, inner, brand, s_light);
    Layout(context, root);
    CHECK(SameColor(Background(context, leaf), s_light),
          "the outer alias, read where the inner theme makes the brand light");

    // A cycle only the two themes close: the outer accent is the brand,
    // the inner brand the accent.
    CHECK(muiTheme_SetTokenAlias(context, inner, brand, accent) == mui_success,
          "allowed: through the inner theme and the context alone, no cycle");
    Layout(context, root);
    muiTokenValue value = Color(s_red);
    CHECK(muiNode_GetTokenValue(context, leaf, accent, &value) == mui_empty &&
              value.type == mui_tokenColor && value.color.a == 0.0f,
          "no value through the cycle");
    CHECK(Background(context, leaf).a == 0.0f, "and the class's layer is silent");
    muiDestroyContext(context);
}

static void TestDepth(void)
{
    muiContext* context = MakeContext();
    muiTokenId surface = MakeToken(context, s_light);
    muiTokenId accent = MakeToken(context, s_red);
    muiTokenId edge = MakeToken(context, s_red);
    muiNodeId nodes[MUI_MAX_THEME_DEPTH + 1];
    muiNodeId parent = s_nullNode;
    for (uint32_t i = 0; i <= MUI_MAX_THEME_DEPTH; i++)
    {
        nodes[i] = MakeChild(context, parent);
        parent = nodes[i];
        muiThemeId theme = MakeTheme(context);
        CHECK(muiNode_SetTheme(context, nodes[i], theme) == mui_success, "themed");
        // The outermost theme sets the accent, the next the edge, and the
        // rest the surface.
        Override(context, theme, i == 0 ? accent : (i == 1 ? edge : surface), s_dark);
    }
    Layout(context, nodes[0]);
    muiNodeId leaf = nodes[MUI_MAX_THEME_DEPTH];
    CHECK(SameColor(Read(context, leaf, surface), s_dark), "the nearest eight");
    CHECK(SameColor(Read(context, leaf, edge), s_dark), "the eighth nearest among them");
    CHECK(SameColor(Read(context, leaf, accent), s_red), "the ninth is passed by");
    CHECK(SameColor(Read(context, nodes[1], accent), s_dark), "within eight, it is read");
    muiDestroyContext(context);
}

int main(void)
{
    TestThemesAreChecked();
    TestLimits();
    TestCycleThroughTheFirstToken();
    TestEveryOverride();
    TestSubtreesReadTheirTheme();
    TestNestedThemes();
    TestDepth();
    return s_failures == 0 ? 0 : 1;
}
