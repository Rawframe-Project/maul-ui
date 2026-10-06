// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's consumer (record mui-0008): applying an
// update whole or not at all. It is checked, then copied, then put in
// place; nodes no node lists any more leave with their subtrees; then
// the changes are reported and what was replaced is freed.

#include "access_tree.h"
#include "access_tree_store.h"
#include "allocator.h"

#include "maul-ui/access_tree.h"

#include <stdalign.h>
#include <string.h>

// Where an id sits in this apply's index of the update, or would go;
// entries of earlier applies count as empty.
static uint32_t UpdatePlace(const muiAccessTree* tree, uint64_t id)
{
    uint32_t at = muiAccessHome(tree, id);
    while (tree->updates[at].apply == tree->apply && tree->updates[at].id != id)
    {
        at = (at + 1) & tree->mask;
    }
    return at;
}

// Whether the update sends a node.
static bool IsSent(const muiAccessTree* tree, uint64_t id)
{
    return tree->updates[UpdatePlace(tree, id)].apply == tree->apply;
}

static bool IsKnown(const muiAccessTree* tree, uint64_t id)
{
    return muiHeldSlotOf(tree, id) != 0 || IsSent(tree, id);
}

// Starts an apply: its own serial, which marks this apply's index
// entries and the nodes it sees listed.
static void Begin(muiAccessTree* tree)
{
    tree->apply++;
    if (tree->apply == 0)
    {
        // After 2^32 applies, the old marks could be read as new.
        memset(tree->updates, 0, ((size_t)tree->mask + 1) * sizeof(muiUpdateSlot));
        for (uint32_t i = 0; i < tree->capacity; i++)
        {
            tree->held[i].listed = 0;
        }
        tree->apply = 1;
    }
}

// Indexes the update and checks it fits the tree.
static muiResult Check(muiAccessTree* tree, const muiAccessUpdate* update)
{
    if (update->nodeCount > tree->capacity)
    {
        return mui_errorCapacity;
    }
    if (update->nodeCount != 0 && update->nodes == nullptr)
    {
        return mui_errorInvalid;
    }
    for (uint32_t i = 0; i < update->nodeCount; i++)
    {
        uint64_t id = update->nodes[i]->id;
        uint32_t at = UpdatePlace(tree, id);
        // The id 0, or a node sent twice.
        if (id == 0 || tree->updates[at].apply == tree->apply)
        {
            return mui_errorInvalid;
        }
        tree->updates[at] = (muiUpdateSlot){id, i + 1, tree->apply};
    }
    uint64_t root = update->root != 0 ? update->root : tree->root;
    if (root == 0 || (update->root != 0 && !IsSent(tree, update->root)) ||
        (update->focus != 0 && !IsKnown(tree, update->focus)))
    {
        return mui_errorInvalid;
    }
    uint32_t added = 0;
    for (uint32_t i = 0; i < update->nodeCount; i++)
    {
        const muiAccessNode* node = update->nodes[i];
        added += muiHeldSlotOf(tree, node->id) == 0 ? 1 : 0;
        if (node->childCount != 0 && update->children == nullptr)
        {
            return mui_errorInvalid;
        }
        for (uint32_t k = 0; k < node->childCount; k++)
        {
            if (!IsKnown(tree, update->children[node->firstChild + k]))
            {
                return mui_errorInvalid;
            }
        }
    }
    return tree->count + added <= tree->capacity ? mui_success : mui_errorCapacity;
}

static void FreeStaged(const muiAccessTree* tree, const muiAccessNode* node,
                       const muiStagedNode* staged)
{
    muiHeldNode owned = {
        .node = {.linkCount = node->linkCount, .childCount = node->childCount},
        .children = staged->children,
    };
    for (uint32_t kind = 0; kind < MUI_ACCESS_TEXTS; kind++)
    {
        owned.node.text[kind] = staged->text[kind];
        owned.node.textLength[kind] = node->textLength[kind];
    }
    owned.node.links = staged->links;
    muiFreeHeld(tree, &owned);
}

// Copies what a node sent points at; false when memory runs out, with
// nothing kept.
static bool Stage(const muiAccessTree* tree, const muiAccessNode* node, const uint64_t* children,
                  muiStagedNode* staged)
{
    *staged = (muiStagedNode){0};
    bool fits = true;
    for (uint32_t kind = 0; kind < MUI_ACCESS_TEXTS && fits; kind++)
    {
        if (node->text[kind] != nullptr)
        {
            size_t length = node->textLength[kind];
            staged->text[kind] = muiAllocate(&tree->allocator, length + 1, 1);
            fits = staged->text[kind] != nullptr;
            if (fits)
            {
                memcpy(staged->text[kind], node->text[kind], length);
                staged->text[kind][length] = '\0';
            }
        }
    }
    if (fits && node->linkCount != 0)
    {
        size_t size = node->linkCount * sizeof(muiAccessLink);
        staged->links = muiAllocate(&tree->allocator, size, alignof(muiAccessLink));
        fits = staged->links != nullptr;
        if (fits)
        {
            memcpy(staged->links, node->links, size);
        }
    }
    if (fits && node->childCount != 0)
    {
        size_t size = node->childCount * sizeof(uint64_t);
        staged->children = muiAllocate(&tree->allocator, size, alignof(uint64_t));
        fits = staged->children != nullptr;
        if (fits)
        {
            memcpy(staged->children, children + node->firstChild, size);
        }
    }
    if (!fits)
    {
        FreeStaged(tree, node, staged);
    }
    return fits;
}

// A node sent, as the tree holds it: pointing at its copies.
static muiHeldNode HeldOf(const muiAccessNode* node, const muiStagedNode* staged)
{
    muiHeldNode held = {.node = *node, .children = staged->children};
    for (uint32_t kind = 0; kind < MUI_ACCESS_TEXTS; kind++)
    {
        held.node.text[kind] = staged->text[kind];
        held.node.textLength[kind] = staged->text[kind] != nullptr ? node->textLength[kind] : 0;
    }
    held.node.links = staged->links;
    held.node.linkCount = staged->links != nullptr ? node->linkCount : 0;
    held.node.firstChild = 0;
    return held;
}

// Lets a node go with every node under it whose parent it is.
static void Remove(muiAccessTree* tree, uint32_t top, uint32_t* retiredCount)
{
    uint32_t count = 0;
    tree->stack[count++] = top;
    while (count != 0)
    {
        uint32_t slot = tree->stack[--count];
        muiHeldNode* held = &tree->held[slot - 1];
        for (uint32_t k = 0; k < held->node.childCount; k++)
        {
            uint32_t child = muiHeldSlotOf(tree, held->children[k]);
            if (child != 0 && tree->held[child - 1].parent == slot)
            {
                tree->stack[count++] = child;
            }
        }
        tree->retired[(*retiredCount)++] = (muiRetiredNode){*held, true};
        muiAccessUnindex(tree, slot);
        *held = (muiHeldNode){0};
        tree->free[tree->freeCount++] = slot;
        tree->count--;
    }
}

// Puts the staged nodes in place; how many it replaced, retired first.
static uint32_t Commit(muiAccessTree* tree, const muiAccessUpdate* update, uint32_t* addedOut)
{
    uint32_t replaced = 0;
    uint32_t added = 0;
    for (uint32_t i = 0; i < update->nodeCount; i++)
    {
        const muiAccessNode* node = update->nodes[i];
        muiHeldNode held = HeldOf(node, &tree->staged[i]);
        uint32_t slot = muiHeldSlotOf(tree, node->id);
        if (slot != 0)
        {
            muiHeldNode* old = &tree->held[slot - 1];
            tree->retired[replaced++] = (muiRetiredNode){*old, false};
            held.parent = old->parent;
            held.listed = old->listed;
            *old = held;
            continue;
        }
        slot = tree->free[--tree->freeCount];
        tree->held[slot - 1] = held;
        muiAccessIndex(tree, slot);
        tree->count++;
        tree->added[added++] = slot;
    }
    // Every child listed now has the parent that lists it.
    for (uint32_t i = 0; i < update->nodeCount; i++)
    {
        uint32_t slot = muiHeldSlotOf(tree, update->nodes[i]->id);
        const muiHeldNode* held = &tree->held[slot - 1];
        for (uint32_t k = 0; k < held->node.childCount; k++)
        {
            muiHeldNode* child = &tree->held[muiHeldSlotOf(tree, held->children[k]) - 1];
            child->parent = slot;
            child->listed = tree->apply;
        }
    }
    *addedOut = added;
    return replaced;
}

// Tells the host what changed, then frees what was replaced.
static void Report(muiAccessTree* tree, const muiAccessChanges* changes, uint32_t added,
                   uint32_t retired, uint64_t oldFocus)
{
    if (changes != nullptr)
    {
        for (uint32_t i = 0; i < added && changes->added != nullptr; i++)
        {
            const muiHeldNode* held = &tree->held[tree->added[i] - 1];
            if (held->node.id != 0)
            {
                changes->added(changes->user, tree, held->node.id);
            }
        }
        for (uint32_t i = 0; i < retired; i++)
        {
            const muiRetiredNode* node = &tree->retired[i];
            void (*report)(void*, const muiAccessTree*, const muiAccessNode*) =
                node->removed ? changes->removed : changes->updated;
            if (report != nullptr)
            {
                report(changes->user, tree, &node->held.node);
            }
        }
        if (changes->focusMoved != nullptr && tree->focus != oldFocus)
        {
            changes->focusMoved(changes->user, tree, oldFocus, tree->focus);
        }
    }
    for (uint32_t i = 0; i < retired; i++)
    {
        muiFreeHeld(tree, &tree->retired[i].held);
    }
}

// Lets go what the tree no longer reaches: a child a replaced node
// listed, which no node sent lists now, with its subtree; a root
// replaced; a node sent with no parent that is not the root.
static void Prune(muiAccessTree* tree, const muiAccessUpdate* update, uint32_t replaced,
                  uint64_t oldRoot, uint32_t* retired)
{
    for (uint32_t i = 0; i < replaced; i++)
    {
        const muiHeldNode* old = &tree->retired[i].held;
        for (uint32_t k = 0; k < old->node.childCount; k++)
        {
            uint32_t child = muiHeldSlotOf(tree, old->children[k]);
            if (child != 0 && tree->held[child - 1].listed != tree->apply)
            {
                Remove(tree, child, retired);
            }
        }
    }
    uint32_t old = muiHeldSlotOf(tree, oldRoot);
    if (old != 0 && oldRoot != tree->root && tree->held[old - 1].listed != tree->apply)
    {
        Remove(tree, old, retired);
    }
    for (uint32_t i = 0; i < update->nodeCount; i++)
    {
        uint64_t id = update->nodes[i]->id;
        uint32_t slot = muiHeldSlotOf(tree, id);
        if (slot != 0 && id != tree->root && tree->held[slot - 1].parent == 0)
        {
            Remove(tree, slot, retired);
        }
    }
}

muiResult muiAccessTree_Apply(muiAccessTree* tree, const muiAccessUpdate* update,
                              const muiAccessChanges* changes)
{
    if (tree == nullptr || update == nullptr)
    {
        return mui_errorInvalid;
    }
    Begin(tree);
    muiResult status = Check(tree, update);
    if (status != mui_success)
    {
        return status;
    }
    for (uint32_t i = 0; i < update->nodeCount; i++)
    {
        if (!Stage(tree, update->nodes[i], update->children, &tree->staged[i]))
        {
            for (uint32_t k = 0; k < i; k++)
            {
                FreeStaged(tree, update->nodes[k], &tree->staged[k]);
            }
            return mui_errorCapacity;
        }
    }
    uint32_t added = 0;
    uint32_t replaced = Commit(tree, update, &added);
    uint32_t retired = replaced;
    uint64_t oldRoot = tree->root;
    if (update->root != 0)
    {
        uint32_t root = muiHeldSlotOf(tree, update->root);
        tree->held[root - 1].parent = 0;
        tree->held[root - 1].listed = tree->apply;
        tree->root = update->root;
    }
    Prune(tree, update, replaced, oldRoot, &retired);
    uint64_t oldFocus = tree->focus;
    tree->focus = update->focus != 0 && muiHeldSlotOf(tree, update->focus) != 0 ? update->focus
                  : muiHeldSlotOf(tree, tree->focus) != 0                       ? tree->focus
                                                                                : tree->root;
    Report(tree, changes, added, retired, oldFocus);
    return mui_success;
}
