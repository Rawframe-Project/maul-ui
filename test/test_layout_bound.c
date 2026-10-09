// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layout bounded by the change (record mui-0003): a tree edited at random
// and laid out after each step, which bounds what it solves, holds every
// node's rectangle, content box and scroll extent to a tree built by
// replaying every edit from nothing and laid out once.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/exit.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/virtual.h"

#include <math.h>
#include <string.h>

enum
{
    MAX_NODES = 96,
    MAX_EDITS = 1200,
    STEPS = 400,
    // Of host content, a word is three characters of 7 units, a line 16.
    WORD = 21,
    LINE = 16
};

typedef enum Kind
{
    kindCreate,
    kindStyle,
    kindContent,
    kindRemove,
    kindSpace,
    kindList,
    kindBind,
    kindExit,
    kindSafe,
    kindScroll
} Kind;

// One edit: a node made under parent (host content when value is not 0),
// a property of a node set to what value picks, a host node's text made
// value characters long, a node removed, the space laid out in, a node
// made a virtual list of what value picks (estimated when property is
// not 0), a list's child bound to item value, which takes it out of the
// flow, a node's exit begun popped (value odd) or cancelled, the
// surface's safe-area insets, or a node scrolled to what value picks.
typedef struct Edit
{
    Kind kind;
    uint32_t node;
    uint32_t parent;
    uint32_t property;
    uint32_t value;
} Edit;

typedef struct Tree
{
    muiContext* context;
    muiNodeId nodes[MAX_NODES];
    bool live[MAX_NODES];
    bool host[MAX_NODES];
    uint32_t parent[MAX_NODES];
    // A virtual list's item count; 0 for a node that is none.
    uint32_t items[MAX_NODES];
    uint32_t count;
    float space;
    muiSides safe;
} Tree;

// Characters of each host node's text, by its key, as both trees read.
static uint32_t s_characters[MAX_NODES + 1];

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)user;
    (void)nodeId;
    (void)height;
    // Words of WORD units but the last, which takes what is left, broken
    // greedily: a line takes words while they fit, at least one.
    float wide = (float)s_characters[hostKey] * 7.0f;
    float space = width.mode == mui_measureMinContent   ? 0.0f
                  : width.mode == mui_measureMaxContent ? INFINITY
                                                        : width.size;
    float across = 0.0f;
    float line = 0.0f;
    float lines = 1.0f;
    for (float left = wide; left > 0.0f; left -= (float)WORD)
    {
        float word = fminf(left, (float)WORD);
        if (line > 0.0f && line + word > space)
        {
            across = fmaxf(across, line);
            lines += 1.0f;
            line = 0.0f;
        }
        line += word;
    }
    across = fmaxf(across, line);
    return (muiSize){across, lines * (float)LINE};
}

// A baseline that moves with the text though its size may not.
static float Baseline(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height)
{
    (void)user;
    (void)nodeId;
    (void)width;
    (void)height;
    return 8.0f + (float)(s_characters[hostKey] % 7);
}

static uint32_t Next(uint32_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static muiDimension Dimension(uint32_t value)
{
    switch (value % 4)
    {
    case 0:
        return (muiDimension){0.0f, 0.0f, mui_dimensionAuto};
    case 1:
        return (muiDimension){0.0f, (float)(value % 160 + 8), mui_dimensionValue};
    case 2:
        return (muiDimension){(float)(value % 9 + 1) * 0.1f, 0.0f, mui_dimensionValue};
    default:
        return (muiDimension){0.5f, (float)(value % 40), mui_dimensionValue};
    }
}

// The layout properties an edit sets, all but content, which makes a
// node host content or not.
static const muiProperty s_properties[] = {
    mui_propertyWidth,         mui_propertyHeight,        mui_propertyMinWidth,
    mui_propertyMinHeight,     mui_propertyMaxWidth,      mui_propertyMaxHeight,
    mui_propertyAspectRatio,   mui_propertyFlexDirection, mui_propertyFlexWrap,
    mui_propertyJustify,       mui_propertyAlignItems,    mui_propertyAlignContent,
    mui_propertyRowGap,        mui_propertyColumnGap,     mui_propertyGrow,
    mui_propertyShrink,        mui_propertyBasis,         mui_propertyAlignSelf,
    mui_propertyMarginStart,   mui_propertyMarginEnd,     mui_propertyMarginTop,
    mui_propertyMarginBottom,  mui_propertyMarginAuto,    mui_propertyBorderStart,
    mui_propertyBorderEnd,     mui_propertyBorderTop,     mui_propertyBorderBottom,
    mui_propertyPaddingStart,  mui_propertyPaddingEnd,    mui_propertyPaddingTop,
    mui_propertyPaddingBottom, mui_propertyPosition,      mui_propertyInsetStart,
    mui_propertyInsetEnd,      mui_propertyInsetTop,      mui_propertyInsetBottom,
    mui_propertyAnchorX,       mui_propertyAnchorY,       mui_propertyTextDirection,
    mui_propertyScrollAxes,    mui_propertySafeArea,
};

// Sets a property (property picks one of s_properties) to what value
// picks: a dimension, a length, a small count or an enumeration.
static void SetStyle(Tree* tree, uint32_t node, uint32_t property, uint32_t value)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    float length = (float)(value % 24);
    float edge = (float)(value % 6);
    muiProperty id = s_properties[property % (sizeof s_properties / sizeof s_properties[0])];
    switch (id)
    {
    case mui_propertyWidth:
        style.sizing.width = Dimension(value);
        break;
    case mui_propertyHeight:
        style.sizing.height = Dimension(value);
        break;
    case mui_propertyMinWidth:
        style.sizing.minWidth = Dimension(value);
        break;
    case mui_propertyMinHeight:
        style.sizing.minHeight = Dimension(value);
        break;
    case mui_propertyMaxWidth:
        style.sizing.maxWidth = value % 2 == 0 ? style.sizing.maxWidth : Dimension(value | 1u);
        break;
    case mui_propertyMaxHeight:
        style.sizing.maxHeight = value % 2 == 0 ? style.sizing.maxHeight : Dimension(value | 1u);
        break;
    case mui_propertyAspectRatio:
        style.sizing.aspectRatio = value % 3 == 0 ? 0.0f : 0.5f + (float)(value % 6) * 0.25f;
        break;
    case mui_propertyFlexDirection:
        style.container.direction = (muiFlexDirection)(value % 4);
        break;
    case mui_propertyFlexWrap:
        style.container.wrap = (muiFlexWrap)(value % 3);
        break;
    case mui_propertyJustify:
        style.container.justify = (muiJustify)(value % 6);
        break;
    case mui_propertyAlignItems:
        style.container.alignItems = (muiAlign)(1 + value % 5);
        break;
    case mui_propertyAlignContent:
        style.container.alignContent = (muiAlignContent)(value % 7);
        break;
    case mui_propertyRowGap:
        style.container.rowGap = (float)(value % 12);
        break;
    case mui_propertyColumnGap:
        style.container.columnGap = (float)(value % 12);
        break;
    case mui_propertyGrow:
        style.item.grow = (float)(value % 3);
        break;
    case mui_propertyShrink:
        style.item.shrink = (float)(value % 3);
        break;
    case mui_propertyBasis:
        style.item.basis = Dimension(value);
        break;
    case mui_propertyAlignSelf:
        style.item.alignSelf = (muiAlign)(value % 6);
        break;
    case mui_propertyMarginStart:
        style.margin.start = length;
        break;
    case mui_propertyMarginEnd:
        style.margin.end = length;
        break;
    case mui_propertyMarginTop:
        style.margin.top = length;
        break;
    case mui_propertyMarginBottom:
        style.margin.bottom = length;
        break;
    case mui_propertyMarginAuto:
        style.marginAuto = (muiEdgeMask)(value % 3 == 0 ? value % 16 : 0);
        break;
    case mui_propertyBorderStart:
        style.border.start = edge;
        break;
    case mui_propertyBorderEnd:
        style.border.end = edge;
        break;
    case mui_propertyBorderTop:
        style.border.top = edge;
        break;
    case mui_propertyBorderBottom:
        style.border.bottom = edge;
        break;
    case mui_propertyPaddingStart:
        style.padding.start = length;
        break;
    case mui_propertyPaddingEnd:
        style.padding.end = length;
        break;
    case mui_propertyPaddingTop:
        style.padding.top = length;
        break;
    case mui_propertyPaddingBottom:
        style.padding.bottom = length;
        break;
    case mui_propertyPosition:
        style.placement.position = (muiPositionKind)(value % 5 == 0 ? 1 : 0);
        break;
    case mui_propertyInsetStart:
        style.placement.inset.start = Dimension(value);
        break;
    case mui_propertyInsetEnd:
        style.placement.inset.end = Dimension(value);
        break;
    case mui_propertyInsetTop:
        style.placement.inset.top = Dimension(value);
        break;
    case mui_propertyInsetBottom:
        style.placement.inset.bottom = Dimension(value);
        break;
    case mui_propertyAnchorX:
        style.placement.anchorX = (float)(value % 5) * 0.25f;
        break;
    case mui_propertyAnchorY:
        style.placement.anchorY = (float)(value % 5) * 0.25f;
        break;
    case mui_propertyTextDirection:
        style.textDirection = (muiTextDirection)(value % 3);
        break;
    case mui_propertyScrollAxes:
        style.scrollAxes = (muiScrollAxes)(value % 4);
        break;
    default:
        style.safeArea = (muiEdgeMask)(value % 16);
        break;
    }
    CHECK(muiNode_SetLayoutValues(tree->context, tree->nodes[node], &style, MUI_PROPERTY_BIT(id)) ==
              mui_success,
          "a style");
}

static void Apply(Tree* tree, const Edit* edit)
{
    switch (edit->kind)
    {
    case kindCreate:
    {
        muiNodeDef def = muiDefaultNodeDef();
        uint32_t index = tree->count++;
        def.hostKey = edit->value != 0 ? index + 1 : 0;
        CHECK(muiCreateNode(tree->context, &def, &tree->nodes[index]) == mui_success, "made");
        tree->live[index] = true;
        tree->host[index] = edit->value != 0;
        tree->parent[index] = edit->parent;
        if (index != 0)
        {
            CHECK(muiNode_InsertChild(tree->context, tree->nodes[edit->parent], tree->nodes[index],
                                      (muiNodeId){0, 0}) == mui_success,
                  "inserted");
        }
        if (tree->host[index])
        {
            muiLayoutStyle style = muiDefaultLayoutStyle();
            style.content = mui_contentHost;
            CHECK(muiNode_SetLayoutValues(tree->context, tree->nodes[index], &style,
                                          MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
                  "host content");
        }
        break;
    }
    case kindStyle:
        SetStyle(tree, edit->node, edit->property, edit->value);
        break;
    case kindContent:
        CHECK(muiNode_MarkContentChanged(tree->context, tree->nodes[edit->node]) == mui_success,
              "content");
        break;
    case kindRemove:
        CHECK(muiDestroyNode(tree->context, tree->nodes[edit->node]) == mui_success, "removed");
        tree->live[edit->node] = false;
        break;
    case kindSpace:
        tree->space = (float)edit->value;
        break;
    case kindList:
    {
        muiVirtualList list = muiDefaultVirtualList();
        list.count = 1 + edit->value % 12;
        list.extent = (float)(16 + edit->value % 24);
        list.fixed = edit->property == 0;
        list.overscan = 0.0f;
        CHECK(muiNode_SetVirtualList(tree->context, tree->nodes[edit->node], &list) == mui_success,
              "a list");
        tree->items[edit->node] = list.count;
        break;
    }
    case kindBind:
        CHECK(muiNode_SetItem(tree->context, tree->nodes[edit->node], edit->value) == mui_success,
              "bound");
        break;
    case kindSafe:
        tree->safe = (muiSides){(float)(edit->value % 9), (float)(edit->value / 9 % 9),
                                (float)(edit->value / 81 % 9), (float)(edit->value / 729 % 9)};
        break;
    case kindScroll:
        CHECK(muiNode_SetScroll(tree->context, tree->nodes[edit->node], (float)(edit->value % 200),
                                (float)(edit->value / 200 % 200)) == mui_success,
              "scrolled");
        break;
    case kindExit:
        if (edit->value % 2 != 0)
        {
            muiInteractionStyle values = muiDefaultInteractionStyle();
            values.exitLayout = mui_exitPop;
            CHECK(muiNode_SetInteractionValues(tree->context, tree->nodes[edit->node], &values,
                                               MUI_PROPERTY_BIT(mui_propertyExitLayout)) ==
                          mui_success &&
                      muiNode_BeginExit(tree->context, tree->nodes[edit->node]) == mui_success,
                  "popped");
        }
        else
        {
            CHECK(muiNode_CancelExit(tree->context, tree->nodes[edit->node]) == mui_success,
                  "back");
        }
        break;
    }
}

static void LayOut(Tree* tree)
{
    const muiLayoutInput input = {tree->space, 600.0f, Measure, NULL, 0, Baseline, tree->safe};
    CHECK(muiComputeLayout(tree->context, tree->nodes[0], &input) == mui_success, "laid out");
}

static bool SameRect(muiRect a, muiRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// Whether two trees lay every live node out alike.
static bool Same(const Tree* a, const Tree* b)
{
    for (uint32_t i = 0; i < a->count; i++)
    {
        if (!a->live[i])
        {
            continue;
        }
        muiSize x = {0.0f, 0.0f};
        muiSize y = {0.0f, 0.0f};
        (void)muiNode_GetScrollExtent(a->context, a->nodes[i], &x);
        (void)muiNode_GetScrollExtent(b->context, b->nodes[i], &y);
        if (!SameRect(muiNode_GetRect(a->context, a->nodes[i]),
                      muiNode_GetRect(b->context, b->nodes[i])) ||
            !SameRect(muiNode_GetContentRect(a->context, a->nodes[i]),
                      muiNode_GetContentRect(b->context, b->nodes[i])) ||
            x.width != y.width || x.height != y.height)
        {
            printf("node %u differs\n", i);
            return false;
        }
    }
    return true;
}

static Tree MakeTree(void)
{
    Tree tree;
    memset(&tree, 0, sizeof tree);
    muiContextDef def = muiDefaultContextDef();
    // Any node may become a list, or exit.
    def.limits.virtualLists = MAX_NODES;
    def.limits.exits = MAX_NODES;
    CHECK(muiCreateContext(&def, &tree.context) == mui_success, "a context");
    tree.space = 400.0f;
    return tree;
}

static bool IsLeaf(const Tree* tree, uint32_t node)
{
    for (uint32_t i = node + 1; i < tree->count; i++)
    {
        if (tree->live[i] && tree->parent[i] == node)
        {
            return false;
        }
    }
    return true;
}

// A random edit the tree can take; with history, estimated lists, exits
// and scrolling too, which a replay laid out once cannot match.
static Edit RandomEdit(const Tree* tree, uint32_t* state, bool history)
{
    uint32_t pick = Next(state) % 16;
    uint32_t node = Next(state) % tree->count;
    while (!tree->live[node])
    {
        node = (node + 1) % tree->count;
    }
    if (pick < 2 && tree->count < MAX_NODES)
    {
        // Under a live node that is not host content.
        uint32_t parent = node;
        while (!tree->live[parent] || tree->host[parent])
        {
            parent = (parent + 1) % tree->count;
        }
        return (Edit){kindCreate, 0, parent, 0, Next(state) % 3 == 0 ? 0u : 1u};
    }
    if (pick < 6 && tree->host[node])
    {
        return (Edit){kindContent, node, 0, 0, 1 + Next(state) % 40};
    }
    if (pick == 6 && node != 0 && IsLeaf(tree, node))
    {
        return (Edit){kindRemove, node, 0, 0, 0};
    }
    if (pick == 7)
    {
        return (Edit){kindSpace, 0, 0, 0, 120 + Next(state) % 400};
    }
    if (pick == 8 && !tree->host[node] && tree->items[node] == 0)
    {
        return (Edit){kindList, node, 0, history ? Next(state) % 2 : 0u, Next(state)};
    }
    uint32_t parent = tree->parent[node];
    if (pick == 9 && node != 0 && tree->live[parent] && tree->items[parent] != 0)
    {
        return (Edit){kindBind, node, 0, 0, Next(state) % tree->items[parent]};
    }
    if (pick == 10 && history && node != 0)
    {
        return (Edit){kindExit, node, 0, 0, Next(state)};
    }
    if (pick == 11)
    {
        return (Edit){kindSafe, 0, 0, 0, Next(state)};
    }
    if (pick == 12 && history)
    {
        return (Edit){kindScroll, node, 0, 0, Next(state)};
    }
    return (Edit){kindStyle, node, 0, Next(state), Next(state)};
}

static void Record(Edit* edits, uint32_t* count, Tree* tree, Edit edit)
{
    if (edit.kind == kindContent)
    {
        s_characters[edit.node + 1] = edit.value;
    }
    edits[(*count)++] = edit;
    Apply(tree, &edit);
}

static void TestBoundedMatchesWhole(void)
{
    static Edit edits[MAX_EDITS];
    uint32_t count = 0;
    uint32_t state = 0x9E3779B9u;
    Tree edited = MakeTree();
    Record(edits, &count, &edited, (Edit){kindCreate, 0, 0, 0, 0});
    for (uint32_t i = 1; i < 24; i++)
    {
        Record(edits, &count, &edited, (Edit){kindCreate, 0, Next(&state) % i, 0, 0});
    }
    // Host content at the leaves of the first ones, of some length.
    for (uint32_t i = 0; i < 24; i++)
    {
        Record(edits, &count, &edited, (Edit){kindCreate, 0, i, 0, 1});
        s_characters[edited.count] = 1 + Next(&state) % 30;
    }
    LayOut(&edited);
    bool same = true;
    for (uint32_t step = 0; step < STEPS && same && count + 3 < MAX_EDITS; step++)
    {
        uint32_t many = 1 + Next(&state) % 3;
        for (uint32_t k = 0; k < many; k++)
        {
            Record(edits, &count, &edited, RandomEdit(&edited, &state, false));
        }
        LayOut(&edited);
        Tree whole = MakeTree();
        for (uint32_t k = 0; k < count; k++)
        {
            Apply(&whole, &edits[k]);
        }
        LayOut(&whole);
        same = Same(&edited, &whole);
        if (!same)
        {
            printf("step %u\n", step);
        }
        muiDestroyContext(whole.context);
    }
    CHECK(same, "a bounded layout is the whole one");
    muiDestroyContext(edited.context);
}

// Gives an edit to a tree and its twin.
static void Both(Tree* tree, Tree* twin, Edit edit)
{
    if (edit.kind == kindContent)
    {
        s_characters[edit.node + 1] = edit.value;
    }
    Apply(tree, &edit);
    Apply(twin, &edit);
}

// A tree edited at random beside a twin given the same edits at the same
// steps, every node of it marked before each layout so that it is solved
// from nothing: estimated lists, which keep what they measured, and
// popped exits, which keep their last rectangle, have one history in
// both.
static void CheckBoundedMatchesFresh(uint32_t seed)
{
    uint32_t state = seed;
    Tree edited = MakeTree();
    Tree twin = MakeTree();
    Both(&edited, &twin, (Edit){kindCreate, 0, 0, 0, 0});
    for (uint32_t i = 1; i < 24; i++)
    {
        Both(&edited, &twin, (Edit){kindCreate, 0, Next(&state) % i, 0, 0});
    }
    for (uint32_t i = 0; i < 24; i++)
    {
        Both(&edited, &twin, (Edit){kindCreate, 0, i, 0, 1});
        s_characters[edited.count] = 1 + Next(&state) % 30;
    }
    bool same = true;
    for (uint32_t step = 0; step < STEPS && same; step++)
    {
        uint32_t many = 1 + Next(&state) % 3;
        for (uint32_t k = 0; k < many; k++)
        {
            Both(&edited, &twin, RandomEdit(&edited, &state, true));
        }
        LayOut(&edited);
        for (uint32_t i = 0; i < twin.count; i++)
        {
            CHECK(!twin.live[i] ||
                      muiNode_MarkContentChanged(twin.context, twin.nodes[i]) == mui_success,
                  "marked");
        }
        LayOut(&twin);
        same = Same(&edited, &twin);
        if (!same)
        {
            printf("step %u\n", step);
        }
    }
    CHECK(same, "a bounded layout is a fresh one, history and all");
    muiDestroyContext(twin.context);
    muiDestroyContext(edited.context);
}

// Three histories; the second and third once took a content size for the
// own one of a node with an aspect ratio below it, or its own (research
// 93).
static void TestBoundedMatchesFresh(void)
{
    CheckBoundedMatchesFresh(0x85EBCA6Bu);
    CheckBoundedMatchesFresh(0x9E3779B1u);
    CheckBoundedMatchesFresh(0x33333335u);
}

// A row 161 wide laid out alone under a right-to-left root keeps the
// direction it inherits: its label at its right.
static void TestRightToLeftAlone(void)
{
    static Edit edits[16];
    uint32_t count = 0;
    Tree tree = MakeTree();
    const Edit made[] = {
        {kindCreate, 0, 0, 0, 0},  {kindCreate, 0, 0, 0, 0},  {kindCreate, 0, 1, 0, 1},
        {kindStyle, 0, 0, 38, 2},  {kindStyle, 0, 0, 7, 2},   {kindStyle, 0, 0, 0, 153},
        {kindStyle, 1, 0, 0, 153}, {kindContent, 2, 0, 0, 3},
    };
    for (size_t i = 0; i < sizeof made / sizeof made[0]; i++)
    {
        Record(edits, &count, &tree, made[i]);
    }
    tree.space = 300.0f;
    LayOut(&tree);
    Record(edits, &count, &tree, (Edit){kindContent, 2, 0, 0, 4});
    LayOut(&tree);
    Tree whole = MakeTree();
    whole.space = 300.0f;
    for (uint32_t k = 0; k < count; k++)
    {
        Apply(&whole, &edits[k]);
    }
    LayOut(&whole);
    muiRect label = muiNode_GetRect(tree.context, tree.nodes[2]);
    bool same = Same(&tree, &whole);
    CHECK(same && label.x > 0.0f, "mirrored as a whole layout mirrors it");
    muiDestroyContext(whole.context);
    muiDestroyContext(tree.context);
}

// One label's new text in a list of 40 rows that keep their size
// solves the label and its row, not the list.
static void TestOneChangeIsBounded(void)
{
    Tree tree = MakeTree();
    static Edit edits[1];
    uint32_t count = 0;
    Apply(&tree, &(Edit){kindCreate, 0, 0, 0, 0});
    // A column of rows, each stretched across it, sized as a window is.
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.container.direction = mui_flexColumn;
    column.sizing.width = (muiDimension){0.0f, 400.0f, mui_dimensionValue};
    column.sizing.height = (muiDimension){0.0f, 600.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(tree.context, tree.nodes[0], &column,
                                  MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                                      MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "a column");
    for (uint32_t i = 0; i < 40; i++)
    {
        uint32_t row = tree.count;
        Apply(&tree, &(Edit){kindCreate, 0, 0, 0, 0});
        SetStyle(&tree, row, 3, 0);
        Apply(&tree, &(Edit){kindCreate, 0, row, 0, 1});
        s_characters[tree.count] = 3;
    }
    LayOut(&tree);
    muiWorkCounts before = muiGetWorkCounts(tree.context);
    count = 0;
    Record(edits, &count, &tree, (Edit){kindContent, 2, 0, 0, 4});
    LayOut(&tree);
    muiWorkCounts after = muiGetWorkCounts(tree.context);
    // Its queries asked again and the row laid out: a few sizes, fewer
    // than the rows, where the whole list solves 87.
    CHECK(after.measured - before.measured <= 4 && after.sized - before.sized <= 16,
          "the label and its row alone");
    muiDestroyContext(tree.context);
}

// A change at the foot of a deep chain, under a box of fixed size, is
// solved there: the box keeps its answers and, with nothing aligned by
// baselines, its moved baseline is read by none, so none of the thirty
// ancestors is solved again (found by mutants that stopped the bound,
// and that took every node's baseline as read).
static void TestDeepChangeIsBounded(void)
{
    static Edit edits[40];
    uint32_t count = 0;
    Tree tree = MakeTree();
    Record(edits, &count, &tree, (Edit){kindCreate, 0, 0, 0, 0});
    for (uint32_t i = 1; i <= 30; i++)
    {
        Record(edits, &count, &tree, (Edit){kindCreate, 0, i - 1, 0, 0});
    }
    // The deepest a box of 109 by 69, its text three characters.
    Record(edits, &count, &tree, (Edit){kindStyle, 30, 0, 0, 101});
    Record(edits, &count, &tree, (Edit){kindStyle, 30, 0, 1, 61});
    Record(edits, &count, &tree, (Edit){kindCreate, 0, 30, 0, 1});
    Record(edits, &count, &tree, (Edit){kindContent, 31, 0, 0, 3});
    LayOut(&tree);
    muiWorkCounts before = muiGetWorkCounts(tree.context);
    Record(edits, &count, &tree, (Edit){kindContent, 31, 0, 0, 4});
    LayOut(&tree);
    muiWorkCounts after = muiGetWorkCounts(tree.context);
    CHECK(after.sized - before.sized <= 8, "the text and its box alone");
    Tree whole = MakeTree();
    for (uint32_t k = 0; k < count; k++)
    {
        Apply(&whole, &edits[k]);
    }
    LayOut(&whole);
    CHECK(Same(&tree, &whole), "as the whole tree laid out");
    muiDestroyContext(whole.context);
    muiDestroyContext(tree.context);
}

// A node of fixed size keeps its answers when its first child's text
// moves its baseline; a container aligning by baselines still places it
// anew. By its items' alignment, then by its own.
static void TestBaselinesAreRead(void)
{
    for (uint32_t own = 0; own < 2; own++)
    {
        static Edit edits[16];
        uint32_t count = 0;
        Tree tree = MakeTree();
        // A root, a row aligning by baselines, a box of 109 by 69 with text
        // first in it, and text beside the box.
        const Edit made[] = {
            {kindCreate, 0, 0, 0, 0},
            {kindCreate, 0, 0, 0, 0},
            {kindCreate, 0, 1, 0, 0},
            {kindCreate, 0, 2, 0, 1},
            {kindCreate, 0, 1, 0, 1},
            {kindStyle, own == 0 ? 1u : 2u, 0, own == 0 ? 10u : 17u, own == 0 ? 4u : 5u},
            {kindStyle, 2, 0, 0, 101},
            {kindStyle, 2, 0, 1, 61},
            {kindContent, 3, 0, 0, 3},
            {kindContent, 4, 0, 0, 2},
        };
        for (size_t i = 0; i < sizeof made / sizeof made[0]; i++)
        {
            Record(edits, &count, &tree, made[i]);
        }
        // By their own alignment, the text beside aligns by its baseline
        // too, so the box's moves it.
        if (own == 1)
        {
            Record(edits, &count, &tree, (Edit){kindStyle, 4, 0, 17, 5});
        }
        LayOut(&tree);
        // One character more: the same size, a baseline one lower.
        Record(edits, &count, &tree, (Edit){kindContent, 3, 0, 0, 4});
        LayOut(&tree);
        Tree whole = MakeTree();
        for (uint32_t k = 0; k < count; k++)
        {
            Apply(&whole, &edits[k]);
        }
        LayOut(&whole);
        CHECK(Same(&tree, &whole),
              own == 0 ? "aligned by its items' baselines" : "aligned by its own baseline");
        muiDestroyContext(whole.context);
        muiDestroyContext(tree.context);
    }
}

int main(void)
{
    TestBaselinesAreRead();
    TestRightToLeftAlone();
    TestBoundedMatchesWhole();
    TestBoundedMatchesFresh();
    TestOneChangeIsBounded();
    TestDeepChangeIsBounded();
    return s_failures == 0 ? 0 : 1;
}
