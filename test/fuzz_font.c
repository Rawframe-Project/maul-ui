// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes fonts, the bytes programs, users and content hand the library:
// FreeType and HarfBuzz read them, and Maul UI's own code reads their
// metrics, axes, colour layers and paint graphs and their CBLC, CBDT and
// sbix bitmaps (src/bitmap_glyph.c, src/colr_paint.c, src/png.c). Any
// bytes are opened as a font or refused. One opened has its metrics read
// and a text in several scripts laid out and painted in it, a bold, an
// italic and a larger span making instances; then every glyph painted is
// rendered as an image, a distance field, a multi-channel field and a
// colour glyph, each succeeding or refusing as malformed or too large,
// never touching memory it does not own, and everything freed after.

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    // The glyphs rendered of an input, and the pixels each may take.
    MAX_GLYPHS = 48,
    PIXEL_BYTES = 1 << 18
};

static unsigned char s_pixels[PIXEL_BYTES];

// Latin with a ligature and marks, Arabic, Devanagari, CJK and an emoji
// sequence, two lines.
static const char s_text[] = "Hag fi\xEF\xAC\x81 e\xCC\x81 \xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A "
                             "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0"
                             "\xA5\x87\n\xE6\xBC\xA2\xE5\xAD\x97 \xF0\x9F\x91\x8D\xF0\x9F\x8F"
                             "\xBD 0123";

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// Whether a rendering's result is one its contract allows for a glyph
// the font has.
static bool IsAllowed(muiResult result)
{
    return result == mui_success || result == mui_errorFormat || result == mui_errorCapacity ||
           result == mui_empty;
}

static void RenderGlyphs(muiTextService* service, const muiDrawList* list)
{
    uint32_t rendered = 0;
    for (uint32_t c = 0; c < list->commandCount && rendered < MAX_GLYPHS; c++)
    {
        const muiDrawCommand* command = &list->commands[c];
        if (command->kind != mui_drawGlyphRun)
        {
            continue;
        }
        const muiDrawGlyphRun* run = &command->glyphRun;
        for (uint32_t g = 0; g < run->glyphCount && rendered < MAX_GLYPHS; g++, rendered++)
        {
            uint32_t glyph = list->glyphs[run->firstGlyph + g].id;
            muiGlyphImage image;
            const muiLinearColor ink = {0.2f, 0.4f, 0.6f, 1.0f};
            Expect(IsAllowed(muiRenderGlyph(service, run->font, glyph, run->size, 0.25f, &image,
                                            s_pixels, sizeof s_pixels)));
            Expect(IsAllowed(muiRenderGlyphField(service, run->font, glyph, run->size, 4, &image,
                                                 s_pixels, sizeof s_pixels)));
            Expect(IsAllowed(muiRenderGlyphMultiField(service, run->font, glyph, run->size, 4,
                                                      &image, s_pixels, sizeof s_pixels)));
            Expect(IsAllowed(muiRenderColorGlyph(service, run->font, glyph, run->size, 0.0f, 0, ink,
                                                 &image, s_pixels, sizeof s_pixels)));
        }
    }
}

// Lays out and paints the text in the service's default font, spans
// asking for instances of it.
static void Paint(muiTextService* service)
{
    muiContextDef contextDef = muiDefaultContextDef();
    muiContext* context = nullptr;
    muiTextBlockId block = {0, 0};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = s_text;
    blockDef.length = sizeof s_text - 1;
    Expect(muiCreateContext(&contextDef, &context) == mui_success &&
           muiCreateTextBlock(service, &blockDef, &block) == mui_success);
    muiTextSpan spans[3] = {{0, 3, MUI_PROPERTY_BIT(mui_propertyFontWeight), muiDefaultTextStyle()},
                            {4, 5, MUI_PROPERTY_BIT(mui_propertyFontSlant), muiDefaultTextStyle()},
                            {10, 3, MUI_PROPERTY_BIT(mui_propertyFontSize), muiDefaultTextStyle()}};
    spans[0].style.weight = 700.0f;
    spans[1].style.slant = mui_slantItalic;
    spans[2].style.size = (muiDimension){1.5f, 0.0f, mui_dimensionValue};
    Expect(muiTextBlock_SetSpans(service, block, spans, 3) == mui_success);
    muiNodeDef nodeDef = muiDefaultNodeDef();
    nodeDef.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = {0, 0};
    Expect(muiCreateNode(context, &nodeDef, &node) == mui_success);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    layout.sizing.width = (muiDimension){0.0f, 120.0f, mui_dimensionValue};
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 16.0f, mui_dimensionValue};
    Expect(muiNode_SetLayoutValues(context, node, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyContent) |
                                       MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success &&
           muiNode_SetTextValues(context, node, &style, MUI_PROPERTY_BIT(mui_propertyFontSize)) ==
               mui_success);
    muiTextHost host = {service, context};
    const muiLayoutInput layoutInput = {1000.0f, 1000.0f, muiMeasureText, &host,
                                        0,       nullptr, {0, 0, 0, 0}};
    const muiDrawInput drawInput = {1, 1.0f, muiPaintText, &host};
    muiDrawList list = {0};
    Expect(muiComputeLayout(context, node, &layoutInput) == mui_success &&
           muiBuildDrawList(context, node, &drawInput) == mui_success &&
           muiGetDrawList(context, &list) == mui_success);
    RenderGlyphs(service, &list);
    muiDestroyContext(context);
    Expect(muiDestroyTextBlock(service, block) == mui_success);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0)
    {
        return 0;
    }
    uint32_t faces = 0;
    muiResult counted = muiCountFontFaces(data, size, &faces);
    Expect(counted == mui_success || counted == mui_errorFormat);
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    muiTextService* service = nullptr;
    Expect(muiCreateTextService(&serviceDef, &service) == mui_success);
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = data;
    fontDef.size = size;
    muiFontId font = {0, 0};
    muiResult created = muiCreateFont(service, &fontDef, &font);
    // A size under a font's 12-byte header is out of the def's range, not
    // a malformed font (muiCreateFont's contract).
    Expect(created == mui_success || created == mui_errorFormat || created == mui_errorCapacity ||
           (size < 12 && created == mui_errorInvalid));
    if (created == mui_success)
    {
        muiFontMetrics metrics;
        Expect(muiFont_GetMetrics(service, font, &metrics) == mui_success &&
               muiSetDefaultFont(service, font) == mui_success);
        Paint(service);
    }
    muiDestroyTextService(service);
    return 0;
}
