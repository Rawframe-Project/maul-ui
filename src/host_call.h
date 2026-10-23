// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code an owner object calls, with the owner marked busy for the
// length of the call and the mark restored after (Maul conventions,
// Threads): for the code that runs host code from a platform's entry points
// as well as from the owner's own functions.

#ifndef MAUL_UI_SRC_HOST_CALL_H
#define MAUL_UI_SRC_HOST_CALL_H

#include "allocator.h"

#include "maul-ui/access.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Marks an owner busy for one of its functions that change it: whether it
// was idle. A busy one counts one misuse, and the call is refused with
// mui_errorState.
static inline bool muiEnterMarked(bool* busy, uint64_t* misuse)
{
    if (*busy)
    {
        (*misuse)++;
        return false;
    }
    *busy = true;
    return true;
}

// The mark is an owner's mutable state, as a lock is: code that reads the
// owner through a const pointer still marks it while host code runs. An
// owner is never defined const (each is allocated), so writing the mark
// through such a pointer is defined.
static inline bool* muiMarkOf(const bool* busy)
{
    return (bool*)busy;
}

// muiAllocate with the owner busy.
static inline void* muiAllocateMarked(const bool* busy, const muiAllocator* allocator, size_t size,
                                      size_t alignment)
{
    bool was = *busy;
    *muiMarkOf(busy) = true;
    void* memory = muiAllocate(allocator, size, alignment);
    *muiMarkOf(busy) = was;
    return memory;
}

// muiRelease with the owner busy.
static inline void muiReleaseMarked(const bool* busy, const muiAllocator* allocator, void* memory,
                                    size_t size, size_t alignment)
{
    bool was = *busy;
    *muiMarkOf(busy) = true;
    muiRelease(allocator, memory, size, alignment);
    *muiMarkOf(busy) = was;
}

// An adapter's action function, called with the adapter busy.
static inline bool muiActMarked(const bool* busy, bool (*action)(void*, const muiAccessRequest*),
                                void* user, const muiAccessRequest* request)
{
    bool was = *busy;
    *muiMarkOf(busy) = true;
    bool done = action(user, request);
    *muiMarkOf(busy) = was;
    return done;
}

#endif // MAUL_UI_SRC_HOST_CALL_H
