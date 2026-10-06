// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling (record mui-0007): offsets within the extents layout
// measured, and a flag the next draw list reads to move transforms.

#include "maul-ui/scroll.h"

#include "context.h"
#include "layer.h"
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

// The step easing a node, or the end of the table.
static uint32_t EaseOf(const muiScrollStore* store, uint32_t slot)
{
    uint32_t i = 0;
    while (i < store->easeCount && store->eases[i].node.index1 != slot)
    {
        i++;
    }
    return i;
}

static void Drop(muiScrollStore* store, uint32_t i)
{
    store->eases[i] = store->eases[--store->easeCount];
}

// Stops the step easing a node, if one does.
static void Cancel(muiContext* context, uint32_t slot)
{
    muiScrollStore* store = &context->scrolling;
    uint32_t i = EaseOf(store, slot);
    if (i < store->easeCount)
    {
        Drop(store, i);
    }
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
    Cancel(context, slot);
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
        Cancel(context, container);
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

// A node's border box along an axis in a scrolling ancestor's border box,
// its shifts included, and the ancestor's padding box.
typedef struct Spans
{
    double start;
    double end;
    double portStart;
    double portEnd;
} Spans;

static Spans SpansOf(const muiContext* context, uint32_t container, uint32_t node, bool horizontal)
{
    const muiTree* tree = &context->tree;
    double start = 0.0;
    for (uint32_t at = node; at != container;)
    {
        const muiRect* rect = &context->layout[at - 1].rect;
        start += (double)(horizontal ? rect->x : rect->y);
        at = muiTreeAt(tree, at)->links.parent;
        const muiLayoutNode* parent = &context->layout[at - 1];
        const muiScrollState* scroll = &context->scrolls[at - 1];
        start += (double)(horizontal ? muiScrollShiftX(parent, scroll)
                                     : muiScrollShiftY(parent, scroll));
    }
    const muiLayoutNode* layout = &context->layout[container - 1];
    const muiEdges* border = &layout->style.border;
    const muiRect* rect = &context->layout[node - 1].rect;
    if (horizontal)
    {
        double left = (double)(layout->rtl ? border->end : border->start);
        double right = (double)(layout->rtl ? border->start : border->end);
        return (Spans){start, start + (double)rect->width, left,
                       (double)layout->rect.width - right};
    }
    return (Spans){start, start + (double)rect->height, (double)border->top,
                   (double)layout->rect.height - (double)border->bottom};
}

// Brings a node's border box into a scrolling ancestor's padding box.
static void Reveal(muiContext* context, uint32_t container, uint32_t node)
{
    // Along an axis it does not scroll, its limit keeps the offset 0.
    for (int axis = 0; axis < 2; axis++)
    {
        const Spans spans = SpansOf(context, container, node, axis == 0);
        MoveContent(context, container, axis == 0,
                    Nearest(spans.start, spans.end, spans.portStart, spans.portEnd));
    }
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

muiScrollRule muiDefaultScrollRule(void)
{
    return MUI_SCROLL_RULE;
}

muiResult muiSetScrollRule(muiContext* context, const muiScrollRule* rule)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (rule == nullptr || !isfinite(rule->wheelStep) || rule->wheelStep < 0.0f ||
        !isfinite(rule->lineStep) || rule->lineStep < 0.0f || !(rule->pageFraction > 0.0f) ||
        rule->pageFraction > 1.0f || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    context->scrolling.rule = *rule;
    return mui_success;
}

// Whether a scroll container can move by right and down, physical units
// its offsets would grow by, along either axis, from where a step easing
// it goes or else from its offsets.
static bool CanMove(const muiContext* context, uint32_t slot, float right, float down)
{
    const muiScrollStore* store = &context->scrolling;
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    uint32_t i = EaseOf(store, slot);
    bool easing = i < store->easeCount;
    float x = easing ? store->eases[i].toX : scroll->x;
    float y = easing ? store->eases[i].toY : scroll->y;
    float across = layout->rtl ? -right : right;
    float limitX = muiScrollLimit(&layout->style, size, scroll, true);
    float limitY = muiScrollLimit(&layout->style, size, scroll, false);
    return (across > 0.0f && x < limitX) || (across < 0.0f && x > 0.0f) ||
           (down > 0.0f && y < limitY) || (down < 0.0f && y > 0.0f);
}

// Moves a scroll container's offsets by right and down, within its
// limits.
static void ScrollBy(muiContext* context, uint32_t slot, float right, float down)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    float across = layout->rtl ? -right : right;
    float x =
        fminf(fmaxf(scroll->x + across, 0.0f), muiScrollLimit(&layout->style, size, scroll, true));
    float y =
        fminf(fmaxf(scroll->y + down, 0.0f), muiScrollLimit(&layout->style, size, scroll, false));
    if (x != scroll->x || y != scroll->y)
    {
        scroll->x = x;
        scroll->y = y;
        context->scrolled = true;
    }
}

// Steps a scroll container by right and down, physical units its
// offsets would grow by, from where a step easing it goes or else from
// its offsets: easing out over the rule's time, at once when motion is
// reduced, the time is 0, or the table is full.
static void Step(muiContext* context, uint32_t slot, float right, float down, uint64_t timeNs)
{
    muiScrollStore* store = &context->scrolling;
    const muiLayoutNode* layout = &context->layout[slot - 1];
    muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    uint32_t i = EaseOf(store, slot);
    bool easing = i < store->easeCount;
    float x = (easing ? store->eases[i].toX : scroll->x) + (layout->rtl ? -right : right);
    float y = (easing ? store->eases[i].toY : scroll->y) + down;
    x = fminf(fmaxf(x, 0.0f), muiScrollLimit(&layout->style, size, scroll, true));
    y = fminf(fmaxf(y, 0.0f), muiScrollLimit(&layout->style, size, scroll, false));
    if (context->environment.reducedMotion || store->rule.easeNs == 0 ||
        (!easing && store->easeCount == MUI_SCROLL_EASES))
    {
        if (easing)
        {
            Drop(store, i);
        }
        scroll->x = x;
        scroll->y = y;
        context->scrolled = true;
        return;
    }
    if (!easing)
    {
        store->easeCount++;
    }
    store->eases[i] =
        (muiScrollEase){muiTreeIdOf(&context->tree, slot), scroll->x, scroll->y, x, y, timeNs};
}

void muiScrollAdvance(muiContext* context, uint64_t nowNs)
{
    muiScrollStore* store = &context->scrolling;
    for (uint32_t i = 0; i < store->easeCount;)
    {
        const muiScrollEase* ease = &store->eases[i];
        uint32_t slot = muiTreeResolve(&context->tree, ease->node);
        if (slot == 0)
        {
            Drop(store, i);
            continue;
        }
        double elapsed = nowNs > ease->startNs ? (double)(nowNs - ease->startNs) : 0.0;
        // A time of 0 gives an infinity or a NaN, which fmin takes as 1.
        double t = fmin(elapsed / (double)store->rule.easeNs, 1.0);
        // A cubic ease out.
        float eased = (float)(1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t));
        const muiLayoutNode* layout = &context->layout[slot - 1];
        muiScrollState* scroll = &context->scrolls[slot - 1];
        const muiSize size = {layout->rect.width, layout->rect.height};
        // Layout may have moved the limits since the step began.
        scroll->x = fminf(ease->fromX + (ease->toX - ease->fromX) * eased,
                          muiScrollLimit(&layout->style, size, scroll, true));
        scroll->y = fminf(ease->fromY + (ease->toY - ease->fromY) * eased,
                          muiScrollLimit(&layout->style, size, scroll, false));
        context->scrolled = true;
        if (t >= 1.0)
        {
            Drop(store, i);
            continue;
        }
        i++;
    }
}

bool muiScrollIsEasingUnder(const muiContext* context, uint32_t root)
{
    const muiScrollStore* store = &context->scrolling;
    for (uint32_t i = 0; i < store->easeCount; i++)
    {
        uint32_t slot = muiTreeResolve(&context->tree, store->eases[i].node);
        if (slot != 0 && muiTreeIsAncestor(&context->tree, root, slot))
        {
            return true;
        }
    }
    return false;
}

uint32_t muiScrollerOf(const muiContext* context, uint32_t stop, uint32_t at, bool horizontal)
{
    const muiTree* tree = &context->tree;
    muiScrollAxes axis = horizontal ? mui_scrollHorizontal : mui_scrollVertical;
    for (; at != 0; at = muiTreeAt(tree, at)->links.parent)
    {
        if ((context->layout[at - 1].style.scrollAxes & axis) != 0)
        {
            return at;
        }
        if (at == stop)
        {
            break;
        }
    }
    return 0;
}

bool muiScrollIsNear(const muiContext* context, uint32_t container, uint32_t node, bool horizontal)
{
    const Spans spans = SpansOf(context, container, node, horizontal);
    double half = (spans.portEnd - spans.portStart) / 2.0;
    return spans.end + half >= spans.portStart && spans.start - half <= spans.portEnd;
}

bool muiScrollLine(muiContext* context, uint32_t container, muiDirection direction, uint64_t timeNs)
{
    bool horizontal = direction == mui_directionLeft || direction == mui_directionRight;
    bool back = direction == mui_directionUp || direction == mui_directionLeft;
    float step = back ? -context->scrolling.rule.lineStep : context->scrolling.rule.lineStep;
    float right = horizontal ? step : 0.0f;
    float down = horizontal ? 0.0f : step;
    if (!CanMove(context, container, right, down))
    {
        return false;
    }
    Step(context, container, right, down, timeNs);
    return true;
}

bool muiScrollPage(muiContext* context, uint32_t container, muiKeyCode code, bool backward,
                   uint64_t timeNs)
{
    const muiLayoutNode* layout = &context->layout[container - 1];
    const muiScrollState* scroll = &context->scrolls[container - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    float limit = muiScrollLimit(&layout->style, size, scroll, false);
    float page = (scroll->extentHeight - limit) * context->scrolling.rule.pageFraction;
    float down = 0.0f;
    switch (code)
    {
    // From anywhere within the limit, to the ends.
    case mui_codeHome:
        down = -limit;
        break;
    case mui_codeEnd:
        down = limit;
        break;
    default:
        down = backward ? -page : page;
        break;
    }
    if (!CanMove(context, container, 0.0f, down))
    {
        return false;
    }
    Step(context, container, 0.0f, down, timeNs);
    return true;
}

// The scroll container that keeps the wheel: the last one, while turns
// come within the latch time and over it. 0 when none does.
static uint32_t Latched(const muiContext* context, uint32_t hit, uint64_t timeNs)
{
    const muiScrollStore* store = &context->scrolling;
    uint32_t slot = muiTreeResolve(&context->tree, store->latched);
    // A time before the last wraps to a long one.
    bool recent = timeNs - store->latchedNs < store->rule.latchNs;
    return slot != 0 && recent && context->layout[slot - 1].style.scrollAxes != mui_scrollNone &&
                   muiTreeIsAncestor(&context->tree, slot, hit)
               ? slot
               : 0;
}

// The nearest scroll container from hit up that can move, not past root
// or the root of hit's layer. 0 when none can.
static uint32_t Choose(const muiContext* context, uint32_t root, uint32_t hit, float right,
                       float down)
{
    const muiTree* tree = &context->tree;
    for (uint32_t at = hit; at != 0; at = muiTreeAt(tree, at)->links.parent)
    {
        // One that does not scroll has limits of 0.
        if (CanMove(context, at, right, down))
        {
            return at;
        }
        if (at == root || muiIsLayerRoot(tree, at))
        {
            break;
        }
    }
    return 0;
}

bool muiScrollWheel(muiContext* context, uint32_t root, muiNodeId hitId, const muiWheelEvent* event)
{
    // A node the handler took away is 0, which nothing holds.
    uint32_t hit = muiTreeResolve(&context->tree, hitId);
    float x = event->deltaX;
    float y = event->deltaY;
    if ((event->modifiers & mui_modShift) != 0 && x == 0.0f)
    {
        // A turn toward the user goes right.
        x = -y;
        y = 0.0f;
    }
    muiScrollStore* store = &context->scrolling;
    float right = x * store->rule.wheelStep;
    float down = -y * store->rule.wheelStep;
    uint32_t container = Latched(context, hit, event->timeNs);
    if (container == 0)
    {
        container = Choose(context, root, hit, right, down);
    }
    if (container == 0)
    {
        return false;
    }
    // A smooth wheel or a touchpad turns by fractions, finely sampled
    // already: those apply at once.
    if (x == truncf(x) && y == truncf(y))
    {
        Step(context, container, right, down, event->timeNs);
    }
    else
    {
        Cancel(context, container);
        ScrollBy(context, container, right, down);
    }
    store->latched = muiTreeIdOf(&context->tree, container);
    store->latchedNs = event->timeNs;
    return true;
}

muiResult muiNode_GetScrollThumb(const muiContext* context, muiNodeId nodeId, bool horizontal,
                                 float track, float minimum, muiScrollThumb* thumbOut)
{
    if (context == nullptr || thumbOut == nullptr || !isfinite(track) || track < 0.0f ||
        !isfinite(minimum) || minimum < 0.0f)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveRead(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    float limit = muiScrollLimit(&layout->style, size, scroll, horizontal);
    if (limit <= 0.0f)
    {
        *thumbOut = (muiScrollThumb){0.0f, track};
        return mui_success;
    }
    float extent = horizontal ? scroll->extentWidth : scroll->extentHeight;
    float length = fminf(fmaxf(track * (extent - limit) / extent, minimum), track);
    float offset = horizontal ? scroll->x : scroll->y;
    *thumbOut = (muiScrollThumb){(track - length) * offset / limit, length};
    return mui_success;
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
