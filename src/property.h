// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Properties as data: each one's place in muiLayoutStyle, its kind and
// the values it allows, so that checking, applying and comparing a set of
// them is one loop over a mask.

#ifndef MAUL_UI_SRC_PROPERTY_H
#define MAUL_UI_SRC_PROPERTY_H

#include "maul-ui/layout.h"
#include "maul-ui/style.h"

#include <stdbool.h>

// Values for the properties a mask names; the other fields are unused.
typedef struct muiPropertySet
{
    muiPropertyMask mask;
    muiLayoutStyle values;
} muiPropertySet;

// CSS's initial values, which muiDefaultLayoutStyle returns.
const muiLayoutStyle* muiLayoutDefaults(void);

// Whether every property mask names has a value in values it allows, and
// mask names only known properties.
bool muiArePropertiesValid(const muiLayoutStyle* values, muiPropertyMask mask);

// Copies the properties mask names from source to target.
void muiApplyProperties(muiLayoutStyle* target, const muiLayoutStyle* source, muiPropertyMask mask);

// Whether a property among mask has different values in a and b.
bool muiDoPropertiesDiffer(const muiLayoutStyle* a, const muiLayoutStyle* b, muiPropertyMask mask);

#endif // MAUL_UI_SRC_PROPERTY_H
