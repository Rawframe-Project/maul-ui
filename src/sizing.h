// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reading a node's authored values along one axis: Scale+Offset
// dimensions, sides of boxes, margins with their automatic sides, and
// limits.

#ifndef MAUL_UI_SRC_SIZING_H
#define MAUL_UI_SRC_SIZING_H

#include "maul-ui/layout.h"

#include <stdbool.h>

// One axis's size and limits of a node.
typedef struct muiAxisSizing
{
    float size;
    float minimum;
    float maximum;
    bool definite;
    bool minimumAuto;
} muiAxisSizing;

// Resolves a dimension against an extent, negative when indefinite;
// returns false for an automatic value or a scale against an indefinite
// extent.
bool muiResolveDimension(muiDimension dimension, float extent, float* valueOut);

// A node's size and limits on one axis, against its parent's extent.
muiAxisSizing muiResolveAxis(const muiSizing* sizing, bool horizontal, float extent);

muiMeasureAxis muiExact(float size);

// The start or end side of edges along an axis (start and end
// horizontally, top and bottom vertically), and both together.
float muiEdgeStart(const muiEdges* edges, bool horizontal);
float muiEdgeEnd(const muiEdges* edges, bool horizontal);
float muiEdgeSum(const muiEdges* edges, bool horizontal);

// Whether the start or end margin along an axis is automatic.
bool muiIsMarginAutoStart(const muiLayoutStyle* style, bool horizontal);
bool muiIsMarginAutoEnd(const muiLayoutStyle* style, bool horizontal);

// A node's margins with its automatic sides as zero.
muiEdges muiMarginsOf(const muiLayoutStyle* style);

// Padding and border on one axis.
float muiBoxSum(const muiLayoutStyle* style, bool horizontal);

// A border-box size within its limits; the minimum wins over the
// maximum, and padding and border win over both.
float muiClampSize(float size, float minimum, float maximum, float box);

#endif // MAUL_UI_SRC_SIZING_H
