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

void muiReleaseEmptySet(muiStyleStore* store, muiStyleClass* class, muiVariant variant)
{
    uint32_t* set = &class->sets[variant];
    if (*set != 0 && store->sets[*set - 1].mask == 0 && store->sets[*set - 1].bindingCount == 0)
    {
        muiPoolGive(&store->setPool, *set);
        *set = 0;
    }
}
