// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public layout functions: authored values in, rectangles out, and
// the run of the solver over the part of a tree that changed.

#include "maul-ui/layout.h"

#include "context.h"
#include "flex.h"
#include "layout_node.h"
#include "tree.h"

#include <math.h>

muiLayoutStyle muiDefaultLayoutStyle(void)
{
    return (muiLayoutStyle){
        .container = {.direction = mui_flexRow,
                      .wrap = mui_wrapNone,
                      .justify = mui_justifyStart,
                      .alignItems = mui_alignStretch,
                      .alignContent = mui_alignContentStretch},
        .item = {.shrink = 1.0f, .alignSelf = mui_alignAuto},
    };
}

static bool IsDimensionValid(muiDimension dimension)
{
    return dimension.kind <= mui_dimensionValue && isfinite(dimension.scale) &&
           isfinite(dimension.offset);
}

static bool IsSizingValid(const muiSizing* sizing)
{
    return IsDimensionValid(sizing->width) && IsDimensionValid(sizing->height) &&
           IsDimensionValid(sizing->minWidth) && IsDimensionValid(sizing->minHeight) &&
           IsDimensionValid(sizing->maxWidth) && IsDimensionValid(sizing->maxHeight);
}

static bool IsLength(float value)
{
    return isfinite(value) && value >= 0.0f;
}

static bool AreEdgesValid(const muiEdges* edges, bool negativeAllowed)
{
    const float values[] = {edges->start, edges->end, edges->top, edges->bottom};
    for (int i = 0; i < 4; i++)
    {
        if (!isfinite(values[i]) || (!negativeAllowed && values[i] < 0.0f))
        {
            return false;
        }
    }
    return true;
}

static bool IsStyleValid(const muiLayoutStyle* style)
{
    const muiFlexContainer* container = &style->container;
    const muiFlexItem* item = &style->item;
    return IsSizingValid(&style->sizing) && container->direction <= mui_flexColumnReverse &&
           container->wrap <= mui_wrapReverse &&
           container->alignContent <= mui_alignContentSpaceEvenly &&
           container->justify <= mui_justifySpaceEvenly && container->alignItems != mui_alignAuto &&
           container->alignItems <= mui_alignCenter && IsLength(container->rowGap) &&
           IsLength(container->columnGap) && IsLength(item->grow) && IsLength(item->shrink) &&
           IsDimensionValid(item->basis) && item->alignSelf <= mui_alignCenter &&
           AreEdgesValid(&style->margin, true) && AreEdgesValid(&style->border, false) &&
           AreEdgesValid(&style->padding, false) && style->content <= mui_contentHost;
}

// The slot of a live node for an edit, or 0 with the status in statusOut.
static uint32_t ResolveEdit(muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (nodeId.index1 == 0 || muiIsMeasuring(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

// A change to a node's size changes its parent's layout too.
static void MarkLayout(muiContext* context, uint32_t slot)
{
    muiTreeMark(&context->tree, slot, mui_stageLayout | mui_stagePaint);
    uint32_t parent = muiTreeAt(&context->tree, slot)->links.parent;
    if (parent != 0)
    {
        muiTreeMark(&context->tree, parent, mui_stageLayout | mui_stagePaint);
    }
}

muiResult muiNode_SetLayoutStyle(muiContext* context, muiNodeId nodeId, const muiLayoutStyle* style)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (style == nullptr || !IsStyleValid(style))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        context->layout[slot - 1].style = *style;
        MarkLayout(context, slot);
    }
    return status;
}

muiResult muiNode_GetLayoutStyle(const muiContext* context, muiNodeId nodeId,
                                 muiLayoutStyle* styleOut)
{
    if (context == nullptr || styleOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *styleOut = context->layout[slot - 1].style;
    return mui_success;
}

muiResult muiNode_MarkContentChanged(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        MarkLayout(context, slot);
    }
    return status;
}

// Forgets what the solver remembers about every node on a path to a
// change, then clears their layout flags.
static void Invalidate(muiContext* context, uint32_t root)
{
    muiTree* tree = &context->tree;
    for (uint32_t at = muiTreeNextOwing(tree, root, 0, mui_stageLayout); at != 0;
         at = muiTreeNextOwing(tree, root, at, mui_stageLayout))
    {
        context->layout[at - 1].cache = (muiLayoutCache){0};
    }
    muiTreeSweep(tree, root, mui_stageLayout);
}

muiResult muiComputeLayout(muiContext* context, muiNodeId rootId, const muiLayoutInput* input)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (input == nullptr || !IsLength(input->availableWidth) || !IsLength(input->availableHeight))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t root = ResolveEdit(context, rootId, &status);
    if (root == 0)
    {
        return status;
    }
    if (muiTreeAt(&context->tree, root)->links.parent != 0)
    {
        return muiRefuse(context);
    }
    Invalidate(context, root);
    muiSolver solver = {
        .tree = &context->tree,
        .nodes = context->layout,
        .measure = input->measure,
        .measureUser = input->measureUser,
    };
    muiSizingInput sizingInput = muiRootInput(&context->layout[root - 1].style,
                                              input->availableWidth, input->availableHeight);
    context->measuring = true;
    muiSize size = muiSolveNode(&solver, root, &sizingInput, false);
    sizingInput.width = (muiMeasureAxis){size.width, mui_measureExact};
    sizingInput.height = (muiMeasureAxis){size.height, mui_measureExact};
    (void)muiSolveNode(&solver, root, &sizingInput, true);
    context->measuring = false;
    context->layout[root - 1].rect = (muiRect){0.0f, 0.0f, size.width, size.height};
    return mui_success;
}

muiRect muiNode_GetRect(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 ? context->layout[slot - 1].rect : (muiRect){0.0f, 0.0f, 0.0f, 0.0f};
}
