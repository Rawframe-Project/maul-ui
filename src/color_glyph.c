// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Colour glyphs (record mui-0006): a COLR version 0 glyph's layers, each
// another glyph's outline, rendered as FreeType's coverage over the
// layers' joint box and composited in order, source over, in premultiplied
// linear light; a layer's colour is its palette entry, sRGB with straight
// alpha, or for entry 0xFFFF the text's colour. The result is stored as an
// sRGB texture holds premultiplied colour: red, green and blue encoded
// with sRGB's transfer function, alpha linear.

#include "color.h"
#include "glyph_outline.h"
#include "text_service.h"

#include "maul-ui/glyph_image.h"

#include FT_COLOR_H

#include <math.h>
#include <string.h>

enum
{
    // The palette entry that stands for the text's colour.
    FOREGROUND = 0xFFFF
};

// A layer's colour, premultiplied linear: its palette entry, the text's
// colour, or transparent for an entry past the palette.
static muiLinearColor LayerColor(const FT_Color* palette, uint32_t entries, uint32_t index,
                                 muiLinearColor foreground)
{
    if (index == FOREGROUND)
    {
        return foreground;
    }
    if (palette == nullptr || index >= entries)
    {
        return (muiLinearColor){0.0f, 0.0f, 0.0f, 0.0f};
    }
    const FT_Color* entry = &palette[index];
    const muiColor color = {(float)entry->red / 255.0f, (float)entry->green / 255.0f,
                            (float)entry->blue / 255.0f, (float)entry->alpha / 255.0f};
    double rgb[3];
    muiColorToLinearRgb(color, rgb);
    return muiPremultiply(rgb, color.a, 1.0f);
}

// The glyph's layers' joint box at a size; false when a layer cannot be
// loaded, with the result.
static bool JointBox(muiFont* record, uint64_t key, uint32_t glyph, long size, FT_Pos offset,
                     muiPixelBox* boxOut, muiResult* result)
{
    FT_LayerIterator iterator = {0};
    FT_UInt layer = 0;
    FT_UInt color = 0;
    FT_Pos left = 0;
    FT_Pos bottom = 0;
    FT_Pos right = 0;
    FT_Pos top = 0;
    bool any = false;
    while (FT_Get_Color_Glyph_Layer(record->face, glyph, &layer, &color, &iterator))
    {
        *result = muiLoadGlyphOutline(record, key, layer, size, offset);
        if (*result != mui_success)
        {
            return false;
        }
        muiPixelBox box = muiOutlineBox(&record->face->glyph->outline);
        if (box.width == 0 || box.height == 0)
        {
            continue;
        }
        left = any && left < box.left ? left : box.left;
        bottom = any && bottom < box.bottom ? bottom : box.bottom;
        right = any && right > box.left + box.width ? right : box.left + box.width;
        top = any && top > box.bottom + box.height ? top : box.bottom + box.height;
        any = true;
    }
    *boxOut = (muiPixelBox){left, bottom, right - left, top - bottom};
    return true;
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
    uint64_t key = 0;
    muiFont* record = muiGlyphFontOf(service, font, glyph, &key, &result);
    if (record == nullptr)
    {
        return result;
    }
    *imageOut = (muiGlyphImage){0, 0, 0, 0};
    FT_LayerIterator probe = {0};
    FT_UInt layer = 0;
    FT_UInt entry = 0;
    if (!FT_Get_Color_Glyph_Layer(record->face, glyph, &layer, &entry, &probe))
    {
        return mui_empty;
    }
    long size = lroundf(pixelSize * 64.0f);
    FT_Pos offset = (FT_Pos)lroundf(offsetX * 64.0f);
    muiPixelBox box = {0, 0, 0, 0};
    if (!JointBox(record, key, glyph, size, offset, &box, &result))
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
    if (!muiReserve(&service->allocator, &service->colorCoverage, count) ||
        !muiReserve(&service->allocator, &service->colorPixels, count * 4 * sizeof(float)))
    {
        return mui_errorCapacity;
    }
    FT_Palette_Data data;
    FT_Color* colors = nullptr;
    uint32_t entries = 0;
    if (FT_Palette_Data_Get(record->face, &data) == 0 && data.num_palettes > 0 &&
        FT_Palette_Select(record->face, (FT_UShort)(palette < data.num_palettes ? palette : 0),
                          &colors) == 0)
    {
        entries = data.num_palette_entries;
    }
    unsigned char* coverage = service->colorCoverage.data;
    float* accumulated = service->colorPixels.data;
    memset(accumulated, 0, count * 4 * sizeof(float));
    FT_LayerIterator iterator = {0};
    while (FT_Get_Color_Glyph_Layer(record->face, glyph, &layer, &entry, &iterator))
    {
        result = muiLoadGlyphOutline(record, key, layer, size, offset);
        if (result != mui_success)
        {
            return result;
        }
        memset(coverage, 0, count);
        result = muiRasterizeOutline(service, &record->face->glyph->outline, &box, coverage,
                                     (int)box.width);
        if (result != mui_success)
        {
            return result;
        }
        Composite(coverage, count, LayerColor(colors, entries, entry, foreground), accumulated);
    }
    Store(accumulated, count, pixels);
    return mui_success;
}
