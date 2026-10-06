// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The draw-command list: golden lists of scenes in a text form, bytes
// equal across builds, snapping, colors, clips, opacity and limits
// (record mui-0005). Run with MAUL_UI_UPDATE_GOLDEN=1 to rewrite
// test/draw_golden.inc from the current output.

// The update path reads the environment and writes a file with the C
// library's portable calls, which Microsoft's C library marks deprecated.
#define _CRT_SECURE_NO_WARNINGS

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/text_style.h"
#include "maul-ui/visual.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct Golden
{
    const char* name;
    const char* text;
} Golden;

static const Golden s_golden[] = {
#include "draw_golden.inc"
    {NULL, NULL},
};

static const muiNodeId s_nullNode = {0, 0};

#define WIDTH           MUI_PROPERTY_BIT(mui_propertyWidth)
#define HEIGHT          MUI_PROPERTY_BIT(mui_propertyHeight)
#define TEXT_LIMIT      16384
#define MAX_GOLDEN      16
#define UPDATE_VARIABLE "MAUL_UI_UPDATE_GOLDEN"

static const muiColor s_red = {1.0f, 0.0f, 0.0f, 1.0f};
static const muiColor s_blue = {0.0f, 0.0f, 1.0f, 1.0f};
static const muiColor s_gray = {0.5f, 0.5f, 0.5f, 1.0f};

// The text of a list, built up line by line.
typedef struct Text
{
    char buffer[TEXT_LIMIT];
    size_t length;
} Text;

static void Append(Text* text, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int written =
        vsnprintf(text->buffer + text->length, TEXT_LIMIT - text->length, format, arguments);
    va_end(arguments);
    CHECK(written >= 0 && (size_t)written < TEXT_LIMIT - text->length, "text fits");
    if (written > 0 && (size_t)written < TEXT_LIMIT - text->length)
    {
        text->length += (size_t)written;
    }
}

static void AppendRect(Text* text, const char* name, muiRect r)
{
    Append(text, " %s=%.9g,%.9g,%.9g,%.9g", name, (double)r.x, (double)r.y, (double)r.width,
           (double)r.height);
}

static void AppendColor(Text* text, const char* name, muiLinearColor c)
{
    Append(text, " %s=%.9g,%.9g,%.9g,%.9g", name, (double)c.r, (double)c.g, (double)c.b,
           (double)c.a);
}

static void AppendCorners(Text* text, muiCorners c)
{
    Append(text, " radii=%.9g,%.9g,%.9g,%.9g", (double)c.topLeft, (double)c.topRight,
           (double)c.bottomRight, (double)c.bottomLeft);
}

static void AppendSides(Text* text, const char* name, muiSides s)
{
    Append(text, " %s=%.9g,%.9g,%.9g,%.9g", name, (double)s.top, (double)s.right, (double)s.bottom,
           (double)s.left);
}

static void AppendCommand(Text* text, const muiDrawCommand* command)
{
    switch (command->kind)
    {
    case mui_drawBox:
        Append(text, "box clip=%u", command->clip);
        AppendRect(text, "rect", command->box.rect);
        AppendCorners(text, command->box.radii);
        AppendColor(text, "fill", command->box.fill);
        Append(text, " gradient=%u", command->box.gradient);
        AppendSides(text, "borders", command->box.borderWidths);
        for (int i = 0; i < 4; i++)
        {
            AppendColor(text, "border", command->box.borderColors[i]);
        }
        break;
    case mui_drawShadow:
        Append(text, "shadow clip=%u inset=%u", command->clip, command->shadow.inset);
        AppendRect(text, "rect", command->shadow.rect);
        AppendCorners(text, command->shadow.radii);
        AppendColor(text, "color", command->shadow.color);
        Append(text, " offset=%.9g,%.9g blur=%.9g spread=%.9g", (double)command->shadow.offsetX,
               (double)command->shadow.offsetY, (double)command->shadow.blur,
               (double)command->shadow.spread);
        break;
    case mui_drawGlyphRun:
        Append(text, "glyphs clip=%u font=%llu size=%.9g origin=%.9g,%.9g first=%u count=%u",
               command->clip, (unsigned long long)command->glyphRun.font,
               (double)command->glyphRun.size, (double)command->glyphRun.originX,
               (double)command->glyphRun.originY, command->glyphRun.firstGlyph,
               command->glyphRun.glyphCount);
        AppendColor(text, "color", command->glyphRun.color);
        break;
    default:
        Append(text, "image clip=%u key=%llu", command->clip,
               (unsigned long long)command->image.image);
        AppendRect(text, "rect", command->image.rect);
        AppendRect(text, "uv", command->image.uv);
        AppendSides(text, "slice", command->image.slice);
        AppendColor(text, "tint", command->image.tint);
        break;
    }
    Append(text, " transform=%u\n", command->transform);
}

static void Describe(const muiDrawList* list, Text* text)
{
    text->length = 0;
    text->buffer[0] = '\0';
    Append(text, "header surface=%llu size=%.9g,%.9g scale=%.9g\n",
           (unsigned long long)list->header.surface, (double)list->header.width,
           (double)list->header.height, (double)list->header.scale);
    for (uint32_t i = 1; i < list->clipCount; i++)
    {
        const muiDrawClip* clip = &list->clips[i];
        Append(text, "clip %u parent=%u invert=%u", i, clip->parent, clip->invert);
        AppendRect(text, "rect", clip->rect);
        AppendCorners(text, clip->radii);
        Append(text, "\n");
    }
    for (uint32_t i = 1; i < list->gradientCount; i++)
    {
        const muiDrawGradient* gradient = &list->gradients[i];
        Append(text, "gradient %u kind=%u interpolation=%u angle=%.9g", i, gradient->kind,
               gradient->interpolation, (double)gradient->angle);
        for (uint32_t s = 0; s < gradient->stopCount; s++)
        {
            Append(text, " at=%.9g", (double)gradient->positions[s]);
            AppendColor(text, "color", gradient->colors[s]);
        }
        Append(text, "\n");
    }
    for (uint32_t i = 0; i < list->glyphCount; i++)
    {
        Append(text, "glyph %u id=%u at=%.9g,%.9g\n", i, list->glyphs[i].id,
               (double)list->glyphs[i].x, (double)list->glyphs[i].y);
    }
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        AppendCommand(text, &list->commands[i]);
    }
}

// Golden texts of this run, for rewriting the file.
static Golden s_seen[MAX_GOLDEN];
static char s_seenText[MAX_GOLDEN][TEXT_LIMIT];
static int s_seenCount = 0;

static void CheckGolden(const char* name, const muiDrawList* list)
{
    Text text;
    Describe(list, &text);
    if (s_seenCount < MAX_GOLDEN)
    {
        memcpy(s_seenText[s_seenCount], text.buffer, text.length + 1);
        s_seen[s_seenCount] = (Golden){name, s_seenText[s_seenCount]};
        s_seenCount++;
    }
    for (const Golden* golden = s_golden; golden->name != NULL; golden++)
    {
        if (strcmp(golden->name, name) == 0)
        {
            bool same = strcmp(golden->text, text.buffer) == 0;
            if (!same)
            {
                printf("--- golden %s\n%s+++ now\n%s", name, golden->text, text.buffer);
            }
            CHECK(same || getenv(UPDATE_VARIABLE) != NULL, "the golden list");
            return;
        }
    }
    CHECK(getenv(UPDATE_VARIABLE) != NULL, "a golden list for every scene");
}

// Rewrites test/draw_golden.inc beside this file.
static void WriteGolden(void)
{
    char path[1024];
    const char* file = __FILE__;
    const char* slash = strrchr(file, '/');
    size_t directory = slash != NULL ? (size_t)(slash - file + 1) : 0;
    CHECK(directory + 32 < sizeof path, "path fits");
    memcpy(path, file, directory);
    const char name[] = "draw_golden.inc";
    memcpy(path + directory, name, sizeof name);
    FILE* out = fopen(path, "w");
    CHECK(out != NULL, "open the golden file");
    if (out == NULL)
    {
        return;
    }
    fprintf(out,
            "// SPDX-License-Identifier: MIT\n// Copyright (c) 2026 Sirac Ozmen\n//\n"
            "// Golden draw lists, written by test_draw with %s=1.\n\n",
            UPDATE_VARIABLE);
    for (int i = 0; i < s_seenCount; i++)
    {
        fprintf(out, "{\"%s\",\n", s_seen[i].name);
        for (const char* line = s_seen[i].text; *line != '\0';)
        {
            const char* end = strchr(line, '\n');
            size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
            fprintf(out, " \"%.*s\\n\"\n", (int)length, line);
            line += length + (end != NULL ? 1 : 0);
        }
        fprintf(out, "},\n");
    }
    fclose(out);
}

static muiContext* MakeContextWith(muiLimits limits)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits = limits;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create context");
    return context;
}

static muiContext* MakeContext(void)
{
    return MakeContextWith(muiDefaultContextDef().limits);
}

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

// A node of a size, a child of parent unless that is null, laid out in a
// row with its padding.
static muiNodeId Add(muiContext* context, muiNodeId parent, float width, float height,
                     muiEdges padding)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "create node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = Length(width);
    layout.sizing.height = Length(height);
    layout.padding = padding;
    CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                  WIDTH | HEIGHT | MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingTop)) == mui_success,
          "layout");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    return node;
}

static void SetVisual(muiContext* context, muiNodeId node, const muiVisualStyle* visual,
                      muiPropertyMask mask)
{
    CHECK(muiNode_SetVisualValues(context, node, visual, mask) == mui_success, "visual");
}

static void SetBorder(muiContext* context, muiNodeId node, muiEdges border)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.border = border;
    CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyBorderStart) |
                                      MUI_PROPERTY_BIT(mui_propertyBorderEnd) |
                                      MUI_PROPERTY_BIT(mui_propertyBorderTop) |
                                      MUI_PROPERTY_BIT(mui_propertyBorderBottom)) == mui_success,
          "border");
}

static muiDrawList Build(muiContext* context, muiNodeId root, float scale)
{
    const muiLayoutInput layout = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &layout) == mui_success, "layout");
    const muiDrawInput input = {7, scale, NULL, NULL};
    CHECK(muiBuildDrawList(context, root, &input) == mui_success, "build");
    muiDrawList list;
    CHECK(muiGetDrawList(context, &list) == mui_success, "get");
    return list;
}

// Builds again with nothing changed, which keeps the list, then with
// the root asked to repaint, which copies its children, and compares the
// bytes of the commands each time.
static void CheckBytesRepeat(muiContext* context, muiNodeId root, float scale)
{
    muiDrawList first = Build(context, root, scale);
    size_t size = first.commandCount * sizeof(muiDrawCommand);
    unsigned char* copy = malloc(size + 1);
    CHECK(copy != NULL, "copy");
    if (copy == NULL)
    {
        return;
    }
    memcpy(copy, first.commands, size);
    muiDrawList second = Build(context, root, scale);
    CHECK(second.commands == first.commands && second.header.generation == first.header.generation,
          "a static frame keeps the list");
    muiVisualStyle visual = muiDefaultVisualStyle();
    CHECK(muiNode_GetVisualStyle(context, root, &visual) == mui_success &&
              muiNode_SetVisualValues(context, root, &visual,
                                      MUI_PROPERTY_BIT(mui_propertyOpacity)) == mui_success,
          "the same opacity, which asks for paint");
    muiDrawList third = Build(context, root, scale);
    CHECK(third.commands != first.commands &&
              third.header.generation == first.header.generation + 1 &&
              third.commandCount == first.commandCount && memcmp(copy, third.commands, size) == 0,
          "a rebuild from the last list gives the same bytes");
    free(copy);
}

static muiVisualStyle Boxed(void)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = s_red;
    const muiDimension radius = {0.0f, 8.0f, mui_dimensionValue};
    visual.radius = (muiCornerRadii){radius, radius, radius, radius};
    visual.borderColor = (muiEdgeColors){s_blue, s_gray, s_blue, s_gray};
    return visual;
}

#define BOXED                                                                                      \
    (MUI_PROPERTY_BIT(mui_propertyBackground) | MUI_PROPERTY_BIT(mui_propertyRadiusTopStart) |     \
     MUI_PROPERTY_BIT(mui_propertyRadiusTopEnd) | MUI_PROPERTY_BIT(mui_propertyRadiusBottomEnd) |  \
     MUI_PROPERTY_BIT(mui_propertyRadiusBottomStart) |                                             \
     MUI_PROPERTY_BIT(mui_propertyBorderColorStart) |                                              \
     MUI_PROPERTY_BIT(mui_propertyBorderColorEnd) | MUI_PROPERTY_BIT(mui_propertyBorderColorTop) | \
     MUI_PROPERTY_BIT(mui_propertyBorderColorBottom))

static void TestBoxes(void)
{
    muiContext* context = MakeContext();
    const muiEdges none = {0};
    muiNodeId root = Add(context, s_nullNode, 200.0f, 100.0f, (muiEdges){10.0f, 0.0f, 10.0f, 0.0f});
    muiVisualStyle visual = Boxed();
    SetVisual(context, root, &visual, BOXED);
    SetBorder(context, root, (muiEdges){2.0f, 4.0f, 1.0f, 3.0f});
    // A child with no visual values draws nothing.
    (void)Add(context, root, 50.0f, 50.0f, none);
    muiNodeId pill = Add(context, root, 60.0f, 20.0f, none);
    visual.radius.topStart = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    visual.radius.bottomEnd = (muiDimension){0.25f, 1.0f, mui_dimensionValue};
    SetVisual(context, pill, &visual, BOXED);
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 2 && list.commands[0].kind == mui_drawBox, "two boxes");
    CHECK(list.commands[1].box.radii.topLeft == 10.0f &&
              list.commands[1].box.radii.bottomRight == 6.0f,
          "a radius held to half the shorter side, a scaled one of it");
    CHECK(list.header.surface == 7 && list.header.width == 200.0f && list.header.scale == 1.0f,
          "the header");
    CheckGolden("boxes", &list);
    CheckBytesRepeat(context, root, 1.0f);
    muiDestroyContext(context);
}

static void TestSnapping(void)
{
    muiContext* context = MakeContext();
    const muiEdges none = {0};
    muiNodeId root = Add(context, s_nullNode, 100.0f, 40.0f, (muiEdges){10.3f, 0.0f, 0.2f, 0.0f});
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = s_gray;
    muiNodeId a = Add(context, root, 20.4f, 10.0f, none);
    muiNodeId b = Add(context, root, 0.1f, 10.0f, none);
    muiNodeId c = Add(context, root, 7.25f, 10.0f, none);
    SetVisual(context, a, &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
    SetVisual(context, b, &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
    SetVisual(context, c, &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
    SetBorder(context, c, (muiEdges){0.3f, 2.7f, 1.0f, 0.0f});
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 3, "three boxes");
    const muiDrawBox* first = &list.commands[0].box;
    const muiDrawBox* second = &list.commands[1].box;
    CHECK(first->rect.x == 10.0f && first->rect.width == 21.0f && first->rect.y == 0.0f,
          "10.3 to 30.7 snaps to 10 to 31");
    CHECK(second->rect.x == 31.0f && second->rect.width == 1.0f,
          "adjacent edges meet, and a sliver keeps a pixel");
    CHECK(list.commands[2].box.borderWidths.left == 1.0f &&
              list.commands[2].box.borderWidths.right == 2.0f &&
              list.commands[2].box.borderWidths.bottom == 0.0f,
          "borders in whole pixels, at least one");
    CheckGolden("snap-1x", &list);
    list = Build(context, root, 2.0f);
    CHECK(list.commands[0].box.rect.x == 10.5f && list.commands[2].box.borderWidths.left == 0.5f,
          "at twice the density, half units");
    CheckGolden("snap-2x", &list);
    muiDestroyContext(context);
}

static void TestClipsShadowsImagesGradients(void)
{
    muiContext* context = MakeContext();
    const muiEdges none = {0};
    muiNodeId root = Add(context, s_nullNode, 300.0f, 200.0f, (muiEdges){20.0f, 0.0f, 20.0f, 0.0f});
    muiVisualStyle visual = Boxed();
    visual.clip = true;
    visual.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f, 4.0f, 8.0f, 1.0f};
    visual.innerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.25f}, 1.0f, 1.0f, 2.0f, 0.0f};
    SetVisual(context, root, &visual,
              BOXED | MUI_PROPERTY_BIT(mui_propertyClip) |
                  MUI_PROPERTY_BIT(mui_propertyOuterShadow) |
                  MUI_PROPERTY_BIT(mui_propertyInnerShadow));
    SetBorder(context, root, (muiEdges){2.0f, 2.0f, 2.0f, 2.0f});
    muiNodeId panel = Add(context, root, 100.0f, 80.0f, none);
    muiVisualStyle inner = muiDefaultVisualStyle();
    inner.clip = true;
    inner.gradient = (muiGradient){mui_gradientLinear, 2, 90.0f, {{s_red, 0.0f}, {s_blue, 1.0f}}};
    inner.image = 42;
    inner.imageSlice = (muiEdges){4.0f, 6.0f, 2.0f, 3.0f};
    inner.imageTint = s_gray;
    SetVisual(context, panel, &inner,
              MUI_PROPERTY_BIT(mui_propertyClip) | MUI_PROPERTY_BIT(mui_propertyGradient) |
                  MUI_PROPERTY_BIT(mui_propertyImage) | MUI_PROPERTY_BIT(mui_propertyImageSlice) |
                  MUI_PROPERTY_BIT(mui_propertyImageTint));
    muiNodeId leaf = Add(context, panel, 30.0f, 30.0f, none);
    muiVisualStyle plain = muiDefaultVisualStyle();
    plain.background = s_gray;
    SetVisual(context, leaf, &plain, MUI_PROPERTY_BIT(mui_propertyBackground));
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.clipCount == 3 && list.clips[2].parent == 1, "a clip inside a clip");
    CHECK(list.commandCount == 6 && list.commands[0].kind == mui_drawShadow &&
              list.commands[1].kind == mui_drawBox && list.commands[2].kind == mui_drawShadow &&
              list.commands[3].kind == mui_drawBox && list.commands[4].kind == mui_drawImage &&
              list.commands[5].kind == mui_drawBox,
          "paint order");
    CHECK(list.commands[0].clip == 0 && list.commands[3].clip == 1 && list.commands[5].clip == 2,
          "each command in the clip it is painted in");
    CHECK(list.commands[2].shadow.inset == 1 && list.commands[2].shadow.rect.x == 2.0f &&
              list.commands[2].shadow.radii.topLeft == 6.0f,
          "the inner shadow in the padding box");
    CHECK(list.gradientCount == 2 && list.commands[3].box.gradient == 1 &&
              list.gradients[1].interpolation == mui_interpolateOklab,
          "a gradient");
    CheckGolden("clips-shadows-images-gradients", &list);
    CheckBytesRepeat(context, root, 1.0f);
    muiDestroyContext(context);
}

static void TestColorsAndOpacity(void)
{
    muiContext* context = MakeContext();
    const muiEdges none = {0};
    muiNodeId root = Add(context, s_nullNode, 100.0f, 100.0f, none);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){0.5f, 0.5f, 0.5f, 0.5f};
    visual.opacity = 0.5f;
    SetVisual(context, root, &visual,
              MUI_PROPERTY_BIT(mui_propertyBackground) | MUI_PROPERTY_BIT(mui_propertyOpacity));
    muiNodeId child = Add(context, root, 10.0f, 10.0f, none);
    visual.background = s_red;
    SetVisual(context, child, &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
    muiNodeId hidden = Add(context, root, 10.0f, 10.0f, none);
    visual.opacity = 0.0f;
    SetVisual(context, hidden, &visual,
              MUI_PROPERTY_BIT(mui_propertyBackground) | MUI_PROPERTY_BIT(mui_propertyOpacity));
    (void)Add(context, hidden, 5.0f, 5.0f, none);
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 2, "a node of no opacity draws nothing below it");
    const muiLinearColor fill = list.commands[0].box.fill;
    // sRGB 0.5 is 0.214041 in linear light; alpha 0.5 times opacity 0.5.
    CHECK(fill.a == 0.25f && fabsf(fill.r - 0.2140411f * 0.25f) < 1e-6f && fill.r == fill.b,
          "linear light, premultiplied, times opacity");
    CHECK(list.commands[1].box.fill.a == 0.5f && list.commands[1].box.fill.r == 0.5f,
          "a child of its own opacity 1 takes its parent's");
    CheckGolden("opacity", &list);
    muiDestroyContext(context);
}

static void TestRightToLeft(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, 100.0f, 50.0f, (muiEdges){0});
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, root, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "rtl");
    muiVisualStyle visual = Boxed();
    visual.radius.topStart = (muiDimension){0.0f, 12.0f, mui_dimensionValue};
    visual.image = 9;
    visual.imageSlice = (muiEdges){1.0f, 2.0f, 3.0f, 4.0f};
    SetVisual(context, root, &visual,
              BOXED | MUI_PROPERTY_BIT(mui_propertyImage) |
                  MUI_PROPERTY_BIT(mui_propertyImageSlice));
    SetBorder(context, root, (muiEdges){1.0f, 3.0f, 0.0f, 0.0f});
    muiDrawList list = Build(context, root, 1.0f);
    const muiDrawBox* box = &list.commands[0].box;
    CHECK(box->radii.topRight == 12.0f && box->radii.topLeft == 8.0f, "start is right");
    CHECK(box->borderWidths.right == 1.0f && box->borderWidths.left == 3.0f, "borders mirror");
    CHECK(list.commands[1].image.slice.left == 1.0f, "an image does not mirror");
    CheckGolden("right-to-left", &list);
    muiDestroyContext(context);
}

static void TestArgumentsAndLimits(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, 10.0f, 10.0f, (muiEdges){0});
    muiDrawList list;
    CHECK(muiGetDrawList(context, &list) == mui_success && list.commandCount == 0 &&
              list.clipCount == 1 && list.gradientCount == 1 && list.transformCount == 1 &&
              list.transforms[0].a == 1.0f && list.transforms[0].d == 1.0f &&
              list.header.generation == 0,
          "empty before any build");
    muiDrawInput input = {1, 1.0f, NULL, NULL};
    CHECK(muiBuildDrawList(NULL, root, &input) == mui_errorInvalid &&
              muiBuildDrawList(context, root, NULL) == mui_errorInvalid &&
              muiBuildDrawList(context, s_nullNode, &input) == mui_errorInvalid &&
              muiGetDrawList(NULL, &list) == mui_errorInvalid &&
              muiGetDrawList(context, NULL) == mui_errorInvalid,
          "null arguments");
    input.scale = 0.0f;
    CHECK(muiBuildDrawList(context, root, &input) == mui_errorInvalid, "scale 0");
    input.scale = NAN;
    CHECK(muiBuildDrawList(context, root, &input) == mui_errorInvalid, "scale NaN");
    input.scale = INFINITY;
    CHECK(muiBuildDrawList(context, root, &input) == mui_errorInvalid, "scale infinite");
    muiNodeId gone = Add(context, s_nullNode, 1.0f, 1.0f, (muiEdges){0});
    CHECK(muiDestroyNode(context, gone) == mui_success, "destroy");
    input.scale = 1.0f;
    CHECK(muiBuildDrawList(context, gone, &input) == mui_errorStale, "a gone root");
    muiDestroyContext(context);

    // Each table's limit fails the build whole, leaving the list empty.
    const char* names[3] = {"commands", "clips", "gradients"};
    for (int which = 0; which < 3; which++)
    {
        muiLimits limits = muiDefaultContextDef().limits;
        limits.drawCommands = which == 0 ? 2 : 8;
        limits.drawClips = which == 1 ? 2 : 8;
        limits.drawGradients = which == 2 ? 2 : 8;
        context = MakeContextWith(limits);
        root = Add(context, s_nullNode, 100.0f, 100.0f, (muiEdges){0});
        muiVisualStyle visual = muiDefaultVisualStyle();
        visual.clip = true;
        visual.gradient =
            (muiGradient){mui_gradientRadial, 2, 0.0f, {{s_red, 0.0f}, {s_blue, 1.0f}}};
        const muiPropertyMask mask =
            MUI_PROPERTY_BIT(mui_propertyClip) | MUI_PROPERTY_BIT(mui_propertyGradient);
        SetVisual(context, root, &visual, mask);
        muiNodeId child = Add(context, root, 10.0f, 10.0f, (muiEdges){0});
        SetVisual(context, child, &visual, mask);
        (void)Build(context, root, 1.0f);
        const muiLayoutInput layout = {100.0f, 100.0f, NULL, NULL, 0, NULL};
        CHECK(muiComputeLayout(context, root, &layout) == mui_success, "layout");
        // Two nodes' fit; a third's does not.
        muiNodeId more = Add(context, child, 5.0f, 5.0f, (muiEdges){0});
        SetVisual(context, more, &visual, mask);
        CHECK(muiComputeLayout(context, root, &layout) == mui_success, "layout");
        CHECK(muiBuildDrawList(context, root, &(muiDrawInput){1, 1.0f, NULL, NULL}) ==
                  mui_errorCapacity,
              names[which]);
        CHECK(muiGetDrawList(context, &list) == mui_success && list.commandCount == 0 &&
                  list.clipCount == 1 && list.gradientCount == 1 && list.header.generation > 0,
              "an empty list");
        muiDestroyContext(context);
    }
}

// Edges of what a node draws, and of what a context keeps between builds.
static void TestEdgeCases(void)
{
    muiContext* context = MakeContext();
    const muiEdges none = {0};
    muiNodeId root = Add(context, s_nullNode, 100.0f, 100.0f, (muiEdges){0.5f, 0.0f, 0.5f, 0.0f});
    muiVisualStyle visual = muiDefaultVisualStyle();
    // Borders alone draw, unless their colors are clear.
    muiNodeId framed = Add(context, root, 20.0f, 20.0f, none);
    SetBorder(context, framed, (muiEdges){1.0f, 1.0f, 1.0f, 1.0f});
    muiNodeId clear = Add(context, root, 20.0f, 20.0f, none);
    SetBorder(context, clear, (muiEdges){1.0f, 1.0f, 1.0f, 1.0f});
    const muiColor transparent = {0.0f, 0.0f, 0.0f, 0.0f};
    visual.borderColor = (muiEdgeColors){transparent, transparent, transparent, transparent};
    SetVisual(context, clear, &visual,
              MUI_PROPERTY_BIT(mui_propertyBorderColorStart) |
                  MUI_PROPERTY_BIT(mui_propertyBorderColorEnd) |
                  MUI_PROPERTY_BIT(mui_propertyBorderColorTop) |
                  MUI_PROPERTY_BIT(mui_propertyBorderColorBottom));
    // An empty box stays empty.
    muiNodeId empty = Add(context, root, 0.0f, 10.0f, none);
    visual = muiDefaultVisualStyle();
    visual.background = s_gray;
    SetVisual(context, empty, &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
    // Borders snapped wider than the box leave an empty padding box.
    muiNodeId thin = Add(context, root, 0.6f, 10.0f, none);
    SetBorder(context, thin, (muiEdges){0.3f, 0.3f, 0.0f, 0.0f});
    visual.innerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 1.0f}, 0.0f, 0.0f, 1.0f, 0.0f};
    SetVisual(context, thin, &visual,
              MUI_PROPERTY_BIT(mui_propertyBackground) | MUI_PROPERTY_BIT(mui_propertyInnerShadow));
    // A clip at a fraction of a pixel snaps.
    muiNodeId clipping = Add(context, root, 10.25f, 10.0f, none);
    visual = muiDefaultVisualStyle();
    visual.clip = true;
    SetVisual(context, clipping, &visual, MUI_PROPERTY_BIT(mui_propertyClip));
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 4, "a framed box, an empty one, and a thin one with its shadow");
    CHECK(list.commands[0].box.borderWidths.top == 1.0f && list.commands[0].box.fill.a == 0.0f,
          "the frame");
    CHECK(list.commands[1].box.rect.width == 0.0f, "the empty box");
    CHECK(list.commands[3].kind == mui_drawShadow && list.commands[3].shadow.rect.width == 0.0f,
          "an inner shadow of no width");
    CHECK(list.clipCount == 2 && list.clips[1].rect.x == 41.0f && list.clips[1].rect.width == 10.0f,
          "a snapped clip");
    muiDestroyContext(context);

    // A context that drew larger records before writes the same bytes as
    // a new one.
    muiContext* used = MakeContext();
    muiNodeId first = Add(used, s_nullNode, 50.0f, 50.0f, none);
    visual = muiDefaultVisualStyle();
    visual.background = s_red;
    visual.gradient = (muiGradient){mui_gradientLinear,
                                    4,
                                    0.0f,
                                    {{s_red, 0.0f}, {s_blue, 0.3f}, {s_red, 0.6f}, {s_blue, 1.0f}}};
    SetVisual(used, first, &visual,
              MUI_PROPERTY_BIT(mui_propertyBackground) | MUI_PROPERTY_BIT(mui_propertyGradient));
    (void)Build(used, first, 1.0f);
    muiContext* fresh = MakeContext();
    muiNodeId roots[2] = {first, Add(fresh, s_nullNode, 50.0f, 50.0f, none)};
    muiContext* contexts[2] = {used, fresh};
    muiDrawList lists[2];
    for (int i = 0; i < 2; i++)
    {
        muiNodeId node = i == 0 ? Add(used, s_nullNode, 50.0f, 50.0f, none) : roots[1];
        visual = muiDefaultVisualStyle();
        visual.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.5f}, 1.0f, 1.0f, 4.0f, 0.0f};
        visual.gradient =
            (muiGradient){mui_gradientRadial, 2, 0.0f, {{s_blue, 0.0f}, {s_red, 1.0f}}};
        SetVisual(contexts[i], node, &visual,
                  MUI_PROPERTY_BIT(mui_propertyOuterShadow) |
                      MUI_PROPERTY_BIT(mui_propertyGradient));
        lists[i] = Build(contexts[i], node, 1.0f);
    }
    CHECK(lists[0].commandCount == 2 && lists[1].commandCount == 2 &&
              memcmp(lists[0].commands, lists[1].commands, 2 * sizeof(muiDrawCommand)) == 0 &&
              memcmp(&lists[0].gradients[1], &lists[1].gradients[1], sizeof(muiDrawGradient)) == 0,
          "no bytes of the earlier list are left");
    muiDestroyContext(used);
    muiDestroyContext(fresh);
}

// The list a build takes from the last one equals one built whole, over
// a run of edits of every kind a list depends on.
typedef struct Random
{
    uint32_t state;
} Random;

static uint32_t NextRandom(Random* random, uint32_t below)
{
    random->state = random->state * 1664525u + 1013904223u;
    return (random->state >> 8) % below;
}

static bool SameTables(const muiDrawList* a, const muiDrawList* b)
{
    return a->commandCount == b->commandCount && a->clipCount == b->clipCount &&
           a->gradientCount == b->gradientCount && a->glyphCount == b->glyphCount &&
           memcmp(a->glyphs, b->glyphs, a->glyphCount * sizeof(muiGlyph)) == 0 &&
           memcmp(a->commands, b->commands, a->commandCount * sizeof(muiDrawCommand)) == 0 &&
           memcmp(a->clips, b->clips, a->clipCount * sizeof(muiDrawClip)) == 0 &&
           memcmp(a->gradients, b->gradients, a->gradientCount * sizeof(muiDrawGradient)) == 0;
}

// Copies a list's tables, which the next build may write over.
typedef struct Snapshot
{
    muiDrawList list;
    muiDrawCommand commands[256];
    muiDrawClip clips[64];
    muiDrawGradient gradients[64];
    muiGlyph glyphs[512];
} Snapshot;

static void Take(Snapshot* snapshot, const muiDrawList* list)
{
    CHECK(list->commandCount <= 256 && list->clipCount <= 64 && list->gradientCount <= 64 &&
              list->glyphCount <= 512,
          "the snapshot holds it");
    snapshot->list = *list;
    memcpy(snapshot->commands, list->commands, list->commandCount * sizeof(muiDrawCommand));
    memcpy(snapshot->clips, list->clips, list->clipCount * sizeof(muiDrawClip));
    memcpy(snapshot->gradients, list->gradients, list->gradientCount * sizeof(muiDrawGradient));
    snapshot->list.commands = snapshot->commands;
    snapshot->list.clips = snapshot->clips;
    memcpy(snapshot->glyphs, list->glyphs, list->glyphCount * sizeof(muiGlyph));
    snapshot->list.gradients = snapshot->gradients;
    snapshot->list.glyphs = snapshot->glyphs;
}

// Whether ancestor is node or above it.
static bool IsAtOrAbove(const muiContext* context, muiNodeId ancestor, muiNodeId node)
{
    for (muiNodeId at = node; at.index1 != 0; at = muiNode_GetParent(context, at))
    {
        if (at.index1 == ancestor.index1 && at.generation == ancestor.generation)
        {
            return true;
        }
    }
    return false;
}

static void Edit(muiContext* context, muiNodeId* nodes, uint32_t count, Random* random,
                 float* available)
{
    muiNodeId node = nodes[NextRandom(random, count)];
    muiVisualStyle visual = muiDefaultVisualStyle();
    CHECK(muiNode_GetVisualStyle(context, node, &visual) == mui_success, "read");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    CHECK(muiNode_GetLayoutStyle(context, node, &layout) == mui_success, "read");
    const float shades[4] = {0.0f, 0.25f, 0.5f, 1.0f};
    switch (NextRandom(random, 11))
    {
    case 9:
    {
        muiTextStyle text = muiDefaultTextStyle();
        text.color = (muiColor){shades[NextRandom(random, 4)], 0.25f, 0.75f, 1.0f};
        CHECK(muiNode_SetTextValues(context, node, &text,
                                    MUI_PROPERTY_BIT(mui_propertyTextColor)) == mui_success,
              "text color");
        break;
    }
    case 0:
        visual.background = (muiColor){shades[NextRandom(random, 4)], 0.5f, 0.2f, 1.0f};
        SetVisual(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
        break;
    case 1:
        visual.clip = !visual.clip;
        visual.radius.topStart = Length((float)NextRandom(random, 6));
        SetVisual(context, node, &visual,
                  MUI_PROPERTY_BIT(mui_propertyClip) |
                      MUI_PROPERTY_BIT(mui_propertyRadiusTopStart));
        break;
    case 2:
        visual.opacity = shades[NextRandom(random, 4)];
        SetVisual(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyOpacity));
        break;
    case 3:
        visual.gradient =
            NextRandom(random, 2) == 0
                ? (muiGradient){0}
                : (muiGradient){mui_gradientLinear, 2, 45.0f, {{s_red, 0.0f}, {s_blue, 1.0f}}};
        SetVisual(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyGradient));
        break;
    case 4:
        layout.padding.start = (float)NextRandom(random, 5) * 1.5f;
        CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                      MUI_PROPERTY_BIT(mui_propertyPaddingStart)) == mui_success,
              "padding");
        break;
    case 5:
        layout.sizing.width = Length(10.0f + (float)NextRandom(random, 40));
        CHECK(muiNode_SetLayoutValues(context, node, &layout, WIDTH) == mui_success, "width");
        break;
    case 6:
        layout.textDirection =
            layout.textDirection == mui_textRightToLeft ? mui_textInherit : mui_textRightToLeft;
        CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                      MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
              "direction");
        break;
    case 7:
        *available = 300.0f + (float)NextRandom(random, 200);
        break;
    case 8:
    {
        // A node other than the root moves under another that is not
        // below it.
        muiNodeId parent = nodes[NextRandom(random, count)];
        if (node.index1 != nodes[0].index1 && !IsAtOrAbove(context, node, parent))
        {
            CHECK(muiNode_Detach(context, node) == mui_success &&
                      muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success,
                  "moved");
        }
        break;
    }
    default:
        visual.outerShadow.color.a = visual.outerShadow.color.a > 0.0f ? 0.0f : 0.5f;
        visual.outerShadow.blur = 3.0f;
        SetVisual(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyOuterShadow));
        break;
    }
}

// Paints a few glyphs in the node's text color and size, which a parent's
// text style reaches; user is the context.
static void PaintGlyphs(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                        muiDrawSink* sink)
{
    (void)hostKey;
    muiComputedTextStyle style;
    CHECK(muiNode_GetTextStyle(user, nodeId, &style) == mui_success, "a paint function reads");
    muiGlyph glyphs[3];
    uint32_t count = 1 + nodeId.index1 % 3;
    for (uint32_t i = 0; i < count; i++)
    {
        glyphs[i] = (muiGlyph){nodeId.index1 * 10 + i, (float)i * style.size * 0.5f, 0.0f};
    }
    const muiGlyphRun run = {style.font, style.size, style.color, width > 30.0f ? 2.0f : 0.0f,
                             fminf(height, style.size) * 0.75f};
    CHECK(muiDrawSink_AddGlyphRun(sink, &run, glyphs, count) == mui_success, "glyphs");
}

static void TestBuildsFromTheLastListMatchWholeOnes(void)
{
    muiContext* context = MakeContext();
    enum
    {
        COUNT = 24
    };
    muiNodeId nodes[COUNT];
    nodes[0] = Add(context, s_nullNode, 400.0f, 300.0f, (muiEdges){4.0f, 0.0f, 4.0f, 0.0f});
    muiLayoutStyle wrap = muiDefaultLayoutStyle();
    wrap.container.wrap = mui_wrapWrap;
    CHECK(muiNode_SetLayoutValues(context, nodes[0], &wrap,
                                  MUI_PROPERTY_BIT(mui_propertyFlexWrap)) == mui_success,
          "wrap");
    for (uint32_t i = 1; i < COUNT; i++)
    {
        muiNodeId parent = nodes[(i - 1) / 3];
        nodes[i] = Add(context, parent, 20.0f + (float)(i % 5) * 7.5f, 16.0f,
                       (muiEdges){1.0f, 0.0f, 1.0f, 0.0f});
        muiVisualStyle visual = muiDefaultVisualStyle();
        visual.background = (muiColor){(float)(i % 3) * 0.4f, 0.3f, 0.6f, 1.0f};
        SetVisual(context, nodes[i], &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
        // Half the leaves are the host's content, painted as glyphs.
        if (i >= 8 && i % 2 == 0)
        {
            muiLayoutStyle content = muiDefaultLayoutStyle();
            content.content = mui_contentHost;
            CHECK(muiNode_SetLayoutValues(context, nodes[i], &content,
                                          MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
                  "host content");
        }
    }
    Random random = {12345u};
    float available = 400.0f;
    // Static: too large for a small stack, such as the web's.
    static Snapshot retained;
    uint32_t glyphs = 0;
    for (int step = 0; step < 300; step++)
    {
        Edit(context, nodes, COUNT, &random, &available);
        const muiLayoutInput layout = {available, 1000.0f, NULL, NULL, 0, NULL};
        CHECK(muiComputeLayout(context, nodes[0], &layout) == mui_success, "layout");
        CHECK(muiBuildDrawList(context, nodes[0], &(muiDrawInput){7, 1.0f, PaintGlyphs, context}) ==
                  mui_success,
              "build from the last");
        // Builds between checks take from lists that were themselves
        // taken from others.
        if (step % 7 != 6)
        {
            continue;
        }
        muiDrawList list;
        CHECK(muiGetDrawList(context, &list) == mui_success, "get");
        Take(&retained, &list);
        // A build at another scale cannot take from the last list, and the
        // one after it at scale 1 cannot take from that one.
        CHECK(muiBuildDrawList(context, nodes[0], &(muiDrawInput){7, 2.0f, PaintGlyphs, context}) ==
                      mui_success &&
                  muiBuildDrawList(context, nodes[0],
                                   &(muiDrawInput){7, 1.0f, PaintGlyphs, context}) == mui_success,
              "builds whole");
        CHECK(muiGetDrawList(context, &list) == mui_success, "get");
        glyphs += list.glyphCount;
        bool same = SameTables(&retained.list, &list);
        CHECK(same, "taken equals whole");
        if (!same)
        {
            printf("step %d\n", step);
            break;
        }
    }
    muiDestroyContext(context);
    CHECK(glyphs > 0, "the lists held glyphs");
}

static muiDrawList BuildAt(muiContext* context, muiNodeId root, float available, uint64_t surface)
{
    const muiLayoutInput layout = {available, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &layout) == mui_success, "layout");
    CHECK(muiBuildDrawList(context, root, &(muiDrawInput){surface, 1.0f, NULL, NULL}) ==
              mui_success,
          "build");
    muiDrawList list;
    CHECK(muiGetDrawList(context, &list) == mui_success, "get");
    return list;
}

// Builds from the last list and checks it against a build from nothing.
static void CheckRetained(muiContext* context, muiNodeId root, const char* what)
{
    static Snapshot retained;
    muiDrawList list = BuildAt(context, root, 1000.0f, 7);
    Take(&retained, &list);
    CHECK(muiBuildDrawList(context, root, &(muiDrawInput){7, 2.0f, NULL, NULL}) == mui_success &&
              muiBuildDrawList(context, root, &(muiDrawInput){7, 1.0f, NULL, NULL}) == mui_success,
          "builds whole");
    CHECK(muiGetDrawList(context, &list) == mui_success && SameTables(&retained.list, &list), what);
}

static void Paint(muiContext* context, muiNodeId node, muiColor color)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    CHECK(muiNode_GetVisualStyle(context, node, &visual) == mui_success, "read");
    visual.background = color;
    SetVisual(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
}

static void SetOpacity(muiContext* context, muiNodeId node, float opacity)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.opacity = opacity;
    SetVisual(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyOpacity));
}

static void AddShadowTo(muiContext* context, muiNodeId node)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.outerShadow = (muiShadow){{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f, 1.0f, 2.0f, 0.0f};
    SetVisual(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyOuterShadow));
}

static void TestRetainedEdges(void)
{
    const muiEdges none = {0};
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    // A subtree hidden, then shown after the list before it changed: its
    // spans from before it was hidden are not taken.
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, 300.0f, 100.0f, none);
    muiNodeId before = Add(context, root, 20.0f, 20.0f, none);
    muiNodeId shown = Add(context, root, 40.0f, 40.0f, none);
    muiNodeId inner = Add(context, shown, 10.0f, 10.0f, none);
    Paint(context, before, s_gray);
    Paint(context, shown, s_red);
    Paint(context, inner, s_blue);
    (void)BuildAt(context, root, 1000.0f, 7);
    SetOpacity(context, shown, 0.0f);
    (void)BuildAt(context, root, 1000.0f, 7);
    AddShadowTo(context, before);
    (void)BuildAt(context, root, 1000.0f, 7);
    // A build more, so that the list taken from is not the one the
    // hidden subtree's commands were last in.
    Paint(context, before, s_red);
    (void)BuildAt(context, root, 1000.0f, 7);
    SetOpacity(context, shown, 1.0f);
    CheckRetained(context, root, "shown again");
    muiDestroyContext(context);

    // A hidden node inside a copied subtree keeps no spans it never had.
    context = MakeContext();
    root = Add(context, s_nullNode, 300.0f, 100.0f, none);
    before = Add(context, root, 20.0f, 20.0f, none);
    muiNodeId copied = Add(context, root, 60.0f, 60.0f, none);
    muiNodeId hidden = Add(context, copied, 30.0f, 30.0f, none);
    muiNodeId leaf = Add(context, hidden, 10.0f, 10.0f, none);
    Paint(context, before, s_gray);
    Paint(context, copied, s_red);
    Paint(context, leaf, s_blue);
    (void)BuildAt(context, root, 1000.0f, 7);
    SetOpacity(context, hidden, 0.0f);
    (void)BuildAt(context, root, 1000.0f, 7);
    AddShadowTo(context, before);
    (void)BuildAt(context, root, 1000.0f, 7);
    SetOpacity(context, hidden, 1.0f);
    CheckRetained(context, root, "shown inside a copied subtree");

    // A subtree whose parent moves down, its own rectangle as it was.
    muiNodeId column = Add(context, s_nullNode, 100.0f, 200.0f, none);
    layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    CHECK(muiNode_SetLayoutValues(context, column, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyFlexDirection)) == mui_success,
          "a column");
    muiNodeId top = Add(context, column, 50.0f, 10.0f, none);
    muiNodeId box = Add(context, column, 50.0f, 50.0f, none);
    Paint(context, Add(context, box, 20.0f, 20.0f, none), s_blue);
    (void)BuildAt(context, column, 1000.0f, 7);
    layout = muiDefaultLayoutStyle();
    layout.sizing.height = Length(30.0f);
    CHECK(muiNode_SetLayoutValues(context, top, &layout, HEIGHT) == mui_success, "taller");
    CheckRetained(context, column, "moved down");

    // A child whose rectangle a direction change leaves as it is still
    // turns its corners.
    muiNodeId full = Add(context, s_nullNode, 100.0f, 20.0f, none);
    muiNodeId child = Add(context, full, 100.0f, 20.0f, none);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = s_gray;
    visual.radius.topStart = Length(10.0f);
    SetVisual(context, child, &visual,
              MUI_PROPERTY_BIT(mui_propertyBackground) |
                  MUI_PROPERTY_BIT(mui_propertyRadiusTopStart));
    muiDrawList list = BuildAt(context, full, 1000.0f, 7);
    CHECK(list.commands[0].box.radii.topLeft == 10.0f, "left to right");
    layout = muiDefaultLayoutStyle();
    layout.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, full, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "right to left");
    list = BuildAt(context, full, 1000.0f, 7);
    CHECK(list.commands[0].box.radii.topRight == 10.0f &&
              list.commands[0].box.radii.topLeft == 0.0f,
          "the corner turns");

    // Another surface, and another root, are built anew.
    list = BuildAt(context, full, 1000.0f, 8);
    CHECK(list.header.surface == 8, "another surface");
    list = BuildAt(context, root, 1000.0f, 8);
    CHECK(list.header.width == 300.0f, "another root");
    muiDestroyContext(context);

    // A root sized by the space alone repaints when the space changes.
    context = MakeContext();
    root = Add(context, s_nullNode, 0.0f, 50.0f, none);
    layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, root, &layout, WIDTH) == mui_success, "full width");
    Paint(context, root, s_gray);
    list = BuildAt(context, root, 400.0f, 7);
    CHECK(list.commands[0].box.rect.width == 400.0f, "400");
    list = BuildAt(context, root, 500.0f, 7);
    CHECK(list.header.width == 500.0f && list.commands[0].box.rect.width == 500.0f, "500");
    muiDestroyContext(context);

    // A build that fails inside a subtree it visited only for a new
    // opacity leaves that subtree no spans to take, though nothing asks it
    // to repaint after.
    muiLimits limits = muiDefaultContextDef().limits;
    limits.drawCommands = 3;
    context = MakeContextWith(limits);
    root = Add(context, s_nullNode, 300.0f, 100.0f, none);
    muiNodeId faded = Add(context, root, 200.0f, 80.0f, none);
    muiNodeId inside = Add(context, faded, 100.0f, 50.0f, none);
    Paint(context, Add(context, inside, 10.0f, 10.0f, none), s_red);
    Paint(context, Add(context, inside, 10.0f, 10.0f, none), s_blue);
    (void)BuildAt(context, root, 1000.0f, 7);
    SetOpacity(context, faded, 0.5f);
    AddShadowTo(context, root);
    Paint(context, root, s_gray);
    CHECK(
        muiComputeLayout(context, root, &(muiLayoutInput){1000.0f, 1000.0f, NULL, NULL, 0, NULL}) ==
                mui_success &&
            muiBuildDrawList(context, root, &(muiDrawInput){7, 1.0f, NULL, NULL}) ==
                mui_errorCapacity,
        "four do not fit");
    visual = muiDefaultVisualStyle();
    SetVisual(context, root, &visual, MUI_PROPERTY_BIT(mui_propertyOuterShadow));
    CheckRetained(context, root, "three fit, none taken from the failure");
    muiDestroyContext(context);

    // A copy that does not fit fails the build.
    limits = muiDefaultContextDef().limits;
    limits.drawCommands = 3;
    context = MakeContextWith(limits);
    root = Add(context, s_nullNode, 100.0f, 100.0f, none);
    muiNodeId a = Add(context, root, 50.0f, 50.0f, none);
    muiNodeId b = Add(context, a, 20.0f, 20.0f, none);
    Paint(context, root, s_gray);
    Paint(context, a, s_red);
    Paint(context, b, s_blue);
    (void)BuildAt(context, root, 1000.0f, 7);
    AddShadowTo(context, root);
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(muiBuildDrawList(context, root, &(muiDrawInput){7, 1.0f, NULL, NULL}) ==
              mui_errorCapacity,
          "the copy does not fit");
    // Without the shadow it fits again, built from nothing the failure
    // left.
    visual = muiDefaultVisualStyle();
    SetVisual(context, root, &visual, MUI_PROPERTY_BIT(mui_propertyOuterShadow));
    list = BuildAt(context, root, 1000.0f, 7);
    CHECK(list.commandCount == 3 && list.commands[2].box.fill.b == 1.0f, "rebuilt after it");
    muiDestroyContext(context);
}

typedef struct GlyphHost
{
    muiContext* context;
    int calls;
    // What a call tries besides adding a run.
    bool misuse;
    // Whether it adds a rectangle before its run.
    bool rect;
    uint32_t count;
    // The slot of a node that paints three glyphs, not two.
    uint32_t wider;
} GlyphHost;

static void PaintTwoGlyphs(void* user, muiNodeId nodeId, uint64_t hostKey, float width,
                           float height, muiDrawSink* sink)
{
    GlyphHost* host = user;
    host->calls++;
    CHECK(hostKey == 42 && width == 30.0f && height == 14.0f, "the key and the content box");
    if (host->misuse)
    {
        muiNodeDef def = muiDefaultNodeDef();
        muiNodeId created = s_nullNode;
        CHECK(muiCreateNode(host->context, &def, &created) == mui_errorInvalid &&
                  muiBuildDrawList(host->context, nodeId, &(muiDrawInput){1, 1.0f, NULL, NULL}) ==
                      mui_errorInvalid,
              "edits from a paint function are refused");
        const muiGlyph one = {1, 0.0f, 0.0f};
        const muiGlyph far = {1, INFINITY, 0.0f};
        muiGlyphRun run = {0, 10.0f, s_red, 0.0f, 0.0f};
        CHECK(muiDrawSink_AddGlyphRun(NULL, &run, &one, 1) == mui_errorInvalid &&
                  muiDrawSink_AddGlyphRun(sink, NULL, &one, 1) == mui_errorInvalid &&
                  muiDrawSink_AddGlyphRun(sink, &run, NULL, 1) == mui_errorInvalid &&
                  muiDrawSink_AddGlyphRun(sink, &run, &one, 0) == mui_errorInvalid &&
                  muiDrawSink_AddGlyphRun(sink, &run, &far, 1) == mui_errorInvalid,
              "arguments");
        const float sizes[3] = {0.0f, -1.0f, NAN};
        for (int i = 0; i < 3; i++)
        {
            run.size = sizes[i];
            CHECK(muiDrawSink_AddGlyphRun(sink, &run, &one, 1) == mui_errorInvalid, "a size");
        }
        run.size = 10.0f;
        const muiColor colors[3] = {
            {0.0f, 1.5f, 0.0f, 1.0f}, {-0.25f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 2.0f, 1.0f}};
        for (int i = 0; i < 3; i++)
        {
            run.color = colors[i];
            CHECK(muiDrawSink_AddGlyphRun(sink, &run, &one, 1) == mui_errorInvalid, "a color");
        }
        run.color = s_red;
        run.originY = INFINITY;
        CHECK(muiDrawSink_AddGlyphRun(sink, &run, &one, 1) == mui_errorInvalid, "an origin y");
        run.originY = 0.0f;
        run.originX = NAN;
        CHECK(muiDrawSink_AddGlyphRun(sink, &run, &one, 1) == mui_errorInvalid, "an origin x");
        run.originX = 0.0f;
        const muiGlyph low = {1, 0.0f, -INFINITY};
        CHECK(muiDrawSink_AddGlyphRun(sink, &run, &low, 1) == mui_errorInvalid, "a glyph's y");
        CHECK(muiDrawSink_AddRect(NULL, (muiRect){0.0f, 0.0f, 1.0f, 1.0f}, s_red) ==
                      mui_errorInvalid &&
                  muiDrawSink_AddRect(sink, (muiRect){NAN, 0.0f, 1.0f, 1.0f}, s_red) ==
                      mui_errorInvalid &&
                  muiDrawSink_AddRect(sink, (muiRect){0.0f, 0.0f, 1.0f, -1.0f}, s_red) ==
                      mui_errorInvalid &&
                  muiDrawSink_AddRect(sink, (muiRect){0.0f, 0.0f, 1.0f, 1.0f},
                                      (muiColor){0.0f, 0.0f, 0.0f, 1.5f}) == mui_errorInvalid,
              "rectangles");
    }
    if (host->rect)
    {
        // At 1.25, 2.5 in the content box: snapped, the quarter-pixel
        // height kept as one pixel.
        CHECK(muiDrawSink_AddRect(sink, (muiRect){1.25f, 2.5f, 10.0f, 0.25f}, s_red) == mui_success,
              "a rectangle");
    }
    const muiGlyph glyphs[3] = {{7, 0.0f, 0.0f}, {9, 5.25f, -1.5f}, {11, 9.0f, 0.0f}};
    const muiGlyphRun run = {99, 12.0f, s_gray, 1.25f, 10.3f};
    uint32_t count = nodeId.index1 == host->wider ? 3 : 2;
    host->count += muiDrawSink_AddGlyphRun(sink, &run, glyphs, count) == mui_success ? 1u : 0u;
}

// A host node of 40 by 20 with a border of 2 and padding of 3 (start)
// and 1 (top), under root.
static muiNodeId AddLabel(muiContext* context, muiNodeId root)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = 42;
    muiNodeId label = s_nullNode;
    CHECK(muiCreateNode(context, &def, &label) == mui_success &&
              muiNode_InsertChild(context, root, label, s_nullNode) == mui_success,
          "label");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = Length(40.0f);
    layout.sizing.height = Length(20.0f);
    layout.content = mui_contentHost;
    layout.border = (muiEdges){2.0f, 2.0f, 2.0f, 2.0f};
    layout.padding = (muiEdges){3.0f, 3.0f, 1.0f, 1.0f};
    CHECK(muiNode_SetLayoutValues(context, label, &layout, MUI_LAYOUT_PROPERTIES) == mui_success,
          "label layout");
    return label;
}

static muiDrawList BuildWith(muiContext* context, muiNodeId root, float scale, GlyphHost* host)
{
    const muiLayoutInput layout = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &layout) == mui_success, "layout");
    CHECK(muiBuildDrawList(context, root, &(muiDrawInput){7, scale, PaintTwoGlyphs, host}) ==
              mui_success,
          "build");
    muiDrawList list;
    CHECK(muiGetDrawList(context, &list) == mui_success, "get");
    return list;
}

static void TestGlyphRuns(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, 200.0f, 100.0f, (muiEdges){10.0f, 0.0f, 5.0f, 0.0f});
    muiNodeId label = AddLabel(context, root);
    muiVisualStyle visual = Boxed();
    visual.opacity = 0.5f;
    visual.clip = true;
    SetVisual(context, label, &visual,
              MUI_PROPERTY_BIT(mui_propertyBackground) | MUI_PROPERTY_BIT(mui_propertyOpacity) |
                  MUI_PROPERTY_BIT(mui_propertyClip));
    GlyphHost host = {context, 0, false, false, 0, 0};
    muiDrawList list = BuildWith(context, root, 2.0f, &host);
    CHECK(host.calls == 1 && list.glyphCount == 2 && list.commandCount == 2, "one call, one run");
    const muiDrawCommand* box = &list.commands[0];
    const muiDrawCommand* run = &list.commands[1];
    CHECK(box->kind == mui_drawBox && run->kind == mui_drawGlyphRun, "after the box");
    CHECK(run->clip == 1 && list.clipCount == 2, "inside the node's own clip");
    // The label sits at 10,5 in the root; its content box starts 5 right
    // (border 2, padding 3) and 3 down (border 2, padding 1).
    CHECK(run->glyphRun.originX == 15.0f + 1.25f, "x keeps its fraction");
    CHECK(run->glyphRun.originY == 18.5f, "y = 8 + 10.3 snaps to half pixels at scale 2");
    CHECK(run->glyphRun.font == 99 && run->glyphRun.size == 12.0f &&
              run->glyphRun.firstGlyph == 0 && run->glyphRun.glyphCount == 2,
          "the run");
    CHECK(list.glyphs[1].id == 9 && list.glyphs[1].x == 5.25f && list.glyphs[1].y == -1.5f,
          "glyphs as given");
    // Gray at half opacity: linear 0.2140 times 0.5, premultiplied.
    CHECK(run->glyphRun.color.a == 0.5f && fabsf(run->glyphRun.color.r - 0.10702f) < 1e-4f,
          "color in linear light times opacity");

    // A static frame paints nothing again; a content change does.
    muiDrawList same = BuildWith(context, root, 2.0f, &host);
    CHECK(host.calls == 1 && same.header.generation == list.header.generation, "kept");
    CHECK(muiNode_MarkContentChanged(context, label) == mui_success, "changed");
    (void)BuildWith(context, root, 2.0f, &host);
    CHECK(host.calls == 2, "painted again");

    // A sibling's repaint copies the label's run and its glyphs.
    muiNodeId sibling = Add(context, root, 10.0f, 10.0f, (muiEdges){0});
    SetVisual(context, sibling, &visual, MUI_PROPERTY_BIT(mui_propertyBackground));
    (void)BuildWith(context, root, 2.0f, &host);
    Paint(context, sibling, s_blue);
    list = BuildWith(context, root, 2.0f, &host);
    CHECK(host.calls == 2 && list.glyphCount == 2 && list.commands[1].glyphRun.firstGlyph == 0,
          "copied, not painted");

    // Refused calls count as misuse and add nothing.
    uint64_t misuse = muiGetContextMisuse(context);
    host.misuse = true;
    CHECK(muiNode_MarkContentChanged(context, label) == mui_success, "changed");
    list = BuildWith(context, root, 2.0f, &host);
    CHECK(muiGetContextMisuse(context) == misuse + 18 && list.glyphCount == 2,
          "thirteen bad runs, three bad rectangles and two edits; a run or rectangle with no "
          "sink has no context to count it");
    host.misuse = false;

    // A rectangle: a box of one fill, from the label's content box at 15,
    // 8 (the label at 10, 5, its border and padding), at the label's
    // opacity of a half, its edges snapped at scale 1, before the run.
    host.rect = true;
    CHECK(muiNode_MarkContentChanged(context, label) == mui_success, "changed");
    list = BuildWith(context, root, 1.0f, &host);
    host.rect = false;
    const muiDrawCommand* added = &list.commands[1];
    CHECK(list.commandCount > 2 && added->kind == mui_drawBox && added->box.rect.x == 16.0f &&
              added->box.rect.width == 10.0f && added->box.rect.y == 11.0f &&
              added->box.rect.height == 1.0f && added->box.fill.r == 0.5f &&
              added->box.fill.a == 0.5f && added->box.radii.topLeft == 0.0f &&
              added->box.gradient == 0 && added->box.borderWidths.top == 0.0f &&
              list.commands[2].kind == mui_drawGlyphRun,
          "placed, snapped, before the run");

    // Not painted when invisible, or with no paint function.
    visual.opacity = 0.0f;
    SetVisual(context, label, &visual, MUI_PROPERTY_BIT(mui_propertyOpacity));
    int calls = host.calls;
    list = BuildWith(context, root, 2.0f, &host);
    CHECK(host.calls == calls && list.glyphCount == 0, "an invisible node");
    visual.opacity = 1.0f;
    SetVisual(context, label, &visual, MUI_PROPERTY_BIT(mui_propertyOpacity));
    CHECK(muiBuildDrawList(context, root, &(muiDrawInput){7, 2.0f, NULL, NULL}) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.glyphCount == 0 &&
              host.calls == calls,
          "no paint function");
    muiDestroyContext(context);
}

static void TestGlyphRunsRightToLeftAndLimits(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Add(context, s_nullNode, 200.0f, 100.0f, (muiEdges){0});
    muiNodeId label = AddLabel(context, root);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    CHECK(muiNode_GetLayoutStyle(context, label, &layout) == mui_success, "read");
    layout.textDirection = mui_textRightToLeft;
    layout.padding.end = 7.0f;
    layout.padding.start = 3.0f;
    layout.sizing.width = Length(44.0f);
    CHECK(muiNode_SetLayoutValues(context, label, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingEnd) | WIDTH) ==
              mui_success,
          "right to left");
    GlyphHost host = {context, 0, false, false, 0, 0};
    muiDrawList list = BuildWith(context, root, 1.0f, &host);
    // After its border's box; its end, padding 7, is on the left.
    CHECK(list.commandCount == 2 && list.commands[1].kind == mui_drawGlyphRun &&
              list.commands[1].glyphRun.originX == 9.0f + 1.25f &&
              list.commands[1].glyphRun.originY == 13.0f,
          "the content box starts at the end side");
    muiDestroyContext(context);

    for (int which = 0; which < 2; which++)
    {
        muiLimits limits = muiDefaultContextDef().limits;
        limits.drawGlyphs = which == 0 ? 3 : 100;
        // Each label draws its border's box, then its run.
        limits.drawCommands = which == 0 ? 100 : 3;
        context = MakeContextWith(limits);
        root = Add(context, s_nullNode, 200.0f, 100.0f, (muiEdges){0});
        (void)AddLabel(context, root);
        (void)AddLabel(context, root);
        host = (GlyphHost){context, 0, false, false, 0, 0};
        const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
        CHECK(
            muiComputeLayout(context, root, &input) == mui_success &&
                muiBuildDrawList(context, root, &(muiDrawInput){7, 1.0f, PaintTwoGlyphs, &host}) ==
                    mui_errorCapacity,
            which == 0 ? "four glyphs in a table of three" : "four commands in three");
        CHECK(host.count == 1 && muiGetDrawList(context, &list) == mui_success &&
                  list.commandCount == 0 && list.glyphCount == 0,
              "the first fits, the list is left empty");
        muiDestroyContext(context);
    }

    // A copied label's glyphs no longer fit once the one before grows.
    muiLimits limits = muiDefaultContextDef().limits;
    limits.drawGlyphs = 4;
    context = MakeContextWith(limits);
    root = Add(context, s_nullNode, 200.0f, 100.0f, (muiEdges){0});
    muiNodeId first = AddLabel(context, root);
    (void)AddLabel(context, root);
    host = (GlyphHost){context, 0, false, false, 0, 0};
    list = BuildWith(context, root, 1.0f, &host);
    CHECK(list.glyphCount == 4, "two runs of two");
    host.wider = first.index1;
    CHECK(muiNode_MarkContentChanged(context, first) == mui_success &&
              muiBuildDrawList(context, root, &(muiDrawInput){7, 1.0f, PaintTwoGlyphs, &host}) ==
                  mui_errorCapacity,
          "three painted and two copied in a table of four");
    muiDestroyContext(context);
}

int main(void)
{
    TestBoxes();
    TestSnapping();
    TestClipsShadowsImagesGradients();
    TestColorsAndOpacity();
    TestRightToLeft();
    TestArgumentsAndLimits();
    TestEdgeCases();
    TestBuildsFromTheLastListMatchWholeOnes();
    TestRetainedEdges();
    TestGlyphRuns();
    TestGlyphRunsRightToLeftAndLimits();
    if (getenv(UPDATE_VARIABLE) != NULL)
    {
        WriteGolden();
    }
    return s_failures == 0 ? 0 : 1;
}
