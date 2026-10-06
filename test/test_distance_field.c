// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Distance fields of segments, against distances computed here in double
// precision: a square exactly, overlapping squares as their union, a
// hole, the even-odd rule, and rotated polygons whose corners sit on
// pixel center lines; and curves flattened within their tolerance.

#include "distance_field.h"
#include "flatten.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_SEGMENTS = 64,
    SIDE = 32
};

static unsigned char s_field[SIDE * SIDE];

// Draws the field of segments into s_field.
static void Draw(const muiSegment* segments, uint32_t count, const muiFieldGrid* grid)
{
    size_t pieces = muiCountPieces(segments, count);
    muiSegment* cut = malloc(pieces * sizeof(muiSegment));
    uint32_t* origins = malloc(pieces * sizeof(uint32_t));
    uint32_t n = muiCutPieces(segments, count, cut, origins);
    CHECK(n == pieces, "as many pieces as counted");
    size_t crossings = muiCountCrossings(cut, n, grid);
    size_t pixels = (size_t)grid->width * grid->height;
    muiFieldScratch scratch = {
        malloc(((size_t)grid->height + 1) * sizeof(uint32_t)),
        malloc((crossings + 1) * sizeof(muiCrossing)),
        malloc((pixels + 1) * sizeof(uint32_t)),
        malloc((pieces + 1) * sizeof(uint32_t)),
        malloc(muiEdgeRoom((uint32_t)pieces) * sizeof(muiSegment)),
        malloc(pixels * sizeof(float)),
    };
    muiDrawDistanceField(cut, origins, n, grid, &scratch, s_field);
    free(scratch.distances);
    free(scratch.edge);
    free(scratch.cellPieces);
    free(scratch.cellStarts);
    free(scratch.crossings);
    free(scratch.rowStarts);
    free(origins);
    free(cut);
}

// A rectangle's four sides, counterclockwise or clockwise.
static uint32_t Rectangle(muiSegment* out, float x0, float y0, float x1, float y1, bool clockwise)
{
    muiSegment sides[4] = {{x0, y0, x1, y0}, {x1, y0, x1, y1}, {x1, y1, x0, y1}, {x0, y1, x0, y0}};
    for (int i = 0; i < 4; i++)
    {
        muiSegment s = sides[clockwise ? 3 - i : i];
        out[i] = clockwise ? (muiSegment){s.x1, s.y1, s.x0, s.y0} : s;
    }
    return 4;
}

static double SegmentDistance(const muiSegment* s, double x, double y)
{
    double x0 = (double)s->x0;
    double y0 = (double)s->y0;
    double dx = (double)s->x1 - x0;
    double dy = (double)s->y1 - y0;
    double t = ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy);
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    double qx = x0 + t * dx - x;
    double qy = y0 + t * dy - y;
    return sqrt(qx * qx + qy * qy);
}

// The byte for a distance, inside or not.
static int Expected(double distance, bool inside, uint32_t spread)
{
    double value = 128.0 + (inside ? distance : -distance) * 128.0 / spread;
    value = floor(value + 0.5);
    return value < 0.0 ? 0 : value > 255.0 ? 255 : (int)value;
}

// The largest difference from the field the segments, all on the edge,
// give in double precision; inside says which centers are inside.
static int WorstError(const muiSegment* segments, uint32_t count, const muiFieldGrid* grid,
                      bool (*inside)(double x, double y))
{
    int worst = 0;
    for (uint32_t r = 0; r < grid->height; r++)
    {
        for (uint32_t c = 0; c < grid->width; c++)
        {
            double x = (double)grid->left + c + 0.5;
            double y = (double)grid->top - r - 0.5;
            double distance = 1e9;
            for (uint32_t i = 0; i < count; i++)
            {
                distance = fmin(distance, SegmentDistance(&segments[i], x, y));
            }
            int error =
                abs(Expected(distance, inside(x, y), grid->spread) - s_field[r * grid->width + c]);
            worst = error > worst ? error : worst;
        }
    }
    return worst;
}

static bool InSquare(double x, double y)
{
    return x > 2.0 && x < 10.0 && y > 2.0 && y < 10.0;
}

static void TestSquare(void)
{
    muiSegment segments[4];
    uint32_t count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, false);
    muiFieldGrid grid = {-2, 14, 16, 16, 4, false};
    Draw(segments, count, &grid);
    CHECK(WorstError(segments, count, &grid, InSquare) == 0, "a square, exactly");
    CHECK(s_field[4 * 16 + 4] == 144 && s_field[3 * 16 + 4] == 112 && s_field[0] == 0 &&
              s_field[8 * 16 + 8] == 240,
          "half a pixel in, half out, far out, deep in");
    count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, true);
    Draw(segments, count, &grid);
    CHECK(WorstError(segments, count, &grid, InSquare) == 0, "either winding");
}

static bool InUnion(double x, double y)
{
    return x > 2.0 && x < 14.0 && y > 2.0 && y < 10.0;
}

static void TestUnion(void)
{
    // Two squares overlapping in the middle draw as the rectangle that is
    // their union: the sides within it are left out.
    muiSegment segments[8];
    uint32_t count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, false);
    count += Rectangle(segments + count, 6.0f, 2.0f, 14.0f, 10.0f, false);
    muiFieldGrid grid = {-2, 14, 20, 16, 4, false};
    Draw(segments, count, &grid);
    unsigned char both[SIDE * SIDE];
    memcpy(both, s_field, sizeof both);
    muiSegment whole[4];
    (void)Rectangle(whole, 2.0f, 2.0f, 14.0f, 10.0f, false);
    Draw(whole, 4, &grid);
    CHECK(memcmp(both, s_field, (size_t)grid.width * grid.height) == 0,
          "two overlapping squares are their union");
    CHECK(WorstError(whole, 4, &grid, InUnion) == 0, "the union, exactly");
    // Under the even-odd rule the overlap is outside.
    grid.evenOdd = true;
    Draw(segments, count, &grid);
    CHECK(s_field[6 * 20 + 10] < 128 && s_field[6 * 20 + 6] > 128 && s_field[6 * 20 + 14] > 128,
          "the overlap outside, the rest inside");
}

static bool InFrame(double x, double y)
{
    return InSquare(x, y) && !(x > 4.0 && x < 8.0 && y > 4.0 && y < 8.0);
}

static void TestHole(void)
{
    muiSegment segments[8];
    uint32_t count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, false);
    count += Rectangle(segments + count, 4.0f, 4.0f, 8.0f, 8.0f, true);
    muiFieldGrid grid = {-2, 14, 16, 16, 4, false};
    Draw(segments, count, &grid);
    CHECK(WorstError(segments, count, &grid, InFrame) == 0, "a square with a hole, exactly");
    CHECK(s_field[8 * 16 + 8] < 128, "the hole is outside");
}

static const double PI = 3.14159265358979323846;

static void TestPolygons(void)
{
    // Rotated polygons with a corner on a row's center line: no piece of
    // a simple outline is left out, so the field is the exact one, to a
    // level of rounding.
    uint32_t state = 5;
    int worst = 0;
    for (int round = 0; round < 60; round++)
    {
        state = state * 1664525u + 1013904223u;
        int corners = 3 + (int)((state >> 8) % 6);
        double radius = 4.0 + (double)((state >> 16) % 60) / 10.0;
        double angle = (double)(state >> 20) / 4096.0;
        muiSegment segments[MAX_SEGMENTS];
        for (int i = 0; i < corners; i++)
        {
            double a0 = angle + 2.0 * PI * i / corners;
            double a1 = angle + 2.0 * PI * (i + 1) / corners;
            segments[i] =
                (muiSegment){(float)(12.5 + radius * cos(a0)), (float)(12.5 + radius * sin(a0)),
                             (float)(12.5 + radius * cos(a1)), (float)(12.5 + radius * sin(a1))};
        }
        float shift = 0.5f - (segments[0].y0 - floorf(segments[0].y0));
        for (int i = 0; i < corners; i++)
        {
            segments[i].y0 += shift;
            segments[i].y1 += shift;
        }
        muiFieldGrid grid = {0, 25, 25, 25, 4, false};
        Draw(segments, (uint32_t)corners, &grid);
        for (uint32_t r = 0; r < grid.height; r++)
        {
            for (uint32_t c = 0; c < grid.width; c++)
            {
                double x = c + 0.5;
                double y = grid.top - r - 0.5;
                double distance = 1e9;
                bool inside = false;
                for (int i = 0; i < corners; i++)
                {
                    const muiSegment* e = &segments[i];
                    distance = fmin(distance, SegmentDistance(e, x, y));
                    double x0 = (double)e->x0;
                    double y0 = (double)e->y0;
                    double y1 = (double)e->y1;
                    if ((y0 > y) != (y1 > y) &&
                        x < x0 + (y - y0) * ((double)e->x1 - x0) / (y1 - y0))
                    {
                        inside = !inside;
                    }
                }
                int error =
                    abs(Expected(distance, inside, grid.spread) - s_field[r * grid.width + c]);
                worst = error > worst ? error : worst;
            }
        }
    }
    CHECK(worst <= 1, "polygons within a level of exact");
}

// The field of a simple polygon, against the exact one: every side is on
// the edge.
static int PolygonError(const muiSegment* segments, int corners, const muiFieldGrid* grid)
{
    Draw(segments, (uint32_t)corners, grid);
    int worst = 0;
    for (uint32_t r = 0; r < grid->height; r++)
    {
        for (uint32_t c = 0; c < grid->width; c++)
        {
            double x = (double)grid->left + c + 0.5;
            double y = (double)grid->top - r - 0.5;
            double distance = 1e9;
            bool inside = false;
            for (int i = 0; i < corners; i++)
            {
                const muiSegment* e = &segments[i];
                distance = fmin(distance, SegmentDistance(e, x, y));
                double x0 = (double)e->x0;
                double y0 = (double)e->y0;
                double y1 = (double)e->y1;
                if ((y0 > y) != (y1 > y) && x < x0 + (y - y0) * ((double)e->x1 - x0) / (y1 - y0))
                {
                    inside = !inside;
                }
            }
            int error =
                abs(Expected(distance, inside, grid->spread) - s_field[r * grid->width + c]);
            worst = error > worst ? error : worst;
        }
    }
    return worst;
}

static void TestNotches(void)
{
    // A square with a V cut into its top whose tip is on a row's center
    // line, and its mirror cut from the left with the tip on a column's:
    // the line touches the tip from inside, and the pieces meeting there
    // stay on the edge.
    const float tips[] = {6.5f, 7.0f, 7.25f};
    int worst = 0;
    for (size_t i = 0; i < sizeof tips / sizeof tips[0]; i++)
    {
        float tip = tips[i];
        muiSegment top[7] = {{2, 2, 12, 2},   {12, 2, 12, 12}, {12, 12, 8, 12}, {8, 12, 7, tip},
                             {7, tip, 6, 12}, {6, 12, 2, 12},  {2, 12, 2, 2}};
        for (int k = 0; k < 7; k++)
        {
            top[k] = (muiSegment){top[k].x0, top[k].y0 + 0.5f, top[k].x1, top[k].y1 + 0.5f};
        }
        muiFieldGrid grid = {-2, 16, 18, 18, 4, false};
        int error = PolygonError(top, 7, &grid);
        worst = error > worst ? error : worst;
        muiSegment side[7];
        for (int k = 0; k < 7; k++)
        {
            // Mirrored across the diagonal, reversed to keep the winding.
            const muiSegment* m = &top[6 - k];
            side[k] = (muiSegment){m->y1, m->x1, m->y0, m->x0};
        }
        error = PolygonError(side, 7, &grid);
        worst = error > worst ? error : worst;
    }
    CHECK(worst <= 1, "notches whose tips touch center lines");
}

static void TestFractionalUnion(void)
{
    // Overlapping rectangles at fractional places, against their union:
    // pieces no center line crosses take their neighbors' verdict.
    uint32_t state = 3;
    int worst = 0;
    for (int round = 0; round < 80; round++)
    {
        float v[6];
        for (int i = 0; i < 6; i++)
        {
            state = state * 1664525u + 1013904223u;
            v[i] = (float)((state >> 8) % 1000) / 1000.0f;
        }
        float x0 = 2.0f + v[0];
        float x1 = 8.0f + v[1];
        float x2 = 12.0f + v[2];
        float y0 = 2.0f + v[3];
        float y1 = 9.0f + v[4];
        float split = 5.0f + v[5];
        muiSegment segments[8];
        uint32_t count = Rectangle(segments, x0, y0, x1, y1, false);
        count += Rectangle(segments + count, split, y0, x2, y1, false);
        muiFieldGrid grid = {-2, 14, 20, 16, 4, false};
        Draw(segments, count, &grid);
        unsigned char both[SIDE * SIDE];
        memcpy(both, s_field, sizeof both);
        muiSegment whole[4];
        (void)Rectangle(whole, x0, y0, x2, y1, false);
        Draw(whole, 4, &grid);
        for (size_t i = 0; i < (size_t)grid.width * grid.height; i++)
        {
            int error = abs((int)both[i] - (int)s_field[i]);
            worst = error > worst ? error : worst;
        }
    }
    CHECK(worst <= 1, "fractional overlaps are their union");
}

// A square turned by angle about (x, y), half a side h.
static void Turned(muiSegment* out, double x, double y, double h, double angle)
{
    double px[4];
    double py[4];
    for (int i = 0; i < 4; i++)
    {
        double a = angle + i * PI / 2.0 + PI / 4.0;
        px[i] = x + h * sqrt(2.0) * cos(a);
        py[i] = y + h * sqrt(2.0) * sin(a);
    }
    for (int i = 0; i < 4; i++)
    {
        out[i] = (muiSegment){(float)px[i], (float)py[i], (float)px[(i + 1) % 4],
                              (float)py[(i + 1) % 4]};
    }
}

static bool InPolygon(const muiSegment* sides, int count, double x, double y)
{
    bool inside = false;
    for (int i = 0; i < count; i++)
    {
        double x0 = (double)sides[i].x0;
        double y0 = (double)sides[i].y0;
        double y1 = (double)sides[i].y1;
        if ((y0 > y) != (y1 > y) && x < x0 + (y - y0) * ((double)sides[i].x1 - x0) / (y1 - y0))
        {
            inside = !inside;
        }
    }
    return inside;
}

// The largest difference from the field of two simple polygons' union,
// against its edge found by sampling each side where the other polygon
// is not, every 1/400 pixel.
static int UnionError(const muiSegment* a, int aCount, const muiSegment* b, int bCount,
                      const muiFieldGrid* grid)
{
    static double samples[2][60000];
    int n = 0;
    for (int i = 0; i < aCount + bCount; i++)
    {
        const muiSegment* side = i < aCount ? &a[i] : &b[i - aCount];
        const muiSegment* other = i < aCount ? b : a;
        int otherCount = i < aCount ? bCount : aCount;
        double dx = (double)side->x1 - (double)side->x0;
        double dy = (double)side->y1 - (double)side->y0;
        int steps = (int)ceil(sqrt(dx * dx + dy * dy) * 400.0);
        for (int k = 0; k <= steps && n < 60000; k++)
        {
            double x = (double)side->x0 + dx * k / steps;
            double y = (double)side->y0 + dy * k / steps;
            if (!InPolygon(other, otherCount, x, y))
            {
                samples[0][n] = x;
                samples[1][n++] = y;
            }
        }
    }
    int worst = 0;
    for (uint32_t r = 0; r < grid->height; r++)
    {
        for (uint32_t c = 0; c < grid->width; c++)
        {
            double x = (double)grid->left + c + 0.5;
            double y = (double)grid->top - r - 0.5;
            double nearest = 1e18;
            for (int k = 0; k < n; k++)
            {
                double dx = samples[0][k] - x;
                double dy = samples[1][k] - y;
                nearest = fmin(nearest, dx * dx + dy * dy);
            }
            bool inside = InPolygon(a, aCount, x, y) || InPolygon(b, bCount, x, y);
            int error =
                abs(Expected(sqrt(nearest), inside, grid->spread) - s_field[r * grid->width + c]);
            worst = error > worst ? error : worst;
        }
    }
    return worst;
}

static void TestTurnedUnion(void)
{
    // Turned squares whose sides cross.
    int worst = 0;
    for (int round = 0; round < 20; round++)
    {
        muiSegment sides[8];
        Turned(sides, 9.3, 9.1, 4.0, 0.1 * round);
        Turned(sides + 4, 13.7, 10.2, 3.5, 0.37 + 0.13 * round);
        muiFieldGrid grid = {0, 22, 24, 22, 4, false};
        Draw(sides, 8, &grid);
        int error = UnionError(sides, 4, sides + 4, 4, &grid);
        worst = error > worst ? error : worst;
    }
    CHECK(worst <= 1, "crossing sides are their union's edge");
}

// A comb above a square, its teeth of less than a pixel reaching into
// it, so pieces of the square's top are crossed more than once.
static int Comb(muiSegment* out, double shift)
{
    double x[40];
    double y[40];
    int n = 0;
    x[n] = 13.0;
    y[n++] = 13.0;
    x[n] = 3.0;
    y[n++] = 13.0;
    x[n] = 3.0;
    y[n++] = 11.0;
    for (int k = 0; k < 6; k++)
    {
        double left = 3.6 + 1.55 * k + shift;
        double width = 0.25 + 0.15 * (k % 4);
        double slant = 0.04 * (k % 3);
        x[n] = left;
        y[n++] = 11.0;
        x[n] = left + slant;
        y[n++] = 9.4 - 0.1 * k;
        x[n] = left + width - slant;
        y[n++] = 9.4 - 0.1 * k;
        x[n] = left + width;
        y[n++] = 11.0;
    }
    x[n] = 13.0;
    y[n++] = 11.0;
    for (int i = 0; i < n; i++)
    {
        out[i] =
            (muiSegment){(float)x[i], (float)y[i], (float)x[(i + 1) % n], (float)y[(i + 1) % n]};
    }
    return n;
}

static void TestComb(void)
{
    // With the square's top just above a row of centers, a center under a
    // narrow tooth tells the tooth's base apart from the edge beside it.
    int worst = 0;
    for (int round = 0; round < 24; round++)
    {
        muiSegment sides[48];
        uint32_t count = Rectangle(sides, 2.0f, 2.0f, 14.0f, round % 2 != 0 ? 9.6f : 10.3f, false);
        int teeth = Comb(sides + count, 0.035 * round);
        muiFieldGrid grid = {0, 15, 16, 15, 2, false};
        Draw(sides, count + (uint32_t)teeth, &grid);
        int error = UnionError(sides, 4, sides + 4, teeth, &grid);
        worst = error > worst ? error : worst;
    }
    CHECK(worst <= 1, "teeth crossing a piece twice are their union's edge");
}

static bool InNearMiss(double x, double y)
{
    return InSquare(x, y) || (x > 9.9999 && x < 13.0 && y > 0.0 && y < 6.0);
}

static void TestNearMiss(void)
{
    // A side crossing the square's bottom a ten-thousandth of a pixel
    // before its corner, beside the next side all the way: the square's
    // side within the rectangle is not the edge.
    muiSegment segments[8];
    uint32_t count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, false);
    count += Rectangle(segments + count, 9.9999f, 0.0f, 13.0f, 6.0f, false);
    muiFieldGrid grid = {-2, 14, 18, 16, 4, false};
    Draw(segments, count, &grid);
    const muiSegment edge[8] = {{2.0f, 2.0f, 9.9999f, 2.0f},  {9.9999f, 2.0f, 9.9999f, 0.0f},
                                {9.9999f, 0.0f, 13.0f, 0.0f}, {13.0f, 0.0f, 13.0f, 6.0f},
                                {13.0f, 6.0f, 10.0f, 6.0f},   {10.0f, 6.0f, 10.0f, 10.0f},
                                {10.0f, 10.0f, 2.0f, 10.0f},  {2.0f, 10.0f, 2.0f, 2.0f}};
    CHECK(WorstError(edge, 8, &grid, InNearMiss) <= 1, "a crossing just before a corner");
}

static void TestJointCrossing(void)
{
    // A side through the joint of two pieces where, in floats, it misses
    // the end of the first by 1e-7 and the start of the second by 2e-8.
    const float ax = 0x1.02cd8cp+3f;
    const float ay = 0x1.011f7p+2f;
    const float cx = 0x1.67a6c2p+3f;
    const float cy = 0x1.cd764cp+1f;
    const float q[4] = {0x1.19f738p+3f, 0x1.c2829ep+1f, 0x1.1e107cp+3f, 0x1.13cb6ep+2f};
    muiSegment sides[7] = {
        {ax, ay, cx, cy},         {cx, cy, cx, cy + 3.0f},  {cx, cy + 3.0f, ax, ay + 3.0f},
        {ax, ay + 3.0f, ax, ay},  {q[0], q[1], q[2], q[3]}, {q[2], q[3], 7.3f, 4.17f},
        {7.3f, 4.17f, q[0], q[1]}};
    muiSegment cut[8];
    uint32_t origins[8];
    CHECK(muiCutPieces(sides, 1, cut, origins) == 4 && cut[0].x1 == 0x1.1c03dap+3f &&
              cut[0].y1 == 0x1.f50cbcp+1f,
          "the joint is where the side passes");
    muiFieldGrid grid = {4, 10, 12, 10, 4, false};
    Draw(sides, 7, &grid);
    CHECK(UnionError(sides, 4, sides + 4, 3, &grid) <= 1, "a side through a joint");
}

// Thin bars across and down a square from 3 to 3 + extent, period
// apart and half that wide: a hatch whose holes are smaller than a pixel.
static uint32_t Hatch(muiSegment* out, float extent, float period)
{
    uint32_t count = 0;
    for (float at = 3.0f; at < 3.0f + extent; at += period)
    {
        count += Rectangle(out + count, at, 3.0f, at + period * 0.5f, 3.0f + extent, false);
        count += Rectangle(out + count, 3.0f, at, 3.0f + extent, at + period * 0.5f, false);
    }
    return count;
}

// Whether every center inside a bar is at or above 128 and every other at
// or below it.
static bool IsSigned(const muiSegment* segments, uint32_t count, const muiFieldGrid* grid)
{
    bool held = true;
    for (uint32_t r = 0; r < grid->height; r++)
    {
        for (uint32_t c = 0; c < grid->width; c++)
        {
            double x = (double)grid->left + c + 0.5;
            double y = (double)grid->top - r - 0.5;
            bool inside = false;
            for (uint32_t i = 0; i < count && !inside; i += 4)
            {
                inside = InPolygon(segments + i, 4, x, y);
            }
            unsigned char value = s_field[r * grid->width + c];
            held = held && (inside ? value >= 128 : value <= 128);
        }
    }
    return held;
}

static void TestHatch(void)
{
    // More parts on the edge than the room for them, then more crossings
    // on a piece than are noted: pieces are kept whole, within bounds.
    static muiSegment segments[1024];
    muiFieldGrid grid = {0, 13, 13, 13, 2, false};
    uint32_t count = Hatch(segments, 6.0f, 0.3f);
    Draw(segments, count, &grid);
    CHECK(IsSigned(segments, count, &grid), "a hatch past the room");
    count = Hatch(segments, 2.0f, 0.05f);
    Draw(segments, count, &grid);
    CHECK(IsSigned(segments, count, &grid), "a hatch past the noted crossings");
}

// Draws the segments and compares the field with the square's from 2 to
// 10 alone.
static bool IsSquare(const muiSegment* segments, uint32_t count)
{
    muiFieldGrid grid = {-2, 14, 16, 16, 4, false};
    Draw(segments, count, &grid);
    unsigned char field[SIDE * SIDE];
    memcpy(field, s_field, sizeof field);
    muiSegment square[4];
    (void)Rectangle(square, 2.0f, 2.0f, 10.0f, 10.0f, false);
    Draw(square, 4, &grid);
    return memcmp(field, s_field, (size_t)grid.width * grid.height) == 0;
}

static bool InEll(double x, double y)
{
    return InSquare(x, y) || (x > 2.0 && x < 12.5 && y > 2.0 && y < 6.5);
}

static void TestDegenerate(void)
{
    muiSegment segments[16];
    uint32_t count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, false);
    segments[count] = (muiSegment){12.0f, 6.0f, 12.0f, 6.0f};
    segments[count + 1] = (muiSegment){6.0f, 6.0f, 6.0f, 6.0f};
    CHECK(IsSquare(segments, count + 2), "lone points are not the edge");
    segments[count] = (muiSegment){12.0f, 4.0f, 13.0f, 8.5f};
    segments[count + 1] = (muiSegment){13.0f, 8.5f, 12.0f, 4.0f};
    segments[count + 2] = (muiSegment){5.0f, 5.0f, 7.5f, 7.0f};
    segments[count + 3] = (muiSegment){7.5f, 7.0f, 5.0f, 5.0f};
    CHECK(IsSquare(segments, count + 4), "contours without area are not the edge");
    count = Rectangle(segments, 2.0f, 2.0f, 6.0f, 10.0f, false);
    count += Rectangle(segments + count, 6.0f, 2.0f, 10.0f, 10.0f, false);
    muiFieldGrid grid = {-2, 14, 16, 16, 4, false};
    Draw(segments, count, &grid);
    CHECK(
        WorstError((muiSegment[4]){{2, 2, 10, 2}, {10, 2, 10, 10}, {10, 10, 2, 10}, {2, 10, 2, 2}},
                   4, &grid, InSquare) == 0,
        "a side two squares share is within their union");
    count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, false);
    count += Rectangle(segments + count, 4.0f, 4.0f, 8.0f, 8.0f, false);
    CHECK(IsSquare(segments, count), "a contour within another, the same way round");
    count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, false);
    count += Rectangle(segments + count, 2.0f, 2.0f, 10.0f, 10.0f, false);
    CHECK(IsSquare(segments, count), "a contour twice");
    // The second starts where the first ends; the union is an L.
    count = Rectangle(segments, 2.0f, 2.0f, 10.0f, 10.0f, false);
    count += Rectangle(segments + count, 2.0f, 2.0f, 12.5f, 6.5f, false);
    grid = (muiFieldGrid){-2, 14, 18, 16, 4, false};
    Draw(segments, count, &grid);
    const muiSegment ell[6] = {{2.0f, 2.0f, 12.5f, 2.0f},   {12.5f, 2.0f, 12.5f, 6.5f},
                               {12.5f, 6.5f, 10.0f, 6.5f},  {10.0f, 6.5f, 10.0f, 10.0f},
                               {10.0f, 10.0f, 2.0f, 10.0f}, {2.0f, 10.0f, 2.0f, 2.0f}};
    CHECK(WorstError(ell, 6, &grid, InEll) == 0, "contours sharing a start");
}

static void TestPiecesJoin(void)
{
    // Awkward floats: each piece starts where the last ended, and the last
    // ends where its segment does.
    const muiSegment segments[2] = {{0.1f, 0.3f, 7.7f, 2.9f}, {7.7f, 2.9f, -3.3f, 0.3f}};
    muiSegment pieces[64];
    uint32_t origins[64];
    uint32_t n = muiCutPieces(segments, 2, pieces, origins);
    bool joined = n == muiCountPieces(segments, 2) && n < 64;
    for (uint32_t i = 1; joined && i < n; i++)
    {
        joined = pieces[i].x0 == pieces[i - 1].x1 && pieces[i].y0 == pieces[i - 1].y1;
    }
    CHECK(joined && pieces[0].x0 == 0.1f && pieces[n - 1].x1 == -3.3f && pieces[n - 1].y1 == 0.3f &&
              origins[n - 1] == 1,
          "pieces join exactly");
    uint32_t first = 0;
    while (first < n && origins[first] == 0)
    {
        first++;
    }
    CHECK(first > 0 && pieces[first - 1].x1 == 7.7f && pieces[first - 1].y1 == 2.9f,
          "the first segment ends exactly");
}

// The farthest a flattened curve's segments are from the curve, sampled.
static double CurveError(const FT_Vector* points, const char* tags, short count, int degree)
{
    FT_Outline outline = {0};
    unsigned short contours[1] = {(unsigned short)(count - 1)};
    outline.n_contours = 1;
    outline.n_points = count;
    outline.points = (FT_Vector*)points;
    outline.tags = (unsigned char*)tags;
    outline.contours = contours;
    muiSegment segments[600];
    uint32_t n = 0;
    CHECK(muiFlattenOutline(&outline, segments, 600, &n) && n < 600, "flattened");
    double worst = 0.0;
    double p[4][2];
    for (int i = 0; i <= degree; i++)
    {
        p[i][0] = points[i].x / 64.0;
        p[i][1] = points[i].y / 64.0;
    }
    for (int k = 0; k <= 1000; k++)
    {
        double t = k / 1000.0;
        double u = 1.0 - t;
        double x = 0.0;
        double y = 0.0;
        if (degree == 2)
        {
            x = u * u * p[0][0] + 2 * u * t * p[1][0] + t * t * p[2][0];
            y = u * u * p[0][1] + 2 * u * t * p[1][1] + t * t * p[2][1];
        }
        else
        {
            x = u * u * u * p[0][0] + 3 * u * u * t * p[1][0] + 3 * u * t * t * p[2][0] +
                t * t * t * p[3][0];
            y = u * u * u * p[0][1] + 3 * u * u * t * p[1][1] + 3 * u * t * t * p[2][1] +
                t * t * t * p[3][1];
        }
        double nearest = 1e9;
        for (uint32_t i = 0; i < n; i++)
        {
            nearest = fmin(nearest, SegmentDistance(&segments[i], x, y));
        }
        worst = fmax(worst, nearest);
    }
    return worst;
}

static void TestFlatten(void)
{
    // A quadratic and a cubic arch, closed by a line back to the start.
    const FT_Vector quadratic[3] = {{0, 0}, {40 * 64, 60 * 64}, {80 * 64, 0}};
    const char quadraticTags[3] = {FT_CURVE_TAG_ON, FT_CURVE_TAG_CONIC, FT_CURVE_TAG_ON};
    CHECK(CurveError(quadratic, quadraticTags, 3, 2) <= (double)MUI_FLATTEN_TOLERANCE,
          "a quadratic within tolerance");
    const FT_Vector cubic[4] = {{0, 0}, {10 * 64, 90 * 64}, {70 * 64, -50 * 64}, {80 * 64, 0}};
    const char cubicTags[4] = {FT_CURVE_TAG_ON, FT_CURVE_TAG_CUBIC, FT_CURVE_TAG_CUBIC,
                               FT_CURVE_TAG_ON};
    CHECK(CurveError(cubic, cubicTags, 4, 3) <= (double)MUI_FLATTEN_TOLERANCE,
          "a cubic within tolerance");
    // Straight, then bent at its end: the larger second difference rules.
    const FT_Vector bent[4] = {{0, 0}, {10 * 64, 0}, {20 * 64, 0}, {30 * 64, 100 * 64}};
    CHECK(CurveError(bent, cubicTags, 4, 3) <= (double)MUI_FLATTEN_TOLERANCE,
          "a cubic bent at its end within tolerance");
    // Counting first gives the count filling gives.
    FT_Outline outline = {0};
    unsigned short contours[1] = {3};
    outline.n_contours = 1;
    outline.n_points = 4;
    outline.points = (FT_Vector*)cubic;
    outline.tags = (unsigned char*)cubicTags;
    outline.contours = contours;
    uint32_t counted = 0;
    uint32_t filled = 0;
    muiSegment segments[600];
    CHECK(muiFlattenOutline(&outline, NULL, 0, &counted) &&
              muiFlattenOutline(&outline, segments, 600, &filled) && counted == filled &&
              segments[filled - 1].x1 == 0.0f && segments[filled - 1].y1 == 0.0f,
          "counted as filled, and closed");
}

int main(void)
{
    TestSquare();
    TestUnion();
    TestHole();
    TestPolygons();
    TestNotches();
    TestFractionalUnion();
    TestTurnedUnion();
    TestComb();
    TestNearMiss();
    TestJointCrossing();
    TestHatch();
    TestDegenerate();
    TestPiecesJoin();
    TestFlatten();
    return s_failures == 0 ? 0 : 1;
}
