// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over a node's style: its type, classes and
// states, which the next layout resolves, and its direct writes, which
// take effect at once because nothing can win over them.

#include "context.h"
#include "layout_node.h"
#include "pool.h"
#include "property.h"
#include "style_store.h"
#include "tree.h"

#include "maul-ui/style.h"

// The states muiState names.
#define KNOWN_STATES 0x7Fu

muiResult muiNode_SetType(muiContext* context, muiNodeId nodeId, muiNodeTypeId typeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    if (typeId.index1 != 0 &&
        muiPoolResolve(&context->style.typePool, typeId.index1, typeId.generation) == 0)
    {
        return mui_errorStale;
    }
    context->style.nodes[slot - 1].type = typeId;
    context->style.nodes[slot - 1].edited = true;
    muiTreeMark(&context->tree, slot, mui_stageStyle);
    return mui_success;
}

muiResult muiNode_SetClasses(muiContext* context, muiNodeId nodeId, const muiStyleId* classes,
                             uint32_t count)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!muiIsClassListValid(classes, count))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        muiSetClassList(&context->style.nodes[slot - 1].classes, classes, count);
        context->style.nodes[slot - 1].edited = true;
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
    return status;
}

muiResult muiNode_SetStates(muiContext* context, muiNodeId nodeId, muiState states)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if ((states & ~KNOWN_STATES) != 0)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0 && context->style.nodes[slot - 1].states != states)
    {
        context->style.nodes[slot - 1].states = states;
        context->style.nodes[slot - 1].edited = true;
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
    return status;
}

muiState muiNode_GetStates(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 ? context->style.nodes[slot - 1].states : 0;
}

muiResult muiNode_SetLayoutValues(muiContext* context, muiNodeId nodeId,
                                  const muiLayoutStyle* values, muiPropertyMask mask)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (values == nullptr || !muiArePropertiesValid(values, mask))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        muiLayoutNode* layout = &context->layout[slot - 1];
        muiApplyProperties(&layout->style, values, mask);
        muiSyncLayoutNode(layout);
        context->style.nodes[slot - 1].direct |= mask;
        context->style.nodes[slot - 1].edited = true;
        muiTreeMarkLayout(&context->tree, slot);
    }
    return status;
}

muiResult muiNode_ResetProperties(muiContext* context, muiNodeId nodeId, muiPropertyMask mask)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if ((mask & ~MUI_LAYOUT_PROPERTIES) != 0)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        context->style.nodes[slot - 1].direct &= ~mask;
        context->style.nodes[slot - 1].edited = true;
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
    return status;
}

muiPropertyMask muiNode_GetDirectProperties(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 ? context->style.nodes[slot - 1].direct : 0;
}
