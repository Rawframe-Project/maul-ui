// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying contexts, their refusals and limits, and the
// memory they take from the host's allocator.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

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
    CHECK(muiContext_GetMisuse(context) == 0, "no misuse yet");
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
    // Headers of another release are refused, and a zeroed version never
    // matches.
    bad = def;
    bad.version = MUI_ABI_VERSION + 1;
    CHECK(muiCreateContext(&bad, &context) == mui_errorVersion && context == NULL,
          "another release");
    bad.version = MUI_ABI_VERSION ^ (1u << 16);
    CHECK(muiCreateContext(&bad, &context) == mui_errorVersion, "another major version");
    bad.version = 0;
    CHECK(muiCreateContext(&bad, &context) == mui_errorVersion, "a zeroed version");
    bad = def;
    bad.limits.nodes = 0;
    CHECK(muiCreateContext(&bad, &context) == mui_errorInvalid, "zero nodes");
    bad = def;
    bad.limits.nodes = MUI_MAX_CONTEXT_LIMIT + 1u;
    CHECK(muiCreateContext(&bad, &context) == mui_errorLimit, "too many nodes");
    bad = def;
    bad.limits.drawGlyphs = 0x80000000u;
    CHECK(muiCreateContext(&bad, &context) == mui_errorLimit, "too many glyphs");
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

// An allocator need promise no more than max_align_t: a context in a block
// at any such distance past a cache line works.
static void TestAnyMaxAlignedBlockWorks(void)
{
    for (size_t shift = 0; shift < 64; shift += alignof(max_align_t))
    {
        ShiftingAllocator shifting = {shift, NULL};
        muiContextDef def = muiDefaultContextDef();
        def.allocator = (muiAllocator){ShiftingAlloc, ShiftingFree, &shifting};
        muiContext* context = NULL;
        CHECK(muiCreateContext(&def, &context) == mui_success, "created");
        CHECK((uintptr_t)context % 64 == shift, "the context opens the block");
        const muiNodeDef node = muiDefaultNodeDef();
        muiNodeId id = {0, 0};
        CHECK(muiCreateNode(context, &node, &id) == mui_success, "a node");
        const muiLayoutInput input = {100.0f, 100.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
        CHECK(muiNode_ComputeLayout(context, id, &input) == mui_success, "laid out");
        muiDestroyContext(context);
    }
}

// Each count's largest, 2^31 - 1 (one less where a slot is kept, and
// 32 for pointers, as documented), asks only for memory; one past it is
// invalid.
static void TestLimitsAtTheirLargest(void)
{
    typedef struct Count
    {
        size_t offset;
        uint32_t largest;
    } Count;
    const uint32_t most = MUI_MAX_CONTEXT_LIMIT;
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
        {offsetof(muiLimits, drawClips), most},
        {offsetof(muiLimits, drawGradients), most},
        {offsetof(muiLimits, drawGlyphs), most},
        {offsetof(muiLimits, layers), most},
        {offsetof(muiLimits, pointers), MUI_MAX_POINTERS},
        {offsetof(muiLimits, pointerRecords), most},
        {offsetof(muiLimits, neighbors), most},
        {offsetof(muiLimits, drawTransforms), most},
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
        bool past = muiCreateContext(&def, &context) == mui_errorLimit;
        if (!largest || !past)
        {
            fprintf(stderr, "count %zu: largest %d, past %d\n", i, largest, past);
        }
        held = held && largest && past;
    }
    CHECK(held, "the largest asks for memory, one past it is a limit");
}

#if SIZE_MAX == UINT32_MAX
// An allocator that refuses every block, keeping the size last asked.
typedef struct Asked
{
    uint32_t calls;
    size_t size;
} Asked;

static void* RefuseAndKeep(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    Asked* asked = context;
    asked->calls++;
    asked->size = size;
    return NULL;
}

static void FreeNothing(void* memory, size_t size, size_t alignment, void* context)
{
    (void)memory;
    (void)size;
    (void)alignment;
    (void)context;
}

// The block a def of items and roots asks for, or 0 when it asks for
// none.
static size_t AskedFor(uint32_t items, uint32_t roots)
{
    Asked asked = {0};
    muiContextDef def = muiDefaultContextDef();
    def.allocator = (muiAllocator){RefuseAndKeep, FreeNothing, &asked};
    def.limits.virtualItems = items;
    def.limits.accessRoots = roots;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_errorCapacity && context == NULL &&
              asked.calls <= 1,
          "refused, after one block at most");
    return asked.calls != 0 ? asked.size : 0;
}

// With 32 bits of size, a def's parts can end within the slack the block
// adds to reach a cache line, so the block's size would wrap. The items
// whose parts end nearest the top, then one root more at a time: each def
// asks for more than the last, until one is refused without asking.
static void TestBlockSizeDoesNotWrap(void)
{
    uint32_t low = 0;
    uint32_t high = MUI_MAX_CONTEXT_LIMIT;
    CHECK(AskedFor(low, 1) != 0 && AskedFor(high, 1) == 0, "the bounds");
    while (high - low > 1)
    {
        uint32_t middle = low + (high - low) / 2;
        if (AskedFor(middle, 1) != 0)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    size_t last = AskedFor(low, 1);
    CHECK(last > SIZE_MAX / 2, "near the top");
    bool refused = false;
    bool grew = true;
    for (uint32_t roots = 2; !refused && roots < 64; roots++)
    {
        size_t size = AskedFor(low, roots);
        refused = size == 0;
        grew = grew && (refused || size > last);
        last = refused ? last : size;
    }
    CHECK(grew && refused, "more each time, then refused");
}
#endif

static void TestNullContextIsHarmless(void)
{
    muiDestroyContext(NULL);
    CHECK(muiContext_GetMisuse(NULL) == 0, "misuse of NULL");
}

int main(void)
{
    TestDefaultDefCreatesAContext();
    TestMemoryIsOneBlockReturnedOnDestroy();
    TestInvalidDefsAreRefused();
    TestAllocatorFailureIsCapacity();
    TestNullContextIsHarmless();
    TestAnyMaxAlignedBlockWorks();
    TestLimitsAtTheirLargest();
#if SIZE_MAX == UINT32_MAX
    TestBlockSizeDoesNotWrap();
#endif
    return s_failures == 0 ? 0 : 1;
}
