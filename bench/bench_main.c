// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layout timings over three trees: a list of 10,000 rows (an icon, a
// label of host content and a growing spacer each, 40,001 nodes), the
// same list styled through node types and a hovered variant instead of
// direct writes, and a tree nine levels deep with three children per
// node (9,841 nodes). For each: a cold layout, a static frame, one
// change (a label's content, or for the styled list a row hovered), and
// a new width.
// Prints the best of five runs in microseconds, and how many times the
// host was asked to measure, which does not depend on the machine.

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"

#include <stdio.h>
#include <time.h>

enum
{
    ROWS = 10000,
    DEPTH = 9,
    RUNS = 5,
    NODE_LIMIT = 50000
};

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Labels are as wide as their key says, 7 units per character of a
// made-up text.
static muiSize MeasureLabel(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                            muiMeasureAxis height)
{
    ++*(long*)user;
    (void)nodeId;
    (void)width;
    (void)height;
    return (muiSize){(float)(hostKey % 40 + 5) * 7.0f, 16.0f};
}

static muiDimension Length(float offset)
{
    return (muiDimension){0.0f, offset, mui_dimensionValue};
}

static void Check(muiResult status, const char* what)
{
    if (status != mui_success)
    {
        fprintf(stderr, "%s failed: %s\n", what, muiResultName(status));
    }
}

// Adds a node with its values written directly, or when type is not the
// null id, given by its type's class.
static muiNodeId Add(muiContext* context, muiNodeId parent, const muiLayoutStyle* style,
                     muiNodeTypeId type, uint64_t hostKey)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = hostKey;
    muiNodeId node = {0, 0};
    Check(muiCreateNode(context, &def, &node), "create");
    if (type.index1 != 0)
    {
        Check(muiNode_SetType(context, node, type), "type");
    }
    else
    {
        Check(muiNode_SetLayoutStyle(context, node, style), "style");
    }
    if (parent.index1 != 0)
    {
        Check(muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}), "insert");
    }
    return node;
}

// A node type with one class that sets every layout property from style,
// the most resolution can apply, and a hovered variant with hovered's
// start padding when hovered is not NULL.
static muiNodeTypeId MakeType(muiContext* context, const muiLayoutStyle* style,
                              const muiLayoutStyle* hovered)
{
    muiStyleId class = {0, 0};
    Check(muiCreateStyle(context, &class), "class");
    Check(muiStyle_SetLayoutValues(context, class, mui_variantBase, style, MUI_LAYOUT_PROPERTIES),
          "values");
    if (hovered != NULL)
    {
        Check(muiStyle_SetLayoutValues(context, class, mui_variantHovered, hovered,
                                       MUI_PROPERTY_BIT(mui_propertyPaddingStart)),
              "hovered");
    }
    muiNodeTypeId type = {0, 0};
    Check(muiCreateNodeType(context, &class, 1, &type), "type");
    return type;
}

// Returns the root; the label of the middle row in labelOut and the row
// in rowOut. With styled set, rows, icons, labels and spacers take their
// values from node types.
static muiNodeId BuildList(muiContext* context, bool styled, muiNodeId* labelOut, muiNodeId* rowOut)
{
    const muiNodeTypeId none = {0, 0};
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.container.direction = mui_flexColumn;
    column.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    muiNodeId root = Add(context, (muiNodeId){0, 0}, &column, none, 0);
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.alignItems = mui_alignCenter;
    row.container.columnGap = 6.0f;
    row.padding = (muiEdges){8.0f, 8.0f, 4.0f, 4.0f};
    muiLayoutStyle hovered = row;
    hovered.padding.start = 12.0f;
    muiLayoutStyle icon = muiDefaultLayoutStyle();
    icon.sizing.width = Length(16.0f);
    icon.sizing.height = Length(16.0f);
    muiLayoutStyle label = muiDefaultLayoutStyle();
    label.content = mui_contentHost;
    muiLayoutStyle spacer = muiDefaultLayoutStyle();
    spacer.item.grow = 1.0f;
    muiNodeTypeId rowType = styled ? MakeType(context, &row, &hovered) : none;
    muiNodeTypeId iconType = styled ? MakeType(context, &icon, NULL) : none;
    muiNodeTypeId labelType = styled ? MakeType(context, &label, NULL) : none;
    muiNodeTypeId spacerType = styled ? MakeType(context, &spacer, NULL) : none;
    for (uint64_t i = 0; i < ROWS; i++)
    {
        muiNodeId line = Add(context, root, &row, rowType, 0);
        Add(context, line, &icon, iconType, 0);
        muiNodeId text = Add(context, line, &label, labelType, i);
        Add(context, line, &spacer, spacerType, 0);
        if (i == ROWS / 2)
        {
            *labelOut = text;
            *rowOut = line;
        }
    }
    return root;
}

static muiNodeId BuildDeep(muiContext* context, muiNodeId parent, int depth, muiNodeId* leafOut)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.container.direction = depth % 2 == 0 ? mui_flexRow : mui_flexColumn;
    style.item.grow = 1.0f;
    style.padding = (muiEdges){1.0f, 1.0f, 1.0f, 1.0f};
    if (depth == DEPTH - 1)
    {
        style.content = mui_contentHost;
    }
    muiNodeId node = Add(context, parent, &style, (muiNodeTypeId){0, 0}, (uint64_t)depth);
    if (depth == DEPTH - 1)
    {
        *leafOut = node;
        return node;
    }
    for (int i = 0; i < 3; i++)
    {
        BuildDeep(context, node, depth + 1, leafOut);
    }
    return node;
}

typedef struct Scene
{
    const char* name;
    muiContext* context;
    muiNodeId root;
    muiNodeId leaf;
    // For the styled list, the row the change hovers; otherwise the null
    // id, and the change is the leaf's content.
    muiNodeId row;
    // Measure calls of the run being timed.
    long measured;
} Scene;

static double Time(Scene* scene, float width, bool changeLeaf)
{
    if (changeLeaf && scene->row.index1 != 0)
    {
        Check(muiNode_SetStates(scene->context, scene->row, mui_stateHovered), "hover");
    }
    else if (changeLeaf)
    {
        Check(muiNode_MarkContentChanged(scene->context, scene->leaf), "mark");
    }
    scene->measured = 0;
    muiLayoutInput input = {width, 100000.0f, MeasureLabel, &scene->measured};
    double start = Seconds();
    muiResult status = muiComputeLayout(scene->context, scene->root, &input);
    double elapsed = Seconds() - start;
    if (status != mui_success)
    {
        fprintf(stderr, "layout failed: %s\n", muiResultName(status));
    }
    return elapsed * 1e6;
}

typedef uint8_t Kind;

enum
{
    kindList,
    kindStyled,
    kindDeep,
};

static void Run(const char* name, Kind kind)
{
    double best[4] = {1e30, 1e30, 1e30, 1e30};
    long measured[4] = {0};
    for (int run = 0; run < RUNS; run++)
    {
        muiContextDef def = muiDefaultContextDef();
        def.limits.nodes = NODE_LIMIT;
        Scene scene = {.name = name};
        if (muiCreateContext(&def, &scene.context) != mui_success)
        {
            fprintf(stderr, "context failed\n");
            return;
        }
        muiNodeId row = {0, 0};
        scene.root = kind == kindDeep
                         ? BuildDeep(scene.context, (muiNodeId){0, 0}, 0, &scene.leaf)
                         : BuildList(scene.context, kind == kindStyled, &scene.leaf, &row);
        scene.row = kind == kindStyled ? row : (muiNodeId){0, 0};
        const float widths[4] = {800.0f, 800.0f, 800.0f, 640.0f};
        for (int i = 0; i < 4; i++)
        {
            double time = Time(&scene, widths[i], i == 2);
            best[i] = time < best[i] ? time : best[i];
            measured[i] = scene.measured;
        }
        muiDestroyContext(scene.context);
    }
    const char* labels[4] = {"cold", "static", "one change", "resize"};
    for (int i = 0; i < 4; i++)
    {
        printf("%-6s %-10s %12.1f us %8ld measured\n", name, labels[i], best[i], measured[i]);
    }
}

int main(void)
{
    Run("list", kindList);
    Run("styled", kindStyled);
    Run("deep", kindDeep);
    return 0;
}
