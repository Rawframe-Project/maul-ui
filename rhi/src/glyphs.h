// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The renderer's glyphs (record mui-0005): two atlases of its own over
// the host's text service, one of a channel for coverage and one of four
// for multi-channel fields, their pages numbered together in the order
// they are made, each an R8 or RGBA8 texture made when its atlas makes
// the page, and the rectangles the atlases changed uploaded each frame.
// A changed rectangle covers its images' empty gutters, so every texel a
// glyph's quad samples has been written; what was never written is never
// sampled. Without Maul UI's text component there are no atlases, and a
// text service is refused.

#ifndef MAUL_UI_RHI_GLYPHS_H
#define MAUL_UI_RHI_GLYPHS_H

#include "maul-ui-rhi/renderer.h"

#include <stdint.h>

// The pages the renderer's atlases may make together, past the four each
// of their defs allow.
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
    // The atlas of coverage and the atlas of multi-channel fields.
    struct muiGlyphAtlas* atlases[2];
    // How many pages of each atlas are numbered here, and how many of
    // the numbered pages have textures.
    uint32_t numbered[2];
    uint32_t madeCount;
    // Each page's atlas and its number there; each atlas page's number
    // here.
    uint8_t pageAtlas[MUI_RHI_MAX_PAGES];
    uint8_t atlasPage[MUI_RHI_MAX_PAGES];
    uint8_t pageOf[2][MUI_RHI_MAX_PAGES];
    mrhiTextureId pages[MUI_RHI_MAX_PAGES];
    // The pages in the open frame.
    mrhiResourceId resources[MUI_RHI_MAX_PAGES];
    // The pages numbered, each with a texture once the frame's glyphs
    // are prepared.
    uint32_t pageCount;
    // The atlases' changed rectangles not yet written, muiAtlasUpdate
    // records whose pages are numbered here, the first writtenCount of
    // them the last frame's own, kept until the next frame in case that
    // frame is forgotten.
    void* updates;
    uint32_t updateCount;
    uint32_t writtenCount;
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

// A glyph's multi-channel distance field at an em of pixelSize field
// pixels and a spread, packed the first time, its top left from the pen
// and baseline in field pixels: false as muiRhiGetGlyph.
bool muiRhiGetGlyphField(muiRhiGlyphs* glyphs, uint64_t font, uint32_t id, float pixelSize,
                         uint32_t spread, muiRhiGlyph* glyphOut);

// After a frame's glyphs are got: textures for pages the atlases made,
// their changed rectangles taken after those pending, and every page
// imported into the open frame.
muiResult muiRhiPrepareGlyphs(muiRhiGlyphs* glyphs);

// The page a frame's changed rectangle writes, below the page count.
uint32_t muiRhiUpdatedPage(const muiRhiGlyphs* glyphs, uint32_t update);

// The bytes the pending rectangles take of a frame's uploads.
uint64_t muiRhiGlyphUploadBytes(const muiRhiGlyphs* glyphs);

// After a frame writes them: the pending rectangles are written, and
// dropped when the next frame is prepared.
void muiRhiGlyphsWritten(muiRhiGlyphs* glyphs);

// The last frame's rectangles pending again, as after a frame dropped.
void muiRhiForgetGlyphs(muiRhiGlyphs* glyphs);

// Writes the changed rectangles in a pass that declares their pages as
// copy destinations.
bool muiRhiWriteGlyphs(const muiRhiGlyphs* glyphs, mrhiPassId pass);

#endif // MAUL_UI_RHI_GLYPHS_H
