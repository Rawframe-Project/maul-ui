// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public node functions: argument checks and refusals around the
// node store.

#include "maul-ui/node.h"

#include "context.h"
#include "tree.h"

#define NODE_DEF_COOKIE 0x6D756E64u // "mund"

static bool IsNull(muiNodeId nodeId)
{
    return nodeId.index1 == 0;
}

// The slot of a live node, or 0 with the status to return in statusOut:
// misuse for the null id, stale for a gone node.
static uint32_t ResolveLive(muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (IsNull(nodeId))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

// The slot of a live node for a query, or 0.
static uint32_t ResolveQuery(const muiContext* context, muiNodeId nodeId)
{
    return context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
}

muiNodeDef muiDefaultNodeDef(void)
{
    return (muiNodeDef){.cookie = NODE_DEF_COOKIE};
}

muiResult muiCreateNode(muiContext* context, const muiNodeDef* def, muiNodeId* nodeIdOut)
{
    if (nodeIdOut != nullptr)
    {
        *nodeIdOut = (muiNodeId){0, 0};
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (def == nullptr || nodeIdOut == nullptr || def->cookie != NODE_DEF_COOKIE)
    {
        return muiRefuse(context);
    }
    uint32_t slot = muiTreeCreate(&context->tree, def->hostKey);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    *nodeIdOut = muiTreeIdOf(&context->tree, slot);
    return mui_success;
}

muiResult muiDestroyNode(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveLive(context, nodeId, &status);
    if (slot != 0)
    {
        muiTreeDestroy(&context->tree, slot);
    }
    return status;
}

bool muiNode_IsValid(const muiContext* context, muiNodeId nodeId)
{
    return ResolveQuery(context, nodeId) != 0;
}

muiResult muiNode_InsertChild(muiContext* context, muiNodeId parentId, muiNodeId childId,
                              muiNodeId beforeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t parent = ResolveLive(context, parentId, &status);
    if (parent == 0)
    {
        return status;
    }
    uint32_t child = ResolveLive(context, childId, &status);
    if (child == 0)
    {
        return status;
    }
    uint32_t before = 0;
    if (!IsNull(beforeId))
    {
        before = muiTreeResolve(&context->tree, beforeId);
        if (before == 0)
        {
            return mui_errorStale;
        }
    }
    muiTree* tree = &context->tree;
    if (muiTreeAt(tree, child)->links.parent != 0 || muiTreeIsAncestor(tree, child, parent) ||
        (before != 0 && muiTreeAt(tree, before)->links.parent != parent))
    {
        return muiRefuse(context);
    }
    muiTreeInsert(tree, parent, child, before);
    return mui_success;
}

muiResult muiNode_Detach(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveLive(context, nodeId, &status);
    if (slot != 0)
    {
        muiTreeDetach(&context->tree, slot);
    }
    return status;
}

// The links of a live node, or NULL.
static const muiTreeLinks* LinksOf(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = ResolveQuery(context, nodeId);
    return slot != 0 ? &muiTreeAt(&context->tree, slot)->links : nullptr;
}

muiNodeId muiNode_GetParent(const muiContext* context, muiNodeId nodeId)
{
    const muiTreeLinks* links = LinksOf(context, nodeId);
    return links != nullptr ? muiTreeIdOf(&context->tree, links->parent) : (muiNodeId){0, 0};
}

muiNodeId muiNode_GetFirstChild(const muiContext* context, muiNodeId nodeId)
{
    const muiTreeLinks* links = LinksOf(context, nodeId);
    return links != nullptr ? muiTreeIdOf(&context->tree, links->firstChild) : (muiNodeId){0, 0};
}

muiNodeId muiNode_GetLastChild(const muiContext* context, muiNodeId nodeId)
{
    const muiTreeLinks* links = LinksOf(context, nodeId);
    return links != nullptr ? muiTreeIdOf(&context->tree, links->lastChild) : (muiNodeId){0, 0};
}

muiNodeId muiNode_GetNextSibling(const muiContext* context, muiNodeId nodeId)
{
    const muiTreeLinks* links = LinksOf(context, nodeId);
    return links != nullptr ? muiTreeIdOf(&context->tree, links->next) : (muiNodeId){0, 0};
}

muiNodeId muiNode_GetPreviousSibling(const muiContext* context, muiNodeId nodeId)
{
    const muiTreeLinks* links = LinksOf(context, nodeId);
    return links != nullptr ? muiTreeIdOf(&context->tree, links->previous) : (muiNodeId){0, 0};
}

uint32_t muiNode_GetChildCount(const muiContext* context, muiNodeId nodeId)
{
    const muiTreeLinks* links = LinksOf(context, nodeId);
    return links != nullptr ? links->childCount : 0;
}

uint64_t muiNode_GetHostKey(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = ResolveQuery(context, nodeId);
    return slot != 0 ? muiTreeAt(&context->tree, slot)->hostKey : 0;
}
