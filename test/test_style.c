// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Style classes, node types, states and direct writes: their checks, and
// the layers resolution applies them in (record mui-0004).

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/exit.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"

#include <math.h>

static const muiNodeId s_nullNode = {0, 0};
static const muiStyleId s_nullStyle = {0, 0};
static const muiNodeTypeId s_nullType = {0, 0};

#define WIDTH         MUI_PROPERTY_BIT(mui_propertyWidth)
#define PADDING_START MUI_PROPERTY_BIT(mui_propertyPaddingStart)

typedef struct Host
{
    muiContext* context;
    uint32_t measured;
    // Results of edits tried from inside the measure function.
    muiResult refused[4];
} Host;

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)nodeId;
    (void)hostKey;
    (void)width;
    (void)height;
    Host* host = user;
    host->measured++;
    return (muiSize){10.0f, 10.0f};
}

static muiSize MeasureAndEdit(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                              muiMeasureAxis height)
{
    Host* host = user;
    muiStyleId style = s_nullStyle;
    muiLayoutStyle values = muiDefaultLayoutStyle();
    host->refused[0] = muiCreateStyle(host->context, &style);
    host->refused[1] = muiNode_SetStates(host->context, nodeId, mui_stateHovered);
    host->refused[2] = muiNode_SetLayoutValues(host->context, nodeId, &values, WIDTH);
    host->refused[3] = muiNode_SetClasses(host->context, nodeId, NULL, 0);
    return Measure(user, nodeId, hostKey, width, height);
}

static muiContext* MakeContextWith(muiLimits limits)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits = limits;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create context");
    return context;
}

static muiContext* MakeContext(void)
{
    return MakeContextWith(muiDefaultContextDef().limits);
}

static muiNodeId MakeNode(muiContext* context)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "create node");
    return node;
}

static muiStyleId MakeStyle(muiContext* context)
{
    muiStyleId style = s_nullStyle;
    CHECK(muiCreateStyle(context, &style) == mui_success, "create style");
    return style;
}

static muiLayoutStyle WithWidth(float width)
{
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    return values;
}

static void SetWidth(muiContext* context, muiStyleId style, muiVariant variant, float width)
{
    muiLayoutStyle values = WithWidth(width);
    CHECK(muiStyle_SetLayoutValues(context, style, variant, &values, WIDTH) == mui_success,
          "set width");
}

static void Compute(muiContext* context, muiNodeId root, Host* host)
{
    muiLayoutInput input = {400.0f, 300.0f, Measure, host, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "compute");
}

// The node's width after a layout of root.
static float WidthAfterLayout(muiContext* context, muiNodeId root, muiNodeId node)
{
    Host host = {.context = context};
    Compute(context, root, &host);
    return muiNode_GetRect(context, node).width;
}

static void TestDefaultLimitsAndTheirChecks(void)
{
    muiLimits limits = muiDefaultContextDef().limits;
    CHECK(limits.styles == 256 && limits.nodeTypes == 64 && limits.propertySets == 1024,
          "default style limits");
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    def.limits.styles = 0x80000000u;
    CHECK(muiCreateContext(&def, &context) == mui_errorInvalid, "styles over 2^31 - 1");
    def = muiDefaultContextDef();
    def.limits.nodeTypes = 0x80000000u;
    CHECK(muiCreateContext(&def, &context) == mui_errorInvalid, "types over 2^31 - 1");
    def = muiDefaultContextDef();
    def.limits.propertySets = 0x80000000u;
    CHECK(muiCreateContext(&def, &context) == mui_errorInvalid, "sets over 2^31 - 1");
    context = MakeContextWith((muiLimits){.nodes = 1});
    muiStyleId style = s_nullStyle;
    CHECK(muiCreateStyle(context, &style) == mui_errorCapacity, "no styles reserved");
    muiNodeTypeId type = s_nullType;
    CHECK(muiCreateNodeType(context, NULL, 0, &type) == mui_errorCapacity, "no types reserved");
    muiDestroyContext(context);
}

static void TestStyleLifetime(void)
{
    muiContext* context = MakeContextWith((muiLimits){.nodes = 1, .styles = 1, .propertySets = 1});
    muiStyleId first = MakeStyle(context);
    muiStyleId second = s_nullStyle;
    CHECK(muiCreateStyle(context, &second) == mui_errorCapacity, "style limit");
    CHECK(second.index1 == 0, "null id on failure");
    CHECK(muiDestroyStyle(context, first) == mui_success, "destroy");
    CHECK(muiDestroyStyle(context, first) == mui_errorStale, "destroyed once");
    muiLayoutStyle values = WithWidth(5.0f);
    muiPropertyMask mask = 0;
    CHECK(muiStyle_SetLayoutValues(context, first, mui_variantBase, &values, WIDTH) ==
              mui_errorStale,
          "set on a gone class");
    CHECK(muiStyle_GetLayoutValues(context, first, mui_variantBase, &values, &mask) ==
              mui_errorStale,
          "read a gone class");
    CHECK(muiStyle_ResetProperties(context, first, mui_variantBase, mui_groupLayout, WIDTH) ==
              mui_errorStale,
          "reset on a gone class");
    second = MakeStyle(context);
    CHECK(second.index1 == first.index1 && second.generation != first.generation,
          "the slot is reused with a new generation");
    CHECK(muiGetContextMisuse(context) == 0, "no misuse so far");
    CHECK(muiDestroyStyle(context, s_nullStyle) == mui_errorInvalid, "null id");
    CHECK(muiCreateStyle(context, NULL) == mui_errorInvalid, "no out pointer");
    CHECK(muiGetContextMisuse(context) == 2, "both counted");
    CHECK(muiCreateStyle(NULL, &second) == mui_errorInvalid, "no context");
    CHECK(muiDestroyStyle(NULL, second) == mui_errorInvalid, "no context to destroy in");
    muiDestroyContext(context);
}

static void TestClassValuesAreCheckedAndRead(void)
{
    muiContext* context = MakeContext();
    muiStyleId style = MakeStyle(context);
    muiLayoutStyle values = muiDefaultLayoutStyle();
    values.padding.start = -1.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, PADDING_START) ==
              mui_errorInvalid,
          "a value the property does not allow");
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values, WIDTH) == mui_success,
          "fields outside the mask are not read");
    values = WithWidth(7.0f);
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantCondition0, &values, WIDTH) ==
              mui_errorInvalid,
          "unknown variant");
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, &values,
                                   MUI_PROPERTY_BIT(mui_propertyScrollAxes + 1)) ==
              mui_errorInvalid,
          "unknown property");
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantBase, NULL, WIDTH) ==
              mui_errorInvalid,
          "no values");
    CHECK(muiStyle_SetLayoutValues(context, s_nullStyle, mui_variantBase, &values, WIDTH) ==
              mui_errorInvalid,
          "null class");
    CHECK(muiStyle_SetLayoutValues(NULL, style, mui_variantBase, &values, WIDTH) ==
              mui_errorInvalid,
          "no context");
    SetWidth(context, style, mui_variantHovered, 7.0f);
    muiLayoutStyle read;
    muiPropertyMask mask = 0;
    CHECK(muiStyle_GetLayoutValues(context, style, mui_variantHovered, &read, &mask) == mui_success,
          "read hovered");
    CHECK(mask == WIDTH && read.sizing.width.offset == 7.0f, "the value and its bit");
    CHECK(read.item.shrink == 1.0f, "defaults for the rest");
    CHECK(muiStyle_GetLayoutValues(context, style, mui_variantPressed, &read, &mask) ==
                  mui_success &&
              mask == 0,
          "an unset variant has nothing");
    CHECK(muiStyle_GetLayoutValues(context, style, mui_variantCondition0, &read, &mask) ==
              mui_errorInvalid,
          "unknown variant read");
    CHECK(muiStyle_GetLayoutValues(context, style, mui_variantBase, NULL, &mask) ==
              mui_errorInvalid,
          "no values out");
    CHECK(muiStyle_GetLayoutValues(context, style, mui_variantBase, &read, NULL) ==
              mui_errorInvalid,
          "no mask out");
    CHECK(muiStyle_GetLayoutValues(context, s_nullStyle, mui_variantBase, &read, &mask) ==
              mui_errorInvalid,
          "null class read");
    CHECK(muiStyle_GetLayoutValues(NULL, style, mui_variantBase, &read, &mask) == mui_errorInvalid,
          "no context read");
    muiDestroyContext(context);
}

static void TestResetGivesSetsBack(void)
{
    muiContext* context = MakeContextWith((muiLimits){.nodes = 1, .styles = 2, .propertySets = 1});
    muiStyleId first = MakeStyle(context);
    muiStyleId second = MakeStyle(context);
    muiLayoutStyle values = WithWidth(3.0f);
    values.padding.start = 2.0f;
    CHECK(muiStyle_SetLayoutValues(context, first, mui_variantBase, &values,
                                   WIDTH | PADDING_START) == mui_success,
          "the one set");
    CHECK(muiStyle_SetLayoutValues(context, second, mui_variantBase, &values, WIDTH) ==
              mui_errorCapacity,
          "no set left");
    CHECK(muiStyle_SetLayoutValues(context, first, mui_variantBase, &values, WIDTH) == mui_success,
          "a variant that has a set needs no other");
    CHECK(muiStyle_ResetProperties(context, first, mui_variantBase, mui_groupLayout, WIDTH) ==
              mui_success,
          "reset one");
    muiLayoutStyle read;
    muiPropertyMask mask = 0;
    CHECK(muiStyle_GetLayoutValues(context, first, mui_variantBase, &read, &mask) == mui_success &&
              mask == PADDING_START,
          "the other stays");
    CHECK(muiStyle_SetLayoutValues(context, second, mui_variantBase, &values, WIDTH) ==
              mui_errorCapacity,
          "still held");
    CHECK(muiStyle_ResetProperties(context, first, mui_variantBase, mui_groupLayout,
                                   PADDING_START) == mui_success,
          "reset the last");
    CHECK(muiStyle_SetLayoutValues(context, second, mui_variantBase, &values, WIDTH) == mui_success,
          "an empty variant gave its set back");
    CHECK(muiStyle_ResetProperties(context, first, mui_variantPressed, mui_groupLayout, WIDTH) ==
              mui_success,
          "resetting an unset variant does nothing");
    CHECK(muiStyle_ResetProperties(context, first, mui_variantCondition0, mui_groupLayout, WIDTH) ==
              mui_errorInvalid,
          "unknown variant reset");
    CHECK(muiStyle_ResetProperties(context, first, mui_variantBase, mui_groupLayout,
                                   MUI_PROPERTY_BIT(mui_propertyScrollAxes + 1)) ==
              mui_errorInvalid,
          "unknown property reset");
    CHECK(muiStyle_ResetProperties(NULL, first, mui_variantBase, mui_groupLayout, WIDTH) ==
              mui_errorInvalid,
          "no context reset");
    CHECK(muiDestroyStyle(context, second) == mui_success, "destroy gives sets back");
    CHECK(muiStyle_SetLayoutValues(context, first, mui_variantHovered, &values, WIDTH) ==
              mui_success,
          "so another variant can have one");
    muiDestroyContext(context);
}

static void TestClassesApplyInOrder(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId narrow = MakeStyle(context);
    muiStyleId wide = MakeStyle(context);
    SetWidth(context, narrow, mui_variantBase, 20.0f);
    SetWidth(context, wide, mui_variantBase, 60.0f);
    muiStyleId order[] = {narrow, wide};
    CHECK(muiNode_SetClasses(context, node, order, 2) == mui_success, "classes");
    CHECK(WidthAfterLayout(context, node, node) == 60.0f, "the later class wins");
    muiStyleId reversed[] = {wide, narrow};
    CHECK(muiNode_SetClasses(context, node, reversed, 2) == mui_success, "reorder");
    CHECK(WidthAfterLayout(context, node, node) == 20.0f, "order is the node's");
    CHECK(muiDestroyStyle(context, narrow) == mui_success, "destroy one");
    CHECK(WidthAfterLayout(context, node, node) == 60.0f, "a gone class is skipped");
    CHECK(muiNode_SetClasses(context, node, NULL, 0) == mui_success, "none");
    CHECK(WidthAfterLayout(context, node, node) == 0.0f, "back to the defaults");
    muiDestroyContext(context);
}

static void TestTypeClassesComeFirst(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId typeClass = MakeStyle(context);
    muiStyleId ownClass = MakeStyle(context);
    SetWidth(context, typeClass, mui_variantBase, 30.0f);
    SetWidth(context, ownClass, mui_variantBase, 40.0f);
    muiNodeTypeId type = s_nullType;
    CHECK(muiCreateNodeType(context, &typeClass, 1, &type) == mui_success, "type");
    CHECK(muiNode_SetType(context, node, type) == mui_success, "set type");
    CHECK(WidthAfterLayout(context, node, node) == 30.0f, "the type's class");
    CHECK(muiNode_SetClasses(context, node, &ownClass, 1) == mui_success, "own class");
    CHECK(WidthAfterLayout(context, node, node) == 40.0f, "the node's own come after");
    CHECK(muiNodeType_SetClasses(context, type, &ownClass, 1) == mui_success, "retype");
    CHECK(muiNode_SetClasses(context, node, &typeClass, 1) == mui_success, "swap");
    CHECK(WidthAfterLayout(context, node, node) == 30.0f, "a type edit restyles its nodes");
    CHECK(muiDestroyNodeType(context, type) == mui_success, "destroy type");
    CHECK(muiNode_SetClasses(context, node, NULL, 0) == mui_success, "no own classes");
    CHECK(WidthAfterLayout(context, node, node) == 0.0f, "a gone type gives nothing");
    CHECK(muiNode_SetType(context, node, type) == mui_errorStale, "gone type");
    CHECK(muiNode_SetType(context, node, s_nullType) == mui_success, "no type");
    CHECK(muiDestroyNodeType(context, type) == mui_errorStale, "destroyed once");
    CHECK(muiNodeType_SetClasses(context, type, NULL, 0) == mui_errorStale, "retype a gone type");
    muiStyleId many[MUI_MAX_CLASSES + 1] = {0};
    CHECK(muiCreateNodeType(context, many, MUI_MAX_CLASSES + 1, &type) == mui_errorInvalid,
          "too many classes");
    CHECK(muiCreateNodeType(context, NULL, 1, &type) == mui_errorInvalid, "no classes");
    CHECK(muiCreateNodeType(context, NULL, 0, NULL) == mui_errorInvalid, "no out pointer");
    CHECK(muiCreateNodeType(context, many, MUI_MAX_CLASSES, &type) == mui_success,
          "the limit itself");
    CHECK(muiNodeType_SetClasses(context, type, many, MUI_MAX_CLASSES + 1) == mui_errorInvalid,
          "too many to retype");
    CHECK(muiNodeType_SetClasses(context, s_nullType, NULL, 0) == mui_errorInvalid, "null type");
    CHECK(muiDestroyNodeType(context, s_nullType) == mui_errorInvalid, "null type destroy");
    CHECK(muiNode_SetClasses(context, node, many, MUI_MAX_CLASSES + 1) == mui_errorInvalid,
          "too many on a node");
    CHECK(muiNode_SetClasses(context, node, NULL, 1) == mui_errorInvalid, "none given");
    CHECK(muiNode_SetClasses(context, s_nullNode, NULL, 0) == mui_errorInvalid, "null node");
    CHECK(muiNode_SetType(context, s_nullNode, type) == mui_errorInvalid, "null node type");
    CHECK(muiCreateNodeType(NULL, NULL, 0, &type) == mui_errorInvalid, "no context");
    CHECK(muiDestroyNodeType(NULL, type) == mui_errorInvalid, "no context to destroy in");
    CHECK(muiNodeType_SetClasses(NULL, type, NULL, 0) == mui_errorInvalid, "no context retype");
    CHECK(muiNode_SetType(NULL, node, type) == mui_errorInvalid, "no context set type");
    CHECK(muiNode_SetClasses(NULL, node, NULL, 0) == mui_errorInvalid, "no context classes");
    muiDestroyContext(context);
}

static void TestStatesLayerAboveEveryBase(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId first = MakeStyle(context);
    muiStyleId second = MakeStyle(context);
    SetWidth(context, first, mui_variantHovered, 30.0f);
    SetWidth(context, first, mui_variantPressed, 40.0f);
    SetWidth(context, second, mui_variantBase, 20.0f);
    muiStyleId order[] = {first, second};
    CHECK(muiNode_SetClasses(context, node, order, 2) == mui_success, "classes");
    CHECK(WidthAfterLayout(context, node, node) == 20.0f, "no state, the base");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    CHECK(muiNode_GetStates(context, node) == mui_stateHovered, "read back");
    CHECK(WidthAfterLayout(context, node, node) == 30.0f,
          "an earlier class's variant beats a later class's base");
    SetWidth(context, second, mui_variantHovered, 35.0f);
    CHECK(WidthAfterLayout(context, node, node) == 35.0f, "within a state, class order");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered | mui_statePressed) == mui_success,
          "press");
    CHECK(WidthAfterLayout(context, node, node) == 40.0f,
          "a stronger state beats a later class's weaker one");
    // Every state in turn over the one before it.
    const muiState states[] = {mui_stateChecked,      mui_stateSelected, mui_stateFocused,
                               mui_stateFocusVisible, mui_stateHovered,  mui_statePressed,
                               mui_stateDisabled,     mui_stateExiting};
    muiStyleId each = MakeStyle(context);
    CHECK(muiNode_SetClasses(context, node, &each, 1) == mui_success, "one class");
    muiState held = 0;
    for (uint32_t i = 0; i < 8; i++)
    {
        SetWidth(context, each, (muiVariant)(i + 1), 100.0f + (float)i);
        held |= states[i];
        // Exiting is the exits' to set.
        CHECK(
            muiNode_SetStates(context, node, held) == mui_success &&
                (states[i] != mui_stateExiting || muiNode_BeginExit(context, node) == mui_success),
            "add a state");
        CHECK(WidthAfterLayout(context, node, node) == 100.0f + (float)i,
              "each state beats the ones before");
    }
    CHECK(muiNode_CancelExit(context, node) == mui_success &&
              muiNode_SetStates(context, node, mui_stateChecked) == mui_success,
          "only checked");
    CHECK(WidthAfterLayout(context, node, node) == 100.0f, "the variant of the state held");
    CHECK(muiNode_SetStates(context, s_nullNode, 0) == mui_errorInvalid, "null node");
    CHECK(muiNode_SetStates(NULL, node, 0) == mui_errorInvalid, "no context");
    CHECK(muiNode_GetStates(NULL, node) == 0, "no context read");
    CHECK(muiDestroyNode(context, node) == mui_success, "destroy");
    CHECK(muiNode_GetStates(context, node) == 0, "a gone node has none");
    CHECK(muiNode_SetStates(context, node, 0) == mui_errorStale, "a gone node");
    muiDestroyContext(context);
}

static void TestDirectWritesWinUntilReset(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    SetWidth(context, style, mui_variantBase, 20.0f);
    SetWidth(context, style, mui_variantDisabled, 25.0f);
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    CHECK(muiNode_SetStates(context, node, mui_stateDisabled) == mui_success, "disabled");
    muiLayoutStyle values = WithWidth(50.0f);
    CHECK(muiNode_SetLayoutValues(context, node, &values, WIDTH) == mui_success, "direct");
    muiLayoutStyle read;
    CHECK(muiNode_GetLayoutStyle(context, node, &read) == mui_success &&
              read.sizing.width.offset == 50.0f,
          "at once");
    CHECK(muiNode_GetDirectProperties(context, node, mui_groupLayout) == WIDTH, "its bit");
    CHECK(WidthAfterLayout(context, node, node) == 50.0f, "over the strongest state");
    CHECK(muiNode_ResetProperties(context, node, mui_groupLayout, WIDTH) == mui_success, "reset");
    CHECK(muiNode_GetDirectProperties(context, node, mui_groupLayout) == 0, "bit cleared");
    CHECK(WidthAfterLayout(context, node, node) == 25.0f, "the classes again");
    muiLayoutStyle all = muiDefaultLayoutStyle();
    CHECK(muiNode_SetLayoutStyle(context, node, &all) == mui_success, "every property");
    CHECK(muiNode_GetDirectProperties(context, node, mui_groupLayout) == MUI_LAYOUT_PROPERTIES,
          "all direct");
    CHECK(WidthAfterLayout(context, node, node) == 0.0f, "a direct default wins too");
    values.padding.start = -1.0f;
    CHECK(muiNode_SetLayoutValues(context, node, &values, WIDTH | PADDING_START) ==
              mui_errorInvalid,
          "one bad value");
    CHECK(muiNode_GetLayoutStyle(context, node, &read) == mui_success &&
              read.sizing.width.offset == 0.0f,
          "refuses the whole write");
    CHECK(muiNode_SetLayoutValues(context, node, NULL, WIDTH) == mui_errorInvalid, "no values");
    CHECK(muiNode_SetLayoutValues(context, node, &values,
                                  MUI_PROPERTY_BIT(mui_propertyScrollAxes + 1)) == mui_errorInvalid,
          "unknown property");
    CHECK(muiNode_SetLayoutValues(context, s_nullNode, &values, WIDTH) == mui_errorInvalid,
          "null node");
    CHECK(muiNode_SetLayoutValues(NULL, node, &values, WIDTH) == mui_errorInvalid, "no context");
    CHECK(muiNode_ResetProperties(context, node, mui_groupLayout,
                                  MUI_PROPERTY_BIT(mui_propertyScrollAxes + 1)) == mui_errorInvalid,
          "unknown property reset");
    CHECK(muiNode_ResetProperties(context, s_nullNode, mui_groupLayout, WIDTH) == mui_errorInvalid,
          "null reset");
    // Groups past the last, and a bit past the interaction group's.
    const muiPropertyMask pastInteraction = MUI_PROPERTY_BIT(mui_propertyExitLayout + 1);
    CHECK(muiNode_ResetProperties(context, node, 4, WIDTH) == mui_errorInvalid &&
              muiNode_ResetProperties(context, node, mui_groupInteraction, pastInteraction) ==
                  mui_errorInvalid &&
              muiNode_ResetProperties(context, node, mui_groupInteraction, 0) == mui_success,
          "a group with no such property");
    CHECK(muiNode_GetDirectProperties(context, node, 4) == 0, "no group past the last");
    muiStyleId group = s_nullStyle;
    CHECK(muiCreateStyle(context, &group) == mui_success &&
              muiStyle_ResetProperties(context, group, mui_variantBase, 4, WIDTH) ==
                  mui_errorInvalid &&
              muiStyle_ResetProperties(context, group, mui_variantBase, mui_groupInteraction,
                                       pastInteraction) == mui_errorInvalid &&
              muiStyle_ResetProperties(context, group, mui_variantBase, mui_groupVisual,
                                       MUI_PROPERTY_BIT(mui_propertyScaleOriginY + 1)) ==
                  mui_errorInvalid &&
              muiStyle_ResetProperties(context, group, mui_variantBase, mui_groupVisual,
                                       MUI_VISUAL_PROPERTIES) == mui_success,
          "style resets check their group");
    CHECK(muiNode_ResetProperties(NULL, node, mui_groupLayout, WIDTH) == mui_errorInvalid,
          "no context reset");
    CHECK(muiNode_GetDirectProperties(NULL, node, mui_groupLayout) == 0, "no context read");
    CHECK(muiDestroyNode(context, node) == mui_success, "destroy");
    CHECK(muiNode_GetDirectProperties(context, node, mui_groupLayout) == 0, "a gone node has none");
    CHECK(muiNode_ResetProperties(context, node, mui_groupLayout, WIDTH) == mui_errorStale,
          "a gone node reset");
    CHECK(muiNode_SetLayoutValues(context, node, &all, WIDTH) == mui_errorStale, "a gone node");
    CHECK(muiNode_SetClasses(context, node, NULL, 0) == mui_errorStale, "gone node classes");
    CHECK(muiNode_SetType(context, node, s_nullType) == mui_errorStale, "gone node type");
    muiDestroyContext(context);
}

static void TestDirectValuesSurviveAClassWrite(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiStyleId style = MakeStyle(context);
    muiLayoutStyle hovered = WithWidth(25.0f);
    hovered.padding.start = 2.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantHovered, &hovered,
                                   WIDTH | PADDING_START) == mui_success,
          "hovered width and padding");
    CHECK(muiNode_SetClasses(context, node, &style, 1) == mui_success, "class");
    muiLayoutStyle direct = WithWidth(50.0f);
    CHECK(muiNode_SetLayoutValues(context, node, &direct, WIDTH) == mui_success, "direct width");
    CHECK(WidthAfterLayout(context, node, node) == 50.0f, "direct");
    CHECK(muiNode_SetStates(context, node, mui_stateHovered) == mui_success, "hover");
    muiLayoutStyle read;
    CHECK(WidthAfterLayout(context, node, node) == 50.0f, "the class's padding lands");
    CHECK(muiNode_GetLayoutStyle(context, node, &read) == mui_success &&
              read.padding.start == 2.0f && read.sizing.width.offset == 50.0f,
          "beside the direct width, which it does not touch");
    muiDestroyContext(context);
}

static void TestClassValuesReachLayout(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiNodeId root = MakeNode(context);
    muiLayoutStyle box = WithWidth(100.0f);
    box.sizing.height = (muiDimension){0.0f, 50.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutStyle(context, root, &box) == mui_success, "root");
    muiNodeId first = MakeNode(context);
    muiNodeId second = MakeNode(context);
    muiLayoutStyle content = muiDefaultLayoutStyle();
    content.content = mui_contentHost;
    const muiPropertyMask contentBit = MUI_PROPERTY_BIT(mui_propertyContent);
    CHECK(muiNode_SetLayoutValues(context, first, &content, contentBit) == mui_success, "first");
    CHECK(muiNode_SetLayoutValues(context, second, &content, contentBit) == mui_success, "second");
    CHECK(muiNode_InsertChild(context, root, first, s_nullNode) == mui_success, "insert first");
    CHECK(muiNode_InsertChild(context, root, second, s_nullNode) == mui_success, "insert second");
    // An absolute class: the first child leaves the flow, so the second
    // starts the row.
    muiStyleId floating = MakeStyle(context);
    muiLayoutStyle placed = muiDefaultLayoutStyle();
    placed.placement.position = mui_positionAbsolute;
    placed.placement.inset.top = (muiDimension){0.0f, 30.0f, mui_dimensionValue};
    CHECK(muiStyle_SetLayoutValues(context, floating, mui_variantBase, &placed,
                                   MUI_PROPERTY_BIT(mui_propertyPosition) |
                                       MUI_PROPERTY_BIT(mui_propertyInsetTop)) == mui_success,
          "absolute class");
    CHECK(muiNode_SetClasses(context, first, &floating, 1) == mui_success, "class");
    Compute(context, root, &host);
    CHECK(muiNode_GetRect(context, first).y == 30.0f, "placed by its inset");
    CHECK(muiNode_GetRect(context, second).x == 0.0f, "out of the flow");
    // A zero width is a value, not automatic: the kind alone changes.
    muiStyleId collapsed = MakeStyle(context);
    SetWidth(context, collapsed, mui_variantPressed, 0.0f);
    CHECK(muiNode_SetClasses(context, second, &collapsed, 1) == mui_success, "class");
    Compute(context, root, &host);
    CHECK(muiNode_GetRect(context, second).width == 10.0f, "its content's width");
    CHECK(muiNode_SetStates(context, second, mui_statePressed) == mui_success, "press");
    Compute(context, root, &host);
    CHECK(muiNode_GetRect(context, second).width == 0.0f, "a width of 0");
    // A type set after a layout restyles the node.
    muiStyleId wide = MakeStyle(context);
    SetWidth(context, wide, mui_variantBase, 40.0f);
    muiNodeTypeId type = s_nullType;
    CHECK(muiCreateNodeType(context, &wide, 1, &type) == mui_success, "type");
    CHECK(muiNode_SetClasses(context, second, NULL, 0) == mui_success, "no classes");
    Compute(context, root, &host);
    CHECK(muiNode_SetType(context, second, type) == mui_success, "set type");
    Compute(context, root, &host);
    CHECK(muiNode_GetRect(context, second).width == 40.0f, "the type's width");
    muiDestroyContext(context);
}

static void TestEveryKindOfValueIsChecked(void)
{
    muiContext* context = MakeContext();
    muiNodeId node = MakeNode(context);
    muiLayoutStyle bad[5];
    for (int i = 0; i < 5; i++)
    {
        bad[i] = muiDefaultLayoutStyle();
    }
    bad[0].placement.anchorX = 1.5f;
    bad[1].placement.anchorY = -0.5f;
    bad[2].sizing.width = (muiDimension){0.0f, 1.0f, 2};
    bad[3].margin.top = INFINITY;
    bad[4].container.alignItems = mui_alignAuto;
    for (int i = 0; i < 5; i++)
    {
        CHECK(muiNode_SetLayoutStyle(context, node, &bad[i]) == mui_errorInvalid, "refused");
    }
    muiLayoutStyle good = muiDefaultLayoutStyle();
    good.placement.anchorX = 1.0f;
    good.margin.top = -4.0f;
    CHECK(muiNode_SetLayoutStyle(context, node, &good) == mui_success, "the limits themselves");
    muiDestroyContext(context);
}

static void TestOnlyChangedValuesLayOutAgain(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiNodeId root = MakeNode(context);
    muiNodeId leaf = MakeNode(context);
    muiLayoutStyle content = muiDefaultLayoutStyle();
    content.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, leaf, &content, MUI_PROPERTY_BIT(mui_propertyContent)) ==
              mui_success,
          "host content");
    CHECK(muiNode_InsertChild(context, root, leaf, s_nullNode) == mui_success, "insert");
    muiStyleId style = MakeStyle(context);
    muiLayoutStyle padded = muiDefaultLayoutStyle();
    padded.padding.start = 4.0f;
    CHECK(muiStyle_SetLayoutValues(context, style, mui_variantPressed, &padded, PADDING_START) ==
              mui_success,
          "pressed pads");
    CHECK(muiNode_SetClasses(context, leaf, &style, 1) == mui_success, "class");
    Compute(context, root, &host);
    uint32_t first = host.measured;
    CHECK(first > 0, "measured once laid out");
    CHECK(muiNode_SetStates(context, leaf, mui_stateHovered) == mui_success, "hover");
    Compute(context, root, &host);
    CHECK(host.measured == first, "a state with no values lays nothing out");
    muiStyleId unrelated = MakeStyle(context);
    SetWidth(context, unrelated, mui_variantBase, 90.0f);
    Compute(context, root, &host);
    CHECK(host.measured == first, "a class no node uses lays nothing out");
    CHECK(muiNode_SetStates(context, leaf, mui_statePressed) == mui_success, "press");
    Compute(context, root, &host);
    CHECK(host.measured > first, "a changed value lays out again");
    CHECK(muiNode_GetRect(context, leaf).width == 14.0f, "with the padding");
    muiDestroyContext(context);
}

static void TestStyleEditsAreRefusedWhileMeasuring(void)
{
    muiContext* context = MakeContext();
    Host host = {.context = context};
    muiNodeId node = MakeNode(context);
    muiLayoutStyle content = muiDefaultLayoutStyle();
    content.content = mui_contentHost;
    CHECK(muiNode_SetLayoutStyle(context, node, &content) == mui_success, "host content");
    muiLayoutInput input = {100.0f, 100.0f, MeasureAndEdit, &host, 0, NULL};
    CHECK(muiComputeLayout(context, node, &input) == mui_success, "compute");
    for (int i = 0; i < 4; i++)
    {
        CHECK(host.refused[i] == mui_errorInvalid, "refused while measuring");
    }
    muiDestroyContext(context);
}

int main(void)
{
    TestDefaultLimitsAndTheirChecks();
    TestStyleLifetime();
    TestClassValuesAreCheckedAndRead();
    TestResetGivesSetsBack();
    TestClassesApplyInOrder();
    TestTypeClassesComeFirst();
    TestStatesLayerAboveEveryBase();
    TestDirectWritesWinUntilReset();
    TestDirectValuesSurviveAClassWrite();
    TestClassValuesReachLayout();
    TestEveryKindOfValueIsChecked();
    TestOnlyChangedValuesLayOutAgain();
    TestStyleEditsAreRefusedWhileMeasuring();
    return s_failures == 0 ? 0 : 1;
}
