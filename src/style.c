// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over style classes and node types. Any change to
// one restyles every node at the next layout (record mui-0004): classes
// change rarely, as a theme does, and no index from a class to its nodes
// has to be kept.

#include "maul-ui/style.h"

#include "context.h"
#include "pool.h"
#include "property.h"
#include "style_store.h"
#include "tree.h"

static void RestyleAll(muiContext* context)
{
    muiTreeMarkAll(&context->tree, mui_stageStyle);
}

// Whether a call that edits style must be refused outright.
static bool IsEditRefused(const muiContext* context, uint32_t index1)
{
    return index1 == 0 || muiIsMeasuring(context);
}

// The slot of a live class for an edit, or 0 with the status in statusOut.
static uint32_t ResolveClassEdit(muiContext* context, muiStyleId styleId, muiResult* statusOut)
{
    if (IsEditRefused(context, styleId.index1))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiPoolResolve(&context->style.classPool, styleId.index1, styleId.generation);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

muiResult muiCreateStyle(muiContext* context, muiStyleId* styleIdOut)
{
    if (styleIdOut != nullptr)
    {
        *styleIdOut = (muiStyleId){0, 0};
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (styleIdOut == nullptr || muiIsMeasuring(context))
    {
        return muiRefuse(context);
    }
    muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolTake(&store->classPool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    store->classes[slot - 1] = (muiStyleClass){0};
    *styleIdOut = (muiStyleId){slot, muiPoolGeneration(&store->classPool, slot)};
    RestyleAll(context);
    return mui_success;
}

muiResult muiDestroyStyle(muiContext* context, muiStyleId styleId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    for (uint32_t v = 0; v < mui_variantCount; v++)
    {
        uint32_t set = store->classes[slot - 1].sets[v];
        if (set != 0)
        {
            muiPoolGive(&store->setPool, set);
        }
    }
    muiPoolGive(&store->classPool, slot);
    RestyleAll(context);
    return mui_success;
}

muiResult muiStyle_SetLayoutValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   const muiLayoutStyle* values, muiPropertyMask mask)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (values == nullptr || variant >= mui_variantCount || !muiArePropertiesValid(values, mask))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    uint32_t* set = &store->classes[slot - 1].sets[variant];
    if (*set == 0)
    {
        *set = muiPoolTake(&store->setPool);
        if (*set == 0)
        {
            return mui_errorCapacity;
        }
        store->sets[*set - 1] = (muiPropertySet){0};
    }
    muiPropertySet* target = &store->sets[*set - 1];
    muiApplyProperties(&target->values, values, mask);
    target->mask |= mask;
    RestyleAll(context);
    return mui_success;
}

muiResult muiStyle_ResetProperties(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   muiPropertyMask mask)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (variant >= mui_variantCount || (mask & ~MUI_LAYOUT_PROPERTIES) != 0)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    uint32_t* set = &store->classes[slot - 1].sets[variant];
    if (*set != 0)
    {
        store->sets[*set - 1].mask &= ~mask;
        // A variant with nothing left gives its set back.
        if (store->sets[*set - 1].mask == 0)
        {
            muiPoolGive(&store->setPool, *set);
            *set = 0;
        }
    }
    RestyleAll(context);
    return mui_success;
}

muiResult muiStyle_GetLayoutValues(const muiContext* context, muiStyleId styleId,
                                   muiVariant variant, muiLayoutStyle* valuesOut,
                                   muiPropertyMask* maskOut)
{
    if (context == nullptr || valuesOut == nullptr || maskOut == nullptr || styleId.index1 == 0 ||
        variant >= mui_variantCount)
    {
        return mui_errorInvalid;
    }
    const muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolResolve(&store->classPool, styleId.index1, styleId.generation);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *valuesOut = muiDefaultLayoutStyle();
    *maskOut = 0;
    uint32_t set = store->classes[slot - 1].sets[variant];
    if (set != 0)
    {
        muiApplyProperties(valuesOut, &store->sets[set - 1].values, store->sets[set - 1].mask);
        *maskOut = store->sets[set - 1].mask;
    }
    return mui_success;
}

muiResult muiCreateNodeType(muiContext* context, const muiStyleId* classes, uint32_t count,
                            muiNodeTypeId* typeIdOut)
{
    if (typeIdOut != nullptr)
    {
        *typeIdOut = (muiNodeTypeId){0, 0};
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (typeIdOut == nullptr || !muiIsClassListValid(classes, count) || muiIsMeasuring(context))
    {
        return muiRefuse(context);
    }
    muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolTake(&store->typePool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    muiSetClassList(&store->types[slot - 1], classes, count);
    *typeIdOut = (muiNodeTypeId){slot, muiPoolGeneration(&store->typePool, slot)};
    RestyleAll(context);
    return mui_success;
}

// The slot of a live node type for an edit, or 0 with the status in
// statusOut.
static uint32_t ResolveTypeEdit(muiContext* context, muiNodeTypeId typeId, muiResult* statusOut)
{
    if (IsEditRefused(context, typeId.index1))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiPoolResolve(&context->style.typePool, typeId.index1, typeId.generation);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

muiResult muiDestroyNodeType(muiContext* context, muiNodeTypeId typeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveTypeEdit(context, typeId, &status);
    if (slot != 0)
    {
        muiPoolGive(&context->style.typePool, slot);
        RestyleAll(context);
    }
    return status;
}

muiResult muiNodeType_SetClasses(muiContext* context, muiNodeTypeId typeId,
                                 const muiStyleId* classes, uint32_t count)
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
    uint32_t slot = ResolveTypeEdit(context, typeId, &status);
    if (slot != 0)
    {
        muiSetClassList(&context->style.types[slot - 1], classes, count);
        RestyleAll(context);
    }
    return status;
}
