// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying contexts, and their refusals.

#include "test_harness.h"

#include "maul-ui/context.h"

#include <stddef.h>
#include <stdlib.h>

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
    bad = def;
    bad.limits.nodes = 0;
    CHECK(muiCreateContext(&bad, &context) == mui_errorInvalid, "zero nodes");
    bad = def;
    bad.limits.nodes = 0x80000000u;
    CHECK(muiCreateContext(&bad, &context) == mui_errorInvalid, "too many nodes");
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
    return s_failures == 0 ? 0 : 1;
}
