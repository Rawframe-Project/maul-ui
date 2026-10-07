// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's culling as a list is packed, without a
// device: boxes inside and outside the target, within the margin an
// edge covers and past it; outside their clip and half in it; under an
// inverted clip, which bounds nothing; under a nested clip, and one
// whose parent comes after it, bounded by the target alone; turned;
// scaled onto the target from past it; a
// shadow whose blur reaches in from outside; an image off the target,
// for which the host is not asked. Then batching: commands changing
// clip, transform and gradient at every step plan as one draw, and
// images split draws only where their texture changes.

#include "pack.h"
#include "plan.h"
#include "test_harness.h"

#include <math.h>

static muiDrawCommand Box(float x, float y, float width, float height, uint32_t clip)
{
    muiDrawCommand command = {.kind = mui_drawBox, .clip = clip};
    command.box.rect = (muiRect){x, y, width, height};
    return command;
}

// The x of each packed instance's quad, by which the cases are told.
static bool Packed(const muiRhiInstance* instances, uint32_t count, const float* xs,
                   uint32_t expected)
{
    bool same = count == expected;
    for (uint32_t i = 0; same && i < count; i++)
    {
        same = instances[i].rect.x == xs[i];
    }
    if (!same)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            printf("  packed %u at x %g\n", i, (double)instances[i].rect.x);
        }
    }
    return same;
}

static void TestBoxes(void)
{
    const float turn = 0.70710678f;
    // An eighth turn about the origin, moved to 32, 32; a quarter scale.
    const muiDrawTransform transforms[3] = {
        {1, 0, 0, 1, 0, 0}, {turn, turn, -turn, turn, 32, 32}, {0.25f, 0, 0, 0.25f, 0, 0}};
    const muiDrawClip clips[6] = {
        {0},
        // 1: 0 to 10 each way.
        {.rect = {0, 0, 10, 10}},
        // 2: 0 to 10 each way, inverted.
        {.rect = {0, 0, 10, 10}, .invert = 1},
        // 3: 10 to 50 across, its parent 5 after it: bounded by the
        // target alone.
        {.rect = {10, 0, 40, 64}, .parent = 5},
        // 4: 10 to 50 across inside clip 1: 10 alone.
        {.rect = {10, 0, 40, 64}, .parent = 1},
        // 5: 0 to 20 across.
        {.rect = {0, 0, 20, 64}},
    };
    muiDrawCommand commands[13] = {
        Box(1, 1, 4, 4, 0),       // kept: inside
        Box(100, 1, 4, 4, 0),     // culled: right of the target
        Box(64.5f, 1, 1, 1, 0),   // kept: within the margin
        Box(66, 1, 4, 4, 0),      // culled: past it
        Box(30, 30, 4, 4, 1),     // culled: outside its clip
        Box(8, 8, 6, 6, 1),       // kept: half in it
        Box(33, 30, 4, 4, 2),     // kept: an inverted clip bounds nothing
        Box(30, 3, 4, 4, 3),      // kept: its clip's parent comes after it
        Box(31, 3, 4, 4, 4),      // culled: outside its parent's clip
        Box(-30, 3, 10, 10, 0),   // kept: turned onto the target
        {.kind = mui_drawShadow}, // kept: its blur reaches in
        Box(3, 70, 4, 4, 0),      // culled: below the target
        Box(200, 10, 8, 8, 0),    // kept: scaled onto the target
    };
    commands[9].transform = 1;
    commands[12].transform = 2;
    commands[10].shadow = (muiDrawShadow){.rect = {70, 10, 10, 10}, .blur = 20};
    muiDrawList list = {.commands = commands, .commandCount = 13};
    list.clips = clips;
    list.clipCount = 6;
    list.transforms = transforms;
    list.transformCount = 3;
    list.header.scale = 1.0f;
    muiRhiCull cull = {0};
    muiRhiImages images = muiRhiMakeImages(&(muiAllocator){0}, NULL, NULL, NULL);
    muiRhiGlyphs glyphs = {0};
    CHECK(muiRhiPrepareCull(&cull, &list, 64, 64) == mui_success, "bounds worked out");
    muiRhiInstance instances[13];
    const muiRhiPacking packing = {&images, &glyphs, &cull};
    uint32_t count = muiRhiPackInstances(&list, &packing, instances);
    const float xs[8] = {1, 64.5f, 8, 33, 30, -30, 40, 200};
    CHECK(
        Packed(instances, count, xs, 8),
        "the target, its margin, clips inverted, nested and out of order, turned, scaled, a blur");
    muiRhiFreeCull(&cull);
    muiRhiFreeImages(&images);
}

// The host's function counts its calls and has no images.
static bool CountCalls(void* context, uint64_t key, muiRhiImage* imageOut)
{
    (void)key;
    (void)imageOut;
    (*(int*)context)++;
    return false;
}

static void TestImages(void)
{
    muiDrawCommand commands[2] = {{.kind = mui_drawImage}, {.kind = mui_drawImage}};
    commands[0].image.rect = (muiRect){80, 0, 8, 8};
    commands[0].image.image = 1;
    commands[1].image.rect = (muiRect){0, 0, 8, 8};
    commands[1].image.image = 2;
    muiDrawList list = {.commands = commands, .commandCount = 2};
    list.header.scale = 2.0f;
    int calls = 0;
    muiRhiCull cull = {0};
    muiRhiImages images = muiRhiMakeImages(&(muiAllocator){0}, NULL, CountCalls, &calls);
    muiRhiGlyphs glyphs = {0};
    muiRhiInstance instances[2];
    const muiRhiPacking packing = {&images, &glyphs, &cull};
    // 128 device pixels at a scale of 2 are 64 units: the first image is
    // past them.
    CHECK(muiRhiPrepareCull(&cull, &list, 128, 128) == mui_success &&
              muiRhiResetImages(&images, 2) == mui_success &&
              muiRhiPackInstances(&list, &packing, instances) == 0 && calls == 1,
          "the host asked for the image on the target alone");
    muiRhiFreeCull(&cull);
    muiRhiFreeImages(&images);
}

// Clips, transforms and gradients are indices into tables the shader
// reads: changing them at every command keeps one draw.
static void TestOneDraw(void)
{
    const muiDrawTransform transforms[2] = {{1, 0, 0, 1, 0, 0}, {0, 1, -1, 0, 32, 0}};
    const muiDrawClip clips[4] = {{0},
                                  {.rect = {0, 0, 32, 64}},
                                  {.rect = {0, 0, 64, 32}, .invert = 1},
                                  {.rect = {4, 4, 40, 40}, .parent = 1, .radii = {8, 8, 8, 8}}};
    const muiDrawGradient gradients[3] = {
        {0},
        {.kind = mui_gradientLinear, .stopCount = 2, .positions = {0.0f, 1.0f}},
        {.kind = mui_gradientConic, .stopCount = 2, .positions = {0.0f, 1.0f}}};
    muiDrawCommand commands[12];
    for (uint32_t i = 0; i < 12; i++)
    {
        commands[i] = Box((float)i, (float)i, 8, 8, i % 4);
        commands[i].transform = i % 2;
        commands[i].box.gradient = i % 3;
    }
    muiDrawList list = {.commands = commands, .commandCount = 12};
    list.clips = clips;
    list.clipCount = 4;
    list.transforms = transforms;
    list.transformCount = 2;
    list.gradients = gradients;
    list.gradientCount = 3;
    list.header.scale = 1.0f;
    muiRhiCull cull = {0};
    muiRhiImages images = muiRhiMakeImages(&(muiAllocator){0}, NULL, NULL, NULL);
    muiRhiGlyphs glyphs = {0};
    muiRhiInstance instances[12];
    const muiRhiPacking packing = {&images, &glyphs, &cull};
    CHECK(muiRhiPrepareCull(&cull, &list, 64, 64) == mui_success, "bounds worked out");
    uint32_t count = muiRhiPackInstances(&list, &packing, instances);
    muiRhiPlan plan = {0};
    const muiRhiSources sources = {NULL, 0, &images, &glyphs, {1, 1}};
    CHECK(count == 12 && muiRhiMakePlan(&plan, instances, count, &sources) == mui_success &&
              plan.drawCount == 1 && plan.draws[0].count == 12,
          "twelve commands, each with another clip, transform and gradient: one draw");
    muiRhiFreePlan(&plan);
    muiRhiFreeCull(&cull);
    muiRhiFreeImages(&images);
}

// Images of two textures among boxes: a draw starts only where the
// texture changes, boxes joining the draw they fall in.
static void TestTextureSplits(void)
{
    muiRhiImages images = muiRhiMakeImages(&(muiAllocator){0}, NULL, NULL, NULL);
    CHECK(muiRhiResetImages(&images, 3) == mui_success, "a table");
    const mrhiResourceId a = {1, 1};
    const mrhiResourceId b = {2, 1};
    // Two keys of texture a, one of b.
    images.entries[0].resource = a;
    images.entries[1].resource = a;
    images.entries[2].resource = b;
    images.count = 3;
    muiRhiGlyphs glyphs = {0};
    const uint32_t kinds[8] = {mui_drawBox,   mui_drawImage, mui_drawBox,   mui_drawImage,
                               mui_drawImage, mui_drawBox,   mui_drawImage, mui_drawBox};
    const uint32_t entries[8] = {0, 0, 0, 1, 2, 0, 0, 0};
    muiRhiInstance instances[8];
    for (uint32_t i = 0; i < 8; i++)
    {
        instances[i] = (muiRhiInstance){.kind = kinds[i], .index = entries[i], .clip = i % 3};
    }
    muiRhiPlan plan = {0};
    const muiRhiSources sources = {NULL, 0, &images, &glyphs, {3, 1}};
    CHECK(muiRhiMakePlan(&plan, instances, 8, &sources) == mui_success && plan.drawCount == 3 &&
              plan.draws[0].first == 0 && plan.draws[0].count == 4 && plan.draws[1].first == 4 &&
              plan.draws[1].count == 2 && plan.draws[2].first == 6 && plan.draws[2].count == 2 &&
              plan.draws[1].texture.index1 == 2 && plan.draws[2].texture.index1 == 1,
          "a, a again by another key, then b, then a: three draws");
    muiRhiFreePlan(&plan);
    muiRhiFreeImages(&images);
}

int main(void)
{
    TestBoxes();
    TestImages();
    TestOneDraw();
    TestTextureSplits();
    return s_failures == 0 ? 0 : 1;
}
