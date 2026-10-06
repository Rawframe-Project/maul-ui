// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drags (record mui-0007): the nearest node that takes drags, the
// threshold per pointer kind, the records and their offsets, capture,
// no click after a drag, the ways a drag is cancelled, routing, and
// calls outside the contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"

#include <math.h>
#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

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

static void Drags(muiContext* context, muiNodeId node)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.drags = true;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyDrags)) == mui_success,
          "drags");
}

// A row: track (200 by 20, taking drags) holding thumb (20 by 20), then
// b (50 by 50) at 200.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId track;
    muiNodeId thumb;
    muiNodeId b;
} Scene;

static void MakeScene(Scene* scene)
{
    muiContextDef def = muiDefaultContextDef();
    *scene = (Scene){0};
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiContext* context = scene->context;
    scene->root = Sized(context, s_nullNode, 300.0f, 300.0f);
    scene->track = Sized(context, scene->root, 200.0f, 20.0f);
    Drags(context, scene->track);
    scene->thumb = Sized(context, scene->track, 20.0f, 20.0f);
    scene->b = Sized(context, scene->root, 50.0f, 50.0f);
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, scene->root, &input) == mui_success, "layout");
}

static void Feed(const Scene* scene, uint32_t pointer, muiPointerKind kind, muiPointerAction action,
                 muiPointerButtons buttons, float x, float y)
{
    const muiPointerEvent event = {0, pointer, kind, action, 0, buttons, x, y, 0};
    CHECK(muiPointerInput(scene->context, scene->root, &event) == mui_success, "input");
}

static void Mouse(const Scene* scene, muiPointerAction action, muiPointerButtons buttons, float x,
                  float y)
{
    Feed(scene, 1, mui_pointerMouse, action, buttons, x, y);
}

// The next record, of a kind on a node.
static bool Next(const Scene* scene, muiPointerRecordKind kind, muiNodeId node,
                 muiPointerRecord* recordOut)
{
    muiPointerRecord record = {0};
    bool got = muiNextPointerRecord(scene->context, &record) == mui_success;
    if (recordOut != NULL)
    {
        *recordOut = record;
    }
    return got && record.kind == kind && Same(record.node, node);
}

static bool None(const Scene* scene)
{
    muiPointerRecord record = {0};
    return muiNextPointerRecord(scene->context, &record) == mui_empty;
}

static void TestDrag(void)
{
    Scene scene;
    MakeScene(&scene);
    muiPointerRecord record = {0};
    // Within the threshold: no drag.
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 14.0f, 6.0f);
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) && None(&scene),
          "four on either axis: not yet");
    // Past it: the track, the nearest that takes drags, starts a drag and
    // captures the pointer.
    Mouse(&scene, mui_pointerMove, 1, 15.0f, 12.0f);
    CHECK(Next(&scene, mui_pointerRecordDragStart, scene.track, &record) && record.x == 15.0f &&
              record.y == 12.0f && record.offsetX == 5.0f && record.offsetY == 2.0f &&
              !record.cancelled && record.buttons == 1 && None(&scene),
          "a drag starts");
    muiPointerState state = {0};
    CHECK(muiPointer_GetState(scene.context, 1, &state) == mui_success &&
              Same(state.captured, scene.track),
          "captured");
    Mouse(&scene, mui_pointerMove, 1, 220.0f, 40.0f);
    CHECK(Next(&scene, mui_pointerRecordDragMove, scene.track, &record) && record.x == 220.0f &&
              record.y == 40.0f && record.offsetX == 210.0f && record.offsetY == 30.0f &&
              None(&scene),
          "moves outside it, a drag move only");
    // Released over b: a release to the capture, the drag's end, the
    // capture gone, and no click.
    Mouse(&scene, mui_pointerRelease, 0, 220.0f, 40.0f);
    CHECK(Next(&scene, mui_pointerRecordRelease, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordDragEnd, scene.track, &record) && !record.cancelled &&
              record.offsetX == 210.0f && record.buttons == 0 &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.track, NULL) && None(&scene),
          "the end, no click");
    // A press that never passed the threshold clicks.
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 12.0f, 12.0f);
    Mouse(&scene, mui_pointerRelease, 0, 12.0f, 12.0f);
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordRelease, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordClick, scene.thumb, NULL) && None(&scene),
          "a click");
    // b takes no drags, nor anything above it.
    Mouse(&scene, mui_pointerPress, 1, 210.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 290.0f, 90.0f);
    Mouse(&scene, mui_pointerRelease, 0, 220.0f, 10.0f);
    CHECK(Next(&scene, mui_pointerRecordPress, scene.b, NULL) &&
              Next(&scene, mui_pointerRecordRelease, scene.b, NULL) &&
              Next(&scene, mui_pointerRecordClick, scene.b, NULL) && None(&scene),
          "no drag on b");
    muiDestroyContext(scene.context);
}

static void TestButtons(void)
{
    // A second button during a drag: no end until the last goes up.
    Scene scene;
    MakeScene(&scene);
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 30.0f, 10.0f);
    const muiPointerEvent second = {0,     1, mui_pointerMouse, mui_pointerPress, 1, 3, 30.0f,
                                    10.0f, 0};
    const muiPointerEvent up = {0, 1, mui_pointerMouse, mui_pointerRelease, 1, 1, 30.0f, 10.0f, 0};
    CHECK(muiPointerInput(scene.context, scene.root, &second) == mui_success &&
              muiPointerInput(scene.context, scene.root, &up) == mui_success,
          "the second button");
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordDragStart, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordPress, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordRelease, scene.track, NULL) && None(&scene),
          "still dragging");
    Mouse(&scene, mui_pointerMove, 1, 40.0f, 10.0f);
    Mouse(&scene, mui_pointerRelease, 0, 40.0f, 10.0f);
    CHECK(Next(&scene, mui_pointerRecordDragMove, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordRelease, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordDragEnd, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.track, NULL) && None(&scene),
          "ended with the last");
    // A move with no buttons where a release went missing: the captured
    // move, then the end, not cancelled.
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 30.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 0, 50.0f, 10.0f);
    muiPointerRecord record = {0};
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordDragStart, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordMove, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordDragEnd, scene.track, &record) && !record.cancelled &&
              record.offsetX == 40.0f &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.track, NULL) && None(&scene),
          "a missing release");
    muiDestroyContext(scene.context);
}

static bool Escape(const Scene* scene)
{
    const muiKeyEvent key = {.code = mui_codeEscape, .down = true};
    bool handled = false;
    CHECK(muiKeyInput(scene->context, scene->root, &key, &handled) == mui_success, "key");
    return handled;
}

static void TestCancel(void)
{
    Scene scene;
    MakeScene(&scene);
    muiPointerRecord record = {0};
    CHECK(!Escape(&scene), "Escape with no drag: the game's");
    // Escape: the end cancelled, the capture gone; the release later
    // clicks nothing.
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 30.0f, 10.0f);
    CHECK(Escape(&scene), "Escape cancels");
    Mouse(&scene, mui_pointerMove, 1, 40.0f, 10.0f);
    Mouse(&scene, mui_pointerRelease, 0, 12.0f, 10.0f);
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordDragStart, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordDragEnd, scene.track, &record) && record.cancelled &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordRelease, scene.thumb, NULL) && None(&scene),
          "cancelled, then no click");
    // A pointer cancel.
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 30.0f, 10.0f);
    Mouse(&scene, mui_pointerCancel, 0, 30.0f, 10.0f);
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordDragStart, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordCancel, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordDragEnd, scene.track, &record) && record.cancelled &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.track, NULL) && None(&scene),
          "a pointer cancel");
    // The capture taken elsewhere.
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 30.0f, 10.0f);
    CHECK(muiPointer_SetCapture(scene.context, 1, scene.b) == mui_success, "elsewhere");
    Mouse(&scene, mui_pointerMove, 1, 60.0f, 10.0f);
    Mouse(&scene, mui_pointerRelease, 0, 60.0f, 10.0f);
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordDragStart, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordDragEnd, scene.track, &record) && record.cancelled &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordMove, scene.b, NULL) &&
              Next(&scene, mui_pointerRecordRelease, scene.b, NULL) &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.b, NULL) && None(&scene),
          "capture lost: cancelled, no click");
    // The node goes: cancelled, still named, its point on the surface.
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 30.0f, 10.0f);
    muiNodeId track = scene.track;
    CHECK(muiDestroyNode(scene.context, track) == mui_success, "gone");
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordDragStart, track, NULL) &&
              Next(&scene, mui_pointerRecordDragEnd, track, &record) && record.cancelled &&
              record.x == 30.0f && record.passThrough &&
              Next(&scene, mui_pointerRecordCaptureLost, track, NULL) && None(&scene),
          "destroyed");
    muiDestroyContext(scene.context);
}

static void TestTouch(void)
{
    // Touch captures what it pressed; at 8 the capture moves to the drag.
    Scene scene;
    MakeScene(&scene);
    Feed(&scene, 5, mui_pointerTouch, mui_pointerPress, 1, 10.0f, 10.0f);
    Feed(&scene, 5, mui_pointerTouch, mui_pointerMove, 1, 18.0f, 10.0f);
    Feed(&scene, 5, mui_pointerTouch, mui_pointerMove, 1, 10.0f, 19.0f);
    muiPointerRecord record = {0};
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordMove, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordDragStart, scene.track, &record) &&
              record.offsetY == 9.0f && record.pointerKind == mui_pointerTouch && None(&scene),
          "touch: eight");
    Feed(&scene, 5, mui_pointerTouch, mui_pointerRelease, 0, 10.0f, 19.0f);
    CHECK(Next(&scene, mui_pointerRecordRelease, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordDragEnd, scene.track, NULL) &&
              Next(&scene, mui_pointerRecordCaptureLost, scene.track, NULL) && None(&scene),
          "touch ends");
    // A pen as touch; a threshold of 0 starts at any move.
    CHECK(muiSetDragThreshold(scene.context, 0.0f, 20.0f) == mui_success, "thresholds");
    Feed(&scene, 6, mui_pointerPen, mui_pointerPress, 1, 10.0f, 10.0f);
    Feed(&scene, 6, mui_pointerPen, mui_pointerMove, 1, 29.0f, 10.0f);
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordMove, scene.thumb, NULL) && None(&scene),
          "a pen within 20");
    Feed(&scene, 6, mui_pointerPen, mui_pointerCancel, 0, 29.0f, 10.0f);
    (void)muiNextPointerRecord(scene.context, &record);
    (void)muiNextPointerRecord(scene.context, &record);
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 10.5f, 10.0f);
    CHECK(Next(&scene, mui_pointerRecordPress, scene.thumb, NULL) &&
              Next(&scene, mui_pointerRecordDragStart, scene.track, NULL) && None(&scene),
          "a mouse past 0");
    muiDestroyContext(scene.context);
}

typedef struct Heard
{
    muiEvent last;
    muiPointerRecord record;
} Heard;

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    (void)nodeId;
    (void)phase;
    Heard* heard = user;
    heard->last = *event;
    heard->record = *event->pointer;
    return true;
}

static void TestRouted(void)
{
    Scene scene;
    MakeScene(&scene);
    Heard heard = {0};
    CHECK(muiSetEventFunction(scene.context, Hear, &heard) == mui_success, "function");
    Mouse(&scene, mui_pointerPress, 1, 10.0f, 10.0f);
    Mouse(&scene, mui_pointerMove, 1, 30.0f, 15.0f);
    muiPointerRecord record = {0};
    bool handled = false;
    CHECK(muiNextPointerRecord(scene.context, &record) == mui_success &&
              muiNextPointerRecord(scene.context, &record) == mui_success &&
              muiDispatchPointerRecord(scene.context, &record, &handled) == mui_success &&
              handled && heard.last.kind == mui_eventPointer &&
              Same(heard.last.target, scene.track) &&
              heard.record.kind == mui_pointerRecordDragStart && heard.record.offsetX == 20.0f &&
              heard.record.offsetY == 5.0f,
          "routed to the dragged node");
    muiDestroyContext(scene.context);
}

static void TestContract(void)
{
    Scene scene;
    MakeScene(&scene);
    uint64_t misuse = muiGetContextMisuse(scene.context);
    CHECK(muiSetDragThreshold(NULL, 1.0f, 1.0f) == mui_errorInvalid &&
              muiSetDragThreshold(scene.context, NAN, 1.0f) == mui_errorInvalid &&
              muiSetDragThreshold(scene.context, -1.0f, 1.0f) == mui_errorInvalid &&
              muiSetDragThreshold(scene.context, 1.0f, INFINITY) == mui_errorInvalid &&
              muiSetDragThreshold(scene.context, 1.0f, -0.5f) == mui_errorInvalid &&
              muiGetContextMisuse(scene.context) == misuse + 4,
          "thresholds outside the contract");
    muiInteractionStyle values = muiDefaultInteractionStyle();
    CHECK(!values.drags, "no drags by default");
    muiDestroyContext(scene.context);
}

int main(void)
{
    TestDrag();
    TestButtons();
    TestCancel();
    TestTouch();
    TestRouted();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
