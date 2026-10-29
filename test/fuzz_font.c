// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes fonts, the bytes programs, users and content hand the library:
// FreeType and HarfBuzz read them, and Maul UI's own code reads their
// metrics, axes, colour layers and paint graphs and their CBLC, CBDT and
// sbix bitmaps (src/bitmap_glyph.c, src/colr_paint.c, src/png.c). Any
// bytes are opened as a font or refused. One opened has its metrics read,
// and its first faces make a family: a face is matched for weights and
// slants from its axes, weight, width and style (src/font_family.c), and
// a text in several scripts is laid out and painted in the family, a
// bold, an italic and a larger span choosing faces and making
// instances; then every glyph painted is
// rendered as an image, a distance field, a multi-channel field and a
// colour glyph, each succeeding or refusing as malformed or too large,
// never touching memory it does not own, and everything freed after. The
// font's first glyph ids are also rendered by id, as an image and a
// colour glyph, since colour graphs and layers are often glyphs no
// character maps; the id past the last is refused.

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
    // The glyph ids rendered by id, from 0.
    MAX_IDS = 64,
    // The faces of a collection opened, the first included.
    MAX_FACES = 4,
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
            Expect(IsAllowed(muiTextService_RenderGlyph(service, run->font, glyph, run->size, 0.25f,
                                                        &image, s_pixels, sizeof s_pixels)));
            Expect(IsAllowed(muiTextService_RenderGlyphField(
                service, run->font, glyph, run->size, 4, &image, s_pixels, sizeof s_pixels)));
            Expect(IsAllowed(muiTextService_RenderGlyphMultiField(
                service, run->font, glyph, run->size, 4, &image, s_pixels, sizeof s_pixels)));
            Expect(IsAllowed(muiTextService_RenderColorGlyph(service, run->font, glyph, run->size,
                                                             0.0f, 0, ink, &image, s_pixels,
                                                             sizeof s_pixels)));
        }
    }
}

// Renders the default font's first glyph ids, whether or not a
// character maps them, and checks that the id past the last is refused.
static void RenderIds(muiTextService* service, uint32_t glyphCount)
{
    const muiLinearColor ink = {0.2f, 0.4f, 0.6f, 1.0f};
    muiGlyphImage image;
    for (uint32_t glyph = 0; glyph < glyphCount && glyph < MAX_IDS; glyph++)
    {
        Expect(IsAllowed(muiTextService_RenderGlyph(service, 0, glyph, 24.0f, 0.0f, &image,
                                                    s_pixels, sizeof s_pixels)));
        Expect(IsAllowed(muiTextService_RenderColorGlyph(service, 0, glyph, 24.0f, 0.0f, 0, ink,
                                                         &image, s_pixels, sizeof s_pixels)));
    }
    Expect(muiTextService_RenderColorGlyph(service, 0, glyphCount, 24.0f, 0.0f, 0, ink, &image,
                                           s_pixels, sizeof s_pixels) == mui_errorInvalid);
}

// Opens the input's further faces, up to MAX_FACES with the first, in
// fonts after it; each is opened or refused as malformed. Returns how
// many fonts there are.
static uint32_t OpenFaces(muiTextService* service, const uint8_t* data, size_t size, uint32_t faces,
                          muiFontId* fonts)
{
    uint32_t count = 1;
    for (uint32_t index = 1; index < faces && index < MAX_FACES; index++)
    {
        muiFontDef fontDef = muiDefaultFontDef();
        fontDef.data = data;
        fontDef.size = size;
        fontDef.faceIndex = index;
        muiResult created = muiCreateFont(service, &fontDef, &fonts[count]);
        Expect(created == mui_success || created == mui_errorFormat ||
               created == mui_errorCapacity);
        count += created == mui_success ? 1 : 0;
    }
    return count;
}

// Matches a face of the family for weights at and around CSS's edges in
// each slant: always one of its faces.
static void MatchFaces(const muiTextService* service, muiFontFamilyId family,
                       const muiFontId* fonts, uint32_t count)
{
    static const float s_weights[] = {1.0f,   100.0f, 399.0f, 400.0f, 450.0f,
                                      500.0f, 501.0f, 700.0f, 1000.0f};
    static const muiFontSlant s_slants[] = {mui_slantNormal, mui_slantItalic, mui_slantOblique};
    for (size_t w = 0; w < sizeof s_weights / sizeof s_weights[0]; w++)
    {
        for (size_t s = 0; s < sizeof s_slants / sizeof s_slants[0]; s++)
        {
            muiFontId face = {0, 0};
            Expect(muiFontFamily_MatchFace(service, family, s_weights[w], s_slants[s], &face) ==
                   mui_success);
            bool found = false;
            for (uint32_t i = 0; i < count; i++)
            {
                found = found ||
                        (face.index1 == fonts[i].index1 && face.generation == fonts[i].generation);
            }
            Expect(found);
        }
    }
}

// Lays out and paints the text in a font or family, spans asking for
// faces and instances of it.
static void Paint(muiTextService* service, uint64_t fontKey)
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
    style.font = fontKey;
    Expect(muiNode_SetLayoutValues(context, node, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyContent) |
                                       MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success &&
           muiNode_SetTextValues(context, node, &style,
                                 MUI_PROPERTY_BIT(mui_propertyFontSize) |
                                     MUI_PROPERTY_BIT(mui_propertyFont)) == mui_success);
    muiTextHost host = {service, context};
    const muiLayoutInput layoutInput = {1000.0f, 1000.0f, muiMeasureText, &host,
                                        0,       nullptr, {0, 0, 0, 0}};
    const muiDrawInput drawInput = {1, 1.0f, muiPaintText, &host};
    muiDrawList list = {0};
    Expect(muiNode_ComputeLayout(context, node, &layoutInput) == mui_success &&
           muiNode_BuildDrawList(context, node, &drawInput) == mui_success &&
           muiContext_GetDrawList(context, &list) == mui_success);
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
        muiFontId fonts[MAX_FACES] = {font};
        uint32_t count = OpenFaces(service, data, size, faces, fonts);
        // The first face is a fallback of the family and of the service
        // too, which a chain holding it already skips.
        const uint64_t fallback = muiFont_GetKey(font);
        muiFontFamilyDef familyDef = muiDefaultFontFamilyDef();
        familyDef.faces = fonts;
        familyDef.faceCount = count;
        familyDef.fallbacks = &fallback;
        familyDef.fallbackCount = 1;
        muiFontFamilyId family = {0, 0};
        muiFontMetrics metrics;
        Expect(muiFont_GetMetrics(service, font, &metrics) == mui_success &&
               muiTextService_SetDefaultFont(service, font) == mui_success &&
               muiCreateFontFamily(service, &familyDef, &family) == mui_success &&
               muiTextService_SetFallbackFonts(service, &fallback, 1) == mui_success);
        MatchFaces(service, family, fonts, count);
        Paint(service, muiFontFamily_GetKey(family));
        RenderIds(service, metrics.glyphCount);
    }
    muiDestroyTextService(service);
    return 0;
}
