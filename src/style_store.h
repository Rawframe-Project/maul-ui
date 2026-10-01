// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What style keeps: classes as property sets per variant, node types as
// class lists, and per node its classes, type, states and which of its
// properties are written directly (record mui-0004). Property sets live
// in a pool of their own so that a class pays only for the variants it
// uses.

#ifndef MAUL_UI_SRC_STYLE_STORE_H
#define MAUL_UI_SRC_STYLE_STORE_H

#include "pool.h"
#include "property.h"

#include "maul-ui/style.h"

#include <stdbool.h>

enum
{
    // The variants a class can have: its base, its states and its
    // conditions.
    MUI_VARIANT_SLOTS = mui_variantCondition0 + MUI_MAX_CONDITIONS
};

typedef struct muiStyleClass
{
    // The property set of each variant, a slot of the set pool; 0 for
    // none.
    uint32_t sets[MUI_VARIANT_SLOTS];
    muiCondition conditions[MUI_MAX_CONDITIONS];
    uint32_t conditionCount;
} muiStyleClass;

typedef struct muiClassList
{
    muiStyleId classes[MUI_MAX_CLASSES];
    uint32_t count;
} muiClassList;

typedef struct muiNodeStyle
{
    muiClassList classes;
    muiNodeTypeId type;
    // The properties written directly, whose values are the node's
    // resolved ones.
    muiPropertyMask direct;
    muiState states;
} muiNodeStyle;

typedef struct muiStyleStore
{
    muiPool classPool;
    muiStyleClass* classes;
    muiPool typePool;
    muiClassList* types;
    muiPool setPool;
    muiPropertySet* sets;
    // Per node, parallel to the node store's slots.
    muiNodeStyle* nodes;
} muiStyleStore;

// Whether classes and count make a list a node or a type may hold.
static inline bool muiIsClassListValid(const muiStyleId* classes, uint32_t count)
{
    return count <= MUI_MAX_CLASSES && (classes != nullptr || count == 0);
}

static inline void muiSetClassList(muiClassList* list, const muiStyleId* classes, uint32_t count)
{
    *list = (muiClassList){.count = count};
    for (uint32_t i = 0; i < count; i++)
    {
        list->classes[i] = classes[i];
    }
}

#endif // MAUL_UI_SRC_STYLE_STORE_H
