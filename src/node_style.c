// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over a node's style: its type, classes and
// states, which the next layout resolves, and its direct writes, which
// take effect at once because nothing can win over them.

#include "animation.h"
#include "context.h"
#include "layout_node.h"
#include "pool.h"
#include "property.h"
#include "style_store.h"
#include "tree.h"

#include "maul-ui/style.h"
#include "maul-ui/visual.h"

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

// Writes the properties mask names, within allowed, directly: at once,
// stopping their transitions; a layout one lays the node out again, a
// visual one paints it again.
static muiResult SetDirect(muiContext* context, muiNodeId nodeId, muiConstValuesRef values,
                           muiPropertyMask mask, muiPropertyMask allowed)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if ((mask & ~allowed) != 0 || !muiArePropertiesValid(values, mask))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    const muiMotion motion = {&context->animations, context->layout, context->visual,
                              context->style.nodes, &context->tree};
    for (uint32_t p = 0; p < mui_propertyCount; p++)
    {
        if ((mask & MUI_PROPERTY_BIT(p)) != 0)
        {
            muiStopAnimation(&motion, slot, (muiProperty)p);
        }
    }
    muiLayoutNode* layout = &context->layout[slot - 1];
    muiApplyProperties((muiValuesRef){&layout->style, &context->visual[slot - 1]}, values, mask);
    muiSyncLayoutNode(layout);
    context->style.nodes[slot - 1].direct |= mask;
    context->style.nodes[slot - 1].edited = true;
    if ((mask & MUI_LAYOUT_PROPERTIES) != 0)
    {
        muiTreeMarkLayout(&context->tree, slot);
    }
    if ((mask & MUI_VISUAL_PROPERTIES) != 0)
    {
        muiTreeMark(&context->tree, slot, mui_stagePaint);
    }
    return status;
}

muiResult muiNode_SetLayoutValues(muiContext* context, muiNodeId nodeId,
                                  const muiLayoutStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetDirect(context, nodeId, (muiConstValuesRef){values, nullptr}, mask,
                     MUI_LAYOUT_PROPERTIES);
}

muiResult muiNode_SetVisualValues(muiContext* context, muiNodeId nodeId,
                                  const muiVisualStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetDirect(context, nodeId, (muiConstValuesRef){nullptr, values}, mask,
                     MUI_VISUAL_PROPERTIES);
}

muiResult muiNode_GetVisualStyle(const muiContext* context, muiNodeId nodeId,
                                 muiVisualStyle* valuesOut)
{
    if (context == nullptr || valuesOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *valuesOut = context->visual[slot - 1];
    return mui_success;
}

muiResult muiNode_ResetProperties(muiContext* context, muiNodeId nodeId, muiPropertyMask mask)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if ((mask & ~MUI_ALL_PROPERTIES) != 0)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        context->style.nodes[slot - 1].direct &= ~mask;
        context->style.nodes[slot - 1].edited = true;
        // Its classes may name none of them: the defaults they go back to.
        context->style.reach |= mask;
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
    return status;
}

muiPropertyMask muiNode_GetDirectProperties(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 ? context->style.nodes[slot - 1].direct : 0;
}
