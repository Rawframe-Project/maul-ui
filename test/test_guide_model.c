// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's first program (docs/guide.md, section 1), as written there
// (tools/check_guide.py checks it, family record 0019): a screen built,
// laid out in a window's size and drawn as a list of boxes.

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

// Adds a node under parent, or a root for the null id, laid out and
// filled as asked; the null id when the context refuses.
static muiNodeId Add(muiContext* context, muiNodeId parent, const muiLayoutStyle* layout,
                     muiColor background)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = background;
    if (muiCreateNode(context, &def, &node) != mui_success ||
        muiNode_SetLayoutValues(context, node, layout, MUI_LAYOUT_PROPERTIES) != mui_success ||
        muiNode_SetVisualValues(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyBackground)) !=
            mui_success)
    {
        return (muiNodeId){0, 0};
    }
    if (parent.index1 != 0 &&
        muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}) != mui_success)
    {
        return (muiNodeId){0, 0};
    }
    return node;
}

// The whole window, a column 16 units in from its edges: a bar 48 high,
// and under it two panels side by side sharing the rest.
static muiNodeId BuildScreen(muiContext* context)
{
    const muiColor white = {1.0f, 1.0f, 1.0f, 1.0f};
    const muiColor blue = {0.2f, 0.4f, 0.8f, 1.0f};
    const muiColor grey = {0.9f, 0.9f, 0.9f, 1.0f};
    const muiColor none = {0.0f, 0.0f, 0.0f, 0.0f};

    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    column.sizing.height = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    column.container.direction = mui_flexColumn;
    column.container.rowGap = 8.0f;
    column.padding = (muiEdges){16.0f, 16.0f, 16.0f, 16.0f};
    muiNodeId root = Add(context, (muiNodeId){0, 0}, &column, white);

    muiLayoutStyle bar = muiDefaultLayoutStyle();
    bar.sizing.height = (muiDimension){0.0f, 48.0f, mui_dimensionValue};
    Add(context, root, &bar, blue);

    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.columnGap = 8.0f;
    row.item.grow = 1.0f;
    muiNodeId panels = Add(context, root, &row, none);

    muiLayoutStyle panel = muiDefaultLayoutStyle();
    panel.item.grow = 1.0f;
    Add(context, panels, &panel, grey);
    Add(context, panels, &panel, grey);
    return root;
}

// Lays the screen out in a window's size and shows what to draw.
static muiResult Frame(muiContext* context, muiNodeId root, float width, float height,
                       muiDrawList* listOut)
{
    const muiLayoutInput layout = {width, height, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    muiResult result = muiComputeLayout(context, root, &layout);
    if (result == mui_success)
    {
        result = muiBuildDrawList(context, root, &draw);
    }
    return result == mui_success ? muiGetDrawList(context, listOut) : result;
}

int main(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    if (muiCreateContext(&def, &context) != mui_success)
    {
        return 1;
    }
    muiNodeId root = BuildScreen(context);
    muiDrawList list;
    bool drawn = root.index1 != 0 && Frame(context, root, 800.0f, 600.0f, &list) == mui_success;
    // The window, the bar and the two panels: four boxes, back to front.
    uint32_t boxes = 0;
    for (uint32_t i = 0; drawn && i < list.commandCount; i++)
    {
        if (list.commands[i].kind == mui_drawBox)
        {
            boxes++;
        }
    }
    muiRect last = list.commands[list.commandCount - 1].box.rect;
    muiDestroyContext(context);
    // The second panel: 380 wide, from 16 + 380 + 8 across, under the bar.
    return drawn && boxes == 4 && last.x == 404.0f && last.y == 72.0f && last.width == 380.0f &&
                   last.height == 512.0f
               ? 0
               : 1;
}
