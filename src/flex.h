// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The solver: sizes a node under constraints and, when asked, lays out
// its subtree, by CSS Flexbox (record mui-0003). Results are border
// boxes in logical units.

#ifndef MAUL_UI_SRC_FLEX_H
#define MAUL_UI_SRC_FLEX_H

#include "layout_node.h"
#include "tree.h"

// What one muiComputeLayout works with.
typedef struct muiSolver
{
    const muiTree* tree;
    muiLayoutNode* nodes;
    muiMeasureFunction measure;
    void* measureUser;
} muiSolver;

// Returns node's border-box size under input. With perform, also sets
// the rectangle of every node below it; a node whose subtree is unchanged
// and whose size is the same as last time is not revisited.
muiSize muiSolveNode(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                     bool perform);

// The input a root is sized under in the host's space: its definite
// sizes within its limits; otherwise fit-content, which on the vertical
// axis is max-content, as for CSS's absolutely positioned boxes.
muiSizingInput muiRootInput(const muiLayoutStyle* style, float availableWidth,
                            float availableHeight);

// Resolves a dimension against an extent, negative when indefinite;
// returns false for an automatic value or an indefinite extent.
bool muiResolveDimension(muiDimension dimension, float extent, float* valueOut);

#endif // MAUL_UI_SRC_FLEX_H
