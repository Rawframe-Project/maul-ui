// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's text snippets (docs/guide.md, section 5), each as written
// there (tools/check_guide.py checks it, family record 0019), run in
// Liberation Sans and their results checked: a label measured, wrapped
// and painted, its text changed, a word made bold; a field typed in,
// copied from, pasted into and undone, its caret found.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"
#include "maul-ui/text_style.h"

#include <string.h>

#include "liberation_sans.inc"

// Section 5: the service and its fonts.

// A text service whose default font is one the program loaded.
static muiTextService* MakeTextService(const void* fontData, size_t fontSize)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    if (muiCreateTextService(&def, &service) != mui_success)
    {
        return NULL;
    }
    muiFontDef font = muiDefaultFontDef();
    font.data = fontData;
    font.size = fontSize;
    muiFontId fontId = {0, 0};
    if (muiCreateFont(service, &font, &fontId) != mui_success ||
        muiSetDefaultFont(service, fontId) != mui_success)
    {
        muiDestroyTextService(service);
        return NULL;
    }
    return service;
}

// Section 5: blocks and labels.

// A label: a node showing a block of text at 16 units, dark grey.
static muiNodeId AddLabel(muiContext* context, muiTextService* service, muiNodeId parent,
                          const char* text, muiTextBlockId* blockOut)
{
    muiNodeId node = {0, 0};
    if (muiCreateTextBlock(service, text, strlen(text), blockOut) != mui_success)
    {
        return node;
    }
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(*blockOut);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 16.0f, mui_dimensionValue};
    style.color = (muiColor){0.2f, 0.2f, 0.2f, 1.0f};
    if (muiCreateNode(context, &def, &node) != mui_success ||
        muiNode_SetLayoutValues(context, node, &layout, MUI_PROPERTY_BIT(mui_propertyContent)) !=
            mui_success ||
        muiNode_SetTextValues(context, node, &style,
                              MUI_PROPERTY_BIT(mui_propertyFontSize) |
                                  MUI_PROPERTY_BIT(mui_propertyTextColor)) != mui_success ||
        muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}) != mui_success)
    {
        return (muiNodeId){0, 0};
    }
    return node;
}

// Section 5: a frame with text.

// Lays a root out and draws it, its text measured and painted by the
// service.
static muiResult FrameWithText(muiContext* context, muiTextService* service, muiNodeId root,
                               float width, float height)
{
    muiTextHost host = {service, context};
    const muiLayoutInput layout = {width, height,          muiMeasureText, &host,
                                   0,     muiTextBaseline, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
    muiResult result = muiComputeLayout(context, root, &layout);
    return result == mui_success ? muiBuildDrawList(context, root, &draw) : result;
}

// Section 5: changing a text.

// Replaces a label's text: the block takes it, and the node is measured
// and painted again at the next frame.
static muiResult SetLabel(muiContext* context, muiTextService* service, muiNodeId label,
                          muiTextBlockId block, const char* text)
{
    muiResult result = muiTextBlock_SetText(service, block, text, strlen(text));
    return result == mui_success ? muiNode_MarkContentChanged(context, label) : result;
}

// Section 5: spans.

// Makes bytes from start up to end of a label's text bold.
static muiResult Embolden(muiContext* context, muiTextService* service, muiNodeId label,
                          muiTextBlockId block, uint32_t start, uint32_t end)
{
    muiTextSpan bold = {start, end - start, MUI_PROPERTY_BIT(mui_propertyFontWeight),
                        muiDefaultTextStyle()};
    bold.style.weight = 700.0f;
    muiResult result = muiTextBlock_SetSpans(service, block, &bold, 1);
    return result == mui_success ? muiNode_MarkContentChanged(context, label) : result;
}

// Section 6: a field.

// Makes a label's block an editable field: one line, at most 64
// characters, undo keeping the last 100 edits.
static muiResult MakeField(muiTextService* service, muiTextBlockId block)
{
    muiTextEditDef def = muiDefaultTextEditDef();
    def.maxLength = 64;
    def.undoLimit = 100;
    return muiTextBlock_SetEditing(service, block, &def);
}

// Section 6: events.

// What the program's clipboard holds, as the editor copies and cuts.
static char s_clipboard[256];

static void WriteClipboard(void* user, const char* text, size_t length)
{
    (void)user;
    size_t kept = length < sizeof s_clipboard - 1 ? length : sizeof s_clipboard - 1;
    memcpy(s_clipboard, text, kept);
    s_clipboard[kept] = '\0';
}

// Hands an event to the focused field: keys, typed text and the
// pointer; a paste it asks for is answered from the clipboard. Whether
// the field took the event.
static bool FieldEvent(muiContext* context, muiTextService* service, muiNodeId field,
                       muiTextBlockId block, const muiEvent* event)
{
    muiTextHost host = {service, context};
    const muiTextEditInput input = {mui_keymapPc, WriteClipboard, NULL};
    muiTextEditOutcome outcome = {false, false, false};
    if (muiTextEditEvent(&host, field, event, &input, &outcome) != mui_success)
    {
        return false;
    }
    if (outcome.paste)
    {
        bool pasted = false;
        if (muiTextBlock_Paste(service, block, s_clipboard, strlen(s_clipboard), &pasted) ==
            mui_success)
        {
            outcome.changed = outcome.changed || pasted;
        }
    }
    if (outcome.changed)
    {
        (void)muiNode_MarkContentChanged(context, field);
    }
    return outcome.handled;
}

// Section 6: the caret.

// Where to draw the caret, in the field's content box as laid out.
static bool CaretOf(muiContext* context, muiTextService* service, muiNodeId field,
                    muiTextBlockId block, muiTextCaret* caretOut)
{
    muiTextHost host = {service, context};
    muiTextSelection selection;
    float width = muiNode_GetContentRect(context, field).width;
    return muiTextBlock_GetSelection(service, block, &selection) == mui_success &&
           muiTextGetCaret(&host, field, width, selection.caret, caretOut) == mui_success;
}

// Section 7: glyph images.

// Makes sure every glyph of a list is in the atlas, then takes the
// rectangles of its pages that changed, which a renderer uploads. How
// many glyphs are packed.
static uint32_t PackGlyphs(muiGlyphAtlas* atlas, const muiDrawList* list, muiAtlasUpdate* updates,
                           uint32_t capacity, uint32_t* updateCount)
{
    uint32_t packed = 0;
    float scale = list->header.scale;
    muiGlyphAtlas_NextFrame(atlas);
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        const muiDrawGlyphRun* run = &list->commands[i].glyphRun;
        if (list->commands[i].kind != mui_drawGlyphRun)
        {
            continue;
        }
        for (uint32_t g = 0; g < run->glyphCount; g++)
        {
            const muiGlyph* glyph = &list->glyphs[run->firstGlyph + g];
            muiAtlasGlyph image;
            if (muiGlyphAtlas_Get(atlas, run->font, glyph->id, run->size * scale,
                                  (run->originX + glyph->x) * scale,
                                  (run->originY + glyph->y) * scale, &image) == mui_success)
            {
                packed++;
            }
        }
    }
    *updateCount = 0;
    (void)muiGlyphAtlas_TakeUpdates(atlas, updates, capacity, updateCount);
    return packed;
}

// The glyph runs of the last list.
static uint32_t Runs(const muiContext* context, uint32_t* glyphsOut)
{
    muiDrawList list = {0};
    CHECK(muiGetDrawList(context, &list) == mui_success, "a list");
    uint32_t runs = 0;
    *glyphsOut = 0;
    for (uint32_t i = 0; i < list.commandCount; i++)
    {
        if (list.commands[i].kind == mui_drawGlyphRun)
        {
            runs++;
            *glyphsOut += list.commands[i].glyphRun.glyphCount;
        }
    }
    return runs;
}

static bool Holds(muiTextService* service, muiTextBlockId block, const char* expected)
{
    const char* text = NULL;
    size_t length = 0;
    return muiTextBlock_GetText(service, block, &text, &length) == mui_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static muiEvent Key(muiKeyCode code, muiKey key, muiModifiers modifiers)
{
    muiEvent event = {0};
    event.kind = mui_eventKeyDown;
    event.code = code;
    event.key = key != 0 ? key : MUI_KEY_NAMED | code;
    event.modifiers = modifiers;
    return event;
}

static void TestField(muiContext* context, muiTextService* service, muiNodeId root)
{
    enum
    {
        CODE_A = 4,
        CODE_C = 6,
        CODE_V = 25,
        CODE_Z = 29
    };
    muiTextBlockId block = {0, 0};
    muiNodeId field = AddLabel(context, service, root, "", &block);
    CHECK(field.index1 != 0 && MakeField(service, block) == mui_success, "a field");
    muiEvent typed = {0};
    typed.kind = mui_eventText;
    typed.text = "hello";
    typed.length = 5;
    muiEvent backspace = Key(mui_codeBackspace, 0, 0);
    CHECK(FieldEvent(context, service, field, block, &typed) &&
              FieldEvent(context, service, field, block, &backspace) &&
              Holds(service, block, "hell"),
          "typed, a letter erased");
    muiEvent all = Key(CODE_A, 'a', mui_modControl);
    muiEvent copy = Key(CODE_C, 'c', mui_modControl);
    muiEvent paste = Key(CODE_V, 'v', mui_modControl);
    muiEvent undo = Key(CODE_Z, 'z', mui_modControl);
    CHECK(FieldEvent(context, service, field, block, &all) &&
              FieldEvent(context, service, field, block, &copy) && strcmp(s_clipboard, "hell") == 0,
          "copied");
    CHECK(FieldEvent(context, service, field, block, &paste) &&
              FieldEvent(context, service, field, block, &paste) &&
              Holds(service, block, "hellhell"),
          "pasted over the selection, then after it");
    CHECK(FieldEvent(context, service, field, block, &undo) && Holds(service, block, "hell"),
          "the second paste undone");
    muiTextCaret caret;
    CHECK(FrameWithText(context, service, root, 800.0f, 600.0f) == mui_success &&
              CaretOf(context, service, field, block, &caret) && caret.x > 20.0f &&
              caret.height > 16.0f,
          "a caret after the text");
}

static void TestAtlas(muiContext* context, muiTextService* service)
{
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    muiGlyphAtlas* atlas = NULL;
    muiDrawList list = {0};
    muiAtlasUpdate updates[16];
    uint32_t count = 0;
    CHECK(muiCreateGlyphAtlas(service, &def, &atlas) == mui_success &&
              muiGetDrawList(context, &list) == mui_success,
          "an atlas");
    uint32_t glyphs = 0;
    Runs(context, &glyphs);
    CHECK(PackGlyphs(atlas, &list, updates, 16, &count) == glyphs && count > 0,
          "every glyph packed, rectangles to upload");
    CHECK(PackGlyphs(atlas, &list, updates, 16, &count) == glyphs && count == 0,
          "the next frame's, packed already");
    muiDestroyGlyphAtlas(atlas);
}

int main(void)
{
    muiTextService* service = MakeTextService(s_liberationSans, sizeof s_liberationSans);
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(service != NULL && muiCreateContext(&def, &context) == mui_success, "made");
    muiNodeDef rootDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.sizing.width = (muiDimension){0.0f, 120.0f, mui_dimensionValue};
    column.container.direction = mui_flexColumn;
    CHECK(muiCreateNode(context, &rootDef, &root) == mui_success &&
              muiNode_SetLayoutValues(context, root, &column, MUI_LAYOUT_PROPERTIES) == mui_success,
          "a column 120 wide");
    muiTextBlockId block = {0, 0};
    muiNodeId label = AddLabel(context, service, root, "Hello", &block);
    CHECK(label.index1 != 0 && FrameWithText(context, service, root, 800.0f, 600.0f) == mui_success,
          "a label laid out and drawn");
    uint32_t glyphs = 0;
    float oneLine = muiNode_GetRect(context, label).height;
    CHECK(Runs(context, &glyphs) == 1 && glyphs == 5 && oneLine > 16.0f && oneLine < 24.0f,
          "one run of five glyphs on one line");
    CHECK(SetLabel(context, service, label, block, "A longer text that wraps in the column") ==
                  mui_success &&
              FrameWithText(context, service, root, 800.0f, 600.0f) == mui_success &&
              muiNode_GetRect(context, label).height >= 3.0f * oneLine,
          "a longer text wrapped onto lines");
    CHECK(Embolden(context, service, label, block, 2, 8) == mui_success &&
              FrameWithText(context, service, root, 800.0f, 600.0f) == mui_success &&
              Runs(context, &glyphs) >= 3,
          "a bold word its own run");
    TestField(context, service, root);
    TestAtlas(context, service);
    muiDestroyContext(context);
    muiDestroyTextService(service);
    return s_failures == 0 ? 0 : 1;
}
