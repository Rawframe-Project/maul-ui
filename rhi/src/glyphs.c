// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The renderer's glyphs (record mui-0005), drawn with Maul UI's glyph
// atlases where Maul UI has its text component (MUI_RHI_TEXT).

#include "glyphs.h"

#include "allocator.h"
#include "streams.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"

#include <stdalign.h>
#include <string.h>

#if MUI_RHI_TEXT

#include "maul-ui/glyph_atlas.h"

enum
{
    // The atlases: coverage, and multi-channel fields.
    COVERAGE = 0,
    FIELDS = 1
};

muiResult muiRhiMakeGlyphs(muiRhiGlyphs* glyphs, const muiAllocator* allocator, mrhiDevice* device,
                           muiTextService* text)
{
    *glyphs = (muiRhiGlyphs){.allocator = *allocator, .device = device};
    if (text == nullptr)
    {
        return mui_success;
    }
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    muiResult status = muiCreateGlyphAtlas(text, &def, &glyphs->atlases[COVERAGE]);
    def.format = mui_atlasFourChannel;
    status =
        status == mui_success ? muiCreateGlyphAtlas(text, &def, &glyphs->atlases[FIELDS]) : status;
    if (status != mui_success)
    {
        muiRhiFreeGlyphs(glyphs);
    }
    return status;
}

void muiRhiFreeGlyphs(muiRhiGlyphs* glyphs)
{
    for (uint32_t i = 0; i < glyphs->madeCount; i++)
    {
        (void)mrhiDestroyTexture(glyphs->device, glyphs->pages[i]);
    }
    muiRhiRelease(&glyphs->allocator, glyphs->updates,
                  (size_t)glyphs->updateCapacity * sizeof(muiAtlasUpdate), alignof(muiAtlasUpdate));
    muiDestroyGlyphAtlas(glyphs->atlases[FIELDS]);
    muiDestroyGlyphAtlas(glyphs->atlases[COVERAGE]);
    *glyphs = (muiRhiGlyphs){0};
}

void muiRhiNextGlyphFrame(muiRhiGlyphs* glyphs)
{
    muiGlyphAtlas_NextFrame(glyphs->atlases[COVERAGE]);
    muiGlyphAtlas_NextFrame(glyphs->atlases[FIELDS]);
}

// An atlas page's number here, numbering it and the atlas's pages before
// it first; false when the pages here are all used.
static bool Number(muiRhiGlyphs* glyphs, uint32_t atlas, uint32_t page, uint32_t* numberOut)
{
    while (glyphs->numbered[atlas] <= page)
    {
        if (glyphs->pageCount >= MUI_RHI_MAX_PAGES)
        {
            return false;
        }
        uint32_t number = glyphs->pageCount++;
        glyphs->pageAtlas[number] = (uint8_t)atlas;
        glyphs->atlasPage[number] = (uint8_t)glyphs->numbered[atlas];
        glyphs->pageOf[atlas][glyphs->numbered[atlas]++] = (uint8_t)number;
    }
    *numberOut = glyphs->pageOf[atlas][page];
    return true;
}

// A glyph an atlas gave, its page numbered here, with the page's size;
// one of no width has no image and no page.
static bool Found(muiRhiGlyphs* glyphs, uint32_t atlas, muiResult got, const muiAtlasGlyph* glyph,
                  muiRhiGlyph* glyphOut)
{
    muiAtlasPage page = {0};
    uint32_t number = 0;
    if (got != mui_success ||
        (glyph->width != 0 &&
         (muiGlyphAtlas_GetPage(glyphs->atlases[atlas], glyph->page, &page) != mui_success ||
          !Number(glyphs, atlas, glyph->page, &number))))
    {
        return false;
    }
    *glyphOut = (muiRhiGlyph){number,     glyph->u,    glyph->v, glyph->width, glyph->height,
                              page.width, page.height, glyph->x, glyph->y};
    return true;
}

bool muiRhiGetGlyph(muiRhiGlyphs* glyphs, uint64_t font, uint32_t id, float pixelSize, float penX,
                    float baselineY, muiRhiGlyph* glyphOut)
{
    muiAtlasGlyph glyph = {0};
    muiGlyphAtlas* atlas = glyphs->atlases[COVERAGE];
    return atlas != nullptr &&
           Found(glyphs, COVERAGE,
                 muiGlyphAtlas_Get(atlas, font, id, pixelSize, penX, baselineY, &glyph), &glyph,
                 glyphOut);
}

bool muiRhiGetGlyphField(muiRhiGlyphs* glyphs, uint64_t font, uint32_t id, float pixelSize,
                         uint32_t spread, muiRhiGlyph* glyphOut)
{
    muiAtlasGlyph glyph = {0};
    muiGlyphAtlas* atlas = glyphs->atlases[FIELDS];
    return atlas != nullptr &&
           Found(glyphs, FIELDS,
                 muiGlyphAtlas_GetMultiField(atlas, font, id, pixelSize, spread, &glyph), &glyph,
                 glyphOut);
}

// Textures for the pages the atlas made since the last frame.
static muiResult MakePages(muiRhiGlyphs* glyphs)
{
    // Pages no glyph was got from yet are numbered first.
    for (uint32_t atlas = 0; atlas < 2; atlas++)
    {
        uint32_t count = muiGlyphAtlas_GetPageCount(glyphs->atlases[atlas]);
        uint32_t number = 0;
        if (count > 0 && !Number(glyphs, atlas, count - 1, &number))
        {
            return mui_errorCapacity;
        }
    }
    for (; glyphs->madeCount < glyphs->pageCount; glyphs->madeCount++)
    {
        uint32_t atlas = glyphs->pageAtlas[glyphs->madeCount];
        muiAtlasPage page = {0};
        if (muiGlyphAtlas_GetPage(glyphs->atlases[atlas], glyphs->atlasPage[glyphs->madeCount],
                                  &page) != mui_success)
        {
            return mui_errorPlatform;
        }
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format =
            page.format == mui_atlasFourChannel ? mrhi_formatRgba8Unorm : mrhi_formatR8Unorm;
        def.usage = mrhi_textureSampled | mrhi_textureCopyDestination;
        def.width = page.width;
        def.height = page.height;
        if (mrhiCreateTexture(glyphs->device, &def, &glyphs->pages[glyphs->madeCount]) !=
            mrhi_success)
        {
            return mui_errorPlatform;
        }
    }
    return mui_success;
}

// Room for a number of changed rectangles, those pending kept.
static bool Reserve(muiRhiGlyphs* glyphs, uint32_t needed)
{
    if (needed <= glyphs->updateCapacity)
    {
        return true;
    }
    uint32_t capacity = glyphs->updateCapacity == 0 ? 64 : glyphs->updateCapacity;
    while (capacity < needed)
    {
        capacity *= 2;
    }
    muiAtlasUpdate* updates = muiRhiAllocate(
        &glyphs->allocator, (size_t)capacity * sizeof(muiAtlasUpdate), alignof(muiAtlasUpdate));
    if (updates == nullptr)
    {
        return false;
    }
    if (glyphs->updateCount > 0)
    {
        memcpy(updates, glyphs->updates, (size_t)glyphs->updateCount * sizeof(muiAtlasUpdate));
    }
    muiRhiRelease(&glyphs->allocator, glyphs->updates,
                  (size_t)glyphs->updateCapacity * sizeof(muiAtlasUpdate), alignof(muiAtlasUpdate));
    glyphs->updates = updates;
    glyphs->updateCapacity = capacity;
    return true;
}

// The atlas's changed rectangles after those pending, the ones the last
// frame wrote dropped first.
static muiResult TakeUpdates(muiRhiGlyphs* glyphs)
{
    muiAtlasUpdate* updates = glyphs->updates;
    uint32_t pending = glyphs->updateCount - glyphs->writtenCount;
    if (glyphs->writtenCount > 0 && pending > 0)
    {
        memmove(updates, updates + glyphs->writtenCount, (size_t)pending * sizeof(muiAtlasUpdate));
    }
    glyphs->updateCount = pending;
    glyphs->writtenCount = 0;
    for (uint32_t atlas = 0; atlas < 2; atlas++)
    {
        uint32_t count = 0;
        while (muiGlyphAtlas_TakeUpdates(
                   glyphs->atlases[atlas], (muiAtlasUpdate*)glyphs->updates + glyphs->updateCount,
                   glyphs->updateCapacity - glyphs->updateCount, &count) == mui_errorCapacity)
        {
            if (!Reserve(glyphs, glyphs->updateCount + count))
            {
                return mui_errorCapacity;
            }
        }
        // Their pages, all numbered by MakePages, as numbered here.
        muiAtlasUpdate* taken = (muiAtlasUpdate*)glyphs->updates + glyphs->updateCount;
        for (uint32_t i = 0; i < count; i++)
        {
            taken[i].page = glyphs->pageOf[atlas][taken[i].page];
        }
        glyphs->updateCount += count;
    }
    return mui_success;
}

muiResult muiRhiPrepareGlyphs(muiRhiGlyphs* glyphs)
{
    if (glyphs->atlases[COVERAGE] == nullptr)
    {
        return mui_success;
    }
    muiResult status = MakePages(glyphs);
    status = status == mui_success ? TakeUpdates(glyphs) : status;
    for (uint32_t i = 0; i < glyphs->madeCount && status == mui_success; i++)
    {
        if (mrhiImportTexture(glyphs->device, glyphs->pages[i], &glyphs->resources[i]) !=
            mrhi_success)
        {
            status = mui_errorPlatform;
        }
    }
    return status;
}

// The bytes a pixel of a page numbered here.
static uint32_t PixelBytes(const muiRhiGlyphs* glyphs, uint32_t page)
{
    return glyphs->pageAtlas[page] == FIELDS ? 4u : 1u;
}

uint64_t muiRhiGlyphUploadBytes(const muiRhiGlyphs* glyphs)
{
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < glyphs->updateCount; i++)
    {
        const muiAtlasUpdate* update = &((const muiAtlasUpdate*)glyphs->updates)[i];
        // Rows at a 256-byte pitch, the whole at an upload block.
        uint64_t row = (uint64_t)update->width * PixelBytes(glyphs, update->page);
        uint64_t rows = (uint64_t)update->height * ((row + 255u) / 256u * 256u);
        bytes += (rows + MUI_RHI_UPLOAD_BLOCK - 1) / MUI_RHI_UPLOAD_BLOCK * MUI_RHI_UPLOAD_BLOCK;
    }
    return bytes;
}

void muiRhiGlyphsWritten(muiRhiGlyphs* glyphs)
{
    glyphs->writtenCount = glyphs->updateCount;
}

void muiRhiForgetGlyphs(muiRhiGlyphs* glyphs)
{
    glyphs->writtenCount = 0;
}

uint32_t muiRhiUpdatedPage(const muiRhiGlyphs* glyphs, uint32_t update)
{
    uint32_t page = ((const muiAtlasUpdate*)glyphs->updates)[update].page;
    return page < glyphs->pageCount ? page : 0;
}

bool muiRhiWriteGlyphs(const muiRhiGlyphs* glyphs, mrhiPassId pass)
{
    for (uint32_t i = 0; i < glyphs->updateCount; i++)
    {
        const muiAtlasUpdate* update = &((const muiAtlasUpdate*)glyphs->updates)[i];
        muiAtlasPage page = {0};
        if (update->page >= glyphs->madeCount ||
            muiGlyphAtlas_GetPage(glyphs->atlases[glyphs->pageAtlas[update->page]],
                                  glyphs->atlasPage[update->page], &page) != mui_success)
        {
            return false;
        }
        size_t pixel = PixelBytes(glyphs, update->page);
        const mrhiTextureCopy into = {
            .resource = glyphs->resources[update->page], .x = update->x, .y = update->y};
        const mrhiTexelLayout layout = {.bytesPerRow = (uint32_t)(page.width * pixel)};
        const mrhiExtent3d size = {update->width, update->height, 1};
        const unsigned char* first =
            page.pixels + ((size_t)update->y * page.width + update->x) * pixel;
        size_t bytes = ((size_t)(update->height - 1) * page.width + update->width) * pixel;
        if (mrhiWriteTexture(glyphs->device, pass, &into, first, bytes, &layout, &size) !=
            mrhi_success)
        {
            return false;
        }
    }
    return true;
}

#else

muiResult muiRhiMakeGlyphs(muiRhiGlyphs* glyphs, const muiAllocator* allocator, mrhiDevice* device,
                           muiTextService* text)
{
    *glyphs = (muiRhiGlyphs){.allocator = *allocator, .device = device};
    return text == nullptr ? mui_success : mui_errorInvalid;
}

void muiRhiFreeGlyphs(muiRhiGlyphs* glyphs)
{
    *glyphs = (muiRhiGlyphs){0};
}

void muiRhiNextGlyphFrame(muiRhiGlyphs* glyphs)
{
    (void)glyphs;
}

bool muiRhiGetGlyph(muiRhiGlyphs* glyphs, uint64_t font, uint32_t id, float pixelSize, float penX,
                    float baselineY, muiRhiGlyph* glyphOut)
{
    (void)glyphs;
    (void)font;
    (void)id;
    (void)pixelSize;
    (void)penX;
    (void)baselineY;
    (void)glyphOut;
    return false;
}

bool muiRhiGetGlyphField(muiRhiGlyphs* glyphs, uint64_t font, uint32_t id, float pixelSize,
                         uint32_t spread, muiRhiGlyph* glyphOut)
{
    (void)glyphs;
    (void)font;
    (void)id;
    (void)pixelSize;
    (void)spread;
    (void)glyphOut;
    return false;
}

muiResult muiRhiPrepareGlyphs(muiRhiGlyphs* glyphs)
{
    (void)glyphs;
    return mui_success;
}

uint64_t muiRhiGlyphUploadBytes(const muiRhiGlyphs* glyphs)
{
    (void)glyphs;
    return 0;
}

void muiRhiGlyphsWritten(muiRhiGlyphs* glyphs)
{
    (void)glyphs;
}

void muiRhiForgetGlyphs(muiRhiGlyphs* glyphs)
{
    (void)glyphs;
}

uint32_t muiRhiUpdatedPage(const muiRhiGlyphs* glyphs, uint32_t update)
{
    (void)glyphs;
    (void)update;
    return 0;
}

bool muiRhiWriteGlyphs(const muiRhiGlyphs* glyphs, mrhiPassId pass)
{
    (void)glyphs;
    (void)pass;
    return true;
}

#endif
