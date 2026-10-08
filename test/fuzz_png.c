// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes PNG images (src/png.c), the colour bitmaps of sbix and CBDT
// fonts. A PNG is asked its size first, with no room; then decoded into
// exactly its width * height * 4 bytes, giving success or a format
// error; an image decoded once decodes the same again. Scratch is freed
// after each, and nothing is read or written past the input, the pixels
// or the scratch.

#include "png.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    // A side as large as glyph images take, kept small for speed.
    MAX_EXTENT = 512
};

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const muiAllocator allocator = {Alloc, Free, nullptr};
    muiBuffer scratch = {nullptr, 0};
    uint32_t width = 0;
    uint32_t height = 0;
    muiResult result =
        muiDecodePng(&allocator, &scratch, data, size, MAX_EXTENT, &width, &height, nullptr, 0);
    Expect(result == mui_errorCapacity || result == mui_errorFormat);
    if (result == mui_errorCapacity && width != 0 && height != 0)
    {
        Expect(width <= MAX_EXTENT && height <= MAX_EXTENT);
        size_t bytes = (size_t)width * height * 4u;
        uint8_t* pixels = malloc(bytes);
        uint8_t* again = malloc(bytes);
        Expect(pixels != nullptr && again != nullptr);
        result = muiDecodePng(&allocator, &scratch, data, size, MAX_EXTENT, &width, &height, pixels,
                              bytes);
        Expect(result == mui_success || result == mui_errorFormat);
        if (result == mui_success)
        {
            Expect(muiDecodePng(&allocator, &scratch, data, size, MAX_EXTENT, &width, &height,
                                again, bytes) == mui_success &&
                   memcmp(pixels, again, bytes) == 0);
        }
        free(again);
        free(pixels);
    }
    muiFreeBuffer(&allocator, &scratch);
    return 0;
}
