// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "sizing.h"

#include <math.h>

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

muiAxisSizing muiResolveAxis(const muiSizing* sizing, bool horizontal, float extent)
{
    muiAxisSizing axis = {.maximum = INFINITY};
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

muiMeasureAxis muiExact(float size)
{
    return (muiMeasureAxis){size, mui_measureExact};
}

float muiEdgeStart(const muiEdges* edges, bool horizontal)
{
    return horizontal ? edges->start : edges->top;
}

float muiEdgeEnd(const muiEdges* edges, bool horizontal)
{
    return horizontal ? edges->end : edges->bottom;
}

float muiEdgeSum(const muiEdges* edges, bool horizontal)
{
    return muiEdgeStart(edges, horizontal) + muiEdgeEnd(edges, horizontal);
}

bool muiIsMarginAutoStart(const muiLayoutStyle* style, bool horizontal)
{
    return (style->marginAuto & (horizontal ? mui_edgeStart : mui_edgeTop)) != 0;
}

bool muiIsMarginAutoEnd(const muiLayoutStyle* style, bool horizontal)
{
    return (style->marginAuto & (horizontal ? mui_edgeEnd : mui_edgeBottom)) != 0;
}

muiEdges muiMarginsOf(const muiLayoutStyle* style)
{
    muiEdges margins = style->margin;
    muiEdgeMask mask = style->marginAuto;
    margins.start = (mask & mui_edgeStart) != 0 ? 0.0f : margins.start;
    margins.end = (mask & mui_edgeEnd) != 0 ? 0.0f : margins.end;
    margins.top = (mask & mui_edgeTop) != 0 ? 0.0f : margins.top;
    margins.bottom = (mask & mui_edgeBottom) != 0 ? 0.0f : margins.bottom;
    return margins;
}

float muiBoxSum(const muiLayoutStyle* style, bool horizontal)
{
    return muiEdgeSum(&style->padding, horizontal) + muiEdgeSum(&style->border, horizontal);
}

float muiClampSize(float size, float minimum, float maximum, float box)
{
    return fmaxf(fmaxf(fminf(size, maximum), minimum), box);
}
