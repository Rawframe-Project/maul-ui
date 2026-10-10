// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text service and its fonts (record mui-0006): creation, limits,
// memory through the service's allocator, Ahem's metrics, and damaged
// fonts refused without harm.

#include "test_harness.h"

#include "maul-ui/font.h"
#include "maul-ui/text.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"

// An allocator that counts what it hands out, checks sizes match on
// release, and fails its allocation number failAt (from 1; 0 never).
typedef struct CountingAllocator
{
    int allocations;
    int frees;
    size_t liveBytes;
    // Alignments given less those returned: 0 when every block went back
    // with its own.
    size_t liveAlignment;
    int failAt;
} CountingAllocator;

static void* CountingAlloc(size_t size, size_t alignment, void* context)
{
    CountingAllocator* counter = context;
    if (counter->failAt != 0 && counter->allocations + 1 == counter->failAt)
    {
        counter->failAt = 0;
        return NULL;
    }
    counter->allocations++;
    counter->liveBytes += size;
    counter->liveAlignment += alignment;
    return alignment <= alignof(max_align_t) ? malloc(size) : NULL;
}

static void CountingFree(void* memory, size_t size, size_t alignment, void* context)
{
    CountingAllocator* counter = context;
    counter->frees++;
    counter->liveBytes -= size;
    counter->liveAlignment -= alignment;
    free(memory);
}

static muiTextService* MakeService(CountingAllocator* counter, uint32_t fonts)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    if (counter != NULL)
    {
        def.allocator = (muiAllocator){CountingAlloc, CountingFree, counter};
    }
    def.limits.fonts = fonts;
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success && service != NULL, "service");
    return service;
}

static muiFontDef AhemDef(muiFontData mode)
{
    muiFontDef def = muiDefaultFontDef();
    def.data = s_ahem;
    def.size = sizeof s_ahem;
    def.dataMode = mode;
    return def;
}

static void TestServiceDefs(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    CHECK(def.limits.fonts == 64, "default font limit");
    muiTextService* service = (muiTextService*)&def;
    CHECK(muiCreateTextService(NULL, &service) == mui_errorInvalid && service == NULL,
          "no def, and the out is cleared");
    CHECK(muiCreateTextService(&def, NULL) == mui_errorInvalid, "no out");
    muiTextServiceDef bad = def;
    bad.cookie = 0;
    CHECK(muiCreateTextService(&bad, &service) == mui_errorInvalid, "bad cookie");
    bad = def;
    bad.version = MUI_DEF_VERSION + 1;
    CHECK(muiCreateTextService(&bad, &service) == mui_errorVersion, "another version");
    bad = def;
    bad.limits.fonts = 0;
    CHECK(muiCreateTextService(&bad, &service) == mui_errorInvalid, "no fonts");
    bad.limits.fonts = 65537;
    CHECK(muiCreateTextService(&bad, &service) == mui_errorInvalid, "too many fonts");
    bad.limits.fonts = 65536;
    CHECK(muiCreateTextService(&bad, &service) == mui_success, "the most fonts");
    muiDestroyTextService(service);
    bad = def;
    bad.allocator.alloc = CountingAlloc;
    CHECK(muiCreateTextService(&bad, &service) == mui_errorInvalid, "half an allocator");
    muiDestroyTextService(NULL);
}

static void TestCountFaces(void)
{
    uint32_t count = 7;
    CHECK(muiCountFontFaces(s_ahem, sizeof s_ahem, &count) == mui_success && count == 1,
          "a single font");
    CHECK(muiCountFontFaces(NULL, 12, &count) == mui_errorInvalid && count == 0, "no data");
    CHECK(muiCountFontFaces(s_ahem, sizeof s_ahem, NULL) == mui_errorInvalid, "no out");
    CHECK(muiCountFontFaces(s_ahem, 11, &count) == mui_errorFormat, "shorter than a header");
    // A collection of two faces: its tag, version, count and offsets.
    unsigned char collection[20] = {'t', 't', 'c', 'f', 0, 1, 0, 0, 0, 0, 0, 2};
    CHECK(muiCountFontFaces(collection, sizeof collection, &count) == mui_success && count == 2,
          "a collection");
    CHECK(muiCountFontFaces(collection, 19, &count) == mui_errorFormat && count == 0,
          "offsets past the end");
    collection[11] = 0;
    CHECK(muiCountFontFaces(collection, sizeof collection, &count) == mui_errorFormat,
          "an empty collection");
    const unsigned char cff[12] = {'O', 'T', 'T', 'O'};
    const unsigned char apple[12] = {'t', 'r', 'u', 'e'};
    CHECK(muiCountFontFaces(cff, sizeof cff, &count) == mui_success && count == 1, "CFF");
    CHECK(muiCountFontFaces(apple, sizeof apple, &count) == mui_success && count == 1, "Apple");
    // A tag of no font, whatever follows it.
    const unsigned char other[16] = {'w', 'O', 'F', '2', 0, 0, 0, 0, 0, 0, 0, 1};
    CHECK(muiCountFontFaces(other, sizeof other, &count) == mui_errorFormat, "another tag");
    const unsigned char woff2[12] = {'w', 'O', 'F', '2'};
    const unsigned char woff[12] = {'w', 'O', 'F', 'F'};
    CHECK(muiCountFontFaces(woff2, sizeof woff2, &count) == mui_errorFormat, "WOFF2");
    CHECK(muiCountFontFaces(woff, sizeof woff, &count) == mui_errorFormat, "WOFF");
}

static void TestAhemMetrics(void)
{
    CountingAllocator counter = {0};
    muiTextService* service = MakeService(&counter, 4);
    const muiFontData modes[2] = {mui_fontDataCopy, mui_fontDataBorrow};
    for (int i = 0; i < 2; i++)
    {
        const muiFontDef def = AhemDef(modes[i]);
        muiFontId font = {0};
        CHECK(muiCreateFont(service, &def, &font) == mui_success && font.index1 != 0, "created");
        CHECK(muiFont_IsValid(service, font), "valid");
        muiFontMetrics metrics = {0};
        CHECK(muiFont_GetMetrics(service, font, &metrics) == mui_success, "metrics");
        CHECK(metrics.unitsPerEm == 1000 && metrics.glyphCount == 278, "units and glyphs");
        // hhea, as Ahem does not ask for its typographic metrics.
        CHECK(metrics.ascent == 0.8f && metrics.descent == 0.2f && metrics.lineGap == 0.0f,
              "ascent, descent and gap");
        CHECK(metrics.capHeight == 0.8f && metrics.xHeight == 0.8f, "cap and x heights");
        CHECK(metrics.underlineOffset == 0.133f && metrics.underlineThickness == 0.02f,
              "underline");
        CHECK(metrics.strikeoutOffset == 0.259f && metrics.strikeoutThickness == 0.05f,
              "strikeout");
        CHECK(muiDestroyFont(service, font) == mui_success, "destroyed");
        CHECK(!muiFont_IsValid(service, font), "gone");
        CHECK(muiFont_GetMetrics(service, font, &metrics) == mui_errorStale, "stale metrics");
        CHECK(muiDestroyFont(service, font) == mui_errorStale, "destroyed twice");
    }
    muiDestroyTextService(service);
    CHECK(counter.liveBytes == 0 && counter.allocations == counter.frees,
          "every block, FreeType's too, returned");
    // Found by a mutant returning a font's copy with another alignment.
    CHECK(counter.liveAlignment == 0, "each with the alignment it was given");
}

static void TestFontArguments(void)
{
    muiTextService* service = MakeService(NULL, 2);
    muiFontDef def = AhemDef(mui_fontDataCopy);
    muiFontId font = {1, 1};
    CHECK(muiCreateFont(NULL, &def, &font) == mui_errorInvalid && font.index1 == 0,
          "no service, and the out is cleared");
    CHECK(muiCreateFont(service, NULL, &font) == mui_errorInvalid, "no def");
    CHECK(muiCreateFont(service, &def, NULL) == mui_errorInvalid, "no out");
    muiFontDef bad = def;
    bad.cookie = 0;
    CHECK(muiCreateFont(service, &bad, &font) == mui_errorInvalid, "bad cookie");
    bad = def;
    bad.data = NULL;
    CHECK(muiCreateFont(service, &bad, &font) == mui_errorInvalid, "no data");
    bad = def;
    bad.size = 11;
    CHECK(muiCreateFont(service, &bad, &font) == mui_errorInvalid, "too short");
    bad = def;
    bad.dataMode = 2;
    CHECK(muiCreateFont(service, &bad, &font) == mui_errorInvalid, "unknown mode");
    bad = def;
    bad.size = (size_t)INT32_MAX + 1u;
    bad.dataMode = mui_fontDataBorrow;
    CHECK(muiCreateFont(service, &bad, &font) == mui_errorInvalid, "too long");
    bad = def;
    bad.faceIndex = 1;
    CHECK(muiCreateFont(service, &bad, &font) == mui_errorFormat, "no second face");
    muiFontMetrics metrics;
    CHECK(muiFont_GetMetrics(NULL, font, &metrics) == mui_errorInvalid, "metrics, no service");
    CHECK(muiFont_GetMetrics(service, (muiFontId){0}, &metrics) == mui_errorInvalid,
          "metrics, null id");
    CHECK(muiDestroyFont(NULL, (muiFontId){1, 1}) == mui_errorInvalid, "destroy, no service");
    CHECK(muiDestroyFont(service, (muiFontId){0}) == mui_errorInvalid, "destroy, null id");
    CHECK(!muiFont_IsValid(NULL, (muiFontId){1, 1}) && !muiFont_IsValid(service, (muiFontId){0}),
          "not valid");

    // The limit, and a slot reused under a new generation.
    muiFontId first = {0};
    muiFontId second = {0};
    CHECK(muiCreateFont(service, &def, &first) == mui_success &&
              muiCreateFont(service, &def, &second) == mui_success,
          "two fonts");
    CHECK(muiCreateFont(service, &def, &font) == mui_errorCapacity && font.index1 == 0,
          "a third is past the limit");
    CHECK(muiFont_GetMetrics(service, first, &metrics) == mui_success, "metrics");
    CHECK(muiFont_GetMetrics(service, first, NULL) == mui_errorInvalid, "metrics, no out");
    CHECK(muiDestroyFont(service, second) == mui_success, "one gone");
    CHECK(muiCreateFont(service, &def, &font) == mui_success && font.index1 == second.index1 &&
              font.generation != second.generation,
          "its slot reused");
    CHECK(!muiFont_IsValid(service, second) && muiFont_IsValid(service, font), "ids told apart");
    // Fonts left to the service are destroyed with it.
    muiDestroyTextService(service);
}

// A copy takes the font's size from the allocator and a borrow does
// not; a face the file lacks is refused before anything is taken.
static void TestFontMemory(void)
{
    CountingAllocator counter = {0};
    muiTextService* service = MakeService(&counter, 2);
    size_t before = counter.liveBytes;
    int allocations = counter.allocations;
    muiFontDef def = AhemDef(mui_fontDataCopy);
    def.faceIndex = 1;
    muiFontId font = {0};
    CHECK(muiCreateFont(service, &def, &font) == mui_errorFormat &&
              counter.allocations == allocations,
          "no second face, and nothing taken");
    def = AhemDef(mui_fontDataBorrow);
    CHECK(muiCreateFont(service, &def, &font) == mui_success, "borrowed");
    size_t borrowed = counter.liveBytes - before;
    CHECK(muiDestroyFont(service, font) == mui_success, "destroyed");
    def = AhemDef(mui_fontDataCopy);
    CHECK(muiCreateFont(service, &def, &font) == mui_success, "copied");
    CHECK(counter.liveBytes - before == borrowed + sizeof s_ahem, "the copy is the font's size");
    CHECK(muiDestroyFont(service, font) == mui_success && counter.liveBytes == before, "returned");
    muiDestroyTextService(service);
}

// Every allocation of a service and a font failing in turn: each call
// fails cleanly or succeeds, and every block comes back.
static void TestAllocationFailures(void)
{
    int failures = 0;
    for (int failAt = 1; failAt < 400; failAt++)
    {
        CountingAllocator counter = {.failAt = failAt};
        muiTextServiceDef def = muiDefaultTextServiceDef();
        def.allocator = (muiAllocator){CountingAlloc, CountingFree, &counter};
        muiTextService* service = NULL;
        muiResult created = muiCreateTextService(&def, &service);
        if (created == mui_success)
        {
            const muiFontDef font = AhemDef(mui_fontDataCopy);
            muiFontId id = {0};
            muiResult made = muiCreateFont(service, &font, &id);
            CHECK(made == mui_success || made == mui_errorCapacity, "font or out of memory");
            failures += made == mui_errorCapacity ? 1 : 0;
            muiDestroyTextService(service);
        }
        else
        {
            CHECK(created == mui_errorCapacity && service == NULL, "service or out of memory");
            failures++;
        }
        CHECK(counter.liveBytes == 0 && counter.allocations == counter.frees,
              "every block returned after a failure");
        if (counter.failAt != 0)
        {
            // Fewer allocations than failAt: every one was tried.
            break;
        }
    }
    CHECK(failures > 3, "failures were injected");
}

// Damaged copies of Ahem: cut short, and with bytes of the header and
// table directory changed. Each is refused or read, never harmful.
static void TestDamagedFonts(void)
{
    CountingAllocator counter = {0};
    muiTextService* service = MakeService(&counter, 1);
    unsigned char* copy = malloc(sizeof s_ahem);
    CHECK(copy != NULL, "copy");
    if (copy == NULL)
    {
        return;
    }
    int refused = 0;
    for (size_t size = 12; size < sizeof s_ahem; size += 97)
    {
        muiFontDef def = AhemDef(mui_fontDataBorrow);
        def.size = size;
        muiFontId font = {0};
        muiResult result = muiCreateFont(service, &def, &font);
        CHECK(result == mui_success || result == mui_errorFormat, "cut short");
        refused += result == mui_errorFormat ? 1 : 0;
        if (result == mui_success)
        {
            CHECK(muiDestroyFont(service, font) == mui_success, "destroyed");
        }
    }
    CHECK(refused > 0, "cut fonts are refused");
    uint32_t state = 1;
    for (int round = 0; round < 2000; round++)
    {
        memcpy(copy, s_ahem, sizeof s_ahem);
        for (int flip = 0; flip < 4; flip++)
        {
            state = state * 1664525u + 1013904223u;
            // The header, the table directory and the tables' first bytes.
            copy[(state >> 8) % 600u] ^= (unsigned char)(state >> 24 | 1u);
        }
        muiFontDef def = AhemDef(mui_fontDataBorrow);
        def.data = copy;
        muiFontId font = {0};
        muiResult result = muiCreateFont(service, &def, &font);
        CHECK(result == mui_success || result == mui_errorFormat, "damaged");
        if (result == mui_success)
        {
            muiFontMetrics metrics;
            CHECK(muiFont_GetMetrics(service, font, &metrics) == mui_success &&
                      metrics.unitsPerEm >= 16 && metrics.unitsPerEm <= 16384,
                  "a font read has sane units");
            CHECK(muiDestroyFont(service, font) == mui_success, "destroyed");
        }
    }
    free(copy);
    muiDestroyTextService(service);
    CHECK(counter.liveBytes == 0, "every block returned");
}

// The offset of a table of a single font, 0 when it has none.
static size_t TableOffset(const unsigned char* font, const char tag[4])
{
    uint32_t tables = (uint32_t)font[4] << 8 | font[5];
    for (uint32_t i = 0; i < tables; i++)
    {
        const unsigned char* record = font + 12 + 16 * i;
        if (memcmp(record, tag, 4) == 0)
        {
            return (size_t)record[8] << 24 | (size_t)record[9] << 16 | (size_t)record[10] << 8 |
                   record[11];
        }
    }
    return 0;
}

static void Put16(unsigned char* at, int value)
{
    at[0] = (unsigned char)((unsigned)value >> 8);
    at[1] = (unsigned char)value;
}

// Renames a table, so a font reads as without it.
static void HideTable(unsigned char* font, const char tag[4])
{
    uint32_t tables = (uint32_t)font[4] << 8 | font[5];
    for (uint32_t i = 0; i < tables; i++)
    {
        unsigned char* record = font + 12 + 16 * i;
        if (memcmp(record, tag, 4) == 0)
        {
            record[0] = 'z';
        }
    }
}

static muiFontMetrics MetricsOf(muiTextService* service, const unsigned char* font)
{
    muiFontDef def = AhemDef(mui_fontDataCopy);
    def.data = font;
    muiFontId id = {0};
    muiFontMetrics metrics = {0};
    CHECK(muiCreateFont(service, &def, &id) == mui_success &&
              muiFont_GetMetrics(service, id, &metrics) == mui_success &&
              muiDestroyFont(service, id) == mui_success,
          "a patched Ahem");
    return metrics;
}

// Which ascent, descent and gap a font's tables give, on copies of Ahem
// with its OS/2 and hhea values changed.
static void TestMetricsChoice(void)
{
    muiTextService* service = MakeService(NULL, 1);
    unsigned char* font = malloc(sizeof s_ahem);
    CHECK(font != NULL, "copy");
    if (font == NULL)
    {
        return;
    }
    size_t os2 = TableOffset(s_ahem, "OS/2");
    size_t hhea = TableOffset(s_ahem, "hhea");
    CHECK(os2 != 0 && hhea != 0, "tables found");
    // Typographic metrics of their own, and windows ones.
    memcpy(font, s_ahem, sizeof s_ahem);
    Put16(font + os2 + 68, 900);
    Put16(font + os2 + 70, -300);
    Put16(font + os2 + 72, 100);
    Put16(font + os2 + 74, 1100);
    Put16(font + os2 + 76, 400);
    muiFontMetrics metrics = MetricsOf(service, font);
    CHECK(metrics.ascent == 0.8f && metrics.descent == 0.2f && metrics.lineGap == 0.0f,
          "hhea without USE_TYPO_METRICS");
    font[os2 + 63] |= 0x80u;
    metrics = MetricsOf(service, font);
    CHECK(metrics.ascent == 0.9f && metrics.descent == 0.3f && metrics.lineGap == 0.1f,
          "typographic with USE_TYPO_METRICS");
    font[os2 + 63] &= 0x7Fu;
    Put16(font + hhea + 4, 0);
    Put16(font + hhea + 6, 0);
    metrics = MetricsOf(service, font);
    CHECK(metrics.ascent == 0.9f && metrics.descent == 0.3f && metrics.lineGap == 0.1f,
          "typographic when hhea has none");
    Put16(font + os2 + 68, 0);
    Put16(font + os2 + 70, 0);
    metrics = MetricsOf(service, font);
    CHECK(metrics.ascent == 1.1f && metrics.descent == 0.4f && metrics.lineGap == 0.0f,
          "windows when neither has any");
    // An OS/2 table before version 2 has no cap or x height.
    memcpy(font, s_ahem, sizeof s_ahem);
    Put16(font + os2, 1);
    metrics = MetricsOf(service, font);
    CHECK(metrics.capHeight == 0.0f && metrics.xHeight == 0.0f && metrics.strikeoutOffset == 0.259f,
          "version 1");
    // A font without glyphs, which FreeType reads.
    size_t maxp = TableOffset(s_ahem, "maxp");
    memcpy(font, s_ahem, sizeof s_ahem);
    Put16(font + maxp + 4, 0);
    muiFontDef def = AhemDef(mui_fontDataBorrow);
    def.data = font;
    muiFontId id = {0};
    CHECK(muiCreateFont(service, &def, &id) == mui_errorFormat, "no glyphs");
    // Without OS/2 or post, what they give is 0.
    memcpy(font, s_ahem, sizeof s_ahem);
    HideTable(font, "OS/2");
    HideTable(font, "post");
    metrics = MetricsOf(service, font);
    CHECK(metrics.ascent == 0.8f && metrics.capHeight == 0.0f && metrics.strikeoutOffset == 0.0f &&
              metrics.strikeoutThickness == 0.0f && metrics.underlineOffset == 0.0f &&
              metrics.underlineThickness == 0.0f,
          "without OS/2 and post");
    free(font);
    muiDestroyTextService(service);
}

int main(void)
{
    TestServiceDefs();
    TestCountFaces();
    TestAhemMetrics();
    TestFontArguments();
    TestFontMemory();
    TestAllocationFailures();
    TestDamagedFonts();
    TestMetricsChoice();
    return s_failures == 0 ? 0 : 1;
}
