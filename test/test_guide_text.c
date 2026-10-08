// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's text snippets (docs/guide.md, section 5), each as written
// there (tools/check_guide.py checks it, family record 0019), run in
// Liberation Sans and their results checked: a label measured, wrapped
// and painted, its text changed, a word made bold.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
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
    muiDestroyContext(context);
    muiDestroyTextService(service);
    return s_failures == 0 ? 0 : 1;
}
