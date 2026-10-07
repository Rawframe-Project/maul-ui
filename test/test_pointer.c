// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pointer input (record mui-0007): hover and press along chains, clicks
// and their counts, capture, cancels, several pointers, tree edits under
// pointers, the record ring and calls outside the contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <math.h>
#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

static const uint64_t s_ms = 1000000u;

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

// A node of a size under parent (none for a root), placed absolutely at
// x, y in its parent's border box.
static muiNodeId Place(muiContext* context, muiNodeId parent, float x, float y, float width,
                       float height)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = Length(width);
    layout.sizing.height = Length(height);
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
    return node;
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static muiContext* MakeContext(uint32_t pointers, uint32_t records)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.pointers = pointers;
    def.limits.pointerRecords = records;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    return context;
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static muiResult Send(muiContext* context, muiNodeId root, uint32_t pointer, muiPointerKind kind,
                      muiPointerAction action, uint8_t button, muiPointerButtons buttons, float x,
                      float y, uint64_t timeNs)
{
    const muiPointerEvent event = {timeNs, pointer, kind, action, button, buttons, x, y, 0};
    return muiPointerInput(context, root, &event);
}

// A mouse event of pointer 1.
static void Mouse(muiContext* context, muiNodeId root, muiPointerAction action, uint8_t button,
                  muiPointerButtons buttons, float x, float y, uint64_t timeNs)
{
    CHECK(Send(context, root, 1, mui_pointerMouse, action, button, buttons, x, y, timeNs) ==
              mui_success,
          "mouse event");
}

// Whether the next record is of a kind, on a node, at a point, with a
// button and click count.
static bool Next(muiContext* context, muiPointerRecordKind kind, muiNodeId node, float x, float y,
                 uint8_t button, uint32_t clickCount)
{
    muiPointerRecord record = {0};
    return muiNextPointerRecord(context, &record) == mui_success && record.kind == kind &&
           Same(record.node, node) && record.x == x && record.y == y && record.button == button &&
           record.clickCount == clickCount;
}

static bool Drained(muiContext* context)
{
    muiPointerRecord record = {0};
    return muiNextPointerRecord(context, &record) == mui_empty;
}

static bool IsHovered(const muiContext* context, muiNodeId node)
{
    return (muiNode_GetStates(context, node) & mui_stateHovered) != 0;
}

static bool IsPressed(const muiContext* context, muiNodeId node)
{
    return (muiNode_GetStates(context, node) & mui_statePressed) != 0;
}

// root (0, 0, 200, 200) holds a (10, 10, 100, 100), which holds b
// (10, 10, 30, 30) and c (50, 10, 30, 30); d (150, 150, 40, 40) is
// root's.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId a;
    muiNodeId b;
    muiNodeId c;
    muiNodeId d;
} Scene;

static Scene MakeScene(uint32_t pointers, uint32_t records)
{
    Scene scene = {.context = MakeContext(pointers, records)};
    scene.root = Place(scene.context, s_nullNode, 0.0f, 0.0f, 200.0f, 200.0f);
    scene.a = Place(scene.context, scene.root, 10.0f, 10.0f, 100.0f, 100.0f);
    scene.b = Place(scene.context, scene.a, 10.0f, 10.0f, 30.0f, 30.0f);
    scene.c = Place(scene.context, scene.a, 50.0f, 10.0f, 30.0f, 30.0f);
    scene.d = Place(scene.context, scene.root, 150.0f, 150.0f, 40.0f, 40.0f);
    Layout(scene.context, scene.root);
    return scene;
}

static void TestHover(void)
{
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    CHECK(muiNode_SetStates(context, s.a, mui_stateChecked) == mui_success, "a checked");
    Mouse(context, s.root, mui_pointerMove, 0, 0, 25.0f, 25.0f, 0);
    CHECK(IsHovered(context, s.b) && IsHovered(context, s.a) && IsHovered(context, s.root) &&
              !IsHovered(context, s.c) && !IsHovered(context, s.d) &&
              muiNode_GetStates(context, s.a) == (mui_stateChecked | mui_stateHovered),
          "the chain of b hovered, the host's state kept");
    Mouse(context, s.root, mui_pointerMove, 0, 0, 160.0f, 160.0f, 1);
    CHECK(IsHovered(context, s.d) && IsHovered(context, s.root) && !IsHovered(context, s.a) &&
              !IsHovered(context, s.b) && muiNode_GetStates(context, s.a) == mui_stateChecked,
          "moved to d");
    CHECK(muiNode_SetStates(context, s.d, 0) == mui_success && IsHovered(context, s.d),
          "the host's states leave hover");
    Mouse(context, s.root, mui_pointerLeave, 0, 0, 160.0f, 160.0f, 2);
    muiPointerState state = {0};
    CHECK(!IsHovered(context, s.d) && !IsHovered(context, s.root) &&
              muiPointer_GetState(context, 1, &state) == mui_empty && Drained(context),
          "left: forgotten, no records");
    muiDestroyContext(context);
}

static void TestStyledHover(void)
{
    // A class whose hovered variant paints red: styling sees pointer
    // hover as it sees the host's states.
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    muiStyleId style;
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){1.0f, 0.0f, 0.0f, 1.0f};
    CHECK(muiCreateStyle(context, &style) == mui_success &&
              muiStyle_SetVisualValues(context, style, mui_variantHovered, &visual,
                                       MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success &&
              muiNode_SetClasses(context, s.a, &style, 1) == mui_success,
          "a class");
    Layout(context, s.root);
    muiVisualStyle read = muiDefaultVisualStyle();
    CHECK(muiNode_GetVisualStyle(context, s.a, &read) == mui_success && read.background.a == 0.0f,
          "not hovered");
    Mouse(context, s.root, mui_pointerMove, 0, 0, 25.0f, 25.0f, 0);
    Layout(context, s.root);
    CHECK(muiNode_GetVisualStyle(context, s.a, &read) == mui_success && read.background.r == 1.0f,
          "hovered through b");
    Mouse(context, s.root, mui_pointerMove, 0, 0, 160.0f, 160.0f, 1);
    Layout(context, s.root);
    CHECK(muiNode_GetVisualStyle(context, s.a, &read) == mui_success && read.background.a == 0.0f,
          "no longer");
    muiDestroyContext(context);
}

static void TestClicks(void)
{
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    const muiPointerButtons primary = 1u << mui_buttonPrimary;
    // Press and release on b: a click on b.
    Mouse(context, s.root, mui_pointerPress, mui_buttonPrimary, primary, 25.0f, 25.0f, 0);
    CHECK(IsPressed(context, s.b) && IsPressed(context, s.a) && IsPressed(context, s.root) &&
              !IsPressed(context, s.c),
          "the chain of b pressed");
    Mouse(context, s.root, mui_pointerMove, 0, primary, 160.0f, 160.0f, 1 * s_ms);
    CHECK(IsPressed(context, s.b) && !IsPressed(context, s.d) && IsHovered(context, s.d) &&
              !IsHovered(context, s.b),
          "pressed where it began, hovering where it is");
    Mouse(context, s.root, mui_pointerMove, 0, primary, 25.0f, 25.0f, 2 * s_ms);
    Mouse(context, s.root, mui_pointerRelease, mui_buttonPrimary, 0, 25.0f, 25.0f, 3 * s_ms);
    CHECK(!IsPressed(context, s.b) && !IsPressed(context, s.root) && IsHovered(context, s.b),
          "released");
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              Next(context, mui_pointerRecordRelease, s.b, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              Next(context, mui_pointerRecordClick, s.b, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              Drained(context),
          "press, release, click; no moves while not captured");
    // Released on c: the click is a's, their common ancestor.
    Mouse(context, s.root, mui_pointerPress, mui_buttonPrimary, primary, 25.0f, 25.0f, 1000 * s_ms);
    Mouse(context, s.root, mui_pointerRelease, mui_buttonPrimary, 0, 65.0f, 25.0f, 1001 * s_ms);
    muiPointerRecord record = {0};
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              Next(context, mui_pointerRecordRelease, s.c, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              muiNextPointerRecord(context, &record) == mui_success &&
              record.kind == mui_pointerRecordClick && Same(record.node, s.a) &&
              record.x == 55.0f && record.y == 15.0f && !record.passThrough && Drained(context),
          "a click on the common ancestor, blocking");
    // Released outside the root: nothing is clicked.
    Mouse(context, s.root, mui_pointerPress, mui_buttonPrimary, primary, 25.0f, 25.0f, 2000 * s_ms);
    Mouse(context, s.root, mui_pointerRelease, mui_buttonPrimary, 0, 500.0f, 500.0f, 2001 * s_ms);
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              muiNextPointerRecord(context, &record) == mui_success &&
              record.kind == mui_pointerRecordRelease && record.node.index1 == 0 &&
              record.passThrough && record.x == 500.0f && Drained(context),
          "released over nothing: passes through, no click");
    // A chord: the secondary button clicks too, naming itself.
    const muiPointerButtons both = primary | 1u << mui_buttonSecondary;
    Mouse(context, s.root, mui_pointerPress, mui_buttonPrimary, primary, 25.0f, 25.0f, 3000 * s_ms);
    Mouse(context, s.root, mui_pointerPress, mui_buttonSecondary, both, 65.0f, 25.0f, 3001 * s_ms);
    CHECK(IsPressed(context, s.b) && !IsPressed(context, s.c), "the first press holds");
    Mouse(context, s.root, mui_pointerRelease, mui_buttonSecondary, primary, 65.0f, 25.0f,
          3002 * s_ms);
    Mouse(context, s.root, mui_pointerRelease, mui_buttonPrimary, 0, 25.0f, 25.0f, 3003 * s_ms);
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              Next(context, mui_pointerRecordPress, s.c, 5.0f, 5.0f, mui_buttonSecondary, 1) &&
              Next(context, mui_pointerRecordRelease, s.c, 5.0f, 5.0f, mui_buttonSecondary, 1) &&
              Next(context, mui_pointerRecordClick, s.a, 55.0f, 15.0f, mui_buttonSecondary, 1) &&
              Next(context, mui_pointerRecordRelease, s.b, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              Next(context, mui_pointerRecordClick, s.b, 5.0f, 5.0f, mui_buttonPrimary, 1) &&
              Drained(context),
          "a chord");
    muiDestroyContext(context);
}

// Presses and releases the primary button at x, y and returns the
// press's click count.
static uint32_t ClickAt(muiContext* context, muiNodeId root, uint8_t button, float x, float y,
                        uint64_t timeNs)
{
    Mouse(context, root, mui_pointerPress, button, (muiPointerButtons)(1u << button), x, y, timeNs);
    Mouse(context, root, mui_pointerRelease, button, 0, x, y, timeNs + s_ms);
    muiPointerRecord record = {0};
    uint32_t count = 0;
    while (muiNextPointerRecord(context, &record) == mui_success)
    {
        count = record.kind == mui_pointerRecordPress ? record.clickCount : count;
    }
    return count;
}

static void TestClickCount(void)
{
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    CHECK(ClickAt(context, s.root, 0, 1.0f, 1.0f, 0) == 1, "the first press");
    CHECK(Send(context, s.root, 4, mui_pointerTouch, mui_pointerPress, 0, 1, 1.0f, 1.0f,
               100 * s_ms) == mui_success &&
              Next(context, mui_pointerRecordPress, s.root, 1.0f, 1.0f, 0, 1) &&
              Send(context, s.root, 4, mui_pointerTouch, mui_pointerCancel, 0, 0, 1.0f, 1.0f,
                   100 * s_ms) == mui_success &&
              Next(context, mui_pointerRecordCancel, s.root, 1.0f, 1.0f, 0, 0),
          "a touch after a mouse starts again");
    while (muiNextPointerRecord(context, &(muiPointerRecord){0}) == mui_success)
    {
    }
    CHECK(ClickAt(context, s.root, 0, 25.0f, 25.0f, 10000 * s_ms) == 1 &&
              ClickAt(context, s.root, 0, 27.0f, 23.0f, 10500 * s_ms) == 2 &&
              ClickAt(context, s.root, 0, 27.0f, 23.0f, 11000 * s_ms) == 3 &&
              ClickAt(context, s.root, 0, 27.0f, 23.0f, 11501 * s_ms) == 1,
          "a series within 500 ms and 2 units");
    CHECK(ClickAt(context, s.root, 0, 30.0f, 23.0f, 11600 * s_ms) == 1 &&
              ClickAt(context, s.root, 0, 30.0f, 25.5f, 11700 * s_ms) == 1 &&
              ClickAt(context, s.root, 1, 30.0f, 25.5f, 11800 * s_ms) == 1 &&
              ClickAt(context, s.root, 1, 30.0f, 25.5f, 11700 * s_ms) == 1,
          "too far on either axis, another button, or back in time");
    CHECK(muiSetClickRule(context, 100 * s_ms, 10.0f) == mui_success &&
              ClickAt(context, s.root, 0, 25.0f, 25.0f, 5000 * s_ms) == 1 &&
              ClickAt(context, s.root, 0, 35.0f, 15.0f, 5100 * s_ms) == 2 &&
              ClickAt(context, s.root, 0, 35.0f, 15.0f, 5201 * s_ms) == 1,
          "the host's rule");
    muiDestroyContext(context);
}

static void TestTouch(void)
{
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    const muiPointerButtons contact = 1u << mui_buttonPrimary;
    // A touch hovers nothing until in contact.
    CHECK(Send(context, s.root, 7, mui_pointerTouch, mui_pointerMove, 0, 0, 25.0f, 25.0f, 0) ==
                  mui_success &&
              !IsHovered(context, s.b),
          "no hover out of contact");
    CHECK(Send(context, s.root, 7, mui_pointerTouch, mui_pointerPress, mui_buttonPrimary, contact,
               25.0f, 25.0f, 1) == mui_success &&
              IsHovered(context, s.b) && IsPressed(context, s.b),
          "in contact");
    // Captured: it hovers b wherever it goes, and b gets its moves.
    CHECK(Send(context, s.root, 7, mui_pointerTouch, mui_pointerMove, 0, contact, 160.0f, 160.0f,
               2) == mui_success &&
              IsHovered(context, s.b) && !IsHovered(context, s.d),
          "captured hover");
    muiPointerState state = {0};
    CHECK(muiPointer_GetState(context, 7, &state) == mui_success &&
              state.kind == mui_pointerTouch && state.buttons == contact && state.x == 160.0f &&
              Same(state.hovered, s.b) && Same(state.pressed, s.b) && Same(state.captured, s.b),
          "its state");
    CHECK(Send(context, s.root, 7, mui_pointerTouch, mui_pointerRelease, mui_buttonPrimary, 0,
               160.0f, 160.0f, 3) == mui_success &&
              !IsHovered(context, s.b) && !IsPressed(context, s.b) &&
              muiPointer_GetState(context, 7, &state) == mui_empty,
          "lifted: forgotten");
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, 0, 1) &&
              Next(context, mui_pointerRecordMove, s.b, 140.0f, 140.0f, 0, 0) &&
              Next(context, mui_pointerRecordRelease, s.b, 140.0f, 140.0f, 0, 1) &&
              Next(context, mui_pointerRecordClick, s.b, 140.0f, 140.0f, 0, 1) &&
              Next(context, mui_pointerRecordCaptureLost, s.b, 160.0f, 160.0f, 0, 0) &&
              Drained(context),
          "the capture target's records");
    muiDestroyContext(context);
}

static void TestCapture(void)
{
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    const muiPointerButtons primary = 1u << mui_buttonPrimary;
    Mouse(context, s.root, mui_pointerMove, 0, 0, 25.0f, 25.0f, 0);
    CHECK(muiPointer_SetCapture(context, 1, s.c) == mui_errorInvalid,
          "no capture without a button");
    Mouse(context, s.root, mui_pointerPress, mui_buttonPrimary, primary, 25.0f, 25.0f, 1);
    CHECK(muiPointer_SetCapture(context, 1, s.c) == mui_success && IsHovered(context, s.c) &&
              !IsHovered(context, s.b) && IsPressed(context, s.b),
          "captured by c, hovering it at once");
    CHECK(muiPointer_SetCapture(context, 1, s.c) == mui_success, "again: nothing");
    Mouse(context, s.root, mui_pointerMove, 0, primary, 160.0f, 160.0f, 2);
    CHECK(muiPointer_SetCapture(context, 1, s.d) == mui_success, "moved to d");
    CHECK(muiPointer_ReleaseCapture(context, 1) == mui_success &&
              muiPointer_ReleaseCapture(context, 1) == mui_success,
          "released, twice");
    Mouse(context, s.root, mui_pointerMove, 0, primary, 25.0f, 25.0f, 3);
    CHECK(IsHovered(context, s.b) && !IsHovered(context, s.d), "hit testing again");
    Mouse(context, s.root, mui_pointerRelease, mui_buttonPrimary, 0, 25.0f, 25.0f, 4);
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, 0, 1) &&
              Next(context, mui_pointerRecordMove, s.c, 100.0f, 140.0f, 0, 0) &&
              Next(context, mui_pointerRecordCaptureLost, s.c, 160.0f, 160.0f, 0, 0) &&
              Next(context, mui_pointerRecordCaptureLost, s.d, 160.0f, 160.0f, 0, 0) &&
              Next(context, mui_pointerRecordRelease, s.b, 5.0f, 5.0f, 0, 1) &&
              Next(context, mui_pointerRecordClick, s.b, 5.0f, 5.0f, 0, 1) && Drained(context),
          "capture records");
    // A captured mouse that leaves the surface keeps hovering its target.
    Mouse(context, s.root, mui_pointerPress, mui_buttonPrimary, primary, 25.0f, 25.0f, 1000 * s_ms);
    CHECK(muiPointer_SetCapture(context, 1, s.b) == mui_success, "captured");
    Mouse(context, s.root, mui_pointerLeave, 0, primary, -5.0f, 25.0f, 1001 * s_ms);
    CHECK(IsHovered(context, s.b), "still hovering its target");
    Mouse(context, s.root, mui_pointerRelease, mui_buttonPrimary, 0, -5.0f, 25.0f, 1002 * s_ms);
    muiPointerState state = {0};
    CHECK(!IsHovered(context, s.b) && muiPointer_GetState(context, 1, &state) == mui_success &&
              state.hovered.index1 == 0,
          "released outside: hovering nothing");
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, 0, 1) &&
              Next(context, mui_pointerRecordRelease, s.b, -25.0f, 5.0f, 0, 1) &&
              Next(context, mui_pointerRecordClick, s.b, -25.0f, 5.0f, 0, 1) &&
              Next(context, mui_pointerRecordCaptureLost, s.b, -5.0f, 25.0f, 0, 0) &&
              Drained(context),
          "leaving is no move");
    // Pressed over nothing, then captured: the release clicks the target.
    Mouse(context, s.root, mui_pointerPress, mui_buttonPrimary, primary, 500.0f, 5.0f, 2000 * s_ms);
    CHECK(muiPointer_SetCapture(context, 1, s.c) == mui_success, "captured");
    Mouse(context, s.root, mui_pointerRelease, mui_buttonPrimary, 0, 500.0f, 5.0f, 2001 * s_ms);
    muiPointerRecord record = {0};
    CHECK(muiNextPointerRecord(context, &record) == mui_success && record.node.index1 == 0 &&
              Next(context, mui_pointerRecordRelease, s.c, 440.0f, -15.0f, 0, 1) &&
              Next(context, mui_pointerRecordClick, s.c, 440.0f, -15.0f, 0, 1),
          "a click on the capture target");
    muiDestroyContext(context);
}

static void TestCancel(void)
{
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    const muiPointerButtons contact = 1u << mui_buttonPrimary;
    CHECK(Send(context, s.root, 3, mui_pointerPen, mui_pointerPress, 0, contact, 65.0f, 25.0f, 0) ==
                  mui_success &&
              Send(context, s.root, 3, mui_pointerPen, mui_pointerCancel, 0, 0, 65.0f, 25.0f, 1) ==
                  mui_success,
          "pressed, cancelled");
    muiPointerState state = {0};
    CHECK(!IsPressed(context, s.c) && !IsHovered(context, s.c) &&
              muiPointer_GetState(context, 3, &state) == mui_empty &&
              Next(context, mui_pointerRecordPress, s.c, 5.0f, 5.0f, 0, 1) &&
              Next(context, mui_pointerRecordCancel, s.c, 5.0f, 5.0f, 0, 0) &&
              Next(context, mui_pointerRecordCaptureLost, s.c, 65.0f, 25.0f, 0, 0) &&
              Drained(context),
          "a cancel: no click");
    // Cancelling a pointer that holds nothing, or one unknown, posts
    // nothing.
    CHECK(Send(context, s.root, 3, mui_pointerPen, mui_pointerMove, 0, 0, 65.0f, 25.0f, 2) ==
                  mui_success &&
              Send(context, s.root, 3, mui_pointerPen, mui_pointerCancel, 0, 0, 65.0f, 25.0f, 3) ==
                  mui_success &&
              Send(context, s.root, 9, mui_pointerPen, mui_pointerCancel, 0, 0, 65.0f, 25.0f, 3) ==
                  mui_success &&
              Drained(context) && !IsHovered(context, s.c),
          "nothing to cancel");
    muiDestroyContext(context);
}

static void TestSeveral(void)
{
    // A mouse over b and a touch on c: a is hovered while either is.
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    const muiPointerButtons contact = 1u << mui_buttonPrimary;
    Mouse(context, s.root, mui_pointerMove, 0, 0, 25.0f, 25.0f, 0);
    CHECK(Send(context, s.root, 5, mui_pointerTouch, mui_pointerPress, 0, contact, 65.0f, 25.0f,
               1) == mui_success &&
              IsHovered(context, s.b) && IsHovered(context, s.c) && IsPressed(context, s.a) &&
              !IsPressed(context, s.b),
          "both");
    Mouse(context, s.root, mui_pointerMove, 0, 0, 160.0f, 160.0f, 2);
    CHECK(!IsHovered(context, s.b) && IsHovered(context, s.a) && IsHovered(context, s.d),
          "a still hovered by the touch");
    CHECK(Send(context, s.root, 5, mui_pointerTouch, mui_pointerRelease, 0, 0, 65.0f, 25.0f, 3) ==
                  mui_success &&
              !IsHovered(context, s.a) && !IsPressed(context, s.a) && IsHovered(context, s.root),
          "the touch lifted");
    muiDestroyContext(context);
}

static void TestEdits(void)
{
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    const muiPointerButtons primary = 1u << mui_buttonPrimary;
    Mouse(context, s.root, mui_pointerPress, mui_buttonPrimary, primary, 25.0f, 25.0f, 0);
    // a moves out of the tree with b in it: root is no longer hovered or
    // pressed, a and b still are; back in, root is again.
    CHECK(muiNode_Detach(context, s.a) == mui_success && !IsHovered(context, s.root) &&
              !IsPressed(context, s.root) && IsHovered(context, s.a) && IsPressed(context, s.b),
          "detached under the pointer");
    CHECK(muiNode_InsertChild(context, s.d, s.a, s_nullNode) == mui_success &&
              IsHovered(context, s.d) && IsPressed(context, s.d) && IsHovered(context, s.root),
          "inserted under d");
    // Edits away from the pointer's chain leave it.
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId e = s_nullNode;
    CHECK(muiCreateNode(context, &def, &e) == mui_success &&
              muiNode_InsertChild(context, s.root, e, s_nullNode) == mui_success &&
              muiNode_Detach(context, e) == mui_success &&
              muiDestroyNode(context, e) == mui_success && IsHovered(context, s.root) &&
              IsPressed(context, s.a),
          "other edits");
    CHECK(muiPointer_SetCapture(context, 1, s.b) == mui_success, "captured");
    while (muiNextPointerRecord(context, &(muiPointerRecord){0}) == mui_success)
    {
    }
    // b destroyed: its chain is let go, its capture lost, nothing clicked.
    CHECK(muiDestroyNode(context, s.b) == mui_success && !IsHovered(context, s.a) &&
              !IsPressed(context, s.a) && !IsHovered(context, s.root) && !IsPressed(context, s.d),
          "destroyed under the pointer");
    muiPointerState state = {0};
    CHECK(muiPointer_GetState(context, 1, &state) == mui_success && state.hovered.index1 == 0 &&
              state.pressed.index1 == 0 && state.captured.index1 == 0 &&
              Next(context, mui_pointerRecordCaptureLost, s.b, 25.0f, 25.0f, 0, 0) &&
              Drained(context),
          "its pointer");
    // A node made in b's slot is not hovered.
    muiNodeId f = s_nullNode;
    CHECK(muiCreateNode(context, &def, &f) == mui_success && f.index1 == s.b.index1 &&
              muiNode_GetStates(context, f) == 0,
          "the slot again");
    Mouse(context, s.root, mui_pointerRelease, mui_buttonPrimary, 0, 25.0f, 25.0f, 1);
    CHECK(Next(context, mui_pointerRecordRelease, s.root, 25.0f, 25.0f, 0, 1) && Drained(context),
          "a release, no click");
    muiDestroyContext(context);
}

static void TestInnerRoot(void)
{
    // Input to the subtree of b, whose rectangle is in a's space: a
    // touch captured by b moves outside it.
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    CHECK(Send(context, s.b, 2, mui_pointerTouch, mui_pointerPress, 0, 1, 15.0f, 15.0f, 0) ==
                  mui_success &&
              Send(context, s.b, 2, mui_pointerTouch, mui_pointerMove, 0, 1, 100.0f, 100.0f, 1) ==
                  mui_success &&
              Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, 0, 1) &&
              Next(context, mui_pointerRecordMove, s.b, 90.0f, 90.0f, 0, 0),
          "points in b's box");
    muiDestroyContext(context);
}

static void TestModal(void)
{
    // m, a modal layer: a press outside it goes to m, blocked.
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    muiNodeId m = Place(context, s.root, 120.0f, 0.0f, 50.0f, 50.0f);
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.layer = mui_layerModal;
    values.passThrough = true;
    CHECK(muiNode_SetInteractionValues(context, m, &values,
                                       MUI_PROPERTY_BIT(mui_propertyLayer) |
                                           MUI_PROPERTY_BIT(mui_propertyPassThrough)) ==
              mui_success,
          "modal, passing through where it is hit");
    Layout(context, s.root);
    Mouse(context, s.root, mui_pointerPress, 0, 1, 25.0f, 25.0f, 0);
    muiPointerRecord record = {0};
    CHECK(muiNextPointerRecord(context, &record) == mui_success && Same(record.node, m) &&
              !record.passThrough && record.x == -95.0f && IsPressed(context, m) &&
              !IsPressed(context, s.b) && !IsHovered(context, s.b),
          "blocked by the modal layer");
    muiDestroyContext(context);
}

static void TestRing(void)
{
    // Room for two records: the rest are counted into one.
    Scene s = MakeScene(1, 2);
    muiContext* context = s.context;
    (void)ClickAt(context, s.root, 0, 25.0f, 25.0f, 0);
    Mouse(context, s.root, mui_pointerPress, 0, 1, 25.0f, 25.0f, 10 * s_ms);
    Mouse(context, s.root, mui_pointerRelease, 0, 0, 25.0f, 25.0f, 11 * s_ms);
    muiPointerRecord record = {0};
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, 0, 2) &&
              Next(context, mui_pointerRecordRelease, s.b, 5.0f, 5.0f, 0, 2) &&
              muiNextPointerRecord(context, &record) == mui_success &&
              record.kind == mui_pointerRecordDropped && record.clickCount == 1 &&
              record.node.index1 == 0 && Drained(context),
          "dropped, counted");
    // Once one is dropped, so are the rest until the ring drains.
    Mouse(context, s.root, mui_pointerPress, 0, 1, 25.0f, 25.0f, 2000 * s_ms);
    Mouse(context, s.root, mui_pointerRelease, 0, 0, 25.0f, 25.0f, 2001 * s_ms);
    CHECK(Next(context, mui_pointerRecordPress, s.b, 5.0f, 5.0f, 0, 1), "one taken");
    Mouse(context, s.root, mui_pointerPress, 0, 1, 25.0f, 25.0f, 4000 * s_ms);
    CHECK(Next(context, mui_pointerRecordRelease, s.b, 5.0f, 5.0f, 0, 1) &&
              muiNextPointerRecord(context, &record) == mui_success &&
              record.kind == mui_pointerRecordDropped && record.clickCount == 2 && Drained(context),
          "kept in place");
    Mouse(context, s.root, mui_pointerRelease, 0, 0, 25.0f, 25.0f, 4001 * s_ms);
    while (muiNextPointerRecord(context, &record) == mui_success)
    {
    }
    // One pointer at a time.
    Mouse(context, s.root, mui_pointerLeave, 0, 0, 25.0f, 25.0f, 12 * s_ms);
    CHECK(Send(context, s.root, 2, mui_pointerMouse, mui_pointerMove, 0, 0, 1.0f, 1.0f, 0) ==
              mui_success,
          "the first");
    CHECK(Send(context, s.root, 3, mui_pointerMouse, mui_pointerMove, 0, 0, 1.0f, 1.0f, 0) ==
                  mui_errorCapacity &&
              Send(context, s.root, 3, mui_pointerMouse, mui_pointerLeave, 0, 0, 1.0f, 1.0f, 0) ==
                  mui_success,
          "a second, past the limit");
    muiDestroyContext(context);
    // No room for records at all.
    s = MakeScene(1, 0);
    Mouse(s.context, s.root, mui_pointerPress, 0, 1, 25.0f, 25.0f, 0);
    Mouse(s.context, s.root, mui_pointerRelease, 0, 0, 25.0f, 25.0f, 1);
    CHECK(muiNextPointerRecord(s.context, &record) == mui_success &&
              record.kind == mui_pointerRecordDropped && record.clickCount == 3,
          "all dropped");
    muiDestroyContext(s.context);
}

typedef struct Random
{
    uint32_t state;
} Random;

static uint32_t NextRandom(Random* random, uint32_t below)
{
    random->state = random->state * 1664525u + 1013904223u;
    return (random->state >> 8) % below;
}

// Whether chain, a pointer's node, is node or below it.
static bool Holds(const muiContext* context, muiNodeId node, muiNodeId chain)
{
    for (muiNodeId at = chain; at.index1 != 0; at = muiNode_GetParent(context, at))
    {
        if (Same(at, node))
        {
            return true;
        }
    }
    return false;
}

// Each node's hover and press against the pointers' chains, worked out
// afresh.
static bool StatesAgree(const muiContext* context, const muiNodeId* nodes, uint32_t count)
{
    muiPointerState states[3];
    bool known[3];
    for (uint32_t p = 0; p < 3; p++)
    {
        known[p] = muiPointer_GetState(context, p + 1, &states[p]) == mui_success;
    }
    bool agree = true;
    for (uint32_t i = 0; agree && i < count; i++)
    {
        if (!muiNode_IsValid(context, nodes[i]))
        {
            continue;
        }
        bool hovered = false;
        bool pressed = false;
        for (uint32_t p = 0; p < 3; p++)
        {
            hovered = hovered || (known[p] && Holds(context, nodes[i], states[p].hovered));
            pressed = pressed || (known[p] && Holds(context, nodes[i], states[p].pressed));
        }
        agree = IsHovered(context, nodes[i]) == hovered && IsPressed(context, nodes[i]) == pressed;
    }
    return agree;
}

// Random events of a mouse, a touch and a pen, captures and tree edits:
// hover and press always agree with the pointers' chains.
static void TestRandom(void)
{
    for (uint32_t seed = 1; seed <= 40; seed++)
    {
        Random random = {seed};
        muiContext* context = MakeContext(16, 8);
        muiNodeId nodes[12];
        nodes[0] = Place(context, s_nullNode, 0.0f, 0.0f, 120.0f, 120.0f);
        for (uint32_t i = 1; i < 12; i++)
        {
            muiNodeId parent = nodes[NextRandom(&random, i)];
            nodes[i] = Place(context, parent, (float)NextRandom(&random, 40),
                             (float)NextRandom(&random, 40), 10.0f + (float)NextRandom(&random, 50),
                             10.0f + (float)NextRandom(&random, 50));
        }
        Layout(context, nodes[0]);
        bool agree = true;
        for (uint32_t step = 0; agree && step < 300; step++)
        {
            uint32_t p = NextRandom(&random, 3);
            muiPointerState state = {0};
            bool known = muiPointer_GetState(context, p + 1, &state) == mui_success;
            muiPointerButtons buttons = known ? state.buttons : 0;
            uint8_t button = (uint8_t)NextRandom(&random, 2);
            muiPointerButtons bit = (muiPointerButtons)(1u << button);
            muiPointerAction action = (muiPointerAction)NextRandom(&random, 5);
            buttons = action == mui_pointerPress     ? (muiPointerButtons)(buttons | bit)
                      : action == mui_pointerRelease ? (muiPointerButtons)(buttons & ~bit)
                      : action == mui_pointerCancel  ? 0
                                                     : buttons;
            muiNodeId node = nodes[NextRandom(&random, 12)];
            switch (NextRandom(&random, 8))
            {
            case 0:
                (void)muiPointer_SetCapture(context, p + 1, node);
                break;
            case 1:
                (void)muiPointer_ReleaseCapture(context, p + 1);
                break;
            case 2:
            {
                // A node other than the root moves under another not
                // below it, or out of the tree.
                muiNodeId parent = nodes[NextRandom(&random, 12)];
                if (!Same(node, nodes[0]) && muiNode_IsValid(context, node))
                {
                    (void)muiNode_Detach(context, node);
                    if (muiNode_IsValid(context, parent) && !Holds(context, node, parent))
                    {
                        (void)muiNode_InsertChild(context, parent, node, s_nullNode);
                    }
                }
                break;
            }
            case 3:
            {
                // A node other than the root destroyed, with its subtree,
                // and made again under the root.
                if (!Same(node, nodes[0]) && muiNode_IsValid(context, node))
                {
                    CHECK(muiDestroyNode(context, node) == mui_success, "destroyed");
                    for (uint32_t i = 1; i < 12; i++)
                    {
                        if (!muiNode_IsValid(context, nodes[i]))
                        {
                            nodes[i] = Place(context, nodes[0], (float)NextRandom(&random, 80),
                                             (float)NextRandom(&random, 80), 20.0f, 20.0f);
                        }
                    }
                }
                Layout(context, nodes[0]);
                break;
            }
            default:
                CHECK(Send(context, nodes[0], p + 1, (muiPointerKind)p, action, button, buttons,
                           (float)NextRandom(&random, 140) - 10.0f,
                           (float)NextRandom(&random, 140) - 10.0f,
                           step * 100 * s_ms) == mui_success,
                      "an event");
                break;
            }
            agree = StatesAgree(context, nodes, 12);
            muiPointerRecord record = {0};
            while (muiNextPointerRecord(context, &record) == mui_success)
            {
                agree = agree && (record.kind == mui_pointerRecordDropped ||
                                  record.kind == mui_pointerRecordCaptureLost ||
                                  record.node.index1 == 0 || muiNode_IsValid(context, record.node));
            }
        }
        CHECK(agree, "hover and press agree with the pointers");
        muiDestroyContext(context);
    }
}

static void TestContract(void)
{
    Scene s = MakeScene(16, 64);
    muiContext* context = s.context;
    const muiPointerEvent good = {0, 1, mui_pointerMouse, mui_pointerMove, 0, 0, 1.0f, 1.0f, 0};
    muiPointerEvent bad[5] = {good, good, good, good, good};
    bad[0].kind = mui_pointerPen + 1;
    bad[1].action = mui_pointerLeave + 1;
    bad[2].button = 8;
    bad[3].x = NAN;
    bad[4].y = INFINITY;
    uint64_t misuse = muiGetContextMisuse(context);
    bool refused = true;
    for (int i = 0; i < 5; i++)
    {
        refused = refused && muiPointerInput(context, s.root, &bad[i]) == mui_errorInvalid;
    }
    CHECK(refused && muiPointerInput(NULL, s.root, &good) == mui_errorInvalid &&
              muiPointerInput(context, s.root, NULL) == mui_errorInvalid &&
              muiPointerInput(context, s_nullNode, &good) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 7,
          "events outside the contract");
    muiPointerState state = {0};
    CHECK(muiPointer_GetState(context, 1, &state) == mui_empty && Drained(context),
          "nothing changed");
    muiPointerRecord record = {0};
    CHECK(muiNextPointerRecord(NULL, &record) == mui_errorInvalid &&
              muiNextPointerRecord(context, NULL) == mui_errorInvalid &&
              muiPointer_GetState(NULL, 1, &state) == mui_errorInvalid &&
              muiPointer_GetState(context, 1, NULL) == mui_errorInvalid,
          "reads outside the contract");
    CHECK(muiSetClickRule(NULL, 1, 1.0f) == mui_errorInvalid &&
              muiSetClickRule(context, 1, -1.0f) == mui_errorInvalid &&
              muiSetClickRule(context, 1, NAN) == mui_errorInvalid &&
              muiSetClickRule(context, 0, 0.0f) == mui_success,
          "click rules");
    CHECK(muiPointer_SetCapture(NULL, 1, s.a) == mui_errorInvalid &&
              muiPointer_SetCapture(context, 1, s.a) == mui_errorInvalid &&
              muiPointer_ReleaseCapture(NULL, 1) == mui_errorInvalid &&
              muiPointer_ReleaseCapture(context, 1) == mui_errorInvalid,
          "capture of a pointer unknown");
    Mouse(context, s.root, mui_pointerPress, 0, 1, 25.0f, 25.0f, 0);
    muiNodeId gone = s.d;
    CHECK(muiDestroyNode(context, gone) == mui_success &&
              muiPointer_SetCapture(context, 1, s_nullNode) == mui_errorInvalid &&
              muiPointer_SetCapture(context, 1, gone) == mui_errorStale &&
              muiPointerInput(context, gone, &good) == mui_errorStale,
          "a node gone");
    muiDestroyContext(context);
    muiContextDef def = muiDefaultContextDef();
    def.limits.pointers = 33;
    CHECK(muiCreateContext(&def, &context) == mui_errorInvalid, "at most 32 pointers");
}

int main(void)
{
    TestHover();
    TestStyledHover();
    TestClicks();
    TestClickCount();
    TestTouch();
    TestCapture();
    TestCancel();
    TestSeveral();
    TestEdits();
    TestInnerRoot();
    TestModal();
    TestRing();
    TestRandom();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
