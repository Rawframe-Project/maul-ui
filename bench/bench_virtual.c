// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtualization timings (record mui-0007): a tree of 100,000 rows, each
// an indent and a label 24 or 36 units tall, estimated at 28 until
// measured; and a table of 100,000 rows of eight cells, fixed at 24. A
// viewport of 800 scrolls each from top to bottom by 997 units a frame,
// then jumps to 500 places across it. The host realizes every window
// from a pool of rows, rebinding them and marking their labels' content
// changed, so no row keeps another's state. A frame is a layout, the
// realization when the window changed and the layout it needs, and a
// draw list. Prints the median, 99th percentile and worst frame in
// microseconds, the host's measures per frame, which do not depend on
// the machine, and the most nodes alive at once.

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/virtual.h"
#include "maul-ui/visual.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum
{
    ROWS = 100000,
    CELLS = 8,
    JUMPS = 500,
    POOL = 512,
    NODE_LIMIT = 8192,
    FRAMES = 4096
};

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static void Check(muiResult status, const char* what)
{
    if (status != mui_success)
    {
        fprintf(stderr, "%s failed: %s\n", what, muiResultName(status));
    }
}

static muiDimension Length(float offset)
{
    return (muiDimension){0.0f, offset, mui_dimensionValue};
}

// The host's side: which item each label shows, by node slot, and how
// many times it was asked to measure.
static uint32_t s_labelItem[NODE_LIMIT + 1];
static uint64_t s_measures;

// A label is 7 units per character of a made-up text, and in the tree
// 36 tall for every third item, else 24.
static muiSize MeasureLabel(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                            muiMeasureAxis height)
{
    (void)width;
    (void)height;
    (void)hostKey;
    s_measures++;
    bool tree = *(const bool*)user;
    uint32_t item = s_labelItem[nodeId.index1];
    float tall = tree && item % 3 == 0 ? 36.0f : 24.0f;
    return (muiSize){(float)(item % 30 + 5) * 7.0f, tall};
}

static muiNodeId Add(muiContext* context, muiNodeId parent, const muiLayoutStyle* style,
                     muiPropertyMask mask)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    Check(muiCreateNode(context, &def, &node), "create");
    Check(muiNode_SetLayoutValues(context, node, style, mask), "style");
    if (parent.index1 != 0)
    {
        Check(muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}), "insert");
    }
    return node;
}

typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId list;
    bool tree;
    // Rows bound now, and rows waiting in the pool, detached.
    muiNodeId bound[POOL];
    uint32_t boundCount;
    muiNodeId pool[POOL];
    uint32_t poolCount;
    uint32_t alive;
} Scene;

// A new row: in the tree an indent and a label; in the table eight
// labels, each its own cell.
static muiNodeId MakeRow(Scene* scene)
{
    muiContext* context = scene->context;
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.direction = mui_flexRow;
    muiNodeId node =
        Add(context, (muiNodeId){0, 0}, &row, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){0.2f, 0.2f, 0.25f, 1.0f};
    Check(muiNode_SetVisualValues(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyBackground)),
          "background");
    muiLayoutStyle label = muiDefaultLayoutStyle();
    label.content = mui_contentHost;
    if (scene->tree)
    {
        muiLayoutStyle indent = muiDefaultLayoutStyle();
        indent.sizing.width = Length(0.0f);
        (void)Add(context, node, &indent, MUI_PROPERTY_BIT(mui_propertyWidth));
        (void)Add(context, node, &label, MUI_PROPERTY_BIT(mui_propertyContent));
        scene->alive += 3;
        return node;
    }
    label.item.grow = 1.0f;
    for (int i = 0; i < CELLS; i++)
    {
        (void)Add(context, node, &label,
                  MUI_PROPERTY_BIT(mui_propertyContent) | MUI_PROPERTY_BIT(mui_propertyGrow));
    }
    scene->alive += 1 + CELLS;
    return node;
}

// Binds a row to an item: its labels show it, measured anew; in the tree
// its indent is the item's depth.
static void Bind(Scene* scene, muiNodeId row, uint32_t index)
{
    muiContext* context = scene->context;
    Check(muiNode_InsertChild(context, scene->list, row, (muiNodeId){0, 0}), "attach");
    Check(muiNode_SetItem(context, row, index), "bind");
    muiNodeId child = muiNode_GetFirstChild(context, row);
    if (scene->tree)
    {
        muiLayoutStyle indent = muiDefaultLayoutStyle();
        indent.sizing.width = Length((float)(index % 8) * 16.0f);
        Check(muiNode_SetLayoutValues(context, child, &indent, MUI_PROPERTY_BIT(mui_propertyWidth)),
              "indent");
        child = muiNode_GetNextSibling(context, child);
    }
    for (; child.index1 != 0; child = muiNode_GetNextSibling(context, child))
    {
        s_labelItem[child.index1] = index;
        Check(muiNode_MarkContentChanged(context, child), "content");
    }
}

// Realizes the window: rows outside it back to the pool, then a row for
// every item in it without one.
static void Realize(Scene* scene)
{
    muiContext* context = scene->context;
    uint32_t first = 0;
    uint32_t end = 0;
    Check(muiNode_GetVirtualWindow(context, scene->list, &first, &end), "window");
    static bool s_shown[POOL];
    for (uint32_t i = 0; i < end - first && i < POOL; i++)
    {
        s_shown[i] = false;
    }
    for (uint32_t i = 0; i < scene->boundCount;)
    {
        uint32_t index = 0;
        muiNodeId row = scene->bound[i];
        if (muiNode_GetItem(context, row, &index) == mui_success && index >= first && index < end)
        {
            s_shown[index - first] = true;
            i++;
            continue;
        }
        Check(muiNode_ClearItem(context, row), "unbind");
        Check(muiNode_Detach(context, row), "detach");
        scene->pool[scene->poolCount++] = row;
        scene->bound[i] = scene->bound[--scene->boundCount];
    }
    for (uint32_t index = first; index < end && scene->boundCount < POOL; index++)
    {
        if (s_shown[index - first])
        {
            continue;
        }
        muiNodeId row = scene->poolCount != 0 ? scene->pool[--scene->poolCount] : MakeRow(scene);
        Bind(scene, row, index);
        scene->bound[scene->boundCount++] = row;
    }
}

static void MakeScene(Scene* scene, bool tree)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.nodes = NODE_LIMIT;
    def.limits.virtualItems = ROWS;
    def.limits.drawCommands = 16384;
    *scene = (Scene){.tree = tree};
    Check(muiCreateContext(&def, &scene->context), "context");
    muiLayoutStyle root = muiDefaultLayoutStyle();
    root.sizing.width = Length(400.0f);
    root.sizing.height = Length(800.0f);
    scene->root = Add(scene->context, (muiNodeId){0, 0}, &root,
                      MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight));
    muiLayoutStyle list = root;
    list.scrollAxes = mui_scrollVertical;
    list.container.direction = mui_flexColumn;
    scene->list = Add(scene->context, scene->root, &list,
                      MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight) |
                          MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                          MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    muiVirtualList virtualList = muiDefaultVirtualList();
    virtualList.count = ROWS;
    virtualList.extent = tree ? 28.0f : 24.0f;
    virtualList.fixed = !tree;
    virtualList.overscan = 200.0f;
    Check(muiNode_SetVirtualList(scene->context, scene->list, &virtualList), "list");
}

typedef struct Timing
{
    double frame[FRAMES];
    uint32_t frames;
    uint64_t measures;
} Timing;

static int Ascending(const void* a, const void* b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;
    return (x > y) - (x < y);
}

static void Report(const char* what, Timing* timing)
{
    qsort(timing->frame, timing->frames, sizeof(double), Ascending);
    uint32_t n = timing->frames;
    printf("  %s: %u frames, median %.1f, 99th %.1f, worst %.1f us, %.1f measures a frame\n", what,
           n, timing->frame[n / 2], timing->frame[n - 1 - n / 100], timing->frame[n - 1],
           (double)timing->measures / (double)n);
}

// One frame: layout, the window realized when it changed and laid out
// again, a draw list.
static void Frame(Scene* scene, uint64_t frame, Timing* timing)
{
    muiContext* context = scene->context;
    const muiLayoutInput input = {
        400.0f, 800.0f, MeasureLabel, &scene->tree, frame * 8333333ull, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    uint64_t measures = s_measures;
    double start = Seconds();
    Check(muiComputeLayout(context, scene->root, &input), "layout");
    bool changed = false;
    muiNotification record = {0};
    while (muiNextNotification(context, &record) == mui_success)
    {
        changed |= record.kind == mui_notificationWindowChanged;
    }
    if (changed)
    {
        Realize(scene);
        Check(muiComputeLayout(context, scene->root, &input), "layout again");
        while (muiNextNotification(context, &record) == mui_success)
        {
        }
    }
    Check(muiBuildDrawList(context, scene->root, &draw), "draw");
    double elapsed = (Seconds() - start) * 1e6;
    if (timing->frames < FRAMES)
    {
        timing->frame[timing->frames++] = elapsed;
        timing->measures += s_measures - measures;
    }
}

static Timing s_scrolled;
static Timing s_jumped;

static void Run(bool tree)
{
    Scene scene;
    MakeScene(&scene, tree);
    uint64_t frame = 0;
    Frame(&scene, frame++, &s_scrolled);
    s_scrolled = (Timing){0};
    muiSize extent = {0};
    Check(muiNode_GetScrollExtent(scene.context, scene.list, &extent), "extent");
    for (float y = 997.0f; y < extent.height - 800.0f; y += 997.0f)
    {
        Check(muiNode_SetScroll(scene.context, scene.list, 0.0f, y), "scroll");
        Frame(&scene, frame++, &s_scrolled);
        Check(muiNode_GetScrollExtent(scene.context, scene.list, &extent), "extent");
    }
    s_jumped = (Timing){0};
    uint32_t seed = 12345;
    for (int i = 0; i < JUMPS; i++)
    {
        seed = seed * 1664525u + 1013904223u;
        float y = (float)(seed >> 8) / (float)(1u << 24) * (extent.height - 800.0f);
        Check(muiNode_SetScroll(scene.context, scene.list, 0.0f, y), "jump");
        Frame(&scene, frame++, &s_jumped);
    }
    printf("%s, %u nodes at most, content %.0f:\n",
           tree ? "tree, 100000 rows of 24 or 36, estimated at 28"
                : "table, 100000 rows of 8 cells, fixed at 24",
           scene.alive, (double)extent.height);
    Report("scrolled by 997", &s_scrolled);
    Report("jumped", &s_jumped);
    muiDestroyContext(scene.context);
}

int main(void)
{
    Run(true);
    Run(false);
    return 0;
}
