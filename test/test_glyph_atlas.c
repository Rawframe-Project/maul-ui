// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph atlases (record mui-0006): images packed with their gutters and
// found again, pens rounded to quarter pixels, plots evicted least
// recently used and never in the frame that uses them, changes taken by
// the renderer, distance fields beside coverage, the lookup table
// growing, memory running out, and calls outside the contract refused.

#include "test_harness.h"

#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/text_block.h"

#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"
#include "color.inc"

enum
{
    // Ahem's glyphs 4 to 277 are all the em square: 10 by 10 at 10
    // pixels, 8 above the baseline; 12 by 12 with the gutter.
    FIRST_BOX = 4,
    LAST_BOX = 277,
    SPACE = 3
};

typedef struct FailingAllocator
{
    int allocations;
    int failAt;
} FailingAllocator;

static void* FailingAlloc(size_t size, size_t alignment, void* context)
{
    FailingAllocator* failing = context;
    failing->allocations++;
    if (failing->failAt != 0 && failing->allocations == failing->failAt)
    {
        return NULL;
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : NULL;
}

static void FailingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

typedef struct Fixture
{
    muiTextService* service;
    uint64_t ahem;
    muiGlyphAtlas* atlas;
} Fixture;

// An atlas of pages pageSide square in plots plotSide square.
static Fixture Make(uint32_t pageSide, uint32_t plotSide, uint32_t pages, FailingAllocator* failing)
{
    Fixture fixture = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    if (failing != NULL)
    {
        def.allocator = (muiAllocator){FailingAlloc, FailingFree, failing};
    }
    if (muiCreateTextService(&def, &fixture.service) != mui_success)
    {
        return fixture;
    }
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    font.dataMode = mui_fontDataBorrow;
    muiFontId ahem = {0, 0};
    if (muiCreateFont(fixture.service, &font, &ahem) != mui_success)
    {
        return fixture;
    }
    fixture.ahem = muiFont_GetKey(ahem);
    muiGlyphAtlasDef atlas = muiDefaultGlyphAtlasDef();
    atlas.pageWidth = pageSide;
    atlas.pageHeight = pageSide;
    atlas.plotWidth = plotSide;
    atlas.plotHeight = plotSide;
    atlas.maxPages = pages;
    (void)muiCreateGlyphAtlas(fixture.service, &atlas, &fixture.atlas);
    return fixture;
}

static void Free(Fixture* fixture)
{
    muiDestroyGlyphAtlas(fixture->atlas);
    muiDestroyTextService(fixture->service);
}

static muiResult Get(Fixture* fixture, uint32_t glyph, float penX, float baselineY,
                     muiAtlasGlyph* out)
{
    return muiGlyphAtlas_Get(fixture->atlas, fixture->ahem, glyph, 10.0f, penX, baselineY, out);
}

static uint8_t Pixel(const Fixture* fixture, uint32_t page, uint32_t x, uint32_t y)
{
    muiAtlasPage view = {0};
    CHECK(muiGlyphAtlas_GetPage(fixture->atlas, page, &view) == mui_success, "page");
    return view.pixels[(size_t)y * view.width + x];
}

static void TestPacked(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    CHECK(muiGlyphAtlas_GetPageCount(fixture.atlas) == 0, "no page before a glyph");
    muiAtlasGlyph glyph = {0};
    CHECK(Get(&fixture, FIRST_BOX, 100.0f, 50.0f, &glyph) == mui_success && glyph.page == 0 &&
              glyph.u == 1 && glyph.v == 1 && glyph.width == 10 && glyph.height == 10 &&
              glyph.x == 100 && glyph.y == 42,
          "inside its gutter, 8 above the baseline");
    CHECK(muiGlyphAtlas_GetPageCount(fixture.atlas) == 1 && Pixel(&fixture, 0, 1, 1) == 255 &&
              Pixel(&fixture, 0, 10, 10) == 255 && Pixel(&fixture, 0, 0, 0) == 0 &&
              Pixel(&fixture, 0, 11, 5) == 0 && Pixel(&fixture, 0, 5, 11) == 0,
          "the image and its empty gutter");
    muiAtlasUpdate updates[4];
    uint32_t count = 0;
    CHECK(muiGlyphAtlas_TakeUpdates(fixture.atlas, updates, 4, &count) == mui_success &&
              count == 1 && updates[0].page == 0 && updates[0].x == 0 && updates[0].y == 0 &&
              updates[0].width == 12 && updates[0].height == 12,
          "the image and its gutter to upload");
    muiAtlasGlyph again = {0};
    CHECK(Get(&fixture, FIRST_BOX, -7.0f, 3.0f, &again) == mui_success && again.u == 1 &&
              again.v == 1 && again.x == -7 && again.y == -5,
          "found again, placed for the new pen");
    CHECK(muiGlyphAtlas_TakeUpdates(fixture.atlas, NULL, 0, &count) == mui_success && count == 0,
          "nothing new to upload");
    // Beside it in the same plot, then the changes as one rectangle.
    CHECK(Get(&fixture, FIRST_BOX + 1, 0.0f, 0.0f, &glyph) == mui_success && glyph.u == 13 &&
              glyph.v == 1,
          "packed beside it");
    CHECK(Get(&fixture, FIRST_BOX + 2, 0.0f, 0.0f, &glyph) == mui_success && glyph.u == 1 &&
              glyph.v == 13,
          "under the first");
    CHECK(muiGlyphAtlas_TakeUpdates(fixture.atlas, updates, 4, &count) == mui_success &&
              count == 1 && updates[0].width == 24 && updates[0].height == 24,
          "a plot's changes as one rectangle");
    CHECK(Get(&fixture, SPACE, 5.0f, 5.0f, &glyph) == mui_success && glyph.width == 0 &&
              glyph.height == 0 &&
              muiGlyphAtlas_TakeUpdates(fixture.atlas, NULL, 0, &count) == mui_success &&
              count == 0,
          "a space has nothing to pack");
    Free(&fixture);
}

static void TestRounding(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    muiAtlasGlyph whole = {0};
    muiAtlasGlyph glyph = {0};
    CHECK(Get(&fixture, FIRST_BOX, 5.1f, 20.0f, &whole) == mui_success && whole.x == 5 &&
              whole.width == 10,
          "a tenth rounds to the pixel");
    CHECK(Get(&fixture, FIRST_BOX, 5.9f, 20.0f, &glyph) == mui_success && glyph.x == 6 &&
              glyph.u == whole.u && glyph.v == whole.v,
          "nine tenths round to the next pixel, the same image");
    CHECK(Get(&fixture, FIRST_BOX, 5.2f, 20.0f, &glyph) == mui_success && glyph.x == 5 &&
              glyph.width == 11 && glyph.u != whole.u,
          "a quarter is an image of its own");
    CHECK(Get(&fixture, FIRST_BOX, 5.0f, 20.5f, &glyph) == mui_success && glyph.y == 13,
          "the baseline to the nearest pixel");
    CHECK(Get(&fixture, FIRST_BOX, -5.1f, -20.4f, &glyph) == mui_success && glyph.x == -5 &&
              glyph.y == -28 && glyph.u == whole.u,
          "below 0 alike");
    muiAtlasGlyph sized = {0};
    CHECK(muiGlyphAtlas_Get(fixture.atlas, fixture.ahem, FIRST_BOX, 12.5f, 0.0f, 0.0f, &sized) ==
                  mui_success &&
              sized.width == 13 && sized.height == 13,
          "a size is an image of its own");
    Free(&fixture);
}

// Plots of 32 hold four images; a page of 64 holds four plots.
static void TestEviction(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    muiAtlasGlyph glyphs[16];
    bool placed = true;
    for (uint32_t i = 0; i < 16; i++)
    {
        placed = placed && Get(&fixture, FIRST_BOX + i, 0.0f, 0.0f, &glyphs[i]) == mui_success;
    }
    CHECK(placed && muiGlyphAtlas_GetPageCount(fixture.atlas) == 1, "a page holds sixteen");
    muiAtlasGlyph glyph = {0};
    CHECK(Get(&fixture, FIRST_BOX + 16, 0.0f, 0.0f, &glyph) == mui_errorCapacity,
          "every plot in use this frame");
    // Next frame: the first plot is used again, so the second, least
    // recently used, goes.
    muiGlyphAtlas_NextFrame(fixture.atlas);
    CHECK(Get(&fixture, FIRST_BOX, 0.0f, 0.0f, &glyph) == mui_success && glyph.u == glyphs[0].u &&
              glyph.v == glyphs[0].v,
          "the first kept");
    uint32_t count = 0;
    muiAtlasUpdate updates[8];
    CHECK(muiGlyphAtlas_TakeUpdates(fixture.atlas, updates, 8, &count) == mui_success && count == 4,
          "four plots changed");
    CHECK(Get(&fixture, FIRST_BOX + 16, 0.0f, 0.0f, &glyph) == mui_success &&
              glyph.u == glyphs[4].u && glyph.v == glyphs[4].v,
          "packed where the second plot's first was");
    CHECK(Pixel(&fixture, 0, glyphs[5].u, glyphs[5].v) == 0, "the rest of that plot cleared");
    CHECK(muiGlyphAtlas_TakeUpdates(fixture.atlas, updates, 8, &count) == mui_success &&
              count == 1 && updates[0].x == 32 && updates[0].y == 0 && updates[0].width == 12,
          "only the new image to upload");
    // The evicted glyphs are rendered again, into the room left.
    CHECK(Get(&fixture, FIRST_BOX + 5, 0.0f, 0.0f, &glyph) == mui_success &&
              glyph.u == glyphs[5].u && Pixel(&fixture, 0, glyph.u, glyph.v) == 255,
          "an evicted glyph comes back");
    CHECK(Get(&fixture, FIRST_BOX + 15, 0.0f, 0.0f, &glyph) == mui_success &&
              glyph.u == glyphs[15].u && glyph.v == glyphs[15].v,
          "the last plot kept");
    Free(&fixture);
}

// Hundreds of keys through sixteen places, over many frames: evicted
// entries pile up stale and are dropped when the table is rebuilt.
static void TestChurn(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    bool placed = true;
    for (uint32_t round = 0; round < 3; round++)
    {
        for (uint32_t glyph = FIRST_BOX; glyph <= LAST_BOX; glyph++)
        {
            muiAtlasGlyph out = {0};
            placed = placed && Get(&fixture, glyph, 0.0f, 0.0f, &out) == mui_success;
            if (glyph % 4 == 3)
            {
                muiGlyphAtlas_NextFrame(fixture.atlas);
            }
        }
    }
    muiAtlasGlyph first = {0};
    muiAtlasGlyph again = {0};
    CHECK(placed && Get(&fixture, LAST_BOX, 0.0f, 0.0f, &first) == mui_success &&
              Get(&fixture, LAST_BOX, 0.0f, 0.0f, &again) == mui_success && again.u == first.u &&
              again.v == first.v,
          "every glyph placed, the last found again");
    Free(&fixture);
}

static void TestPagesAndPlots(void)
{
    // A glyph larger than a plot less its gutter is not packed.
    Fixture fixture = Make(64, 16, 2, NULL);
    muiAtlasGlyph glyph = {0};
    CHECK(muiGlyphAtlas_Get(fixture.atlas, fixture.ahem, FIRST_BOX, 15.0f, 0.0f, 0.0f, &glyph) ==
                  mui_errorCapacity &&
              glyph.width == 15 && glyph.height == 15,
          "too large, its size told");
    CHECK(muiGlyphAtlas_Get(fixture.atlas, fixture.ahem, FIRST_BOX, 14.0f, 0.0f, 0.0f, &glyph) ==
                  mui_errorCapacity &&
              glyph.width == 14 && glyph.height == 15,
          "a pixel too tall");
    // Sixteen plots of one image a page, then a second page.
    bool placed = true;
    for (uint32_t i = 0; i < 17; i++)
    {
        placed = placed && Get(&fixture, FIRST_BOX + i, 0.0f, 0.0f, &glyph) == mui_success;
    }
    CHECK(placed && glyph.page == 1 && glyph.u == 1 && glyph.v == 1 &&
              muiGlyphAtlas_GetPageCount(fixture.atlas) == 2,
          "a second page");
    muiAtlasPage page = {0};
    CHECK(muiGlyphAtlas_GetPage(fixture.atlas, 1, &page) == mui_success && page.width == 64 &&
              page.height == 64 && page.pixels[64 + 1] == 255,
          "its pixels");
    CHECK(muiGlyphAtlas_GetPage(fixture.atlas, 2, &page) == mui_errorInvalid, "a page not made");
    Free(&fixture);
}

static void TestManyEntries(void)
{
    // 274 glyphs at 4 positions: the table is rebuilt several times.
    Fixture fixture = Make(1024, 256, 4, NULL);
    muiAtlasGlyph first[4];
    bool placed = true;
    for (uint32_t glyph = FIRST_BOX; glyph <= LAST_BOX; glyph++)
    {
        for (uint32_t quarter = 0; quarter < 4; quarter++)
        {
            muiAtlasGlyph out = {0};
            placed =
                placed && Get(&fixture, glyph, (float)quarter / 4.0f, 0.0f, &out) == mui_success;
            if (glyph == FIRST_BOX)
            {
                first[quarter] = out;
            }
        }
    }
    CHECK(placed, "every glyph placed");
    bool found = true;
    for (uint32_t quarter = 0; quarter < 4; quarter++)
    {
        muiAtlasGlyph out = {0};
        found = found &&
                Get(&fixture, FIRST_BOX, (float)quarter / 4.0f, 0.0f, &out) == mui_success &&
                out.page == first[quarter].page && out.u == first[quarter].u &&
                out.v == first[quarter].v;
    }
    CHECK(found, "the first ones found where they were");
    Free(&fixture);
}

static void TestContract(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    CHECK(def.pageWidth == 1024 && def.pageHeight == 1024 && def.plotWidth == 256 &&
              def.plotHeight == 256 && def.maxPages == 4,
          "the default def");
    muiGlyphAtlasDef bad[11];
    for (int i = 0; i < 11; i++)
    {
        bad[i] = def;
    }
    bad[0].cookie = 0;
    bad[1].pageWidth = 32;
    bad[2].pageHeight = 32768;
    bad[3].plotWidth = 8;
    bad[4].plotHeight = 8192;
    bad[5].plotWidth = 1000;
    bad[6].plotHeight = 2048;
    bad[7].maxPages = 0;
    bad[8].maxPages = 65;
    bad[9].pageWidth = 16384;
    bad[9].pageHeight = 16384;
    bad[9].plotWidth = 16;
    bad[9].plotHeight = 16;
    bad[10].pageHeight = 1000;
    muiGlyphAtlas* atlas = (muiGlyphAtlas*)&def;
    for (int i = 0; i < 11; i++)
    {
        CHECK(muiCreateGlyphAtlas(fixture.service, &bad[i], &atlas) == mui_errorInvalid &&
                  atlas == NULL,
              "a def refused");
    }
    CHECK(muiCreateGlyphAtlas(NULL, &def, &atlas) == mui_errorInvalid &&
              muiCreateGlyphAtlas(fixture.service, NULL, &atlas) == mui_errorInvalid &&
              muiCreateGlyphAtlas(fixture.service, &def, NULL) == mui_errorInvalid,
          "NULL arguments to create");
    muiGlyphAtlasDef edges = def;
    edges.pageWidth = 64;
    edges.pageHeight = 16384;
    edges.plotWidth = 16;
    edges.plotHeight = 4096;
    edges.maxPages = 64;
    CHECK(muiCreateGlyphAtlas(fixture.service, &edges, &atlas) == mui_success, "the edges");
    muiDestroyGlyphAtlas(atlas);
    muiAtlasGlyph glyph = {0};
    CHECK(muiGlyphAtlas_Get(NULL, fixture.ahem, FIRST_BOX, 10.0f, 0.0f, 0.0f, &glyph) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_Get(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 0.0f, 0.0f, NULL) ==
                  mui_errorInvalid,
          "NULL arguments to get");
    const float sizes[] = {0.0f, NAN, 4097.0f};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
        CHECK(muiGlyphAtlas_Get(fixture.atlas, fixture.ahem, FIRST_BOX, sizes[i], 0.0f, 0.0f,
                                &glyph) == mui_errorInvalid,
              "a size refused");
    }
    const float pens[] = {NAN, INFINITY, 16777218.0f, -16777218.0f};
    for (size_t i = 0; i < sizeof pens / sizeof pens[0]; i++)
    {
        CHECK(Get(&fixture, FIRST_BOX, pens[i], 0.0f, &glyph) == mui_errorInvalid &&
                  Get(&fixture, FIRST_BOX, 0.0f, pens[i], &glyph) == mui_errorInvalid,
              "a pen refused");
    }
    CHECK(Get(&fixture, FIRST_BOX, 16777216.0f, -16777216.0f, &glyph) == mui_success &&
              glyph.x == 16777216 && glyph.y == -16777224,
          "the farthest pen");
    CHECK(Get(&fixture, LAST_BOX + 1, 0.0f, 0.0f, &glyph) == mui_errorInvalid &&
              muiGlyphAtlas_Get(fixture.atlas, fixture.ahem + 1, FIRST_BOX, 10.0f, 0.0f, 0.0f,
                                &glyph) == mui_errorStale &&
              muiGlyphAtlas_Get(fixture.atlas, 0, FIRST_BOX, 10.0f, 0.0f, 0.0f, &glyph) ==
                  mui_errorStale,
          "a glyph past the font, a key of no font, no default font");
    uint32_t count = 0;
    muiAtlasUpdate update = {0};
    CHECK(muiGlyphAtlas_TakeUpdates(NULL, &update, 1, &count) == mui_errorInvalid &&
              muiGlyphAtlas_TakeUpdates(fixture.atlas, &update, 1, NULL) == mui_errorInvalid &&
              muiGlyphAtlas_TakeUpdates(fixture.atlas, NULL, 1, &count) == mui_errorInvalid,
          "NULL arguments to take updates");
    // Two plots changed: one rectangle is too few, and none are taken.
    for (uint32_t i = 0; i < 5; i++)
    {
        (void)Get(&fixture, FIRST_BOX + 1 + i, 0.0f, 0.0f, &glyph);
    }
    CHECK(muiGlyphAtlas_TakeUpdates(fixture.atlas, &update, 1, &count) == mui_errorCapacity &&
              count == 2,
          "too few to take");
    muiAtlasUpdate updates[2];
    CHECK(muiGlyphAtlas_TakeUpdates(fixture.atlas, updates, 2, &count) == mui_success &&
              count == 2 && updates[1].x == 32,
          "then taken");
    muiAtlasPage page = {0};
    CHECK(muiGlyphAtlas_GetPage(NULL, 0, &page) == mui_errorInvalid &&
              muiGlyphAtlas_GetPage(fixture.atlas, 0, NULL) == mui_errorInvalid &&
              muiGlyphAtlas_GetPageCount(NULL) == 0,
          "NULL arguments to pages");
    muiGlyphAtlas_NextFrame(NULL);
    muiDestroyGlyphAtlas(NULL);
    Free(&fixture);
}

static void TestFields(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    // The box at 10 pixels with a spread of 2: 14 by 14, from 2 left of
    // the pen and 10 above the baseline.
    unsigned char field[14 * 14];
    muiGlyphImage image = {0};
    CHECK(muiRenderGlyphField(fixture.service, fixture.ahem, FIRST_BOX, 10.0f, 2, &image, field,
                              sizeof field) == mui_success &&
              image.width == 14 && image.height == 14,
          "the field alone");
    muiAtlasGlyph glyph = {0};
    CHECK(muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 2, &glyph) ==
                  mui_success &&
              glyph.u == 1 && glyph.v == 1 && glyph.width == 14 && glyph.height == 14 &&
              glyph.x == -2 && glyph.y == -10,
          "placed from the pen and baseline at its size");
    bool same = true;
    for (uint32_t y = 0; y < 14; y++)
    {
        for (uint32_t x = 0; x < 14; x++)
        {
            same = same && Pixel(&fixture, 0, x + 1, y + 1) == field[y * 14 + x];
        }
    }
    CHECK(same && Pixel(&fixture, 0, 0, 0) == 0 && Pixel(&fixture, 0, 15, 8) == 0 &&
              Pixel(&fixture, 0, 8, 15) == 0,
          "the field's bytes and an empty gutter");
    muiAtlasGlyph again = {0};
    CHECK(muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 2, &again) ==
                  mui_success &&
              again.u == 1 && again.v == 1,
          "found again");
    muiAtlasGlyph other = {0};
    CHECK(Get(&fixture, FIRST_BOX, 0.0f, 0.0f, &other) == mui_success && other.u == 17 &&
              other.width == 10 && Pixel(&fixture, 0, 17, 1) == 255,
          "coverage of the same glyph apart");
    // Each in a plot of its own: 18 and 17 pixels with their gutters.
    CHECK(muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 3, &other) ==
                  mui_success &&
              other.u == 33 && other.v == 1 && other.width == 16,
          "another spread apart");
    CHECK(muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.5f, 2, &other) ==
                  mui_success &&
              other.u == 1 && other.v == 33 && other.width == 15,
          "another size apart");
    other = (muiAtlasGlyph){7, 7, 7, 7, 7, 7, 7};
    CHECK(muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 32, &other) ==
                  mui_errorCapacity &&
              other.width == 74 && other.height == 74 && other.page == 0 && other.u == 0 &&
              other.v == 0 && other.x == 0 && other.y == 0,
          "larger than a plot, its size told");
    CHECK(muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, SPACE, 10.0f, 2, &other) ==
                  mui_success &&
              other.width == 0,
          "a space has no field");
    CHECK(muiGlyphAtlas_GetField(NULL, fixture.ahem, FIRST_BOX, 10.0f, 2, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 2, NULL) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 0.0f, 2, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 1, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 33, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem, 100000, 10.0f, 2, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem + 1, FIRST_BOX, 10.0f, 2,
                                     &other) == mui_errorStale &&
              muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem + 1, FIRST_BOX, 10.0f, 1,
                                     &other) == mui_errorInvalid &&
              muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem + 1, FIRST_BOX, 10.0f, 33,
                                     &other) == mui_errorInvalid,
          "fields outside the contract");
    Free(&fixture);
}

// A four-byte pixel of a page.
static const unsigned char* Pixel4(muiGlyphAtlas* atlas, uint32_t x, uint32_t y)
{
    muiAtlasPage view = {0};
    CHECK(muiGlyphAtlas_GetPage(atlas, 0, &view) == mui_success &&
              view.format == mui_atlasFourChannel,
          "a page of four channels");
    return view.pixels + ((size_t)y * view.width + x) * 4;
}

static bool IsEmpty4(muiGlyphAtlas* atlas, uint32_t x, uint32_t y)
{
    const unsigned char* p = Pixel4(atlas, x, y);
    return p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0;
}

// An atlas of four channels beside one of one channel over the same
// service: multi-channel fields packed four bytes a pixel, each kind
// refused by the other atlas, and an emptied plot cleared in every
// channel.
static void TestMultiFields(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    def.pageWidth = 64;
    def.pageHeight = 64;
    def.plotWidth = 32;
    def.plotHeight = 32;
    def.maxPages = 1;
    def.format = (muiAtlasFormat)3;
    muiGlyphAtlas* atlas = NULL;
    CHECK(muiCreateGlyphAtlas(fixture.service, &def, &atlas) == mui_errorInvalid,
          "an unknown format");
    def.format = mui_atlasFourChannel;
    CHECK(muiCreateGlyphAtlas(fixture.service, &def, &atlas) == mui_success, "four channels");
    unsigned char field[14 * 14 * 4];
    muiGlyphImage image = {0};
    CHECK(muiRenderGlyphMultiField(fixture.service, fixture.ahem, FIRST_BOX, 10.0f, 2, &image,
                                   field, sizeof field) == mui_success &&
              image.width == 14,
          "the field alone");
    muiAtlasGlyph glyph = {0};
    CHECK(muiGlyphAtlas_GetMultiField(atlas, fixture.ahem, FIRST_BOX, 10.0f, 2, &glyph) ==
                  mui_success &&
              glyph.u == 1 && glyph.v == 1 && glyph.width == 14 && glyph.height == 14 &&
              glyph.x == -2 && glyph.y == -10,
          "placed as a field");
    bool same = true;
    for (uint32_t y = 0; y < 14; y++)
    {
        for (uint32_t x = 0; x < 14; x++)
        {
            same = same && memcmp(Pixel4(atlas, x + 1, y + 1), &field[(y * 14 + x) * 4], 4) == 0;
        }
    }
    CHECK(same && IsEmpty4(atlas, 0, 0) && IsEmpty4(atlas, 15, 8) && IsEmpty4(atlas, 8, 15),
          "the field's bytes and an empty gutter");
    muiAtlasPage page = {0};
    muiAtlasGlyph other = {0};
    CHECK(Get(&fixture, FIRST_BOX, 0.0f, 0.0f, &other) == mui_success &&
              muiGlyphAtlas_GetPage(fixture.atlas, 0, &page) == mui_success &&
              page.format == mui_atlasOneChannel,
          "coverage in the atlas of one channel");
    CHECK(muiGlyphAtlas_GetMultiField(fixture.atlas, fixture.ahem, FIRST_BOX, 10.0f, 2, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetField(atlas, fixture.ahem, FIRST_BOX, 10.0f, 2, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_Get(atlas, fixture.ahem, FIRST_BOX, 10.0f, 0.0f, 0.0f, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetMultiField(NULL, fixture.ahem, FIRST_BOX, 10.0f, 2, &other) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetMultiField(atlas, fixture.ahem, FIRST_BOX, 10.0f, 1, &other) ==
                  mui_errorInvalid,
          "each kind refused by the other atlas");
    // In a later frame, fields of 24 to 27 pixels with their gutters, one
    // to a plot, leave no room: a small one empties a plot, and what the
    // field there left past the small one's 10 pixels is cleared in every
    // channel.
    muiGlyphAtlas_NextFrame(atlas);
    const float sizes[4] = {10.0f, 11.0f, 12.0f, 13.0f};
    for (uint32_t i = 0; i < 4; i++)
    {
        CHECK(muiGlyphAtlas_GetMultiField(atlas, fixture.ahem, FIRST_BOX, sizes[i], 6, &other) ==
                      mui_success &&
                  other.width == 22 + i,
              "a plot filled");
    }
    muiGlyphAtlas_NextFrame(atlas);
    CHECK(muiGlyphAtlas_GetMultiField(atlas, fixture.ahem, FIRST_BOX, 4.0f, 2, &other) ==
                  mui_success &&
              other.width == 8 && (other.u - 1) % 32 == 0 && (other.v - 1) % 32 == 0,
          "a plot emptied and packed again");
    bool cleared = true;
    for (uint32_t y = 0; y < 26; y++)
    {
        for (uint32_t x = 10; x < 26; x++)
        {
            cleared = cleared && IsEmpty4(atlas, other.u - 1 + x, other.v - 1 + y);
        }
    }
    CHECK(cleared, "the old field cleared in every channel");
    muiDestroyGlyphAtlas(atlas);
    Free(&fixture);
}

// An atlas of colour glyphs: MaulColor.ttf's A packed as
// muiRenderColorGlyph renders it, its B and Ahem's box (Ahem has no COLR
// table) empty, each tint an image of its own, and each kind refused by
// the other atlases.
static void TestColorGlyphs(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = s_color;
    fontDef.size = sizeof s_color;
    fontDef.dataMode = mui_fontDataBorrow;
    muiFontId colorFont = {0, 0};
    CHECK(muiCreateFont(fixture.service, &fontDef, &colorFont) == mui_success, "Maul Color");
    uint64_t color = muiFont_GetKey(colorFont);
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    def.pageWidth = 64;
    def.pageHeight = 64;
    def.plotWidth = 32;
    def.plotHeight = 32;
    def.maxPages = 1;
    def.format = mui_atlasColor;
    muiGlyphAtlas* atlas = NULL;
    CHECK(muiCreateGlyphAtlas(fixture.service, &def, &atlas) == mui_success, "colour glyphs");
    const muiLinearColor green = {0.0f, 1.0f, 0.0f, 1.0f};
    unsigned char image[8 * 8 * 4];
    muiGlyphImage placed = {0};
    CHECK(muiRenderColorGlyph(fixture.service, color, 1, 10.0f, 0.0f, 0, green, &placed, image,
                              sizeof image) == mui_success,
          "the glyph alone");
    muiAtlasGlyph glyph = {0};
    CHECK(muiGlyphAtlas_GetColor(atlas, color, 1, 10.0f, 2.0f, 20.0f, 0, green, &glyph) ==
                  mui_success &&
              glyph.u == 1 && glyph.v == 1 && glyph.width == 8 && glyph.height == 8 &&
              glyph.x == 3 && glyph.y == 12,
          "placed as coverage is");
    muiAtlasPage page = {0};
    CHECK(muiGlyphAtlas_GetPage(atlas, 0, &page) == mui_success && page.format == mui_atlasColor,
          "a page of colour glyphs");
    bool same = true;
    for (uint32_t y = 0; y < 8; y++)
    {
        same = same && memcmp(page.pixels + ((size_t)(y + 1) * page.width + 1) * 4,
                              image + (size_t)y * 8 * 4, 8 * 4) == 0;
    }
    CHECK(same, "the glyph's bytes");
    muiAtlasGlyph again = {0};
    CHECK(muiGlyphAtlas_GetColor(atlas, color, 1, 10.0f, 2.0f, 20.0f, 0, green, &again) ==
                  mui_success &&
              again.u == 1 && again.v == 1,
          "the same tint found again");
    const muiLinearColor blue = {0.0f, 0.0f, 1.0f, 1.0f};
    CHECK(muiGlyphAtlas_GetColor(atlas, color, 1, 10.0f, 2.0f, 20.0f, 0, blue, &again) ==
                  mui_success &&
              (again.u != 1 || again.v != 1),
          "another tint an image of its own");
    CHECK(muiGlyphAtlas_GetColor(atlas, color, 1, 10.0f, 2.0f, 20.0f, 1, green, &again) ==
                  mui_success &&
              (again.u != 1 || again.v != 1),
          "another palette an image of its own");
    glyph = (muiAtlasGlyph){7, 7, 7, 7, 7, 7, 7};
    CHECK(muiGlyphAtlas_GetColor(atlas, color, 2, 10.0f, 0.0f, 0.0f, 0, green, &glyph) ==
                  mui_empty &&
              glyph.width == 0 && glyph.page == 0,
          "a glyph without colour layers empty");
    CHECK(muiGlyphAtlas_GetColor(atlas, fixture.ahem, FIRST_BOX, 10.0f, 0.0f, 0.0f, 0, green,
                                 &glyph) == mui_empty,
          "a font without COLR empty");
    CHECK(muiGlyphAtlas_Get(atlas, color, 1, 10.0f, 0.0f, 0.0f, &glyph) == mui_errorInvalid &&
              muiGlyphAtlas_GetField(atlas, color, 1, 10.0f, 2, &glyph) == mui_errorInvalid &&
              muiGlyphAtlas_GetMultiField(atlas, color, 1, 10.0f, 2, &glyph) == mui_errorInvalid &&
              muiGlyphAtlas_GetColor(fixture.atlas, color, 1, 10.0f, 0.0f, 0.0f, 0, green,
                                     &glyph) == mui_errorInvalid &&
              muiGlyphAtlas_GetColor(NULL, color, 1, 10.0f, 0.0f, 0.0f, 0, green, &glyph) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetColor(atlas, color, 1, 10.0f, 0.0f, 0.0f, 0, green, NULL) ==
                  mui_errorInvalid &&
              muiGlyphAtlas_GetColor(atlas, color + 1, 1, 10.0f, 0.0f, 0.0f, 0, green, &glyph) ==
                  mui_errorStale,
          "each kind refused by the other atlases");
    muiDestroyGlyphAtlas(atlas);
    Free(&fixture);
}

// Many tints of one glyph, each in both palettes, in a larger atlas of
// colour glyphs, enough that their keys share probe chains: each found
// with its own. The text's colour in each image (its small box, at 4, 3
// from the image's left and bottom) is the tint's and its first layer (at
// 1, 8) the palette's, as a glyph rendered alone has them, the tint kept
// as 8-bit sRGB with straight alpha; every other tint is half clear.
static void TestTints(void)
{
    Fixture fixture = Make(64, 32, 1, NULL);
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = s_color;
    fontDef.size = sizeof s_color;
    fontDef.dataMode = mui_fontDataBorrow;
    muiFontId colorFont = {0, 0};
    CHECK(muiCreateFont(fixture.service, &fontDef, &colorFont) == mui_success, "Maul Color");
    uint64_t color = muiFont_GetKey(colorFont);
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    def.pageWidth = 256;
    def.pageHeight = 256;
    def.plotWidth = 64;
    def.plotHeight = 64;
    def.maxPages = 1;
    def.format = mui_atlasColor;
    muiGlyphAtlas* atlas = NULL;
    CHECK(muiCreateGlyphAtlas(fixture.service, &def, &atlas) == mui_success, "colour glyphs");
    enum
    {
        TINTS = 32
    };
    // Every key twice: the second time each is found, not packed.
    bool right = true;
    for (int pass = 0; pass < 2; pass++)
    {
        for (int k = 0; k < TINTS * 2; k++)
        {
            float grey = (float)(k / 2 + 1) / (float)(TINTS + 1);
            float alpha = k / 2 % 2 == 0 ? 1.0f : 0.5f;
            const muiLinearColor tint = {grey * alpha, grey * alpha, grey * alpha, alpha};
            uint32_t palette = (uint32_t)k % 2;
            unsigned char alone[8 * 8 * 4];
            muiGlyphImage image = {0};
            muiAtlasGlyph glyph = {0};
            muiAtlasPage page = {0};
            bool got = muiRenderColorGlyph(fixture.service, color, 1, 10.0f, 0.0f, palette, tint,
                                           &image, alone, sizeof alone) == mui_success &&
                       muiGlyphAtlas_GetColor(atlas, color, 1, 10.0f, 0.0f, 0.0f, palette, tint,
                                              &glyph) == mui_success &&
                       muiGlyphAtlas_GetPage(atlas, 0, &page) == mui_success;
            // Rows 4 and 0 from the top, columns 3 and 0.
            const size_t rows[2] = {4, 0};
            const size_t columns[2] = {3, 0};
            for (int at = 0; at < 2 && got; at++)
            {
                const unsigned char* packed =
                    page.pixels + ((glyph.v + rows[at]) * page.width + glyph.u + columns[at]) * 4;
                const unsigned char* expected = &alone[(rows[at] * 8 + columns[at]) * 4];
                for (int c = 0; c < 4; c++)
                {
                    right = right && abs(packed[c] - expected[c]) <= 1;
                }
            }
            right = right && got;
        }
    }
    CHECK(right, "each tint and palette its own image");
    muiDestroyGlyphAtlas(atlas);
    Free(&fixture);
}

static void TestMemoryRunningOut(void)
{
    for (int failAt = 1; failAt < 40; failAt++)
    {
        FailingAllocator failing = {0, 0};
        Fixture fixture = Make(64, 32, 2, &failing);
        if (fixture.atlas == NULL)
        {
            CHECK(fixture.service != NULL, "the service and font");
            Free(&fixture);
            continue;
        }
        failing = (FailingAllocator){0, failAt};
        bool sound = true;
        for (uint32_t i = 0; i < 40; i++)
        {
            muiAtlasGlyph glyph = {0};
            muiResult result = i % 3 == 2
                                   ? muiGlyphAtlas_GetField(fixture.atlas, fixture.ahem,
                                                            FIRST_BOX + i % 20, 6.0f, 2, &glyph)
                                   : Get(&fixture, FIRST_BOX + i % 20, 0.0f, 0.0f, &glyph);
            sound = sound && (result == mui_success || result == mui_errorCapacity);
        }
        CHECK(sound, "placed or out of memory");
        // Once memory is back, every glyph is found in a new frame.
        failing.failAt = 0;
        muiGlyphAtlas_NextFrame(fixture.atlas);
        for (uint32_t i = 0; i < 20; i++)
        {
            muiAtlasGlyph glyph = {0};
            sound = sound && Get(&fixture, FIRST_BOX + i, 0.0f, 0.0f, &glyph) == mui_success &&
                    glyph.width == 10;
        }
        CHECK(sound, "then all of them");
        Free(&fixture);
    }
    // Creating the atlas itself.
    FailingAllocator failing = {0, 0};
    Fixture fixture = Make(64, 32, 1, &failing);
    muiDestroyGlyphAtlas(fixture.atlas);
    failing.failAt = failing.allocations + 1;
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    muiGlyphAtlas* atlas = NULL;
    CHECK(muiCreateGlyphAtlas(fixture.service, &def, &atlas) == mui_errorCapacity && atlas == NULL,
          "no memory for the atlas");
    muiDestroyTextService(fixture.service);
}

int main(void)
{
    TestPacked();
    TestRounding();
    TestEviction();
    TestChurn();
    TestPagesAndPlots();
    TestManyEntries();
    TestContract();
    TestFields();
    TestMultiFields();
    TestColorGlyphs();
    TestTints();
    TestMemoryRunningOut();
    return s_failures == 0 ? 0 : 1;
}
