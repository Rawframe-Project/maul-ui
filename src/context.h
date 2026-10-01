// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's parts, which the modules of the public API share.

#ifndef MAUL_UI_SRC_CONTEXT_H
#define MAUL_UI_SRC_CONTEXT_H

#include "animation.h"
#include "layout_node.h"
#include "notify.h"
#include "style_store.h"
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
    muiStyleStore style;
    // What conditions read of the world outside the tree.
    muiEnvironment environment;
    // How many times the host edited a class, a node type or the
    // environment, each of which restyles every node.
    uint32_t styleEdits;
    muiNotifyQueue notifications;
    // Transition specs and running transitions, and the latest time a
    // layout run was given.
    muiAnimationStore animations;
    uint64_t lastTimeNs;
    uint64_t misuse;
    // Set while a measure function runs; edits are refused then.
    bool measuring;
};

// Counts one refused call and returns mui_errorInvalid for it.
muiResult muiRefuse(muiContext* context);

// Whether an edit must be refused because a measure function is running.
bool muiIsMeasuring(const muiContext* context);

// The slot of a live node for an edit, or 0 with the status to return in
// statusOut: misuse for the null id or an edit from a measure function,
// stale for a gone node.
uint32_t muiResolveEdit(muiContext* context, muiNodeId nodeId, muiResult* statusOut);

#endif // MAUL_UI_SRC_CONTEXT_H
