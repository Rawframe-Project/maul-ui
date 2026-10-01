// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Properties as data: each one's place in muiLayoutStyle or
// muiVisualStyle, its kind and the values it allows, so that checking,
// applying and comparing a set of them is one loop over a mask.

#ifndef MAUL_UI_SRC_PROPERTY_H
#define MAUL_UI_SRC_PROPERTY_H

#include "maul-ui/layout.h"
#include "maul-ui/style.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <stdbool.h>

// Every value a style can set.
typedef struct muiStyleValues
{
    muiLayoutStyle layout;
    muiVisualStyle visual;
} muiStyleValues;

// Values where they live: a node keeps its two structs apart. A pointer
// may be NULL when the mask a call takes names none of its properties.
typedef struct muiValuesRef
{
    muiLayoutStyle* layout;
    muiVisualStyle* visual;
} muiValuesRef;

typedef struct muiConstValuesRef
{
    const muiLayoutStyle* layout;
    const muiVisualStyle* visual;
} muiConstValuesRef;

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
    muiStyleValues values;
    muiTransitionBinding bindings[MUI_MAX_VARIANT_TRANSITIONS];
    uint32_t bindingCount;
} muiPropertySet;

static inline muiValuesRef muiRefOf(muiStyleValues* values)
{
    return (muiValuesRef){&values->layout, &values->visual};
}

static inline muiConstValuesRef muiConstRefOf(const muiStyleValues* values)
{
    return (muiConstValuesRef){&values->layout, &values->visual};
}

static inline muiConstValuesRef muiConstRef(muiValuesRef values)
{
    return (muiConstValuesRef){values.layout, values.visual};
}

// CSS's initial values, which muiDefaultLayoutStyle returns, and the
// visual defaults muiDefaultVisualStyle returns.
const muiLayoutStyle* muiLayoutDefaults(void);
const muiVisualStyle* muiVisualDefaults(void);

// Whether every property mask names has a value in values it allows, and
// mask names only known properties.
bool muiArePropertiesValid(muiConstValuesRef values, muiPropertyMask mask);

// Copies the properties mask names from source to target.
void muiApplyProperties(muiValuesRef target, muiConstValuesRef source, muiPropertyMask mask);

// Whether a property among mask has different values in a and b.
bool muiDoPropertiesDiffer(muiConstValuesRef a, muiConstValuesRef b, muiPropertyMask mask);

// The values a property moves through, in out: one for a number, two
// (scale, offset) for a Scale+Offset dimension or radius. Returns how
// many; 0 for a value that cannot move: an enumerator, an automatic
// dimension, or (until they interpolate in Oklab) a color.
uint32_t muiPropertyChannels(muiConstValuesRef values, muiProperty property, float out[2]);

// Writes a property's channels, held to the values the property allows.
void muiSetPropertyChannels(muiValuesRef values, muiProperty property, const float channels[2]);

#endif // MAUL_UI_SRC_PROPERTY_H
