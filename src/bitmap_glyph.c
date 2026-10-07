// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Colour bitmap glyphs (record mui-0006), in CBLC and CBDT as OpenType has
// them. A strike's index subtables name glyph ranges; each of the five
// index formats finds a glyph's image in CBDT, one of image formats 17
// (small metrics), 18 (big metrics) or 19 (the index's metrics), each a
// PNG. Every offset and length is checked against its table.

#include "bitmap_glyph.h"

enum
{
    BITMAP_SIZE = 48,
    // A BitmapSize record's fields.
    SIZE_ARRAY = 0,
    SIZE_COUNT = 8,
    SIZE_START = 40,
    SIZE_END = 42,
    SIZE_PPEM_Y = 45,
    SIZE_DEPTH = 46,
    BIG_METRICS = 8,
    SMALL_METRICS = 5
};

// A span of a table, its reads checked.
typedef struct Span
{
    const uint8_t* data;
    size_t size;
} Span;

static bool Has(Span s, size_t at, size_t length)
{
    return at <= s.size && length <= s.size - at;
}

static uint32_t U8(Span s, size_t at)
{
    return Has(s, at, 1) ? s.data[at] : 0;
}

static uint32_t U16(Span s, size_t at)
{
    return Has(s, at, 2) ? (uint32_t)s.data[at] << 8 | s.data[at + 1] : 0;
}

static uint32_t U32(Span s, size_t at)
{
    return Has(s, at, 4) ? (uint32_t)s.data[at] << 24 | (uint32_t)s.data[at + 1] << 16 |
                               (uint32_t)s.data[at + 2] << 8 | s.data[at + 3]
                         : 0;
}

static int32_t I8(Span s, size_t at)
{
    return (int32_t)(int8_t)(uint8_t)U8(s, at);
}

bool muiHasColorBitmaps(const muiFont* font)
{
    return font->cblc.size > 0 && font->cbdt.size > 0;
}

// Where a glyph's image lies in CBDT: its offset, length and image
// format, and the index's big metrics where it has them.
typedef struct Image
{
    size_t offset;
    size_t length;
    uint32_t format;
    bool hasMetrics;
    size_t metrics;
    Span index;
} Image;

// The index of a glyph in a sorted array of 16-bit glyphs, stride bytes
// apart; count when absent.
static uint32_t Search(Span s, size_t at, uint32_t count, size_t stride, uint32_t glyph)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        uint32_t found = U16(s, at + middle * stride);
        if (found == glyph)
        {
            return middle;
        }
        if (found < glyph)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return count;
}

// Offsets of each glyph of the range and one past: 32 bits (format 1) or
// 16 (format 3).
static bool ByOffsets(Span t, size_t at, uint32_t i, uint32_t width, Image* image)
{
    size_t first = at + (size_t)i * width;
    if (!Has(t, first, (size_t)width * 2))
    {
        return false;
    }
    uint32_t start = width == 4 ? U32(t, first) : U16(t, first);
    uint32_t end = width == 4 ? U32(t, first + 4) : U16(t, first + 2);
    image->offset += start;
    image->length = end > start ? end - start : 0;
    return image->length > 0;
}

// Images of one size: one after another (format 2), or for a sorted list
// of glyphs (format 5); both with big metrics shared.
static bool BySize(Span t, size_t at, uint32_t i, uint32_t glyph, bool listed, Image* image)
{
    uint32_t size = U32(t, at);
    image->hasMetrics = true;
    image->metrics = at + 4;
    if (listed)
    {
        uint32_t count = U32(t, at + 4 + BIG_METRICS);
        size_t list = at + 8 + BIG_METRICS;
        if (!Has(t, list, (size_t)count * 2))
        {
            return false;
        }
        i = Search(t, list, count, 2, glyph);
        if (i == count)
        {
            return false;
        }
    }
    image->offset += (size_t)i * size;
    image->length = size;
    return size > 0;
}

// Glyph and offset pairs, sorted, and one past (format 4).
static bool ByPairs(Span t, size_t at, uint32_t glyph, Image* image)
{
    uint32_t count = U32(t, at);
    size_t pairs = at + 4;
    if (!Has(t, pairs, ((size_t)count + 1) * 4))
    {
        return false;
    }
    uint32_t i = Search(t, pairs, count, 4, glyph);
    if (i == count)
    {
        return false;
    }
    uint32_t start = U16(t, pairs + (size_t)i * 4 + 2);
    uint32_t end = U16(t, pairs + (size_t)i * 4 + 6);
    image->offset += start;
    image->length = end > start ? end - start : 0;
    return image->length > 0;
}

// Finds a glyph in one index subtable, whose range holds it.
static bool InSubtable(Span t, size_t at, uint32_t first, uint32_t glyph, Image* image)
{
    uint32_t indexFormat = U16(t, at);
    *image = (Image){U32(t, at + 4), 0, U16(t, at + 2), false, 0, t};
    if (!Has(t, at, 8))
    {
        return false;
    }
    uint32_t i = glyph - first;
    switch (indexFormat)
    {
    case 1:
        return ByOffsets(t, at + 8, i, 4, image);
    case 2:
        return BySize(t, at + 8, i, glyph, false, image);
    case 3:
        return ByOffsets(t, at + 8, i, 2, image);
    case 4:
        return ByPairs(t, at + 8, glyph, image);
    case 5:
        return BySize(t, at + 8, i, glyph, true, image);
    default:
        return false;
    }
}

// Finds a glyph's image in one strike.
static bool InStrike(Span t, size_t record, uint32_t glyph, Image* image)
{
    if (glyph < U16(t, record + SIZE_START) || glyph > U16(t, record + SIZE_END) ||
        U8(t, record + SIZE_DEPTH) != 32)
    {
        return false;
    }
    size_t array = U32(t, record + SIZE_ARRAY);
    uint32_t count = U32(t, record + SIZE_COUNT);
    if (!Has(t, array, (size_t)count * 8))
    {
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        size_t entry = array + (size_t)i * 8;
        uint32_t first = U16(t, entry);
        if (glyph >= first && glyph <= U16(t, entry + 2))
        {
            return InSubtable(t, array + U32(t, entry + 4), first, glyph, image);
        }
    }
    return false;
}

// Reads an image's metrics and PNG from CBDT.
static bool ReadImage(Span cbdt, const Image* image, uint32_t ppem, muiBitmapGlyph* out)
{
    if (!Has(cbdt, image->offset, image->length))
    {
        return false;
    }
    Span data = {cbdt.data + image->offset, image->length};
    size_t png = 0;
    switch (image->format)
    {
    case 17:
        out->left = I8(data, 2);
        out->top = I8(data, 3);
        png = SMALL_METRICS;
        break;
    case 18:
        out->left = I8(data, 2);
        out->top = I8(data, 3);
        png = BIG_METRICS;
        break;
    case 19:
        if (!image->hasMetrics)
        {
            return false;
        }
        out->left = I8(image->index, image->metrics + 2);
        out->top = I8(image->index, image->metrics + 3);
        break;
    default:
        return false;
    }
    uint32_t length = U32(data, png);
    if (!Has(data, png + 4, length) || length == 0)
    {
        return false;
    }
    out->png = data.data + png + 4;
    out->size = length;
    out->ppem = ppem;
    return true;
}

// How well a strike's ppem suits a size: smaller is better; the smallest
// reaching it first, then the largest below it.
static uint32_t Rank(uint32_t ppem, float pixelSize)
{
    return (float)ppem >= pixelSize ? ppem : 0x10000u + (0xFFFFu - ppem);
}

bool muiFindBitmapGlyph(const muiFont* font, uint32_t glyph, float pixelSize,
                        muiBitmapGlyph* glyphOut)
{
    if (!muiHasColorBitmaps(font))
    {
        return false;
    }
    Span cblc = {font->cblc.data, font->cblc.size};
    Span cbdt = {font->cbdt.data, font->cbdt.size};
    uint32_t sizes = U32(cblc, 4);
    if (U16(cblc, 0) < 2 || !Has(cblc, 8, (size_t)sizes * BITMAP_SIZE))
    {
        return false;
    }
    bool found = false;
    uint32_t best = UINT32_MAX;
    for (uint32_t i = 0; i < sizes; i++)
    {
        size_t record = 8 + (size_t)i * BITMAP_SIZE;
        uint32_t ppem = U8(cblc, record + SIZE_PPEM_Y);
        uint32_t rank = Rank(ppem, pixelSize);
        Image image;
        muiBitmapGlyph candidate;
        if (ppem > 0 && rank < best && InStrike(cblc, record, glyph, &image) &&
            ReadImage(cbdt, &image, ppem, &candidate))
        {
            *glyphOut = candidate;
            best = rank;
            found = true;
        }
    }
    return found;
}
