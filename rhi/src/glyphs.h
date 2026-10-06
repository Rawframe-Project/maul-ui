// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The renderer's glyphs (record mui-0005): an atlas of its own over the
// host's text service, each of its pages an R8 texture made when the
// atlas makes the page, and the rectangles the atlas changed uploaded
// each frame. A changed rectangle covers its images' empty gutters, so
// every texel a glyph's quad samples has been written; what was never
// written is never sampled. Without Maul UI's text component there is no
// atlas, and a text service is refused.

#ifndef MAUL_UI_RHI_GLYPHS_H
#define MAUL_UI_RHI_GLYPHS_H

#include "maul-ui-rhi/renderer.h"

#include <stdint.h>

// The pages the renderer's atlas may make, as Maul UI's atlases allow.
#define MUI_RHI_MAX_PAGES 64u

// A glyph's image in a page: the page, its rect there in texels, the
// page's size, and its top left in device pixels.
typedef struct muiRhiGlyph
{
    uint32_t page;
    uint32_t u;
    uint32_t v;
    uint32_t width;
    uint32_t height;
    uint32_t pageWidth;
    uint32_t pageHeight;
    int32_t x;
    int32_t y;
} muiRhiGlyph;

typedef struct muiRhiGlyphs
{
    muiAllocator allocator;
    mrhiDevice* device;
    struct muiGlyphAtlas* atlas;
    mrhiTextureId pages[MUI_RHI_MAX_PAGES];
    // The pages in the open frame.
    mrhiResourceId resources[MUI_RHI_MAX_PAGES];
    uint32_t pageCount;
    // The atlas's changed rectangles this frame, its muiAtlasUpdate
    // records.
    void* updates;
    uint32_t updateCount;
    uint32_t updateCapacity;
} muiRhiGlyphs;

// Glyphs over a text service, or none for NULL: `mui_errorInvalid` for a
// service where Maul UI was built without text.
muiResult muiRhiMakeGlyphs(muiRhiGlyphs* glyphs, const muiAllocator* allocator, mrhiDevice* device,
                           muiTextService* text);

void muiRhiFreeGlyphs(muiRhiGlyphs* glyphs);

// Starts a frame: glyphs of earlier frames may be evicted.
void muiRhiNextGlyphFrame(muiRhiGlyphs* glyphs);

// A glyph's coverage for a pen in device pixels, packed the first time,
// of no width for a glyph without an image: false when there is no atlas
// or it cannot be packed.
bool muiRhiGetGlyph(muiRhiGlyphs* glyphs, uint64_t font, uint32_t id, float pixelSize, float penX,
                    float baselineY, muiRhiGlyph* glyphOut);

// A glyph's distance field at an em of pixelSize field pixels and a
// spread, packed the first time, its top left from the pen and baseline
// in field pixels: false as muiRhiGetGlyph.
bool muiRhiGetGlyphField(muiRhiGlyphs* glyphs, uint64_t font, uint32_t id, float pixelSize,
                         uint32_t spread, muiRhiGlyph* glyphOut);

// After a frame's glyphs are got: textures for pages the atlas made, its
// changed rectangles taken, and every page imported into the open frame.
muiResult muiRhiPrepareGlyphs(muiRhiGlyphs* glyphs);

// The page a frame's changed rectangle writes, below the page count.
uint32_t muiRhiUpdatedPage(const muiRhiGlyphs* glyphs, uint32_t update);

// Writes the changed rectangles in a pass that declares their pages as
// copy destinations.
bool muiRhiWriteGlyphs(const muiRhiGlyphs* glyphs, mrhiPassId pass);

#endif // MAUL_UI_RHI_GLYPHS_H
