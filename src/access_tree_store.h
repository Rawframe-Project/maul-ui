// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's consumer (record mui-0008): the nodes a tree
// holds, found by id, and what applying an update stages and retires.

#ifndef MAUL_UI_SRC_ACCESS_TREE_STORE_H
#define MAUL_UI_SRC_ACCESS_TREE_STORE_H

#include "maul-ui/access_tree.h"

#include <stdbool.h>
#include <stdint.h>

// A node held: the record, its texts and links pointing at the tree's
// copies, and its children; an id of 0 marks a free slot.
typedef struct muiHeldNode
{
    muiAccessNode node;
    uint64_t* children;
    // The parent's slot, 0 for none, and the apply that last saw a node
    // it sent list this one.
    uint32_t parent;
    uint32_t listed;
} muiHeldNode;

// What applying an update copies before it changes anything.
typedef struct muiStagedNode
{
    char* text[MUI_ACCESS_TEXTS];
    muiAccessLink* links;
    uint64_t* children;
} muiStagedNode;

// A node an apply replaced or let go, kept for the report.
typedef struct muiRetiredNode
{
    muiHeldNode held;
    bool removed;
} muiRetiredNode;

// A slot in the update's index: the node's place in the update plus 1,
// and the apply it was written in.
typedef struct muiUpdateSlot
{
    uint64_t id;
    uint32_t index1;
    uint32_t apply;
} muiUpdateSlot;

struct muiAccessTree
{
    muiAllocator allocator;
    size_t blockSize;
    uint32_t capacity;
    uint32_t count;
    // Slot i is held[i - 1].
    muiHeldNode* held;
    // Free slots, a stack.
    uint32_t* free;
    uint32_t freeCount;
    // Held slots by id, open addressing with linear probing; 0 is empty.
    uint32_t* map;
    muiUpdateSlot* updates;
    uint32_t mask;
    // The apply under way, counting from 1.
    uint32_t apply;
    muiStagedNode* staged;
    muiRetiredNode* retired;
    uint32_t* added;
    uint32_t* stack;
    uint64_t root;
    uint64_t focus;
};

// The slot holding a node, or 0.
uint32_t muiHeldSlotOf(const muiAccessTree* tree, uint64_t id);

// Frees what a held node owns.
void muiFreeHeld(const muiAccessTree* tree, const muiHeldNode* held);

#endif // MAUL_UI_SRC_ACCESS_TREE_STORE_H
