// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The inflater on streams written bit by bit here, each breaking one of
// RFC 1950's or RFC 1951's rules where the stream is otherwise sound, its
// checksum right: each refused, and none reads or writes past its buffers,
// which are allocated to their exact sizes for the sanitizers to watch.

#include "inflate.h"
#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

typedef struct Writer
{
    uint8_t bytes[512];
    size_t size;
    uint32_t bit;
} Writer;

// Bits least significant first.
static void Put(Writer* w, uint32_t value, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (w->bit == 0)
        {
            w->bytes[w->size++] = 0;
        }
        w->bytes[w->size - 1] |= (uint8_t)(((value >> i) & 1u) << w->bit);
        w->bit = (w->bit + 1) % 8;
    }
}

// A Huffman code, most significant bit first.
static void Code(Writer* w, uint32_t code, uint32_t length)
{
    for (uint32_t i = length; i-- > 0;)
    {
        Put(w, code >> i, 1);
    }
}

static void Header(Writer* w, uint8_t flags)
{
    *w = (Writer){0};
    w->bytes[0] = 0x78;
    w->bytes[1] = flags;
    w->size = 2;
}

// The fixed code's literals and lengths.
static void Fixed(Writer* w, uint32_t symbol)
{
    if (symbol < 144)
    {
        Code(w, 0x30 + symbol, 8);
    }
    else if (symbol < 256)
    {
        Code(w, 0x190 + symbol - 144, 9);
    }
    else if (symbol < 280)
    {
        Code(w, symbol - 256, 7);
    }
    else
    {
        Code(w, 0xC0 + symbol - 280, 8);
    }
}

static uint32_t Adler(const uint8_t* data, size_t size)
{
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < size; i++)
    {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return b << 16 | a;
}

// To the next byte, then the checksum of what the stream should give.
static void Trailer(Writer* w, const char* text)
{
    w->bit = 0;
    uint32_t adler = Adler((const uint8_t*)text, strlen(text));
    for (int i = 3; i >= 0; i--)
    {
        w->bytes[w->size++] = (uint8_t)(adler >> (i * 8));
    }
}

// Inflates into exactly outSize bytes, both buffers allocated to size.
static muiResult Inflate(const Writer* w, size_t outSize, char* textOut)
{
    uint8_t* in = malloc(w->size);
    uint8_t* out = malloc(outSize > 0 ? outSize : 1);
    memcpy(in, w->bytes, w->size);
    muiResult result = muiInflateZlib(in, w->size, out, outSize);
    if (textOut != NULL && result == mui_success)
    {
        memcpy(textOut, out, outSize);
        textOut[outSize] = '\0';
    }
    free(out);
    free(in);
    return result;
}

static void TestFixed(void)
{
    Writer w;
    Header(&w, 0x01);
    Put(&w, 1, 1);
    Put(&w, 1, 2);
    Fixed(&w, 'a');
    Fixed(&w, 'b');
    Fixed(&w, 'c');
    Fixed(&w, 256);
    Trailer(&w, "abc");
    char text[8] = {0};
    CHECK(Inflate(&w, 3, text) == mui_success && strcmp(text, "abc") == 0, "a fixed block");
    CHECK(Inflate(&w, 2, NULL) == mui_errorFormat, "literals past the output");
    w.bytes[1] = 0x02;
    CHECK(Inflate(&w, 3, NULL) == mui_errorFormat, "a header whose check is wrong");
    // A match reaching back past the start: 'a', then 3 bytes 2 back.
    Header(&w, 0x01);
    Put(&w, 1, 1);
    Put(&w, 1, 2);
    Fixed(&w, 'a');
    Fixed(&w, 257);
    Code(&w, 1, 5);
    Fixed(&w, 256);
    Trailer(&w, "aaaa");
    CHECK(Inflate(&w, 4, NULL) == mui_errorFormat, "a distance past the start");
}

static void TestStored(void)
{
    Writer w;
    Header(&w, 0x01);
    Put(&w, 1, 1);
    Put(&w, 0, 2);
    w.bit = 0;
    const uint8_t block[7] = {3, 0, 0xFC, 0xFF, 'a', 'b', 'c'};
    memcpy(w.bytes + w.size, block, sizeof block);
    w.size += sizeof block;
    Trailer(&w, "abc");
    char text[8] = {0};
    CHECK(Inflate(&w, 3, text) == mui_success && strcmp(text, "abc") == 0, "a stored block");
    w.bytes[5] ^= 1;
    CHECK(Inflate(&w, 3, NULL) == mui_errorFormat, "a stored length its complement denies");
    // A length past the input: ten bytes said, three given.
    Header(&w, 0x01);
    Put(&w, 1, 1);
    Put(&w, 0, 2);
    w.bit = 0;
    const uint8_t longer[7] = {10, 0, 0xF5, 0xFF, 'a', 'b', 'c'};
    memcpy(w.bytes + w.size, longer, sizeof longer);
    w.size += sizeof longer;
    CHECK(Inflate(&w, 10, NULL) == mui_errorFormat, "a stored block past the input");
}

// A dynamic block's header: literal and distance counts, then the code
// length code's lengths in the format's order, count of them.
static void Dynamic(Writer* w, uint32_t literals, uint32_t distances, const uint8_t* lengths,
                    uint32_t count)
{
    Header(w, 0x01);
    Put(w, 1, 1);
    Put(w, 2, 2);
    Put(w, literals - 257, 5);
    Put(w, distances - 1, 5);
    Put(w, count - 4, 4);
    for (uint32_t i = 0; i < count; i++)
    {
        Put(w, lengths[i], 3);
    }
}

static void TestDynamic(void)
{
    Writer w;
    // Code length symbols 16 and 1, one bit each (in the format's order,
    // 16 is first and 1 is 18th): 1 is code 0, 16 is code 1.
    uint8_t first[18] = {1};
    first[17] = 1;
    Dynamic(&w, 257, 1, first, 18);
    Code(&w, 1, 1);
    Put(&w, 0, 2);
    CHECK(Inflate(&w, 1, NULL) == mui_errorFormat, "a repeat with nothing before it");
    // Only symbol 18, one bit: runs of zeros past the lengths there are,
    // 138 + 138 + 34 + 138 of 316.
    uint8_t zeros[4] = {0, 0, 1, 0};
    Dynamic(&w, 286, 30, zeros, 4);
    const uint32_t runs[4] = {127, 127, 23, 127};
    for (int i = 0; i < 4; i++)
    {
        Code(&w, 0, 1);
        Put(&w, runs[i], 7);
    }
    CHECK(Inflate(&w, 1, NULL) == mui_errorFormat, "a repeat past the lengths");
    // Code length symbols 0, 1 and 18 all of one bit: three codes where
    // two fit. Read as if they did, bit 0 is 18 and bit 1 is 1, and the
    // rest would make "a" whole.
    uint8_t over[18] = {0, 0, 1, 1};
    over[17] = 1;
    Dynamic(&w, 257, 1, over, 18);
    const uint32_t lengths[][2] = {{0, 86}, {1, 0}, {0, 127}, {0, 9}, {1, 0}, {1, 0}};
    for (int i = 0; i < 6; i++)
    {
        Code(&w, lengths[i][0], 1);
        if (lengths[i][0] == 0)
        {
            Put(&w, lengths[i][1], 7);
        }
    }
    Code(&w, 0, 1);
    Code(&w, 1, 1);
    Trailer(&w, "a");
    CHECK(Inflate(&w, 1, NULL) == mui_errorFormat, "a code with more codes than fit");
}

int main(void)
{
    TestFixed();
    TestStored();
    TestDynamic();
    return s_failures == 0 ? 0 : 1;
}
