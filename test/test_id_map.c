// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The id map: finding, inserting and removing, against a plain list,
// with deletions that wrap the table's end.

#include "id_map.h"
#include "test_harness.h"

#include <stddef.h>

static uint64_t Next(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void TestRandom(void)
{
    enum
    {
        Size = 64,
        Ids = 32
    };
    static uint64_t s_keys[Size];
    static void* s_values[Size];
    static int s_targets[Ids];
    muiIdMap map;
    muiIdMapInit(&map, s_keys, s_values, Size);
    uint64_t ids[Ids];
    bool in[Ids] = {false};
    uint64_t state = 0x2545F4914F6CDD1DULL;
    for (uint32_t i = 0; i < Ids; i++)
    {
        ids[i] = Next(&state) | 1;
    }
    bool ok = true;
    uint32_t count = 0;
    for (int round = 0; round < 20000 && ok; round++)
    {
        uint32_t i = (uint32_t)(Next(&state) % Ids);
        if (in[i])
        {
            ok = muiIdMapRemove(&map, ids[i]) == &s_targets[i] &&
                 muiIdMapRemove(&map, ids[i]) == NULL;
            count--;
        }
        else
        {
            ok = muiIdMapFind(&map, ids[i]) == NULL && muiIdMapInsert(&map, ids[i], &s_targets[i]);
            count++;
        }
        in[i] = !in[i];
        for (uint32_t k = 0; k < Ids && ok; k++)
        {
            ok = muiIdMapFind(&map, ids[k]) == (in[k] ? &s_targets[k] : NULL);
        }
        ok = ok && map.count == count;
    }
    CHECK(ok, "the map agrees with the list");
}

static void TestLimits(void)
{
    uint64_t keys[4];
    void* values[4];
    int target = 0;
    muiIdMap map;
    muiIdMapInit(&map, keys, values, 4);
    CHECK(!muiIdMapInsert(&map, 0, &target) && muiIdMapFind(&map, 0) == NULL &&
              muiIdMapRemove(&map, 0) == NULL,
          "the id 0");
    CHECK(muiIdMapInsert(&map, 5, &target) && muiIdMapInsert(&map, 6, &target) &&
              !muiIdMapInsert(&map, 7, &target) && map.count == 2,
          "half full");
    CHECK(muiIdMapRemove(&map, 5) == &target && muiIdMapInsert(&map, 7, &target) &&
              muiIdMapFind(&map, 7) == &target && muiIdMapFind(&map, 6) == &target,
          "room again");
}

int main(void)
{
    TestRandom();
    TestLimits();
    return s_failures == 0 ? 0 : 1;
}
