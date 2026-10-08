// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's snippets after its first program (docs/guide.md), each as
// written there (tools/check_guide.py checks it, family record 0019),
// run and their results checked: refusals named and counted, and a
// context in a counted allocator within its limits, a frame taking no
// memory.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/theme.h"
#include "maul-ui/token.h"
#include "maul-ui/visual.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// Section 2: results, ids and refusals.

// What a context says of calls that went wrong.
static void Refusals(muiContext* context)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    if (muiCreateNode(context, &def, &node) != mui_success)
    {
        return;
    }
    (void)muiDestroyNode(context, node);
    // A later node may take the slot; the old id never names it.
    muiResult stale = muiNode_InsertChild(context, node, node, (muiNodeId){0, 0});
    // A null id where a node is needed is the program's bug: refused,
    // and counted.
    muiResult invalid = muiNode_InsertChild(context, (muiNodeId){0, 0}, node, (muiNodeId){0, 0});
    printf("%s, %s, %llu refused\n", muiResultName(stale), muiResultName(invalid),
           (unsigned long long)muiGetContextMisuse(context));
}

// Section 2: defs, allocators and limits.

// The bytes the library holds, counted.
static size_t s_held;

static void* Alloc(size_t size, size_t alignment, void* user)
{
    (void)user;
    // The library asks for no more alignment than malloc gives.
    void* memory = alignment <= alignof(max_align_t) ? malloc(size) : NULL;
    s_held += memory != NULL ? size : 0;
    return memory;
}

static void Free(void* memory, size_t size, size_t alignment, void* user)
{
    (void)alignment;
    (void)user;
    s_held -= size;
    free(memory);
}

// A context for a small panel: room for 64 nodes and 256 draw commands,
// taken when it is made, from the counted allocator.
static muiContext* SmallContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.allocator = (muiAllocator){Alloc, Free, NULL};
    def.limits.nodes = 64;
    def.limits.drawCommands = 256;
    muiContext* context = NULL;
    return muiCreateContext(&def, &context) == mui_success ? context : NULL;
}

// Section 3: nodes and the tree.

// Counts a node's children.
static uint32_t CountChildren(const muiContext* context, muiNodeId parent)
{
    uint32_t count = 0;
    for (muiNodeId child = muiNode_GetFirstChild(context, parent); child.index1 != 0;
         child = muiNode_GetNextSibling(context, child))
    {
        count++;
    }
    return count;
}

// Moves a node to the front of its parent's children, as a list sorted
// again does.
static muiResult MoveFirst(muiContext* context, muiNodeId node)
{
    muiNodeId parent = muiNode_GetParent(context, node);
    muiNodeId first = muiNode_GetFirstChild(context, parent);
    if (first.index1 == node.index1)
    {
        return mui_success;
    }
    muiResult result = muiNode_Detach(context, node);
    return result == mui_success ? muiNode_InsertChild(context, parent, node, first) : result;
}

// Section 4: classes, states and node types.

// A button class: padded and blue, lighter while hovered, darker while
// pressed; and a node type of buttons, which lists it.
static muiResult MakeButtonType(muiContext* context, muiStyleId* classOut, muiNodeTypeId* typeOut)
{
    muiResult result = muiCreateStyle(context, classOut);
    muiStyleId button = *classOut;
    if (result != mui_success)
    {
        return result;
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding = (muiEdges){12.0f, 12.0f, 6.0f, 6.0f};
    const muiPropertyMask padding =
        MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
        MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom);
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    muiVisualStyle normal = muiDefaultVisualStyle();
    normal.background = (muiColor){0.2f, 0.4f, 0.8f, 1.0f};
    muiVisualStyle hovered = normal;
    hovered.background = (muiColor){0.3f, 0.5f, 0.9f, 1.0f};
    muiVisualStyle pressed = normal;
    pressed.background = (muiColor){0.1f, 0.3f, 0.6f, 1.0f};
    if ((result = muiStyle_SetLayoutValues(context, button, mui_variantBase, &layout, padding)) !=
            mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantBase, &normal,
                                           background)) != mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantHovered, &hovered,
                                           background)) != mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantPressed, &pressed,
                                           background)) != mui_success)
    {
        return result;
    }
    return muiCreateNodeType(context, &button, 1, typeOut);
}

// Section 4: conditions.

// A class that stacks a row's children on small viewports, a phone's.
static muiResult MakeStacking(muiContext* context, muiStyleId* classOut)
{
    muiResult result = muiCreateStyle(context, classOut);
    muiCondition small = muiDefaultCondition();
    small.viewports = mui_viewportSmall;
    muiVariant variant = mui_variantBase;
    if (result == mui_success)
    {
        result = muiStyle_AddCondition(context, *classOut, &small, &variant);
    }
    muiLayoutStyle stacked = muiDefaultLayoutStyle();
    stacked.container.direction = mui_flexColumn;
    return result == mui_success
               ? muiStyle_SetLayoutValues(context, *classOut, variant, &stacked,
                                          MUI_PROPERTY_BIT(mui_propertyFlexDirection))
               : result;
}

// Section 4: tokens and themes.

// An accent color token the button class paints with, and a theme that
// makes it orange in the subtree it is set on.
static muiResult UseAccent(muiContext* context, muiStyleId button, muiNodeId warning)
{
    muiTokenValue value = {.type = mui_tokenColor, .color = {0.2f, 0.4f, 0.8f, 1.0f}};
    muiTokenId accent = {0, 0};
    muiThemeId alert = {0, 0};
    muiResult result = muiCreateToken(context, &value, &accent);
    if (result == mui_success)
    {
        result =
            muiStyle_SetToken(context, button, mui_variantBase, mui_propertyBackground, accent);
    }
    if (result == mui_success)
    {
        result = muiCreateTheme(context, &alert);
    }
    value.color = (muiColor){0.9f, 0.5f, 0.1f, 1.0f};
    if (result == mui_success)
    {
        result = muiTheme_SetTokenValue(context, alert, accent, &value);
    }
    return result == mui_success ? muiNode_SetTheme(context, warning, alert) : result;
}

static void TestRefusals(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    Refusals(context);
    CHECK(muiGetContextMisuse(context) == 1, "one refusal counted, the stale id not");
    muiDestroyContext(context);
}

static void TestLimits(void)
{
    muiContext* context = SmallContext();
    CHECK(context != NULL && s_held > 0, "the limits' memory taken at once");
    size_t held = s_held;
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    muiNodeId node = {0, 0};
    bool made = muiCreateNode(context, &def, &root) == mui_success;
    for (int i = 1; i < 64; i++)
    {
        made = made && muiCreateNode(context, &def, &node) == mui_success &&
               muiNode_InsertChild(context, root, node, (muiNodeId){0, 0}) == mui_success;
    }
    CHECK(made && muiCreateNode(context, &def, &node) == mui_errorCapacity && s_held == held,
          "64 nodes, the 65th refused, nothing more taken");
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    CHECK(muiComputeLayout(context, root, &layout) == mui_success &&
              muiBuildDrawList(context, root, &draw) == mui_success && s_held == held,
          "a frame laid out and drawn taking nothing");
    muiDestroyContext(context);
    CHECK(s_held == 0, "everything given back");
}

// Adds a child of no style to a parent.
static muiNodeId Child(muiContext* context, muiNodeId parent)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    CHECK(muiCreateNode(context, &def, &node) == mui_success &&
              muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}) == mui_success,
          "a child");
    return node;
}

static void TestTree(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success, "a root");
    muiNodeId a = Child(context, root);
    Child(context, root);
    muiNodeId c = Child(context, root);
    CHECK(CountChildren(context, root) == 3 && MoveFirst(context, c) == mui_success &&
              muiNode_GetFirstChild(context, root).index1 == c.index1 &&
              muiNode_GetNextSibling(context, c).index1 == a.index1 &&
              CountChildren(context, root) == 3 && MoveFirst(context, c) == mui_success,
          "three children, the last moved first");
    muiDestroyContext(context);
}

static bool SameColor(muiColor a, muiColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static muiColor Background(muiContext* context, muiNodeId root, muiNodeId node)
{
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    muiVisualStyle visual = muiDefaultVisualStyle();
    CHECK(muiComputeLayout(context, root, &layout) == mui_success &&
              muiNode_GetVisualStyle(context, node, &visual) == mui_success,
          "styled");
    return visual.background;
}

static void TestStyles(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success, "a root");
    muiNodeId ok = Child(context, root);
    muiNodeId row = Child(context, root);
    muiNodeId warning = Child(context, row);
    muiStyleId button = {0, 0};
    muiNodeTypeId buttons = {0, 0};
    CHECK(MakeButtonType(context, &button, &buttons) == mui_success &&
              muiNode_SetType(context, ok, buttons) == mui_success &&
              muiNode_SetType(context, warning, buttons) == mui_success,
          "two buttons");
    const muiColor blue = {0.2f, 0.4f, 0.8f, 1.0f};
    const muiColor lighter = {0.3f, 0.5f, 0.9f, 1.0f};
    const muiColor darker = {0.1f, 0.3f, 0.6f, 1.0f};
    CHECK(SameColor(Background(context, root, ok), blue), "blue");
    CHECK(muiNode_SetStates(context, ok, mui_stateHovered) == mui_success &&
              SameColor(Background(context, root, ok), lighter),
          "lighter while hovered");
    CHECK(muiNode_SetStates(context, ok, mui_stateHovered | mui_statePressed) == mui_success &&
              SameColor(Background(context, root, ok), darker),
          "darker while pressed, which wins");
    muiRect box = muiNode_GetRect(context, ok);
    muiRect content = muiNode_GetContentRect(context, ok);
    CHECK(box.height == 12.0f && content.width == box.width - 24.0f, "padded");
    muiStyleId stacking = {0, 0};
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    CHECK(MakeStacking(context, &stacking) == mui_success &&
              muiNode_SetClasses(context, row, &stacking, 1) == mui_success,
          "a stacking row");
    (void)Background(context, root, row);
    CHECK(muiNode_GetLayoutStyle(context, row, &layout) == mui_success &&
              layout.container.direction == mui_flexRow,
          "a row on a medium viewport");
    muiEnvironment phone = muiDefaultEnvironment();
    phone.viewport = mui_viewportSmall;
    CHECK(muiSetContextEnvironment(context, &phone) == mui_success, "a phone");
    (void)Background(context, root, row);
    CHECK(muiNode_GetLayoutStyle(context, row, &layout) == mui_success &&
              layout.container.direction == mui_flexColumn,
          "a column on a small one");
    CHECK(UseAccent(context, button, row) == mui_success, "an accent");
    CHECK(muiNode_SetStates(context, ok, 0) == mui_success &&
              SameColor(Background(context, root, ok), blue) &&
              SameColor(Background(context, root, warning), (muiColor){0.9f, 0.5f, 0.1f, 1.0f}),
          "the accent, orange under the theme");
    muiDestroyContext(context);
}

int main(void)
{
    TestRefusals();
    TestLimits();
    TestTree();
    TestStyles();
    return s_failures == 0 ? 0 : 1;
}
