// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes zlib streams (src/inflate.c), the image data of the PNGs in
// colour bitmap fonts. The input's first two bytes give the output's
// size, up to 64 KiB, the rest is the stream. Inflating gives success or
// a format error, never touching memory past the input or the output,
// which is exactly the size given; and a stream inflated once inflates
// the same again.

#include "inflate.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 2)
    {
        return 0;
    }
    size_t outSize = (size_t)data[0] << 8 | data[1];
    // One byte at least, so that malloc gives memory to watch.
    uint8_t* out = malloc(outSize != 0 ? outSize : 1);
    uint8_t* again = malloc(outSize != 0 ? outSize : 1);
    Expect(out != nullptr && again != nullptr);
    muiResult result = muiInflateZlib(data + 2, size - 2, out, outSize);
    Expect(result == mui_success || result == mui_errorFormat);
    if (result == mui_success)
    {
        Expect(muiInflateZlib(data + 2, size - 2, again, outSize) == mui_success &&
               memcmp(out, again, outSize) == 0);
    }
    free(again);
    free(out);
    return 0;
}
