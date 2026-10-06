// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The renderer's glyphs (record mui-0005), drawn with Maul UI's glyph
// atlas where Maul UI has its text component (MUI_RHI_TEXT).

#include "glyphs.h"

#include "allocator.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"

#include <stdalign.h>

#if MUI_RHI_TEXT

#include "maul-ui/glyph_atlas.h"

muiResult muiRhiMakeGlyphs(muiRhiGlyphs* glyphs, const muiAllocator* allocator, mrhiDevice* device,
                           muiTextService* text)
{
    *glyphs = (muiRhiGlyphs){.allocator = *allocator, .device = device};
    if (text == nullptr)
    {
        return mui_success;
    }
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    return muiCreateGlyphAtlas(text, &def, &glyphs->atlas);
}

void muiRhiFreeGlyphs(muiRhiGlyphs* glyphs)
{
    for (uint32_t i = 0; i < glyphs->pageCount; i++)
    {
        (void)mrhiDestroyTexture(glyphs->device, glyphs->pages[i]);
    }
    muiRhiRelease(&glyphs->allocator, glyphs->updates,
                  (size_t)glyphs->updateCapacity * sizeof(muiAtlasUpdate), alignof(muiAtlasUpdate));
    muiDestroyGlyphAtlas(glyphs->atlas);
    *glyphs = (muiRhiGlyphs){0};
}

void muiRhiNextGlyphFrame(muiRhiGlyphs* glyphs)
{
    muiGlyphAtlas_NextFrame(glyphs->atlas);
}

bool muiRhiGetGlyph(muiRhiGlyphs* glyphs, uint64_t font, uint32_t id, float pixelSize, float penX,
                    float baselineY, muiRhiGlyph* glyphOut)
{
    muiAtlasGlyph glyph = {0};
    muiAtlasPage page = {0};
    if (glyphs->atlas == nullptr ||
        muiGlyphAtlas_Get(glyphs->atlas, font, id, pixelSize, penX, baselineY, &glyph) !=
            mui_success ||
        glyph.width == 0 || muiGlyphAtlas_GetPage(glyphs->atlas, glyph.page, &page) != mui_success)
    {
        return false;
    }
    *glyphOut = (muiRhiGlyph){glyph.page, glyph.u,     glyph.v, glyph.width, glyph.height,
                              page.width, page.height, glyph.x, glyph.y};
    return true;
}

// Textures for the pages the atlas made since the last frame.
static muiResult MakePages(muiRhiGlyphs* glyphs)
{
    uint32_t count = muiGlyphAtlas_GetPageCount(glyphs->atlas);
    while (glyphs->pageCount < count && glyphs->pageCount < MUI_RHI_MAX_PAGES)
    {
        muiAtlasPage page = {0};
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = mrhi_formatR8Unorm;
        def.usage = mrhi_textureSampled | mrhi_textureCopyDestination;
        if (muiGlyphAtlas_GetPage(glyphs->atlas, glyphs->pageCount, &page) != mui_success)
        {
            return mui_errorPlatform;
        }
        def.width = page.width;
        def.height = page.height;
        if (mrhiCreateTexture(glyphs->device, &def, &glyphs->pages[glyphs->pageCount]) !=
            mrhi_success)
        {
            return mui_errorPlatform;
        }
        glyphs->pageCount++;
    }
    return mui_success;
}

// The atlas's changed rectangles, the array grown to hold them.
static muiResult TakeUpdates(muiRhiGlyphs* glyphs)
{
    uint32_t count = 0;
    while (muiGlyphAtlas_TakeUpdates(glyphs->atlas, glyphs->updates, glyphs->updateCapacity,
                                     &count) == mui_errorCapacity)
    {
        uint32_t capacity = glyphs->updateCapacity == 0 ? 64 : glyphs->updateCapacity;
        while (capacity < count)
        {
            capacity *= 2;
        }
        muiAtlasUpdate* updates = muiRhiAllocate(
            &glyphs->allocator, (size_t)capacity * sizeof(muiAtlasUpdate), alignof(muiAtlasUpdate));
        if (updates == nullptr)
        {
            return mui_errorCapacity;
        }
        muiRhiRelease(&glyphs->allocator, glyphs->updates,
                      (size_t)glyphs->updateCapacity * sizeof(muiAtlasUpdate),
                      alignof(muiAtlasUpdate));
        glyphs->updates = updates;
        glyphs->updateCapacity = capacity;
    }
    glyphs->updateCount = count;
    return mui_success;
}

muiResult muiRhiPrepareGlyphs(muiRhiGlyphs* glyphs)
{
    glyphs->updateCount = 0;
    if (glyphs->atlas == nullptr)
    {
        return mui_success;
    }
    muiResult status = MakePages(glyphs);
    status = status == mui_success ? TakeUpdates(glyphs) : status;
    for (uint32_t i = 0; i < glyphs->pageCount && status == mui_success; i++)
    {
        if (mrhiImportTexture(glyphs->device, glyphs->pages[i], &glyphs->resources[i]) !=
            mrhi_success)
        {
            status = mui_errorPlatform;
        }
    }
    return status;
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
        if (update->page >= glyphs->pageCount ||
            muiGlyphAtlas_GetPage(glyphs->atlas, update->page, &page) != mui_success)
        {
            return false;
        }
        const mrhiTextureCopy into = {
            .resource = glyphs->resources[update->page], .x = update->x, .y = update->y};
        const mrhiTexelLayout layout = {.bytesPerRow = page.width};
        const mrhiExtent3d size = {update->width, update->height, 1};
        const unsigned char* first = page.pixels + (size_t)update->y * page.width + update->x;
        size_t bytes = (size_t)(update->height - 1) * page.width + update->width;
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

muiResult muiRhiPrepareGlyphs(muiRhiGlyphs* glyphs)
{
    glyphs->updateCount = 0;
    return mui_success;
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
