// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling (record mui-0007): offsets within the extents layout
// measured, and a flag the next draw list reads to move transforms.

#include "maul-ui/scroll.h"

#include "context.h"
#include "scroll.h"
#include "scroll_store.h"
#include "tree.h"

#include <math.h>

static uint32_t ResolveRead(const muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (nodeId.index1 == 0)
    {
        *statusOut = mui_errorInvalid;
        return 0;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

muiResult muiNode_SetScroll(muiContext* context, muiNodeId nodeId, float x, float y)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!isfinite(x) || !isfinite(y))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    const muiLayoutNode* layout = &context->layout[slot - 1];
    muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    x = fminf(fmaxf(x, 0.0f), muiScrollLimit(&layout->style, size, scroll, true));
    y = fminf(fmaxf(y, 0.0f), muiScrollLimit(&layout->style, size, scroll, false));
    if (x != scroll->x || y != scroll->y)
    {
        scroll->x = x;
        scroll->y = y;
        context->scrolled = true;
    }
    return mui_success;
}

// Moves a scroll container's offset, along one axis, so that its
// children move by move on the surface, within its limits.
static void MoveContent(muiContext* context, uint32_t container, bool horizontal, double move)
{
    const muiLayoutNode* layout = &context->layout[container - 1];
    muiScrollState* scroll = &context->scrolls[container - 1];
    // A child moves by -x, or by x under right to left, and by -y.
    double x = (double)scroll->x;
    double offset = horizontal ? (layout->rtl ? x + move : x - move) : (double)scroll->y - move;
    const muiSize size = {layout->rect.width, layout->rect.height};
    float limit = muiScrollLimit(&layout->style, size, scroll, horizontal);
    float clamped = fminf(fmaxf((float)offset, 0.0f), limit);
    float* field = horizontal ? &scroll->x : &scroll->y;
    if (clamped != *field)
    {
        *field = clamped;
        context->scrolled = true;
    }
}

// How far the children must move for a box from start to end to come
// into the box from boxStart to boxEnd, as CSSOM View's "nearest".
static double Nearest(double start, double end, double boxStart, double boxEnd)
{
    bool before = start < boxStart;
    bool after = end > boxEnd;
    bool larger = end - start > boxEnd - boxStart;
    if (before == after)
    {
        // Inside, or past both edges.
        return 0.0;
    }
    // Align the start: past the start and no larger, or past the end and
    // larger; else the end.
    return before != larger ? boxStart - start : boxEnd - end;
}

// Brings a node's border box into a scrolling ancestor's padding box.
static void Reveal(muiContext* context, uint32_t container, uint32_t node)
{
    const muiTree* tree = &context->tree;
    // The node's box in the container's border box, its shifts included.
    double x = 0.0;
    double y = 0.0;
    for (uint32_t at = node; at != container;)
    {
        x += (double)context->layout[at - 1].rect.x;
        y += (double)context->layout[at - 1].rect.y;
        at = muiTreeAt(tree, at)->links.parent;
        x += (double)muiScrollShiftX(&context->layout[at - 1], &context->scrolls[at - 1]);
        y += (double)muiScrollShiftY(&context->layout[at - 1], &context->scrolls[at - 1]);
    }
    const muiLayoutNode* layout = &context->layout[container - 1];
    const muiEdges* border = &layout->style.border;
    double left = (double)(layout->rtl ? border->end : border->start);
    double right = (double)layout->rect.width - (double)(layout->rtl ? border->start : border->end);
    double top = (double)border->top;
    double bottom = (double)layout->rect.height - (double)border->bottom;
    // Along an axis it does not scroll, its limit keeps the offset 0.
    const muiRect* rect = &context->layout[node - 1].rect;
    MoveContent(context, container, true, Nearest(x, x + (double)rect->width, left, right));
    MoveContent(context, container, false, Nearest(y, y + (double)rect->height, top, bottom));
}

void muiScrollReveal(muiContext* context, uint32_t slot)
{
    const muiTree* tree = &context->tree;
    for (uint32_t at = muiTreeAt(tree, slot)->links.parent; at != 0;
         at = muiTreeAt(tree, at)->links.parent)
    {
        if (context->layout[at - 1].style.scrollAxes != mui_scrollNone)
        {
            Reveal(context, at, slot);
        }
    }
}

muiResult muiNode_ScrollIntoView(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        muiScrollReveal(context, slot);
    }
    return status;
}

muiResult muiNode_GetScroll(const muiContext* context, muiNodeId nodeId, float* xOut, float* yOut)
{
    if (context == nullptr || xOut == nullptr || yOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveRead(context, nodeId, &status);
    if (slot != 0)
    {
        *xOut = context->scrolls[slot - 1].x;
        *yOut = context->scrolls[slot - 1].y;
    }
    return status;
}

muiResult muiNode_GetScrollExtent(const muiContext* context, muiNodeId nodeId, muiSize* extentOut)
{
    if (context == nullptr || extentOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveRead(context, nodeId, &status);
    if (slot != 0)
    {
        const muiScrollState* scroll = &context->scrolls[slot - 1];
        *extentOut = (muiSize){scroll->extentWidth, scroll->extentHeight};
    }
    return status;
}
