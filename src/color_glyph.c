// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Colour glyphs (record mui-0006): a COLR version 1 glyph's paint graph
// (colr_paint.c) where it has one, else its version 0 layers, each another
// glyph's outline, rendered as FreeType's coverage over the layers' joint
// box and composited in order, source over, in premultiplied linear
// light; a layer's colour is its palette entry, sRGB with straight alpha,
// or for entry 0xFFFF the text's colour. The result is stored as an sRGB
// texture holds premultiplied colour: red, green and blue encoded with
// sRGB's transfer function, alpha linear.

#include "color.h"
#include "colr_paint.h"
#include "text_service.h"

#include "maul-ui/glyph_image.h"

#include FT_COLOR_H

#include <math.h>
#include <string.h>

// The glyph's layers' joint box at a size.
static muiResult JointBox(const muiPaintSource* source, uint32_t glyph, muiPixelBox* boxOut)
{
    FT_LayerIterator iterator = {0};
    FT_UInt layer = 0;
    FT_UInt color = 0;
    *boxOut = (muiPixelBox){0, 0, 0, 0};
    while (FT_Get_Color_Glyph_Layer(source->font->face, glyph, &layer, &color, &iterator))
    {
        muiResult result =
            muiLoadGlyphOutline(source->font, source->key, layer, source->size, source->offset);
        if (result != mui_success)
        {
            return result;
        }
        muiJoinPixelBox(boxOut, muiOutlineBox(&source->font->face->glyph->outline));
    }
    return mui_success;
}

// Composites one layer's coverage of its colour over the accumulated
// pixels: source over, premultiplied.
static void Composite(const unsigned char* coverage, size_t count, muiLinearColor color,
                      float* accumulated)
{
    for (size_t i = 0; i < count; i++)
    {
        float k = (float)coverage[i] / 255.0f;
        float* pixel = &accumulated[i * 4];
        float keep = 1.0f - color.a * k;
        pixel[0] = color.r * k + pixel[0] * keep;
        pixel[1] = color.g * k + pixel[1] * keep;
        pixel[2] = color.b * k + pixel[2] * keep;
        pixel[3] = color.a * k + pixel[3] * keep;
    }
}

// Paints a glyph's version 0 layers over a box into premultiplied linear
// pixels.
static muiResult PaintLayers(const muiPaintSource* source, uint32_t glyph, const muiPixelBox* box,
                             float* accumulated)
{
    muiTextService* service = source->service;
    size_t count = (size_t)box->width * (size_t)box->height;
    if (!muiReserve(&service->allocator, &service->colorCoverage, count))
    {
        return mui_errorCapacity;
    }
    unsigned char* coverage = service->colorCoverage.data;
    FT_LayerIterator iterator = {0};
    FT_UInt layer = 0;
    FT_UInt entry = 0;
    while (FT_Get_Color_Glyph_Layer(source->font->face, glyph, &layer, &entry, &iterator))
    {
        muiResult result =
            muiLoadGlyphOutline(source->font, source->key, layer, source->size, source->offset);
        if (result != mui_success)
        {
            return result;
        }
        memset(coverage, 0, count);
        result = muiRasterizeOutline(service, &source->font->face->glyph->outline, box, coverage,
                                     (int)box->width);
        if (result != mui_success)
        {
            return result;
        }
        Composite(coverage, count,
                  muiPaletteColor(source->palette, source->entries, entry, source->foreground),
                  accumulated);
    }
    return mui_success;
}

// Stores premultiplied linear pixels as an sRGB texture holds them.
static void Store(const float* accumulated, size_t count, unsigned char* pixels)
{
    for (size_t i = 0; i < count * 4; i++)
    {
        float value = fminf(fmaxf(accumulated[i], 0.0f), 1.0f);
        float encoded = i % 4 == 3 ? value : muiEncodeSrgb(value);
        pixels[i] = (unsigned char)(encoded * 255.0f + 0.5f);
    }
}

// Selects a palette, the first for one past the font's, into a source.
static void SelectPalette(muiPaintSource* source, uint32_t palette)
{
    FT_Face face = source->font->face;
    FT_Palette_Data data;
    FT_Color* colors = nullptr;
    if (FT_Palette_Data_Get(face, &data) == 0 && data.num_palettes > 0 &&
        FT_Palette_Select(face, (FT_UShort)(palette < data.num_palettes ? palette : 0), &colors) ==
            0)
    {
        source->palette = colors;
        source->entries = data.num_palette_entries;
    }
}

// A colour glyph, of either version, in a source.
typedef struct ColorGlyph
{
    muiPaintSource source;
    uint32_t glyph;
#if MUI_COLR_PAINT
    // The graph's root where the glyph has a version 1 graph.
    bool painted;
    FT_OpaquePaint root;
#endif
} ColorGlyph;

// Whether the glyph has colour of either version, and which.
static bool FindColor(ColorGlyph* color)
{
    FT_Face face = color->source.font->face;
#if MUI_COLR_PAINT
    color->painted = muiFindColorPaint(face, color->glyph, &color->root);
    if (color->painted)
    {
        return true;
    }
#endif
    FT_LayerIterator probe = {0};
    FT_UInt layer = 0;
    FT_UInt entry = 0;
    return FT_Get_Color_Glyph_Layer(face, color->glyph, &layer, &entry, &probe) != 0;
}

static muiResult BoxOf(const ColorGlyph* color, muiPixelBox* boxOut)
{
#if MUI_COLR_PAINT
    if (color->painted)
    {
        return muiColorPaintBox(&color->source, color->glyph, color->root, boxOut);
    }
#endif
    return JointBox(&color->source, color->glyph, boxOut);
}

static muiResult Paint(const ColorGlyph* color, const muiPixelBox* box, float* accumulated)
{
#if MUI_COLR_PAINT
    if (color->painted)
    {
        return muiPaintColorGlyph(&color->source, color->glyph, color->root, box, accumulated);
    }
#endif
    return PaintLayers(&color->source, color->glyph, box, accumulated);
}

muiResult muiRenderColorGlyph(muiTextService* service, uint64_t font, uint32_t glyph,
                              float pixelSize, float offsetX, uint32_t palette,
                              muiLinearColor foreground, muiGlyphImage* imageOut,
                              unsigned char* pixels, size_t capacity)
{
    if (service == nullptr || imageOut == nullptr || (pixels == nullptr && capacity != 0) ||
        !muiIsGlyphSizeValid(pixelSize) || !(offsetX >= 0.0f && offsetX < 1.0f))
    {
        return mui_errorInvalid;
    }
    muiResult result = mui_success;
    ColorGlyph color = {.glyph = glyph};
    color.source.service = service;
    color.source.font = muiGlyphFontOf(service, font, glyph, &color.source.key, &result);
    if (color.source.font == nullptr)
    {
        return result;
    }
    *imageOut = (muiGlyphImage){0, 0, 0, 0};
    if (!color.source.font->colorLayers || !FindColor(&color))
    {
        return mui_empty;
    }
    color.source.size = lroundf(pixelSize * 64.0f);
    color.source.offset = (FT_Pos)lroundf(offsetX * 64.0f);
    color.source.foreground = foreground;
    muiPixelBox box = {0, 0, 0, 0};
    result = BoxOf(&color, &box);
    if (result != mui_success)
    {
        return result;
    }
    if (box.width > MUI_MAX_IMAGE_EXTENT || box.height > MUI_MAX_IMAGE_EXTENT)
    {
        return mui_errorFormat;
    }
    *imageOut = (muiGlyphImage){(int32_t)box.left, (int32_t)(box.bottom + box.height),
                                (uint32_t)box.width, (uint32_t)box.height};
    size_t count = (size_t)box.width * (size_t)box.height;
    if (count * 4 > capacity)
    {
        return mui_errorCapacity;
    }
    if (count == 0)
    {
        return mui_success;
    }
    if (!muiReserve(&service->allocator, &service->colorPixels, count * 4 * sizeof(float)))
    {
        return mui_errorCapacity;
    }
    SelectPalette(&color.source, palette);
    float* accumulated = service->colorPixels.data;
    memset(accumulated, 0, count * 4 * sizeof(float));
    result = Paint(&color, &box, accumulated);
    if (result == mui_success)
    {
        Store(accumulated, count, pixels);
    }
    return result;
}
