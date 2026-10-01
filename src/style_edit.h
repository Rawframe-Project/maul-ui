// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the functions that edit classes share: resolving a class for an
// edit, its variants and their property sets, and restyling every node
// after a change.

#ifndef MAUL_UI_SRC_STYLE_EDIT_H
#define MAUL_UI_SRC_STYLE_EDIT_H

#include "context.h"
#include "style_store.h"

#include <stdbool.h>

// Counts a host edit of style and requests the style of every node.
void muiRestyleAll(muiContext* context);

// The slot of a live class for an edit, or 0 with the status in
// statusOut: misuse for the null id or an edit from a measure function,
// stale for a gone class.
uint32_t muiResolveClassEdit(muiContext* context, muiStyleId styleId, muiResult* statusOut);

// Whether variant is the base, a state or one of the class's conditions.
bool muiHasVariant(const muiStyleClass* class, muiVariant variant);

// The property set of a class's variant, made empty if it has none, or
// NULL when the context has no set left.
muiPropertySet* muiTakeVariantSet(muiStyleStore* store, muiStyleClass* class, muiVariant variant);

// Gives a variant's set back when it neither sets a value nor names a
// transition.
void muiReleaseEmptySet(muiStyleStore* store, muiStyleClass* class, muiVariant variant);

#endif // MAUL_UI_SRC_STYLE_EDIT_H
