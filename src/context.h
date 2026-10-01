// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's parts, which the modules of the public API share.

#ifndef MAUL_UI_SRC_CONTEXT_H
#define MAUL_UI_SRC_CONTEXT_H

#include "layout_node.h"
#include "tree.h"

#include "maul-ui/context.h"

struct muiContext
{
    muiAllocator allocator;
    // The size of the one block that holds the context and its slots.
    size_t blockSize;
    muiTree tree;
    // Layout's values per node, parallel to the tree's slots.
    muiLayoutNode* layout;
    uint64_t misuse;
    // Set while a measure function runs; edits are refused then.
    bool measuring;
};

// Counts one refused call and returns mui_errorInvalid for it.
muiResult muiRefuse(muiContext* context);

// Whether an edit must be refused because a measure function is running.
bool muiIsMeasuring(const muiContext* context);

#endif // MAUL_UI_SRC_CONTEXT_H
