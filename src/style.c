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

// Whether variant is the base, a state or one of the class's conditions.
static bool HasVariant(const muiStyleClass* class, muiVariant variant)
{
    return variant < mui_variantCondition0 ||
           (uint32_t)(variant - mui_variantCondition0) < class->conditionCount;
}

// The condition of a variant that has one, or NULL.
static const muiCondition* ConditionOf(const muiStyleClass* class, muiVariant variant)
{
    return variant >= mui_variantCondition0 && HasVariant(class, variant)
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
    uint32_t slot = ResolveClassEdit(context, styleId, &status);
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
    if (values == nullptr || !muiArePropertiesValid(values, mask))
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
    muiStyleClass* class = &store->classes[slot - 1];
    const muiCondition* condition = ConditionOf(class, variant);
    if (!HasVariant(class, variant) ||
        (condition != nullptr && (mask & muiForbiddenProperties(condition)) != 0))
    {
        return muiRefuse(context);
    }
    uint32_t* set = &class->sets[variant];
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
    if ((mask & ~MUI_LAYOUT_PROPERTIES) != 0)
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
    if (!HasVariant(&store->classes[slot - 1], variant))
    {
        return muiRefuse(context);
    }
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
    if (!HasVariant(&store->classes[slot - 1], variant))
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
    uint32_t slot = ResolveClassEdit(context, styleId, &status);
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
    RestyleAll(context);
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
    uint32_t slot = ResolveClassEdit(context, styleId, &status);
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
    RestyleAll(context);
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
    uint32_t slot = ResolveClassEdit(context, styleId, &status);
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
    RestyleAll(context);
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
        RestyleAll(context);
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
