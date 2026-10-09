// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CSS's absolute positioning inside a flex container, one axis at a time:
// the containing block is the container's padding box; both insets of an
// axis stretch an automatic size between them; otherwise the width is
// fit-content and the height the content's; with no inset the child sits
// where it would as the container's only flex item. The anchor point then
// moves it by a fraction of its own size.

#include "absolute.h"

#include "sizing.h"

#include <math.h>

// The container along one axis: where its padding box and content box
// start in its border box, and how large they are.
typedef struct Span
{
    float paddingStart;
    float paddingSize;
    float contentStart;
    float contentSize;
} Span;

// A child's insets along one axis, resolved against the padding box.
typedef struct Insets
{
    float start;
    float end;
    bool hasStart;
    bool hasEnd;
} Insets;

static Span SpanOf(const muiLayoutStyle* container, const muiEdges* padding, float size,
                   bool horizontal)
{
    float borderStart = muiEdgeStart(&container->border, horizontal);
    float border = muiEdgeSum(&container->border, horizontal);
    return (Span){
        .paddingStart = borderStart,
        .paddingSize = fmaxf(size - border, 0.0f),
        .contentStart = borderStart + muiEdgeStart(padding, horizontal),
        .contentSize = fmaxf(size - muiBoxSum(padding, container, horizontal), 0.0f),
    };
}

// Its insets along an axis, horizontal ones swapped when its own direction
// runs against its container's (muiAgainstParent).
static Insets InsetsOf(const muiInsets* inset, bool horizontal, float extent, bool against)
{
    Insets result = {0};
    muiDimension start = horizontal ? (against ? inset->end : inset->start) : inset->top;
    muiDimension end = horizontal ? (against ? inset->start : inset->end) : inset->bottom;
    result.hasStart = muiResolveDimension(start, extent, &result.start);
    result.hasEnd = muiResolveDimension(end, extent, &result.end);
    return result;
}

// The child's size along an axis when its own value or both insets fix
// it; returns false when it comes from content.
static bool FixedSize(const muiLayoutStyle* style, const muiEdges* padding, bool horizontal,
                      const Span* spanX, const Span* spanY, const Insets* insets, float* sizeOut)
{
    const Span* span = horizontal ? spanX : spanY;
    muiAxisSizing axis = muiResolveAxis(&style->sizing, horizontal, span->paddingSize);
    muiEdges margins = muiMarginsOf(style, false);
    float box = muiBoxSum(padding, style, horizontal);
    if (axis.definite)
    {
        *sizeOut = muiClampSize(axis.size, axis.minimum, axis.maximum, box);
        return true;
    }
    if (insets->hasStart && insets->hasEnd)
    {
        float space =
            span->paddingSize - insets->start - insets->end - muiEdgeSum(&margins, horizontal);
        *sizeOut = muiClampSize(space, axis.minimum, axis.maximum, box);
        return true;
    }
    return false;
}

// Where an absolute child with no inset on an axis sits: as the
// container's only flex item, under justify-content on the main axis and
// align-self on the cross axis.
static float StaticOffset(const muiLayoutStyle* container, const muiLayoutStyle* child,
                          bool horizontal, const Span* span, float size, bool rtl)
{
    muiFlexDirection direction = container->container.direction;
    bool row = direction == mui_flexRow || direction == mui_flexRowReverse;
    muiEdges margins = muiMarginsOf(child, rtl);
    float start = muiEdgeStart(&margins, horizontal);
    float end = muiEdgeEnd(&margins, horizontal);
    float freeSpace = span->contentSize - size - start - end;
    bool reverse = false;
    float lead = 0.0f;
    if (horizontal == row)
    {
        reverse = direction == mui_flexRowReverse || direction == mui_flexColumnReverse;
        muiJustify justify = container->container.justify;
        bool centered = justify == mui_justifyCenter || justify == mui_justifySpaceAround ||
                        justify == mui_justifySpaceEvenly;
        lead = justify == mui_justifyEnd ? freeSpace : (centered ? freeSpace / 2.0f : 0.0f);
    }
    else
    {
        reverse = container->container.wrap == mui_wrapReverse;
        muiAlign align = child->item.alignSelf != mui_alignAuto ? child->item.alignSelf
                                                                : container->container.alignItems;
        lead = align == mui_alignEnd ? freeSpace
                                     : (align == mui_alignCenter ? freeSpace / 2.0f : 0.0f);
    }
    float flow = lead + (reverse ? end : start);
    float offset = reverse ? span->contentSize - flow - size : flow;
    return span->contentStart + offset;
}

// The child's offset from the container's border box along an axis.
static float Offset(const muiLayoutStyle* container, const muiLayoutStyle* child, bool horizontal,
                    const Span* span, const Insets* insets, float size, bool rtl)
{
    muiEdges margins = muiMarginsOf(child, rtl);
    float start = muiEdgeStart(&margins, horizontal);
    float end = muiEdgeEnd(&margins, horizontal);
    if (insets->hasStart && insets->hasEnd)
    {
        // Automatic margins share what the insets leave, as CSS solves an
        // over-constrained box; without them the end inset gives way.
        float freeSpace = span->paddingSize - insets->start - insets->end - size - start - end;
        bool autoStart = muiIsMarginAutoStart(child, horizontal, rtl);
        bool autoEnd = muiIsMarginAutoEnd(child, horizontal, rtl);
        if (autoStart && autoEnd)
        {
            start += fmaxf(freeSpace, 0.0f) / 2.0f;
        }
        else if (autoStart)
        {
            start += freeSpace;
        }
        return span->paddingStart + insets->start + start;
    }
    if (insets->hasStart)
    {
        return span->paddingStart + insets->start + start;
    }
    if (insets->hasEnd)
    {
        return span->paddingStart + span->paddingSize - insets->end - end - size;
    }
    return StaticOffset(container, child, horizontal, span, size, rtl);
}

static void PlaceChild(const muiSolver* solver, const muiLayoutStyle* container, uint32_t child,
                       const Span* spanX, const Span* spanY, bool rtl)
{
    muiLayoutNode* layout = &solver->nodes[child - 1];
    const muiLayoutStyle* style = &layout->style;
    bool against = muiAgainstParent(style, rtl);
    Insets insetX = InsetsOf(&style->placement.inset, true, spanX->paddingSize, against);
    Insets insetY = InsetsOf(&style->placement.inset, false, spanY->paddingSize, against);
    if (layout->listed)
    {
        // An item of a virtual list: across it, its content box; along it,
        // its own size, placed after layout (src/virtual.c).
        bool across = container->scrollAxes != mui_scrollHorizontal;
        const Span* span = across ? spanX : spanY;
        Insets* insets = across ? &insetX : &insetY;
        *insets = (Insets){span->contentStart - span->paddingStart,
                           span->paddingStart + span->paddingSize - span->contentStart -
                               span->contentSize,
                           true, true};
    }
    muiSizingInput input = {
        .parentWidth = spanX->paddingSize,
        .parentHeight = spanY->paddingSize,
        .rtl = rtl,
    };
    if (layout->popped)
    {
        // Where its exit popped it, at that size, its height its
        // content's unless its style gives one.
        input.width = muiExact(layout->rect.width);
        input.height = muiExact(layout->rect.height);
        input.contentHeight = !muiResolveAxis(&style->sizing, false, spanY->paddingSize).definite;
        (void)solver->solve(solver, child, &input, true);
        return;
    }
    float width = 0.0f;
    float height = 0.0f;
    // Its padding with the safe area, in the direction it inherits.
    const muiEdges padding = muiPaddingOf(style, &solver->safeArea, muiIsRtl(style, rtl));
    bool fixedHeight = FixedSize(style, &padding, false, spanX, spanY, &insetY, &height);
    if (!FixedSize(style, &padding, true, spanX, spanY, &insetX, &width))
    {
        muiEdges margins = muiMarginsOf(style, rtl);
        float space = spanX->paddingSize - (insetX.hasStart ? insetX.start : 0.0f) -
                      (insetX.hasEnd ? insetX.end : 0.0f) - muiEdgeSum(&margins, true);
        input.width = (muiMeasureAxis){fmaxf(space, 0.0f), mui_measureAtMost};
        input.height =
            fixedHeight ? muiExact(height) : (muiMeasureAxis){0.0f, mui_measureMaxContent};
        width = solver->solve(solver, child, &input, false).width;
    }
    else if (fixedHeight && muiRatioWidthMinimum(style) &&
             muiResolveAxis(&style->sizing, true, spanX->paddingSize).definite)
    {
        // Its ratio may give it a minimum width (src/solve.c).
        input.width = (muiMeasureAxis){0.0f, mui_measureMinContent};
        input.height = muiExact(height);
        width = fmaxf(width, solver->solve(solver, child, &input, false).width);
    }
    if (!fixedHeight)
    {
        input.width = muiExact(width);
        input.height = (muiMeasureAxis){0.0f, mui_measureMaxContent};
        height = solver->solve(solver, child, &input, false).height;
    }
    float x = Offset(container, style, true, spanX, &insetX, width, rtl);
    float y = Offset(container, style, false, spanY, &insetY, height, rtl);
    x -= style->placement.anchorX * width;
    y -= style->placement.anchorY * height;
    layout->rect = (muiRect){x, y, width, height};
    input.width = muiExact(width);
    input.height = muiExact(height);
    // A height neither given nor set by both insets is its content's,
    // unless its aspect ratio gives it.
    input.contentHeight = !fixedHeight && style->sizing.aspectRatio <= 0.0f;
    (void)solver->solve(solver, child, &input, true);
}

void muiPlaceAbsolute(const muiSolver* solver, uint32_t container, muiSize size, bool rtl)
{
    const muiLayoutStyle* style = &solver->nodes[container - 1].style;
    const muiEdges padding = muiPaddingOf(style, &solver->safeArea, rtl);
    Span spanX = SpanOf(style, &padding, size.width, true);
    Span spanY = SpanOf(style, &padding, size.height, false);
    for (uint32_t c = muiTreeAt(solver->tree, container)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        muiLayoutNode* layout = &solver->nodes[c - 1];
        if (layout->absolute)
        {
            if (layout->popped && rtl)
            {
                // Its last rectangle is where it was drawn; laid out from
                // the start, as the container mirrors it back after this.
                layout->rect.x = size.width - layout->rect.x - layout->rect.width;
            }
            PlaceChild(solver, style, c, &spanX, &spanY, rtl);
        }
    }
}
