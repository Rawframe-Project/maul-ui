// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text blocks (record mui-0006): their lifetime and keys, measuring
// lines at every kind of width request, line height and letter spacing,
// painting glyph runs aligned and in bidi order, and memory running out.
// Ahem draws every glyph as a box one em wide, ascent 0.8 em and descent
// 0.2 em, so every value is exact.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"
#include "break_test.inc"
#include "liberation_sans.inc"

static const muiNodeId s_nullNode = {0, 0};

#define SIZE    MUI_PROPERTY_BIT(mui_propertyFontSize)
#define LINE    MUI_PROPERTY_BIT(mui_propertyLineHeight)
#define SPACING MUI_PROPERTY_BIT(mui_propertyLetterSpacing)
#define ALIGN   MUI_PROPERTY_BIT(mui_propertyTextAlign)
#define WRAP    MUI_PROPERTY_BIT(mui_propertyTextWrap)
#define FONT    MUI_PROPERTY_BIT(mui_propertyFont)
#define GLYPH_A 67u
#define GLYPH_B 68u
#define GLYPH_C 69u

// An allocator that fails its allocation number failAt (from 1; 0
// never), counting from when failAt was set.
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

typedef struct Scene
{
    muiTextService* service;
    muiFontId font;
    muiContext* context;
    muiTextHost host;
} Scene;

static Scene MakeScene(FailingAllocator* failing)
{
    Scene scene = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    if (failing != NULL)
    {
        def.allocator = (muiAllocator){FailingAlloc, FailingFree, failing};
    }
    CHECK(muiCreateTextService(&def, &scene.service) == mui_success, "service");
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    CHECK(muiCreateFont(scene.service, &font, &scene.font) == mui_success &&
              muiSetDefaultFont(scene.service, scene.font) == mui_success,
          "the default font");
    muiContextDef context = muiDefaultContextDef();
    CHECK(muiCreateContext(&context, &scene.context) == mui_success, "context");
    scene.host = (muiTextHost){scene.service, scene.context};
    return scene;
}

static void FreeScene(Scene* scene)
{
    muiDestroyContext(scene->context);
    muiDestroyTextService(scene->service);
}

// A node showing text at size 10, its key's block made for it.
static muiNodeId AddText(Scene* scene, muiNodeId parent, const char* text)
{
    muiTextBlockId block = {0, 0};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = text;
    blockDef.length = strlen(text);
    CHECK(muiCreateTextBlock(scene->service, &blockDef, &block) == mui_success, "block");
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(scene->context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(scene->context, parent, node, s_nullNode) == mui_success,
              "insert");
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(scene->context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
          "host content");
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiNode_SetTextValues(scene->context, node, &style, SIZE) == mui_success, "size");
    return node;
}

static void SetText(Scene* scene, muiNodeId node, muiTextStyle style, muiPropertyMask mask)
{
    CHECK(muiNode_SetTextValues(scene->context, node, &style, mask) == mui_success, "style");
}

// Lays a root out, which styles it, at an available width.
static void Layout(Scene* scene, muiNodeId root, float width)
{
    const muiLayoutInput input = {width, 1000.0f, muiMeasureText, &scene->host,
                                  0,     NULL,    {0, 0, 0, 0}};
    CHECK(muiComputeLayout(scene->context, root, &input) == mui_success, "layout");
}

static muiSize Measure(Scene* scene, muiNodeId node, muiMeasureMode mode, float width)
{
    return muiMeasureText(&scene->host, node, muiNode_GetHostKey(scene->context, node),
                          (muiMeasureAxis){width, mode},
                          (muiMeasureAxis){0.0f, mui_measureMaxContent});
}

static muiDrawList Paint(Scene* scene, muiNodeId root)
{
    const muiDrawInput input = {1, 1.0f, muiPaintText, &scene->host};
    CHECK(muiBuildDrawList(scene->context, root, &input) == mui_success, "paint");
    muiDrawList list;
    CHECK(muiGetDrawList(scene->context, &list) == mui_success, "list");
    return list;
}

static bool SameSize(muiSize size, float width, float height)
{
    return size.width == width && size.height == height;
}

static void TestBlocksAndKeys(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    def.limits.textBlocks = 2;
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    muiTextBlockId a = {0, 0};
    muiTextBlockId b = {0, 0};
    muiTextBlockId c = {7, 7};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    const muiTextBlockDef empty = muiDefaultTextBlockDef();
    blockDef.text = "a";
    blockDef.length = 1;
    CHECK(muiCreateTextBlock(service, &blockDef, &a) == mui_success &&
              muiCreateTextBlock(service, &empty, &b) == mui_success,
          "two blocks, the default def's empty");
    blockDef.text = "c";
    CHECK(muiCreateTextBlock(service, &blockDef, &c) == mui_errorCapacity && c.index1 == 0,
          "the limit");
    CHECK(muiTextBlock_GetKey(a) != 0 && muiTextBlock_GetKey(a) != muiTextBlock_GetKey(b), "keys");
    blockDef.text = "a";
    const muiTextBlockDef noText = {empty.cookie, NULL, 1};
    const muiTextBlockDef tooLong = {empty.cookie, "a", (size_t)1 << 31};
    CHECK(muiCreateTextBlock(NULL, &blockDef, &c) == mui_errorInvalid &&
              muiCreateTextBlock(service, NULL, &c) == mui_errorInvalid &&
              muiCreateTextBlock(service, &(muiTextBlockDef){0, "a", 1}, &c) == mui_errorInvalid &&
              muiCreateTextBlock(service, &noText, &c) == mui_errorInvalid &&
              muiCreateTextBlock(service, &blockDef, NULL) == mui_errorInvalid &&
              muiCreateTextBlock(service, &tooLong, &c) == mui_errorInvalid,
          "arguments: no def, no cookie, no text for a length, too long");
    CHECK(muiTextBlock_SetText(service, a, "abc", 3) == mui_success &&
              muiTextBlock_SetText(service, a, NULL, 1) == mui_errorInvalid &&
              muiTextBlock_SetText(NULL, a, "a", 1) == mui_errorInvalid &&
              muiTextBlock_SetText(service, (muiTextBlockId){0, 0}, "a", 1) == mui_errorInvalid,
          "setting text");
    CHECK(muiDestroyTextBlock(service, a) == mui_success &&
              muiDestroyTextBlock(service, a) == mui_errorStale &&
              muiTextBlock_SetText(service, a, "a", 1) == mui_errorStale &&
              muiDestroyTextBlock(NULL, b) == mui_errorInvalid &&
              muiDestroyTextBlock(service, (muiTextBlockId){0, 0}) == mui_errorInvalid,
          "destroying");
    CHECK(muiFont_GetKey((muiFontId){1, 2}) == ((uint64_t)2 << 16) &&
              muiFont_GetKey((muiFontId){0, 0}) == 0,
          "a font's key");
    CHECK(muiSetDefaultFont(NULL, (muiFontId){0, 0}) == mui_errorInvalid &&
              muiSetDefaultFont(service, (muiFontId){1, 1}) == mui_errorStale &&
              muiSetDefaultFont(service, (muiFontId){0, 0}) == mui_success,
          "the default font");
    CHECK(muiGetTextServiceFailures(NULL) == 0 && muiGetTextServiceFailures(service) == 0,
          "no failures");
    muiDestroyTextService(service);
}

static void TestAccessText(void)
{
    // A node reads as its block's text: as the function gives it, and in
    // the accessibility tree as a label.
    Scene scene = MakeScene(NULL);
    muiNodeId node = AddText(&scene, s_nullNode, "Save");
    uint64_t key = muiNode_GetHostKey(scene.context, node);
    muiAccessContent content;
    CHECK(muiAccessTextOf(&scene.host, node, key, false, &content) && content.length == 4 &&
              memcmp(content.text, "Save", 4) == 0 && !content.marks.selected &&
              content.marks.lineCount == 0 && content.marks.wordCount == 0,
          "the block's text, no boundaries unasked");
    CHECK(!muiAccessTextOf(&scene.host, node, key + 1, false, &content) &&
              !muiAccessTextOf(NULL, node, key, false, &content) &&
              !muiAccessTextOf(&scene.host, node, key, false, NULL),
          "no block");
    CHECK(muiSetAccessTextFunction(scene.context, muiAccessTextOf, &scene.host) == mui_success &&
              muiAccess_Enable(scene.context, node) == mui_success,
          "read");
    muiAccessUpdate update;
    CHECK(muiBuildAccessUpdate(scene.context, node, &update) == mui_success &&
              update.nodeCount == 1 && update.nodes[0]->role == mui_roleLabel &&
              update.nodes[0]->textLength[mui_accessValue] == 4 &&
              memcmp(update.nodes[0]->text[mui_accessValue], "Save", 4) == 0 &&
              update.nodes[0]->marks.lineCount == 1 && update.nodes[0]->marks.wordCount == 1,
          "a label reading Save, a line and a word");
    // Laid out 35 wide, a word a line, as painted; its words are the
    // segments with letters.
    muiNodeId wrapped = AddText(&scene, s_nullNode, "ab cd, ef");
    Layout(&scene, wrapped, 35.0f);
    key = muiNode_GetHostKey(scene.context, wrapped);
    const muiAccessTextMarks* marks = &content.marks;
    CHECK(muiAccessTextOf(&scene.host, wrapped, key, true, &content) && marks->lineCount == 3 &&
              marks->lineStarts[0] == 0 && marks->lineStarts[1] == 3 && marks->lineStarts[2] == 7 &&
              marks->wordCount == 3 && marks->words[0].end == 2 && marks->words[1].start == 3 &&
              marks->words[1].end == 5 && marks->words[2].start == 7 && marks->words[2].end == 9 &&
              !marks->selected,
          "lines as painted, words");
    FreeScene(&scene);
}

static void TestMeasuring(void)
{
    Scene scene = MakeScene(NULL);
    muiNodeId node = AddText(&scene, s_nullNode, "ab cd");
    Layout(&scene, node, 1000.0f);
    CHECK(SameSize(Measure(&scene, node, mui_measureMaxContent, 0.0f), 50.0f, 10.0f),
          "max-content: one line of five ems");
    CHECK(SameSize(Measure(&scene, node, mui_measureMinContent, 0.0f), 20.0f, 20.0f),
          "min-content: a line a word, the space hanging");
    CHECK(SameSize(Measure(&scene, node, mui_measureAtMost, 35.0f), 20.0f, 20.0f), "wrapped at 35");
    CHECK(SameSize(Measure(&scene, node, mui_measureAtMost, 50.0f), 50.0f, 10.0f), "fits at 50");
    CHECK(SameSize(Measure(&scene, node, mui_measureExact, 35.0f), 35.0f, 20.0f),
          "an exact width is the width");
    CHECK(SameSize(Measure(&scene, node, mui_measureAtMost, 5.0f), 20.0f, 20.0f),
          "a word wider than the line overflows");
    muiTextStyle style = muiDefaultTextStyle();
    style.wrap = mui_textNoWrap;
    SetText(&scene, node, style, WRAP);
    Layout(&scene, node, 1000.0f);
    CHECK(SameSize(Measure(&scene, node, mui_measureAtMost, 35.0f), 50.0f, 10.0f), "no wrapping");
    style.letterSpacing = (muiDimension){0.0f, 2.0f, mui_dimensionValue};
    style.lineHeight = (muiDimension){1.5f, 0.0f, mui_dimensionValue};
    SetText(&scene, node, style, SPACING | LINE);
    Layout(&scene, node, 1000.0f);
    CHECK(SameSize(Measure(&scene, node, mui_measureMaxContent, 0.0f), 60.0f, 15.0f),
          "spacing after each of five clusters; 1.5 lines");
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
}

// Changing wrapping alone lays the text out again (found by a mutant
// seeing a change only when the automatic line height changed with it).
static void TestWrapChanged(void)
{
    Scene scene = MakeScene(NULL);
    muiNodeId node = AddText(&scene, s_nullNode, "ab cd");
    Layout(&scene, node, 35.0f);
    CHECK(muiNode_GetRect(scene.context, node).height == 20.0f, "wrapped at 35, two lines");
    muiTextStyle style = muiDefaultTextStyle();
    style.wrap = mui_textNoWrap;
    SetText(&scene, node, style, WRAP);
    Layout(&scene, node, 35.0f);
    CHECK(muiNode_GetRect(scene.context, node).height == 10.0f, "not wrapped, one line");
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
}

// Invalid input against a live service counts one misuse each, its
// atlases' and its editing's included; a NULL service, a stale id and a
// query taking the service as const count nothing.
static void TestMisuse(void)
{
    Scene scene = MakeScene(NULL);
    muiTextService* service = scene.service;
    CHECK(muiGetTextServiceMisuse(service) == 0 && muiGetTextServiceMisuse(NULL) == 0, "none yet");
    muiTextBlockId block = {0, 0};
    CHECK(muiCreateTextBlock(service, &(muiTextBlockDef){0}, &block) == mui_errorInvalid &&
              muiGetTextServiceMisuse(service) == 1,
          "a def without its cookie");
    CHECK(muiCreateTextBlock(NULL, &(muiTextBlockDef){0}, &block) == mui_errorInvalid &&
              muiGetTextServiceMisuse(service) == 1,
          "no service: nowhere to count");
    const muiTextBlockDef def = muiDefaultTextBlockDef();
    CHECK(muiCreateTextBlock(service, &def, &block) == mui_success, "a block");
    const muiTextSelection all = {0, {0, mui_affinityDownstream}};
    CHECK(muiTextBlock_Select(service, block, all) == mui_errorInvalid &&
              muiGetTextServiceMisuse(service) == 2,
          "selecting in a block not editing");
    const uint64_t zero = 0;
    CHECK(muiSetFallbackFonts(service, &zero, 1) == mui_errorInvalid &&
              muiGetTextServiceMisuse(service) == 3,
          "a fallback that names nothing");
    muiGlyphAtlas* atlas = NULL;
    const muiGlyphAtlasDef atlasDef = muiDefaultGlyphAtlasDef();
    muiAtlasGlyph glyph = {0};
    const muiLinearColor black = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(muiCreateGlyphAtlas(service, &atlasDef, &atlas) == mui_success &&
              muiGlyphAtlas_GetColor(atlas, 0, 1, 10.0f, 0.0f, 0.0f, 0, black, &glyph) ==
                  mui_errorInvalid &&
              muiGetTextServiceMisuse(service) == 4,
          "a colour glyph from a one-channel atlas, counted on its service");
    muiDestroyGlyphAtlas(atlas);
    CHECK(muiDestroyTextBlock(service, block) == mui_success &&
              muiDestroyTextBlock(service, block) == mui_errorStale &&
              muiGetTextServiceMisuse(service) == 4,
          "a stale id is no misuse");
    CHECK(muiFont_GetMetrics(service, scene.font, NULL) == mui_errorInvalid &&
              muiGetTextServiceMisuse(service) == 4,
          "a const query counts nothing");
    FreeScene(&scene);
}

static void TestLineBreaksInText(void)
{
    Scene scene = MakeScene(NULL);
    const struct
    {
        const char* text;
        float width;
        float lines;
    } cases[] = {
        {"", 0.0f, 0.0f},
        {"a\nbc", 20.0f, 2.0f},
        {"a\n", 10.0f, 2.0f},
        {"a\r\nb", 10.0f, 2.0f},
        {" ", 0.0f, 1.0f},
        {"a  ", 10.0f, 1.0f},
        {"a\xE2\x80\xA8z", 10.0f, 2.0f},
        {"a\t\t", 10.0f, 1.0f},
        {"a\xC2\x85"
         "b",
         10.0f, 2.0f},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        muiNodeId node = AddText(&scene, s_nullNode, cases[i].text);
        Layout(&scene, node, 1000.0f);
        muiSize size = Measure(&scene, node, mui_measureMaxContent, 0.0f);
        CHECK(SameSize(size, cases[i].width, cases[i].lines * 10.0f), cases[i].text);
    }
    FreeScene(&scene);
}

static void TestPainting(void)
{
    Scene scene = MakeScene(NULL);
    muiNodeId root = AddText(&scene, s_nullNode, "ab c");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, 25.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(scene.context, root, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "width");
    Layout(&scene, root, 1000.0f);
    muiDrawList list = Paint(&scene, root);
    CHECK(list.commandCount == 2 && list.glyphCount == 3,
          "two lines, three glyphs: the hanging space draws nothing");
    const muiDrawGlyphRun* first = &list.commands[0].glyphRun;
    const muiDrawGlyphRun* second = &list.commands[1].glyphRun;
    CHECK(first->font == muiFont_GetKey(scene.font) && first->size == 10.0f &&
              first->originY == 8.0f && second->originY == 18.0f,
          "the default font's key; baselines 0.8 em into each line");
    CHECK(first->glyphCount == 2 && list.glyphs[0].id == GLYPH_A && list.glyphs[1].x == 10.0f,
          "the first line");
    CHECK(second->glyphCount == 1 && list.glyphs[2].id == GLYPH_C && list.glyphs[2].x == 0.0f,
          "the second line");

    muiTextStyle style = muiDefaultTextStyle();
    style.align = mui_textAlignEnd;
    SetText(&scene, root, style, ALIGN);
    Layout(&scene, root, 1000.0f);
    list = Paint(&scene, root);
    CHECK(list.glyphs[0].x == 5.0f && list.glyphs[2].x == 15.0f,
          "end alignment, the space hanging");
    style.align = mui_textAlignCenter;
    SetText(&scene, root, style, ALIGN);
    Layout(&scene, root, 1000.0f);
    list = Paint(&scene, root);
    CHECK(list.glyphs[0].x == 2.5f && list.glyphs[2].x == 7.5f, "centered");

    // Right to left, start alignment is to the right; the latin text in
    // it keeps its order.
    layout.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(scene.context, root, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "right to left");
    style.align = mui_textAlignStart;
    SetText(&scene, root, style, ALIGN);
    Layout(&scene, root, 1000.0f);
    list = Paint(&scene, root);
    CHECK(list.glyphs[0].id == GLYPH_A && list.glyphs[0].x == 5.0f && list.glyphs[2].x == 15.0f,
          "start is the right");
    FreeScene(&scene);
}

// Lines of "a" at size 10, each 10 tall, in a scroll container 50 tall,
// scrolled down by scroll; the list painted.
static muiDrawList PaintScrolled(Scene* scene, uint32_t lines, float scroll, float scale)
{
    static char text[2 * 128];
    uint32_t length = 0;
    for (uint32_t i = 0; i < lines; i++)
    {
        text[length++] = 'a';
        text[length++] = '\n';
    }
    text[length - 1] = '\0';
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId root = s_nullNode;
    CHECK(muiCreateNode(scene->context, &def, &root) == mui_success, "root");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, 100.0f, mui_dimensionValue};
    layout.sizing.height = (muiDimension){0.0f, 50.0f, mui_dimensionValue};
    layout.scrollAxes = mui_scrollVertical;
    CHECK(muiNode_SetLayoutValues(scene->context, root, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight) |
                                      MUI_PROPERTY_BIT(mui_propertyScrollAxes)) == mui_success,
          "a scroll container");
    muiNodeId node = AddText(scene, root, text);
    muiLayoutStyle item = muiDefaultLayoutStyle();
    item.item.alignSelf = mui_alignStart;
    CHECK(muiNode_SetLayoutValues(scene->context, node, &item,
                                  MUI_PROPERTY_BIT(mui_propertyAlignSelf)) == mui_success,
          "its full height");
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.scale = (muiLocalScale){scale, 1.0f, 0.0f, 0.0f};
    CHECK(muiNode_SetVisualValues(scene->context, node, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyScaleX)) == mui_success,
          "scaled");
    Layout(scene, root, 1000.0f);
    CHECK(muiNode_SetScroll(scene->context, root, 0.0f, scroll) == mui_success, "scrolled");
    return Paint(scene, root);
}

// Whether a list's runs, one a line, are lines first up to end in order.
static bool PaintsLines(const muiDrawList* list, uint32_t first, uint32_t end)
{
    bool in = list->commandCount == end - first;
    for (uint32_t i = 0; in && i < list->commandCount; i++)
    {
        in = list->commands[i].kind == mui_drawGlyphRun &&
             list->commands[i].glyphRun.originY == (float)(first + i) * 10.0f + 8.0f;
    }
    return in;
}

// A paragraph of many lines paints those that can be seen and one more
// each way; one of few paints them all.
static void TestPaintingWhatIsSeen(void)
{
    Scene scene = MakeScene(NULL);
    muiDrawList list = PaintScrolled(&scene, 100, 0.0f, 1.0f);
    CHECK(PaintsLines(&list, 0, 7), "lines 0 to 5 meet the port, and line 6");
    FreeScene(&scene);
    scene = MakeScene(NULL);
    list = PaintScrolled(&scene, 100, 200.0f, 1.0f);
    CHECK(PaintsLines(&list, 18, 27), "lines 19 to 25 meet it scrolled 200, and one each way");
    FreeScene(&scene);
    scene = MakeScene(NULL);
    list = PaintScrolled(&scene, 100, 950.0f, 1.0f);
    CHECK(PaintsLines(&list, 93, 100), "at the end, lines 94 to 99 and one before");
    FreeScene(&scene);
    scene = MakeScene(NULL);
    list = PaintScrolled(&scene, 64, 200.0f, 1.0f);
    CHECK(PaintsLines(&list, 0, 64), "64 lines are painted whole");
    FreeScene(&scene);
    scene = MakeScene(NULL);
    list = PaintScrolled(&scene, 100, 0.0f, 0.0f);
    CHECK(list.commandCount == 0, "none when nothing can be seen");
    FreeScene(&scene);
}

// Kept lines follow the line scale: a block measured again after its
// size or spacing changes measures as one given them first.
static void TestKeptLines(void)
{
    Scene scene = MakeScene(NULL);
    const char* text = "aaaa bbbb cccc\ndddd";
    muiNodeId kept = AddText(&scene, s_nullNode, text);
    // Laid out at the width measured, so the width alone breaks nothing
    // again.
    Layout(&scene, kept, 100.0f);
    muiSize before = Measure(&scene, kept, mui_measureAtMost, 100.0f);
    CHECK(SameSize(before, 90.0f, 30.0f), "two words, then one, then the last paragraph");
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 20.0f, mui_dimensionValue};
    SetText(&scene, kept, style, SIZE);
    Layout(&scene, kept, 100.0f);
    muiNodeId fresh = AddText(&scene, s_nullNode, text);
    SetText(&scene, fresh, style, SIZE);
    Layout(&scene, fresh, 1000.0f);
    muiSize after = Measure(&scene, kept, mui_measureAtMost, 100.0f);
    muiSize first = Measure(&scene, fresh, mui_measureAtMost, 100.0f);
    CHECK(SameSize(after, 80.0f, 80.0f) && SameSize(after, first.width, first.height),
          "a word a line at size 20, as a block given it first");
    style.letterSpacing = (muiDimension){0.0f, 5.0f, mui_dimensionValue};
    SetText(&scene, kept, style, SIZE | SPACING);
    SetText(&scene, fresh, style, SIZE | SPACING);
    Layout(&scene, kept, 100.0f);
    after = Measure(&scene, kept, mui_measureMaxContent, 0.0f);
    first = Measure(&scene, fresh, mui_measureMaxContent, 0.0f);
    CHECK(after.width == first.width && after.width > 280.0f, "spacing as given first");
    FreeScene(&scene);
}

static void TestLineHeightSpacingAndSlack(void)
{
    Scene scene = MakeScene(NULL);
    muiNodeId node = AddText(&scene, s_nullNode, "ab");
    muiTextStyle style = muiDefaultTextStyle();
    style.lineHeight = (muiDimension){1.5f, 0.0f, mui_dimensionValue};
    style.letterSpacing = (muiDimension){0.0f, 2.0f, mui_dimensionValue};
    SetText(&scene, node, style, LINE | SPACING);
    Layout(&scene, node, 1000.0f);
    muiDrawList list = Paint(&scene, node);
    // Half of 5 units of leading above the ascent: 10.5, snapped to 11.
    CHECK(list.commandCount == 1 && list.commands[0].glyphRun.originY == 11.0f,
          "the baseline under half the leading");
    CHECK(list.glyphs[1].x == 12.0f, "spacing after the first cluster");
    // A width a little under the measured one, as layout may give back,
    // keeps the line whole.
    muiNodeId words = AddText(&scene, s_nullNode, "a b");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, 29.999f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(scene.context, words, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "narrower by a thousandth");
    Layout(&scene, words, 1000.0f);
    CHECK(Paint(&scene, words).commandCount == 1, "one line still");
    FreeScene(&scene);
}

static void TestBidiOrder(void)
{
    Scene scene = MakeScene(NULL);
    // RIGHT-TO-LEFT OVERRIDE: the letters display right to left, and the
    // control itself draws nothing.
    muiNodeId node = AddText(&scene, s_nullNode,
                             "\xE2\x80\xAE"
                             "abc");
    Layout(&scene, node, 1000.0f);
    muiDrawList list = Paint(&scene, node);
    CHECK(list.glyphCount == 3 && list.glyphs[0].id == GLYPH_C && list.glyphs[1].id == GLYPH_B &&
              list.glyphs[2].id == GLYPH_A && list.glyphs[2].x == 20.0f,
          "c b a, left to right");
    // The same block shaped again for another direction: the final
    // exclamation mark takes the paragraph's, so it moves to the left.
    muiNodeId mark = AddText(&scene, s_nullNode, "ab!");
    Layout(&scene, mark, 1000.0f);
    list = Paint(&scene, mark);
    uint32_t exclamation = list.glyphs[2].id;
    CHECK(list.glyphs[0].id == GLYPH_A, "left to right: a b !");
    muiLayoutStyle direction = muiDefaultLayoutStyle();
    direction.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(scene.context, mark, &direction,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "right to left");
    Layout(&scene, mark, 1000.0f);
    list = Paint(&scene, mark);
    CHECK(list.glyphCount == 3 && list.glyphs[0].id == exclamation && list.glyphs[1].id == GLYPH_A,
          "right to left: ! a b");
    // A node takes its direction from its ancestors.
    muiNodeId parent = AddText(&scene, s_nullNode, "");
    muiNodeId child = AddText(&scene, parent, "ab");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.textDirection = mui_textRightToLeft;
    CHECK(!muiNode_IsRightToLeft(scene.context, child) &&
              muiNode_SetLayoutValues(scene.context, parent, &layout,
                                      MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success &&
              muiNode_IsRightToLeft(scene.context, child) && !muiNode_IsRightToLeft(NULL, child) &&
              !muiNode_IsRightToLeft(scene.context, s_nullNode),
          "direction from an ancestor");
    FreeScene(&scene);
}

static void TestFontsAndChanges(void)
{
    Scene scene = MakeScene(NULL);
    muiNodeId node = AddText(&scene, s_nullNode, "abc");
    Layout(&scene, node, 1000.0f);
    CHECK(SameSize(Measure(&scene, node, mui_measureMaxContent, 0.0f), 30.0f, 10.0f), "abc");
    // A changed text measures anew once the node is told.
    muiTextBlockId block = {(uint32_t)muiNode_GetHostKey(scene.context, node),
                            (uint32_t)(muiNode_GetHostKey(scene.context, node) >> 32)};
    CHECK(muiTextBlock_SetText(scene.service, block, "abcd", 4) == mui_success &&
              muiNode_MarkContentChanged(scene.context, node) == mui_success,
          "new text");
    Layout(&scene, node, 1000.0f);
    CHECK(muiNode_GetRect(scene.context, node).width == 40.0f, "laid out anew");
    // A font key that names no font, or no default font, lays out nothing.
    muiTextStyle style = muiDefaultTextStyle();
    style.font = 12345;
    SetText(&scene, node, style, FONT);
    Layout(&scene, node, 1000.0f);
    CHECK(SameSize(Measure(&scene, node, mui_measureMaxContent, 0.0f), 0.0f, 0.0f) &&
              Paint(&scene, node).commandCount == 0,
          "no such font");
    style.font = 0;
    SetText(&scene, node, style, FONT);
    CHECK(muiSetDefaultFont(scene.service, (muiFontId){0, 0}) == mui_success, "none");
    Layout(&scene, node, 1000.0f);
    CHECK(SameSize(Measure(&scene, node, mui_measureMaxContent, 0.0f), 0.0f, 0.0f),
          "no default font");
    CHECK(SameSize(muiMeasureText(&scene.host, node, 99,
                                  (muiMeasureAxis){0.0f, mui_measureMaxContent},
                                  (muiMeasureAxis){0.0f, mui_measureMaxContent}),
                   0.0f, 0.0f) &&
              SameSize(muiMeasureText(NULL, node, 99, (muiMeasureAxis){0.0f, mui_measureMaxContent},
                                      (muiMeasureAxis){0.0f, mui_measureMaxContent}),
                       0.0f, 0.0f),
          "a key that names no block, and no host");
    FreeScene(&scene);
}

static void TestRealShaping(void)
{
    // Liberation Sans kerns A and V, and has a line gap; at its units per
    // em (2048) a unit of size is a unit of the font.
    Scene scene = MakeScene(NULL);
    muiFontDef def = muiDefaultFontDef();
    def.data = s_liberationSans;
    def.size = sizeof s_liberationSans;
    def.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    muiFontMetrics metrics;
    CHECK(muiCreateFont(scene.service, &def, &font) == mui_success &&
              muiFont_GetMetrics(scene.service, font, &metrics) == mui_success &&
              metrics.lineGap > 0.0f,
          "Liberation Sans, with a line gap");
    muiNodeId node = AddText(&scene, s_nullNode, "AV");
    muiTextStyle style = muiDefaultTextStyle();
    style.font = muiFont_GetKey(font);
    style.size = (muiDimension){0.0f, 2048.0f, mui_dimensionValue};
    SetText(&scene, node, style, FONT | SIZE);
    Layout(&scene, node, 100000.0f);
    muiSize size = Measure(&scene, node, mui_measureMaxContent, 0.0f);
    float content = (metrics.ascent + metrics.descent) * 2048.0f;
    CHECK(size.width == 1214.0f + 1366.0f, "A kerned against V: 1366 less 152");
    CHECK(size.height == content + metrics.lineGap * 2048.0f, "the line gap in the line height");
    FreeScene(&scene);
}

// The break test font, at its units per em (1000) so a unit of size is
// a unit of the font: A, V and x 600 wide, a hyphen 300 less 200 before
// V, and a space and x one ligature 850 wide.
static muiNodeId AddBreakTest(Scene* scene, const char* text)
{
    muiFontDef def = muiDefaultFontDef();
    def.data = s_breakTest;
    def.size = sizeof s_breakTest;
    def.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    CHECK(muiCreateFont(scene->service, &def, &font) == mui_success, "the break test font");
    muiNodeId node = AddText(scene, s_nullNode, text);
    muiTextStyle style = muiDefaultTextStyle();
    style.font = muiFont_GetKey(font);
    style.size = (muiDimension){0.0f, 1000.0f, mui_dimensionValue};
    SetText(scene, node, style, FONT | SIZE);
    return node;
}

static void SetWidth(Scene* scene, muiNodeId node, float width)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(scene->context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "width");
}

static void TestUnsafeBreaks(void)
{
    Scene scene = MakeScene(NULL);
    // Kerning across a break: the hyphen ending a line is not kerned
    // against the V that starts the next.
    muiNodeId kerned = AddBreakTest(&scene, "A-V");
    Layout(&scene, kerned, 100000.0f);
    CHECK(SameSize(Measure(&scene, kerned, mui_measureMaxContent, 0.0f), 1300.0f, 1000.0f),
          "kerned on one line");
    CHECK(SameSize(Measure(&scene, kerned, mui_measureAtMost, 1000.0f), 900.0f, 2000.0f),
          "broken after the hyphen: 600 and 300");
    muiTextStyle style = muiDefaultTextStyle();
    style.align = mui_textAlignEnd;
    SetText(&scene, kerned, style, ALIGN);
    SetWidth(&scene, kerned, 1000.0f);
    Layout(&scene, kerned, 100000.0f);
    muiDrawList list = Paint(&scene, kerned);
    CHECK(list.commandCount == 2 && list.glyphCount == 3 && list.glyphs[0].x == 100.0f &&
              list.glyphs[1].x == 700.0f && list.glyphs[2].x == 400.0f,
          "aligned by the widths of the lines as broken");

    // A ligature across a break: the x after the break is drawn.
    muiNodeId ligature = AddBreakTest(&scene, "A x");
    Layout(&scene, ligature, 100000.0f);
    CHECK(SameSize(Measure(&scene, ligature, mui_measureMaxContent, 0.0f), 1450.0f, 1000.0f),
          "the space and x as one glyph");
    SetWidth(&scene, ligature, 1000.0f);
    Layout(&scene, ligature, 100000.0f);
    list = Paint(&scene, ligature);
    CHECK(list.commandCount == 2 && list.glyphCount == 2 && list.glyphs[1].id == 5 &&
              list.commands[1].glyphRun.originY == 1800.0f,
          "x alone on the second line");
    CHECK(SameSize(Measure(&scene, ligature, mui_measureAtMost, 1000.0f), 600.0f, 2000.0f),
          "two lines 600 wide");

    // A line shaped alone in pieces: an override to right to left, its
    // end, then the space and x across the break.
    muiNodeId pieces = AddBreakTest(&scene, "\xE2\x80\xAE"
                                            "AV"
                                            "\xE2\x80\xAC"
                                            " x");
    SetWidth(&scene, pieces, 1300.0f);
    Layout(&scene, pieces, 100000.0f);
    list = Paint(&scene, pieces);
    CHECK(list.commandCount == 2 && list.glyphCount == 3 && list.glyphs[0].id == 4 &&
              list.glyphs[1].id == 3 && list.glyphs[1].x == 600.0f && list.glyphs[2].id == 5,
          "V A, then x: each piece shaped once");
    FreeScene(&scene);
}

static void TestBaselines(void)
{
    Scene scene = MakeScene(NULL);
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId row = s_nullNode;
    CHECK(muiCreateNode(scene.context, &def, &row) == mui_success, "row");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.alignItems = mui_alignBaseline;
    CHECK(muiNode_SetLayoutValues(scene.context, row, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyAlignItems)) == mui_success,
          "aligned by baseline");
    // Ahem's ascent is 0.8 of its size: baselines 8 and 3 + 16 down.
    muiNodeId small = AddText(&scene, row, "ab");
    muiNodeId large = AddText(&scene, row, "ab");
    muiNodeId empty = AddText(&scene, row, "");
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 20.0f, mui_dimensionValue};
    SetText(&scene, large, style, SIZE);
    layout.padding.top = 3.0f;
    CHECK(muiNode_SetLayoutValues(scene.context, large, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyPaddingTop)) == mui_success,
          "padding");
    CHECK(muiTextBaseline(&scene.host, small, muiNode_GetHostKey(scene.context, small), 0.0f,
                          0.0f) == 8.0f &&
              isnan(muiTextBaseline(&scene.host, empty, muiNode_GetHostKey(scene.context, empty),
                                    0.0f, 0.0f)),
          "first baselines");
    const muiLayoutInput input = {500.0f, 1000.0f,         muiMeasureText, &scene.host,
                                  0,      muiTextBaseline, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(scene.context, row, &input) == mui_success, "layout");
    // Empty text has no baseline, so its box's bottom is one.
    muiRect rects[3] = {muiNode_GetRect(scene.context, small),
                        muiNode_GetRect(scene.context, large),
                        muiNode_GetRect(scene.context, empty)};
    CHECK(rects[0].y == 11.0f && rects[1].y == 0.0f && rects[2].y == 19.0f &&
              muiNode_GetRect(scene.context, row).height == 23.0f,
          "baselines line up, the row as tall as the deepest");
    FreeScene(&scene);
}

static void TestMemoryRunningOut(void)
{
    // Each allocation of shaping and painting fails in turn: the block
    // measures as empty, the service counts it, and nothing leaks (the
    // sanitizers check).
    for (int failAt = 1; failAt < 40; failAt++)
    {
        FailingAllocator failing = {0, 0};
        Scene scene = MakeScene(&failing);
        muiNodeId node = AddText(&scene, s_nullNode,
                                 "ab cd\xE2\x80\xAE"
                                 "ef");
        failing = (FailingAllocator){0, failAt};
        Layout(&scene, node, 1000.0f);
        const muiDrawInput input = {1, 1.0f, muiPaintText, &scene.host};
        CHECK(muiBuildDrawList(scene.context, node, &input) == mui_success, "paint");
        bool failed = failing.allocations >= failAt;
        CHECK(failed == (muiGetTextServiceFailures(scene.service) != 0), "counted");
        FreeScene(&scene);
    }
    // And lines shaped alone, at an unsafe break.
    for (int failAt = 1; failAt < 30; failAt++)
    {
        FailingAllocator failing = {0, 0};
        Scene scene = MakeScene(&failing);
        muiNodeId node = AddBreakTest(&scene, "A x A-V");
        SetWidth(&scene, node, 1000.0f);
        failing = (FailingAllocator){0, failAt};
        Layout(&scene, node, 100000.0f);
        const muiDrawInput input = {1, 1.0f, muiPaintText, &scene.host};
        CHECK(muiBuildDrawList(scene.context, node, &input) == mui_success, "paint");
        bool failed = failing.allocations >= failAt;
        CHECK(failed == (muiGetTextServiceFailures(scene.service) != 0), "counted");
        FreeScene(&scene);
    }
    // Setting text keeps the old one when memory runs out.
    FailingAllocator failing = {0, 0};
    Scene scene = MakeScene(&failing);
    muiTextBlockId block = {0, 0};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = "ab";
    blockDef.length = 2;
    CHECK(muiCreateTextBlock(scene.service, &blockDef, &block) == mui_success, "block");
    failing = (FailingAllocator){0, 1};
    blockDef.text = "x";
    blockDef.length = 1;
    CHECK(muiTextBlock_SetText(scene.service, block, "abcd", 4) == mui_errorCapacity &&
              muiCreateTextBlock(scene.service, &blockDef, &block) == mui_success,
          "refused, then fine");
    FreeScene(&scene);
}

int main(void)
{
    TestBlocksAndKeys();
    TestAccessText();
    TestMeasuring();
    TestWrapChanged();
    TestMisuse();
    TestLineBreaksInText();
    TestPainting();
    TestPaintingWhatIsSeen();
    TestKeptLines();
    TestLineHeightSpacingAndSlack();
    TestBidiOrder();
    TestFontsAndChanges();
    TestRealShaping();
    TestUnsafeBreaks();
    TestBaselines();
    TestMemoryRunningOut();
    return s_failures == 0 ? 0 : 1;
}
