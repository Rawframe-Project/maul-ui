// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Painting a subtree into the draw list (record mui-0005). The walk is
// preorder over the tree's links, each node reading what its parent left
// in the per-node paint state, so it needs no stack. Records are zeroed
// before their fields are written, so equal trees give equal bytes.

#include "color.h"
#include "context.h"
#include "draw_store.h"
#include "layout_node.h"
#include "tree.h"

#include "maul-ui/draw.h"

#include <math.h>
#include <string.h>

// Records with no padding, so that their bytes are their fields'.
static_assert(sizeof(muiDrawBox) == 136 && sizeof(muiDrawShadow) == 68 &&
                  sizeof(muiDrawImage) == 72 && sizeof(muiDrawCommand) == 152 &&
                  sizeof(muiDrawClip) == 44 && sizeof(muiDrawGradient) == 96,
              "draw records have no padding");

enum
{
    // Colors converted in a build, kept by a hash of their bits: nodes
    // share colors through their classes, and a conversion takes three
    // powers.
    COLOR_CACHE = 64
};

// A color's bits and its red, green and blue in linear light. A zeroed
// entry holds clear black, whose are 0.
typedef struct CachedColor
{
    uint32_t bits[4];
    double rgb[3];
} CachedColor;

// What one build writes to, and whether something did not fit.
typedef struct Painter
{
    muiContext* context;
    muiDrawStore* store;
    float scale;
    bool full;
    CachedColor colors[COLOR_CACHE];
} Painter;

// A color in linear light times opacity, converted from the cache when a
// node before converted the same bits.
static muiLinearColor Linear(Painter* painter, muiColor color, float opacity)
{
    uint32_t words[4];
    memcpy(words, &color, sizeof color);
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < 4; i++)
    {
        hash = (hash ^ words[i]) * 16777619u;
    }
    CachedColor* entry = &painter->colors[hash % COLOR_CACHE];
    if (memcmp(entry->bits, words, sizeof words) != 0)
    {
        memcpy(entry->bits, words, sizeof words);
        muiColorToLinearRgb(color, entry->rgb);
    }
    return muiPremultiply(entry->rgb, color.a, opacity);
}

// An edge at the nearest device pixel, halves away from the origin's
// left.
static float SnapEdge(float value, float scale)
{
    return floorf(value * scale + 0.5f) / scale;
}

// A span from start, by its edges; a span that was not empty keeps one
// device pixel.
static void SnapSpan(float* start, float* length, float scale)
{
    float first = SnapEdge(*start, scale);
    float last = SnapEdge(*start + *length, scale);
    if (*length > 0.0f && last <= first)
    {
        last = first + 1.0f / scale;
    }
    *start = first;
    *length = last - first;
}

static muiRect SnapRect(muiRect rect, float scale)
{
    SnapSpan(&rect.x, &rect.width, scale);
    SnapSpan(&rect.y, &rect.height, scale);
    return rect;
}

// A border width in whole device pixels, at least one when not 0, as CSS
// draws borders.
static float SnapWidth(float width, float scale)
{
    if (width <= 0.0f)
    {
        return 0.0f;
    }
    float pixels = floorf(width * scale);
    return (pixels < 1.0f ? 1.0f : pixels) / scale;
}

// A radius of the border box: scale times its shorter side plus offset,
// both of which are at least 0, held to half that side.
static float Radius(muiDimension radius, float shorter)
{
    return fminf(radius.scale * shorter + radius.offset, shorter * 0.5f);
}

static muiCorners Radii(const muiCornerRadii* radii, muiRect rect, bool rtl)
{
    float shorter = fminf(rect.width, rect.height);
    float topStart = Radius(radii->topStart, shorter);
    float topEnd = Radius(radii->topEnd, shorter);
    float bottomEnd = Radius(radii->bottomEnd, shorter);
    float bottomStart = Radius(radii->bottomStart, shorter);
    return rtl ? (muiCorners){topEnd, topStart, bottomStart, bottomEnd}
               : (muiCorners){topStart, topEnd, bottomEnd, bottomStart};
}

static muiDrawCommand* TakeCommand(Painter* painter, muiDrawKind kind, uint32_t clip)
{
    muiDrawStore* store = painter->store;
    if (store->commandCount == store->commandCapacity)
    {
        painter->full = true;
        return nullptr;
    }
    muiDrawCommand* command = &store->commands[store->commandCount++];
    memset(command, 0, sizeof *command);
    command->kind = kind;
    command->clip = clip;
    return command;
}

static uint32_t AddGradient(Painter* painter, const muiGradient* gradient, float opacity)
{
    muiDrawStore* store = painter->store;
    if (store->gradientCount == store->gradientCapacity)
    {
        painter->full = true;
        return 0;
    }
    uint32_t index = store->gradientCount++;
    muiDrawGradient* entry = &store->gradients[index];
    memset(entry, 0, sizeof *entry);
    entry->kind = gradient->kind;
    entry->stopCount = gradient->stopCount;
    entry->interpolation = mui_interpolateOklab;
    entry->angle = gradient->angle;
    for (uint32_t i = 0; i < gradient->stopCount; i++)
    {
        entry->colors[i] = Linear(painter, gradient->stops[i].color, opacity);
        entry->positions[i] = gradient->stops[i].position;
    }
    return index;
}

static void AddShadow(Painter* painter, const muiShadow* shadow, muiRect rect, muiCorners radii,
                      bool inset, const muiPaintState* state)
{
    if (shadow->color.a <= 0.0f)
    {
        return;
    }
    muiDrawCommand* command = TakeCommand(painter, mui_drawShadow, state->clip);
    if (command == nullptr)
    {
        return;
    }
    command->shadow.rect = rect;
    command->shadow.radii = radii;
    command->shadow.color = Linear(painter, shadow->color, state->opacity);
    command->shadow.offsetX = shadow->offsetX;
    command->shadow.offsetY = shadow->offsetY;
    command->shadow.blur = shadow->blur;
    command->shadow.spread = shadow->spread;
    command->shadow.inset = inset ? 1u : 0u;
}

// The border widths and colors, physical.
typedef struct Borders
{
    muiSides widths;
    muiColor colors[4];
} Borders;

static Borders BordersOf(const muiLayoutNode* layout, const muiVisualStyle* visual, float scale)
{
    const muiEdges* width = &layout->style.border;
    const muiEdgeColors* color = &visual->borderColor;
    float start = SnapWidth(width->start, scale);
    float end = SnapWidth(width->end, scale);
    Borders borders = {
        .widths = {SnapWidth(width->top, scale), end, SnapWidth(width->bottom, scale), start},
        .colors = {color->top, color->end, color->bottom, color->start},
    };
    if (layout->rtl)
    {
        borders.widths.right = start;
        borders.widths.left = end;
        borders.colors[1] = color->start;
        borders.colors[3] = color->end;
    }
    return borders;
}

static bool HasVisibleBorder(const Borders* borders)
{
    const float widths[4] = {borders->widths.top, borders->widths.right, borders->widths.bottom,
                             borders->widths.left};
    for (uint32_t i = 0; i < 4; i++)
    {
        if (widths[i] > 0.0f && borders->colors[i].a > 0.0f)
        {
            return true;
        }
    }
    return false;
}

static void AddBox(Painter* painter, const muiVisualStyle* visual, const Borders* borders,
                   muiRect rect, muiCorners radii, const muiPaintState* state)
{
    bool gradient = visual->gradient.kind != mui_gradientNone;
    if (visual->background.a <= 0.0f && !gradient && !HasVisibleBorder(borders))
    {
        return;
    }
    muiDrawCommand* command = TakeCommand(painter, mui_drawBox, state->clip);
    if (command == nullptr)
    {
        return;
    }
    command->box.rect = SnapRect(rect, painter->scale);
    command->box.radii = radii;
    command->box.fill = Linear(painter, visual->background, state->opacity);
    command->box.gradient = gradient ? AddGradient(painter, &visual->gradient, state->opacity) : 0;
    command->box.borderWidths = borders->widths;
    // A side of no width draws no color, so it carries none.
    const float widths[4] = {borders->widths.top, borders->widths.right, borders->widths.bottom,
                             borders->widths.left};
    for (uint32_t i = 0; i < 4; i++)
    {
        if (widths[i] > 0.0f)
        {
            command->box.borderColors[i] = Linear(painter, borders->colors[i], state->opacity);
        }
    }
}

static void AddImage(Painter* painter, const muiVisualStyle* visual, muiRect rect,
                     const muiPaintState* state)
{
    if (visual->image == 0)
    {
        return;
    }
    muiDrawCommand* command = TakeCommand(painter, mui_drawImage, state->clip);
    if (command == nullptr)
    {
        return;
    }
    const muiEdges* slice = &visual->imageSlice;
    command->image.rect = SnapRect(rect, painter->scale);
    command->image.image = visual->image;
    command->image.uv = (muiRect){0.0f, 0.0f, 1.0f, 1.0f};
    // An image does not mirror: its slice's start and end are its left and
    // right.
    command->image.slice = (muiSides){slice->top, slice->end, slice->bottom, slice->start};
    command->image.tint = Linear(painter, visual->imageTint, state->opacity);
}

// The padding box of a border box, and its corners' radii.
static void PaddingBox(muiRect* rect, muiCorners* radii, const muiSides* widths)
{
    rect->x += widths->left;
    rect->y += widths->top;
    rect->width = fmaxf(rect->width - widths->left - widths->right, 0.0f);
    rect->height = fmaxf(rect->height - widths->top - widths->bottom, 0.0f);
    radii->topLeft = fmaxf(radii->topLeft - fmaxf(widths->left, widths->top), 0.0f);
    radii->topRight = fmaxf(radii->topRight - fmaxf(widths->right, widths->top), 0.0f);
    radii->bottomRight = fmaxf(radii->bottomRight - fmaxf(widths->right, widths->bottom), 0.0f);
    radii->bottomLeft = fmaxf(radii->bottomLeft - fmaxf(widths->left, widths->bottom), 0.0f);
}

// A clip of the node's rounded border box for its children, inside the
// clip it is drawn in; that clip when none fits.
static uint32_t AddClip(Painter* painter, muiRect rect, muiCorners radii, uint32_t parent)
{
    muiDrawStore* store = painter->store;
    if (store->clipCount == store->clipCapacity)
    {
        painter->full = true;
        return parent;
    }
    uint32_t index = store->clipCount++;
    muiDrawClip* clip = &store->clips[index];
    memset(clip, 0, sizeof *clip);
    clip->rect = SnapRect(rect, painter->scale);
    clip->radii = radii;
    clip->parent = parent;
    return index;
}

// Paints one node from its parent's state into its own; false when the
// node and its subtree draw nothing.
static bool PaintNode(Painter* painter, uint32_t slot, const muiPaintState* parent)
{
    muiContext* context = painter->context;
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiVisualStyle* visual = &context->visual[slot - 1];
    muiPaintState* state = &painter->store->states[slot - 1];
    *state = (muiPaintState){parent->x + layout->rect.x, parent->y + layout->rect.y, parent->clip,
                             parent->opacity * visual->opacity};
    if (state->opacity <= 0.0f)
    {
        return false;
    }
    muiRect rect = {state->x, state->y, layout->rect.width, layout->rect.height};
    muiCorners radii = Radii(&visual->radius, rect, layout->rtl);
    Borders borders = BordersOf(layout, visual, painter->scale);
    AddShadow(painter, &visual->outerShadow, rect, radii, false, state);
    AddBox(painter, visual, &borders, rect, radii, state);
    muiRect inner = rect;
    muiCorners innerRadii = radii;
    PaddingBox(&inner, &innerRadii, &borders.widths);
    AddShadow(painter, &visual->innerShadow, inner, innerRadii, true, state);
    AddImage(painter, visual, rect, state);
    if (visual->clip)
    {
        state->clip = AddClip(painter, rect, radii, state->clip);
    }
    return true;
}

// The node after at in preorder below root, skipping at's subtree when
// descend is false; 0 at the end.
static uint32_t Next(const muiTree* tree, uint32_t root, uint32_t at, bool descend)
{
    if (descend && muiTreeAt(tree, at)->links.firstChild != 0)
    {
        return muiTreeAt(tree, at)->links.firstChild;
    }
    while (at != root && muiTreeAt(tree, at)->links.next == 0)
    {
        at = muiTreeAt(tree, at)->links.parent;
    }
    return at == root ? 0 : muiTreeAt(tree, at)->links.next;
}

static void Paint(Painter* painter, uint32_t root)
{
    const muiTree* tree = &painter->context->tree;
    const muiPaintState top = {0.0f, 0.0f, 0, 1.0f};
    for (uint32_t at = root; at != 0 && !painter->full;)
    {
        uint32_t parent = at == root ? 0 : muiTreeAt(tree, at)->links.parent;
        const muiPaintState* above = parent != 0 ? &painter->store->states[parent - 1] : &top;
        bool drawn = PaintNode(painter, at, above);
        at = Next(tree, root, at, drawn);
    }
}

static void Clear(muiDrawStore* store)
{
    store->header = (muiDrawHeader){0};
    store->commandCount = 0;
    store->clipCount = 1;
    store->gradientCount = 1;
}

muiResult muiBuildDrawList(muiContext* context, muiNodeId rootId, const muiDrawInput* input)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (input == nullptr || rootId.index1 == 0 || !isfinite(input->scale) || input->scale <= 0.0f ||
        muiIsMeasuring(context))
    {
        return muiRefuse(context);
    }
    uint32_t root = muiTreeResolve(&context->tree, rootId);
    if (root == 0)
    {
        return mui_errorStale;
    }
    muiDrawStore* store = &context->draw;
    uint64_t generation = store->header.generation + 1;
    Clear(store);
    // Large for the stack, so it is cleared here once rather than built.
    static_assert(sizeof(Painter) < 8192, "a painter fits a stack frame");
    Painter painter;
    memset(&painter, 0, sizeof painter);
    painter.context = context;
    painter.store = store;
    painter.scale = input->scale;
    Paint(&painter, root);
    if (painter.full)
    {
        Clear(store);
        store->header.generation = generation;
        return mui_errorCapacity;
    }
    const muiRect* rect = &context->layout[root - 1].rect;
    store->header = (muiDrawHeader){
        .surface = input->surface,
        .generation = generation,
        .width = rect->width,
        .height = rect->height,
        .scale = input->scale,
    };
    (void)muiTreeSweep(&context->tree, root, mui_stagePaint);
    return mui_success;
}

muiResult muiGetDrawList(const muiContext* context, muiDrawList* listOut)
{
    if (context == nullptr || listOut == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiDrawStore* store = &context->draw;
    *listOut = (muiDrawList){
        .header = store->header,
        .commands = store->commands,
        .commandCount = store->commandCount,
        .clipCount = store->clipCount,
        .clips = store->clips,
        .transforms = &store->identity,
        .transformCount = 1,
        .gradientCount = store->gradientCount,
        .gradients = store->gradients,
    };
    return mui_success;
}
