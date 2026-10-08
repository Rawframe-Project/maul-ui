// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font keys (src/font_instance.c): a slot's generation is kept to the 24
// bits a key gives it, wrapping past them and never 0, so that a slot
// used 2^24 times does not reach into the instance's bits above. White
// box: generations that large are given rather than counted to.

#include "font_instance.h"
#include "test_harness.h"

#include <stdint.h>

int main(void)
{
    CHECK(muiKeyGeneration(5) == 5 && muiKeyGeneration(0) == 1, "small, and never 0");
    CHECK(muiKeyGeneration((1u << 24) + 5) == 5 && muiKeyGeneration(1u << 24) == 1,
          "wrapped past 24 bits");
    uint64_t key = muiKeyOf(3, (1u << 24) + 5);
    CHECK(key == ((uint64_t)5 << 16 | 2) && key >> 40 == 0, "no instance bits");
    return s_failures == 0 ? 0 : 1;
}
