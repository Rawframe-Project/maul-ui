// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's parts, which the modules of the public API share.

#ifndef MAUL_UI_SRC_CONTEXT_H
#define MAUL_UI_SRC_CONTEXT_H

#include "tree.h"

#include "maul-ui/context.h"

struct muiContext
{
    muiAllocator allocator;
    // The size of the one block that holds the context and its slots.
    size_t blockSize;
    muiTree tree;
    uint64_t misuse;
};

// Counts one refused call and returns mui_errorInvalid for it.
muiResult muiRefuse(muiContext* context);

#endif // MAUL_UI_SRC_CONTEXT_H
