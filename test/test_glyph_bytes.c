// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph images and atlas pages are bytes the library writes: rendered
// into a caller's buffer, or packed into pages from the service's memory.
// Written over memory that held one byte pattern, then over memory that
// held a second, every glyph of every renderer and atlas format gives the
// same bytes, so nothing the memory held before reaches them.

#include "test_harness.h"

#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"
#include "bitmap.inc"
#include "color.inc"
#include "liberation_sans.inc"
#include "sbix.inc"

// The byte allocations and buffers hold before the library writes them.
static int s_pattern;

static void* Alloc(size_t size, size_t alignment, void* user)
{
    (void)user;
    // The library asks for no more than malloc's alignment.
    void* block = alignment <= alignof(max_align_t) ? malloc(size) : NULL;
    if (block != NULL)
    {
        memset(block, s_pattern, size);
    }
    return block;
}

static void Free(void* block, size_t size, size_t alignment, void* user)
{
    (void)size;
    (void)alignment;
    (void)user;
    free(block);
}

typedef struct Font
{
    const unsigned char* data;
    size_t size;
    const char* name;
} Font;

static const Font s_fonts[] = {
    {s_ahem, sizeof s_ahem, "ahem"},    {s_liberationSans, sizeof s_liberationSans, "liberation"},
    {s_color, sizeof s_color, "color"}, {s_bitmap, sizeof s_bitmap, "bitmap"},
    {s_sbix, sizeof s_sbix, "sbix"},
};

static unsigned char s_pixels[2][256 * 256 * 4];
static unsigned char s_pages[2][8][256 * 256 * 4];
static muiAtlasUpdate s_updates[2][4096];
static uint32_t s_updateCount[2];

// A service of one font, its memory of the pattern.
static muiTextService* Service(uint64_t* font, const Font* which)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    def.allocator = (muiAllocator){Alloc, Free, NULL};
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = which->data;
    fontDef.size = which->size;
    muiFontId id;
    CHECK(muiCreateFont(service, &fontDef, &id) == mui_success, "font");
    *font = muiFont_GetKey(id);
    return service;
}

// Renders a glyph one of the four ways into a buffer of the pattern:
// its result, and the image's bytes.
static int Images(const Font* which, int pass, uint32_t glyph, float size, int kind,
                  size_t* bytesOut)
{
    uint64_t font;
    muiTextService* service = Service(&font, which);
    memset(s_pixels[pass], s_pattern, sizeof s_pixels[pass]);
    muiGlyphImage image = {0};
    muiResult result;
    size_t channels = 1;
    switch (kind)
    {
    case 0:
        result = muiTextService_RenderGlyph(service, font, glyph, size, 0.25f, &image,
                                            s_pixels[pass], sizeof s_pixels[pass]);
        break;
    case 1:
        result = muiTextService_RenderColorGlyph(service, font, glyph, size, 0.5f, 0,
                                                 (muiLinearColor){0.2f, 0.4f, 0.6f, 1.0f}, &image,
                                                 s_pixels[pass], sizeof s_pixels[pass]);
        channels = 4;
        break;
    case 2:
        result = muiTextService_RenderGlyphField(service, font, glyph, size, 4, &image,
                                                 s_pixels[pass], sizeof s_pixels[pass]);
        break;
    default:
        result = muiTextService_RenderGlyphMultiField(service, font, glyph, size, 4, &image,
                                                      s_pixels[pass], sizeof s_pixels[pass]);
        channels = 4;
        break;
    }
    *bytesOut = result == mui_success ? (size_t)image.width * image.height * channels : 0;
    muiDestroyTextService(service);
    return (int)result;
}

static void TestImages(void)
{
    const float sizes[] = {7.0f, 16.0f, 33.5f};
    for (size_t f = 0; f < sizeof s_fonts / sizeof s_fonts[0]; f++)
    {
        for (int kind = 0; kind < 4; kind++)
        {
            for (uint32_t glyph = 0; glyph < 12; glyph++)
            {
                for (size_t s = 0; s < 3; s++)
                {
                    size_t bytes[2];
                    int results[2];
                    for (int pass = 0; pass < 2; pass++)
                    {
                        s_pattern = pass == 0 ? 0xAA : 0x55;
                        results[pass] =
                            Images(&s_fonts[f], pass, glyph, sizes[s], kind, &bytes[pass]);
                    }
                    bool same = results[0] == results[1] && bytes[0] == bytes[1] &&
                                memcmp(s_pixels[0], s_pixels[1], bytes[0]) == 0;
                    if (!same)
                    {
                        fprintf(stderr, "image differs: %s kind %d glyph %u size %g result %d\n",
                                s_fonts[f].name, kind, glyph, (double)sizes[s], results[0]);
                    }
                    CHECK(same, "the same image bytes");
                }
            }
        }
    }
}

// Fills an atlas of small plots well past its pages, frame by frame, so
// plots are evicted and used again; then copies its pages and updates.
static void Atlas(const Font* which, muiAtlasFormat format, int pass)
{
    uint64_t font;
    muiTextService* service = Service(&font, which);
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    def.pageWidth = 128;
    def.pageHeight = 128;
    def.plotWidth = 32;
    def.plotHeight = 32;
    def.maxPages = 2;
    def.format = format;
    muiGlyphAtlas* atlas = NULL;
    CHECK(muiCreateGlyphAtlas(service, &def, &atlas) == mui_success, "atlas");
    s_updateCount[pass] = 0;
    for (uint32_t frame = 0; frame < 6; frame++)
    {
        for (uint32_t glyph = 0; glyph < 20; glyph++)
        {
            muiAtlasGlyph out;
            float size = 9.0f + (float)((glyph * 7 + frame * 5) % 23);
            muiLinearColor ink = {0.3f, 0.6f, 0.9f, 1.0f};
            (void)(format == mui_atlasColor
                       ? muiGlyphAtlas_GetColorGlyph(atlas, font, glyph, size, 0.25f * (float)frame,
                                                     0.0f, 0, ink, &out)
                   : format == mui_atlasFourChannel
                       ? muiGlyphAtlas_GetGlyphMultiField(atlas, font, glyph, size, 3, &out)
                       : muiGlyphAtlas_GetGlyph(atlas, font, glyph, size, 0.25f * (float)frame,
                                                0.0f, &out));
        }
        uint32_t count = 0;
        (void)muiGlyphAtlas_TakeUpdates(atlas, &s_updates[pass][s_updateCount[pass]],
                                        4096 - s_updateCount[pass], &count);
        s_updateCount[pass] += count;
        muiGlyphAtlas_BeginFrame(atlas);
    }
    for (uint32_t page = 0; page < 8; page++)
    {
        muiAtlasPage view;
        memset(s_pages[pass][page], 0, sizeof s_pages[pass][page]);
        if (muiGlyphAtlas_GetPage(atlas, page, &view) == mui_success)
        {
            size_t channels = format == mui_atlasOneChannel ? 1 : 4;
            memcpy(s_pages[pass][page], view.pixels, (size_t)view.width * view.height * channels);
        }
    }
    muiDestroyGlyphAtlas(atlas);
    muiDestroyTextService(service);
}

static void TestAtlases(void)
{
    const muiAtlasFormat formats[] = {mui_atlasOneChannel, mui_atlasColor, mui_atlasFourChannel};
    for (size_t f = 0; f < sizeof s_fonts / sizeof s_fonts[0]; f++)
    {
        for (size_t k = 0; k < 3; k++)
        {
            for (int pass = 0; pass < 2; pass++)
            {
                s_pattern = pass == 0 ? 0xAA : 0x55;
                Atlas(&s_fonts[f], formats[k], pass);
            }
            bool same = s_updateCount[0] == s_updateCount[1] &&
                        memcmp(s_updates[0], s_updates[1],
                               s_updateCount[0] * sizeof(muiAtlasUpdate)) == 0 &&
                        memcmp(s_pages[0], s_pages[1], sizeof s_pages[0]) == 0;
            if (!same)
            {
                fprintf(stderr, "atlas differs: %s format %d, %u updates\n", s_fonts[f].name,
                        (int)formats[k], s_updateCount[0]);
            }
            CHECK(same, "the same atlas bytes");
        }
    }
}

int main(void)
{
    TestImages();
    TestAtlases();
    return s_failures == 0 ? 0 : 1;
}
