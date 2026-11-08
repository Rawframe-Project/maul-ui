// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text component's buffers sized by a count of items: a count whose
// bytes do not fit a size_t, as a product or as a sum of 32-bit counts
// can where a size_t is 32 bits, is refused without asking the
// allocator, never reserved at a size wrapped short.

#include "test_harness.h"
#include "text_block.h"

#include <stdint.h>
#include <stdlib.h>

static size_t s_asked;
static uint32_t s_calls;

static void* Refuse(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_asked = size;
    s_calls++;
    return NULL;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)memory;
    (void)size;
    (void)alignment;
    (void)context;
}

static void TestProductPastSizeMax(void)
{
    const muiAllocator allocator = {Refuse, Free, NULL};
    muiBuffer buffer = {0};
    // One item more than SIZE_MAX bytes of 8-byte items hold: on a 32-bit
    // size_t, 2^29 + 1 items, whose bytes would wrap to 8.
    const uint64_t count = (uint64_t)(SIZE_MAX / 8) + 1u;
    s_calls = 0;
    CHECK(!muiReserveItems(&allocator, &buffer, count, 0, 8), "refused");
    CHECK(s_calls == 0 && buffer.data == NULL && buffer.bytes == 0,
          "without asking the allocator, the buffer as it was");
}

static void TestSumOfCounts(void)
{
    const muiAllocator allocator = {Refuse, Free, NULL};
    muiBuffer buffer = {0};
    // Two 32-bit counts and one more, as a kept count and a new one make:
    // 2^33 bytes, past a 32-bit size_t, where a size_t sum would wrap to 0.
    const uint32_t kept = UINT32_MAX;
    const uint32_t added = UINT32_MAX;
    s_calls = 0;
    s_asked = 0;
    CHECK(!muiReserveItems(&allocator, &buffer, (uint64_t)kept + added + 2u, 0, 1),
          "refused, by the size or by the allocator");
#if SIZE_MAX == UINT32_MAX
    CHECK(s_calls == 0 && s_asked == 0, "past a 32-bit size_t, without asking the allocator");
#else
    CHECK(s_calls == 1 && s_asked == ((size_t)1 << 33), "the allocator asked for the whole sum");
#endif
    CHECK(buffer.data == NULL && buffer.bytes == 0, "the buffer as it was");
}

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return malloc(size);
}

static void Release(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static void TestItemsKept(void)
{
    const muiAllocator allocator = {Allocate, Release, NULL};
    muiBuffer buffer = {0};
    CHECK(muiReserveItems(&allocator, &buffer, 4, 0, sizeof(uint64_t)) &&
              buffer.bytes >= 4 * sizeof(uint64_t),
          "four items");
    uint64_t* items = buffer.data;
    for (uint64_t i = 0; buffer.data != NULL && i < 4; i++)
    {
        items[i] = 0x0101010101010101u * (i + 1);
    }
    CHECK(muiReserveItems(&allocator, &buffer, 100, 4, sizeof(uint64_t)) &&
              buffer.bytes >= 100 * sizeof(uint64_t),
          "a hundred");
    items = buffer.data;
    CHECK(items != NULL && items[0] == 0x0101010101010101u && items[3] == 0x0404040404040404u,
          "the first four kept");
    CHECK(muiReserveItems(&allocator, &buffer, 0, 0, sizeof(uint64_t)), "none, held already");
    muiFreeBuffer(&allocator, &buffer);
}

int main(void)
{
    TestProductPastSizeMax();
    TestSumOfCounts();
    TestItemsKept();
    return s_failures == 0 ? 0 : 1;
}
