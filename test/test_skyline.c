// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The skyline packer of glyph atlas plots: placement bottom-left, full
// plots refused, and random rectangles that never overlap while the
// segments tile the width.

#include "skyline.h"
#include "test_harness.h"

#include <string.h>

enum
{
    SIDE = 64
};

static muiSkylineNode s_nodes[SIDE];
static unsigned char s_grid[SIDE][SIDE];

static muiSkyline Make(uint16_t width, uint16_t height)
{
    muiSkyline skyline = {s_nodes, 0, width, height};
    muiSkylineReset(&skyline);
    memset(s_grid, 0, sizeof s_grid);
    return skyline;
}

// The segments run left to right without gaps, across the width, and
// neighbors differ in depth.
static bool Tiles(const muiSkyline* skyline)
{
    uint32_t x = 0;
    for (uint32_t i = 0; i < skyline->count; i++)
    {
        const muiSkylineNode* node = &skyline->nodes[i];
        if (node->x != x || node->width == 0 || node->y > skyline->height ||
            (i > 0 && node->y == skyline->nodes[i - 1].y))
        {
            return false;
        }
        x += node->width;
    }
    return x == skyline->width && skyline->count <= skyline->width;
}

// Marks a rectangle placed; false when it overlaps one before it.
static bool Claim(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    for (uint32_t row = y; row < y + height; row++)
    {
        for (uint32_t column = x; column < x + width; column++)
        {
            if (s_grid[row][column] != 0)
            {
                return false;
            }
            s_grid[row][column] = 1;
        }
    }
    return true;
}

static void TestBottomLeft(void)
{
    muiSkyline skyline = Make(16, 16);
    uint32_t x = 99;
    uint32_t y = 99;
    CHECK(muiSkylineInsert(&skyline, 8, 4, &x, &y) && x == 0 && y == 0, "first at the corner");
    CHECK(muiSkylineInsert(&skyline, 4, 8, &x, &y) && x == 8 && y == 0, "beside it, as high");
    CHECK(muiSkylineInsert(&skyline, 8, 2, &x, &y) && x == 0 && y == 4, "under the first");
    CHECK(muiSkylineInsert(&skyline, 4, 3, &x, &y) && x == 12 && y == 0, "the top right");
    CHECK(muiSkylineInsert(&skyline, 16, 8, &x, &y) && x == 0 && y == 8 && Tiles(&skyline),
          "across, under the deepest");
    CHECK(!muiSkylineInsert(&skyline, 1, 1, &x, &y) && x == 0 && y == 8, "full, nothing changed");
    CHECK(skyline.count == 1 && skyline.nodes[0].y == 16, "one segment, at the bottom");
}

static void TestFillsExactly(void)
{
    muiSkyline skyline = Make(16, 16);
    uint32_t x = 0;
    uint32_t y = 0;
    bool placed = true;
    for (int i = 0; i < 16; i++)
    {
        placed = placed && muiSkylineInsert(&skyline, 4, 4, &x, &y) && Claim(x, y, 4, 4);
    }
    CHECK(placed && Tiles(&skyline), "sixteen 4 by 4 squares fill 16 by 16");
    CHECK(!muiSkylineInsert(&skyline, 1, 1, &x, &y), "then nothing fits");
    muiSkylineReset(&skyline);
    CHECK(muiSkylineInsert(&skyline, 16, 16, &x, &y) && x == 0 && y == 0, "reset empties it");
    CHECK(!muiSkylineInsert(&skyline, 0, 4, &x, &y) && !muiSkylineInsert(&skyline, 4, 0, &x, &y),
          "empty rectangles refused");
    skyline = Make(16, 16);
    CHECK(!muiSkylineInsert(&skyline, 17, 1, &x, &y) && !muiSkylineInsert(&skyline, 1, 17, &x, &y),
          "larger than the area refused");
}

static void TestRandom(void)
{
    uint32_t state = 7;
    for (int round = 0; round < 200; round++)
    {
        muiSkyline skyline = Make(SIDE, SIDE);
        uint32_t area = 0;
        for (int i = 0; i < 400; i++)
        {
            state = state * 1664525u + 1013904223u;
            uint32_t width = 1 + (state >> 8) % 12;
            uint32_t height = 1 + (state >> 20) % 12;
            uint32_t x = 0;
            uint32_t y = 0;
            if (muiSkylineInsert(&skyline, width, height, &x, &y))
            {
                CHECK(x + width <= SIDE && y + height <= SIDE && Claim(x, y, width, height),
                      "inside and apart");
                area += width * height;
            }
            CHECK(Tiles(&skyline), "segments tile the width");
        }
        CHECK(area > SIDE * SIDE / 2, "more than half packed");
    }
}

int main(void)
{
    TestBottomLeft();
    TestFillsExactly();
    TestRandom();
    return s_failures == 0 ? 0 : 1;
}
