// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Exit transitions (record mui-0007): the exiting state, the subtree out
// of hit testing, focus and navigation, the finished record once no
// transition runs, cancelling, and the table's contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/exit.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

#define MS 1000000ull

static muiNodeId Sized(muiContext* context, muiNodeId parent, float width, float height)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    style.sizing.height = (muiDimension){0.0f, height, mui_dimensionValue};
    style.item.shrink = 0.0f;
    CHECK(muiNode_SetLayoutValues(context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight) |
                                      MUI_PROPERTY_BIT(mui_propertyShrink)) == mui_success,
          "size");
    return node;
}

static void Layout(muiContext* context, muiNodeId root, uint64_t timeNs)
{
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, timeNs, NULL, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static void Focusable(muiContext* context, muiNodeId node)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable");
}

// The node hit at x, y; the null id for none.
static muiNodeId HitAt(const muiContext* context, muiNodeId root, float x, float y)
{
    muiHit hit = {0};
    CHECK(muiHitTest(context, root, x, y, &hit) == mui_success, "hit test");
    return hit.node;
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// How many exits finished since last asked; the last in nodeOut.
static int Finished(muiContext* context, muiNodeId* nodeOut)
{
    int count = 0;
    muiNotification record = {0};
    while (muiNextNotification(context, &record) == mui_success)
    {
        if (record.kind == mui_notificationExitFinished)
        {
            *nodeOut = record.node;
            count++;
        }
    }
    return count;
}

// A root of 400 by 300 in a row: a panel of 200 by 100 holding a child
// of 50 by 50, and a node of 50 by 50 beside it.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId panel;
    muiNodeId child;
    muiNodeId beside;
} Scene;

static void MakeScene(Scene* scene)
{
    muiContextDef def = muiDefaultContextDef();
    *scene = (Scene){0};
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiContext* context = scene->context;
    scene->root = Sized(context, s_nullNode, 400.0f, 300.0f);
    scene->panel = Sized(context, scene->root, 200.0f, 100.0f);
    scene->child = Sized(context, scene->panel, 50.0f, 50.0f);
    scene->beside = Sized(context, scene->root, 50.0f, 50.0f);
    Layout(context, scene->root, 0);
}

static void TestInteraction(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    Focusable(context, scene.child);
    Focusable(context, scene.beside);
    // A second child, which Tab must pass over as well.
    Focusable(context, Sized(context, scene.panel, 50.0f, 50.0f));
    Layout(context, scene.root, 0);
    CHECK(Same(HitAt(context, scene.root, 10.0f, 10.0f), scene.child) &&
              Same(HitAt(context, scene.root, 150.0f, 10.0f), scene.panel),
          "hit before");
    CHECK(muiFocus_Set(context, 0, scene.child, mui_focusByCode) == mui_success, "focused");
    muiNodeId lost = s_nullNode;
    muiNotification record = {0};
    while (muiNextNotification(context, &record) == mui_success)
    {
    }
    CHECK(muiNode_BeginExit(context, scene.panel) == mui_success, "exit");
    while (muiNextNotification(context, &record) == mui_success)
    {
        lost = record.kind == mui_notificationFocusLost ? record.node : lost;
    }
    CHECK(Same(lost, scene.child) && muiFocus_Get(context, 0).index1 == 0, "focus given up");
    CHECK((muiNode_GetStates(context, scene.panel) & mui_stateExiting) != 0 &&
              (muiNode_GetStates(context, scene.child) & mui_stateExiting) == 0,
          "the state on the node alone");
    CHECK(muiNode_SetStates(context, scene.panel, mui_stateChecked) == mui_success &&
              muiNode_GetStates(context, scene.panel) == (mui_stateChecked | mui_stateExiting),
          "kept by setting states");
    CHECK(muiNode_SetStates(context, scene.beside, mui_stateExiting) == mui_success &&
              muiNode_GetStates(context, scene.beside) == 0,
          "not set by setting states");
    Layout(context, scene.root, 0);
    CHECK(Same(HitAt(context, scene.root, 10.0f, 10.0f), scene.root) &&
              Same(HitAt(context, scene.root, 150.0f, 10.0f), scene.root) &&
              Same(HitAt(context, scene.root, 210.0f, 10.0f), scene.beside),
          "out of hit testing, the subtree too: the root behind");
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiFocus_Set(context, 0, scene.child, mui_focusByCode) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 1,
          "nothing under it takes focus");
    const muiKeyEvent tab = {.code = mui_codeTab, .down = true};
    bool handled = false;
    CHECK(muiKeyInput(context, scene.root, &tab, &handled) == mui_success && handled &&
              Same(muiFocus_Get(context, 0), scene.beside),
          "Tab passes it over");
    CHECK(muiFocus_MoveToward(context, scene.root, 0, mui_directionLeft) == mui_empty &&
              Same(muiFocus_Get(context, 0), scene.beside),
          "navigation finds nothing there");
    // Cancelled: back.
    CHECK(muiNode_CancelExit(context, scene.panel) == mui_success &&
              muiNode_GetStates(context, scene.panel) == mui_stateChecked,
          "cancelled");
    Layout(context, scene.root, 0);
    CHECK(Same(HitAt(context, scene.root, 10.0f, 10.0f), scene.child) &&
              muiFocus_Set(context, 0, scene.child, mui_focusByCode) == mui_success,
          "input again");
    muiDestroyContext(context);
}

static void TestFinished(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    muiNodeId node = s_nullNode;
    // No transition: done at the next layout, once.
    CHECK(muiNode_BeginExit(context, scene.beside) == mui_success &&
              muiNode_BeginExit(context, scene.beside) == mui_success,
          "twice is once");
    CHECK(Finished(context, &node) == 0, "not before a layout");
    Layout(context, scene.root, 0);
    CHECK(Finished(context, &node) == 1 && Same(node, scene.beside), "done");
    Layout(context, scene.root, 0);
    CHECK(Finished(context, &node) == 0, "once");
    // A class whose exiting variant fades opacity out over 100 ms.
    muiTransitionDef fade = muiDefaultTransitionDef();
    fade.durationNs = 100 * MS;
    fade.easing = mui_easingLinear;
    muiTransitionId transition = {0};
    CHECK(muiCreateTransition(context, &fade, &transition) == mui_success, "transition");
    muiStyleId style = {0};
    const muiStyleDef styleDef = muiDefaultStyleDef();
    CHECK(muiCreateStyle(context, &styleDef, &style) == mui_success, "class");
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.opacity = 0.0f;
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantExiting, &visual,
                                   MUI_PROPERTY_BIT(mui_propertyOpacity)) == mui_success &&
              muiStyle_SetTransition(context, style, mui_variantExiting, transition,
                                     mui_groupVisual,
                                     MUI_PROPERTY_BIT(mui_propertyOpacity)) == mui_success &&
              muiNode_SetClasses(context, scene.child, &style, 1) == mui_success,
          "the child fades when exiting");
    Layout(context, scene.root, 0);
    // The child exits itself, inside a panel that exits: the panel waits
    // on its child's fade too.
    CHECK(muiNode_BeginExit(context, scene.child) == mui_success &&
              muiNode_BeginExit(context, scene.panel) == mui_success,
          "exits");
    Layout(context, scene.root, 10 * MS);
    CHECK(Finished(context, &node) == 0, "fading");
    Layout(context, scene.root, 60 * MS);
    CHECK(Finished(context, &node) == 0, "still fading");
    Layout(context, scene.root, 111 * MS);
    CHECK(Finished(context, &node) == 2, "both done when the fade ends");
    // Destroyed: forgotten.
    CHECK(muiDestroyNode(context, scene.panel) == mui_success, "destroyed");
    Layout(context, scene.root, 200 * MS);
    CHECK(Finished(context, &node) == 0, "nothing more");
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

static bool Tab(muiContext* context, muiNodeId root)
{
    const muiKeyEvent tab = {.code = mui_codeTab, .down = true};
    bool handled = false;
    CHECK(muiKeyInput(context, root, &tab, &handled) == mui_success, "tab");
    return handled;
}

static void TestLayers(void)
{
    // The panel's child an overlay: hit apart from the panel, until it
    // exits with it.
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    SetLayer(context, scene.child, mui_layerOverlay);
    Layout(context, scene.root, 0);
    CHECK(Same(HitAt(context, scene.root, 10.0f, 10.0f), scene.child), "the overlay hit");
    CHECK(muiNode_BeginExit(context, scene.panel) == mui_success, "exit");
    Layout(context, scene.root, 0);
    CHECK(Same(HitAt(context, scene.root, 10.0f, 10.0f), scene.root), "the overlay exits too");
    muiDestroyContext(context);
    // A modal child: it covers the rest until the panel exits.
    MakeScene(&scene);
    context = scene.context;
    SetLayer(context, scene.child, mui_layerModal);
    Focusable(context, scene.beside);
    Layout(context, scene.root, 0);
    CHECK(muiFocus_Set(context, 0, scene.beside, mui_focusByCode) == mui_errorInvalid &&
              !Tab(context, scene.root) &&
              Same(HitAt(context, scene.root, 210.0f, 10.0f), scene.child),
          "covered, blocked");
    CHECK(muiNode_BeginExit(context, scene.panel) == mui_success, "exit");
    Layout(context, scene.root, 0);
    CHECK(Same(HitAt(context, scene.root, 210.0f, 10.0f), scene.beside) &&
              Tab(context, scene.root) && Same(muiFocus_Get(context, 0), scene.beside),
          "no longer");
    CHECK(muiFocus_Set(context, 0, (muiNodeId){0, 0}, mui_focusByCode) == mui_success &&
              muiFocus_Set(context, 0, scene.beside, mui_focusByCode) == mui_success,
          "focusable by code");
    // The root exiting: nothing in it navigates.
    CHECK(muiFocus_Set(context, 0, (muiNodeId){0, 0}, mui_focusByCode) == mui_success &&
              muiNode_BeginExit(context, scene.root) == mui_success && !Tab(context, scene.root) &&
              muiFocus_Get(context, 0).index1 == 0,
          "the root exiting");
    muiDestroyContext(context);
}

static void TestOtherTree(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    muiNodeId other = Sized(context, s_nullNode, 10.0f, 10.0f);
    muiNodeId node = s_nullNode;
    CHECK(muiNode_BeginExit(context, other) == mui_success, "exit elsewhere");
    Layout(context, scene.root, 0);
    CHECK(Finished(context, &node) == 0, "not by another root's layout");
    Layout(context, other, 0);
    CHECK(Finished(context, &node) == 1 && Same(node, other), "by its own");
    muiDestroyContext(context);
}

static void SetExitLayout(muiContext* context, muiNodeId node, muiExitLayout policy)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.exitLayout = policy;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyExitLayout)) == mui_success,
          "exit layout");
}

static void TestPop(void)
{
    // A row of three at 0, 100 and 200; the middle one holds a child as
    // wide as it.
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Sized(context, s_nullNode, 400.0f, 100.0f);
    muiNodeId a = Sized(context, root, 100.0f, 50.0f);
    muiNodeId b = Sized(context, root, 100.0f, 50.0f);
    muiNodeId c = Sized(context, root, 100.0f, 50.0f);
    muiNodeId inside = Sized(context, b, 0.0f, 10.0f);
    muiLayoutStyle full = muiDefaultLayoutStyle();
    full.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    full.sizing.height = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, inside, &full,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "as large as its parent");
    Layout(context, root, 0);
    CHECK(muiDefaultInteractionStyle().exitLayout == mui_exitKeep &&
              muiNode_GetRect(context, c).x == 200.0f,
          "kept by default");
    // Kept: nothing moves.
    CHECK(muiNode_BeginExit(context, b) == mui_success, "exit, kept");
    Layout(context, root, 0);
    CHECK(muiNode_GetRect(context, c).x == 200.0f && muiNode_GetRect(context, b).x == 100.0f,
          "in place");
    CHECK(muiNode_CancelExit(context, b) == mui_success, "cancelled");
    // Popped: c closes up, b stays where it was, its child at its size.
    SetExitLayout(context, b, mui_exitPop);
    CHECK(muiNode_BeginExit(context, b) == mui_success, "exit, popped");
    Layout(context, root, 0);
    muiRect rb = muiNode_GetRect(context, b);
    CHECK(muiNode_GetRect(context, a).x == 0.0f && muiNode_GetRect(context, c).x == 100.0f &&
              rb.x == 100.0f && rb.y == 0.0f && rb.width == 100.0f && rb.height == 50.0f &&
              muiNode_GetRect(context, inside).width == 100.0f &&
              muiNode_GetRect(context, inside).height == 50.0f,
          "out of the flow, where it was");
    // Its style changing while it exits leaves it out.
    SetExitLayout(context, b, mui_exitKeep);
    Layout(context, root, 0);
    CHECK(muiNode_GetRect(context, c).x == 100.0f, "read when it began");
    // Cancelled: back in the flow.
    CHECK(muiNode_CancelExit(context, b) == mui_success, "cancelled");
    Layout(context, root, 0);
    CHECK(muiNode_GetRect(context, b).x == 100.0f && muiNode_GetRect(context, c).x == 200.0f,
          "back");
    // Popped and cancelled with the policy still pop: back too.
    SetExitLayout(context, b, mui_exitPop);
    CHECK(muiNode_BeginExit(context, b) == mui_success, "popped again");
    Layout(context, root, 0);
    CHECK(muiNode_GetRect(context, c).x == 100.0f && muiNode_CancelExit(context, b) == mui_success,
          "out, then cancelled");
    Layout(context, root, 0);
    CHECK(muiNode_GetRect(context, c).x == 200.0f, "back again");
    // An absolute node popped and cancelled stays absolute.
    muiLayoutStyle absolute = muiDefaultLayoutStyle();
    absolute.placement.position = mui_positionAbsolute;
    CHECK(muiNode_SetLayoutValues(context, a, &absolute, MUI_PROPERTY_BIT(mui_propertyPosition)) ==
              mui_success,
          "absolute");
    SetExitLayout(context, a, mui_exitPop);
    CHECK(muiNode_BeginExit(context, a) == mui_success &&
              muiNode_CancelExit(context, a) == mui_success,
          "popped and back");
    Layout(context, root, 0);
    CHECK(muiNode_GetRect(context, b).x == 0.0f, "still out of the flow");
    // Bad values refused.
    muiInteractionStyle bad = muiDefaultInteractionStyle();
    bad.exitLayout = 2;
    CHECK(muiNode_SetInteractionValues(
              context, b, &bad, MUI_PROPERTY_BIT(mui_propertyExitLayout)) == mui_errorInvalid,
          "a value past pop");
    muiDestroyContext(context);
}

// A node popped out of a right-to-left row stays where it was, however
// often the row is laid out again.
static void TestPopRightToLeft(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Sized(context, s_nullNode, 400.0f, 100.0f);
    muiLayoutStyle rtl = muiDefaultLayoutStyle();
    rtl.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, root, &rtl,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "right to left");
    muiNodeId a = Sized(context, root, 100.0f, 50.0f);
    muiNodeId b = Sized(context, root, 100.0f, 50.0f);
    Layout(context, root, 0);
    CHECK(muiNode_GetRect(context, a).x == 300.0f && muiNode_GetRect(context, b).x == 200.0f,
          "from the right");
    SetExitLayout(context, b, mui_exitPop);
    CHECK(muiNode_BeginExit(context, b) == mui_success, "popped");
    for (int i = 0; i < 3; i++)
    {
        CHECK(muiNode_MarkContentChanged(context, root) == mui_success, "the row again");
        Layout(context, root, 0);
        CHECK(muiNode_GetRect(context, b).x == 200.0f && muiNode_GetRect(context, a).x == 300.0f,
              "where it was, each time");
    }
    muiDestroyContext(context);
}

static void TestContract(void)
{
    muiContextDef def = muiDefaultContextDef();
    CHECK(def.limits.exits == 64, "64 by default");
    def.limits.exits = 2;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Sized(context, s_nullNode, 100.0f, 100.0f);
    muiNodeId a = Sized(context, root, 10.0f, 10.0f);
    muiNodeId b = Sized(context, root, 10.0f, 10.0f);
    muiNodeId c = Sized(context, root, 10.0f, 10.0f);
    CHECK(muiNode_BeginExit(context, a) == mui_success &&
              muiNode_BeginExit(context, b) == mui_success &&
              muiNode_BeginExit(context, c) == mui_errorCapacity &&
              (muiNode_GetStates(context, c) & mui_stateExiting) == 0,
          "full");
    CHECK(muiNode_CancelExit(context, b) == mui_success &&
              muiNode_BeginExit(context, c) == mui_success,
          "room after a cancel");
    CHECK(muiNode_CancelExit(context, b) == mui_success, "cancelling none");
    muiNodeId gone = a;
    CHECK(muiDestroyNode(context, a) == mui_success && muiNode_BeginExit(context, b) == mui_success,
          "room after a destroy");
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiNode_BeginExit(NULL, b) == mui_errorInvalid &&
              muiNode_CancelExit(NULL, b) == mui_errorInvalid &&
              muiNode_BeginExit(context, s_nullNode) == mui_errorInvalid &&
              muiNode_CancelExit(context, s_nullNode) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 2,
          "nulls");
    CHECK(muiNode_BeginExit(context, gone) == mui_errorStale &&
              muiNode_CancelExit(context, gone) == mui_errorStale,
          "stale");
    // A slot reused by a new node is not exiting.
    muiNodeId fresh = Sized(context, root, 10.0f, 10.0f);
    CHECK(fresh.index1 == gone.index1 && muiNode_GetStates(context, fresh) == 0, "a new node");
    muiDestroyContext(context);
}

int main(void)
{
    TestInteraction();
    TestFinished();
    TestLayers();
    TestOtherTree();
    TestPop();
    TestPopRightToLeft();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
