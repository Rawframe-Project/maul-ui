// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context and the one block of memory it reserves at creation.

#include "context.h"

#include "allocator.h"
#include "invariant.h"

#include <string.h>

#define CONTEXT_DEF_COOKIE 0x6D756378u // "mucx"

// Slots are 1-based uint32_t values with room for a parent link count.
#define MAX_NODES 0x7FFFFFFFu

muiContextDef muiDefaultContextDef(void)
{
    return (muiContextDef){.cookie = CONTEXT_DEF_COOKIE, .limits = {.nodes = 4096}};
}

muiResult muiCreateContext(const muiContextDef* def, muiContext** contextOut)
{
    if (contextOut != nullptr)
    {
        *contextOut = nullptr;
    }
    if (def == nullptr || contextOut == nullptr || def->cookie != CONTEXT_DEF_COOKIE ||
        !muiIsAllocatorValid(&def->allocator) || def->limits.nodes == 0 ||
        def->limits.nodes > MAX_NODES)
    {
        return mui_errorInvalid;
    }
    muiLayout layout = {0};
    size_t contextOffset = muiLayoutAdd(&layout, 1, sizeof(muiContext), alignof(muiContext));
    size_t nodesOffset =
        muiLayoutAdd(&layout, def->limits.nodes, sizeof(muiTreeNode), alignof(muiTreeNode));
    if (layout.overflow)
    {
        return mui_errorCapacity;
    }
    unsigned char* block = muiAllocate(&def->allocator, layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    memset(block, 0, layout.size);
    // The context opens the block, so the block is freed through it.
    MUI_ASSERT(contextOffset == 0);
    muiContext* context = (muiContext*)block;
    context->allocator = def->allocator;
    context->blockSize = layout.size;
    muiTreeInit(&context->tree, (muiTreeNode*)(block + nodesOffset), def->limits.nodes);
    *contextOut = context;
    return mui_success;
}

void muiDestroyContext(muiContext* context)
{
    if (context == nullptr)
    {
        return;
    }
    const muiAllocator allocator = context->allocator;
    muiRelease(&allocator, context, context->blockSize, alignof(max_align_t));
}

uint64_t muiGetContextMisuse(const muiContext* context)
{
    return context != nullptr ? context->misuse : 0;
}

muiResult muiRefuse(muiContext* context)
{
    context->misuse++;
    return mui_errorInvalid;
}
