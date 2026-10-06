// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility (record mui-0008): the host's data per node, and the
// roots updates are built for.

#include "maul-ui/access.h"

#include "access.h"
#include "access_store.h"
#include "allocator.h"
#include "context.h"
#include "tree.h"

#include <stdalign.h>
#include <string.h>

// Texts are below 2^31 bytes, so a length and its NUL fit 32 bits.
#define MAX_TEXT ((size_t)INT32_MAX)

uint64_t muiAccessIdOf(muiNodeId nodeId)
{
    return ((uint64_t)nodeId.generation << 32) | nodeId.index1;
}

muiNodeId muiNodeIdOfAccess(uint64_t id)
{
    return (muiNodeId){(uint32_t)id, (uint32_t)(id >> 32)};
}

muiAccessEntry* muiAccessEntryOf(const muiContext* context, uint32_t slot)
{
    const muiAccessStore* store = &context->access;
    uint32_t i = store->entryOf[slot - 1];
    if (i == 0)
    {
        return nullptr;
    }
    muiAccessEntry* entry = &store->entries[i - 1];
    // The entry may since belong to another node, or to one destroyed.
    bool own = entry->node.index1 == slot && muiTreeResolve(&context->tree, entry->node) == slot;
    return own ? entry : nullptr;
}

// Frees the texts of the entries of nodes destroyed since and takes them
// out.
static void Purge(muiContext* context)
{
    muiAccessStore* store = &context->access;
    for (uint32_t i = store->count; i > 0; i--)
    {
        muiAccessEntry* entry = &store->entries[i - 1];
        if (muiTreeResolve(&context->tree, entry->node) != 0)
        {
            continue;
        }
        for (uint32_t kind = 0; kind < MUI_ACCESS_TEXTS; kind++)
        {
            muiAccessFreeText(entry, &context->allocator, kind);
        }
        if (store->entryOf[entry->node.index1 - 1] == i)
        {
            store->entryOf[entry->node.index1 - 1] = 0;
        }
        *entry = store->entries[--store->count];
        if (i - 1 < store->count)
        {
            store->entryOf[entry->node.index1 - 1] = i;
        }
    }
}

// The entry of the node at slot for an edit, made when it has none;
// NULL when the table is full.
static muiAccessEntry* Take(muiContext* context, uint32_t slot)
{
    muiAccessEntry* entry = muiAccessEntryOf(context, slot);
    if (entry != nullptr)
    {
        return entry;
    }
    muiAccessStore* store = &context->access;
    if (store->count == store->capacity)
    {
        Purge(context);
    }
    if (store->count == store->capacity)
    {
        return nullptr;
    }
    entry = &store->entries[store->count++];
    store->entryOf[slot - 1] = store->count;
    *entry = (muiAccessEntry){.node = muiTreeIdOf(&context->tree, slot)};
    return entry;
}

// Counts an edit of an entry and marks its node.
static void Edited(muiContext* context, uint32_t slot, muiAccessEntry* entry)
{
    entry->version = entry->version == UINT32_MAX ? 1 : entry->version + 1;
    muiNoteAccess(context, slot);
}

// How many bytes follow a UTF-8 lead byte; 4 for a byte that leads
// nothing well-formed.
static size_t Trailing(unsigned char lead)
{
    if (lead < 0x80)
    {
        return 0;
    }
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        return 1;
    }
    if ((lead & 0xF0) == 0xE0)
    {
        return 2;
    }
    return lead >= 0xF0 && lead <= 0xF4 ? 3 : 4;
}

// Whether the bytes after a lead byte are in range: 0x80 to 0xBF, the
// first narrower after E0, ED, F0 and F4, which would otherwise start
// overlong forms, surrogates, or code points past U+10FFFF.
static bool AreTrailing(const unsigned char* bytes, unsigned char lead, size_t count)
{
    unsigned char low = lead == 0xE0 ? 0xA0 : lead == 0xF0 ? 0x90 : 0x80;
    unsigned char high = lead == 0xED ? 0x9F : lead == 0xF4 ? 0x8F : 0xBF;
    bool valid = count == 0 || (bytes[0] >= low && bytes[0] <= high);
    for (size_t k = 1; k < count && valid; k++)
    {
        valid = bytes[k] >= 0x80 && bytes[k] <= 0xBF;
    }
    return valid;
}

// Whether text is well-formed UTF-8, by Unicode's table of well-formed
// byte sequences, without a NUL.
static bool IsUtf8(const unsigned char* text, size_t length)
{
    size_t i = 0;
    while (i < length)
    {
        unsigned char lead = text[i];
        size_t more = Trailing(lead);
        if (lead == 0 || more == 4 || length - i <= more || !AreTrailing(text + i + 1, lead, more))
        {
            return false;
        }
        i += more + 1;
    }
    return true;
}

muiResult muiNode_SetAccessRole(muiContext* context, muiNodeId nodeId, muiRole role)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (role > MUI_ROLE_LAST)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiAccessEntry* entry = Take(context, slot);
    if (entry == nullptr)
    {
        return mui_errorCapacity;
    }
    if (entry->role != role)
    {
        entry->role = role;
        Edited(context, slot, entry);
    }
    return mui_success;
}

muiResult muiNode_GetAccessRole(const muiContext* context, muiNodeId nodeId, muiRole* roleOut)
{
    if (context == nullptr || roleOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiAccessEntry* entry = muiAccessEntryOf(context, slot);
    *roleOut = entry != nullptr ? entry->role : mui_roleGeneric;
    return mui_success;
}

muiResult muiNode_SetAccessText(muiContext* context, muiNodeId nodeId, muiAccessTextKind kind,
                                const char* text, size_t length)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (kind >= MUI_ACCESS_TEXTS || (text == nullptr && length != 0) || length > MAX_TEXT ||
        !IsUtf8((const unsigned char*)text, length))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiAccessEntry* entry = muiAccessEntryOf(context, slot);
    if (length == 0)
    {
        // Clearing a text a node does not have makes no entry.
        if (entry != nullptr && entry->text[kind] != nullptr)
        {
            muiAccessFreeText(entry, &context->allocator, kind);
            Edited(context, slot, entry);
        }
        return mui_success;
    }
    entry = entry != nullptr ? entry : Take(context, slot);
    if (entry == nullptr)
    {
        return mui_errorCapacity;
    }
    if (entry->length[kind] == length && memcmp(entry->text[kind], text, length) == 0)
    {
        return mui_success;
    }
    char* copy = muiAllocate(&context->allocator, length + 1, 1);
    if (copy == nullptr)
    {
        return mui_errorCapacity;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    muiAccessFreeText(entry, &context->allocator, kind);
    entry->text[kind] = copy;
    entry->length[kind] = (uint32_t)length;
    Edited(context, slot, entry);
    return mui_success;
}

muiResult muiNode_GetAccessText(const muiContext* context, muiNodeId nodeId, muiAccessTextKind kind,
                                const char** textOut, size_t* lengthOut)
{
    if (context == nullptr || textOut == nullptr || lengthOut == nullptr || nodeId.index1 == 0 ||
        kind >= MUI_ACCESS_TEXTS)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiAccessEntry* entry = muiAccessEntryOf(context, slot);
    if (entry == nullptr || entry->text[kind] == nullptr)
    {
        return mui_empty;
    }
    *textOut = entry->text[kind];
    *lengthOut = entry->length[kind];
    return mui_success;
}

muiResult muiNode_SetAccessFlags(muiContext* context, muiNodeId nodeId, muiAccessFlags flags)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if ((flags & ~(muiAccessFlags)MUI_ACCESS_HOST_FLAGS) != 0)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiAccessEntry* entry = Take(context, slot);
    if (entry == nullptr)
    {
        return mui_errorCapacity;
    }
    if (entry->flags != flags)
    {
        entry->flags = flags;
        Edited(context, slot, entry);
    }
    return mui_success;
}

muiResult muiNode_GetAccessFlags(const muiContext* context, muiNodeId nodeId,
                                 muiAccessFlags* flagsOut)
{
    if (context == nullptr || flagsOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiAccessEntry* entry = muiAccessEntryOf(context, slot);
    *flagsOut = entry != nullptr ? entry->flags : 0;
    return mui_success;
}

muiAccessRoot* muiAccessRootOf(const muiContext* context, uint32_t slot)
{
    const muiAccessStore* store = &context->access;
    for (uint32_t i = 0; i < store->rootCount; i++)
    {
        muiAccessRoot* root = &store->roots[i];
        if (root->node.index1 == slot && muiTreeResolve(&context->tree, root->node) == slot)
        {
            return root;
        }
    }
    return nullptr;
}

// Takes out the roots destroyed since.
static void PurgeRoots(muiAccessStore* store, const muiTree* tree)
{
    for (uint32_t i = store->rootCount; i > 0; i--)
    {
        if (muiTreeResolve(tree, store->roots[i - 1].node) == 0)
        {
            store->roots[i - 1] = store->roots[--store->rootCount];
        }
    }
}

// Allocates the buffers for every slot: copies, the update's node
// pointers and children (8-aligned), then the places and the scratch
// order (4-aligned); false when memory runs out.
static bool AllocateBuffers(muiContext* context)
{
    muiAccessStore* store = &context->access;
    uint32_t slots = context->tree.capacity;
    if ((size_t)slots > SIZE_MAX / muiAccessSlotBytes())
    {
        return false;
    }
    unsigned char* block = muiAllocate(&context->allocator, (size_t)slots * muiAccessSlotBytes(),
                                       alignof(max_align_t));
    if (block == nullptr)
    {
        return false;
    }
    memset(block, 0, (size_t)slots * muiAccessSlotBytes());
    static_assert(sizeof(muiAccessNode) % alignof(uint64_t) == 0, "the parts stay 8-aligned");
    size_t at = 0;
    store->copies = (muiAccessNode*)block;
    at += (size_t)slots * sizeof(muiAccessNode);
    store->nodes = (const muiAccessNode**)(block + at);
    at += (size_t)slots * sizeof(const muiAccessNode*);
    store->children = (uint64_t*)(block + at);
    at += (size_t)slots * sizeof(uint64_t);
    store->sent = (muiAccessSent*)(block + at);
    at += (size_t)slots * sizeof(muiAccessSent);
    store->order = (uint32_t*)(block + at);
    store->buffers = block;
    store->slots = slots;
    return true;
}

muiResult muiAccess_Enable(muiContext* context, muiNodeId rootId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t root = muiResolveEdit(context, rootId, &status);
    if (root == 0)
    {
        return status;
    }
    if (muiTreeAt(&context->tree, root)->links.parent != 0)
    {
        return muiRefuse(context);
    }
    muiAccessStore* store = &context->access;
    muiAccessRoot* entry = muiAccessRootOf(context, root);
    if (entry != nullptr)
    {
        entry->whole = true;
        return mui_success;
    }
    PurgeRoots(store, &context->tree);
    if (store->rootCount == store->rootCapacity ||
        (store->buffers == nullptr && !AllocateBuffers(context)))
    {
        return mui_errorCapacity;
    }
    store->roots[store->rootCount++] =
        (muiAccessRoot){.node = muiTreeIdOf(&context->tree, root), .whole = true};
    return mui_success;
}

muiResult muiAccess_Disable(muiContext* context, muiNodeId rootId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (rootId.index1 == 0 || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiAccessStore* store = &context->access;
    uint32_t root = muiTreeResolve(&context->tree, rootId);
    muiAccessRoot* entry = root != 0 ? muiAccessRootOf(context, root) : nullptr;
    PurgeRoots(store, &context->tree);
    if (entry != nullptr)
    {
        // The purge kept it, as it is live; find it again.
        entry = muiAccessRootOf(context, root);
        *entry = store->roots[--store->rootCount];
    }
    if (store->rootCount == 0)
    {
        muiAccessFreeBuffers(store, &context->allocator);
    }
    return entry != nullptr ? mui_success : mui_empty;
}
