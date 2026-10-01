// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layout timings over two trees: a list of 10,000 rows (an icon, a label
// of host content and a growing spacer each, 40,001 nodes) and a tree
// nine levels deep with three children per node (9,841 nodes). For each:
// a cold layout, a static frame, one label changed, and a new width.
// Prints the best of five runs in microseconds, and how many times the
// host was asked to measure, which does not depend on the machine.

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

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

static muiNodeId Add(muiContext* context, muiNodeId parent, const muiLayoutStyle* style,
                     uint64_t hostKey)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = hostKey;
    muiNodeId node = {0, 0};
    if (muiCreateNode(context, &def, &node) != mui_success ||
        muiNode_SetLayoutStyle(context, node, style) != mui_success ||
        (parent.index1 != 0 &&
         muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}) != mui_success))
    {
        fprintf(stderr, "building the tree failed\n");
    }
    return node;
}

// Returns the root; the label of the middle row in labelOut.
static muiNodeId BuildList(muiContext* context, muiNodeId* labelOut)
{
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.container.direction = mui_flexColumn;
    column.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    muiNodeId root = Add(context, (muiNodeId){0, 0}, &column, 0);
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.alignItems = mui_alignCenter;
    row.container.columnGap = 6.0f;
    row.padding = (muiEdges){8.0f, 8.0f, 4.0f, 4.0f};
    muiLayoutStyle icon = muiDefaultLayoutStyle();
    icon.sizing.width = Length(16.0f);
    icon.sizing.height = Length(16.0f);
    muiLayoutStyle label = muiDefaultLayoutStyle();
    label.content = mui_contentHost;
    muiLayoutStyle spacer = muiDefaultLayoutStyle();
    spacer.item.grow = 1.0f;
    for (uint64_t i = 0; i < ROWS; i++)
    {
        muiNodeId line = Add(context, root, &row, 0);
        Add(context, line, &icon, 0);
        muiNodeId text = Add(context, line, &label, i);
        Add(context, line, &spacer, 0);
        if (i == ROWS / 2)
        {
            *labelOut = text;
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
    muiNodeId node = Add(context, parent, &style, (uint64_t)depth);
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
    // Measure calls of the run being timed.
    long measured;
} Scene;

static double Time(Scene* scene, float width, bool changeLeaf)
{
    if (changeLeaf && muiNode_MarkContentChanged(scene->context, scene->leaf) != mui_success)
    {
        fprintf(stderr, "mark failed\n");
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

static void Run(const char* name, bool list)
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
        scene.root = list ? BuildList(scene.context, &scene.leaf)
                          : BuildDeep(scene.context, (muiNodeId){0, 0}, 0, &scene.leaf);
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
        printf("%-5s %-10s %12.1f us %8ld measured\n", name, labels[i], best[i], measured[i]);
    }
}

int main(void)
{
    Run("list", true);
    Run("deep", false);
    return 0;
}
