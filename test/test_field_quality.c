// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Distance fields measured against the shape (record mui-0006): Liberation
// Sans's printable ASCII glyphs (glyphs 3 to 97 in its order) from fields
// of an em of 32, 64 and 128, spread 4 as the renderer's, drawn at 1.5, 3
// and 6
// times the field's em as the reference renderer draws them, replicated
// here in double precision: each channel bilinearly between texel
// centers, the median or alpha made a distance in device pixels, and the
// coverage half a pixel either side of it, msdfgen's expression. Each is
// compared pixel for pixel with muiRenderGlyph's exact area coverage at
// the drawn em and the same pen. The median is held no worse than alpha
// in mean error and in pixels off by more than a quarter, and both within
// bounds just above what they measure (a mean of 0.036 and 0.46% of the
// pixels). Run with --report for the numbers.

#include "test_harness.h"

#include "maul-ui/font.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/text_block.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "liberation_sans.inc"

enum
{
    FIRST_GLYPH = 3,
    // The spread the reference renderer draws fields with.
    SPREAD = 4,
    LAST_GLYPH = 97,
    // Bytes enough for a glyph at the largest drawn em, 768 pixels.
    CAPACITY = 1 << 21
};

static unsigned char s_reference[CAPACITY];
static unsigned char s_field[CAPACITY];

// What a kind of field drew against the shape.
typedef struct Errors
{
    double sum;
    double largest;
    uint64_t pixels;
    uint64_t far;
} Errors;

// A field's image and how it is read.
typedef struct Field
{
    muiGlyphImage image;
    uint32_t spread;
    // Device pixels a field pixel spans.
    double magnification;
} Field;

// A texel's alpha as a distance in field pixels; texels past the image
// read as far outside.
static double Alpha(const Field* field, int64_t column, int64_t row)
{
    if (column < 0 || row < 0 || column >= (int64_t)field->image.width ||
        row >= (int64_t)field->image.height)
    {
        return -(double)field->spread;
    }
    const unsigned char* p = &s_field[((size_t)row * field->image.width + (size_t)column) * 4];
    return (p[3] - 128.0) * (double)field->spread / 128.0;
}

// A field's distance in field pixels at a point of its image, in texels
// from its top left: each channel bilinear between texel centers, then
// the median of the three or alpha, as the renderer samples them.
static double Sample(const Field* field, double u, double v, bool median)
{
    double x = u - 0.5;
    double y = v - 0.5;
    double cx = floor(x);
    double cy = floor(y);
    double fx = x - cx;
    double fy = y - cy;
    int64_t c = (int64_t)cx;
    int64_t r = (int64_t)cy;
    if (!median)
    {
        return (1 - fx) * (1 - fy) * Alpha(field, c, r) + fx * (1 - fy) * Alpha(field, c + 1, r) +
               (1 - fx) * fy * Alpha(field, c, r + 1) + fx * fy * Alpha(field, c + 1, r + 1);
    }
    double channel[3];
    for (int k = 0; k < 3; k++)
    {
        double t[4];
        for (int i = 0; i < 4; i++)
        {
            int64_t tc = c + i % 2;
            int64_t tr = r + i / 2;
            bool inside = tc >= 0 && tr >= 0 && tc < (int64_t)field->image.width &&
                          tr < (int64_t)field->image.height;
            t[i] = inside ? s_field[((size_t)tr * field->image.width + (size_t)tc) * 4 + (size_t)k]
                          : 0.0;
        }
        channel[k] = (1 - fx) * (1 - fy) * t[0] + fx * (1 - fy) * t[1] + (1 - fx) * fy * t[2] +
                     fx * fy * t[3];
    }
    double value =
        fmax(fmin(channel[0], channel[1]), fmin(fmax(channel[0], channel[1]), channel[2]));
    return (value - 128.0) * (double)field->spread / 128.0;
}

// The reference's coverage of a device pixel, 0 past its image.
static double Reference(const muiGlyphImage* image, int64_t x, int64_t y)
{
    int64_t column = x - image->left;
    int64_t row = image->top - 1 - y;
    if (column < 0 || row < 0 || column >= (int64_t)image->width || row >= (int64_t)image->height)
    {
        return 0.0;
    }
    return s_reference[(size_t)row * image->width + (size_t)column] / 255.0;
}

// Draws a field over its extent in device pixels, from the median or
// alpha, and adds its errors against the reference.
static void Compare(const Field* field, const muiGlyphImage* reference, bool median, Errors* errors)
{
    double m = field->magnification;
    // Device pixels (x right, y up; pixel x, y spans x to x + 1 and y to
    // y + 1) the field's image covers.
    int64_t x0 = (int64_t)floor((double)field->image.left * m);
    int64_t x1 = (int64_t)ceil((double)(field->image.left + (int32_t)field->image.width) * m);
    int64_t y0 = (int64_t)floor((double)(field->image.top - (int32_t)field->image.height) * m);
    int64_t y1 = (int64_t)ceil((double)field->image.top * m);
    for (int64_t y = y0; y < y1; y++)
    {
        for (int64_t x = x0; x < x1; x++)
        {
            double u = ((double)x + 0.5) / m - field->image.left;
            double v = field->image.top - ((double)y + 0.5) / m;
            double distance = Sample(field, u, v, median) * m;
            double drawn = fmin(fmax(0.5 + distance, 0.0), 1.0);
            double exact = Reference(reference, x, y);
            if ((drawn > 0.0 && drawn < 1.0) || (exact > 0.0 && exact < 1.0))
            {
                double error = fabs(drawn - exact);
                errors->sum += error;
                errors->largest = fmax(errors->largest, error);
                errors->pixels++;
                errors->far += error > 0.25 ? 1u : 0u;
            }
        }
    }
}

int main(int argc, char** argv)
{
    bool report = argc > 1 && strcmp(argv[1], "--report") == 0;
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&serviceDef, &service) == mui_success, "a text service");
    muiFontDef def = muiDefaultFontDef();
    def.data = s_liberationSans;
    def.size = sizeof s_liberationSans;
    def.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    CHECK(muiCreateFont(service, &def, &font) == mui_success, "Liberation Sans");
    uint64_t key = muiFont_GetKey(font);
    const float ems[3] = {32.0f, 64.0f, 128.0f};
    const float magnifications[3] = {1.5f, 3.0f, 6.0f};
    Errors total[2] = {{0.0, 0.0, 0, 0}, {0.0, 0.0, 0, 0}};
    for (int e = 0; e < 3; e++)
    {
        for (int k = 0; k < 3; k++)
        {
            Errors errors[2] = {{0.0, 0.0, 0, 0}, {0.0, 0.0, 0, 0}};
            Field field = {.spread = SPREAD, .magnification = (double)magnifications[k]};
            for (uint32_t glyph = FIRST_GLYPH; glyph <= LAST_GLYPH; glyph++)
            {
                muiGlyphImage reference = {0};
                bool rendered =
                    muiRenderGlyphMultiField(service, key, glyph, ems[e], field.spread,
                                             &field.image, s_field, CAPACITY) == mui_success &&
                    muiRenderGlyph(service, key, glyph, ems[e] * magnifications[k], 0.0f,
                                   &reference, s_reference, CAPACITY) == mui_success;
                CHECK(rendered, "a glyph's field and coverage");
                if (!rendered || field.image.width == 0)
                {
                    continue;
                }
                Compare(&field, &reference, true, &errors[0]);
                Compare(&field, &reference, false, &errors[1]);
            }
            for (int kind = 0; kind < 2; kind++)
            {
                total[kind].sum += errors[kind].sum;
                total[kind].pixels += errors[kind].pixels;
                total[kind].far += errors[kind].far;
                total[kind].largest = fmax(total[kind].largest, errors[kind].largest);
                if (report)
                {
                    printf("em %3.0f x%.1f %-6s mean %.4f largest %.3f off by a quarter %6.3f%%"
                           " of %llu\n",
                           (double)ems[e], (double)magnifications[k],
                           kind == 0 ? "median" : "alpha",
                           errors[kind].sum / (double)errors[kind].pixels, errors[kind].largest,
                           100.0 * (double)errors[kind].far / (double)errors[kind].pixels,
                           (unsigned long long)errors[kind].pixels);
                }
            }
        }
    }
    double mean[2] = {total[0].sum / (double)total[0].pixels,
                      total[1].sum / (double)total[1].pixels};
    double far[2] = {(double)total[0].far / (double)total[0].pixels,
                     (double)total[1].far / (double)total[1].pixels};
    if (report)
    {
        printf("all: median mean %.4f largest %.3f far %.3f%%; alpha mean %.4f largest %.3f far "
               "%.3f%%\n",
               mean[0], total[0].largest, 100.0 * far[0], mean[1], total[1].largest,
               100.0 * far[1]);
    }
    CHECK(mean[0] <= mean[1] && far[0] <= far[1], "the median no worse than alpha");
    CHECK(mean[0] < 0.04 && far[0] < 0.006, "the median within its bounds");
    muiDestroyTextService(service);
    return s_failures == 0 ? 0 : 1;
}
