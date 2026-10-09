// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Focus (record mui-0007): per player slot, shown by cause, sequential
// order with tab orders and layers, modal layers, pointer presses, nodes
// that stop taking focus, notifications and calls outside the contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/exit.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// A node of a size under parent (none for a root), placed absolutely at
// x, y, with a focus mode and tab order.
static muiNodeId Place(muiContext* context, muiNodeId parent, float x, float y, float size,
                       muiFocusMode mode, uint8_t tabOrder)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = Length(size);
    layout.sizing.height = Length(size);
    layout.placement.position = parent.index1 != 0 ? mui_positionAbsolute : mui_positionFlow;
    layout.placement.inset.start = Length(x);
    layout.placement.inset.top = Length(y);
    CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight) |
                                      MUI_PROPERTY_BIT(mui_propertyPosition) |
                                      MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                                      MUI_PROPERTY_BIT(mui_propertyInsetTop)) == mui_success,
          "placed");
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.focusMode = mode;
    values.tabOrder = tabOrder;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode) |
                                           MUI_PROPERTY_BIT(mui_propertyTabOrder)) == mui_success,
          "focus values");
    return node;
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static muiContext* MakeContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    return context;
}

// Whether the next notification reports a focus change.
static bool Noted(muiContext* context, muiNotificationKind kind, muiNodeId node, uint32_t player)
{
    muiNotification record = {0};
    return muiNextNotification(context, &record) == mui_success && record.kind == kind &&
           Same(record.node, node) && record.count == player;
}

static bool Quiet(muiContext* context)
{
    muiNotification record = {0};
    return muiNextNotification(context, &record) == mui_empty;
}

static void Drain(muiContext* context)
{
    muiNotification record = {0};
    while (muiNextNotification(context, &record) == mui_success)
    {
    }
}

static muiState StatesOf(const muiContext* context, muiNodeId node)
{
    return muiNode_GetStates(context, node) & (mui_stateFocused | mui_stateFocusVisible);
}

static void TestSet(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
    muiNodeId a = Place(context, root, 0.0f, 0.0f, 50.0f, mui_focusPointer, 0);
    muiNodeId b = Place(context, root, 60.0f, 0.0f, 50.0f, mui_focusAll, 0);
    Layout(context, root);
    Drain(context);
    CHECK(muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success &&
              Same(muiFocus_Get(context, 0), a) &&
              StatesOf(context, a) == (mui_stateFocused | mui_stateFocusVisible) &&
              Noted(context, mui_notificationFocusGained, a, 0) && Quiet(context),
          "code focus, shown before any pointer");
    CHECK(muiFocus_Set(context, 0, b, mui_focusByPointer) == mui_success &&
              StatesOf(context, a) == 0 && StatesOf(context, b) == mui_stateFocused &&
              Noted(context, mui_notificationFocusLost, a, 0) &&
              Noted(context, mui_notificationFocusGained, b, 0) && Quiet(context),
          "a pointer moves it, hidden");
    CHECK(muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success &&
              StatesOf(context, a) == mui_stateFocused,
          "code after a pointer: hidden");
    CHECK(muiFocus_Set(context, 0, a, mui_focusByNavigation) == mui_success &&
              StatesOf(context, a) == (mui_stateFocused | mui_stateFocusVisible) &&
              muiFocus_Set(context, 0, b, mui_focusByCode) == mui_success &&
              StatesOf(context, b) == (mui_stateFocused | mui_stateFocusVisible),
          "navigation shows, and code after it");
    Drain(context);
    CHECK(muiFocus_Set(context, 0, b, mui_focusByPointer) == mui_success &&
              StatesOf(context, b) == mui_stateFocused && Quiet(context),
          "the same node hidden: no notification");
    // A second player on b: b stays focused while either is.
    CHECK(muiFocus_Set(context, 3, b, mui_focusByNavigation) == mui_success &&
              StatesOf(context, b) == (mui_stateFocused | mui_stateFocusVisible) &&
              Noted(context, mui_notificationFocusGained, b, 3) &&
              muiFocus_Set(context, 0, s_nullNode, mui_focusByCode) == mui_success &&
              StatesOf(context, b) == (mui_stateFocused | mui_stateFocusVisible) &&
              Noted(context, mui_notificationFocusLost, b, 0) &&
              muiFocus_Set(context, 3, s_nullNode, mui_focusByCode) == mui_success &&
              StatesOf(context, b) == 0 && muiFocus_Get(context, 3).index1 == 0,
          "two players");
    // The host's own focused state stays apart.
    CHECK(muiNode_SetStates(context, root, mui_stateFocused) == mui_success &&
              muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success &&
              muiFocus_Set(context, 0, s_nullNode, mui_focusByCode) == mui_success &&
              muiNode_GetStates(context, root) == mui_stateFocused,
          "the host's states");
    muiDestroyContext(context);
}

static void TestStyled(void)
{
    // A class whose focus-visible variant paints red.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
    muiNodeId a = Place(context, root, 0.0f, 0.0f, 50.0f, mui_focusAll, 0);
    muiStyleId style;
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){1.0f, 0.0f, 0.0f, 1.0f};
    const muiStyleDef styleDef = muiDefaultStyleDef();
    CHECK(muiCreateStyle(context, &styleDef, &style) == mui_success &&
              muiStyle_SetVisualValues(context, style, mui_variantFocusVisible, &visual,
                                       MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success &&
              muiNode_SetClasses(context, a, &style, 1) == mui_success,
          "a class");
    Layout(context, root);
    muiVisualStyle read = muiDefaultVisualStyle();
    CHECK(muiFocus_Set(context, 0, a, mui_focusByPointer) == mui_success, "focused");
    Layout(context, root);
    CHECK(muiNode_GetVisualStyle(context, a, &read) == mui_success && read.background.a == 0.0f,
          "hidden: no ring");
    CHECK(muiFocus_Set(context, 0, a, mui_focusByNavigation) == mui_success, "shown");
    Layout(context, root);
    CHECK(muiNode_GetVisualStyle(context, a, &read) == mui_success && read.background.r == 1.0f,
          "shown: the ring");
    muiDestroyContext(context);
}

static void TestOrder(void)
{
    // In tree order: root, n1 (all), n5 (all, n1's child), n2 (pointer),
    // n3 (all, order 2), n4 (all, order 1), n6 (all, disabled).
    // Sequential order: n4, n3, n1, n5.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
    muiNodeId n1 = Place(context, root, 0.0f, 0.0f, 50.0f, mui_focusAll, 0);
    muiNodeId n5 = Place(context, n1, 0.0f, 0.0f, 10.0f, mui_focusAll, 0);
    muiNodeId n2 = Place(context, root, 60.0f, 0.0f, 50.0f, mui_focusPointer, 0);
    muiNodeId n3 = Place(context, root, 120.0f, 0.0f, 50.0f, mui_focusAll, 2);
    muiNodeId n4 = Place(context, root, 180.0f, 0.0f, 50.0f, mui_focusAll, 1);
    muiNodeId n6 = Place(context, root, 240.0f, 0.0f, 50.0f, mui_focusAll, 0);
    CHECK(muiNode_SetStates(context, n6, mui_stateDisabled) == mui_success, "disabled");
    Layout(context, root);
    const muiNodeId order[5] = {n4, n3, n1, n5, n4};
    bool forward = true;
    for (int i = 0; i < 5; i++)
    {
        forward = forward && muiFocus_Move(context, root, 0, false) == mui_success &&
                  Same(muiFocus_Get(context, 0), order[i]);
    }
    CHECK(forward && StatesOf(context, n4) == (mui_stateFocused | mui_stateFocusVisible),
          "forward, wrapping, shown");
    CHECK(muiFocus_Set(context, 0, n4, mui_focusByCode) == mui_success &&
              muiFocus_Move(context, root, 0, true) == mui_success &&
              Same(muiFocus_Get(context, 0), n5) &&
              muiFocus_Move(context, root, 0, true) == mui_success &&
              Same(muiFocus_Get(context, 0), n1) &&
              muiFocus_Move(context, root, 0, true) == mui_success &&
              Same(muiFocus_Get(context, 0), n3) &&
              muiFocus_Move(context, root, 0, true) == mui_success &&
              Same(muiFocus_Get(context, 0), n4),
          "backward, wrapping");
    // From n2, out of the order but in tree order after n5: on to n4
    // (wrapping), back to n5.
    CHECK(muiFocus_Set(context, 0, n2, mui_focusByPointer) == mui_success &&
              muiFocus_Move(context, root, 0, false) == mui_success &&
              Same(muiFocus_Get(context, 0), n4) &&
              muiFocus_Set(context, 0, n2, mui_focusByPointer) == mui_success &&
              muiFocus_Move(context, root, 0, true) == mui_success &&
              Same(muiFocus_Get(context, 0), n5),
          "from a node Tab passes over");
    // From no focus: backward starts at the last.
    CHECK(muiFocus_Set(context, 1, s_nullNode, mui_focusByCode) == mui_success &&
              muiFocus_Move(context, root, 1, true) == mui_success &&
              Same(muiFocus_Get(context, 1), n5),
          "backward from nothing");
    // A focus in another tree starts again in this one.
    muiNodeId other = Place(context, s_nullNode, 0.0f, 0.0f, 10.0f, mui_focusAll, 0);
    CHECK(muiFocus_Set(context, 1, other, mui_focusByCode) == mui_success &&
              muiFocus_Move(context, root, 1, false) == mui_success &&
              Same(muiFocus_Get(context, 1), n4) &&
              muiFocus_Move(context, other, 1, false) == mui_success &&
              Same(muiFocus_Get(context, 1), other) &&
              muiFocus_Move(context, other, 1, false) == mui_success &&
              Same(muiFocus_Get(context, 1), other),
          "another tree; a tree of one");
    muiDestroyContext(context);
}

static void SetLayer(muiContext* context, muiNodeId node, muiLayerKind kind)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.layer = kind;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "layer");
}

static void TestLayers(void)
{
    // Base: b1, b2. A dialog d (a layer) with d1 and d2, and in it a menu
    // m (a layer) with m1.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
    muiNodeId b1 = Place(context, root, 0.0f, 0.0f, 20.0f, mui_focusAll, 0);
    muiNodeId d = Place(context, root, 100.0f, 100.0f, 100.0f, mui_focusNone, 0);
    muiNodeId d1 = Place(context, d, 0.0f, 0.0f, 20.0f, mui_focusAll, 0);
    muiNodeId m = Place(context, d, 50.0f, 0.0f, 40.0f, mui_focusNone, 0);
    muiNodeId m1 = Place(context, m, 0.0f, 0.0f, 20.0f, mui_focusAll, 0);
    muiNodeId d2 = Place(context, d, 0.0f, 50.0f, 20.0f, mui_focusAll, 0);
    muiNodeId b2 = Place(context, root, 0.0f, 50.0f, 20.0f, mui_focusAll, 0);
    SetLayer(context, d, mui_layerActivation);
    SetLayer(context, m, mui_layerActivation);
    Layout(context, root);
    // Tab in the base passes over the dialog; in the dialog, over the
    // menu; in the menu, it stays.
    CHECK(muiFocus_Set(context, 0, b1, mui_focusByCode) == mui_success &&
              muiFocus_Move(context, root, 0, false) == mui_success &&
              Same(muiFocus_Get(context, 0), b2) &&
              muiFocus_Move(context, root, 0, false) == mui_success &&
              Same(muiFocus_Get(context, 0), b1),
          "the base");
    CHECK(muiFocus_Set(context, 0, d1, mui_focusByCode) == mui_success &&
              muiFocus_Move(context, root, 0, false) == mui_success &&
              Same(muiFocus_Get(context, 0), d2) &&
              muiFocus_Move(context, root, 0, false) == mui_success &&
              Same(muiFocus_Get(context, 0), d1),
          "the dialog");
    CHECK(muiFocus_Set(context, 0, m1, mui_focusByCode) == mui_success &&
              muiFocus_Move(context, root, 0, false) == mui_success &&
              Same(muiFocus_Get(context, 0), m1),
          "the menu");
    // The dialog made modal: the base takes no focus, and Tab from it, or
    // from nothing, starts in the dialog.
    SetLayer(context, d, mui_layerModal);
    Layout(context, root);
    CHECK(muiFocus_Set(context, 1, b1, mui_focusByCode) == mui_errorInvalid &&
              muiFocus_Set(context, 1, d2, mui_focusByCode) == mui_success &&
              muiFocus_Set(context, 1, m1, mui_focusByCode) == mui_success,
          "covered nodes refused");
    CHECK(muiFocus_Set(context, 2, s_nullNode, mui_focusByCode) == mui_success &&
              muiFocus_Move(context, root, 2, false) == mui_success &&
              Same(muiFocus_Get(context, 2), d1),
          "from nothing into the dialog");
    // Player 3 focused b1 before the dialog was modal.
    SetLayer(context, d, mui_layerActivation);
    Layout(context, root);
    CHECK(muiFocus_Set(context, 3, b1, mui_focusByCode) == mui_success, "b1");
    SetLayer(context, d, mui_layerModal);
    Layout(context, root);
    CHECK(muiFocus_Move(context, root, 3, true) == mui_success &&
              Same(muiFocus_Get(context, 3), d2),
          "from a covered node into the dialog");
    // A modal layer in another tree covers nothing here.
    muiNodeId other = Place(context, s_nullNode, 0.0f, 0.0f, 10.0f, mui_focusNone, 0);
    muiNodeId modal = Place(context, other, 0.0f, 0.0f, 10.0f, mui_focusAll, 0);
    SetLayer(context, d, mui_layerActivation);
    SetLayer(context, modal, mui_layerModal);
    Layout(context, root);
    Layout(context, other);
    CHECK(muiFocus_Set(context, 1, b1, mui_focusByCode) == mui_success &&
              muiFocus_Set(context, 1, s_nullNode, mui_focusByCode) == mui_success &&
              muiFocus_Move(context, root, 1, false) == mui_success &&
              Same(muiFocus_Get(context, 1), b1),
          "another tree's modal");
    muiDestroyContext(context);
}

static void TestPointer(void)
{
    // f (pointer) holds c (none); g (all) beside it.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
    muiNodeId f = Place(context, root, 0.0f, 0.0f, 100.0f, mui_focusPointer, 0);
    muiNodeId c = Place(context, f, 10.0f, 10.0f, 20.0f, mui_focusNone, 0);
    muiNodeId g = Place(context, root, 150.0f, 0.0f, 100.0f, mui_focusAll, 0);
    Layout(context, root);
    muiPointerEvent press = {0, 1, mui_pointerMouse, mui_pointerPress, 0, 1, 15.0f, 15.0f, 2};
    muiPointerEvent release = press;
    release.action = mui_pointerRelease;
    release.buttons = 0;
    CHECK(muiPointerInput(context, root, &press) == mui_success &&
              muiPointerInput(context, root, &release) == mui_success &&
              Same(muiFocus_Get(context, 2), f) && muiFocus_Get(context, 0).index1 == 0 &&
              StatesOf(context, f) == mui_stateFocused && StatesOf(context, c) == 0,
          "a press focuses the nearest focusable, for its player, hidden");
    // Code after a pointer stays hidden; Tab shows.
    CHECK(muiFocus_Set(context, 2, g, mui_focusByCode) == mui_success &&
              StatesOf(context, g) == mui_stateFocused &&
              muiFocus_Move(context, root, 2, false) == mui_success &&
              StatesOf(context, g) == (mui_stateFocused | mui_stateFocusVisible) &&
              muiFocus_Set(context, 2, f, mui_focusByCode) == mui_success &&
              StatesOf(context, f) == (mui_stateFocused | mui_stateFocusVisible),
          "then code and Tab, and code after Tab shows");
    // A second button while one is held changes nothing; a press over
    // nothing focusable takes the focus away.
    press.x = 280.0f;
    press.y = 280.0f;
    release.x = 280.0f;
    release.y = 280.0f;
    CHECK(muiPointerInput(context, root, &press) == mui_success &&
              muiPointerInput(context, root, &release) == mui_success &&
              muiFocus_Get(context, 2).index1 == 0,
          "over nothing");
    press.x = 160.0f;
    press.y = 10.0f;
    press.buttons = 3;
    press.button = 1;
    CHECK(muiFocus_Set(context, 2, f, mui_focusByCode) == mui_success, "f again");
    muiPointerEvent first = press;
    first.buttons = 1;
    first.button = 0;
    first.x = 15.0f;
    first.y = 15.0f;
    CHECK(muiPointerInput(context, root, &first) == mui_success &&
              muiPointerInput(context, root, &press) == mui_success &&
              Same(muiFocus_Get(context, 2), f),
          "a chord");
    // A modal layer over f: a press beside it is blocked by the layer,
    // which takes no focus, and f, covered, takes none either.
    muiNodeId modal = Place(context, f, 200.0f, 200.0f, 50.0f, mui_focusNone, 0);
    SetLayer(context, modal, mui_layerModal);
    Layout(context, root);
    CHECK(muiPointerInput(context, root, &release) == mui_success, "all released");
    CHECK(muiPointerInput(context, root, &first) == mui_success &&
              muiFocus_Get(context, 2).index1 == 0,
          "a press under a modal layer");
    // The modal layer destroyed: f takes focus again.
    CHECK(muiDestroyNode(context, modal) == mui_success &&
              muiFocus_Set(context, 2, f, mui_focusByCode) == mui_success,
          "a modal layer gone");
    first.player = MUI_MAX_PLAYERS;
    CHECK(muiPointerInput(context, root, &first) == mui_errorInvalid, "a player past the slots");
    muiDestroyContext(context);
}

static void TestLoss(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
    muiNodeId a = Place(context, root, 0.0f, 0.0f, 50.0f, mui_focusAll, 0);
    muiNodeId b = Place(context, root, 60.0f, 0.0f, 50.0f, mui_focusAll, 0);
    Layout(context, root);
    // A direct write that makes it unfocusable.
    CHECK(muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success &&
              muiFocus_Set(context, 1, a, mui_focusByCode) == mui_success,
          "two players on a");
    Drain(context);
    muiInteractionStyle values = muiDefaultInteractionStyle();
    CHECK(muiNode_SetInteractionValues(context, a, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success &&
              muiFocus_Get(context, 0).index1 == 0 && muiFocus_Get(context, 1).index1 == 0 &&
              StatesOf(context, a) == 0 && Noted(context, mui_notificationFocusLost, a, 0) &&
              Noted(context, mui_notificationFocusLost, a, 1) && Quiet(context),
          "no longer focusable");
    // Disabled: let go at the next styling.
    CHECK(muiFocus_Set(context, 0, b, mui_focusByCode) == mui_success &&
              muiNode_SetStates(context, b, mui_stateDisabled) == mui_success,
          "disabled");
    Drain(context);
    Layout(context, root);
    CHECK(muiFocus_Get(context, 0).index1 == 0 && Noted(context, mui_notificationFocusLost, b, 0),
          "let go");
    CHECK(muiNode_SetStates(context, b, 0) == mui_success &&
              muiNode_BeginExit(context, b) == mui_success &&
              muiFocus_Set(context, 0, b, mui_focusByCode) == mui_errorInvalid &&
              muiNode_CancelExit(context, b) == mui_success,
          "exiting: refused");
    Layout(context, root);
    // Detached: kept; destroyed: lost.
    CHECK(muiFocus_Set(context, 0, b, mui_focusByCode) == mui_success &&
              muiNode_Detach(context, b) == mui_success && Same(muiFocus_Get(context, 0), b) &&
              muiFocus_Move(context, root, 0, false) == mui_empty,
          "detached, kept; nothing else to reach");
    Drain(context);
    CHECK(muiDestroyNode(context, b) == mui_success && muiFocus_Get(context, 0).index1 == 0 &&
              Noted(context, mui_notificationFocusLost, b, 0) && Quiet(context),
          "destroyed");
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId again = s_nullNode;
    CHECK(muiCreateNode(context, &def, &again) == mui_success && again.index1 == b.index1 &&
              StatesOf(context, again) == 0,
          "the slot again");
    // Destroying something else leaves a live focus.
    values.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, a, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success &&
              muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success &&
              muiDestroyNode(context, again) == mui_success && Same(muiFocus_Get(context, 0), a),
          "other destructions");
    muiDestroyContext(context);
}

enum
{
    RANDOM_NODES = 16
};

typedef struct Random
{
    uint32_t state;
} Random;

static uint32_t NextRandom(Random* random, uint32_t below)
{
    random->state = random->state * 1664525u + 1013904223u;
    return (random->state >> 8) % below;
}

// A random tree as the test knows it: parents, layers, modes, orders.
typedef struct Tree
{
    muiNodeId nodes[RANDOM_NODES];
    uint32_t parent[RANDOM_NODES];
    bool layer[RANDOM_NODES];
    bool reached[RANDOM_NODES];
    uint8_t order[RANDOM_NODES];
} Tree;

// Lists scope's nodes in tree order without the layers below it, as
// indices; returns the count.
static uint32_t Collect(const Tree* tree, uint32_t at, uint32_t scope, uint32_t* out,
                        uint32_t count)
{
    if (at != scope && tree->layer[at])
    {
        return count;
    }
    out[count++] = at;
    // Children were inserted in index order.
    for (uint32_t i = 1; i < RANDOM_NODES; i++)
    {
        if (tree->parent[i] == at)
        {
            count = Collect(tree, i, scope, out, count);
        }
    }
    return count;
}

static uint64_t OracleKey(const Tree* tree, uint32_t index, uint32_t place)
{
    return (uint64_t)(tree->order[index] == 0 ? 256u : tree->order[index]) << 32 | place;
}

// The node sequential navigation reaches from focus, as an index.
static uint32_t OracleNext(const Tree* tree, uint32_t focus, bool backward)
{
    uint32_t scope = focus;
    while (scope != 0 && !tree->layer[scope])
    {
        scope = tree->parent[scope];
    }
    uint32_t list[RANDOM_NODES];
    uint32_t count = Collect(tree, scope, scope, list, 0);
    uint64_t current = 0;
    for (uint32_t p = 0; p < count; p++)
    {
        current = list[p] == focus ? OracleKey(tree, focus, p) : current;
    }
    uint32_t best = RANDOM_NODES;
    uint32_t wrap = RANDOM_NODES;
    uint64_t bestKey = 0;
    uint64_t wrapKey = 0;
    for (uint32_t p = 0; p < count; p++)
    {
        uint32_t i = list[p];
        if (!tree->reached[i])
        {
            continue;
        }
        uint64_t key = OracleKey(tree, i, p);
        bool beyond = backward ? key < current : key > current;
        if (beyond && (best == RANDOM_NODES || (backward ? key > bestKey : key < bestKey)))
        {
            best = i;
            bestKey = key;
        }
        if (wrap == RANDOM_NODES || (backward ? key > wrapKey : key < wrapKey))
        {
            wrap = i;
            wrapKey = key;
        }
    }
    return best != RANDOM_NODES ? best : wrap;
}

// Random trees with random modes, orders, disabled nodes and layers:
// each move lands where an order worked out afresh says.
static void TestRandomOrder(void)
{
    for (uint32_t seed = 1; seed <= 60; seed++)
    {
        Random random = {seed};
        muiContext* context = MakeContext();
        Tree tree = {0};
        tree.nodes[0] = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
        for (uint32_t i = 1; i < RANDOM_NODES; i++)
        {
            tree.parent[i] = NextRandom(&random, i);
            muiFocusMode mode = (muiFocusMode)NextRandom(&random, 3);
            tree.order[i] = (uint8_t)(NextRandom(&random, 2) == 0 ? 0 : NextRandom(&random, 4));
            tree.nodes[i] =
                Place(context, tree.nodes[tree.parent[i]], 0.0f, 0.0f, 10.0f, mode, tree.order[i]);
            bool disabled = NextRandom(&random, 6) == 0;
            if (disabled)
            {
                CHECK(muiNode_SetStates(context, tree.nodes[i], mui_stateDisabled) == mui_success,
                      "disabled");
            }
            tree.reached[i] = mode == mui_focusAll && !disabled;
            tree.layer[i] = NextRandom(&random, 5) == 0;
            if (tree.layer[i])
            {
                SetLayer(context, tree.nodes[i], mui_layerActivation);
            }
        }
        Layout(context, tree.nodes[0]);
        bool agree = true;
        for (uint32_t step = 0; agree && step < 40; step++)
        {
            // Sometimes from a node by pointer, out of order or not.
            uint32_t from = NextRandom(&random, RANDOM_NODES);
            if (NextRandom(&random, 3) == 0)
            {
                (void)muiFocus_Set(context, 0, tree.nodes[from], mui_focusByPointer);
            }
            muiNodeId focus = muiFocus_Get(context, 0);
            if (focus.index1 == 0)
            {
                continue;
            }
            uint32_t at = 0;
            while (!Same(tree.nodes[at], focus))
            {
                at++;
            }
            bool backward = NextRandom(&random, 2) == 0;
            uint32_t expected = OracleNext(&tree, at, backward);
            muiResult result = muiFocus_Move(context, tree.nodes[0], 0, backward);
            agree =
                expected == RANDOM_NODES
                    ? result == mui_empty && Same(muiFocus_Get(context, 0), focus)
                    : result == mui_success && Same(muiFocus_Get(context, 0), tree.nodes[expected]);
        }
        // From no focus: the base's first.
        CHECK(muiFocus_Set(context, 0, s_nullNode, mui_focusByCode) == mui_success, "none");
        uint32_t list[RANDOM_NODES];
        uint32_t count = Collect(&tree, 0, 0, list, 0);
        uint32_t first = RANDOM_NODES;
        uint64_t firstKey = 0;
        for (uint32_t p = 0; p < count; p++)
        {
            uint64_t key = OracleKey(&tree, list[p], p);
            if (tree.reached[list[p]] && (first == RANDOM_NODES || key < firstKey))
            {
                first = list[p];
                firstKey = key;
            }
        }
        muiResult result = muiFocus_Move(context, tree.nodes[0], 0, false);
        agree = agree &&
                (first == RANDOM_NODES
                     ? result == mui_empty
                     : result == mui_success && Same(muiFocus_Get(context, 0), tree.nodes[first]));
        CHECK(agree, "moves agree with the order");
        muiDestroyContext(context);
    }
}

static void TestContract(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
    muiNodeId a = Place(context, root, 0.0f, 0.0f, 50.0f, mui_focusAll, 0);
    Layout(context, root);
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiFocus_Set(NULL, 0, a, mui_focusByCode) == mui_errorInvalid &&
              muiFocus_Set(context, MUI_MAX_PLAYERS, a, mui_focusByCode) == mui_errorInvalid &&
              muiFocus_Set(context, 0, a, mui_focusByNavigation + 1) == mui_errorInvalid &&
              muiFocus_Set(context, 0, root, mui_focusByCode) == mui_errorInvalid &&
              muiFocus_Move(NULL, root, 0, false) == mui_errorInvalid &&
              muiFocus_Move(context, s_nullNode, 0, false) == mui_errorInvalid &&
              muiFocus_Move(context, root, MUI_MAX_PLAYERS, false) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 5 && muiFocus_Get(context, 0).index1 == 0,
          "outside the contract, nothing changed");
    CHECK(muiFocus_Get(NULL, 0).index1 == 0 && muiFocus_Get(context, MUI_MAX_PLAYERS).index1 == 0 &&
              muiFocus_Set(context, MUI_MAX_PLAYERS - 1, a, mui_focusByCode) == mui_success &&
              Same(muiFocus_Get(context, MUI_MAX_PLAYERS - 1), a),
          "the last player");
    muiNodeId gone = a;
    CHECK(muiDestroyNode(context, gone) == mui_success &&
              muiFocus_Set(context, 0, gone, mui_focusByCode) == mui_errorStale &&
              muiFocus_Move(context, root, 0, false) == mui_empty,
          "a node gone; nothing to reach");
    CHECK(muiDestroyNode(context, root) == mui_success &&
              muiFocus_Move(context, root, 0, false) == mui_errorStale,
          "a root gone");
    muiDestroyContext(context);
}

// Each player's hold on focus is let go on its own: after player 0's
// node is destroyed and player 0 focuses and lets go of another, player
// 1's node destroyed still loses player 1's focus.
static void TestDestroyedPerPlayer(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 300.0f, mui_focusNone, 0);
    muiNodeId a = Place(context, root, 0.0f, 0.0f, 50.0f, mui_focusAll, 0);
    muiNodeId b = Place(context, root, 60.0f, 0.0f, 50.0f, mui_focusAll, 0);
    muiNodeId c = Place(context, root, 120.0f, 0.0f, 50.0f, mui_focusAll, 0);
    Layout(context, root);
    CHECK(muiFocus_Set(context, 0, a, mui_focusByCode) == mui_success &&
              muiFocus_Set(context, 1, b, mui_focusByCode) == mui_success,
          "player 0 on a, player 1 on b");
    CHECK(muiDestroyNode(context, a) == mui_success && muiFocus_Get(context, 0).index1 == 0 &&
              Same(muiFocus_Get(context, 1), b),
          "a destroyed: player 0's focus gone, player 1's kept");
    CHECK(muiFocus_Set(context, 0, c, mui_focusByCode) == mui_success &&
              muiFocus_Set(context, 0, s_nullNode, mui_focusByCode) == mui_success,
          "player 0 on c, then on nothing");
    Drain(context);
    CHECK(muiDestroyNode(context, b) == mui_success && muiFocus_Get(context, 1).index1 == 0 &&
              Noted(context, mui_notificationFocusLost, b, 1),
          "b destroyed: player 1's focus gone, and told");
    muiDestroyContext(context);
}

int main(void)
{
    TestSet();
    TestDestroyedPerPlayer();
    TestStyled();
    TestOrder();
    TestLayers();
    TestPointer();
    TestLoss();
    TestRandomOrder();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
