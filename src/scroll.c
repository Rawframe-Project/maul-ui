// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling: offsets within the extents layout measured, and a flag the
// next draw list reads to move transforms.

#include "maul-ui/scroll.h"

#include "context.h"
#include "fpenv.h"
#include "invariant.h"
#include "layer.h"
#include "motion_math.h"
#include "scroll.h"
#include "scroll_store.h"
#include "tree.h"

#include "maul-tween/spring.h"

#include <math.h>

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

// Moves offsets x and y of a scroll container by right and down,
// physical units its offsets would grow by, within its limits.
static void Reach(const muiContext* context, uint32_t slot, float* x, float* y, float right,
                  float down)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    *x = fminf(fmaxf(*x + (scroll->right ? -right : right), 0.0f),
               muiScrollLimit(&layout->style, size, scroll, true));
    *y = fminf(fmaxf(*y + (scroll->bottom ? -down : down), 0.0f),
               muiScrollLimit(&layout->style, size, scroll, false));
}

// Stops the step easing a node, if one does.
// Stops what moves the node at slot, and takes back its overscroll.
static void Cancel(muiContext* context, uint32_t slot)
{
    muiScrollStore* store = &context->scrolling;
    uint32_t i = EaseOf(store, slot);
    if (i < store->easeCount)
    {
        Drop(store, i);
    }
    muiScrollState* scroll = &context->scrolls[slot - 1];
    if (scroll->overX != 0.0f || scroll->overY != 0.0f)
    {
        scroll->overX = 0.0f;
        scroll->overY = 0.0f;
        muiNoteScrolled(context, slot);
    }
}

muiResult muiSetNodeScroll(muiContext* context, muiNodeId nodeId, float x, float y)
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
    muiScrollState* scroll = &context->scrolls[slot - 1];
    Reach(context, slot, &x, &y, 0.0f, 0.0f);
    Cancel(context, slot);
    if (x != scroll->x || y != scroll->y)
    {
        scroll->x = x;
        scroll->y = y;
        muiNoteScrolled(context, slot);
    }
    return mui_success;
}

static MUI_NOINLINE muiResult NodeSetScrollWorker(muiContext* context, muiNodeId nodeId, float x,
                                                  float y)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = muiSetNodeScroll(context, nodeId, x, y);
        muiLeave(context);
    }
    return status;
}

muiResult muiNode_SetScroll(muiContext* context, muiNodeId nodeId, float x, float y)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NodeSetScrollWorker(context, nodeId, x, y);
    muiFloatEnvLeave(floatEnv);
    return result;
}

// Moves a scroll container's offset, along one axis, so that its
// children move by move on the surface, within its limits.
static void MoveContent(muiContext* context, uint32_t container, bool horizontal, double move)
{
    const muiLayoutNode* layout = &context->layout[container - 1];
    muiScrollState* scroll = &context->scrolls[container - 1];
    // A child moves by minus the offset, or by it from the right or the
    // bottom.
    bool far = horizontal ? scroll->right : scroll->bottom;
    double offset = (double)(horizontal ? scroll->x : scroll->y) + (far ? move : -move);
    const muiSize size = {layout->rect.width, layout->rect.height};
    float limit = muiScrollLimit(&layout->style, size, scroll, horizontal);
    float clamped = fminf(fmaxf((float)offset, 0.0f), limit);
    float* field = horizontal ? &scroll->x : &scroll->y;
    if (clamped != *field)
    {
        Cancel(context, container);
        *field = clamped;
        muiNoteScrolled(context, container);
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

muiResult muiScrollNodeIntoView(muiContext* context, muiNodeId nodeId)
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

static MUI_NOINLINE muiResult NodeScrollIntoViewWorker(muiContext* context, muiNodeId nodeId)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = muiScrollNodeIntoView(context, nodeId);
        muiLeave(context);
    }
    return status;
}

muiResult muiNode_ScrollIntoView(muiContext* context, muiNodeId nodeId)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NodeScrollIntoViewWorker(context, nodeId);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiScrollRule muiDefaultScrollRule(void)
{
    return MUI_SCROLL_RULE;
}

static muiResult SetScrollRule(muiContext* context, const muiScrollRule* rule)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (rule == nullptr || !isfinite(rule->wheelStep) || rule->wheelStep < 0.0f ||
        !isfinite(rule->lineStep) || rule->lineStep < 0.0f || !(rule->pageFraction > 0.0f) ||
        rule->pageFraction > 1.0f || !(rule->decelerationRate > 0.0f) ||
        !(rule->decelerationRate < 1.0f))
    {
        return muiRefuse(context);
    }
    context->scrolling.rule = *rule;
    return mui_success;
}

static MUI_NOINLINE muiResult SetScrollRuleWorker(muiContext* context, const muiScrollRule* rule)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = SetScrollRule(context, rule);
        muiLeave(context);
    }
    return status;
}

muiResult muiSetScrollRule(muiContext* context, const muiScrollRule* rule)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = SetScrollRuleWorker(context, rule);
    muiFloatEnvLeave(floatEnv);
    return result;
}

// Whether a scroll container can move by right and down along either
// axis, from where a step easing it goes or else from its offsets, which
// are always within its limits (overscroll is kept apart).
static bool CanMove(const muiContext* context, uint32_t slot, float right, float down)
{
    const muiScrollStore* store = &context->scrolling;
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    uint32_t i = EaseOf(store, slot);
    bool easing = i < store->easeCount && store->eases[i].kind == muiScrollEaseStep;
    float x = easing ? store->eases[i].toX : scroll->x;
    float y = easing ? store->eases[i].toY : scroll->y;
    float toX = x;
    float toY = y;
    Reach(context, slot, &toX, &toY, right, down);
    return toX != x || toY != y;
}

// Moves a scroll container's offsets by right and down, within its
// limits.
static void ScrollBy(muiContext* context, uint32_t slot, float right, float down)
{
    muiScrollState* scroll = &context->scrolls[slot - 1];
    float x = scroll->x;
    float y = scroll->y;
    Reach(context, slot, &x, &y, right, down);
    if (x != scroll->x || y != scroll->y)
    {
        scroll->x = x;
        scroll->y = y;
        muiNoteScrolled(context, slot);
    }
}

// Steps a scroll container by right and down, physical units its
// offsets would grow by, from where a step easing it goes or else from
// its offsets: easing out over the rule's time, at once when motion is
// reduced, the time is 0, or the table is full.
static void Step(muiContext* context, uint32_t slot, float right, float down, uint64_t timeNs)
{
    muiScrollStore* store = &context->scrolling;
    muiScrollState* scroll = &context->scrolls[slot - 1];
    // A step goes on from where a step easing goes; a fling or a bounce
    // stops where it is.
    uint32_t i = EaseOf(store, slot);
    if (i == store->easeCount || store->eases[i].kind != muiScrollEaseStep)
    {
        Cancel(context, slot);
        i = store->easeCount;
    }
    bool easing = i < store->easeCount;
    float x = easing ? store->eases[i].toX : scroll->x;
    float y = easing ? store->eases[i].toY : scroll->y;
    Reach(context, slot, &x, &y, right, down);
    if (context->environment.reducedMotion || store->rule.easeNs == 0 ||
        (!easing && store->easeCount == MUI_SCROLL_EASES))
    {
        if (easing)
        {
            Drop(store, i);
        }
        scroll->x = x;
        scroll->y = y;
        muiNoteScrolled(context, slot);
        return;
    }
    if (!easing)
    {
        store->easeCount++;
    }
    store->eases[i] = (muiScrollEase){.node = muiTreeIdOf(&context->tree, slot),
                                      .fromX = scroll->x,
                                      .fromY = scroll->y,
                                      .toX = x,
                                      .toY = y,
                                      .startNs = timeNs};
}

// Moves a step's offsets to where elapsed nanoseconds take them, a cubic
// ease out within the limits layout left; whether it is done.
static bool Ease(muiContext* context, uint32_t slot, const muiScrollEase* ease, double elapsed)
{
    // A time of 0 gives an infinity or a NaN, which fmin takes as 1.
    double t = fmin(elapsed / (double)context->scrolling.rule.easeNs, 1.0);
    float eased = (float)(1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t));
    // Between offsets within the limits, at most the limits layout left.
    muiScrollState* scroll = &context->scrolls[slot - 1];
    scroll->x = ease->fromX + (ease->toX - ease->fromX) * eased;
    scroll->y = ease->fromY + (ease->toY - ease->fromY) * eased;
    Reach(context, slot, &scroll->x, &scroll->y, 0.0f, 0.0f);
    return t >= 1.0;
}

// The bounce's spring, critically damped: Flutter's iOS spring,
// stiffness 100 over mass 0.5, damped by 2 sqrt(100 x 0.5), so 14.14
// radians a second; and the overscroll below which it is done, in units.
#define MUI_BOUNCE_STIFFNESS 100.0
#define MUI_BOUNCE_DAMPING   14.142135623730951
#define MUI_BOUNCE_MASS      0.5
#define MUI_BOUNCE_REST      0.1f

// The flings' slowest speed, below which they stop, in units a second.
#define MUI_FLING_STOP 10.0

static mtwnSpring BounceSpring(void)
{
    mtwnSpring spring;
    mtwnResult made =
        mtwnMakeSpring(MUI_BOUNCE_STIFFNESS, MUI_BOUNCE_DAMPING, MUI_BOUNCE_MASS, &spring);
    MUI_ASSERT(made == mtwn_success);
    return spring;
}

// One axis of a fling: its offset, its overscroll, and whether it is
// settled, at rest with nothing past a limit.
typedef struct muiFlung
{
    float offset;
    float over;
    bool settled;
} muiFlung;

// An axis of a fling from from at velocity v on decay, seconds on:
// within 0 and limit, the decay's; past one, at it, and with overscroll
// the bounded decay's spring on past it, which takes over at the edge at
// the speed the fling met it.
static muiFlung FlingAxis(const mtwnDecay* decay, const mtwnSpring* spring, double from, double v,
                          double limit, double seconds, bool overscroll)
{
    double at = 0.0;
    mtwnSampleDecay(decay, from, v, seconds, &at, NULL);
    if (at >= 0.0 && at <= limit)
    {
        return (muiFlung){(float)at, 0.0f, v == 0.0};
    }
    double edge = at > limit ? limit : 0.0;
    // A fling that began outside the limits, layout having moved one behind
    // it, or that never meets the edge (by rounding) stops at the limit.
    bool began = from >= 0.0 && from <= limit;
    mtwnBoundedDecay bounded;
    if (!overscroll || !began || isinf(mtwnDecayCrossingTime(decay, from, v, edge)) ||
        mtwnMakeBoundedDecay(decay, spring, 0.0, limit, &bounded) != mtwn_success)
    {
        return (muiFlung){(float)edge, 0.0f, true};
    }
    double position = 0.0;
    double moving = 0.0;
    mtwnSampleBoundedDecay(&bounded, from, v, seconds, &position, &moving);
    double over = position - edge;
    bool rest = fabs(over) < (double)MUI_BOUNCE_REST && fabs(moving) < MUI_FLING_STOP;
    return (muiFlung){(float)edge, rest ? 0.0f : (float)over, rest};
}

// Moves a fling's offsets to where elapsed nanoseconds take them: from
// its start at its velocity, keeping the rule's rate of it a millisecond;
// past a limit with overscroll, on past it. Whether it is done: slow
// enough with nothing past a limit, or settled along each axis.
static bool Decay(muiContext* context, uint32_t slot, const muiScrollEase* ease, double elapsed)
{
    // -1000 ln r a second, by the library's logarithm as before: same bits.
    mtwnDecay decay;
    mtwnResult made =
        mtwnMakeDecay(-muiLog((double)context->scrolling.rule.decelerationRate) * 1000.0, &decay);
    MUI_ASSERT(made == mtwn_success);
    const mtwnSpring spring = BounceSpring();
    double seconds = elapsed / 1e9;
    double kept = 0.0;
    mtwnSampleDecay(&decay, 0.0, 1.0, seconds, NULL, &kept);
    const muiLayoutNode* layout = &context->layout[slot - 1];
    muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    bool overscroll = context->scrolling.rule.overscroll;
    muiScrollAxes axes = layout->style.scrollAxes;
    muiFlung x = FlingAxis(&decay, &spring, (double)ease->fromX, (double)ease->velocityX,
                           (double)muiScrollLimit(&layout->style, size, scroll, true), seconds,
                           overscroll && (axes & mui_scrollHorizontal) != 0);
    muiFlung y = FlingAxis(&decay, &spring, (double)ease->fromY, (double)ease->velocityY,
                           (double)muiScrollLimit(&layout->style, size, scroll, false), seconds,
                           overscroll && (axes & mui_scrollVertical) != 0);
    scroll->x = x.offset;
    scroll->y = y.offset;
    scroll->overX = x.over;
    scroll->overY = y.over;
    double speed = sqrt((double)ease->velocityX * (double)ease->velocityX +
                        (double)ease->velocityY * (double)ease->velocityY) *
                   kept;
    return (speed < MUI_FLING_STOP && x.over == 0.0f && y.over == 0.0f) || (x.settled && y.settled);
}

// Moves a bounce's overscroll along the spring from where it began at
// rest toward none; whether it is done, the overscroll then none.
static bool Bounce(muiContext* context, uint32_t slot, const muiScrollEase* ease, double elapsed)
{
    const mtwnSpring spring = BounceSpring();
    double seconds = elapsed / 1e9;
    double overX = 0.0;
    double overY = 0.0;
    mtwnSampleSpring(&spring, (double)ease->fromX, 0.0, seconds, &overX, NULL);
    mtwnSampleSpring(&spring, (double)ease->fromY, 0.0, seconds, &overY, NULL);
    muiScrollState* scroll = &context->scrolls[slot - 1];
    scroll->overX = (float)overX;
    scroll->overY = (float)overY;
    if (fabsf(scroll->overX) < MUI_BOUNCE_REST && fabsf(scroll->overY) < MUI_BOUNCE_REST)
    {
        scroll->overX = 0.0f;
        scroll->overY = 0.0f;
        return true;
    }
    return false;
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
        bool done = ease->kind == muiScrollEaseFling    ? Decay(context, slot, ease, elapsed)
                    : ease->kind == muiScrollEaseBounce ? Bounce(context, slot, ease, elapsed)
                                                        : Ease(context, slot, ease, elapsed);
        muiNoteScrolled(context, slot);
        if (done)
        {
            Drop(store, i);
            continue;
        }
        i++;
    }
}

// The pan of a pointer, which drags one node at a time; the end of the
// table for none.
static uint32_t PanOf(const muiScrollStore* store, uint32_t pointer)
{
    uint32_t i = 0;
    while (i < store->panCount && store->pans[i].pointer != pointer)
    {
        i++;
    }
    return i;
}

// Takes out the pans of nodes destroyed since, whose drags end unseen.
static void PurgePans(muiContext* context)
{
    muiScrollStore* store = &context->scrolling;
    for (uint32_t i = store->panCount; i > 0; i--)
    {
        if (muiTreeResolve(&context->tree, store->pans[i - 1].node) == 0)
        {
            store->pans[i - 1] = store->pans[--store->panCount];
        }
    }
}

// iOS's rubber band, Maul Tween's: how far past a limit the children move
// for a pan past it by past in a scrollport of port; and the pan past it
// that moved them by over, its inverse, 0 from the scrollport on.
#define MUI_RUBBER_BAND 0.55

static float Band(float past, float port)
{
    return (float)mtwnRubberBand((double)past, (double)port, MUI_RUBBER_BAND);
}

static float Unband(float over, float port)
{
    return over < port ? (float)mtwnRubberBandInverse((double)over, (double)port, MUI_RUBBER_BAND)
                       : 0.0f;
}

// Where a pan along one axis puts the offset, within 0 and limit, and
// the overscroll past either, signed, when the rule allows it.
static float Along(const muiContext* context, float wanted, float limit, float port, bool axis,
                   float* overOut)
{
    float offset = fminf(fmaxf(wanted, 0.0f), limit);
    *overOut = 0.0f;
    if (context->scrolling.rule.overscroll && axis && wanted != offset)
    {
        *overOut = wanted < 0.0f ? -Band(-wanted, port) : Band(wanted - limit, port);
    }
    return offset;
}

// The scrollport of a node, its padding box.
static muiSize PortOf(const muiLayoutNode* layout)
{
    const muiEdges* border = &layout->style.border;
    return (muiSize){fmaxf(layout->rect.width - border->start - border->end, 0.0f),
                     fmaxf(layout->rect.height - border->top - border->bottom, 0.0f)};
}

// Moves a panned container's offsets opposite the pointer's offset from
// the drag's start, logical under right to left, within its limits; past
// them, its overscroll when the rule allows it.
static void Pan(muiContext* context, uint32_t slot, const muiScrollPan* pan,
                const muiPointerRecord* record)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    const muiSize port = PortOf(layout);
    muiScrollAxes axes = layout->style.scrollAxes;
    float across = scroll->right ? record->offsetX : -record->offsetX;
    float down = scroll->bottom ? record->offsetY : -record->offsetY;
    float overX = 0.0f;
    float overY = 0.0f;
    float x =
        Along(context, pan->startX + across, muiScrollLimit(&layout->style, size, scroll, true),
              port.width, (axes & mui_scrollHorizontal) != 0, &overX);
    float y =
        Along(context, pan->startY + down, muiScrollLimit(&layout->style, size, scroll, false),
              port.height, (axes & mui_scrollVertical) != 0, &overY);
    if (x != scroll->x || y != scroll->y || overX != scroll->overX || overY != scroll->overY)
    {
        scroll->x = x;
        scroll->y = y;
        scroll->overX = overX;
        scroll->overY = overY;
        muiNoteScrolled(context, slot);
    }
}

// The flings' window of moves, and their least and greatest speeds, in
// units a second.
#define MUI_FLING_WINDOW_NS 100000000ull
#define MUI_FLING_MIN       50.0
#define MUI_FLING_MAX       8000.0

// The slope of a least squares line through a pan's moves of the last
// 100 ms, per axis, in units a second; 0 with fewer than two.
static void VelocityOf(const muiScrollPan* pan, double* xOut, double* yOut)
{
    uint32_t count = pan->count < MUI_SCROLL_SAMPLES ? pan->count : MUI_SCROLL_SAMPLES;
    const muiScrollSample* last = &pan->samples[(pan->count - 1) % MUI_SCROLL_SAMPLES];
    double sumT = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    uint32_t used = 0;
    for (uint32_t k = 0; k < count; k++)
    {
        const muiScrollSample* sample = &pan->samples[(pan->count - 1 - k) % MUI_SCROLL_SAMPLES];
        if (last->timeNs - sample->timeNs > MUI_FLING_WINDOW_NS)
        {
            break;
        }
        // Seconds before the last.
        sumT -= (double)(last->timeNs - sample->timeNs) / 1e9;
        sumX += (double)sample->x;
        sumY += (double)sample->y;
        used++;
    }
    // One move, or moves at one time, give no spread: no velocity.
    *xOut = 0.0;
    *yOut = 0.0;
    double meanT = sumT / used;
    double meanX = sumX / used;
    double meanY = sumY / used;
    double spread = 0.0;
    double alongX = 0.0;
    double alongY = 0.0;
    for (uint32_t k = 0; k < used; k++)
    {
        const muiScrollSample* sample = &pan->samples[(pan->count - 1 - k) % MUI_SCROLL_SAMPLES];
        double t = -(double)(last->timeNs - sample->timeNs) / 1e9 - meanT;
        spread += t * t;
        alongX += t * ((double)sample->x - meanX);
        alongY += t * ((double)sample->y - meanY);
    }
    if (spread > 0.0)
    {
        *xOut = alongX / spread;
        *yOut = alongY / spread;
    }
}

// Starts a fling of a panned container at the pan's velocity, the
// offsets' opposite the pointer's; none below the least speed, under
// reduced motion, or with the ease table full.
static void StartFling(muiContext* context, uint32_t slot, const muiScrollPan* pan, uint64_t timeNs)
{
    muiScrollStore* store = &context->scrolling;
    double vx = 0.0;
    double vy = 0.0;
    VelocityOf(pan, &vx, &vy);
    double speed = sqrt(vx * vx + vy * vy);
    if (speed < MUI_FLING_MIN || context->environment.reducedMotion ||
        store->easeCount == MUI_SCROLL_EASES)
    {
        return;
    }
    double scale = speed > MUI_FLING_MAX ? MUI_FLING_MAX / speed : 1.0;
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    store->eases[store->easeCount++] = (muiScrollEase){
        .node = muiTreeIdOf(&context->tree, slot),
        .fromX = scroll->x,
        .fromY = scroll->y,
        .startNs = timeNs,
        .kind = muiScrollEaseFling,
        .velocityX = (float)((scroll->right ? vx : -vx) * scale),
        .velocityY = (float)((scroll->bottom ? vy : -vy) * scale),
    };
}

// Springs a released overscroll back to none; at once under reduced
// motion or with the ease table full.
static void StartBounce(muiContext* context, uint32_t slot, uint64_t timeNs)
{
    muiScrollStore* store = &context->scrolling;
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    if (context->environment.reducedMotion || store->easeCount == MUI_SCROLL_EASES)
    {
        Cancel(context, slot);
        return;
    }
    store->eases[store->easeCount++] = (muiScrollEase){
        .node = muiTreeIdOf(&context->tree, slot),
        .fromX = scroll->overX,
        .fromY = scroll->overY,
        .startNs = timeNs,
        .kind = muiScrollEaseBounce,
    };
}

void muiScrollStopFlings(muiContext* context, uint32_t slot, uint64_t timeNs)
{
    muiScrollStore* store = &context->scrolling;
    for (uint32_t at = slot; at != 0; at = muiTreeAt(&context->tree, at)->links.parent)
    {
        uint32_t i = EaseOf(store, at);
        if (i == store->easeCount || store->eases[i].kind != muiScrollEaseFling)
        {
            continue;
        }
        // A fling stopped past a limit springs back from there.
        const muiScrollState* scroll = &context->scrolls[at - 1];
        if (scroll->overX != 0.0f || scroll->overY != 0.0f)
        {
            store->eases[i] = (muiScrollEase){.node = store->eases[i].node,
                                              .fromX = scroll->overX,
                                              .fromY = scroll->overY,
                                              .startNs = timeNs,
                                              .kind = muiScrollEaseBounce};
            continue;
        }
        Drop(store, i);
    }
}

bool muiScrollPointer(muiContext* context, const muiPointerRecord* record)
{
    muiScrollStore* store = &context->scrolling;
    uint32_t node = muiTreeResolve(&context->tree, record->node);
    if (node == 0)
    {
        return false;
    }
    bool drag = record->kind == mui_pointerRecordDragStart ||
                record->kind == mui_pointerRecordDragMove ||
                record->kind == mui_pointerRecordDragEnd;
    if (!drag || record->pointerKind == mui_pointerMouse ||
        context->layout[node - 1].style.scrollAxes == mui_scrollNone)
    {
        return false;
    }
    uint32_t i = PanOf(store, record->pointer);
    if (record->kind == mui_pointerRecordDragStart)
    {
        PurgePans(context);
        i = PanOf(store, record->pointer);
        if (i == store->panCount)
        {
            if (store->panCount == MUI_SCROLL_PANS)
            {
                return false;
            }
            store->panCount++;
        }
        // A pan caught past a limit goes on from the pan that put it there.
        const muiScrollState* scroll = &context->scrolls[node - 1];
        const muiSize port = PortOf(&context->layout[node - 1]);
        float startX =
            scroll->x + copysignf(Unband(fabsf(scroll->overX), port.width), scroll->overX);
        float startY =
            scroll->y + copysignf(Unband(fabsf(scroll->overY), port.height), scroll->overY);
        Cancel(context, node);
        store->pans[i] = (muiScrollPan){record->pointer, record->node, startX, startY, 0, {{0}}};
    }
    if (i == store->panCount)
    {
        return false;
    }
    muiScrollPan* pan = &store->pans[i];
    pan->samples[pan->count++ % MUI_SCROLL_SAMPLES] =
        (muiScrollSample){record->timeNs, record->offsetX, record->offsetY};
    Pan(context, node, pan, record);
    if (record->kind == mui_pointerRecordDragEnd)
    {
        const muiScrollState* scroll = &context->scrolls[node - 1];
        if (scroll->overX != 0.0f || scroll->overY != 0.0f)
        {
            StartBounce(context, node, record->timeNs);
        }
        else if (!record->cancelled)
        {
            StartFling(context, node, pan, record->timeNs);
        }
        store->pans[i] = store->pans[--store->panCount];
    }
    return true;
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

// The origin of a node's border box in the root's space: its place and
// its ancestors' added up to the root, or to the top of its tree.
void muiScrollOriginOf(const muiContext* context, uint32_t root, uint32_t node, double* xOut,
                       double* yOut)
{
    const muiTree* tree = &context->tree;
    double x = 0.0;
    double y = 0.0;
    for (uint32_t at = node; at != 0;)
    {
        x += (double)context->layout[at - 1].rect.x;
        y += (double)context->layout[at - 1].rect.y;
        if (at == root)
        {
            break;
        }
        // Where its parent, scrolling, moves it.
        at = muiTreeAt(tree, at)->links.parent;
        if (at != 0)
        {
            x += (double)muiScrollShiftX(&context->layout[at - 1], &context->scrolls[at - 1]);
            y += (double)muiScrollShiftY(&context->layout[at - 1], &context->scrolls[at - 1]);
        }
    }
    *xOut = x;
    *yOut = y;
}
