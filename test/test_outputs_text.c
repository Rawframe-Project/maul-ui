// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a failure leaves in the text component's outputs (Maul
// conventions, Errors and assertions): a call whose first parameter is the
// text service or a glyph atlas clears every output on any failure, before
// it checks that owner, so a NULL owner and a stale id both leave zeros.

#include "test_harness.h"

#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Every output a call can write, filled with a pattern no call writes
// before each call and read back after it.
typedef struct Outputs
{
    muiFontId font;
    muiFontFamilyId family;
    muiFontMetrics metrics;
    muiTextBlockId block;
    muiGlyphAtlas* atlas;
    muiAtlasGlyph glyph;
    muiAtlasPage page;
    muiGlyphImage image;
    muiTextSelection selection;
    muiInputPurpose purpose;
    const muiTextSpan* spans;
    const char* text;
    size_t length;
    uint32_t start;
    uint32_t end;
    uint32_t count;
    bool changed;
    bool redo;
} Outputs;

static void Fill(Outputs* outputs)
{
    memset(outputs, 0xA5, sizeof *outputs);
}

static bool IsZero(const void* memory, size_t size)
{
    const unsigned char* bytes = memory;
    for (size_t i = 0; i < size; i++)
    {
        if (bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}

#define CLEARED(field) IsZero(&outputs.field, sizeof outputs.field)

// Calls every query and edit of a block that writes outputs, on a service
// and a block that may be gone, and checks each failure clears them.
static void CheckBlock(muiTextService* service, muiTextBlockId block, muiResult expected,
                       const char* why)
{
    Outputs outputs;
    char message[160];
#define EXPECT(call, ...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        Fill(&outputs);                                                                            \
        muiResult result_ = (call);                                                                \
        snprintf(message, sizeof message, "%s: %s clears its outputs", why, #call);                \
        CHECK(result_ == expected && (__VA_ARGS__), message);                                      \
    } while (0)

    EXPECT(muiTextBlock_GetSpans(service, block, &outputs.spans, &outputs.count),
           CLEARED(spans) && CLEARED(count));
    EXPECT(muiTextBlock_GetText(service, block, &outputs.text, &outputs.length),
           CLEARED(text) && CLEARED(length));
    EXPECT(muiTextBlock_FindDeletion(service, block, 0, mui_deleteBackward, &outputs.start,
                                     &outputs.end),
           CLEARED(start) && CLEARED(end));
    EXPECT(muiTextBlock_GetComposition(service, block, &outputs.start, &outputs.end),
           CLEARED(start) && CLEARED(end));
    EXPECT(muiTextBlock_GetSelection(service, block, &outputs.selection), CLEARED(selection));
    EXPECT(muiTextBlock_Type(service, block, "a", 1, &outputs.changed), CLEARED(changed));
    EXPECT(muiTextBlock_Paste(service, block, "a", 1, &outputs.changed), CLEARED(changed));
    EXPECT(muiTextBlock_Erase(service, block, mui_deleteBackward, &outputs.changed),
           CLEARED(changed));
    EXPECT(muiTextBlock_EraseTo(service, block, 0, &outputs.changed), CLEARED(changed));
    EXPECT(muiTextBlock_GetSelectedText(service, block, &outputs.text, &outputs.length),
           CLEARED(text) && CLEARED(length));
    EXPECT(muiTextBlock_Undo(service, block, &outputs.changed), CLEARED(changed));
    EXPECT(muiTextBlock_Redo(service, block, &outputs.changed), CLEARED(changed));
    EXPECT(muiTextBlock_GetInputPurpose(service, block, &outputs.purpose), CLEARED(purpose));
    EXPECT(muiTextBlock_GetUndoState(service, block, &outputs.changed, &outputs.redo),
           CLEARED(changed) && CLEARED(redo));
    EXPECT(muiTextBlock_Compose(service, block, "a", 1, 0, NULL, 0, &outputs.changed),
           CLEARED(changed));
#undef EXPECT
}

static void TestNullServiceClearsOutputs(void)
{
    CheckBlock(NULL, (muiTextBlockId){1, 1}, mui_errorInvalid, "a NULL service");

    Outputs outputs;
    Fill(&outputs);
    const muiFontDef fontDef = muiDefaultFontDef();
    CHECK(muiCreateFont(NULL, &fontDef, &outputs.font) == mui_errorInvalid && CLEARED(font),
          "muiCreateFont clears its output");
    Fill(&outputs);
    CHECK(muiFont_GetMetrics(NULL, (muiFontId){1, 1}, &outputs.metrics) == mui_errorInvalid &&
              CLEARED(metrics),
          "muiFont_GetMetrics clears its output");
    Fill(&outputs);
    const muiFontFamilyDef familyDef = muiDefaultFontFamilyDef();
    CHECK(muiCreateFontFamily(NULL, &familyDef, &outputs.family) == mui_errorInvalid &&
              CLEARED(family),
          "muiCreateFontFamily clears its output");
    Fill(&outputs);
    CHECK(muiFontFamily_MatchFace(NULL, (muiFontFamilyId){1, 1}, 400.0f, mui_slantNormal,
                                  &outputs.font) == mui_errorInvalid &&
              CLEARED(font),
          "muiFontFamily_MatchFace clears its output");
    Fill(&outputs);
    const muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    CHECK(muiCreateTextBlock(NULL, &blockDef, &outputs.block) == mui_errorInvalid && CLEARED(block),
          "muiCreateTextBlock clears its output");
    Fill(&outputs);
    const muiGlyphAtlasDef atlasDef = muiDefaultGlyphAtlasDef();
    CHECK(muiCreateGlyphAtlas(NULL, &atlasDef, &outputs.atlas) == mui_errorInvalid &&
              CLEARED(atlas),
          "muiCreateGlyphAtlas clears its output");
    Fill(&outputs);
    CHECK(muiTextService_RenderGlyph(NULL, 0, 0, 16.0f, 0.0f, &outputs.image, NULL, 0) ==
                  mui_errorInvalid &&
              CLEARED(image),
          "muiTextService_RenderGlyph clears its output");
    Fill(&outputs);
    const muiLinearColor black = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(muiTextService_RenderColorGlyph(NULL, 0, 0, 16.0f, 0.0f, 0, black, &outputs.image, NULL,
                                          0) == mui_errorInvalid &&
              CLEARED(image),
          "muiTextService_RenderColorGlyph clears its output");
    Fill(&outputs);
    CHECK(muiTextService_RenderGlyphField(NULL, 0, 0, 16.0f, 4, &outputs.image, NULL, 0) ==
                  mui_errorInvalid &&
              CLEARED(image),
          "muiTextService_RenderGlyphField clears its output");
    Fill(&outputs);
    CHECK(muiTextService_RenderGlyphMultiField(NULL, 0, 0, 16.0f, 4, &outputs.image, NULL, 0) ==
                  mui_errorInvalid &&
              CLEARED(image),
          "muiTextService_RenderGlyphMultiField clears its output");
}

static void TestNullAtlasClearsOutputs(void)
{
    Outputs outputs;
    Fill(&outputs);
    CHECK(muiGlyphAtlas_GetGlyph(NULL, 0, 0, 16.0f, 0.0f, 0.0f, &outputs.glyph) ==
                  mui_errorInvalid &&
              CLEARED(glyph),
          "muiGlyphAtlas_GetGlyph clears its output");
    Fill(&outputs);
    const muiLinearColor black = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(muiGlyphAtlas_GetColorGlyph(NULL, 0, 0, 16.0f, 0.0f, 0.0f, 0, black, &outputs.glyph) ==
                  mui_errorInvalid &&
              CLEARED(glyph),
          "muiGlyphAtlas_GetColorGlyph clears its output");
    Fill(&outputs);
    CHECK(muiGlyphAtlas_GetGlyphField(NULL, 0, 0, 16.0f, 4, &outputs.glyph) == mui_errorInvalid &&
              CLEARED(glyph),
          "muiGlyphAtlas_GetGlyphField clears its output");
    Fill(&outputs);
    CHECK(muiGlyphAtlas_GetGlyphMultiField(NULL, 0, 0, 16.0f, 4, &outputs.glyph) ==
                  mui_errorInvalid &&
              CLEARED(glyph),
          "muiGlyphAtlas_GetGlyphMultiField clears its output");
    Fill(&outputs);
    CHECK(muiGlyphAtlas_GetPage(NULL, 0, &outputs.page) == mui_errorInvalid && CLEARED(page),
          "muiGlyphAtlas_GetPage clears its output");
    Fill(&outputs);
    CHECK(muiGlyphAtlas_TakeUpdates(NULL, NULL, 0, &outputs.count) == mui_errorInvalid &&
              CLEARED(count),
          "muiGlyphAtlas_TakeUpdates clears its output");
}

static void TestStaleIdsClearOutputs(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    muiTextBlockId block = {0};
    CHECK(muiCreateTextBlock(service, &blockDef, &block) == mui_success, "block");
    CHECK(muiDestroyTextBlock(service, block) == mui_success, "block gone");
    CheckBlock(service, block, mui_errorStale, "a stale block");

    Outputs outputs;
    Fill(&outputs);
    CHECK(muiFont_GetMetrics(service, (muiFontId){1, 1}, &outputs.metrics) == mui_errorStale &&
              CLEARED(metrics),
          "a stale font: muiFont_GetMetrics clears its output");
    muiDestroyTextService(service);
}

int main(void)
{
    TestNullServiceClearsOutputs();
    TestNullAtlasClearsOutputs();
    TestStaleIdsClearOutputs();
    return s_failures == 0 ? 0 : 1;
}
