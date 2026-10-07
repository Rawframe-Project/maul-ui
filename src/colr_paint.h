// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// COLR version 1 paint graphs (record mui-0006): a glyph's graph walked
// from its root, each paint evaluated into a premultiplied linear-light
// surface over the glyph's box of pixels, transforms composed on the way
// down from font units, a PaintGlyph's outline a FreeType coverage mask
// over its child, layers composited source over. At most
// MUI_MAX_PAINT_DEPTH paints deep, so a cycle in a damaged font ends.

#ifndef MAUL_UI_SRC_COLR_PAINT_H
#define MAUL_UI_SRC_COLR_PAINT_H

#include "glyph_outline.h"

#include FT_COLOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Whether FreeType reads version 1 graphs: from 2.13, which an installed
// FreeType may be older than.
#define MUI_COLR_PAINT (FREETYPE_MAJOR > 2 || (FREETYPE_MAJOR == 2 && FREETYPE_MINOR >= 13))

enum
{
    MUI_MAX_PAINT_DEPTH = 64,
    // The palette entry that stands for the text's colour.
    MUI_FOREGROUND_ENTRY = 0xFFFF
};

// What a colour glyph is painted with, of either version.
typedef struct muiPaintSource
{
    muiTextService* service;
    muiFont* font;
    // The font key with its default resolved, as muiGlyphFontOf gives it.
    uint64_t key;
    // The em in 64ths of a pixel, and how far the pen is right of a pixel
    // boundary in 64ths.
    long size;
    FT_Pos offset;
    // The palette's colours, entries of them, NULL for none.
    const FT_Color* palette;
    uint32_t entries;
    // The text's colour, linear and premultiplied.
    muiLinearColor foreground;
} muiPaintSource;

// A palette entry's colour, premultiplied linear: sRGB with straight
// alpha in the palette, the text's colour for MUI_FOREGROUND_ENTRY, and
// transparent for an entry past the palette or with no palette.
muiLinearColor muiPaletteColor(const FT_Color* palette, uint32_t entries, uint32_t index,
                               muiLinearColor foreground);

#if MUI_COLR_PAINT

// Whether a glyph has a version 1 graph, and its root.
bool muiFindColorPaint(FT_Face face, uint32_t glyph, FT_OpaquePaint* rootOut);

// The box of whole pixels a glyph's graph paints: its clip box where the
// font has one, else the joint box of its outlines under their transforms;
// a width of 0 for nothing.
muiResult muiColorPaintBox(const muiPaintSource* source, uint32_t glyph, FT_OpaquePaint root,
                           muiPixelBox* boxOut);

// Paints a glyph's graph over a box into premultiplied linear pixels, four
// floats a pixel, rows from the top, nothing outside its clip box.
muiResult muiPaintColorGlyph(const muiPaintSource* source, uint32_t glyph, FT_OpaquePaint root,
                             const muiPixelBox* box, float* pixels);

#endif

#endif // MAUL_UI_SRC_COLR_PAINT_H
