// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The property table. Rows follow the muiProperty numbering, which a
// static assertion ties to the table's length.

#include "property.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

typedef uint8_t Kind;

enum
{
    // A muiDimension.
    kindDimension,
    // A float: finite, finite and at least 0, or from 0 to 1.
    kindFinite,
    kindLength,
    kindFraction,
    // A uint8_t enumerator from low to high.
    kindEnum,
};

typedef struct Row
{
    uint16_t offset;
    // The field's size in bytes.
    uint8_t size;
    Kind kind;
    // For kindEnum, the values allowed.
    uint8_t low;
    uint8_t high;
} Row;

#define FIELD(field, type)     (uint16_t)offsetof(muiLayoutStyle, field), (uint8_t)sizeof(type)
#define DIMENSION(field)       {FIELD(field, muiDimension), kindDimension, 0, 0}
#define NUMBER(field, kind)    {FIELD(field, float), kind, 0, 0}
#define ENUM(field, low, high) {FIELD(field, uint8_t), kindEnum, low, high}

static const Row s_rows[] = {
    DIMENSION(sizing.width),
    DIMENSION(sizing.height),
    DIMENSION(sizing.minWidth),
    DIMENSION(sizing.minHeight),
    DIMENSION(sizing.maxWidth),
    DIMENSION(sizing.maxHeight),
    NUMBER(sizing.aspectRatio, kindLength),
    ENUM(container.direction, mui_flexRow, mui_flexColumnReverse),
    ENUM(container.wrap, mui_wrapNone, mui_wrapReverse),
    ENUM(container.justify, mui_justifyStart, mui_justifySpaceEvenly),
    // A container's alignment cannot be automatic.
    ENUM(container.alignItems, mui_alignStretch, mui_alignCenter),
    ENUM(container.alignContent, mui_alignContentStretch, mui_alignContentSpaceEvenly),
    NUMBER(container.rowGap, kindLength),
    NUMBER(container.columnGap, kindLength),
    NUMBER(item.grow, kindLength),
    NUMBER(item.shrink, kindLength),
    DIMENSION(item.basis),
    ENUM(item.alignSelf, mui_alignAuto, mui_alignCenter),
    NUMBER(margin.start, kindFinite),
    NUMBER(margin.end, kindFinite),
    NUMBER(margin.top, kindFinite),
    NUMBER(margin.bottom, kindFinite),
    ENUM(marginAuto, 0, mui_edgeStart | mui_edgeEnd | mui_edgeTop | mui_edgeBottom),
    NUMBER(border.start, kindLength),
    NUMBER(border.end, kindLength),
    NUMBER(border.top, kindLength),
    NUMBER(border.bottom, kindLength),
    NUMBER(padding.start, kindLength),
    NUMBER(padding.end, kindLength),
    NUMBER(padding.top, kindLength),
    NUMBER(padding.bottom, kindLength),
    ENUM(placement.position, mui_positionFlow, mui_positionAbsolute),
    DIMENSION(placement.inset.start),
    DIMENSION(placement.inset.end),
    DIMENSION(placement.inset.top),
    DIMENSION(placement.inset.bottom),
    NUMBER(placement.anchorX, kindFraction),
    NUMBER(placement.anchorY, kindFraction),
    ENUM(textDirection, mui_textInherit, mui_textRightToLeft),
    ENUM(content, mui_contentNone, mui_contentHost),
};

static_assert(sizeof s_rows / sizeof s_rows[0] == mui_propertyCount, "one row per property");
static_assert(sizeof(muiDimension) <= UINT8_MAX, "sizes fit a row");
static_assert(MUI_LAYOUT_PROPERTIES == (((muiPropertyMask)1 << mui_propertyCount) - 1),
              "the layout mask names every property");

static const muiLayoutStyle s_defaults = {
    .container = {.direction = mui_flexRow,
                  .wrap = mui_wrapNone,
                  .justify = mui_justifyStart,
                  .alignItems = mui_alignStretch,
                  .alignContent = mui_alignContentStretch},
    .item = {.shrink = 1.0f, .alignSelf = mui_alignAuto},
};

const muiLayoutStyle* muiLayoutDefaults(void)
{
    return &s_defaults;
}

// The index of the lowest set bit of a mask that is not 0, by a de Bruijn
// sequence: portable, and the same on every compiler.
static uint32_t LowestBit(muiPropertyMask mask)
{
    static const uint8_t index[64] = {
        0,  1,  48, 2,  57, 49, 28, 3,  61, 58, 50, 42, 38, 29, 17, 4,  62, 55, 59, 36, 53, 51,
        43, 22, 45, 39, 33, 30, 24, 18, 12, 5,  63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21,
        44, 32, 23, 11, 46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9,  13, 8,  7,  6};
    return index[((mask & (~mask + 1)) * 0x03F79D71B4CB0A89ull) >> 58];
}

static const void* At(const muiLayoutStyle* style, const Row* row)
{
    return (const unsigned char*)style + row->offset;
}

static void* AtMutable(muiLayoutStyle* style, const Row* row)
{
    return (unsigned char*)style + row->offset;
}

static float NumberAt(const muiLayoutStyle* style, const Row* row)
{
    float value = 0.0f;
    memcpy(&value, At(style, row), sizeof value);
    return value;
}

static muiDimension DimensionAt(const muiLayoutStyle* style, const Row* row)
{
    muiDimension value = {0};
    memcpy(&value, At(style, row), sizeof value);
    return value;
}

static bool IsValid(const muiLayoutStyle* values, const Row* row)
{
    switch (row->kind)
    {
    case kindDimension:
    {
        muiDimension value = DimensionAt(values, row);
        return value.kind <= mui_dimensionValue && isfinite(value.scale) && isfinite(value.offset);
    }
    case kindFinite:
        return isfinite(NumberAt(values, row));
    case kindLength:
    {
        float value = NumberAt(values, row);
        return isfinite(value) && value >= 0.0f;
    }
    case kindFraction:
    {
        float value = NumberAt(values, row);
        return value >= 0.0f && value <= 1.0f;
    }
    default:
    {
        uint8_t value = *(const uint8_t*)At(values, row);
        return value >= row->low && value <= row->high;
    }
    }
}

static bool AreEqual(const muiLayoutStyle* a, const muiLayoutStyle* b, const Row* row)
{
    if (row->kind == kindDimension)
    {
        // Compared by field: a dimension has padding bytes.
        muiDimension x = DimensionAt(a, row);
        muiDimension y = DimensionAt(b, row);
        return x.kind == y.kind && x.scale == y.scale && x.offset == y.offset;
    }
    return memcmp(At(a, row), At(b, row), row->size) == 0;
}

bool muiArePropertiesValid(const muiLayoutStyle* values, muiPropertyMask mask)
{
    if ((mask & ~MUI_LAYOUT_PROPERTIES) != 0)
    {
        return false;
    }
    for (muiPropertyMask left = mask; left != 0; left &= left - 1)
    {
        if (!IsValid(values, &s_rows[LowestBit(left)]))
        {
            return false;
        }
    }
    return true;
}

void muiApplyProperties(muiLayoutStyle* target, const muiLayoutStyle* source, muiPropertyMask mask)
{
    for (muiPropertyMask left = mask; left != 0; left &= left - 1)
    {
        const Row* row = &s_rows[LowestBit(left)];
        // Constant sizes, so each copy compiles to a move.
        switch (row->kind)
        {
        case kindDimension:
            memcpy(AtMutable(target, row), At(source, row), sizeof(muiDimension));
            break;
        case kindEnum:
            memcpy(AtMutable(target, row), At(source, row), sizeof(uint8_t));
            break;
        default:
            memcpy(AtMutable(target, row), At(source, row), sizeof(float));
            break;
        }
    }
}

bool muiDoPropertiesDiffer(const muiLayoutStyle* a, const muiLayoutStyle* b, muiPropertyMask mask)
{
    for (muiPropertyMask left = mask; left != 0; left &= left - 1)
    {
        if (!AreEqual(a, b, &s_rows[LowestBit(left)]))
        {
            return true;
        }
    }
    return false;
}

uint32_t muiPropertyChannels(const muiLayoutStyle* style, muiProperty property, float out[2])
{
    const Row* row = &s_rows[property];
    switch (row->kind)
    {
    case kindEnum:
        return 0;
    case kindDimension:
    {
        muiDimension value = DimensionAt(style, row);
        if (value.kind != mui_dimensionValue)
        {
            return 0;
        }
        out[0] = value.scale;
        out[1] = value.offset;
        return 2;
    }
    default:
        out[0] = NumberAt(style, row);
        return 1;
    }
}

void muiSetPropertyChannels(muiLayoutStyle* style, muiProperty property, const float values[2])
{
    const Row* row = &s_rows[property];
    void* at = AtMutable(style, row);
    switch (row->kind)
    {
    case kindEnum:
        break;
    case kindDimension:
    {
        const muiDimension value = {values[0], values[1], mui_dimensionValue};
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindLength:
    {
        // A spring can overshoot below 0.
        const float value = fmaxf(values[0], 0.0f);
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindFraction:
    {
        const float value = fminf(fmaxf(values[0], 0.0f), 1.0f);
        memcpy(at, &value, sizeof value);
        break;
    }
    default:
        memcpy(at, &values[0], sizeof values[0]);
        break;
    }
}
