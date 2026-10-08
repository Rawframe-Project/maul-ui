// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Images grown (src/image_scale.c): each output pixel the source sampled
// bilinearly at its centre, between the four source pixels around it
// (found by a mutant blending the lower two the wrong way). White box:
// bitmap glyphs grown through it are compared only at whole pixels.

#include "image_scale.h"
#include "test_harness.h"

#include <math.h>

// Output pixel x, y's red, of an output four pixels wide.
static float RedAt(const float* out, int x, int y)
{
    return out[((size_t)y * 4 + (size_t)x) * 4];
}

int main(void)
{
    // Two by two, opaque, red 0 and 1 across the top and 2 and 3 across
    // the bottom.
    const float pixels[16] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                              2.0f, 0.0f, 0.0f, 1.0f, 3.0f, 0.0f, 0.0f, 1.0f};
    const muiScaleSource source = {pixels, 2, 2};
    float out[4 * 4 * 4];
    muiScaleImage(&source, 2.0, 0.0, 0.0, out, 4, 4);
    // Output pixel 1, 1's centre is the source's 0.75, 0.75, a quarter of
    // the way from the first pixel's centre to the last's each way; 2, 2
    // three quarters.
    CHECK(fabsf(RedAt(out, 1, 1) - 0.75f) < 1e-5f && out[(1 * 4 + 1) * 4 + 3] == 1.0f,
          "a quarter each way");
    CHECK(fabsf(RedAt(out, 2, 1) - 1.25f) < 1e-5f && fabsf(RedAt(out, 1, 2) - 1.75f) < 1e-5f,
          "three quarters across, or down");
    CHECK(fabsf(RedAt(out, 2, 2) - 2.25f) < 1e-5f, "three quarters each way");
    return s_failures == 0 ? 0 : 1;
}
