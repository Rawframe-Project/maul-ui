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
#include "maul-ui/transition.h"

#include <stdbool.h>

// A spec a variant gives a set of properties.
typedef struct muiTransitionBinding
{
    muiPropertyMask mask;
    muiTransitionId transition;
} muiTransitionBinding;

// Values for the properties a mask names, the other fields unused, and
// the transitions given to properties, no property in two bindings.
typedef struct muiPropertySet
{
    muiPropertyMask mask;
    muiLayoutStyle values;
    muiTransitionBinding bindings[MUI_MAX_VARIANT_TRANSITIONS];
    uint32_t bindingCount;
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

// The values a property moves through, in out: one for a number, two
// (scale, offset) for a Scale+Offset dimension. Returns how many; 0 for
// a value that cannot move, an enumerator or an automatic dimension.
uint32_t muiPropertyChannels(const muiLayoutStyle* style, muiProperty property, float out[2]);

// Writes a property's channels, held to the values the property allows.
void muiSetPropertyChannels(muiLayoutStyle* style, muiProperty property, const float values[2]);

#endif // MAUL_UI_SRC_PROPERTY_H
