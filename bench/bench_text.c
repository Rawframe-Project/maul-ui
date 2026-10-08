// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text timings (record mui-0006) in Liberation Sans at 14 units: a
// column of 2,000 labels of one to four words, laid out cold (which
// shapes every block), at a new width (which only breaks lines), and
// painted; and one paragraph of 4,000 words, measured cold, at ten
// widths, and painted at one; and the labels' glyphs got from a glyph
// atlas, first rendering and packing each, then all found; and distance
// fields of 52 letters at 32 pixels. Prints the best of five runs in
// microseconds.

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"
#include "maul-ui/text_style.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "liberation_sans.inc"

enum
{
    LABELS = 2000,
    WORDS = 4000,
    RUNS = 5,
    TEXT_LIMIT = 40000
};

static const char* const s_words[] = {
    "the",    "layout", "of",    "text",  "breaks", "lines", "where", "Unicode",
    "allows", "and",    "glyph", "runs",  "follow", "each",  "item",  "shaped",
    "once",   "for",    "every", "width", "button", "label", "menu",  "settings",
};

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static uint32_t Next(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

// Words picked by a fixed sequence, separated by spaces.
static size_t MakeText(char* out, size_t capacity, uint32_t words, uint32_t* state)
{
    size_t length = 0;
    for (uint32_t i = 0; i < words; i++)
    {
        const char* word = s_words[Next(state) % (sizeof s_words / sizeof s_words[0])];
        size_t size = strlen(word);
        if (length + size + 1 >= capacity)
        {
            break;
        }
        if (i != 0)
        {
            out[length++] = ' ';
        }
        memcpy(out + length, word, size);
        length += size;
    }
    return length;
}

static void Check(muiResult result, const char* what)
{
    if (result != mui_success)
    {
        fprintf(stderr, "%s failed: %s\n", what, muiResultName(result));
    }
}

typedef struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    muiNodeId root;
} Scene;

static Scene MakeScene(void)
{
    Scene scene = {0};
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    serviceDef.limits.textBlocks = LABELS + 1;
    Check(muiCreateTextService(&serviceDef, &scene.service), "service");
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = s_liberationSans;
    fontDef.size = sizeof s_liberationSans;
    fontDef.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    Check(muiCreateFont(scene.service, &fontDef, &font), "font");
    Check(muiSetDefaultFont(scene.service, font), "default font");
    muiContextDef contextDef = muiDefaultContextDef();
    contextDef.limits.drawGlyphs = 1u << 18;
    Check(muiCreateContext(&contextDef, &scene.context), "context");
    scene.host = (muiTextHost){scene.service, scene.context};
    muiNodeDef def = muiDefaultNodeDef();
    Check(muiCreateNode(scene.context, &def, &scene.root), "root");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    Check(muiNode_SetLayoutValues(scene.context, scene.root, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyFlexDirection)),
          "column");
    muiTextStyle text = muiDefaultTextStyle();
    text.size = (muiDimension){0.0f, 14.0f, mui_dimensionValue};
    Check(muiNode_SetTextValues(scene.context, scene.root, &text,
                                MUI_PROPERTY_BIT(mui_propertyFontSize)),
          "size");
    return scene;
}

static muiNodeId AddText(Scene* scene, const char* text, size_t length)
{
    muiTextBlockId block = {0, 0};
    Check(muiCreateTextBlock(scene->service, text, length, &block), "block");
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = {0, 0};
    Check(muiCreateNode(scene->context, &def, &node), "node");
    Check(muiNode_InsertChild(scene->context, scene->root, node, (muiNodeId){0, 0}), "insert");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    Check(muiNode_SetLayoutValues(scene->context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)),
          "content");
    return node;
}

static double TimeLayout(Scene* scene, float width)
{
    const muiLayoutInput input = {width, 1e9f, muiMeasureText, &scene->host, 0, NULL, {0, 0, 0, 0}};
    double start = Seconds();
    Check(muiComputeLayout(scene->context, scene->root, &input), "layout");
    return (Seconds() - start) * 1e6;
}

static double TimePaint(Scene* scene)
{
    const muiDrawInput input = {1, 1.0f, muiPaintText, &scene->host};
    double start = Seconds();
    Check(muiBuildDrawList(scene->context, scene->root, &input), "paint");
    return (Seconds() - start) * 1e6;
}

// Gets every glyph of the list from the atlas, as a renderer drawing it
// at a scale of 1 does.
static double TimeAtlas(Scene* scene, muiGlyphAtlas* atlas)
{
    muiDrawList list;
    Check(muiGetDrawList(scene->context, &list), "list");
    double start = Seconds();
    for (uint32_t i = 0; i < list.commandCount; i++)
    {
        const muiDrawGlyphRun* run = &list.commands[i].glyphRun;
        for (uint32_t g = run->firstGlyph; g < run->firstGlyph + run->glyphCount; g++)
        {
            muiAtlasGlyph out;
            Check(muiGlyphAtlas_Get(atlas, run->font, list.glyphs[g].id, run->size,
                                    run->originX + list.glyphs[g].x,
                                    run->originY + list.glyphs[g].y, &out),
                  "atlas");
        }
    }
    return (Seconds() - start) * 1e6;
}

static void Keep(double* best, double time)
{
    *best = time < *best ? time : *best;
}

static void RunLabels(void)
{
    double best[5] = {1e30, 1e30, 1e30, 1e30, 1e30};
    char text[256];
    for (int run = 0; run < RUNS; run++)
    {
        Scene scene = MakeScene();
        uint32_t state = 7;
        for (uint32_t i = 0; i < LABELS; i++)
        {
            size_t length = MakeText(text, sizeof text, 1 + Next(&state) % 4, &state);
            (void)AddText(&scene, text, length);
        }
        Keep(&best[0], TimeLayout(&scene, 300.0f));
        Keep(&best[1], TimeLayout(&scene, 120.0f));
        Keep(&best[2], TimePaint(&scene));
        muiGlyphAtlasDef atlasDef = muiDefaultGlyphAtlasDef();
        muiGlyphAtlas* atlas = NULL;
        Check(muiCreateGlyphAtlas(scene.service, &atlasDef, &atlas), "atlas");
        Keep(&best[3], TimeAtlas(&scene, atlas));
        muiGlyphAtlas_NextFrame(atlas);
        Keep(&best[4], TimeAtlas(&scene, atlas));
        muiDestroyGlyphAtlas(atlas);
        muiDestroyContext(scene.context);
        muiDestroyTextService(scene.service);
    }
    printf("labels    cold        %10.1f us\n", best[0]);
    printf("labels    new width   %10.1f us\n", best[1]);
    printf("labels    paint       %10.1f us\n", best[2]);
    printf("atlas     cold        %10.1f us\n", best[3]);
    printf("atlas     found       %10.1f us\n", best[4]);
}

static void RunParagraph(void)
{
    static char text[TEXT_LIMIT];
    double best[3] = {1e30, 1e30, 1e30};
    for (int run = 0; run < RUNS; run++)
    {
        Scene scene = MakeScene();
        uint32_t state = 11;
        size_t length = MakeText(text, sizeof text, WORDS, &state);
        (void)AddText(&scene, text, length);
        Keep(&best[0], TimeLayout(&scene, 600.0f));
        double widths = 0.0;
        for (int i = 0; i < 10; i++)
        {
            widths += TimeLayout(&scene, 300.0f + (float)i * 40.0f);
        }
        Keep(&best[1], widths / 10.0);
        Keep(&best[2], TimePaint(&scene));
        muiDestroyContext(scene.context);
        muiDestroyTextService(scene.service);
    }
    printf("paragraph cold        %10.1f us\n", best[0]);
    printf("paragraph a width     %10.1f us\n", best[1]);
    printf("paragraph paint       %10.1f us\n", best[2]);
}

// Typing a letter in the middle of a long editing block, in paragraphs
// of 60 words, and laying it out and painting it again: the time a
// keystroke takes, over 20 of them.
static double TimeTyping(uint32_t words)
{
    static char text[200000];
    Scene scene = MakeScene();
    uint32_t state = 13;
    size_t length = MakeText(text, sizeof text, words, &state);
    for (size_t i = 0, spaces = 0; i < length; i++)
    {
        spaces += text[i] == ' ' ? 1 : 0;
        text[i] = text[i] == ' ' && spaces % 60 == 0 ? '\n' : text[i];
    }
    muiTextBlockId block = {0, 0};
    Check(muiCreateTextBlock(scene.service, text, length, &block), "block");
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(block);
    // In a view 600 tall that scrolls, as an editor shows it.
    muiNodeDef viewDef = muiDefaultNodeDef();
    muiNodeId view = {0, 0};
    Check(muiCreateNode(scene.context, &viewDef, &view), "view");
    Check(muiNode_InsertChild(scene.context, scene.root, view, (muiNodeId){0, 0}), "insert");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, 600.0f, mui_dimensionValue};
    layout.sizing.height = (muiDimension){0.0f, 600.0f, mui_dimensionValue};
    layout.scrollAxes = mui_scrollVertical;
    Check(muiNode_SetLayoutValues(scene.context, view, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight) |
                                      MUI_PROPERTY_BIT(mui_propertyScrollAxes)),
          "a view");
    muiNodeId node = {0, 0};
    Check(muiCreateNode(scene.context, &def, &node), "node");
    Check(muiNode_InsertChild(scene.context, view, node, (muiNodeId){0, 0}), "insert");
    layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    layout.item.alignSelf = mui_alignStart;
    Check(muiNode_SetLayoutValues(scene.context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent) |
                                      MUI_PROPERTY_BIT(mui_propertyAlignSelf)),
          "content");
    muiTextEditDef edit = muiDefaultTextEditDef();
    edit.flags = mui_editMultiline;
    Check(muiTextBlock_SetEditing(scene.service, block, &edit), "editing");
    uint32_t middle = (uint32_t)length / 2;
    while (middle < length && text[middle] != ' ')
    {
        middle++;
    }
    const muiTextPosition at = {middle, mui_affinityDownstream};
    Check(muiTextBlock_Select(scene.service, block, (muiTextSelection){middle, at}), "select");
    (void)TimeLayout(&scene, 600.0f);
    // Scrolled to the middle, where the typing is.
    Check(muiNode_SetScroll(scene.context, view, 0.0f,
                            muiNode_GetContentRect(scene.context, node).height / 2.0f),
          "scrolled");
    (void)TimePaint(&scene);
    double total = 0.0;
    for (int key = 0; key < 20; key++)
    {
        double start = Seconds();
        Check(muiTextBlock_Type(scene.service, block, "a", 1, NULL), "type");
        Check(muiNode_MarkContentChanged(scene.context, node), "changed");
        const muiLayoutInput input = {600.0f, 1e9f, muiMeasureText, &scene.host,
                                      0,      NULL, {0, 0, 0, 0}};
        Check(muiComputeLayout(scene.context, scene.root, &input), "layout");
        const muiDrawInput draw = {1, 1.0f, muiPaintText, &scene.host};
        Check(muiBuildDrawList(scene.context, scene.root, &draw), "paint");
        total += (Seconds() - start) * 1e6;
    }
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
    return total / 20.0;
}

static void RunTyping(void)
{
    double best[2] = {1e30, 1e30};
    for (int run = 0; run < RUNS; run++)
    {
        Keep(&best[0], TimeTyping(4000));
        Keep(&best[1], TimeTyping(16000));
    }
    printf("typing    25 KB       %10.1f us\n", best[0]);
    printf("typing    100 KB      %10.1f us\n", best[1]);
}

// Liberation Sans's A to Z and a to z are glyphs 36 to 87.
static void RunFields(void)
{
    static unsigned char pixels[1 << 16];
    Scene scene = MakeScene();
    double best = 1e30;
    for (int run = 0; run < RUNS; run++)
    {
        double start = Seconds();
        for (uint32_t glyph = 36; glyph < 88; glyph++)
        {
            muiGlyphImage image;
            Check(muiRenderGlyphField(scene.service, 0, glyph, 32.0f, 4, &image, pixels,
                                      sizeof pixels),
                  "field");
        }
        Keep(&best, (Seconds() - start) * 1e6 / 52.0);
    }
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
    printf("field     a letter    %10.1f us\n", best);
}

int main(void)
{
    RunLabels();
    RunParagraph();
    RunFields();
    RunTyping();
    return 0;
}
