// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over style classes and node types. Any change to
// one restyles every node at the next layout (record mui-0004): classes
// change rarely, as a theme does, and no index from a class to its nodes
// has to be kept.

#include "maul-ui/style.h"

#include "condition.h"
#include "context.h"
#include "pool.h"
#include "property.h"
#include "style_edit.h"
#include "style_store.h"
#include "tree.h"

// The condition of a variant that has one, or NULL.
static const muiCondition* ConditionOf(const muiStyleClass* class, muiVariant variant)
{
    return variant >= mui_variantCondition0 && muiHasVariant(class, variant)
               ? &class->conditions[variant - mui_variantCondition0]
               : nullptr;
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
    // No node can list a class that did not exist, so none restyles.
    *styleIdOut = (muiStyleId){slot, muiPoolGeneration(&store->classPool, slot)};
    return mui_success;
}

muiResult muiDestroyStyle(muiContext* context, muiStyleId styleId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    for (uint32_t v = 0; v < MUI_VARIANT_SLOTS; v++)
    {
        uint32_t set = store->classes[slot - 1].sets[v];
        if (set != 0)
        {
            muiPoolGive(&store->setPool, set);
        }
    }
    muiPoolGive(&store->classPool, slot);
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_SetLayoutValues(muiContext* context, muiStyleId styleId, muiVariant variant,
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
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    muiStyleClass* class = &store->classes[slot - 1];
    const muiCondition* condition = ConditionOf(class, variant);
    if (!muiHasVariant(class, variant) ||
        (condition != nullptr && (mask & muiForbiddenProperties(condition)) != 0))
    {
        return muiRefuse(context);
    }
    muiPropertySet* target = muiTakeVariantSet(store, class, variant);
    if (target == nullptr)
    {
        return mui_errorCapacity;
    }
    muiApplyProperties(&target->values, values, mask);
    target->mask |= mask;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_ResetProperties(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   muiPropertyMask mask)
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
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    if (!muiHasVariant(&store->classes[slot - 1], variant))
    {
        return muiRefuse(context);
    }
    muiStyleClass* class = &store->classes[slot - 1];
    if (class->sets[variant] != 0)
    {
        store->sets[class->sets[variant] - 1].mask &= ~mask;
        muiReleaseEmptySet(store, class, variant);
    }
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_GetLayoutValues(const muiContext* context, muiStyleId styleId,
                                   muiVariant variant, muiLayoutStyle* valuesOut,
                                   muiPropertyMask* maskOut)
{
    if (context == nullptr || valuesOut == nullptr || maskOut == nullptr || styleId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    const muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolResolve(&store->classPool, styleId.index1, styleId.generation);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    if (!muiHasVariant(&store->classes[slot - 1], variant))
    {
        return mui_errorInvalid;
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

muiResult muiStyle_AddCondition(muiContext* context, muiStyleId styleId,
                                const muiCondition* condition, muiVariant* variantOut)
{
    if (variantOut != nullptr)
    {
        *variantOut = mui_variantBase;
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (condition == nullptr || variantOut == nullptr || !muiIsConditionValid(condition))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleClass* class = &context->style.classes[slot - 1];
    if (class->conditionCount == MUI_MAX_CONDITIONS)
    {
        return mui_errorCapacity;
    }
    class->conditions[class->conditionCount] = *condition;
    *variantOut = (muiVariant)(mui_variantCondition0 + class->conditionCount);
    class->conditionCount++;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_SetCondition(muiContext* context, muiStyleId styleId, muiVariant variant,
                                const muiCondition* condition)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (condition == nullptr || !muiIsConditionValid(condition))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    muiStyleClass* class = &store->classes[slot - 1];
    if (ConditionOf(class, variant) == nullptr)
    {
        return muiRefuse(context);
    }
    uint32_t set = class->sets[variant];
    // The values set already may not be what the new condition reads.
    if (set != 0 && (store->sets[set - 1].mask & muiForbiddenProperties(condition)) != 0)
    {
        return muiRefuse(context);
    }
    class->conditions[variant - mui_variantCondition0] = *condition;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_GetCondition(const muiContext* context, muiStyleId styleId, muiVariant variant,
                                muiCondition* conditionOut)
{
    if (context == nullptr || conditionOut == nullptr || styleId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    const muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolResolve(&store->classPool, styleId.index1, styleId.generation);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiCondition* condition = ConditionOf(&store->classes[slot - 1], variant);
    if (condition == nullptr)
    {
        return mui_errorInvalid;
    }
    *conditionOut = *condition;
    return mui_success;
}

muiResult muiStyle_ClearConditions(muiContext* context, muiStyleId styleId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    muiStyleClass* class = &store->classes[slot - 1];
    for (uint32_t i = 0; i < class->conditionCount; i++)
    {
        uint32_t* set = &class->sets[mui_variantCondition0 + i];
        if (*set != 0)
        {
            muiPoolGive(&store->setPool, *set);
            *set = 0;
        }
    }
    class->conditionCount = 0;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiSetContextEnvironment(muiContext* context, const muiEnvironment* environment)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (environment == nullptr || !muiIsEnvironmentValid(environment) || muiIsMeasuring(context))
    {
        return muiRefuse(context);
    }
    const muiEnvironment* current = &context->environment;
    if (current->viewport != environment->viewport || current->input != environment->input ||
        current->textScale != environment->textScale ||
        current->reducedMotion != environment->reducedMotion)
    {
        context->environment = *environment;
        muiRestyleAll(context);
    }
    return mui_success;
}

muiEnvironment muiGetContextEnvironment(const muiContext* context)
{
    return context != nullptr ? context->environment : muiDefaultEnvironment();
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
    // No node can have a type that did not exist, so none restyles.
    *typeIdOut = (muiNodeTypeId){slot, muiPoolGeneration(&store->typePool, slot)};
    return mui_success;
}

// The slot of a live node type for an edit, or 0 with the status in
// statusOut.
static uint32_t ResolveTypeEdit(muiContext* context, muiNodeTypeId typeId, muiResult* statusOut)
{
    if (typeId.index1 == 0 || muiIsMeasuring(context))
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
        muiRestyleAll(context);
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
        muiRestyleAll(context);
    }
    return status;
}
