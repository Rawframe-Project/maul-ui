// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// COLR's composite modes on four pairs of premultiplied pixels: values
// of every channel, not below 0 or 1 alone; the second pair with
// channels of 0 and 1 where color dodge and color burn branch; the third
// with dark backdrops under bright sources where soft light branches;
// the fourth with channels falling from red to blue, which hue and
// saturation must sort.
// The expected results come from W3C Compositing and Blending Level 1's
// formulas, written apart from muiComposite, in double precision.

#include "colr_composite.h"
#include "test_harness.h"

#include <math.h>

static const float SOURCES[4][4] = {
    {0.2f, 0.4f, 0.6f, 0.8f},
    {1.0f, 0.0f, 0.4f, 1.0f},
    {0.54f, 0.18f, 0.42f, 0.6f},
    {0.63f, 0.35f, 0.07f, 0.7f},
};
static const float BACKDROPS[4][4] = {
    {0.3f, 0.15f, 0.45f, 0.5f},
    {0.5f, 0.0f, 1.0f, 1.0f},
    {0.09f, 0.18f, 0.045f, 0.9f},
    {0.48f, 0.24f, 0.12f, 0.6f},
};
// Each mode's result for each pair, from the W3C formulas.
static const float EXPECTED[28][4][4] = {
    {{0.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 0.0f, 0.0f}},
    {{0.2f, 0.4f, 0.6f, 0.8f},
     {1.0f, 0.0f, 0.4f, 1.0f},
     {0.54f, 0.18f, 0.42f, 0.6f},
     {0.63f, 0.35f, 0.07f, 0.7f}},
    {{0.3f, 0.15f, 0.45f, 0.5f},
     {0.5f, 0.0f, 1.0f, 1.0f},
     {0.09f, 0.18f, 0.045f, 0.9f},
     {0.48f, 0.24f, 0.12f, 0.6f}},
    {{0.26f, 0.43f, 0.69f, 0.9f},
     {1.0f, 0.0f, 0.4f, 1.0f},
     {0.576f, 0.252f, 0.438f, 0.96f},
     {0.774f, 0.422f, 0.106f, 0.88f}},
    {{0.4f, 0.35f, 0.75f, 0.9f},
     {0.5f, 0.0f, 1.0f, 1.0f},
     {0.144f, 0.198f, 0.087f, 0.96f},
     {0.732f, 0.38f, 0.148f, 0.88f}},
    {{0.1f, 0.2f, 0.3f, 0.4f},
     {1.0f, 0.0f, 0.4f, 1.0f},
     {0.486f, 0.162f, 0.378f, 0.54f},
     {0.378f, 0.21f, 0.042f, 0.42f}},
    {{0.24f, 0.12f, 0.36f, 0.4f},
     {0.5f, 0.0f, 1.0f, 1.0f},
     {0.054f, 0.108f, 0.027f, 0.54f},
     {0.336f, 0.168f, 0.084f, 0.42f}},
    {{0.1f, 0.2f, 0.3f, 0.4f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {0.054f, 0.018f, 0.042f, 0.06f},
     {0.252f, 0.14f, 0.028f, 0.28f}},
    {{0.06f, 0.03f, 0.09f, 0.1f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {0.036f, 0.072f, 0.018f, 0.36f},
     {0.144f, 0.072f, 0.036f, 0.18f}},
    {{0.16f, 0.23f, 0.39f, 0.5f},
     {1.0f, 0.0f, 0.4f, 1.0f},
     {0.522f, 0.234f, 0.396f, 0.9f},
     {0.522f, 0.282f, 0.078f, 0.6f}},
    {{0.34f, 0.32f, 0.66f, 0.8f},
     {0.5f, 0.0f, 1.0f, 1.0f},
     {0.108f, 0.126f, 0.069f, 0.6f},
     {0.588f, 0.308f, 0.112f, 0.7f}},
    {{0.16f, 0.23f, 0.39f, 0.5f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {0.09f, 0.09f, 0.06f, 0.42f},
     {0.396f, 0.212f, 0.064f, 0.46f}},
    {{0.5f, 0.55f, 1.0f, 1.0f},
     {1.0f, 0.0f, 1.0f, 1.0f},
     {0.63f, 0.36f, 0.465f, 1.0f},
     {1.0f, 0.59f, 0.19f, 1.0f}},
    {{0.44f, 0.49f, 0.78f, 0.9f},
     {1.0f, 0.0f, 1.0f, 1.0f},
     {0.5814f, 0.3276f, 0.4461f, 0.96f},
     {0.8076f, 0.506f, 0.1816f, 0.88f}},
    {{0.32f, 0.35f, 0.77f, 0.9f},
     {1.0f, 0.0f, 1.0f, 1.0f},
     {0.1872f, 0.1548f, 0.0978f, 0.96f},
     {0.7992f, 0.38f, 0.0808f, 0.88f}},
    {{0.26f, 0.35f, 0.69f, 0.9f},
     {0.5f, 0.0f, 0.4f, 1.0f},
     {0.144f, 0.198f, 0.087f, 0.96f},
     {0.732f, 0.38f, 0.106f, 0.88f}},
    {{0.4f, 0.43f, 0.75f, 0.9f},
     {1.0f, 0.0f, 1.0f, 1.0f},
     {0.576f, 0.252f, 0.438f, 0.96f},
     {0.774f, 0.422f, 0.148f, 0.88f}},
    {{0.48f, 0.47f, 0.79f, 0.9f},
     {1.0f, 0.0f, 1.0f, 1.0f},
     {0.63f, 0.244285714f, 0.15f, 0.96f},
     {0.816f, 0.548f, 0.157333333f, 0.88f}},
    {{0.16f, 0.23f, 0.736666667f, 0.9f},
     {0.5f, 0.0f, 1.0f, 1.0f},
     {0.09f, 0.09f, 0.06f, 0.96f},
     {0.722666667f, 0.212f, 0.064f, 0.88f}},
    {{0.28f, 0.35f, 0.77f, 0.9f},
     {1.0f, 0.0f, 0.8f, 1.0f},
     {0.5328f, 0.1548f, 0.2922f, 0.96f},
     {0.7992f, 0.38f, 0.0808f, 0.88f}},
    {{0.352f, 0.35f, 0.75973666f, 0.9f},
     {0.707106781f, 0.0f, 1.0f, 1.0f},
     {0.228672f, 0.16344f, 0.113352f, 0.96f},
     {0.763727536f, 0.38f, 0.09424f, 0.88f}},
    {{0.3f, 0.31f, 0.45f, 0.9f},
     {0.5f, 0.0f, 0.6f, 1.0f},
     {0.522f, 0.144f, 0.411f, 0.96f},
     {0.438f, 0.254f, 0.106f, 0.88f}},
    {{0.38f, 0.43f, 0.51f, 0.9f},
     {0.5f, 0.0f, 0.6f, 1.0f},
     {0.5328f, 0.2952f, 0.4272f, 0.96f},
     {0.5052f, 0.422f, 0.1732f, 0.88f}},
    {{0.22f, 0.29f, 0.66f, 0.9f},
     {0.5f, 0.0f, 0.4f, 1.0f},
     {0.1386f, 0.1224f, 0.0789f, 0.96f},
     {0.6984f, 0.296f, 0.0724f, 0.88f}},
    {{0.2452f, 0.4352f, 0.7152f, 0.9f},
     {0.755813953f, 0.0f, 0.302325581f, 1.0f},
     {0.22365f, 0.14265f, 0.16665f, 0.96f},
     {0.70722f, 0.39722f, 0.12322f, 0.88f}},
    {{0.3904f, 0.3604f, 0.7204f, 0.9f},
     {0.5f, 0.0f, 1.0f, 1.0f},
     {0.130043478f, 0.210130435f, 0.06f, 0.96f},
     {0.77428f, 0.36628f, 0.10628f, 0.88f}},
    {{0.2614f, 0.4314f, 0.6914f, 0.9f},
     {0.755813953f, 0.0f, 0.302325581f, 1.0f},
     {0.312026786f, 0.09f, 0.208017857f, 0.96f},
     {0.74124f, 0.38924f, 0.07324f, 0.88f}},
    {{0.3986f, 0.3486f, 0.7486f, 0.9f},
     {0.556756757f, 0.113513514f, 1.0f, 1.0f},
     {0.34407f, 0.39807f, 0.28707f, 0.96f},
     {0.76476f, 0.41276f, 0.18076f, 0.88f}},
};

static const char* const NAMES[28] = {
    "clear",      "src",       "dest",     "src over",    "dest over",  "src in",     "dest in",
    "src out",    "dest out",  "src atop", "dest atop",   "xor",        "plus",       "screen",
    "overlay",    "darken",    "lighten",  "color dodge", "color burn", "hard light", "soft light",
    "difference", "exclusion", "multiply", "hue",         "saturation", "color",      "luminosity"};

int main(void)
{
    for (uint32_t mode = 0; mode < 28; mode++)
    {
        float backdrop[4][4];
        for (int pair = 0; pair < 4; pair++)
        {
            for (int i = 0; i < 4; i++)
            {
                backdrop[pair][i] = BACKDROPS[pair][i];
            }
        }
        muiComposite(mode, &SOURCES[0][0], &backdrop[0][0], 4);
        bool near = true;
        for (int pair = 0; pair < 4; pair++)
        {
            for (int i = 0; i < 4; i++)
            {
                near = near && fabsf(backdrop[pair][i] - EXPECTED[mode][pair][i]) <= 1e-5f;
            }
        }
        CHECK(near, NAMES[mode]);
    }
    // A mode past COLR's is source over.
    float backdrop[4] = {BACKDROPS[0][0], BACKDROPS[0][1], BACKDROPS[0][2], BACKDROPS[0][3]};
    muiComposite(28, SOURCES[0], backdrop, 1);
    CHECK(fabsf(backdrop[3] - EXPECTED[3][0][3]) <= 1e-5f &&
              fabsf(backdrop[0] - EXPECTED[3][0][0]) <= 1e-5f,
          "a mode past COLR's, source over");
    return s_failures == 0 ? 0 : 1;
}
