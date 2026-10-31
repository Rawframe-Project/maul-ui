// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Enumerations past their values (Maul conventions, API shape): a kind
// is an int32_t, so every call and def field that takes one refuses -1
// and INT32_MIN, which a check of the form kind <= last would let
// through to a table below its start, and 256, whose low byte is a valid
// value, which a check on a narrower copy would let through. A set of
// bits refuses the bits it does not name.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/popup.h"
#include "maul-ui/range.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/text_style.h"
#include "maul-ui/token.h"
#include "maul-ui/transition.h"
#include "maul-ui/virtual.h"
#include "maul-ui/visual.h"

#include <stdint.h>
#include <string.h>

// Kinds past their values.
static const int32_t s_kinds[] = {-1, INT32_MIN, 256};

// Bits no set names.
static const uint32_t s_bits[] = {0x80000000u, 0xFFFFFFFFu, 0x100u};

#define KINDS for (size_t k = 0; k < sizeof s_kinds / sizeof s_kinds[0]; k++)
#define BITS  for (size_t k = 0; k < sizeof s_bits / sizeof s_bits[0]; k++)

typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId node;
    muiStyleId style;
} Scene;

static Scene MakeScene(void)
{
    Scene scene = {0};
    muiContextDef def = muiDefaultContextDef();
    CHECK(muiCreateContext(&def, &scene.context) == mui_success, "context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    CHECK(muiCreateNode(scene.context, &nodeDef, &scene.root) == mui_success &&
              muiCreateNode(scene.context, &nodeDef, &scene.node) == mui_success &&
              muiNode_InsertChild(scene.context, scene.root, scene.node, (muiNodeId){0, 0}) ==
                  mui_success,
          "nodes");
    muiStyleDef styleDef = muiDefaultStyleDef();
    CHECK(muiCreateStyle(scene.context, &styleDef, &scene.style) == mui_success, "style");
    return scene;
}

// The style's base layout values, valid to set again.
static muiLayoutStyle BaseLayout(const Scene* s)
{
    muiLayoutStyle values = {0};
    muiPropertyMask mask = 0;
    CHECK(muiStyle_GetLayoutValues(s->context, s->style, mui_variantBase, &values, &mask) ==
              mui_success,
          "base layout values");
    return values;
}

static void TestAccess(Scene* s)
{
    KINDS
    {
        const int32_t v = s_kinds[k];
        CHECK(muiNode_SetAccessRole(s->context, s->node, v) == mui_errorInvalid, "a role");
        CHECK(muiNode_SetAccessText(s->context, s->node, v, "a", 1) == mui_errorInvalid, "a text");
        const char* text = NULL;
        size_t length = 0;
        CHECK(muiNode_GetAccessText(s->context, s->node, v, &text, &length) == mui_errorInvalid,
              "a text read");
        CHECK(muiNode_SetAccessRelation(s->context, s->node, v, &s->root, 1) == mui_errorInvalid,
              "a relation");
        muiNodeId targets[1];
        uint32_t count = 0;
        CHECK(muiNode_GetAccessRelation(s->context, s->node, v, targets, 1, &count) ==
                  mui_errorInvalid,
              "a relation read");
        for (int field = 0; field < 6; field++)
        {
            muiAccessValues values = muiDefaultAccessValues();
            int32_t* kinds[6] = {&values.live, &values.popup,   &values.orientation,
                                 &values.sort, &values.invalid, &values.current};
            *kinds[field] = v;
            CHECK(muiNode_SetAccessValues(s->context, s->node, &values) == mui_errorInvalid,
                  "an access value");
        }
        muiAccessRequest request = {0};
        request.action = v;
        request.target = muiNode_GetAccessId(s->node);
        bool handled = true;
        CHECK(muiContext_PerformAccessAction(s->context, &request, &handled) == mui_errorInvalid,
              "an action");
    }
}

static void TestFocusAndInput(Scene* s)
{
    KINDS
    {
        const int32_t v = s_kinds[k];
        CHECK(muiContext_SetFocus(s->context, 0, s->node, v) == mui_errorInvalid, "a focus cause");
        CHECK(muiNode_MoveFocusToward(s->context, s->root, 0, v) == mui_errorInvalid,
              "a direction to move");
        CHECK(muiNode_SetNeighbor(s->context, s->node, v, s->root) == mui_errorInvalid,
              "a neighbor's direction");
        CHECK(muiNode_GetNeighbor(s->context, s->node, v).index1 == 0, "a neighbor read");
        bool handled = true;
        // A code past a usage: 256 is one, so 0x10000 stands for it.
        muiKeyEvent key = {0};
        key.code = v == 256 ? 0x10000 : v;
        key.down = true;
        CHECK(muiNode_RouteKey(s->context, s->root, &key, &handled) == mui_errorInvalid,
              "a key code");
        muiNavigationEvent navigation = {0};
        navigation.action = v;
        CHECK(muiNode_RouteNavigation(s->context, s->root, &navigation, &handled) ==
                  mui_errorInvalid,
              "a navigation");
        for (int field = 0; field < 2; field++)
        {
            muiPointerEvent pointer = {0};
            *(field == 0 ? &pointer.kind : &pointer.action) = v;
            CHECK(muiNode_RoutePointer(s->context, s->root, &pointer) == mui_errorInvalid,
                  "a pointer event");
        }
        muiScrollThumb thumb;
        CHECK(muiNode_GetScrollThumb(s->context, s->node, v, 100.0f, 10.0f, &thumb) ==
                  mui_errorInvalid,
              "a scroll thumb's axis");
        muiValueRange range = muiDefaultValueRange();
        range.axis = v;
        CHECK(muiNode_SetValueRange(s->context, s->node, &range) == mui_errorInvalid,
              "a range's axis");
        muiVirtualList list = muiDefaultVirtualList();
        list.axis = v;
        CHECK(muiNode_SetVirtualList(s->context, s->node, &list) == mui_errorInvalid,
              "a list's axis");
        for (int field = 0; field < 2; field++)
        {
            muiPopup popup = muiDefaultPopup();
            popup.anchor = s->root;
            *(field == 0 ? &popup.side : &popup.align) = v;
            CHECK(muiNode_SetPopup(s->context, s->node, &popup) == mui_errorInvalid, "a popup");
        }
    }
}

// Each enumerated property of a group, set to v through a style and a
// node.
static void TestValues(Scene* s, int32_t v)
{
    static const muiProperty layout[] = {
        mui_propertyFlexDirection, mui_propertyFlexWrap,      mui_propertyJustify,
        mui_propertyAlignItems,    mui_propertyAlignContent,  mui_propertyAlignSelf,
        mui_propertyPosition,      mui_propertyTextDirection, mui_propertyContent,
        mui_propertyWidth,
    };
    for (size_t i = 0; i < sizeof layout / sizeof layout[0]; i++)
    {
        muiLayoutStyle values = BaseLayout(s);
        int32_t* fields[] = {
            &values.container.direction,  &values.container.wrap,         &values.container.justify,
            &values.container.alignItems, &values.container.alignContent, &values.item.alignSelf,
            &values.placement.position,   &values.textDirection,          &values.content,
            &values.sizing.width.kind,
        };
        *fields[i] = v;
        muiPropertyMask mask = MUI_PROPERTY_BIT(layout[i]);
        CHECK(muiStyle_SetLayoutValues(s->context, s->style, mui_variantBase, &values, mask) ==
                  mui_errorInvalid,
              "a layout value in a style");
        CHECK(muiNode_SetLayoutValues(s->context, s->node, &values, mask) == mui_errorInvalid,
              "a layout value on a node");
    }
    static const muiProperty interaction[] = {mui_propertyHitMode, mui_propertyLayer,
                                              mui_propertyFocusMode, mui_propertyExitLayout};
    for (size_t i = 0; i < sizeof interaction / sizeof interaction[0]; i++)
    {
        muiInteractionStyle values = {0};
        int32_t* fields[] = {&values.hitMode, &values.layer, &values.focusMode, &values.exitLayout};
        *fields[i] = v;
        muiPropertyMask mask = MUI_PROPERTY_BIT(interaction[i]);
        CHECK(muiStyle_SetInteractionValues(s->context, s->style, mui_variantBase, &values, mask) ==
                  mui_errorInvalid,
              "an interaction value in a style");
        CHECK(muiNode_SetInteractionValues(s->context, s->node, &values, mask) == mui_errorInvalid,
              "an interaction value on a node");
    }
    static const muiProperty text[] = {mui_propertyFontSlant, mui_propertyTextAlign,
                                       mui_propertyTextWrap};
    for (size_t i = 0; i < sizeof text / sizeof text[0]; i++)
    {
        muiTextStyle values = {0};
        int32_t* fields[] = {&values.slant, &values.align, &values.wrap};
        *fields[i] = v;
        muiPropertyMask mask = MUI_PROPERTY_BIT(text[i]);
        CHECK(muiStyle_SetTextValues(s->context, s->style, mui_variantBase, &values, mask) ==
                  mui_errorInvalid,
              "a text value in a style");
        CHECK(muiNode_SetTextValues(s->context, s->node, &values, mask) == mui_errorInvalid,
              "a text value on a node");
    }
    for (int i = 0; i < 3; i++)
    {
        muiVisualStyle values = {0};
        muiProperty property = i == 0   ? mui_propertyImageRepeatX
                               : i == 1 ? mui_propertyImageRepeatY
                                        : mui_propertyGradient;
        *(i == 0   ? &values.imageRepeatX
          : i == 1 ? &values.imageRepeatY
                   : &values.gradient.kind) = v;
        muiPropertyMask mask = MUI_PROPERTY_BIT(property);
        CHECK(muiStyle_SetVisualValues(s->context, s->style, mui_variantBase, &values, mask) ==
                  mui_errorInvalid,
              "a visual value in a style");
        CHECK(muiNode_SetVisualValues(s->context, s->node, &values, mask) == mui_errorInvalid,
              "a visual value on a node");
    }
}

// The bit sets among the properties: a bit no set names.
static void TestValueBits(Scene* s, uint32_t bits)
{
    for (int i = 0; i < 3; i++)
    {
        muiLayoutStyle values = BaseLayout(s);
        muiProperty property = i == 0   ? mui_propertyMarginAuto
                               : i == 1 ? mui_propertySafeArea
                                        : mui_propertyScrollAxes;
        *(i == 0 ? &values.marginAuto : i == 1 ? &values.safeArea : &values.scrollAxes) = bits;
        CHECK(muiStyle_SetLayoutValues(s->context, s->style, mui_variantBase, &values,
                                       MUI_PROPERTY_BIT(property)) == mui_errorInvalid,
              "layout bits");
    }
    for (int i = 0; i < 2; i++)
    {
        muiTextStyle values = {0};
        *(i == 0 ? &values.decoration : &values.synthesis) = bits;
        muiProperty property = i == 0 ? mui_propertyTextDecoration : mui_propertyFontSynthesis;
        CHECK(muiStyle_SetTextValues(s->context, s->style, mui_variantBase, &values,
                                     MUI_PROPERTY_BIT(property)) == mui_errorInvalid,
              "text bits");
    }
    CHECK(muiNode_SetStates(s->context, s->node, bits) == mui_errorInvalid, "states");
    muiCondition condition = muiDefaultCondition();
    condition.viewports = bits;
    muiVariant variant = 0;
    CHECK(muiStyle_AddCondition(s->context, s->style, &condition, &variant) == mui_errorInvalid,
          "a condition's viewports");
    condition = muiDefaultCondition();
    condition.inputs = bits;
    CHECK(muiStyle_AddCondition(s->context, s->style, &condition, &variant) == mui_errorInvalid,
          "a condition's inputs");
    muiEnvironment environment = muiDefaultEnvironment();
    environment.viewport = bits | mui_viewportSmall;
    CHECK(muiContext_SetEnvironment(s->context, &environment) == mui_errorInvalid,
          "an environment's viewport");
    environment = muiDefaultEnvironment();
    environment.input = bits | mui_inputPointer;
    CHECK(muiContext_SetEnvironment(s->context, &environment) == mui_errorInvalid,
          "an environment's input");
}

static void TestStyle(Scene* s)
{
    muiTokenDef tokenDef = muiDefaultTokenDef();
    tokenDef.value.type = mui_tokenColor;
    muiTokenId token = {0, 0};
    CHECK(muiCreateToken(s->context, &tokenDef, &token) == mui_success, "token");
    muiTransitionDef transitionDef = muiDefaultTransitionDef();
    muiTransitionId transition = {0, 0};
    CHECK(muiCreateTransition(s->context, &transitionDef, &transition) == mui_success,
          "transition");
    KINDS
    {
        const int32_t v = s_kinds[k];
        TestValues(s, v);
        // A variant past the base, the states and the conditions.
        muiLayoutStyle layout = {0};
        muiVisualStyle visual = {0};
        muiTextStyle text = {0};
        muiInteractionStyle interaction = {0};
        muiPropertyMask mask = 0;
        muiCondition condition = muiDefaultCondition();
        muiTokenId tokenRead = {0, 0};
        muiTransitionId transitionRead = {0, 0};
        CHECK(
            muiStyle_SetLayoutValues(s->context, s->style, v, &layout, 0) == mui_errorInvalid &&
                muiStyle_GetLayoutValues(s->context, s->style, v, &layout, &mask) ==
                    mui_errorInvalid &&
                muiStyle_SetVisualValues(s->context, s->style, v, &visual, 0) == mui_errorInvalid &&
                muiStyle_GetVisualValues(s->context, s->style, v, &visual, &mask) ==
                    mui_errorInvalid &&
                muiStyle_SetTextValues(s->context, s->style, v, &text, 0) == mui_errorInvalid &&
                muiStyle_GetTextValues(s->context, s->style, v, &text, &mask) == mui_errorInvalid &&
                muiStyle_SetInteractionValues(s->context, s->style, v, &interaction, 0) ==
                    mui_errorInvalid &&
                muiStyle_GetInteractionValues(s->context, s->style, v, &interaction, &mask) ==
                    mui_errorInvalid,
            "a variant's values");
        CHECK(muiStyle_ResetProperties(s->context, s->style, v, mui_groupLayout, 0) ==
                      mui_errorInvalid &&
                  muiStyle_SetCondition(s->context, s->style, v, &condition) == mui_errorInvalid &&
                  muiStyle_GetCondition(s->context, s->style, v, &condition) == mui_errorInvalid &&
                  muiStyle_SetToken(s->context, s->style, v, mui_propertyBackground, token) ==
                      mui_errorInvalid &&
                  muiStyle_GetToken(s->context, s->style, v, mui_propertyBackground, &tokenRead) ==
                      mui_errorInvalid &&
                  muiStyle_SetTransition(s->context, s->style, v, transition, mui_groupLayout, 0) ==
                      mui_errorInvalid &&
                  muiStyle_GetTransition(s->context, s->style, v, mui_propertyWidth,
                                         &transitionRead) == mui_errorInvalid,
              "a variant's other calls");
        // A property past every group.
        CHECK(muiStyle_SetToken(s->context, s->style, mui_variantBase, v, token) ==
                      mui_errorInvalid &&
                  muiStyle_GetToken(s->context, s->style, mui_variantBase, v, &tokenRead) ==
                      mui_errorInvalid &&
                  muiStyle_GetTransition(s->context, s->style, mui_variantBase, v,
                                         &transitionRead) == mui_errorInvalid &&
                  !muiNode_IsTransitioning(s->context, s->node, v),
              "a property");
        // A group past the four.
        CHECK(muiStyle_ResetProperties(s->context, s->style, mui_variantBase, v, 0) ==
                      mui_errorInvalid &&
                  muiNode_ResetProperties(s->context, s->node, v, 0) == mui_errorInvalid &&
                  muiNode_GetDirectProperties(s->context, s->node, v) == 0 &&
                  muiStyle_SetTransition(s->context, s->style, mui_variantBase, transition, v, 0) ==
                      mui_errorInvalid,
              "a group");
        // Defs and conditions.
        muiTokenDef badToken = muiDefaultTokenDef();
        badToken.value.type = v;
        muiTokenId made = {0, 0};
        CHECK(muiCreateToken(s->context, &badToken, &made) == mui_errorInvalid, "a token type");
        muiTokenValue value = {0};
        value.type = v;
        CHECK(muiToken_SetValue(s->context, token, &value) == mui_errorInvalid,
              "a token value's type");
        for (int field = 0; field < 2; field++)
        {
            muiTransitionDef badTransition = muiDefaultTransitionDef();
            *(field == 0 ? &badTransition.kind : &badTransition.easing) = v;
            muiTransitionId madeTransition = {0, 0};
            CHECK(muiCreateTransition(s->context, &badTransition, &madeTransition) ==
                      mui_errorInvalid,
                  "a transition's kind and easing");
        }
        for (int field = 0; field < 2; field++)
        {
            muiCondition badCondition = muiDefaultCondition();
            *(field == 0 ? &badCondition.motion : &badCondition.direction) = v;
            muiVariant variant = 0;
            CHECK(muiStyle_AddCondition(s->context, s->style, &badCondition, &variant) ==
                      mui_errorInvalid,
                  "a condition's motion and direction");
        }
    }
    BITS
    {
        TestValueBits(s, s_bits[k]);
    }
}

int main(void)
{
    Scene scene = MakeScene();
    TestAccess(&scene);
    TestFocusAndInput(&scene);
    TestStyle(&scene);
    muiDestroyContext(scene.context);
    return s_failures == 0 ? 0 : 1;
}
