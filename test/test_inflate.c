// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The inflater on streams written bit by bit here, each breaking one of
// RFC 1950's or RFC 1951's rules where the stream is otherwise sound, its
// checksum right: each refused, and none reads or writes past its buffers,
// which are allocated to their exact sizes for the sanitizers to watch.
// The longer streams break a rule where the fast loop runs (eight bytes
// of input left) and again where the checked loop ends the block.

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
static void TrailerBytes(Writer* w, const uint8_t* data, size_t size)
{
    w->bit = 0;
    uint32_t adler = Adler(data, size);
    for (int i = 3; i >= 0; i--)
    {
        w->bytes[w->size++] = (uint8_t)(adler >> (i * 8));
    }
}

static void Trailer(Writer* w, const char* text)
{
    TrailerBytes(w, (const uint8_t*)text, strlen(text));
}

// Inflates into exactly outSize bytes, both buffers allocated to size.
static muiResult InflateBytes(const uint8_t* bytes, size_t size, size_t outSize, char* textOut)
{
    uint8_t* in = malloc(size > 0 ? size : 1);
    uint8_t* out = malloc(outSize > 0 ? outSize : 1);
    memcpy(in, bytes, size);
    muiResult result = muiInflateZlib(in, size, out, outSize);
    if (textOut != NULL && result == mui_success)
    {
        memcpy(textOut, out, outSize);
        textOut[outSize] = '\0';
    }
    free(out);
    free(in);
    return result;
}

static muiResult Inflate(const Writer* w, size_t outSize, char* textOut)
{
    return InflateBytes(w->bytes, w->size, outSize, textOut);
}

// A fixed block's header.
static void FixedBlock(Writer* w)
{
    Header(w, 0x01);
    Put(w, 1, 1);
    Put(w, 1, 2);
}

// Count letters a to z over and over, one fixed literal each.
static void Letters(Writer* w, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        Fixed(w, 'a' + i % 26);
    }
}

// Every shorter prefix of a stream is refused.
static bool PrefixesRefused(const uint8_t* bytes, size_t size, size_t outSize)
{
    for (size_t cut = 0; cut < size; cut++)
    {
        if (InflateBytes(bytes, cut, outSize, NULL) != mui_errorFormat)
        {
            return false;
        }
    }
    return true;
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

static void TestFast(void)
{
    // 64 letters, then 10 bytes 26 back (length symbol 264; distance
    // code 9, 25 and 3 extra bits of 1): the fast loop takes the letters,
    // the checked loop the match and the end.
    char expected[80] = {0};
    for (int i = 0; i < 74; i++)
    {
        expected[i] = (char)('a' + i % 26);
    }
    Writer w;
    FixedBlock(&w);
    Letters(&w, 64);
    Fixed(&w, 264);
    Code(&w, 9, 5);
    Put(&w, 1, 3);
    Fixed(&w, 256);
    Trailer(&w, expected);
    char text[80] = {0};
    CHECK(Inflate(&w, 74, text) == mui_success && strcmp(text, expected) == 0,
          "letters through the fast loop, a match through the checked one");
    CHECK(Inflate(&w, 10, NULL) == mui_errorFormat, "a literal past the output, in the fast loop");
    CHECK(Inflate(&w, 73, NULL) == mui_errorFormat, "a match past the output, checked loop");
    CHECK(Inflate(&w, 75, NULL) == mui_errorFormat, "a stream short of its output");
    CHECK(PrefixesRefused(w.bytes, w.size, 74), "every cut of it, in either loop");
}

static void TestFastDistances(void)
{
    // A match before anything, 40 letters after: the fast loop meets it.
    Writer w;
    FixedBlock(&w);
    Fixed(&w, 257);
    Code(&w, 0, 5);
    Letters(&w, 40);
    Fixed(&w, 256);
    Trailer(&w, "aaa");
    CHECK(Inflate(&w, 43, NULL) == mui_errorFormat, "a distance past the start, in the fast loop");
    // 40 letters, then 3 bytes 41 back (distance code 10, 33 and 4
    // extra bits of 8): the checked loop meets it.
    FixedBlock(&w);
    Letters(&w, 40);
    Fixed(&w, 257);
    Code(&w, 10, 5);
    Put(&w, 8, 4);
    Fixed(&w, 256);
    Trailer(&w, "a");
    CHECK(Inflate(&w, 43, NULL) == mui_errorFormat,
          "a distance past the start, in the checked loop");
}

static void TestFastMeaningless(void)
{

    // One letter, then 258 bytes 1 back (symbol 285), then 40 letters,
    // into 100: the fast loop's match past the output.
    Writer w;
    FixedBlock(&w);
    Letters(&w, 1);
    Fixed(&w, 285);
    Code(&w, 0, 5);
    Letters(&w, 40);
    Fixed(&w, 256);
    Trailer(&w, "a");
    CHECK(Inflate(&w, 100, NULL) == mui_errorFormat, "a match past the output, in the fast loop");

    // Length symbol 286 and distance code 30 have no meaning, in either
    // loop.
    FixedBlock(&w);
    Fixed(&w, 286);
    Letters(&w, 40);
    Fixed(&w, 256);
    Trailer(&w, "a");
    CHECK(Inflate(&w, 41, NULL) == mui_errorFormat, "length symbol 286, in the fast loop");
    FixedBlock(&w);
    Letters(&w, 40);
    Fixed(&w, 286);
    Fixed(&w, 256);
    Trailer(&w, "a");
    CHECK(Inflate(&w, 41, NULL) == mui_errorFormat, "length symbol 286, in the checked loop");
    FixedBlock(&w);
    Letters(&w, 1);
    Fixed(&w, 257);
    Code(&w, 30, 5);
    Letters(&w, 40);
    Fixed(&w, 256);
    Trailer(&w, "a");
    CHECK(Inflate(&w, 44, NULL) == mui_errorFormat, "distance code 30, in the fast loop");
    FixedBlock(&w);
    Letters(&w, 40);
    Fixed(&w, 257);
    Code(&w, 30, 5);
    Fixed(&w, 256);
    Trailer(&w, "a");
    CHECK(Inflate(&w, 43, NULL) == mui_errorFormat, "distance code 30, in the checked loop");
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

// A dynamic block whose longest symbol takes the most bits a symbol can,
// 48: length symbol 284 and distance code 29, both 15-bit codes, with 5
// and 13 extra bits. 'a' takes 1 bit, 285 (258 bytes) 2, the end 3,
// distance code 0 1. Lengths go by the code length code's symbols 0, 1,
// 2, 3, 15 and 18, 3 bits each (codes 0 to 5 in that order).
static void LongestHeader(Writer* w)
{
    // In the format's order 18, 0, 3, 2, 1 and 15 are 2nd, 3rd, 13th,
    // 15th, 17th and 18th (from 0).
    uint8_t lengths[19] = {0};
    lengths[2] = lengths[3] = lengths[13] = lengths[15] = lengths[17] = lengths[18] = 3;
    Dynamic(w, 286, 30, lengths, 19);
    // Runs of zeros by 18 (11 + 7 extra bits), lengths by their codes.
    const uint32_t steps[][2] = {{5, 86}, {1, 0}, {5, 127}, {5, 9},  {3, 0}, {5, 16},
                                 {4, 0},  {2, 0}, {1, 0},   {5, 17}, {4, 0}};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++)
    {
        Code(w, steps[i][0], 3);
        if (steps[i][0] == 5)
        {
            Put(w, steps[i][1], 7);
        }
    }
}

static void TestLongestSymbol(void)
{
    // 'a', 96 matches of 258 one back, k more letters, the 48-bit match
    // (227 bytes 24,577 back), then tail letters and the end: every k
    // starts the long match at another bit of the buffer, so one of them
    // starts it with fewer than 48 left had the refill come later; with
    // no tail the checked loop decodes it near the input's end.
    static uint8_t expected[25200];
    memset(expected, 'a', sizeof expected);
    bool all = true;
    for (uint32_t tail = 0; tail <= 64; tail += 64)
    {
        for (uint32_t k = 0; k < 64; k++)
        {
            Writer w;
            LongestHeader(&w);
            Code(&w, 0, 1);
            for (int i = 0; i < 96; i++)
            {
                Code(&w, 2, 2);
                Code(&w, 0, 1);
            }
            for (uint32_t i = 0; i < k; i++)
            {
                Code(&w, 0, 1);
            }
            Code(&w, 7u << 12, 15);
            Put(&w, 0, 5);
            Code(&w, 1u << 14, 15);
            Put(&w, 0, 13);
            for (uint32_t i = 0; i < tail; i++)
            {
                Code(&w, 0, 1);
            }
            Code(&w, 6, 3);
            size_t size = 1 + 96 * 258 + k + 227 + tail;
            TrailerBytes(&w, expected, size);
            all = all && Inflate(&w, size, NULL) == mui_success;
        }
    }
    CHECK(all, "a 48-bit symbol at every bit of the buffer, in either loop");
}

// A fixed block ended by the fast loop, a stored block longer than the
// bytes the buffer holds, then a fixed block: the bits the fast loop
// loaded past the first block's end must not reach the third.
static void TestStoredAfterFast(void)
{
    char expected[80] = {0};
    for (int i = 0; i < 40; i++)
    {
        expected[i] = (char)('a' + i % 26);
    }
    memcpy(expected + 40, "~~~~~~~~~~~~~~~~~~~~xy", 22);
    Writer w;
    Header(&w, 0x01);
    Put(&w, 0, 1);
    Put(&w, 1, 2);
    Letters(&w, 40);
    Fixed(&w, 256);
    Put(&w, 0, 1);
    Put(&w, 0, 2);
    w.bit = 0;
    const uint8_t block[4] = {20, 0, 0xEB, 0xFF};
    memcpy(w.bytes + w.size, block, sizeof block);
    memcpy(w.bytes + w.size + 4, "~~~~~~~~~~~~~~~~~~~~", 20);
    w.size += 24;
    Put(&w, 1, 1);
    Put(&w, 1, 2);
    Fixed(&w, 'x');
    Fixed(&w, 'y');
    Fixed(&w, 256);
    Trailer(&w, expected);
    char text[80] = {0};
    CHECK(Inflate(&w, 62, text) == mui_success && strcmp(text, expected) == 0,
          "a stored block after one the fast loop ended");
}

// zlib's level 9 on 1,500 bytes of 29 letters, the next of each 0.8
// times as likely: a dynamic block whose rare letters have codes longer
// than the fast table's 9 bits, met in both loops.
static const uint8_t LONG_CODES[] = {
    0x78, 0xDA, 0x1D, 0x94, 0xDB, 0x6E, 0xE3, 0x48, 0x0C, 0x44, 0xBF, 0x85, 0xF7, 0xBE, 0x48, 0x96,
    0x9C, 0x71, 0x62, 0x0C, 0xB0, 0x8B, 0x79, 0x99, 0xB7, 0xFD, 0xFF, 0xFF, 0xD9, 0x43, 0x07, 0x89,
    0x03, 0x4B, 0xEC, 0x62, 0xB1, 0xAA, 0xD8, 0x0F, 0x37, 0x8D, 0x97, 0xAD, 0x94, 0x94, 0xD3, 0x45,
    0x5C, 0x8E, 0xD4, 0x67, 0x49, 0x59, 0xCA, 0x12, 0x7E, 0x96, 0x8A, 0x94, 0x64, 0x89, 0x85, 0xA8,
    0xA4, 0xA9, 0x4C, 0xD7, 0xDF, 0x53, 0xC5, 0x64, 0xF2, 0xC4, 0xE4, 0xD6, 0x21, 0x36, 0x54, 0xD5,
    0xDF, 0x6A, 0xAE, 0x87, 0x4C, 0xB1, 0x1F, 0x8A, 0x0D, 0x34, 0x1D, 0xAF, 0xDF, 0x5D, 0xE9, 0xA1,
    0x6A, 0xCB, 0xA8, 0x29, 0x4D, 0x1E, 0xCB, 0xCA, 0x69, 0xB2, 0xA6, 0xF3, 0xC5, 0x83, 0x2E, 0x39,
    0x9F, 0xEA, 0x73, 0x6B, 0xA4, 0x77, 0x1B, 0xDB, 0x00, 0x87, 0x19, 0xBF, 0x43, 0x5D, 0x38, 0x7D,
    0x89, 0x1C, 0x36, 0x21, 0x6B, 0xD2, 0x5F, 0xFD, 0x4F, 0x48, 0xAA, 0xE6, 0x30, 0xD0, 0xC1, 0x0D,
    0xC8, 0xFC, 0x55, 0x81, 0x05, 0x3C, 0x95, 0x43, 0xC5, 0x58, 0xC3, 0xD6, 0x1D, 0xCB, 0x66, 0x86,
    0x1F, 0x53, 0xC1, 0x72, 0xD5, 0xE1, 0x47, 0xF7, 0x17, 0xBA, 0x5C, 0xBA, 0xA8, 0xE1, 0x9F, 0xDB,
    0x9A, 0x31, 0xCD, 0x5E, 0x65, 0xCF, 0xE4, 0x54, 0x9E, 0x16, 0x41, 0x7F, 0xBD, 0x9B, 0x8A, 0x4F,
    0xCE, 0xE5, 0x41, 0x55, 0xA9, 0x05, 0x38, 0x3B, 0xFD, 0xB2, 0x19, 0xF4, 0xDC, 0xBA, 0x52, 0x23,
    0x2E, 0x3A, 0x8A, 0x3B, 0x0A, 0x02, 0x6C, 0xA2, 0xB5, 0x5D, 0x1E, 0x1B, 0x7E, 0x32, 0x54, 0x82,
    0x81, 0x98, 0x30, 0xFC, 0x4B, 0x5B, 0x5D, 0x9F, 0x01, 0xFD, 0x41, 0x9D, 0xAB, 0xFB, 0x4D, 0x01,
    0x23, 0x51, 0x2B, 0x19, 0x7B, 0x23, 0x1A, 0xB2, 0x4B, 0x83, 0x6C, 0x6D, 0xDD, 0x23, 0x1F, 0x5F,
    0x11, 0xA3, 0x00, 0xFA, 0xCC, 0xC0, 0x9B, 0xB0, 0x31, 0xC4, 0xAD, 0xA7, 0x16, 0xD9, 0x73, 0xAE,
    0x0F, 0x87, 0x26, 0xAA, 0x59, 0xF3, 0x6C, 0xDA, 0x95, 0xE7, 0xA5, 0xB2, 0xE3, 0x1C, 0x5E, 0x18,
    0x66, 0x15, 0x76, 0x9B, 0x8F, 0xA7, 0x0F, 0x2B, 0xA7, 0x31, 0x0C, 0x64, 0x15, 0xF6, 0x7E, 0x0C,
    0xD6, 0x8C, 0xCB, 0x6E, 0xD4, 0x79, 0xD0, 0x32, 0xBD, 0x07, 0x2A, 0x50, 0x5A, 0xC7, 0x78, 0x2C,
    0xF1, 0x92, 0x73, 0x60, 0x8C, 0xDB, 0xF6, 0x06, 0xFF, 0xD2, 0x75, 0x79, 0xAD, 0xE0, 0x41, 0xBA,
    0xEC, 0xC6, 0x4B, 0x85, 0x8B, 0x2B, 0xB3, 0x6C, 0x85, 0xC0, 0x53, 0x50, 0xAF, 0x70, 0xC0, 0xEE,
    0x6A, 0xA2, 0x4C, 0x93, 0xF1, 0xB6, 0xD3, 0x61, 0x6A, 0x7E, 0x9C, 0x88, 0x11, 0xD9, 0xC1, 0x3A,
    0xF0, 0x08, 0x69, 0xF9, 0x48, 0xA4, 0x3D, 0x99, 0x27, 0x19, 0x1F, 0xB0, 0x7F, 0x80, 0xBB, 0x54,
    0xCF, 0x85, 0x61, 0x7A, 0x76, 0x79, 0x67, 0xA9, 0x7D, 0x26, 0x46, 0x44, 0x91, 0xD6, 0x35, 0x80,
    0xD6, 0x76, 0x5E, 0xDB, 0xAA, 0xB6, 0x9B, 0x00, 0xF3, 0x36, 0x3A, 0x8D, 0xA6, 0x47, 0xD9, 0x70,
    0x38, 0x7A, 0x7E, 0xD4, 0xD1, 0x91, 0x48, 0xAF, 0x4B, 0xAE, 0x9E, 0xF8, 0x87, 0x41, 0x2B, 0x88,
    0x35, 0x41, 0xC1, 0x93, 0x42, 0x91, 0xB7, 0xE3, 0x13, 0xF1, 0xCA, 0x8E, 0x39, 0x3D, 0xFB, 0xF5,
    0x81, 0xAB, 0xAA, 0x13, 0x59, 0x68, 0xD9, 0xAA, 0xE1, 0xD8, 0xF9, 0x38, 0x3A, 0x64, 0x78, 0xA2,
    0xDF, 0xA4, 0x05, 0x30, 0xF7, 0x75, 0x71, 0xC4, 0xA7, 0x3B, 0x7C, 0x6C, 0xAC, 0x28, 0x82, 0xD8,
    0x0B, 0xC0, 0x31, 0xB1, 0x6F, 0x54, 0xBB, 0xB7, 0xB2, 0x01, 0x72, 0x99, 0xBC, 0x66, 0xBC, 0x87,
    0x6E, 0x79, 0xF3, 0xEE, 0x98, 0xC3, 0xD9, 0x10, 0x26, 0xA8, 0x8E, 0xA3, 0x8C, 0x8F, 0x56, 0xA0,
    0x00, 0x1A, 0x79, 0x75, 0x30, 0xE8, 0x6A, 0x83, 0x93, 0xBE, 0x9A, 0x5B, 0x8B, 0xE0, 0x5F, 0x43,
    0x74, 0x7E, 0xE9, 0x18, 0xE6, 0x45, 0x4C, 0x91, 0x6C, 0x23, 0x5F, 0x13, 0x30, 0x3B, 0x18, 0xF5,
    0xA2, 0xAA, 0x30, 0x16, 0x55, 0x5A, 0x14, 0xC2, 0x0C, 0xA8, 0x69, 0xE1, 0x3C, 0x3B, 0xED, 0x5D,
    0xD7, 0x79, 0x24, 0x20, 0x2C, 0x24, 0x1E, 0x57, 0xBB, 0xA3, 0x3C, 0x67, 0x5F, 0xE1, 0x4B, 0x76,
    0xA6, 0xED, 0xC8, 0xBB, 0xB6, 0x54, 0x73, 0xDA, 0x30, 0xE8, 0xBD, 0xD2, 0x77, 0x7E, 0x13, 0x4B,
    0xE9, 0xA0, 0x23, 0xDF, 0x0B, 0x04, 0x25, 0x2D, 0xE3, 0x04, 0xA2, 0xA8, 0xFA, 0xB7, 0x63, 0xDE,
    0x8E, 0x18, 0xE3, 0x7D, 0x92, 0x6E, 0xF4, 0xAB, 0x59, 0x38, 0x61, 0xB5, 0xF4, 0x47, 0x5E, 0xAC,
    0x02, 0x11, 0x14, 0xFF, 0x76, 0xA2, 0x54, 0x83, 0xBE, 0xB7, 0xBB, 0xDD, 0x1D, 0xEB, 0x76, 0xDD,
    0xAE, 0xDD, 0x63, 0xF4, 0x75, 0xB3, 0x95, 0xDD, 0x6D, 0x15, 0x59, 0x8D, 0x4E, 0x3C, 0x6A, 0xA2,
    0xEA, 0x86, 0xD0, 0x83, 0xC0, 0x21, 0x13, 0xA6, 0x44, 0x7E, 0xB6, 0xB1, 0x55, 0xE7, 0x2E, 0xE9,
    0x5B, 0xA6, 0x45, 0xE4, 0x46, 0x39, 0x7C, 0xFE, 0xF2, 0x0E, 0x01, 0x67, 0xE4, 0x43, 0x1D, 0xA4,
    0xF6, 0x75, 0xE0, 0xDA, 0x96, 0x1B, 0xB9, 0xAC, 0x3B, 0xFD, 0xD7, 0x5B, 0x47, 0xD7, 0xEC, 0xC2,
    0x23, 0xAC, 0x03, 0x06, 0x22, 0xA5, 0x1D, 0xC4, 0xD6, 0xDE, 0x7B, 0x83, 0x09, 0x12, 0x97, 0x9A,
    0xAC, 0x7B, 0xD1, 0x80, 0xBB, 0xAD, 0x05, 0xC3, 0xB8, 0xC7, 0x24, 0x08, 0x79, 0xAF, 0xF2, 0x5E,
    0x64, 0x49, 0x12, 0x69, 0xE3, 0xC9, 0xD1, 0x31, 0x7A, 0x59, 0x09, 0x93, 0x56, 0xE8, 0x8A, 0xF1,
    0x91, 0x96, 0xE4, 0x91, 0x6C, 0xE2, 0xF2, 0xC3, 0xAC, 0x8C, 0xC2, 0x99, 0x16, 0x2B, 0x7A, 0xF0,
    0x91, 0x2D, 0x16, 0xBD, 0xD1, 0x6D, 0xD5, 0x79, 0xD3, 0x93, 0x6B, 0x60, 0xE9, 0x6C, 0xFA, 0x9F,
    0x55, 0xE1, 0xCF, 0xF0, 0xBF, 0xD7, 0x73, 0xEA, 0x55, 0x3A, 0xEA, 0x1B, 0x7B, 0x31, 0xB6, 0xF3,
    0xF7, 0x98, 0x37, 0x37, 0x24, 0xDF, 0xE2, 0xD7, 0x69, 0xDC, 0x97, 0xE6, 0x4B, 0x95, 0x6B, 0xD6,
    0xE5, 0x7F, 0xF7, 0xAB, 0xD8, 0x98,
};

static void TestLongCodes(void)
{
    CHECK(InflateBytes(LONG_CODES, sizeof LONG_CODES, 1500, NULL) == mui_success,
          "codes longer than the fast table");
    CHECK(InflateBytes(LONG_CODES, sizeof LONG_CODES, 1499, NULL) == mui_errorFormat,
          "a long-coded stream past its output");
    CHECK(PrefixesRefused(LONG_CODES, sizeof LONG_CODES, 1500), "every cut of a long-coded stream");
}

int main(void)
{
    TestFixed();
    TestFast();
    TestFastDistances();
    TestFastMeaningless();
    TestLongCodes();
    TestLongestSymbol();
    TestStoredAfterFast();
    TestStored();
    TestDynamic();
    return s_failures == 0 ? 0 : 1;
}
