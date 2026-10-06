// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph images (record mui-0006): Ahem's boxes give exact coverage at
// whole and fractional sizes and offsets and exact distance fields,
// Liberation Sans renders the same bytes on every platform, overlapping
// contours make one shape, and calls outside the contract are refused.

#include "test_harness.h"

#include "maul-ui/font.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/text_block.h"

#include <math.h>
#include <string.h>

#include "ahem.inc"
#include "large_glyph.inc"
#include "liberation_sans.inc"
#include "overlap.inc"

enum
{
    // Ahem's a: the em square, 0.8 above the baseline and 0.2 below.
    AHEM_BOX = 67,
    AHEM_SPACE = 3,
    LIBERATION_A = 36
};

static unsigned char s_pixels[1 << 20];

typedef struct Fonts
{
    muiTextService* service;
    muiFontId ahem;
    muiFontId liberation;
} Fonts;

static Fonts MakeFonts(void)
{
    Fonts fonts = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    CHECK(muiCreateTextService(&def, &fonts.service) == mui_success, "service");
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    font.dataMode = mui_fontDataBorrow;
    CHECK(muiCreateFont(fonts.service, &font, &fonts.ahem) == mui_success, "Ahem");
    font.data = s_liberationSans;
    font.size = sizeof s_liberationSans;
    CHECK(muiCreateFont(fonts.service, &font, &fonts.liberation) == mui_success, "Liberation");
    return fonts;
}

static muiGlyphImage Render(const Fonts* fonts, muiFontId font, uint32_t glyph, float size,
                            float offset)
{
    muiGlyphImage image = {-1, -1, 0, 0};
    CHECK(muiRenderGlyph(fonts->service, muiFont_GetKey(font), glyph, size, offset, &image,
                         s_pixels, sizeof s_pixels) == mui_success,
          "rendered");
    return image;
}

static bool SameImage(muiGlyphImage image, int32_t left, int32_t top, uint32_t width,
                      uint32_t height)
{
    return image.left == left && image.top == top && image.width == width && image.height == height;
}

static uint8_t At(muiGlyphImage image, uint32_t x, uint32_t y)
{
    return s_pixels[y * image.width + x];
}

static uint32_t Hash(muiGlyphImage image)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < (size_t)image.width * image.height; i++)
    {
        hash = (hash ^ s_pixels[i]) * 16777619u;
    }
    return hash;
}

static void TestBoxes(void)
{
    Fonts fonts = MakeFonts();
    muiGlyphImage image = Render(&fonts, fonts.ahem, AHEM_BOX, 10.0f, 0.0f);
    bool full = SameImage(image, 0, 8, 10, 10);
    for (uint32_t i = 0; full && i < 100; i++)
    {
        full = s_pixels[i] == 255;
    }
    CHECK(full, "the em square, 8 up and 2 down, all covered");
    // Half a pixel right: the first and last columns are half covered.
    image = Render(&fonts, fonts.ahem, AHEM_BOX, 10.0f, 0.5f);
    CHECK(SameImage(image, 0, 8, 11, 10) && At(image, 0, 5) == 128 && At(image, 1, 5) == 255 &&
              At(image, 10, 9) == 128,
          "half a pixel right");
    // 12.5 pixels: 10 up, 2.5 down, 12.5 wide.
    image = Render(&fonts, fonts.ahem, AHEM_BOX, 12.5f, 0.0f);
    CHECK(SameImage(image, 0, 10, 13, 13) && At(image, 0, 0) == 255 && At(image, 12, 0) == 128 &&
              At(image, 0, 12) == 128 && At(image, 12, 12) == 64,
          "a fractional size");
    // The face is set to each size in turn.
    image = Render(&fonts, fonts.ahem, AHEM_BOX, 10.0f, 0.0f);
    CHECK(SameImage(image, 0, 8, 10, 10) && At(image, 9, 9) == 255, "back to 10");
    image = Render(&fonts, fonts.ahem, AHEM_SPACE, 10.0f, 0.0f);
    CHECK(SameImage(image, 0, 0, 0, 0), "a space is empty");
    muiDestroyTextService(fonts.service);
}

static void TestSameEverywhere(void)
{
    Fonts fonts = MakeFonts();
    muiGlyphImage image = Render(&fonts, fonts.liberation, LIBERATION_A, 13.5f, 0.0f);
    CHECK(SameImage(image, 0, 10, 9, 10) && Hash(image) == 0x863ce274u, "A at 13.5");
    image = Render(&fonts, fonts.liberation, LIBERATION_A, 13.5f, 0.25f);
    CHECK(SameImage(image, 0, 10, 10, 10) && Hash(image) == 0xb08c8004u, "A a quarter right");
    // Large: the box is the outline's, so its top and bottom rows are
    // touched, and the strokes' insides are covered.
    image = Render(&fonts, fonts.liberation, LIBERATION_A, 400.0f, 0.75f);
    uint8_t top = 0;
    uint8_t bottom = 0;
    for (uint32_t x = 0; x < image.width; x++)
    {
        top = At(image, x, 0) > top ? At(image, x, 0) : top;
        bottom = At(image, x, image.height - 1) > bottom ? At(image, x, image.height - 1) : bottom;
    }
    CHECK(image.width > 200 && image.height > 250 && top > 0 && bottom == 255,
          "A large, filling its box");
    muiDestroyTextService(fonts.service);
}

static void TestContract(void)
{
    Fonts fonts = MakeFonts();
    uint64_t ahem = muiFont_GetKey(fonts.ahem);
    muiGlyphImage image = {0, 0, 0, 0};
    // The size first, then the bytes.
    CHECK(muiRenderGlyph(fonts.service, ahem, AHEM_BOX, 10.0f, 0.0f, &image, NULL, 0) ==
                  mui_errorCapacity &&
              SameImage(image, 0, 8, 10, 10),
          "too few bytes, the size told");
    CHECK(muiRenderGlyph(fonts.service, ahem, AHEM_BOX, 10.0f, 0.0f, &image, s_pixels, 99) ==
              mui_errorCapacity,
          "one byte short");
    CHECK(muiRenderGlyph(fonts.service, ahem, AHEM_SPACE, 10.0f, 0.0f, &image, NULL, 0) ==
              mui_success,
          "an empty image needs no bytes");
    const float sizes[] = {0.0f, 1.0f / 128.0f, -1.0f, NAN, INFINITY, 4097.0f};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
        CHECK(muiRenderGlyph(fonts.service, ahem, AHEM_BOX, sizes[i], 0.0f, &image, s_pixels,
                             sizeof s_pixels) == mui_errorInvalid,
              "a size refused");
    }
    CHECK(muiRenderGlyph(fonts.service, ahem, AHEM_BOX, 1.0f / 64.0f, 0.0f, &image, s_pixels,
                         sizeof s_pixels) == mui_success &&
              muiRenderGlyph(fonts.service, ahem, AHEM_BOX, MUI_MAX_GLYPH_PIXEL_SIZE, 0.0f, &image,
                             NULL, 0) == mui_errorCapacity &&
              image.width == 4096,
          "the smallest and largest sizes");
    const float offsets[] = {-0.01f, 1.0f, NAN};
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; i++)
    {
        CHECK(muiRenderGlyph(fonts.service, ahem, AHEM_BOX, 10.0f, offsets[i], &image, s_pixels,
                             sizeof s_pixels) == mui_errorInvalid,
              "an offset refused");
    }
    muiFontMetrics metrics;
    CHECK(muiFont_GetMetrics(fonts.service, fonts.ahem, &metrics) == mui_success &&
              muiRenderGlyph(fonts.service, ahem, metrics.glyphCount - 1, 10.0f, 0.0f, &image,
                             s_pixels, sizeof s_pixels) == mui_success &&
              muiRenderGlyph(fonts.service, ahem, metrics.glyphCount, 10.0f, 0.0f, &image, s_pixels,
                             sizeof s_pixels) == mui_errorInvalid,
          "the last glyph id and one past it");
    CHECK(muiRenderGlyph(NULL, ahem, AHEM_BOX, 10.0f, 0.0f, &image, s_pixels, sizeof s_pixels) ==
                  mui_errorInvalid &&
              muiRenderGlyph(fonts.service, ahem, AHEM_BOX, 10.0f, 0.0f, NULL, s_pixels,
                             sizeof s_pixels) == mui_errorInvalid &&
              muiRenderGlyph(fonts.service, ahem, AHEM_BOX, 10.0f, 0.0f, &image, NULL, 1) ==
                  mui_errorInvalid,
          "NULL arguments");
    // Key 0 names the default font, and a gone font's key is stale.
    CHECK(muiRenderGlyph(fonts.service, 0, AHEM_BOX, 10.0f, 0.0f, &image, s_pixels,
                         sizeof s_pixels) == mui_errorStale,
          "no default font");
    CHECK(muiSetDefaultFont(fonts.service, fonts.ahem) == mui_success &&
              muiRenderGlyph(fonts.service, 0, AHEM_BOX, 10.0f, 0.0f, &image, s_pixels,
                             sizeof s_pixels) == mui_success &&
              SameImage(image, 0, 8, 10, 10),
          "the default font");
    CHECK(muiSetDefaultFont(fonts.service, (muiFontId){0, 0}) == mui_success &&
              muiDestroyFont(fonts.service, fonts.ahem) == mui_success &&
              muiRenderGlyph(fonts.service, ahem, AHEM_BOX, 10.0f, 0.0f, &image, s_pixels,
                             sizeof s_pixels) == mui_errorStale,
          "a gone font");
    muiDestroyTextService(fonts.service);
}

// A 16-em box in a font of 16 units per em: 1,600 pixels a side at 100,
// and too large for the rasterizer at the largest size.
static void TestTooLarge(void)
{
    Fonts fonts = MakeFonts();
    muiFontDef def = muiDefaultFontDef();
    def.data = s_largeGlyph;
    def.size = sizeof s_largeGlyph;
    def.dataMode = mui_fontDataBorrow;
    muiFontId large = {0, 0};
    CHECK(muiCreateFont(fonts.service, &def, &large) == mui_success, "the large font");
    muiGlyphImage image = {0, 0, 0, 0};
    CHECK(muiRenderGlyph(fonts.service, muiFont_GetKey(large), 1, 100.0f, 0.0f, &image, NULL, 0) ==
                  mui_errorCapacity &&
              SameImage(image, 0, 1600, 1600, 1600),
          "16 ems at 100 pixels");
    image = (muiGlyphImage){0, 0, 0, 0};
    CHECK(muiRenderGlyph(fonts.service, muiFont_GetKey(large), 1, MUI_MAX_GLYPH_PIXEL_SIZE, 0.0f,
                         &image, NULL, 0) == mui_errorFormat &&
              SameImage(image, 0, 0, 0, 0),
          "65,536 pixels a side refused");
    muiDestroyTextService(fonts.service);
}

static muiGlyphImage RenderField(const Fonts* fonts, muiFontId font, uint32_t glyph, float size,
                                 uint32_t spread)
{
    muiGlyphImage image = {-1, -1, 0, 0};
    CHECK(muiRenderGlyphField(fonts->service, muiFont_GetKey(font), glyph, size, spread, &image,
                              s_pixels, sizeof s_pixels) == mui_success,
          "field rendered");
    return image;
}

static void TestFields(void)
{
    Fonts fonts = MakeFonts();
    // The em square, 8 up and 2 down at 10 pixels, reached 4 pixels past:
    // half a pixel in is 144, half out 112, 4 or more out 0, and the
    // middle, 5 pixels in, is held at 255.
    muiGlyphImage image = RenderField(&fonts, fonts.ahem, AHEM_BOX, 10.0f, 4);
    CHECK(SameImage(image, -4, 12, 18, 18) && At(image, 4, 9) == 144 && At(image, 3, 9) == 112 &&
              At(image, 0, 9) == 16 && At(image, 0, 0) == 0 && At(image, 8, 8) == 255 &&
              At(image, 13, 9) == 144 && At(image, 14, 9) == 112,
          "the box's field");
    image = RenderField(&fonts, fonts.ahem, AHEM_SPACE, 10.0f, 4);
    CHECK(SameImage(image, 0, 0, 0, 0), "a space has no field");
    image = RenderField(&fonts, fonts.liberation, LIBERATION_A, 32.0f, 4);
    CHECK(SameImage(image, -4, 27, 30, 31) && Hash(image) == 0x0c496e85u, "A at 32, reach 4");
    image = RenderField(&fonts, fonts.liberation, LIBERATION_A, 32.0f, 8);
    CHECK(SameImage(image, -8, 31, 38, 39) && Hash(image) == 0x5795a770u, "A at 32, reach 8");
    // Two boxes overlapping from 300 to 600 units: across the middle row
    // the field only rises to the middle and falls, with no dip where a
    // box's side lies inside the other.
    muiFontDef def = muiDefaultFontDef();
    def.data = s_overlap;
    def.size = sizeof s_overlap;
    def.dataMode = mui_fontDataBorrow;
    muiFontId overlap = {0, 0};
    CHECK(muiCreateFont(fonts.service, &def, &overlap) == mui_success, "the overlap font");
    image = RenderField(&fonts, overlap, 1, 100.0f, 8);
    bool rising = true;
    uint32_t middle = image.height / 2;
    for (uint32_t x = 1; x < image.width / 2; x++)
    {
        rising = rising && At(image, x, middle) >= At(image, x - 1, middle) &&
                 At(image, image.width - 1 - x, middle) >= At(image, image.width - x, middle);
    }
    CHECK(SameImage(image, -8, 78, 106, 86) && rising && At(image, 45, middle) == 255, "one shape");
    muiDestroyTextService(fonts.service);
}

static void TestFieldContract(void)
{
    Fonts fonts = MakeFonts();
    uint64_t ahem = muiFont_GetKey(fonts.ahem);
    muiGlyphImage image = {0, 0, 0, 0};
    CHECK(muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 10.0f, 4, &image, NULL, 0) ==
                  mui_errorCapacity &&
              SameImage(image, -4, 12, 18, 18),
          "too few bytes, the size told");
    CHECK(muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 10.0f, 4, &image, s_pixels, 323) ==
              mui_errorCapacity,
          "one byte short");
    CHECK(muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 10.0f, MUI_MIN_FIELD_SPREAD, &image,
                              s_pixels, sizeof s_pixels) == mui_success &&
              image.width == 14 &&
              muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 10.0f, MUI_MAX_FIELD_SPREAD,
                                  &image, s_pixels, sizeof s_pixels) == mui_success &&
              image.width == 74,
          "the least and most reach");
    CHECK(muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 10.0f, MUI_MIN_FIELD_SPREAD - 1,
                              &image, s_pixels, sizeof s_pixels) == mui_errorInvalid &&
              muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 10.0f, MUI_MAX_FIELD_SPREAD + 1,
                                  &image, s_pixels, sizeof s_pixels) == mui_errorInvalid &&
              muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 0.0f, 4, &image, s_pixels,
                                  sizeof s_pixels) == mui_errorInvalid &&
              muiRenderGlyphField(NULL, ahem, AHEM_BOX, 10.0f, 4, &image, s_pixels,
                                  sizeof s_pixels) == mui_errorInvalid &&
              muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 10.0f, 4, NULL, s_pixels,
                                  sizeof s_pixels) == mui_errorInvalid &&
              muiRenderGlyphField(fonts.service, ahem, AHEM_BOX, 10.0f, 4, &image, NULL, 1) ==
                  mui_errorInvalid,
          "a spread, size or argument refused");
    muiFontMetrics metrics;
    CHECK(muiFont_GetMetrics(fonts.service, fonts.ahem, &metrics) == mui_success &&
              muiRenderGlyphField(fonts.service, ahem, metrics.glyphCount, 10.0f, 4, &image,
                                  s_pixels, sizeof s_pixels) == mui_errorInvalid &&
              muiRenderGlyphField(fonts.service, ahem + 7, AHEM_BOX, 10.0f, 4, &image, s_pixels,
                                  sizeof s_pixels) == mui_errorStale,
          "a glyph past the font, a key of no font");
    muiFontDef def = muiDefaultFontDef();
    def.data = s_largeGlyph;
    def.size = sizeof s_largeGlyph;
    def.dataMode = mui_fontDataBorrow;
    muiFontId large = {0, 0};
    CHECK(muiCreateFont(fonts.service, &def, &large) == mui_success &&
              muiRenderGlyphField(fonts.service, muiFont_GetKey(large), 1, MUI_MAX_GLYPH_PIXEL_SIZE,
                                  4, &image, NULL, 0) == mui_errorFormat,
          "too large");
    muiDestroyTextService(fonts.service);
}

int main(void)
{
    TestBoxes();
    TestSameEverywhere();
    TestContract();
    TestTooLarge();
    TestFields();
    TestFieldContract();
    return s_failures == 0 ? 0 : 1;
}
