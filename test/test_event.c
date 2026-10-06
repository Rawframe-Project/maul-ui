// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Routed input (record mui-0007): the route's order, handled in either
// phase, targets under focus and modal layers, the defaults when nothing
// handles a key or an action, a route fixed against edits, pointer
// records, and calls outside the contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"

#include <stddef.h>
#include <string.h>

static const muiNodeId s_nullNode = {0, 0};

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static muiNodeId Box(muiContext* context, muiNodeId parent, float x, float y, float size,
                     muiFocusMode mode)
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
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focus mode");
    return node;
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

// What the host's function heard, and what it does.
typedef struct Heard
{
    muiContext* context;
    muiNodeId root;
    uint32_t count;
    muiNodeId nodes[16];
    muiPhase phases[16];
    muiEvent last;
    // Handles the event at this node in this phase.
    muiNodeId handleAt;
    muiPhase handlePhase;
    // Destroys this node when the root first hears an event.
    muiNodeId destroy;
    // What feeding input from the function gave.
    muiResult nested;
    muiResult nestedPointer;
} Heard;

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Heard* heard = user;
    if (heard->count < 16)
    {
        heard->nodes[heard->count] = nodeId;
        heard->phases[heard->count] = phase;
    }
    heard->count++;
    heard->last = *event;
    if (heard->destroy.index1 != 0 && Same(nodeId, heard->root))
    {
        CHECK(muiDestroyNode(heard->context, heard->destroy) == mui_success, "destroyed inside");
        heard->destroy = s_nullNode;
        const muiKeyEvent key = {0, 'x', 27, 0, true, false, 0};
        bool handled = false;
        heard->nested = muiKeyInput(heard->context, heard->root, &key, &handled);
        const muiPointerEvent move = {0, 1, mui_pointerMouse, mui_pointerMove, 0, 0, 1.0f, 1.0f, 0};
        heard->nestedPointer = muiPointerInput(heard->context, heard->root, &move);
        if (heard->nested == mui_errorInvalid)
        {
            heard->nested = muiSetEventFunction(heard->context, NULL, NULL);
        }
    }
    return Same(nodeId, heard->handleAt) && phase == heard->handlePhase;
}

// Whether the function heard these nodes in these phases, in order.
static bool Order(const Heard* heard, const muiNodeId* nodes, const muiPhase* phases,
                  uint32_t count)
{
    bool same = heard->count == count;
    for (uint32_t i = 0; same && i < count; i++)
    {
        same = Same(heard->nodes[i], nodes[i]) && heard->phases[i] == phases[i];
    }
    return same;
}

static bool Key(muiContext* context, muiNodeId root, muiKeyCode code, muiModifiers modifiers,
                bool down)
{
    const muiKeyEvent event = {7, MUI_KEY_NAMED | code, code, modifiers, down, false, 0};
    bool handled = false;
    CHECK(muiKeyInput(context, root, &event, &handled) == mui_success, "key");
    return handled;
}

// root holds a, which holds b; c beside a; all take focus but root.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId a;
    muiNodeId b;
    muiNodeId c;
    Heard heard;
} Scene;

static void MakeScene(Scene* scene)
{
    muiContextDef def = muiDefaultContextDef();
    *scene = (Scene){0};
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiContext* context = scene->context;
    scene->root = Box(context, s_nullNode, 0.0f, 0.0f, 500.0f, mui_focusNone);
    scene->a = Box(context, scene->root, 0.0f, 0.0f, 100.0f, mui_focusAll);
    scene->b = Box(context, scene->a, 10.0f, 10.0f, 20.0f, mui_focusAll);
    scene->c = Box(context, scene->root, 200.0f, 0.0f, 100.0f, mui_focusAll);
    Layout(context, scene->root);
    scene->heard = (Heard){.context = context, .root = scene->root};
    CHECK(muiSetEventFunction(context, Hear, &scene->heard) == mui_success, "function");
}

static void TestOrder(void)
{
    Scene s;
    MakeScene(&s);
    CHECK(muiFocus_Set(s.context, 0, s.b, mui_focusByCode) == mui_success, "focus b");
    const muiNodeId nodes[6] = {s.root, s.a, s.b, s.b, s.a, s.root};
    const muiPhase phases[6] = {mui_phaseTunnel, mui_phaseTunnel, mui_phaseTunnel,
                                mui_phaseBubble, mui_phaseBubble, mui_phaseBubble};
    CHECK(!Key(s.context, s.root, 27, 0, true) && Order(&s.heard, nodes, phases, 6) &&
              s.heard.last.kind == mui_eventKeyDown && s.heard.last.code == 27 &&
              s.heard.last.key == (MUI_KEY_NAMED | 27) && s.heard.last.timeNs == 7 &&
              Same(s.heard.last.target, s.b),
          "down to the focus and back up");
    // Handled tunnelling through a: nothing further, Tab moves nothing.
    s.heard.count = 0;
    s.heard.handleAt = s.a;
    s.heard.handlePhase = mui_phaseTunnel;
    CHECK(Key(s.context, s.root, mui_codeTab, 0, true) && Order(&s.heard, nodes, phases, 2) &&
              Same(muiFocus_Get(s.context, 0), s.b),
          "handled tunnelling");
    s.heard.count = 0;
    s.heard.handlePhase = mui_phaseBubble;
    CHECK(Key(s.context, s.root, 27, 0, false) && Order(&s.heard, nodes, phases, 5) &&
              s.heard.last.kind == mui_eventKeyUp,
          "handled bubbling");
    muiDestroyContext(s.context);
}

static void TestDefaults(void)
{
    Scene s;
    MakeScene(&s);
    // Tab order: a, b, c. With no focus, a key goes to the root.
    CHECK(!Key(s.context, s.root, 27, 0, true) && Same(s.heard.last.target, s.root) &&
              s.heard.count == 2,
          "no focus: the root");
    CHECK(Key(s.context, s.root, mui_codeTab, 0, true) && Same(muiFocus_Get(s.context, 0), s.a) &&
              Key(s.context, s.root, mui_codeTab, mui_modCapsLock, true) &&
              Same(muiFocus_Get(s.context, 0), s.b) &&
              Key(s.context, s.root, mui_codeTab, mui_modShift, true) &&
              Same(muiFocus_Get(s.context, 0), s.a),
          "Tab, with a lock on, and Shift+Tab");
    CHECK(!Key(s.context, s.root, mui_codeTab, mui_modControl, true) &&
              !Key(s.context, s.root, mui_codeTab, 0, false) &&
              Same(muiFocus_Get(s.context, 0), s.a),
          "Control+Tab and a key up: no default");
    CHECK(Key(s.context, s.root, mui_codeArrowRight, 0, true) &&
              Same(muiFocus_Get(s.context, 0), s.c) &&
              !Key(s.context, s.root, mui_codeArrowLeft, mui_modShift, true) &&
              Key(s.context, s.root, mui_codeArrowLeft, mui_modNumLock, true) &&
              Same(muiFocus_Get(s.context, 0), s.a) &&
              !Key(s.context, s.root, mui_codeArrowDown, 0, true) &&
              !Key(s.context, s.root, mui_codeArrowUp, 0, true) &&
              Same(muiFocus_Get(s.context, 0), s.a),
          "arrows, not with Shift; nothing up or down from a, b inside it");
    // Navigation.
    bool handled = false;
    muiNavigationEvent nav = {3, mui_navigateNext, 0};
    CHECK(muiNavigationInput(s.context, s.root, &nav, &handled) == mui_success && handled &&
              Same(muiFocus_Get(s.context, 0), s.b) && s.heard.last.kind == mui_eventNavigation &&
              s.heard.last.navigation == mui_navigateNext,
          "next");
    nav.action = mui_navigatePrevious;
    CHECK(muiNavigationInput(s.context, s.root, &nav, &handled) == mui_success && handled &&
              Same(muiFocus_Get(s.context, 0), s.a),
          "previous");
    nav.action = mui_navigateRight;
    CHECK(muiNavigationInput(s.context, s.root, &nav, &handled) == mui_success && handled &&
              Same(muiFocus_Get(s.context, 0), s.c),
          "right");
    nav.action = mui_navigateDown;
    CHECK(muiNavigationInput(s.context, s.root, &nav, &handled) == mui_success && !handled,
          "nothing below c");
    nav.action = mui_navigateActivate;
    CHECK(muiNavigationInput(s.context, s.root, &nav, &handled) == mui_success && !handled &&
              s.heard.last.navigation == mui_navigateActivate,
          "activate: no default");
    nav.action = mui_navigateCancel;
    CHECK(muiNavigationInput(s.context, s.root, &nav, &handled) == mui_success && !handled,
          "cancel: no default");
    // Without a function, the defaults still run.
    CHECK(muiSetEventFunction(s.context, NULL, NULL) == mui_success &&
              Key(s.context, s.root, mui_codeArrowLeft, 0, true) &&
              Same(muiFocus_Get(s.context, 0), s.a),
          "no function");
    muiDestroyContext(s.context);
}

static void TestText(void)
{
    Scene s;
    MakeScene(&s);
    CHECK(muiFocus_Set(s.context, 2, s.c, mui_focusByCode) == mui_success, "focus c");
    const muiTextEvent text = {9, "h\xC3\xA9", 3, 2};
    bool handled = true;
    CHECK(muiTextInput(s.context, s.root, &text, &handled) == mui_success && !handled &&
              s.heard.count == 4 && s.heard.last.kind == mui_eventText &&
              s.heard.last.length == 3 && memcmp(s.heard.last.text, "h\xC3\xA9", 3) == 0 &&
              s.heard.last.player == 2 && Same(s.heard.last.target, s.c),
          "text to player 2's focus");
    s.heard.handleAt = s.c;
    s.heard.handlePhase = mui_phaseBubble;
    const muiTextEvent empty = {9, NULL, 0, 2};
    CHECK(muiTextInput(s.context, s.root, &empty, &handled) == mui_success && handled,
          "empty text, handled");
    muiDestroyContext(s.context);
}

static void TestTargets(void)
{
    Scene s;
    MakeScene(&s);
    // A modal layer m over the focused b: input goes to m.
    muiNodeId m = Box(s.context, s.root, 300.0f, 300.0f, 50.0f, mui_focusNone);
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.layer = mui_layerModal;
    CHECK(muiFocus_Set(s.context, 0, s.b, mui_focusByCode) == mui_success &&
              muiNode_SetInteractionValues(s.context, m, &values,
                                           MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "modal");
    Layout(s.context, s.root);
    CHECK(!Key(s.context, s.root, mui_codeEscape, 0, true) && Same(s.heard.last.target, m),
          "a covered focus: the modal layer");
    // Focus in another tree: the root.
    muiNodeId other = Box(s.context, s_nullNode, 0.0f, 0.0f, 10.0f, mui_focusAll);
    CHECK(muiFocus_Set(s.context, 0, other, mui_focusByCode) == mui_success &&
              !Key(s.context, s.root, 27, 0, true) && Same(s.heard.last.target, m),
          "focus elsewhere: the top modal layer here");
    muiDestroyContext(s.context);
}

static void TestEdits(void)
{
    // The root's function destroys a in the tunnel: a and b hear nothing,
    // the root bubbles; input fed from inside is refused.
    Scene s;
    MakeScene(&s);
    CHECK(muiFocus_Set(s.context, 0, s.b, mui_focusByCode) == mui_success, "focus b");
    s.heard.destroy = s.a;
    const muiNodeId nodes[2] = {s.root, s.root};
    const muiPhase phases[2] = {mui_phaseTunnel, mui_phaseBubble};
    CHECK(!Key(s.context, s.root, 27, 0, true) && Order(&s.heard, nodes, phases, 2) &&
              s.heard.nested == mui_errorInvalid && s.heard.nestedPointer == mui_errorInvalid,
          "the route kept, those gone passed over; no input inside");
    bool handled = false;
    const muiPointerRecord record = {.kind = mui_pointerRecordClick, .node = s.c};
    s.heard.destroy = s.c;
    s.heard.count = 0;
    CHECK(muiDispatchPointerRecord(s.context, &record, &handled) == mui_success && !handled &&
              s.heard.count == 2 && s.heard.last.kind == mui_eventPointer &&
              s.heard.last.pointer == &record,
          "a pointer record whose node goes");
    muiDestroyContext(s.context);
}

static void TestPointerRecords(void)
{
    Scene s;
    MakeScene(&s);
    const muiPointerEvent press = {0, 1, mui_pointerMouse, mui_pointerPress, 0, 1, 15.0f, 15.0f, 0};
    muiPointerRecord record = {0};
    CHECK(muiPointerInput(s.context, s.root, &press) == mui_success &&
              muiNextPointerRecord(s.context, &record) == mui_success,
          "a press record");
    s.heard.handleAt = s.b;
    s.heard.handlePhase = mui_phaseBubble;
    bool handled = false;
    CHECK(muiDispatchPointerRecord(s.context, &record, &handled) == mui_success && handled &&
              s.heard.count == 4 && Same(s.heard.last.target, s.b) &&
              s.heard.last.pointer->kind == mui_pointerRecordPress,
          "routed to its node");
    const muiPointerRecord nothing = {.kind = mui_pointerRecordRelease};
    s.heard.count = 0;
    CHECK(muiDispatchPointerRecord(s.context, &nothing, &handled) == mui_success && !handled &&
              s.heard.count == 0,
          "a record over nothing");
    // A press focuses by pointer; a key after it makes code focus shown.
    CHECK(Same(muiFocus_Get(s.context, 0), s.b) &&
              muiFocus_Set(s.context, 0, s.c, mui_focusByCode) == mui_success &&
              (muiNode_GetStates(s.context, s.c) & mui_stateFocusVisible) == 0 &&
              !Key(s.context, s.root, 27, 0, false) &&
              muiFocus_Set(s.context, 0, s.a, mui_focusByCode) == mui_success &&
              (muiNode_GetStates(s.context, s.a) & mui_stateFocusVisible) == 0 &&
              !Key(s.context, s.root, 27, 0, true) &&
              muiFocus_Set(s.context, 0, s.c, mui_focusByCode) == mui_success &&
              (muiNode_GetStates(s.context, s.c) & mui_stateFocusVisible) != 0,
          "a key down after a pointer shows code focus; a key up does not");
    muiDestroyContext(s.context);
}

static void TestContract(void)
{
    Scene s;
    MakeScene(&s);
    muiContext* context = s.context;
    bool handled = false;
    const muiKeyEvent key = {0, 'a', 4, 0, true, false, 0};
    muiKeyEvent badKey = key;
    badKey.player = MUI_MAX_PLAYERS;
    const muiTextEvent badText = {0, NULL, 1, 0};
    const muiNavigationEvent badNav = {0, mui_navigateCancel + 1, 0};
    const muiNavigationEvent nav = {0, mui_navigateUp, 0};
    const muiTextEvent text = {0, "a", 1, 0};
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiKeyInput(NULL, s.root, &key, &handled) == mui_errorInvalid &&
              muiKeyInput(context, s.root, NULL, &handled) == mui_errorInvalid &&
              muiKeyInput(context, s.root, &key, NULL) == mui_errorInvalid &&
              muiKeyInput(context, s_nullNode, &key, &handled) == mui_errorInvalid &&
              muiKeyInput(context, s.root, &badKey, &handled) == mui_errorInvalid &&
              muiTextInput(NULL, s.root, &text, &handled) == mui_errorInvalid &&
              muiTextInput(context, s.root, &badText, &handled) == mui_errorInvalid &&
              muiTextInput(context, s.root, &text, NULL) == mui_errorInvalid &&
              muiNavigationInput(NULL, s.root, &nav, &handled) == mui_errorInvalid &&
              muiNavigationInput(context, s.root, &badNav, &handled) == mui_errorInvalid &&
              muiNavigationInput(context, s.root, &nav, NULL) == mui_errorInvalid &&
              muiDispatchPointerRecord(NULL, &(muiPointerRecord){0}, &handled) ==
                  mui_errorInvalid &&
              muiDispatchPointerRecord(context, NULL, &handled) == mui_errorInvalid &&
              muiSetEventFunction(NULL, NULL, NULL) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 9 && s.heard.count == 0,
          "outside the contract");
    CHECK(muiDestroyNode(context, s.root) == mui_success &&
              muiKeyInput(context, s.root, &key, &handled) == mui_errorStale &&
              muiTextInput(context, s.root, &text, &handled) == mui_errorStale &&
              muiNavigationInput(context, s.root, &nav, &handled) == mui_errorStale,
          "a root gone");
    muiDestroyContext(context);
}

int main(void)
{
    TestOrder();
    TestDefaults();
    TestText();
    TestTargets();
    TestEdits();
    TestPointerRecords();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
