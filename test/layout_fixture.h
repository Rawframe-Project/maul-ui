// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The shape of a layout fixture, as tools/gen_layout_fixtures.py writes
// it: the nodes of one tree in preorder, each with its depth, style, host
// content (a box of a size, or that many such boxes, words, wrapping)
// and the rectangle Chrome gave it.

#ifndef MAUL_UI_TEST_LAYOUT_FIXTURE_H
#define MAUL_UI_TEST_LAYOUT_FIXTURE_H

#include "maul-ui/layout.h"

typedef struct LayoutFixtureNode
{
    uint8_t depth;
    muiLayoutStyle style;
    muiSize content;
    uint32_t words;
    muiRect expected;
} LayoutFixtureNode;

typedef struct LayoutFixture
{
    const char* name;
    float availableWidth;
    float availableHeight;
    const LayoutFixtureNode* nodes;
    int count;
} LayoutFixture;

#endif // MAUL_UI_TEST_LAYOUT_FIXTURE_H
