// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "style_edit.h"

#include "pool.h"
#include "tree.h"

void muiRestyleAll(muiContext* context)
{
    context->styleEdits++;
    muiTreeMarkAll(&context->tree, mui_stageStyle);
}

uint32_t muiResolveClassEdit(muiContext* context, muiStyleId styleId, muiResult* statusOut)
{
    if (styleId.index1 == 0 || muiIsMeasuring(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiPoolResolve(&context->style.classPool, styleId.index1, styleId.generation);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

bool muiHasVariant(const muiStyleClass* class, muiVariant variant)
{
    return variant < mui_variantCondition0 ||
           (uint32_t)(variant - mui_variantCondition0) < class->conditionCount;
}

muiPropertySet* muiTakeVariantSet(muiStyleStore* store, muiStyleClass* class, muiVariant variant)
{
    uint32_t* set = &class->sets[variant];
    if (*set == 0)
    {
        *set = muiPoolTake(&store->setPool);
        if (*set == 0)
        {
            return nullptr;
        }
        store->sets[*set - 1] = (muiPropertySet){0};
    }
    return &store->sets[*set - 1];
}

void muiDropTokenNames(muiStyleStore* store, muiPropertySet* set, muiPropertyMask mask)
{
    if ((set->tokenMask & mask) == 0)
    {
        return;
    }
    uint32_t* link = &set->firstTokenName;
    while (*link != 0)
    {
        uint32_t name = *link;
        if ((mask & MUI_PROPERTY_BIT(store->names[name - 1].property)) != 0)
        {
            *link = store->names[name - 1].next;
            muiPoolGive(&store->namePool, name);
        }
        else
        {
            link = &store->names[name - 1].next;
        }
    }
    set->tokenMask &= ~mask;
}

void muiFreeSet(muiStyleStore* store, uint32_t set)
{
    muiDropTokenNames(store, &store->sets[set - 1], MUI_ALL_PROPERTIES);
    muiPoolGive(&store->setPool, set);
}

void muiReleaseEmptySet(muiStyleStore* store, muiStyleClass* class, muiVariant variant)
{
    uint32_t* set = &class->sets[variant];
    if (*set != 0 && store->sets[*set - 1].mask == 0 && store->sets[*set - 1].tokenMask == 0 &&
        store->sets[*set - 1].bindingCount == 0)
    {
        muiFreeSet(store, *set);
        *set = 0;
    }
}
