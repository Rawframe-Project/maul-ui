// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The atlas's glyph table: keys alike but for their palette, or but for
// their tint, each found as its own entry, so a key's every part tells it
// apart where keys share a probe chain; and an entry stays stale however
// many times its plot was evicted since.

#include "glyph_table.h"
#include "test_harness.h"

enum
{
    KEYS = 64
};

// Inserts keys made by vary, then finds each and checks it is its own.
static bool EachOwn(void (*vary)(muiGlyphKey* key, uint32_t k))
{
    const muiAllocator allocator = {0};
    // One plot, never evicted.
    const uint64_t generations[1] = {0};
    muiGlyphTable table = {0};
    bool own = true;
    for (uint32_t k = 0; k < KEYS && own; k++)
    {
        muiGlyphKey key = {1, 1, 4, 0, 0};
        vary(&key, k);
        own = muiReserveEntry(&allocator, &table, generations);
        muiAtlasEntry* entry = own ? muiFindEntry(&table, &key) : nullptr;
        own = own && entry->plot == 0;
        if (own)
        {
            *entry = (muiAtlasEntry){.key = key, .plot = 1, .u = (uint16_t)k};
            table.count++;
        }
    }
    for (uint32_t k = 0; k < KEYS && own; k++)
    {
        muiGlyphKey key = {1, 1, 4, 0, 0};
        vary(&key, k);
        const muiAtlasEntry* entry = muiFindEntry(&table, &key);
        own = entry->plot == 1 && entry->u == k;
    }
    muiFreeGlyphTable(&allocator, &table);
    return own;
}

static void ByPalette(muiGlyphKey* key, uint32_t k)
{
    key->palette = k;
}

static void ByTint(muiGlyphKey* key, uint32_t k)
{
    key->tint = 0x01010101u * k;
}

// An entry placed at generation 0, its plot evicted 2^32 times since, or
// once, or not at all; and a glyph with no image, never stale.
static bool StaleAcrossEvictions(void)
{
    const uint64_t generations[3] = {(uint64_t)UINT32_MAX + 1, 1, 0};
    const muiAtlasEntry far = {.plot = 1};
    const muiAtlasEntry once = {.plot = 2};
    const muiAtlasEntry kept = {.plot = 3};
    const muiAtlasEntry bare = {.plot = MUI_NO_PLOT, .generation = 7};
    return muiIsEntryStale(&far, generations) && muiIsEntryStale(&once, generations) &&
           !muiIsEntryStale(&kept, generations) && !muiIsEntryStale(&bare, generations);
}

int main(void)
{
    CHECK(EachOwn(ByPalette), "keys apart by their palette");
    CHECK(EachOwn(ByTint), "keys apart by their tint");
    CHECK(StaleAcrossEvictions(), "an entry stale after 2^32 evictions of its plot");
    return s_failures == 0 ? 0 : 1;
}
