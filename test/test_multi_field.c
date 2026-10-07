// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Multi-channel distance fields of segments: sampled as a renderer
// samples them, each channel bilinearly and then their median, a square's
// convex corners and an L's concave one stay where they are, where the
// one-channel field (alpha) rounds them; so do the corners of a plus of
// two crossing bars, a teardrop's one corner and a thin spike's tip; a
// smooth contour, one curve, is white; an open edge reads its line past
// its ends; the median's sign agrees with the inside for a union of
// overlapping squares; and alpha is the one-channel field byte for byte.

#include "distance_field.h"
#include "multi_field.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SIDE = 32,
    SPREAD = 4,
    MAX_SEGMENTS = 64
};

static unsigned char s_multi[SIDE * SIDE * 4];
static unsigned char s_single[SIDE * SIDE];

static const muiFieldGrid s_grid = {0, SIDE, SIDE, SIDE, SPREAD, false};

// Draws the multi-channel field of segments into s_multi and the
// one-channel field into s_single; curves gives each segment's curve.
static void Draw(const muiSegment* segments, const uint32_t* curves, uint32_t count)
{
    size_t pieces = muiCountPieces(segments, count);
    muiSegment* cut = malloc(pieces * sizeof(muiSegment));
    uint32_t* origins = malloc(pieces * sizeof(uint32_t));
    uint32_t n = muiCutPieces(segments, count, cut, origins);
    size_t crossings = muiCountCrossings(cut, n, &s_grid);
    size_t area = (size_t)SIDE * SIDE;
    size_t room = muiEdgeRoom(n);
    muiFieldScratch scratch = {
        .rowStarts = malloc(((size_t)SIDE + 1) * sizeof(uint32_t)),
        .crossings = malloc((crossings + 1) * sizeof(muiCrossing)),
        .cellStarts = malloc((area + 1) * sizeof(uint32_t)),
        .cellPieces = malloc((pieces + 1) * sizeof(uint32_t)),
        .edge = malloc(room * sizeof(muiSegment)),
        .edgeOrigins = malloc(room * sizeof(uint32_t)),
        .edgeSides = malloc(room),
        .distances = malloc(area * sizeof(float)),
    };
    uint32_t* loops = malloc((room * 3 + 1) * sizeof(uint32_t));
    const muiMultiScratch multi = {
        curves,           malloc(room),         loops,
        loops + room + 1, loops + room * 2 + 1, malloc(area * 9 * sizeof(float)),
        malloc(area)};
    muiDrawMultiField(cut, origins, n, &s_grid, &scratch, &multi, s_multi);
    muiFieldScratch single = scratch;
    single.edgeOrigins = NULL;
    single.edgeSides = NULL;
    muiDrawDistanceField(cut, origins, n, &s_grid, &single, s_single);
    free(multi.inside);
    free(multi.channels);
    free(multi.edgeColors);
    free(loops);
    free(scratch.distances);
    free(scratch.edgeSides);
    free(scratch.edgeOrigins);
    free(scratch.edge);
    free(scratch.cellPieces);
    free(scratch.cellStarts);
    free(scratch.crossings);
    free(scratch.rowStarts);
    free(origins);
    free(cut);
}

// A channel's byte at a pixel, as a distance in pixels, positive inside.
static double At(int column, int row, int channel)
{
    column = column < 0 ? 0 : column >= SIDE ? SIDE - 1 : column;
    row = row < 0 ? 0 : row >= SIDE ? SIDE - 1 : row;
    int byte = s_multi[((size_t)row * SIDE + (size_t)column) * 4 + (size_t)channel];
    return (byte - 128) * (double)SPREAD / 128.0;
}

// A channel bilinearly at a point, y up, as a texture sampler takes it
// between pixel centers.
static double Sample(double x, double y, int channel)
{
    double u = x - 0.5;
    double v = (double)SIDE - y - 0.5;
    int c = (int)floor(u);
    int r = (int)floor(v);
    double fu = u - c;
    double fv = v - r;
    return (1 - fu) * (1 - fv) * At(c, r, channel) + fu * (1 - fv) * At(c + 1, r, channel) +
           (1 - fu) * fv * At(c, r + 1, channel) + fu * fv * At(c + 1, r + 1, channel);
}

static double Median(double a, double b, double c)
{
    return fmax(fmin(a, b), fmin(fmax(a, b), c));
}

// The distance a renderer reads: the median of the channels, or alpha.
static double Read(double x, double y, bool multi)
{
    return multi ? Median(Sample(x, y, 0), Sample(x, y, 1), Sample(x, y, 2)) : Sample(x, y, 3);
}

// Where the read distance crosses 0 along a line from (x, y) by (dx, dy)
// for u from -1 to 1, inside at -1: u.
static double Crossing(double x, double y, double dx, double dy, bool multi)
{
    double low = -1.0;
    double high = 1.0;
    for (int i = 0; i < 40; i++)
    {
        double middle = (low + high) * 0.5;
        if (Read(x + middle * dx, y + middle * dy, multi) > 0.0)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return (low + high) * 0.5;
}

// Whether every corner is kept: for each, given as x, y and a direction
// out of the shape, the median crosses 0 along that direction within a
// twentieth of its length.
static bool CornersKept(const float* corners, uint32_t count)
{
    bool kept = true;
    for (uint32_t i = 0; i < count; i++)
    {
        const float* c = &corners[4 * i];
        kept = kept &&
               fabs(Crossing((double)c[0], (double)c[1], (double)c[2], (double)c[3], true)) < 0.05;
    }
    return kept;
}

static uint32_t Polygon(muiSegment* out, uint32_t* curves, const float* points, uint32_t count,
                        bool oneCurve)
{
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t j = (i + 1) % count;
        out[i] = (muiSegment){points[2 * i], points[2 * i + 1], points[2 * j], points[2 * j + 1]};
        curves[i] = oneCurve ? 0 : i;
    }
    return count;
}

// A square from 8 to 24, counterclockwise: its corner at 24, 24 along the
// diagonal, read from both fields.
static void TestConvexCorner(void)
{
    muiSegment segments[4];
    uint32_t curves[4];
    const float square[8] = {8, 8, 24, 8, 24, 24, 8, 24};
    Draw(segments, curves, Polygon(segments, curves, square, 4, false));
    double multi = Crossing(24.0, 24.0, 1.0, 1.0, true);
    double single = Crossing(24.0, 24.0, 1.0, 1.0, false);
    CHECK(fabs(multi) < 0.05, "the median keeps the convex corner");
    // The others too, the one where the loop closes among them.
    const float corners[16] = {8, 8, -1, -1, 24, 8, 1, -1, 24, 24, 1, 1, 8, 24, -1, 1};
    CHECK(CornersKept(corners, 4), "every corner");
    CHECK(single < -0.15, "the one-channel field rounds it inward");
    // Along a side, both find the edge.
    CHECK(fabs(Crossing(24.0, 16.0, 1.0, 0.0, true)) < 0.05 &&
              fabs(Crossing(24.0, 16.0, 1.0, 0.0, false)) < 0.05,
          "a side, in both");
}

// An L: its concave corner at 16, 16, read along the diagonal into the
// notch.
static void TestConcaveCorner(void)
{
    muiSegment segments[6];
    uint32_t curves[6];
    const float shape[12] = {6, 6, 26, 6, 26, 16, 16, 16, 16, 26, 6, 26};
    Draw(segments, curves, Polygon(segments, curves, shape, 6, false));
    double multi = Crossing(16.0, 16.0, 1.0, 1.0, true);
    double single = Crossing(16.0, 16.0, 1.0, 1.0, false);
    CHECK(fabs(multi) < 0.05, "the median keeps the concave corner");
    CHECK(single > 0.15, "the one-channel field fills it in");
}

// A sixteen-sided polygon of one curve has no corner: every channel the
// same, the alpha's.
static void TestSmooth(void)
{
    muiSegment segments[16];
    uint32_t curves[16];
    float points[32];
    for (int i = 0; i < 16; i++)
    {
        double angle = i * 3.14159265358979 / 8.0;
        points[2 * i] = (float)(16.0 + 9.0 * cos(angle));
        points[2 * i + 1] = (float)(16.0 + 9.0 * sin(angle));
    }
    Draw(segments, curves, Polygon(segments, curves, points, 16, true));
    bool white = true;
    for (size_t at = 0; at < (size_t)SIDE * SIDE; at++)
    {
        const unsigned char* p = &s_multi[at * 4];
        white = white && p[0] == p[1] && p[1] == p[2] && abs(p[0] - p[3]) <= 1;
    }
    CHECK(white, "one curve, white: every channel the true distance");
    // The same points as separate lines turn by 22.5 degrees at each:
    // corners, so the channels differ somewhere.
    Draw(segments, curves, Polygon(segments, curves, points, 16, false));
    bool differ = false;
    for (size_t at = 0; at < (size_t)SIDE * SIDE; at++)
    {
        differ = differ || s_multi[at * 4] != s_multi[at * 4 + 1];
    }
    CHECK(differ, "lines turning past the threshold, coloured");
}

// Two overlapping squares: their union's median sign is the inside's,
// alpha is the one-channel field, and the corners where their sides
// cross are kept.
static void TestUnion(void)
{
    muiSegment segments[8];
    uint32_t curves[8];
    const float a[8] = {6, 6, 20, 6, 20, 20, 6, 20};
    const float b[8] = {12, 12, 26, 12, 26, 26, 12, 26};
    (void)Polygon(segments, curves, a, 4, false);
    (void)Polygon(segments + 4, curves + 4, b, 4, false);
    for (int i = 4; i < 8; i++)
    {
        curves[i] += 4;
    }
    Draw(segments, curves, 8);
    bool agrees = true;
    bool alpha = true;
    for (size_t at = 0; at < (size_t)SIDE * SIDE; at++)
    {
        const unsigned char* p = &s_multi[at * 4];
        int median = p[0] + p[1] + p[2] -
                     (p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2])) -
                     (p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]));
        agrees = agrees && (abs(p[3] - 128) <= 1 || (median >= 128) == (p[3] >= 128));
        alpha = alpha && p[3] == s_single[at];
    }
    CHECK(agrees, "the median's sign, the union's inside");
    CHECK(alpha, "alpha, the one-channel field");
    // The corners where their sides cross: the chains of the union's edge
    // meeting at 12, 20 would both start cyan if coloured one by one.
    CHECK(fabs(Crossing(20.0, 12.0, 1.0, -1.0, true)) < 0.05 &&
              fabs(Crossing(12.0, 20.0, -1.0, 1.0, true)) < 0.05,
          "the crossings' corners kept");
    CHECK(Crossing(12.0, 20.0, -1.0, 1.0, false) > 0.15, "the one-channel field fills them in");
}

// A plus of two bars crossing: the union's edge leaves one bar for the
// other at four places, two of them sharing each x, and its twelve
// corners are kept.
static void TestPlus(void)
{
    muiSegment segments[8];
    uint32_t curves[8];
    const float across[8] = {4, 12, 28, 12, 28, 20, 4, 20};
    const float up[8] = {12, 4, 20, 4, 20, 28, 12, 28};
    (void)Polygon(segments, curves, across, 4, false);
    (void)Polygon(segments + 4, curves + 4, up, 4, false);
    for (int i = 4; i < 8; i++)
    {
        curves[i] += 4;
    }
    Draw(segments, curves, 8);
    // Convex corners out away from the middle, concave ones toward it.
    const float corners[48] = {4,  12, -1, -1, 12, 12, -1, -1, 12, 4,  -1, -1, 20, 4,  1,  -1,
                               20, 12, 1,  -1, 28, 12, 1,  -1, 28, 20, 1,  1,  20, 20, 1,  1,
                               20, 28, 1,  1,  12, 28, -1, 1,  12, 20, -1, 1,  4,  20, -1, 1};
    CHECK(CornersKept(corners, 12), "every corner of the plus");
}

// A teardrop: an arc of two curves meeting smoothly and one corner, at
// the apex, which is kept.
static void TestTeardrop(void)
{
    enum
    {
        // Fine enough that no joint of the arc turns past the threshold.
        ARC = 48
    };
    muiSegment segments[ARC + 2];
    uint32_t curves[ARC + 2];
    // The tangent points from the apex, 27, 16, to the circle.
    double reach = asin(7.0 / 13.0);
    double from = reach + 3.14159265358979 / 2.0;
    float points[2 * (ARC + 2)];
    points[0] = 27.0f;
    points[1] = 16.0f;
    for (int i = 0; i <= ARC; i++)
    {
        double angle = -from + (2.0 * from) * i / ARC;
        // Around the far side of the circle, from one tangent to the other.
        double turn = 3.14159265358979 + angle;
        points[2 * (i + 1)] = (float)(14.0 + 7.0 * cos(turn));
        points[2 * (i + 1) + 1] = (float)(16.0 - 7.0 * sin(turn));
    }
    uint32_t n = Polygon(segments, curves, points, ARC + 2, true);
    // The first side and the arc's first half one curve, the arc's second
    // half and the last side another: they meet smoothly halfway.
    for (uint32_t i = 0; i < n; i++)
    {
        curves[i] = i <= ARC / 2 ? 0 : 1;
    }
    Draw(segments, curves, n);
    const float apex[4] = {27, 16, 1, 0};
    CHECK(CornersKept(apex, 1), "the teardrop's corner");
}

// A spike whose tip turns by more than 171.9 degrees, with no turn's
// sine past the threshold: a corner all the same.
static void TestSpike(void)
{
    muiSegment segments[3];
    uint32_t curves[3];
    const float spike[6] = {4.0f, 14.8f, 28.0f, 16.0f, 4.0f, 17.2f};
    Draw(segments, curves, Polygon(segments, curves, spike, 3, false));
    const float tip[4] = {28, 16, 1, 0};
    CHECK(CornersKept(tip, 1), "the spike's tip");
}

// A lone segment upward along x = 16 from 8 to 24: inside on its right,
// the edge open. Past either end its channels read its line.
static void TestOpenEdge(void)
{
    const muiSegment segment = {16.0f, 8.0f, 16.0f, 24.0f};
    const uint32_t curve = 0;
    Draw(&segment, &curve, 1);
    // Pixels 15.5, 5.5 and 15.5, 26.5: half a pixel left of the line.
    CHECK(s_multi[((size_t)26 * SIDE + 15) * 4 + 1] == 112 &&
              s_multi[((size_t)5 * SIDE + 15) * 4 + 1] == 112,
          "past both ends, the line");
}

int main(void)
{
    TestConvexCorner();
    TestConcaveCorner();
    TestSmooth();
    TestUnion();
    TestPlus();
    TestTeardrop();
    TestSpike();
    TestOpenEdge();
    return s_failures == 0 ? 0 : 1;
}
