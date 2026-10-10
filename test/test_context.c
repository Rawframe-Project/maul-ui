// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying contexts, their refusals, and where the parts
// of a context's block fall. White-box: it reads the parts' addresses.

#include "allocator.h"
#include "context.h"
#include "test_harness.h"

#include "maul-ui/context.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// An allocator that counts what it hands out and can be told to fail.
typedef struct CountingAllocator
{
    int allocations;
    int frees;
    size_t liveBytes;
    bool fail;
} CountingAllocator;

static void* CountingAlloc(size_t size, size_t alignment, void* context)
{
    CountingAllocator* counter = context;
    if (counter->fail)
    {
        return NULL;
    }
    counter->allocations++;
    counter->liveBytes += size;
    // The library asks for no more than max_align_t, which malloc serves.
    return alignment <= alignof(max_align_t) ? malloc(size) : NULL;
}

static void CountingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    CountingAllocator* counter = context;
    counter->frees++;
    counter->liveBytes -= size;
    free(memory);
}

static muiAllocator MakeAllocator(CountingAllocator* counter)
{
    return (muiAllocator){CountingAlloc, CountingFree, counter};
}

static void TestDefaultDefCreatesAContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    CHECK(def.limits.nodes == 4096, "default node limit");
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create");
    CHECK(context != NULL, "context set");
    CHECK(muiGetContextMisuse(context) == 0, "no misuse yet");
    muiDestroyContext(context);
}

static void TestMemoryIsOneBlockReturnedOnDestroy(void)
{
    CountingAllocator counter = {0};
    muiContextDef def = muiDefaultContextDef();
    def.allocator = MakeAllocator(&counter);
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create");
    CHECK(counter.allocations == 1, "one allocation");
    muiDestroyContext(context);
    CHECK(counter.frees == 1 && counter.liveBytes == 0, "all memory returned");
}

static void TestInvalidDefsAreRefused(void)
{
    muiContext* context = (muiContext*)&context;
    CHECK(muiCreateContext(NULL, &context) == mui_errorInvalid, "NULL def");
    CHECK(context == NULL, "out cleared");
    muiContextDef def = muiDefaultContextDef();
    CHECK(muiCreateContext(&def, NULL) == mui_errorInvalid, "NULL out");

    muiContextDef bad = def;
    bad.cookie = 0;
    CHECK(muiCreateContext(&bad, &context) == mui_errorInvalid, "bad cookie");
    // Headers of another minor or major are refused; another patch's are
    // the same version.
    bad = def;
    bad.version = MUI_DEF_VERSION + 1;
    CHECK(muiCreateContext(&bad, &context) == mui_errorVersion && context == NULL,
          "another minor version");
    bad.version = MUI_DEF_VERSION ^ (1u << 16);
    CHECK(muiCreateContext(&bad, &context) == mui_errorVersion, "another major version");
    bad = def;
    bad.limits.nodes = 0;
    CHECK(muiCreateContext(&bad, &context) == mui_errorInvalid, "zero nodes");
    bad = def;
    bad.limits.nodes = 0x80000000u;
    CHECK(muiCreateContext(&bad, &context) == mui_errorInvalid, "too many nodes");
    bad = def;
    bad.limits.drawGlyphs = 0x80000000u;
    CHECK(muiCreateContext(&bad, &context) == mui_errorInvalid, "too many glyphs");
    CountingAllocator counter = {0};
    bad = def;
    bad.allocator = MakeAllocator(&counter);
    bad.allocator.free = NULL;
    CHECK(muiCreateContext(&bad, &context) == mui_errorInvalid, "half-set allocator");
    CHECK(counter.allocations == 0, "nothing allocated");
}

static void TestAllocatorFailureIsCapacity(void)
{
    CountingAllocator counter = {.fail = true};
    muiContextDef def = muiDefaultContextDef();
    def.allocator = MakeAllocator(&counter);
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_errorCapacity, "capacity");
    CHECK(context == NULL, "no context");
}

// An allocator that gives blocks a set distance past a cache line, as an
// allocator that promises only max_align_t may.
typedef struct ShiftingAllocator
{
    size_t shift;
    void* raw;
} ShiftingAllocator;

static void* ShiftingAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    ShiftingAllocator* shifting = context;
    shifting->raw = malloc(size + 128);
    if (shifting->raw == NULL)
    {
        return NULL;
    }
    uintptr_t line = ((uintptr_t)shifting->raw + 63) & ~(uintptr_t)63;
    return (unsigned char*)shifting->raw + (line - (uintptr_t)shifting->raw) + shifting->shift;
}

static void ShiftingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)memory;
    (void)size;
    (void)alignment;
    free(((ShiftingAllocator*)context)->raw);
}

static bool OnLine(const void* part)
{
    return (uintptr_t)part % 64 == 0;
}

static void TestPartsStartOnCacheLines(void)
{
    for (size_t shift = 0; shift < 64; shift += alignof(max_align_t))
    {
        ShiftingAllocator shifting = {shift, NULL};
        muiContextDef def = muiDefaultContextDef();
        def.allocator = (muiAllocator){ShiftingAlloc, ShiftingFree, &shifting};
        // Odd counts, so no part ends on a cache line by chance.
        def.limits = (muiLimits){.nodes = 1001,
                                 .styles = 33,
                                 .nodeTypes = 7,
                                 .propertySets = 101,
                                 .notifications = 9,
                                 .transitions = 11,
                                 .animations = 13,
                                 .tokens = 15,
                                 .tokenNames = 17,
                                 .themes = 3,
                                 .themeOverrides = 19,
                                 .drawCommands = 1001,
                                 .drawClips = 21,
                                 .drawGradients = 23,
                                 .drawGlyphs = 25};
        muiContext* context = NULL;
        CHECK(muiCreateContext(&def, &context) == mui_success, "created");
        CHECK((uintptr_t)context % 64 == shift, "the context opens the block");
        CHECK(OnLine(context->tree.nodes) && OnLine(context->layout) && OnLine(context->visual) &&
                  OnLine(context->text) && OnLine(context->textRecords) &&
                  OnLine(context->style.nodes) && OnLine(context->draw.states) &&
                  OnLine(context->draw.tables[0].commands),
              "per-node parts start on cache lines, wherever the block does");
        // The parts end inside the block.
        const unsigned char* end = (const unsigned char*)context + context->blockSize;
        CHECK((const unsigned char*)(context->draw.tables[1].glyphs +
                                     context->draw.glyphCapacity) <= end,
              "the last part fits");
        muiDestroyContext(context);
    }
}

// A block's parts follow one another, each padded only up to its own
// alignment: one that already starts aligned is not padded (found by a
// mutant padding a whole alignment more).
static void TestPartsPacked(void)
{
    muiLayout layout = {0, false};
    CHECK(muiLayoutAdd(&layout, 3, 8, 8) == 0 && layout.size == 24, "three eights");
    CHECK(muiLayoutAdd(&layout, 2, 4, 8) == 24 && layout.size == 32, "aligned already");
    CHECK(muiLayoutAdd(&layout, 1, 2, 2) == 32 && layout.size == 34, "two bytes");
    CHECK(muiLayoutAdd(&layout, 1, 16, 16) == 48 && layout.size == 64, "padded to sixteen");
    CHECK(muiLayoutAdd(&layout, SIZE_MAX, 2, 1) == 0 && layout.overflow &&
              muiLayoutAdd(&layout, 1, 1, 1) == 0,
          "an overflow, and nothing after it");
}

// Each count's largest, 2^31 - 1 (one less where a slot is kept, and
// MUI_MAX_POINTERS for pointers), asks only for memory; one past it is
// invalid (found by a mutant refusing the largest notifications).
static void TestLimitsAtTheirLargest(void)
{
    typedef struct Count
    {
        size_t offset;
        uint32_t largest;
    } Count;
    const uint32_t most = 0x7FFFFFFFu;
    const Count counts[] = {
        {offsetof(muiLimits, nodes), most},
        {offsetof(muiLimits, styles), most},
        {offsetof(muiLimits, nodeTypes), most},
        {offsetof(muiLimits, propertySets), most},
        {offsetof(muiLimits, notifications), most},
        {offsetof(muiLimits, transitions), most},
        {offsetof(muiLimits, animations), most},
        {offsetof(muiLimits, tokens), most},
        {offsetof(muiLimits, tokenNames), most},
        {offsetof(muiLimits, themes), most},
        {offsetof(muiLimits, themeOverrides), most},
        {offsetof(muiLimits, drawCommands), most},
        {offsetof(muiLimits, drawClips), most - 1},
        {offsetof(muiLimits, drawGradients), most - 1},
        {offsetof(muiLimits, drawGlyphs), most},
        {offsetof(muiLimits, layers), most},
        {offsetof(muiLimits, pointers), MUI_MAX_POINTERS},
        {offsetof(muiLimits, pointerRecords), most},
        {offsetof(muiLimits, neighbors), most},
        {offsetof(muiLimits, drawTransforms), most - 1},
        {offsetof(muiLimits, ranges), most},
        {offsetof(muiLimits, popups), most},
        {offsetof(muiLimits, exits), most},
        {offsetof(muiLimits, virtualLists), most},
        {offsetof(muiLimits, virtualItems), most},
        {offsetof(muiLimits, accessNodes), most},
        {offsetof(muiLimits, accessRoots), most},
    };
    bool held = true;
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++)
    {
        CountingAllocator counter = {.fail = true};
        muiContextDef def = muiDefaultContextDef();
        def.allocator = MakeAllocator(&counter);
        muiContext* context = NULL;
        uint32_t value = counts[i].largest;
        memcpy((unsigned char*)&def.limits + counts[i].offset, &value, sizeof value);
        bool largest = muiCreateContext(&def, &context) == mui_errorCapacity;
        value++;
        memcpy((unsigned char*)&def.limits + counts[i].offset, &value, sizeof value);
        bool past = muiCreateContext(&def, &context) == mui_errorInvalid;
        if (!largest || !past)
        {
            fprintf(stderr, "count %zu: largest %d, past %d\n", i, largest, past);
        }
        held = held && largest && past;
    }
    CHECK(held, "the largest asks for memory, one past it is invalid");
}

static void TestNullContextIsHarmless(void)
{
    muiDestroyContext(NULL);
    CHECK(muiGetContextMisuse(NULL) == 0, "misuse of NULL");
}

int main(void)
{
    TestDefaultDefCreatesAContext();
    TestMemoryIsOneBlockReturnedOnDestroy();
    TestInvalidDefsAreRefused();
    TestAllocatorFailureIsCapacity();
    TestNullContextIsHarmless();
    TestPartsStartOnCacheLines();
    TestPartsPacked();
    TestLimitsAtTheirLargest();
    return s_failures == 0 ? 0 : 1;
}
