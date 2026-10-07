// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's records (record mui-0005). A shadow's shape is
// its box offset and grown by its spread, as CSS's box-shadow, its radii
// grown with it by CSS's adjustment for small radii (or shrunk, inset);
// an outer shadow's quad reaches three sigmas past its shape, an inset
// one's is its box. An image's slice insets are one logical unit a texel
// as drawn, all four shrunk by one factor where two facing ones would
// not fit, as CSS's border-image shrinks them. A glyph run whose
// transform only moves it is drawn as coverage the atlas renders at its
// device pixels, the pen's place there and the quad's in the run's own
// units; one a transform scales or turns, or a glyph too large for the
// atlas as coverage, from a multi-channel distance field of an em of 32,
// 64 or 128 pixels, the least at least the em drawn, its spread 4 field
// pixels: a byte steps by a 32nd of a field pixel, and an em drawn at 8
// pixels from a field of 32 still reaches a device pixel past the edge.

#include "pack.h"

#include <math.h>
#include <string.h>

static_assert(sizeof(muiRhiInstance) == 144, "an instance is the shader's 144 bytes");
static_assert(sizeof(muiRhiGradient) == 112, "a gradient is the shader's 112 bytes");
static_assert(sizeof(muiRhiTransform) == 32, "a transform is the shader's 32 bytes");
static_assert(sizeof(muiRhiClip) == 48, "a clip is the shader's 48 bytes");

enum
{
    // How far a glyph's field reaches, in field pixels (see above).
    FIELD_SPREAD = 4
};

// An index into a table of a number of entries, 0 where it is past them.
static uint32_t Index(uint32_t index, uint32_t count)
{
    return index < count ? index : 0;
}

// Whether a glyph run's span lies in the list's glyph table.
static bool IsRunValid(const muiDrawList* list, const muiDrawGlyphRun* run)
{
    return run->firstGlyph <= list->glyphCount &&
           run->glyphCount <= list->glyphCount - run->firstGlyph;
}

uint32_t muiRhiCountInstances(const muiDrawList* list)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        const muiDrawCommand* command = &list->commands[i];
        if (command->kind == mui_drawGlyphRun)
        {
            count += IsRunValid(list, &command->glyphRun) ? command->glyphRun.glyphCount : 0;
        }
        else
        {
            count += command->kind >= mui_drawBox && command->kind <= mui_drawImage ? 1 : 0;
        }
    }
    return count;
}

uint32_t muiRhiCountImages(const muiDrawList* list)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        count += list->commands[i].kind == mui_drawImage ? 1 : 0;
    }
    return count;
}

static muiRhiInstance BoxOf(const muiDrawList* list, const muiDrawCommand* command)
{
    const muiDrawBox* box = &command->box;
    muiRhiInstance instance = {
        .rect = box->rect,
        .radii = box->radii,
        .fill = box->fill,
        .widths = box->borderWidths,
        .kind = mui_drawBox,
        .clip = Index(command->clip, list->clipCount),
        .transform = Index(command->transform, list->transformCount),
        .index = Index(box->gradient, list->gradientCount),
    };
    memcpy(instance.colors, box->borderColors, sizeof(instance.colors));
    return instance;
}

// A radius grown by an outer shadow's spread: by the spread where it is
// at least as large, less where it is smaller, and a square corner kept
// square (CSS Backgrounds 3, the spread distance's adjustment).
static float Spread(float radius, float spread)
{
    if (spread < 0.0f || radius >= spread)
    {
        return fmaxf(radius + spread, 0.0f);
    }
    if (radius <= 0.0f)
    {
        return 0.0f;
    }
    float ratio = radius / spread - 1.0f;
    return radius + spread * (1.0f + ratio * ratio * ratio);
}

static muiCorners SpreadAll(muiCorners radii, float spread)
{
    return (muiCorners){Spread(radii.topLeft, spread), Spread(radii.topRight, spread),
                        Spread(radii.bottomRight, spread), Spread(radii.bottomLeft, spread)};
}

static muiRhiInstance ShadowOf(const muiDrawList* list, const muiDrawCommand* command)
{
    const muiDrawShadow* shadow = &command->shadow;
    bool inset = shadow->inset != 0;
    float spread = inset ? -shadow->spread : shadow->spread;
    muiRect shape = {
        shadow->rect.x + shadow->offsetX - spread,
        shadow->rect.y + shadow->offsetY - spread,
        fmaxf(shadow->rect.width + 2.0f * spread, 0.0f),
        fmaxf(shadow->rect.height + 2.0f * spread, 0.0f),
    };
    float sigma = shadow->blur * 0.5f;
    float reach = 3.0f * sigma;
    muiRect quad = inset ? shadow->rect
                         : (muiRect){shape.x - reach, shape.y - reach, shape.width + 2.0f * reach,
                                     shape.height + 2.0f * reach};
    const muiCorners box = shadow->radii;
    return (muiRhiInstance){
        .rect = quad,
        .radii = SpreadAll(box, spread),
        .fill = shadow->color,
        .widths = {shape.x, shape.y, shape.width, shape.height},
        .colors =
            {
                {shadow->rect.x, shadow->rect.y, shadow->rect.width, shadow->rect.height},
                {box.topLeft, box.topRight, box.bottomRight, box.bottomLeft},
                {sigma, inset ? 1.0f : 0.0f, 0.0f, 0.0f},
            },
        .kind = mui_drawShadow,
        .clip = Index(command->clip, list->clipCount),
        .transform = Index(command->transform, list->transformCount),
    };
}

// The factor that fits two facing insets into a length.
static float Fit(float length, float low, float high)
{
    return low + high > length && low + high > 0.0f ? length / (low + high) : 1.0f;
}

static muiRhiInstance ImageOf(const muiDrawList* list, const muiDrawCommand* command,
                              const muiRhiImageEntry* entry, uint32_t index)
{
    const muiDrawImage* image = &command->image;
    const muiSides slice = {fmaxf(image->slice.top, 0.0f), fmaxf(image->slice.right, 0.0f),
                            fmaxf(image->slice.bottom, 0.0f), fmaxf(image->slice.left, 0.0f)};
    float fit = fminf(Fit(image->rect.width, slice.left, slice.right),
                      fminf(Fit(image->rect.height, slice.top, slice.bottom), 1.0f));
    // Texel insets run the way the uv does: back, for a mirrored image.
    float width = (float)entry->image.width * (image->uv.width < 0.0f ? -1.0f : 1.0f);
    float height = (float)entry->image.height * (image->uv.height < 0.0f ? -1.0f : 1.0f);
    return (muiRhiInstance){
        .rect = image->rect,
        .fill = image->tint,
        .colors =
            {
                {image->uv.x, image->uv.y, image->uv.x + image->uv.width,
                 image->uv.y + image->uv.height},
                {slice.top * fit, slice.right * fit, slice.bottom * fit, slice.left * fit},
                {slice.top / height, slice.right / width, slice.bottom / height,
                 slice.left / width},
            },
        .kind = mui_drawImage,
        .clip = Index(command->clip, list->clipCount),
        .transform = Index(command->transform, list->transformCount),
        .index = index,
    };
}

// A glyph's instance from its image in the atlas and its quad in the
// run's units; a field's factor makes a sample a distance in pixels, 0
// for coverage.
static muiRhiInstance GlyphOf(const muiDrawList* list, const muiDrawCommand* command,
                              const muiRhiGlyph* glyph, muiRect rect, float field)
{
    float width = (float)glyph->pageWidth;
    float height = (float)glyph->pageHeight;
    float u0 = (float)glyph->u / width;
    float v0 = (float)glyph->v / height;
    float u1 = (float)(glyph->u + glyph->width) / width;
    float v1 = (float)(glyph->v + glyph->height) / height;
    return (muiRhiInstance){
        .rect = rect,
        .fill = command->glyphRun.color,
        .colors =
            {
                {u0, v0, u1, v1},
                {u0 - 0.5f / width, v0 - 0.5f / height, u1 + 0.5f / width, v1 + 0.5f / height},
                {field, 0.0f, 0.0f, 0.0f},
            },
        .kind = mui_drawGlyphRun,
        .clip = Index(command->clip, list->clipCount),
        .transform = Index(command->transform, list->transformCount),
        .index = glyph->page,
    };
}

// The em of a field for an em drawn at a number of pixels.
static float FieldEm(float drawn)
{
    return drawn <= 32.0f ? 32.0f : (drawn <= 64.0f ? 64.0f : 128.0f);
}

// A glyph from its distance field, at a pen in the run's units.
static bool FieldGlyph(const muiDrawList* list, const muiDrawCommand* command, muiRhiGlyphs* glyphs,
                       uint32_t id, float penX, float penY, float drawn,
                       muiRhiInstance* instanceOut)
{
    const muiDrawGlyphRun* run = &command->glyphRun;
    float em = FieldEm(drawn);
    const uint32_t spread = FIELD_SPREAD;
    muiRhiGlyph glyph = {0};
    if (!muiRhiGetGlyphField(glyphs, run->font, id, em, spread, &glyph) || glyph.width == 0)
    {
        return false;
    }
    // Units of the run a field pixel spans.
    float k = run->size / em;
    const muiRect rect = {penX + (float)glyph.x * k, penY + (float)glyph.y * k,
                          (float)glyph.width * k, (float)glyph.height * k};
    // A sample's distance in field pixels is (255 sample - 128) spread /
    // 128; in device pixels, before the transform's scale, k times the
    // list's scale of that.
    float field = 255.0f * (float)spread / 128.0f * k * list->header.scale;
    *instanceOut = GlyphOf(list, command, &glyph, rect, field);
    return true;
}

// Whether an instance meets its clip's bounds.
static bool Kept(const muiRhiCull* cull, const muiDrawList* list, const muiRhiInstance* instance)
{
    return !muiRhiIsCulled(cull, list, instance->rect, instance->clip, instance->transform);
}

// A glyph run's instances: how many.
static uint32_t PackRun(const muiDrawList* list, const muiDrawCommand* command,
                        const muiRhiPacking* packing, muiRhiInstance* instances)
{
    muiRhiGlyphs* glyphs = packing->glyphs;
    const muiDrawGlyphRun* run = &command->glyphRun;
    if (!IsRunValid(list, run))
    {
        return 0;
    }
    uint32_t transform = Index(command->transform, list->transformCount);
    const muiDrawTransform moved = list->transformCount > 0 ? list->transforms[transform]
                                                            : (muiDrawTransform){1, 0, 0, 1, 0, 0};
    bool moves = moved.a == 1.0f && moved.b == 0.0f && moved.c == 0.0f && moved.d == 1.0f;
    float scale = list->header.scale;
    float drawn = run->size * scale * sqrtf(fabsf(moved.a * moved.d - moved.b * moved.c));
    uint32_t count = 0;
    for (uint32_t i = 0; i < run->glyphCount; i++)
    {
        const muiGlyph* at = &list->glyphs[run->firstGlyph + i];
        float penX = run->originX + at->x;
        float penY = run->originY + at->y;
        muiRhiGlyph glyph = {0};
        if (moves && muiRhiGetGlyph(glyphs, run->font, at->id, run->size * scale,
                                    (penX + moved.e) * scale, (penY + moved.f) * scale, &glyph))
        {
            if (glyph.width == 0)
            {
                continue;
            }
            const muiRect rect = {(float)glyph.x / scale - moved.e,
                                  (float)glyph.y / scale - moved.f, (float)glyph.width / scale,
                                  (float)glyph.height / scale};
            instances[count] = GlyphOf(list, command, &glyph, rect, 0.0f);
            count += Kept(packing->cull, list, &instances[count]) ? 1 : 0;
        }
        else if (FieldGlyph(list, command, glyphs, at->id, penX, penY, drawn, &instances[count]))
        {
            count += Kept(packing->cull, list, &instances[count]) ? 1 : 0;
        }
    }
    return count;
}

// An image's instance, its image found unless it is culled first.
static bool PackImage(const muiDrawList* list, const muiDrawCommand* command,
                      const muiRhiPacking* packing, muiRhiInstance* instanceOut)
{
    if (muiRhiIsCulled(packing->cull, list, command->image.rect,
                       Index(command->clip, list->clipCount),
                       Index(command->transform, list->transformCount)))
    {
        return false;
    }
    muiRhiImages* images = packing->images;
    uint32_t index = muiRhiFindImage(images, command->image.image);
    if (index == MUI_RHI_NO_IMAGE)
    {
        return false;
    }
    *instanceOut = ImageOf(list, command, &images->entries[index], index);
    return true;
}

uint32_t muiRhiPackInstances(const muiDrawList* list, const muiRhiPacking* packing,
                             muiRhiInstance* instances)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        const muiDrawCommand* command = &list->commands[i];
        if (command->kind == mui_drawBox)
        {
            instances[count] = BoxOf(list, command);
            count += Kept(packing->cull, list, &instances[count]) ? 1 : 0;
        }
        else if (command->kind == mui_drawShadow)
        {
            instances[count] = ShadowOf(list, command);
            count += Kept(packing->cull, list, &instances[count]) ? 1 : 0;
        }
        else if (command->kind == mui_drawImage)
        {
            count += PackImage(list, command, packing, &instances[count]) ? 1 : 0;
        }
        else if (command->kind == mui_drawGlyphRun)
        {
            count += PackRun(list, command, packing, &instances[count]);
        }
    }
    return count;
}

uint32_t muiRhiPackGradients(const muiDrawList* list, muiRhiGradient* gradients)
{
    uint32_t count = list->gradientCount > 0 ? list->gradientCount : 1;
    if (gradients == nullptr)
    {
        return count;
    }
    memset(gradients, 0, (size_t)count * sizeof(muiRhiGradient));
    for (uint32_t i = 0; i < list->gradientCount; i++)
    {
        const muiDrawGradient* from = &list->gradients[i];
        muiRhiGradient* to = &gradients[i];
        to->kind = from->kind;
        to->stopCount = from->stopCount < MUI_MAX_DRAW_STOPS ? from->stopCount : MUI_MAX_DRAW_STOPS;
        to->interpolation = from->interpolation;
        to->angle = from->angle;
        memcpy(to->colors, from->colors, sizeof(to->colors));
        memcpy(to->positions, from->positions, sizeof(to->positions));
    }
    return count;
}

uint32_t muiRhiPackTransforms(const muiDrawList* list, muiRhiTransform* transforms)
{
    uint32_t count = list->transformCount > 0 ? list->transformCount : 1;
    if (transforms == nullptr)
    {
        return count;
    }
    transforms[0] = (muiRhiTransform){{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}};
    for (uint32_t i = 0; i < list->transformCount; i++)
    {
        const muiDrawTransform* from = &list->transforms[i];
        transforms[i] =
            (muiRhiTransform){{from->a, from->b, from->c, from->d}, {from->e, from->f, 0.0f, 0.0f}};
    }
    return count;
}

uint32_t muiRhiPackClips(const muiDrawList* list, muiRhiClip* clips)
{
    uint32_t count = list->clipCount > 0 ? list->clipCount : 1;
    if (clips == nullptr)
    {
        return count;
    }
    clips[0] = (muiRhiClip){0};
    for (uint32_t i = 0; i < list->clipCount; i++)
    {
        const muiDrawClip* from = &list->clips[i];
        clips[i] = (muiRhiClip){
            .rect = from->rect,
            .radii = from->radii,
            .parent = Index(from->parent, list->clipCount),
            .transform = Index(from->transform, list->transformCount),
            .invert = from->invert != 0 ? 1u : 0u,
        };
    }
    return count;
}
