// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PNG decoder (record mui-0006) on pngs.bin, written by
// make_pngs.py: every colour type and bit depth, plain and interlaced,
// every filter, stored, fixed and dynamic data over several chunks, and
// images Pillow encoded, each decoded to the RGBA it was made from; and
// damaged images refused. Then seeded fuzzing, run under the sanitizers
// with every test: the cases damaged at random, and half the time their
// CRCs made right again so that the damage reaches the image data; each
// is decoded or refused without harm. The seed is fixed, so a failure
// repeats.

#include "png.h"
#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

#include "pngs.inc"

enum
{
    MAX_CASES = 256,
    MAX_EXTENT = 4096,
    FUZZ_ROUNDS = 3000
};

typedef struct Case
{
    const uint8_t* png;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    const uint8_t* rgba;
} Case;

static Case s_cases[MAX_CASES];
static uint32_t s_count;
static uint8_t s_pixels[1 << 20];
static uint8_t s_damaged[1 << 16];
static muiBuffer s_scratch;

static uint32_t Little32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool ReadCases(void)
{
    const uint8_t* p = s_pngs;
    s_count = Little32(p);
    p += 4;
    if (s_count > MAX_CASES)
    {
        return false;
    }
    for (uint32_t i = 0; i < s_count; i++)
    {
        Case* c = &s_cases[i];
        c->size = Little32(p);
        c->png = p + 4;
        p += 4 + c->size;
        c->width = Little32(p);
        c->height = Little32(p + 4);
        c->rgba = p + 8;
        p += 8 + (size_t)c->width * c->height * 4;
    }
    return p == s_pngs + sizeof s_pngs;
}

static muiResult Decode(const uint8_t* png, size_t size, uint32_t* width, uint32_t* height)
{
    const muiAllocator allocator = {0};
    return muiDecodePng(&allocator, &s_scratch, png, size, MAX_EXTENT, width, height, s_pixels,
                        sizeof s_pixels);
}

static void TestCases(void)
{
    uint32_t good = 0;
    uint32_t refused = 0;
    for (uint32_t i = 0; i < s_count; i++)
    {
        const Case* c = &s_cases[i];
        uint32_t width = 0;
        uint32_t height = 0;
        muiResult result = Decode(c->png, c->size, &width, &height);
        if (c->width == 0)
        {
            refused += result == mui_errorFormat ? 1 : 0;
            continue;
        }
        good += result == mui_success && width == c->width && height == c->height &&
                        memcmp(s_pixels, c->rgba, (size_t)width * height * 4) == 0
                    ? 1
                    : 0;
    }
    uint32_t bad = 0;
    for (uint32_t i = 0; i < s_count; i++)
    {
        bad += s_cases[i].width == 0 ? 1 : 0;
    }
    CHECK(good == s_count - bad, "every good image decoded to its pixels");
    CHECK(refused == bad, "every damaged image refused");
}

static void TestContract(void)
{
    const Case* c = &s_cases[3];
    const muiAllocator allocator = {0};
    muiBuffer scratch = {0};
    uint32_t width = 0;
    uint32_t height = 0;
    CHECK(muiDecodePng(&allocator, &scratch, c->png, c->size, MAX_EXTENT, &width, &height, s_pixels,
                       (size_t)c->width * c->height * 4 - 1) == mui_errorCapacity &&
              width == c->width && height == c->height,
          "too few bytes, the size told");
    CHECK(muiDecodePng(&allocator, &scratch, c->png, c->size, MAX_EXTENT, &width, &height, NULL,
                       0) == mui_errorCapacity &&
              width == c->width,
          "no pixels, the size told");
    uint32_t largest = c->width > c->height ? c->width : c->height;
    CHECK(muiDecodePng(&allocator, &scratch, c->png, c->size, largest - 1, &width, &height,
                       s_pixels, sizeof s_pixels) == mui_errorFormat &&
              muiDecodePng(&allocator, &scratch, c->png, c->size, largest, &width, &height,
                           s_pixels, sizeof s_pixels) == mui_success,
          "a side past the largest asked for");
    CHECK(muiDecodePng(&allocator, &scratch, NULL, 0, MAX_EXTENT, &width, &height, s_pixels,
                       sizeof s_pixels) == mui_errorFormat &&
              width == 0,
          "nothing");
    muiFreeBuffer(&allocator, &scratch);
}

static uint32_t Next(uint32_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static uint32_t Crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
        {
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

// Writes each whole chunk's CRC as its bytes now are.
static void Recrc(uint8_t* png, size_t size)
{
    size_t at = 8;
    while (at <= size && size - at >= 12)
    {
        uint32_t length = (uint32_t)png[at] << 24 | (uint32_t)png[at + 1] << 16 |
                          (uint32_t)png[at + 2] << 8 | png[at + 3];
        if (length > size - at - 12)
        {
            return;
        }
        uint32_t crc = Crc32(png + at + 4, 4 + (size_t)length);
        uint8_t* end = png + at + 8 + length;
        end[0] = (uint8_t)(crc >> 24);
        end[1] = (uint8_t)(crc >> 16);
        end[2] = (uint8_t)(crc >> 8);
        end[3] = (uint8_t)crc;
        at += 12 + (size_t)length;
    }
}

static void TestDamage(void)
{
    uint32_t state = 0x9E3779B9u;
    uint32_t decoded = 0;
    uint32_t refused = 0;
    for (uint32_t round = 0; round < FUZZ_ROUNDS; round++)
    {
        const Case* c = &s_cases[Next(&state) % s_count];
        size_t size = c->size < sizeof s_damaged ? c->size : sizeof s_damaged;
        memcpy(s_damaged, c->png, size);
        uint32_t flips = 1 + Next(&state) % 6;
        for (uint32_t i = 0; i < flips; i++)
        {
            // Past the signature most of the time.
            size_t at = 8 + Next(&state) % (size > 8 ? size - 8 : 1);
            s_damaged[at < size ? at : 0] ^= (uint8_t)(1u << (Next(&state) % 8));
        }
        if (Next(&state) % 4 == 0)
        {
            size = Next(&state) % (size + 1);
        }
        if (Next(&state) % 2 == 0)
        {
            Recrc(s_damaged, size);
        }
        uint32_t width = 0;
        uint32_t height = 0;
        muiResult result = Decode(s_damaged, size, &width, &height);
        decoded += result == mui_success ? 1 : 0;
        refused += result == mui_errorFormat ? 1 : 0;
    }
    CHECK(decoded + refused == FUZZ_ROUNDS && refused > FUZZ_ROUNDS / 2,
          "damaged images decoded or refused, most refused");
}

int main(void)
{
    CHECK(ReadCases(), "the cases");
    TestCases();
    TestContract();
    TestDamage();
    const muiAllocator allocator = {0};
    muiFreeBuffer(&allocator, &s_scratch);
    return s_failures == 0 ? 0 : 1;
}
