// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's snippets after its first program (docs/guide.md), each as
// written there (tools/check_guide.py checks it, family record 0019),
// run and their results checked: refusals named and counted, and a
// context in a counted allocator within its limits, a frame taking no
// memory.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// Section 2: results, ids and refusals.

// What a context says of calls that went wrong.
static void Refusals(muiContext* context)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    if (muiCreateNode(context, &def, &node) != mui_success)
    {
        return;
    }
    (void)muiDestroyNode(context, node);
    // A later node may take the slot; the old id never names it.
    muiResult stale = muiNode_InsertChild(context, node, node, (muiNodeId){0, 0});
    // A null id where a node is needed is the program's bug: refused,
    // and counted.
    muiResult invalid = muiNode_InsertChild(context, (muiNodeId){0, 0}, node, (muiNodeId){0, 0});
    printf("%s, %s, %llu refused\n", muiResultName(stale), muiResultName(invalid),
           (unsigned long long)muiGetContextMisuse(context));
}

// Section 2: defs, allocators and limits.

// The bytes the library holds, counted.
static size_t s_held;

static void* Alloc(size_t size, size_t alignment, void* user)
{
    (void)user;
    // The library asks for no more alignment than malloc gives.
    void* memory = alignment <= alignof(max_align_t) ? malloc(size) : NULL;
    s_held += memory != NULL ? size : 0;
    return memory;
}

static void Free(void* memory, size_t size, size_t alignment, void* user)
{
    (void)alignment;
    (void)user;
    s_held -= size;
    free(memory);
}

// A context for a small panel: room for 64 nodes and 256 draw commands,
// taken when it is made, from the counted allocator.
static muiContext* SmallContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.allocator = (muiAllocator){Alloc, Free, NULL};
    def.limits.nodes = 64;
    def.limits.drawCommands = 256;
    muiContext* context = NULL;
    return muiCreateContext(&def, &context) == mui_success ? context : NULL;
}

static void TestRefusals(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    Refusals(context);
    CHECK(muiGetContextMisuse(context) == 1, "one refusal counted, the stale id not");
    muiDestroyContext(context);
}

static void TestLimits(void)
{
    muiContext* context = SmallContext();
    CHECK(context != NULL && s_held > 0, "the limits' memory taken at once");
    size_t held = s_held;
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    muiNodeId node = {0, 0};
    bool made = muiCreateNode(context, &def, &root) == mui_success;
    for (int i = 1; i < 64; i++)
    {
        made = made && muiCreateNode(context, &def, &node) == mui_success &&
               muiNode_InsertChild(context, root, node, (muiNodeId){0, 0}) == mui_success;
    }
    CHECK(made && muiCreateNode(context, &def, &node) == mui_errorCapacity && s_held == held,
          "64 nodes, the 65th refused, nothing more taken");
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    CHECK(muiComputeLayout(context, root, &layout) == mui_success &&
              muiBuildDrawList(context, root, &draw) == mui_success && s_held == held,
          "a frame laid out and drawn taking nothing");
    muiDestroyContext(context);
    CHECK(s_held == 0, "everything given back");
}

int main(void)
{
    TestRefusals();
    TestLimits();
    return s_failures == 0 ? 0 : 1;
}
