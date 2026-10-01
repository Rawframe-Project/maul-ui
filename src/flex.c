// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CSS Flexbox's layout algorithm (section 9) for one line, over the
// node store. A container is run in its own frame of reference: main and
// cross axes, with physical start and end mapped once in Setup. Sizing
// passes are cached per node by their input; a full pass writes the
// children's rectangles and recurses.

#include "flex.h"

#include "flex_resolve.h"
#include "invariant.h"

#include <math.h>

#define FIRST_CHILD(tree, node)  (muiTreeAt((tree), (node))->links.firstChild)
#define NEXT_SIBLING(tree, node) (muiTreeAt((tree), (node))->links.next)

bool muiResolveDimension(muiDimension dimension, float extent, float* valueOut)
{
    if (dimension.kind != mui_dimensionValue)
    {
        return false;
    }
    if (dimension.scale == 0.0f)
    {
        *valueOut = dimension.offset;
        return true;
    }
    if (extent < 0.0f)
    {
        return false;
    }
    *valueOut = dimension.scale * extent + dimension.offset;
    return true;
}

static muiMeasureAxis Exact(float size)
{
    return (muiMeasureAxis){size, mui_measureExact};
}

static float EdgeStart(const muiEdges* edges, bool horizontal)
{
    return horizontal ? edges->start : edges->top;
}

static float EdgeEnd(const muiEdges* edges, bool horizontal)
{
    return horizontal ? edges->end : edges->bottom;
}

static float EdgeSum(const muiEdges* edges, bool horizontal)
{
    return EdgeStart(edges, horizontal) + EdgeEnd(edges, horizontal);
}

// Padding and border on one axis.
static float BoxSum(const muiLayoutStyle* style, bool horizontal)
{
    return EdgeSum(&style->padding, horizontal) + EdgeSum(&style->border, horizontal);
}

// A border-box size within its limits; the minimum wins over the maximum,
// and padding and border win over both.
static float ClampSize(float size, float minimum, float maximum, float box)
{
    return fmaxf(fmaxf(fminf(size, maximum), minimum), box);
}

// One axis's size and limits of a node.
typedef struct AxisSizing
{
    float size;
    float minimum;
    float maximum;
    bool definite;
    bool minimumAuto;
} AxisSizing;

static AxisSizing ResolveAxis(const muiSizing* sizing, bool horizontal, float extent)
{
    AxisSizing axis = {.maximum = INFINITY};
    muiDimension size = horizontal ? sizing->width : sizing->height;
    muiDimension minimum = horizontal ? sizing->minWidth : sizing->minHeight;
    muiDimension maximum = horizontal ? sizing->maxWidth : sizing->maxHeight;
    axis.definite = muiResolveDimension(size, extent, &axis.size);
    axis.minimumAuto = minimum.kind == mui_dimensionAuto;
    if (!muiResolveDimension(minimum, extent, &axis.minimum))
    {
        axis.minimum = 0.0f;
    }
    if (!muiResolveDimension(maximum, extent, &axis.maximum))
    {
        axis.maximum = INFINITY;
    }
    return axis;
}

static bool IsSized(muiMeasureMode mode)
{
    return mode == mui_measureExact || mode == mui_measureAtMost;
}

// Whether a size computed under an old constraint answers a new one, by
// the rules Yoga's cache uses: the same constraint; an exact size equal to
// what an unshrunk sizing gave; a max-content size that fits the new
// space; or a smaller space the old size still fits.
static bool AxisAnswers(muiMeasureAxis next, muiMeasureAxis old, float result)
{
    if (next.mode == old.mode && (!IsSized(next.mode) || next.size == old.size))
    {
        return true;
    }
    if (next.mode == mui_measureExact)
    {
        return old.mode != mui_measureMinContent && next.size == result;
    }
    if (next.mode == mui_measureAtMost)
    {
        bool fitsMaxContent = old.mode == mui_measureMaxContent;
        bool stricter = old.mode == mui_measureAtMost && next.size < old.size;
        return (fitsMaxContent || stricter) && result <= next.size;
    }
    return false;
}

static bool IsScaled(muiDimension dimension)
{
    return dimension.kind == mui_dimensionValue && dimension.scale != 0.0f;
}

// Whether a node's own sizing reads its parent's extents: only its scaled
// limits do, as the parent resolves the node's size itself.
static bool ReadsParentExtent(const muiSizing* sizing)
{
    return IsScaled(sizing->minWidth) || IsScaled(sizing->maxWidth) ||
           IsScaled(sizing->minHeight) || IsScaled(sizing->maxHeight);
}

static const muiCacheEntry* FindCached(const muiLayoutCache* cache, const muiSizingInput* input,
                                       bool keyExtents)
{
    for (int i = 0; i < MUI_CACHE_ENTRIES; i++)
    {
        const muiCacheEntry* entry = &cache->entries[i];
        bool sameExtents = !keyExtents || (entry->input.parentWidth == input->parentWidth &&
                                           entry->input.parentHeight == input->parentHeight);
        if (entry->valid && sameExtents &&
            AxisAnswers(input->width, entry->input.width, entry->size.width) &&
            AxisAnswers(input->height, entry->input.height, entry->size.height))
        {
            return entry;
        }
    }
    return nullptr;
}

static void StoreCached(muiLayoutCache* cache, const muiSizingInput* input, muiSize size)
{
    cache->entries[cache->next] = (muiCacheEntry){.input = *input, .size = size, .valid = true};
    cache->next = (uint8_t)((cache->next + 1) % MUI_CACHE_ENTRIES);
}

// The content-box constraint for host content from a border-box one.
static muiMeasureAxis ContentAxis(muiMeasureAxis axis, float box)
{
    if (axis.mode == mui_measureExact || axis.mode == mui_measureAtMost)
    {
        return (muiMeasureAxis){fmaxf(axis.size - box, 0.0f), axis.mode};
    }
    return axis;
}

static float SaneLength(float value)
{
    return isfinite(value) && value > 0.0f ? value : 0.0f;
}

static muiSize SizeLeaf(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    float boxWidth = BoxSum(style, true);
    float boxHeight = BoxSum(style, false);
    muiSize content = {0.0f, 0.0f};
    if (style->content == mui_contentHost && solver->measure != nullptr)
    {
        muiNodeId id = muiTreeIdOf(solver->tree, node);
        uint64_t hostKey = muiTreeAt(solver->tree, node)->hostKey;
        content =
            solver->measure(solver->measureUser, id, hostKey, ContentAxis(input->width, boxWidth),
                            ContentAxis(input->height, boxHeight));
        content.width = SaneLength(content.width);
        content.height = SaneLength(content.height);
    }
    AxisSizing width = ResolveAxis(&style->sizing, true, input->parentWidth);
    AxisSizing height = ResolveAxis(&style->sizing, false, input->parentHeight);
    muiSize size;
    size.width = input->width.mode == mui_measureExact
                     ? input->width.size
                     : ClampSize(content.width + boxWidth, width.minimum, width.maximum, boxWidth);
    size.height =
        input->height.mode == mui_measureExact
            ? input->height.size
            : ClampSize(content.height + boxHeight, height.minimum, height.maximum, boxHeight);
    return size;
}

// A container in its own frame: main and cross axes, its padding and
// border per side, its constraints and limits, and what it has resolved
// so far.
typedef struct Frame
{
    const muiSolver* solver;
    uint32_t node;
    const muiLayoutStyle* style;
    bool row;
    bool reverse;
    float boxMainStart;
    float boxMain;
    float boxCrossStart;
    float boxCross;
    muiMeasureAxis mainIn;
    muiMeasureAxis crossIn;
    AxisSizing mainLimits;
    AxisSizing crossLimits;
    // The children's percentage bases along each axis, negative when
    // indefinite.
    float extentMain;
    float extentCross;
    float gap;
    uint32_t count;
    float innerMain;
    float innerCross;
    float lineCross;
} Frame;

static muiSizingInput ChildInput(const Frame* frame, muiMeasureAxis main, muiMeasureAxis cross)
{
    muiSizingInput input;
    input.width = frame->row ? main : cross;
    input.height = frame->row ? cross : main;
    input.parentWidth = frame->row ? frame->extentMain : frame->extentCross;
    input.parentHeight = frame->row ? frame->extentCross : frame->extentMain;
    return input;
}

static float MainOf(const Frame* frame, muiSize size)
{
    return frame->row ? size.width : size.height;
}

static float CrossOf(const Frame* frame, muiSize size)
{
    return frame->row ? size.height : size.width;
}

static muiAlign AlignOf(const Frame* frame, const muiLayoutStyle* child)
{
    return child->item.alignSelf != mui_alignAuto ? child->item.alignSelf
                                                  : frame->style->container.alignItems;
}

// The constraint a child is sized under on the cross axis: its own
// definite size; with stretch, the line's size when the container's cross
// size is definite; otherwise fit-content within the container.
static muiMeasureAxis CrossConstraint(const Frame* frame, const muiLayoutStyle* child,
                                      const AxisSizing* cross, bool stretch)
{
    float boxCross = BoxSum(child, !frame->row);
    float margin = EdgeSum(&child->margin, !frame->row);
    if (cross->definite)
    {
        return Exact(ClampSize(cross->size, cross->minimum, cross->maximum, boxCross));
    }
    if (frame->crossIn.mode == mui_measureExact)
    {
        float space = fmaxf(frame->innerCross - margin, 0.0f);
        if (stretch && AlignOf(frame, child) == mui_alignStretch)
        {
            return Exact(ClampSize(space, cross->minimum, cross->maximum, boxCross));
        }
        return (muiMeasureAxis){space, mui_measureAtMost};
    }
    if (frame->crossIn.mode == mui_measureAtMost)
    {
        float space = frame->crossIn.size - frame->boxCross - margin;
        return (muiMeasureAxis){fmaxf(space, 0.0f), mui_measureAtMost};
    }
    return (muiMeasureAxis){0.0f, frame->crossIn.mode};
}

// The child's main size measured from its content under mode.
static float ContentMain(const Frame* frame, uint32_t child, muiMeasureMode mode,
                         muiMeasureAxis cross)
{
    muiSizingInput input = ChildInput(frame, (muiMeasureAxis){0.0f, mode}, cross);
    return MainOf(frame, muiSolveNode(frame->solver, child, &input, false));
}

// CSS Flexbox section 4.5: the smaller of the specified size and the
// min-content size, each within the maximum.
static float AutomaticMinimum(const Frame* frame, uint32_t child, const AxisSizing* main,
                              muiMeasureAxis cross)
{
    float content = fminf(ContentMain(frame, child, mui_measureMinContent, cross), main->maximum);
    if (main->definite)
    {
        content = fminf(content, fminf(main->size, main->maximum));
    }
    return content;
}

// Section 9.2: a child's margins, limits, flex base size and
// hypothetical main size.
static void PrepareItem(const Frame* frame, uint32_t child)
{
    muiLayoutNode* layout = &frame->solver->nodes[child - 1];
    const muiLayoutStyle* style = &layout->style;
    muiFlexItemState* item = &layout->item;
    AxisSizing main = ResolveAxis(&style->sizing, frame->row, frame->extentMain);
    AxisSizing cross = ResolveAxis(&style->sizing, !frame->row, frame->extentCross);
    float boxMain = BoxSum(style, frame->row);
    muiMeasureAxis crossConstraint = CrossConstraint(frame, style, &cross, true);
    *item = (muiFlexItemState){
        .marginMain = EdgeSum(&style->margin, frame->row),
        .marginCross = EdgeSum(&style->margin, !frame->row),
        .maxMain = main.maximum,
        .minCross = cross.minimum,
        .maxCross = cross.maximum,
    };
    float base = 0.0f;
    bool fromContent = false;
    if (!muiResolveDimension(style->item.basis, frame->extentMain, &base))
    {
        muiMeasureMode mode = frame->mainIn.mode == mui_measureMinContent ? mui_measureMinContent
                                                                          : mui_measureMaxContent;
        fromContent = !main.definite;
        base = main.definite ? main.size : ContentMain(frame, child, mode, crossConstraint);
    }
    item->base = fmaxf(base, boxMain);
    item->innerBase = item->base - boxMain;
    float minimum = main.minimum;
    if (main.minimumAuto && fromContent)
    {
        // A base from content is never below its automatic minimum, which
        // therefore only matters if the line shrinks.
        item->minimumPending = true;
        minimum = 0.0f;
    }
    else if (main.minimumAuto)
    {
        minimum = AutomaticMinimum(frame, child, &main, crossConstraint);
    }
    item->minMain = fmaxf(minimum, boxMain);
    item->hypothetical = ClampSize(item->base, item->minMain, item->maxMain, boxMain);
}

// Computes the automatic minimums PrepareItem left pending, for a line
// that shrinks.
static void ResolvePendingMinimums(const Frame* frame)
{
    const muiTree* tree = frame->solver->tree;
    for (uint32_t c = FIRST_CHILD(tree, frame->node); c != 0; c = NEXT_SIBLING(tree, c))
    {
        muiLayoutNode* layout = &frame->solver->nodes[c - 1];
        muiFlexItemState* item = &layout->item;
        if (!item->minimumPending)
        {
            continue;
        }
        const muiLayoutStyle* style = &layout->style;
        AxisSizing main = ResolveAxis(&style->sizing, frame->row, frame->extentMain);
        AxisSizing cross = ResolveAxis(&style->sizing, !frame->row, frame->extentCross);
        muiMeasureAxis crossConstraint = CrossConstraint(frame, style, &cross, true);
        float minimum = AutomaticMinimum(frame, c, &main, crossConstraint);
        item->minMain = fmaxf(minimum, BoxSum(style, frame->row));
        item->minimumPending = false;
        // A content-based base is never below the minimum, so the
        // hypothetical size stands.
    }
}

static Frame Setup(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    muiFlexDirection direction = style->container.direction;
    Frame frame = {.solver = solver, .node = node, .style = style};
    frame.row = direction == mui_flexRow || direction == mui_flexRowReverse;
    frame.reverse = direction == mui_flexRowReverse || direction == mui_flexColumnReverse;
    frame.boxMainStart =
        EdgeStart(&style->padding, frame.row) + EdgeStart(&style->border, frame.row);
    frame.boxMain = BoxSum(style, frame.row);
    frame.boxCrossStart =
        EdgeStart(&style->padding, !frame.row) + EdgeStart(&style->border, !frame.row);
    frame.boxCross = BoxSum(style, !frame.row);
    frame.mainIn = frame.row ? input->width : input->height;
    frame.crossIn = frame.row ? input->height : input->width;
    float parentMain = frame.row ? input->parentWidth : input->parentHeight;
    float parentCross = frame.row ? input->parentHeight : input->parentWidth;
    frame.mainLimits = ResolveAxis(&style->sizing, frame.row, parentMain);
    frame.crossLimits = ResolveAxis(&style->sizing, !frame.row, parentCross);
    frame.gap = frame.row ? style->container.columnGap : style->container.rowGap;
    frame.extentMain = -1.0f;
    frame.extentCross = -1.0f;
    if (frame.mainIn.mode == mui_measureExact)
    {
        frame.innerMain = fmaxf(frame.mainIn.size - frame.boxMain, 0.0f);
        frame.extentMain = frame.innerMain;
    }
    if (frame.crossIn.mode == mui_measureExact)
    {
        frame.innerCross = fmaxf(frame.crossIn.size - frame.boxCross, 0.0f);
        frame.extentCross = frame.innerCross;
    }
    return frame;
}

// Section 9.2 and 9.3: every child's hypothetical main size, then the
// container's inner main size, from its content when not given.
static void SizeMain(Frame* frame)
{
    const muiTree* tree = frame->solver->tree;
    float sum = 0.0f;
    for (uint32_t c = FIRST_CHILD(tree, frame->node); c != 0; c = NEXT_SIBLING(tree, c))
    {
        PrepareItem(frame, c);
        const muiFlexItemState* item = &frame->solver->nodes[c - 1].item;
        sum += item->hypothetical + item->marginMain;
        frame->count++;
    }
    float gaps = frame->count > 1 ? frame->gap * (float)(frame->count - 1) : 0.0f;
    if (frame->mainIn.mode != mui_measureExact)
    {
        float outer = ClampSize(sum + gaps + frame->boxMain, frame->mainLimits.minimum,
                                frame->mainLimits.maximum, frame->boxMain);
        frame->innerMain = outer - frame->boxMain;
    }
    if (sum + gaps > frame->innerMain)
    {
        ResolvePendingMinimums(frame);
    }
    muiResolveFlexibleLengths(tree, frame->solver->nodes, frame->node, frame->innerMain, gaps);
}

// Section 9.4: every child's hypothetical cross size and the line's cross
// size, then the container's, and stretched children's cross sizes.
static void SizeCross(Frame* frame)
{
    const muiTree* tree = frame->solver->tree;
    float line = 0.0f;
    for (uint32_t c = FIRST_CHILD(tree, frame->node); c != 0; c = NEXT_SIBLING(tree, c))
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        muiFlexItemState* item = &frame->solver->nodes[c - 1].item;
        AxisSizing cross = ResolveAxis(&style->sizing, !frame->row, frame->extentCross);
        muiMeasureAxis constraint = CrossConstraint(frame, style, &cross, false);
        muiSizingInput input = ChildInput(frame, Exact(item->target), constraint);
        float size = CrossOf(frame, muiSolveNode(frame->solver, c, &input, false));
        item->cross = ClampSize(size, item->minCross, item->maxCross, BoxSum(style, !frame->row));
        line = fmaxf(line, item->cross + item->marginCross);
    }
    if (frame->crossIn.mode != mui_measureExact)
    {
        float outer = ClampSize(line + frame->boxCross, frame->crossLimits.minimum,
                                frame->crossLimits.maximum, frame->boxCross);
        frame->innerCross = outer - frame->boxCross;
    }
    // A single line is as large as the container's inner cross size.
    frame->lineCross = frame->innerCross;
    for (uint32_t c = FIRST_CHILD(tree, frame->node); c != 0; c = NEXT_SIBLING(tree, c))
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        muiFlexItemState* item = &frame->solver->nodes[c - 1].item;
        AxisSizing cross = ResolveAxis(&style->sizing, !frame->row, frame->extentCross);
        if (!cross.definite && AlignOf(frame, style) == mui_alignStretch)
        {
            item->cross = ClampSize(frame->lineCross - item->marginCross, item->minCross,
                                    item->maxCross, BoxSum(style, !frame->row));
        }
    }
}

// The child's offset from the line's cross start, its margin included.
static float CrossOffset(const Frame* frame, const muiLayoutStyle* style,
                         const muiFlexItemState* item)
{
    float before = EdgeStart(&style->margin, !frame->row);
    float after = EdgeEnd(&style->margin, !frame->row);
    switch (AlignOf(frame, style))
    {
    case mui_alignEnd:
        return frame->lineCross - item->cross - after;
    case mui_alignCenter:
        return before + (frame->lineCross - item->marginCross - item->cross) / 2.0f;
    default:
        return before;
    }
}

// Section 9.5 and 9.6: places every child along both axes, sets its
// rectangle and lays it out in full.
static void Place(const Frame* frame)
{
    const muiTree* tree = frame->solver->tree;
    float used = 0.0f;
    for (uint32_t c = FIRST_CHILD(tree, frame->node); c != 0; c = NEXT_SIBLING(tree, c))
    {
        used +=
            frame->solver->nodes[c - 1].item.target + frame->solver->nodes[c - 1].item.marginMain;
    }
    float gaps = frame->count > 1 ? frame->gap * (float)(frame->count - 1) : 0.0f;
    float lead = 0.0f;
    float between = 0.0f;
    muiJustifySpacing(frame->style->container.justify, frame->innerMain - used - gaps, frame->count,
                      &lead, &between);
    float flow = lead;
    for (uint32_t c = FIRST_CHILD(tree, frame->node); c != 0; c = NEXT_SIBLING(tree, c))
    {
        muiLayoutNode* layout = &frame->solver->nodes[c - 1];
        const muiFlexItemState* item = &layout->item;
        const muiEdges* margin = &layout->style.margin;
        // In a reversed container the flow starts at the physical end.
        flow += frame->reverse ? EdgeEnd(margin, frame->row) : EdgeStart(margin, frame->row);
        float main = frame->reverse ? frame->innerMain - flow - item->target : flow;
        flow += item->target +
                (frame->reverse ? EdgeStart(margin, frame->row) : EdgeEnd(margin, frame->row));
        flow += frame->gap + between;
        float cross = CrossOffset(frame, &layout->style, item);
        float mainAt = frame->boxMainStart + main;
        float crossAt = frame->boxCrossStart + cross;
        layout->rect = frame->row ? (muiRect){mainAt, crossAt, item->target, item->cross}
                                  : (muiRect){crossAt, mainAt, item->cross, item->target};
        muiSizingInput input = ChildInput(frame, Exact(item->target), Exact(item->cross));
        input.parentWidth = frame->row ? frame->innerMain : frame->innerCross;
        input.parentHeight = frame->row ? frame->innerCross : frame->innerMain;
        (void)muiSolveNode(frame->solver, c, &input, true);
    }
}

// Sizes a row container's width as fit-content: its max-content size
// when that fits the space, else the space but no less than its
// min-content size.
static muiMeasureAxis FitMain(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    muiSizingInput probe = *input;
    float space = probe.width.size;
    probe.width = (muiMeasureAxis){0.0f, mui_measureMaxContent};
    float size = muiSolveNode(solver, node, &probe, false).width;
    if (size > space)
    {
        probe.width = (muiMeasureAxis){0.0f, mui_measureMinContent};
        size = fmaxf(muiSolveNode(solver, node, &probe, false).width, space);
    }
    return Exact(size);
}

static muiSize SizeContainer(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                             bool perform)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    bool row = style->container.direction == mui_flexRow ||
               style->container.direction == mui_flexRowReverse;
    muiSizingInput fitted = *input;
    muiMeasureAxis* main = row ? &fitted.width : &fitted.height;
    if (main->mode == mui_measureAtMost)
    {
        // Fit-content on the vertical axis is the max-content size, as
        // min-content and max-content heights are the same in CSS.
        *main = row ? FitMain(solver, node, input) : (muiMeasureAxis){0.0f, mui_measureMaxContent};
    }
    Frame frame = Setup(solver, node, &fitted);
    SizeMain(&frame);
    SizeCross(&frame);
    if (perform)
    {
        Place(&frame);
    }
    float mainSize = frame.innerMain + frame.boxMain;
    float crossSize = frame.innerCross + frame.boxCross;
    return row ? (muiSize){mainSize, crossSize} : (muiSize){crossSize, mainSize};
}

muiSize muiSolveNode(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                     bool perform)
{
    muiLayoutCache* cache = &solver->nodes[node - 1].cache;
    if (perform)
    {
        MUI_ASSERT(input->width.mode == mui_measureExact && input->height.mode == mui_measureExact);
        if (cache->finalValid && cache->finalSize.width == input->width.size &&
            cache->finalSize.height == input->height.size)
        {
            return cache->finalSize;
        }
    }
    else
    {
        // The parent gave both sizes: nothing below can change them.
        if (input->width.mode == mui_measureExact && input->height.mode == mui_measureExact)
        {
            return (muiSize){input->width.size, input->height.size};
        }
        const muiCacheEntry* hit =
            FindCached(cache, input, ReadsParentExtent(&solver->nodes[node - 1].style.sizing));
        if (hit != nullptr)
        {
            return hit->size;
        }
    }
    muiSize size = FIRST_CHILD(solver->tree, node) == 0
                       ? SizeLeaf(solver, node, input)
                       : SizeContainer(solver, node, input, perform);
    if (perform)
    {
        cache->finalValid = true;
        cache->finalSize = size;
    }
    else
    {
        StoreCached(cache, input, size);
    }
    return size;
}

muiSizingInput muiRootInput(const muiLayoutStyle* style, float availableWidth,
                            float availableHeight)
{
    muiSizingInput input = {
        .width = {availableWidth, mui_measureAtMost},
        .height = {availableHeight, mui_measureAtMost},
        .parentWidth = availableWidth,
        .parentHeight = availableHeight,
    };
    AxisSizing width = ResolveAxis(&style->sizing, true, availableWidth);
    AxisSizing height = ResolveAxis(&style->sizing, false, availableHeight);
    if (width.definite)
    {
        input.width =
            Exact(ClampSize(width.size, width.minimum, width.maximum, BoxSum(style, true)));
    }
    if (height.definite)
    {
        input.height =
            Exact(ClampSize(height.size, height.minimum, height.maximum, BoxSum(style, false)));
    }
    return input;
}
