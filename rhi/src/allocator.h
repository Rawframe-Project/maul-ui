// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's memory (record mui-0005): through the host's
// allocator alone, as Maul UI's own allocation functions stay inside a
// shared build of Maul UI; a zeroed allocator is the C library's.

#ifndef MAUL_UI_RHI_ALLOCATOR_H
#define MAUL_UI_RHI_ALLOCATOR_H

#include "maul-ui/base.h"

#include <stddef.h>

// Whether an allocator is valid: both functions, or neither.
bool muiRhiIsAllocatorValid(const muiAllocator* allocator);

// Memory of a size and alignment, or NULL.
void* muiRhiAllocate(const muiAllocator* allocator, size_t size, size_t alignment);

// Gives back memory muiRhiAllocate gave, of its size and alignment; NULL
// is nothing.
void muiRhiRelease(const muiAllocator* allocator, void* memory, size_t size, size_t alignment);

#endif // MAUL_UI_RHI_ALLOCATOR_H
