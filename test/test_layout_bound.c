// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layout bounded by the change (record mui-0003): a tree edited at random
// and laid out after each step, which bounds what it solves, holds every
// node's rectangle, content box and scroll extent to a tree built by
// replaying every edit from nothing and laid out once.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"

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
    kindSpace
} Kind;

// One edit: a node made under parent (host content when value is not 0),
// a property of a node set to what value picks, a host node's text made
// value characters long, a node removed, or the space laid out in.
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
    uint32_t count;
    float space;
} Tree;

// Characters of each host node's text, by its key, as both trees read.
static uint32_t s_characters[MAX_NODES + 1];

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)user;
    (void)nodeId;
    (void)height;
    float wide = (float)s_characters[hostKey] * 7.0f;
    float words = ceilf(wide / (float)WORD);
    float across = wide;
    if (width.mode == mui_measureMinContent)
    {
        across = fminf(wide, (float)WORD);
    }
    else if (width.mode != mui_measureMaxContent)
    {
        // Whole words a line, at least one.
        float perLine = fmaxf(floorf(width.size / (float)WORD), 1.0f);
        across = fminf(wide, perLine * (float)WORD);
    }
    float lines = across > 0.0f ? ceilf(words * (float)WORD / fmaxf(across, (float)WORD)) : 0.0f;
    return (muiSize){across, fmaxf(lines, 1.0f) * (float)LINE};
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

// Sets property (an index into the list below) to what value picks.
static void SetStyle(Tree* tree, uint32_t node, uint32_t property, uint32_t value)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    float length = (float)(value % 24);
    muiProperty id = mui_propertyWidth;
    switch (property % 18)
    {
    case 0:
        style.sizing.width = Dimension(value);
        id = mui_propertyWidth;
        break;
    case 1:
        style.sizing.height = Dimension(value);
        id = mui_propertyHeight;
        break;
    case 2:
        style.sizing.maxWidth = value % 2 == 0 ? style.sizing.maxWidth : Dimension(value | 1u);
        id = mui_propertyMaxWidth;
        break;
    case 3:
        style.container.direction = (muiFlexDirection)(value % 4);
        id = mui_propertyFlexDirection;
        break;
    case 4:
        style.container.wrap = (muiFlexWrap)(value % 3);
        id = mui_propertyFlexWrap;
        break;
    case 5:
        style.container.alignItems = (muiAlign)(1 + value % 5);
        id = mui_propertyAlignItems;
        break;
    case 6:
        style.item.alignSelf = (muiAlign)(value % 6);
        id = mui_propertyAlignSelf;
        break;
    case 7:
        style.item.grow = (float)(value % 3);
        id = mui_propertyGrow;
        break;
    case 8:
        style.item.shrink = (float)(value % 3);
        id = mui_propertyShrink;
        break;
    case 9:
        style.item.basis = Dimension(value);
        id = mui_propertyBasis;
        break;
    case 10:
        style.margin.start = length;
        id = mui_propertyMarginStart;
        break;
    case 11:
        style.margin.top = length;
        id = mui_propertyMarginTop;
        break;
    case 12:
        style.padding.start = length;
        id = mui_propertyPaddingStart;
        break;
    case 13:
        style.padding.top = length;
        id = mui_propertyPaddingTop;
        break;
    case 14:
        style.container.justify = (muiJustify)(value % 6);
        id = mui_propertyJustify;
        break;
    case 15:
        style.placement.position = (muiPositionKind)(value % 5 == 0 ? 1 : 0);
        id = mui_propertyPosition;
        break;
    case 16:
        style.textDirection = (muiTextDirection)(value % 3);
        id = mui_propertyTextDirection;
        break;
    default:
        style.scrollAxes = (muiScrollAxes)(value % 4);
        id = mui_propertyScrollAxes;
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
    }
}

static void LayOut(Tree* tree)
{
    const muiLayoutInput input = {tree->space, 600.0f, Measure, NULL, 0, Baseline, {0, 0, 0, 0}};
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

// A random edit the tree can take.
static Edit RandomEdit(const Tree* tree, uint32_t* state)
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
            Record(edits, &count, &edited, RandomEdit(&edited, &state));
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

// A row 161 wide laid out alone under a right-to-left root keeps the
// direction it inherits: its label at its right.
static void TestRightToLeftAlone(void)
{
    static Edit edits[16];
    uint32_t count = 0;
    Tree tree = MakeTree();
    const Edit made[] = {
        {kindCreate, 0, 0, 0, 0},  {kindCreate, 0, 0, 0, 0},  {kindCreate, 0, 1, 0, 1},
        {kindStyle, 0, 0, 16, 2},  {kindStyle, 0, 0, 3, 2},   {kindStyle, 0, 0, 0, 153},
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
            {kindStyle, own == 0 ? 1u : 2u, 0, own == 0 ? 5u : 6u, own == 0 ? 4u : 5u},
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
            Record(edits, &count, &tree, (Edit){kindStyle, 4, 0, 6, 5});
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
    TestOneChangeIsBounded();
    TestDeepChangeIsBounded();
    return s_failures == 0 ? 0 : 1;
}
