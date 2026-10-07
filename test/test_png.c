// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PNG decoder (record mui-0006) on pngs.bin, written by
// make_pngs.py: every colour type and bit depth, plain and interlaced,
// every filter, stored, fixed and dynamic data over several chunks, and
// images Pillow encoded, each decoded to the RGBA it was made from; and
// damaged images refused. Then seeded fuzzing, run under the sanitizers
// with every test: the cases damaged at random, and half the time their
// CRCs made right again so that the damage reaches the image data; and
// their image data inflated, damaged and written again in stored blocks,
// its checksum and CRCs right, so that the damage reaches unfiltering and
// the samples; each is decoded or refused without harm. The seed is
// fixed, so a failure repeats.

#include "inflate.h"
#include "png.h"
#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

#include "pngs.inc"

enum
{
    MAX_CASES = 256,
    MAX_EXTENT = 4096,
    FUZZ_ROUNDS = 3000,
    RAW_ROUNDS = 1500
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
static uint8_t s_joined[1 << 16];
static uint8_t s_raw[1 << 20];
static uint8_t s_rebuilt[(1 << 20) + (1 << 16) + 1024];

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

static uint32_t Big32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void PutBig32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

// The bytes of a filtered image: a filter byte and the packed samples of
// each row, of each Adam7 pass when interlaced.
static size_t RawSize(uint32_t width, uint32_t height, uint32_t bitsPerPixel, bool interlaced)
{
    static const uint32_t x0[7] = {0, 4, 0, 2, 0, 1, 0};
    static const uint32_t y0[7] = {0, 0, 4, 0, 2, 0, 1};
    static const uint32_t dx[7] = {8, 8, 4, 4, 2, 2, 1};
    static const uint32_t dy[7] = {8, 8, 8, 4, 4, 2, 2};
    if (!interlaced)
    {
        return (size_t)height * (1 + ((size_t)width * bitsPerPixel + 7) / 8);
    }
    size_t size = 0;
    for (int pass = 0; pass < 7; pass++)
    {
        uint32_t w = width > x0[pass] ? (width - x0[pass] + dx[pass] - 1) / dx[pass] : 0;
        uint32_t h = height > y0[pass] ? (height - y0[pass] + dy[pass] - 1) / dy[pass] : 0;
        size += w != 0 && h != 0 ? (size_t)h * (1 + ((size_t)w * bitsPerPixel + 7) / 8) : 0;
    }
    return size;
}

static uint32_t SamplesPerPixel(uint8_t colorType)
{
    static const uint32_t samples[7] = {1, 0, 3, 1, 2, 0, 4};
    return colorType < 7 ? samples[colorType] : 0;
}

// Writes a chunk of a type and data with its CRC; its size.
static size_t PutChunk(uint8_t* out, const char type[4], const uint8_t* data, uint32_t length)
{
    PutBig32(out, length);
    memcpy(out + 4, type, 4);
    if (length != 0)
    {
        memcpy(out + 8, data, length);
    }
    PutBig32(out + 8 + length, Crc32(out + 4, 4 + (size_t)length));
    return 12 + (size_t)length;
}

// Writes data as a zlib stream of stored blocks; its size.
static size_t PutStored(uint8_t* out, const uint8_t* data, size_t size)
{
    size_t at = 2;
    out[0] = 0x78;
    out[1] = 0x01;
    size_t done = 0;
    uint32_t a = 1;
    uint32_t b = 0;
    do
    {
        size_t n = size - done < 65535 ? size - done : 65535;
        out[at] = done + n == size ? 1 : 0;
        out[at + 1] = (uint8_t)n;
        out[at + 2] = (uint8_t)(n >> 8);
        out[at + 3] = (uint8_t)~n;
        out[at + 4] = (uint8_t)(~n >> 8);
        memcpy(out + at + 5, data + done, n);
        at += 5 + n;
        done += n;
    } while (done < size);
    for (size_t i = 0; i < size; i++)
    {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    PutBig32(out + at, b << 16 | a);
    return at + 4;
}

// Joins a case's IDAT data into s_joined and copies its chunks before the
// first IDAT into s_rebuilt after the signature; the size copied, 0 for
// a case that is not a whole PNG or does not fit.
static size_t Split(const Case* c, size_t* joinedOut)
{
    size_t at = 8;
    size_t copied = 8;
    size_t joined = 0;
    memcpy(s_rebuilt, c->png, 8);
    while (c->size >= 12 && at <= c->size - 12)
    {
        uint32_t length = Big32(c->png + at);
        if (length > c->size - at - 12)
        {
            return 0;
        }
        const uint8_t* type = c->png + at + 4;
        if (memcmp(type, "IDAT", 4) == 0)
        {
            if (length > sizeof s_joined - joined)
            {
                return 0;
            }
            memcpy(s_joined + joined, type + 4, length);
            joined += length;
        }
        else if (memcmp(type, "IEND", 4) == 0)
        {
            *joinedOut = joined;
            return joined != 0 ? copied : 0;
        }
        else if (joined == 0)
        {
            memcpy(s_rebuilt + copied, c->png + at, 12 + (size_t)length);
            copied += 12 + (size_t)length;
        }
        at += 12 + (size_t)length;
    }
    return 0;
}

// A case rebuilt with its image data inflated, damaged and written again
// in stored blocks, one IDAT before IEND; its size, 0 for a case not
// whole or too large.
static size_t Rebuild(const Case* c, uint32_t* state)
{
    size_t joined = 0;
    size_t at = Split(c, &joined);
    // IHDR is first, and its depth, colour type and interlacing give the
    // filtered image's size.
    if (at < 8 + 12 + 13 || memcmp(s_rebuilt + 12, "IHDR", 4) != 0)
    {
        return 0;
    }
    const uint8_t* header = s_rebuilt + 16;
    uint32_t bits = header[8] * SamplesPerPixel(header[9]);
    size_t raw = RawSize(Big32(header), Big32(header + 4), bits, header[12] != 0);
    if (bits == 0 || raw == 0 || raw > sizeof s_raw ||
        muiInflateZlib(s_joined, joined, s_raw, raw) != mui_success)
    {
        return 0;
    }
    for (uint32_t i = 0, flips = 1 + Next(state) % 6; i < flips; i++)
    {
        size_t where = Next(state) % raw;
        // Now and then a filter type in or past the five.
        s_raw[where] = Next(state) % 3 == 0 ? (uint8_t)(Next(state) % 7)
                                            : (uint8_t)(s_raw[where] ^ 1u << (Next(state) % 8));
    }
    static uint8_t stored[(1 << 20) + 128];
    size_t storedSize = PutStored(stored, s_raw, raw);
    at += PutChunk(s_rebuilt + at, "IDAT", stored, (uint32_t)storedSize);
    at += PutChunk(s_rebuilt + at, "IEND", NULL, 0);
    return at;
}

static void TestDamagedData(void)
{
    uint32_t state = 0x85EBCA6Bu;
    uint32_t rebuilt = 0;
    uint32_t decoded = 0;
    uint32_t refused = 0;
    for (uint32_t round = 0; round < RAW_ROUNDS; round++)
    {
        size_t size = Rebuild(&s_cases[Next(&state) % s_count], &state);
        if (size == 0)
        {
            continue;
        }
        rebuilt++;
        uint32_t width = 0;
        uint32_t height = 0;
        muiResult result = Decode(s_rebuilt, size, &width, &height);
        decoded += result == mui_success ? 1 : 0;
        refused += result == mui_errorFormat ? 1 : 0;
    }
    CHECK(rebuilt > RAW_ROUNDS / 2 && decoded + refused == rebuilt && decoded != 0 && refused != 0,
          "damaged image data decoded or refused, some of each");
}

int main(void)
{
    CHECK(ReadCases(), "the cases");
    TestCases();
    TestContract();
    TestDamage();
    TestDamagedData();
    const muiAllocator allocator = {0};
    muiFreeBuffer(&allocator, &s_scratch);
    return s_failures == 0 ? 0 : 1;
}
