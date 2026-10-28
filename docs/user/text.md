# Text

The core lays out and draws no text: it measures and paints a node's host content through the functions the frame gives it. The text component, built by default (`MAUL_UI_TEXT`), is a text service that does both. Fonts are read by FreeType, text is shaped by HarfBuzz, and lines are broken and runs ordered by Unicode's rules. A program may bring another text stack instead and hand its own functions to the frame.

```c
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
```

## Contents

- [Fonts and the service](#fonts-and-the-service)
- [Labels](#labels)
- [Spans](#spans)
- [Fallback and families](#fallback-and-families)
- [What to expect](#what-to-expect)

## Fonts and the service

The service holds fonts, families and text blocks. A font is the bytes of a TrueType, OpenType or collection file, copied or borrowed:

```c
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
```

A borrowed font's bytes must stay unchanged until the font is destroyed. Compressed web fonts (WOFF, WOFF2) are refused with `mui_errorUnsupported`; decompress them first. The service is used by one thread at a time, like a context, and one service can serve several contexts.

## Labels

A block is UTF-8 text the service lays out. A node shows it when its host key is the block's key and its content is the host's. Its text style (font, size, color, weight, alignment, wrapping) comes through the same layers as every property, and is inherited as CSS inherits it:

```c
// A label: a node showing a block of text at 16 units, dark grey.
static muiNodeId AddLabel(muiContext* context, muiTextService* service, muiNodeId parent,
                          const char* text, muiTextBlockId* blockOut)
{
    muiNodeId node = {0, 0};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = text;
    blockDef.length = strlen(text);
    if (muiCreateTextBlock(service, &blockDef, blockOut) != mui_success)
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
```

The frame hands the service's functions to layout and drawing, with a `muiTextHost` naming the service and the context:

```c
// Lays a root out and draws it, its text measured and painted by the
// service.
static muiResult FrameWithText(muiContext* context, muiTextService* service, muiNodeId root,
                               float width, float height)
{
    muiTextHost host = {service, context};
    const muiLayoutInput layout = {
        width, height, muiMeasureText, &host, 0, muiMeasureTextBaseline, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
    muiResult result = muiComputeLayout(context, root, &layout);
    return result == mui_success ? muiBuildDrawList(context, root, &draw) : result;
}
```

A label in a column 120 wide takes one line for "Hello" and wraps a longer text onto as many lines as it needs, at the break opportunities Unicode gives, its height growing. Each line's glyphs reach the draw list as glyph runs, in their font and size ([Drawing](drawing.md)).

A block's text is the program's to change. The context does not watch it: marking the node's content changed has it measured and painted again, and its siblings move if its size changes.

```c
// Replaces a label's text: the block takes it, and the node is measured
// and painted again at the next frame.
static muiResult SetLabel(muiContext* context, muiTextService* service, muiNodeId label,
                          muiTextBlockId block, const char* text)
{
    muiResult result = muiTextBlock_SetText(service, block, text, strlen(text));
    return result == mui_success ? muiNode_MarkContentChanged(context, label) : result;
}
```

## Spans

Spans style parts of a block's text over the node's style: color, decoration, font, size, weight, slant and baseline shift, each by byte range. A span's text is drawn as glyph runs of its own. Spans are what a program's own markup turns into; the library parses no markup.

```c
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
```

## Fallback and families

`muiSetFallbackFonts` names fonts tried for characters the style's font lacks, such as an emoji font or one for another script. Each grapheme cluster is drawn in the first font that has all of it.

A font family (`muiCreateFontFamily`) holds faces and picks one by weight, width and slant as CSS does, setting a variable font's axes from the style. A style with a weight a face cannot reach gets a synthetic bold, and an italic with no italic face gets a slant.

## What to expect

- Text measures the same on every machine and at every device scale, since nothing is hinted and the dependencies are pinned.
- Shaping uses no language and ignores the process's locale.
- Bidirectional text, combining marks, emoji sequences and scripts that need shaping, such as Arabic and Devanagari, work as Unicode and HarfBuzz define them.
- Line breaking is greedy: a word wider than its line overflows it rather than breaking inside.
- [Text editing](text-editing.md) covers fields, carets and input methods; [Drawing](drawing.md#glyphs) covers turning glyph runs into pixels.
