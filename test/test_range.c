// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Range values (record mui-0007): the model and its steps, keys by
// ARIA's slider pattern along the axis, presses paging, drags keeping
// the grab, cancelling back, notifications, and calls outside the
// contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/range.h"
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

static void Interact(muiContext* context, muiNodeId node, bool drags, bool focus)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.drags = drags;
    values.focusMode = focus ? mui_focusAll : mui_focusNone;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyDrags) |
                                           MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "interaction");
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static float Value(const muiContext* context, muiNodeId node)
{
    muiValueRange range = {0};
    CHECK(muiNode_GetValueRange(context, node, &range) == mui_success, "range");
    return range.value;
}

// How many range notifications wait, for node; others are dropped.
static int Changes(muiContext* context, muiNodeId node)
{
    int count = 0;
    muiNotification note = {0};
    while (muiNextNotification(context, &note) == mui_success)
    {
        count += note.kind == mui_notificationRangeChanged && Same(note.node, node);
    }
    return count;
}

// A row: slider (220 by 20, padding 10 at each end: a track from 10 to
// 210) holding thumb (20 by 20, at 10), 0 to 100 by 1, a page of 10.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId slider;
    muiNodeId thumb;
} Scene;

static void MakeScene(Scene* scene, muiTextDirection direction)
{
    muiContextDef def = muiDefaultContextDef();
    *scene = (Scene){0};
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiContext* context = scene->context;
    scene->root = Sized(context, s_nullNode, 300.0f, 300.0f);
    scene->slider = Sized(context, scene->root, 220.0f, 20.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.padding = (muiEdges){10.0f, 10.0f, 0.0f, 0.0f};
    style.textDirection = direction;
    CHECK(muiNode_SetLayoutValues(context, scene->slider, &style,
                                  MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
                                      MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "padding");
    Interact(context, scene->slider, true, true);
    scene->thumb = Sized(context, scene->slider, 20.0f, 20.0f);
    Layout(context, scene->root);
    muiValueRange range = muiDefaultValueRange();
    range.thumb = scene->thumb;
    CHECK(muiNode_SetValueRange(context, scene->slider, &range) == mui_success, "range");
}

static bool Key(const Scene* scene, muiKeyCode code, muiModifiers modifiers)
{
    const muiKeyEvent event = {.code = code, .modifiers = modifiers, .down = true};
    bool handled = false;
    CHECK(muiKeyInput(scene->context, scene->root, &event, &handled) == mui_success, "key");
    return handled;
}

static void Mouse(const Scene* scene, muiPointerAction action, muiPointerButtons buttons, float x)
{
    const muiPointerEvent event = {0, 1, mui_pointerMouse, action, 0, buttons, x, 10.0f, 0};
    CHECK(muiPointerInput(scene->context, scene->root, &event) == mui_success, "input");
}

// Dispatches every waiting record; how many were handled.
static int Dispatch(const Scene* scene)
{
    int handled = 0;
    muiPointerRecord record = {0};
    while (muiNextPointerRecord(scene->context, &record) == mui_success)
    {
        bool taken = false;
        CHECK(muiDispatchPointerRecord(scene->context, &record, &taken) == mui_success, "dispatch");
        handled += taken;
    }
    return handled;
}

static void TestModel(void)
{
    Scene scene;
    MakeScene(&scene, mui_textLeftToRight);
    muiContext* context = scene.context;
    muiValueRange range = muiDefaultValueRange();
    CHECK(range.minimum == 0.0f && range.maximum == 100.0f && range.value == 0.0f &&
              range.step == 1.0f && range.page == 10.0f && range.axis == mui_rangeHorizontal &&
              range.thumb.index1 == 0,
          "the default");
    CHECK(muiNode_SetRangeValue(context, scene.slider, 33.4f) == mui_success &&
              Value(context, scene.slider) == 33.0f && Changes(context, scene.slider) == 0,
          "on a step, by code: no notification");
    CHECK(muiNode_SetRangeValue(context, scene.slider, 500.0f) == mui_success &&
              Value(context, scene.slider) == 100.0f &&
              muiNode_SetRangeValue(context, scene.slider, -5.0f) == mui_success &&
              Value(context, scene.slider) == 0.0f,
          "within the ends");
    // Steps from the minimum, the last at or below the maximum.
    range = (muiValueRange){.minimum = 1.0f, .maximum = 10.0f, .value = 9.9f, .step = 2.0f};
    CHECK(muiNode_SetValueRange(context, scene.root, &range) == mui_success &&
              Value(context, scene.root) == 9.0f,
          "1, 3, ..., 9");
    range = (muiValueRange){.maximum = 10.0f, .value = 10.0f, .step = 4.0f};
    CHECK(muiNode_SetValueRange(context, scene.root, &range) == mui_success &&
              Value(context, scene.root) == 8.0f,
          "0, 4, 8: rounding past the maximum steps back");
    range.minimum = 1.0f;
    range.step = 0.0f;
    range.value = 2.25f;
    CHECK(muiNode_SetValueRange(context, scene.root, &range) == mui_success &&
              Value(context, scene.root) == 2.25f,
          "a step of 0: any value");
    CHECK(muiNode_ClearValueRange(context, scene.root) == mui_success &&
              muiNode_GetValueRange(context, scene.root, &range) == mui_empty &&
              muiNode_SetRangeValue(context, scene.root, 1.0f) == mui_empty &&
              muiNode_ClearValueRange(context, scene.root) == mui_success,
          "cleared");
    muiDestroyContext(context);
}

static void TestKeys(void)
{
    Scene scene;
    MakeScene(&scene, mui_textLeftToRight);
    muiContext* context = scene.context;
    muiNodeId slider = scene.slider;
    CHECK(muiFocus_Set(context, 0, slider, mui_focusByCode) == mui_success, "focus");
    CHECK(Key(&scene, mui_codeArrowRight, 0) && Value(context, slider) == 1.0f &&
              Changes(context, slider) == 1,
          "right: a step up");
    CHECK(Key(&scene, mui_codeArrowLeft, 0) && Value(context, slider) == 0.0f &&
              Key(&scene, mui_codeArrowLeft, 0) && Value(context, slider) == 0.0f &&
              Changes(context, slider) == 1,
          "left: down, then at the end taken, unchanged");
    CHECK(!Key(&scene, mui_codeArrowUp, 0) && !Key(&scene, mui_codeArrowDown, 0) &&
              Value(context, slider) == 0.0f,
          "across a horizontal range: not taken");
    CHECK(Key(&scene, mui_codePageUp, 0) && Value(context, slider) == 10.0f &&
              Key(&scene, mui_codePageDown, 0) && Value(context, slider) == 0.0f &&
              Key(&scene, mui_codeEnd, 0) && Value(context, slider) == 100.0f &&
              Key(&scene, mui_codeHome, 0) && Value(context, slider) == 0.0f,
          "pages and ends");
    CHECK(!Key(&scene, mui_codeArrowRight, mui_modShift) &&
              !Key(&scene, mui_codeEnd, mui_modControl) && Value(context, slider) == 0.0f,
          "modifiers: the game's");
    const muiNavigationEvent right = {0, mui_navigateRight, 0};
    bool handled = false;
    CHECK(muiNavigationInput(context, scene.root, &right, &handled) == mui_success && handled &&
              Value(context, slider) == 1.0f,
          "a gamepad's right");
    const muiNavigationEvent up = {0, mui_navigateUp, 0};
    CHECK(muiNavigationInput(context, scene.root, &up, &handled) == mui_success && !handled,
          "a gamepad's up: navigation, nothing there");
    // Vertical, continuous: up and down a hundredth of the span.
    muiValueRange range = {
        .minimum = 0.0f, .maximum = 50.0f, .value = 10.0f, .axis = mui_rangeVertical};
    CHECK(muiNode_SetValueRange(context, slider, &range) == mui_success &&
              Key(&scene, mui_codeArrowUp, 0) && Value(context, slider) == 10.5f &&
              Key(&scene, mui_codeArrowDown, 0) && Key(&scene, mui_codeArrowDown, 0) &&
              Value(context, slider) == 9.5f && !Key(&scene, mui_codeArrowRight, 0),
          "vertical");
    // Not a range: the keys do what they would.
    CHECK(muiNode_ClearValueRange(context, slider) == mui_success && !Key(&scene, mui_codeHome, 0),
          "no range");
    muiDestroyContext(context);
    MakeScene(&scene, mui_textRightToLeft);
    context = scene.context;
    CHECK(muiFocus_Set(context, 0, scene.slider, mui_focusByCode) == mui_success &&
              Key(&scene, mui_codeArrowLeft, 0) && Value(context, scene.slider) == 1.0f &&
              Key(&scene, mui_codeArrowRight, 0) && Value(context, scene.slider) == 0.0f,
          "right to left: left is up");
    muiDestroyContext(context);
}

static void TestPointer(void)
{
    Scene scene;
    MakeScene(&scene, mui_textLeftToRight);
    muiContext* context = scene.context;
    muiNodeId slider = scene.slider;
    // The thumb spans 10 to 30; a press right of it pages up, one on it
    // does nothing.
    Mouse(&scene, mui_pointerPress, 1, 150.0f);
    Mouse(&scene, mui_pointerRelease, 0, 150.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, slider) == 10.0f && Changes(context, slider) == 1,
          "a page up");
    Mouse(&scene, mui_pointerPress, 1, 15.0f);
    Mouse(&scene, mui_pointerRelease, 0, 15.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, slider) == 10.0f, "on the thumb");
    CHECK(muiNode_SetRangeValue(context, slider, 50.0f) == mui_success, "to 50");
    Mouse(&scene, mui_pointerPress, 1, 5.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, slider) == 40.0f, "left of it: a page down");
    Mouse(&scene, mui_pointerRelease, 0, 5.0f);
    (void)Dispatch(&scene);
    // A drag from 5 on the thumb keeps that grab: at 105 the thumb's left
    // is at 100, half its travel of 180 from 10.
    Mouse(&scene, mui_pointerPress, 1, 15.0f);
    Mouse(&scene, mui_pointerMove, 1, 105.0f);
    CHECK(Dispatch(&scene) == 2 && Value(context, slider) == 50.0f, "half way");
    Mouse(&scene, mui_pointerMove, 1, 400.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, slider) == 100.0f, "past the end");
    Mouse(&scene, mui_pointerMove, 1, -50.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, slider) == 0.0f, "before the start");
    Mouse(&scene, mui_pointerMove, 1, 60.0f);
    (void)Dispatch(&scene);
    float there = Value(context, slider);
    (void)Changes(context, slider);
    Mouse(&scene, mui_pointerRelease, 0, 60.0f);
    CHECK(Dispatch(&scene) >= 1 && Value(context, slider) == there && there == 25.0f &&
              Changes(context, slider) == 0,
          "released where it is");
    // A drag from the track grabs the thumb's middle; Escape puts the
    // value back where the drag began.
    CHECK(muiNode_SetRangeValue(context, slider, 0.0f) == mui_success, "to 0");
    Mouse(&scene, mui_pointerPress, 1, 150.0f);
    Mouse(&scene, mui_pointerMove, 1, 160.0f);
    CHECK(Dispatch(&scene) == 2 && Value(context, slider) == 78.0f, "from the track: 140 of 180");
    const muiKeyEvent escape = {.code = mui_codeEscape, .down = true};
    bool handled = false;
    CHECK(muiKeyInput(context, scene.root, &escape, &handled) == mui_success && handled, "Escape");
    CHECK(Dispatch(&scene) >= 1 && Value(context, slider) == 10.0f,
          "back to the value the drag began at");
    Mouse(&scene, mui_pointerRelease, 0, 160.0f);
    (void)Dispatch(&scene);
    muiDestroyContext(context);
}

static void TestShapes(void)
{
    // Without a thumb the value follows the pointer over the whole track.
    Scene scene;
    MakeScene(&scene, mui_textLeftToRight);
    muiContext* context = scene.context;
    muiValueRange range = muiDefaultValueRange();
    CHECK(muiNode_SetValueRange(context, scene.slider, &range) == mui_success, "no thumb");
    Mouse(&scene, mui_pointerPress, 1, 50.0f);
    Mouse(&scene, mui_pointerMove, 1, 110.0f);
    CHECK(Dispatch(&scene) == 2 && Value(context, scene.slider) == 50.0f, "the middle");
    Mouse(&scene, mui_pointerRelease, 0, 110.0f);
    (void)Dispatch(&scene);
    // A thumb that takes drags itself reaches its range; right to left
    // the value grows leftward.
    Interact(context, scene.slider, false, true);
    Interact(context, scene.thumb, true, false);
    range.thumb = scene.thumb;
    CHECK(muiNode_SetValueRange(context, scene.slider, &range) == mui_success, "thumb");
    Mouse(&scene, mui_pointerPress, 1, 15.0f);
    Mouse(&scene, mui_pointerMove, 1, 105.0f);
    CHECK(Dispatch(&scene) == 2 && Value(context, scene.slider) == 50.0f, "through the thumb");
    Mouse(&scene, mui_pointerRelease, 0, 105.0f);
    (void)Dispatch(&scene);
    muiDestroyContext(context);
    MakeScene(&scene, mui_textRightToLeft);
    context = scene.context;
    // Right to left the thumb sits at the right, 190 to 210.
    Mouse(&scene, mui_pointerPress, 1, 50.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, scene.slider) == 10.0f, "left of the thumb: up");
    // From the track the grab is the thumb's middle: at 110 its left is
    // at 100.
    Mouse(&scene, mui_pointerMove, 1, 110.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, scene.slider) == 50.0f, "the middle");
    Mouse(&scene, mui_pointerMove, 1, 0.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, scene.slider) == 100.0f, "the left end");
    muiDestroyContext(context);
}

static void TestVertical(void)
{
    // A column 20 wide, 220 tall, padding 10 at top and bottom, a thumb
    // of 20 at its top: the minimum at the bottom.
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Sized(context, s_nullNode, 300.0f, 300.0f);
    muiNodeId slider = Sized(context, root, 20.0f, 220.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.padding = (muiEdges){0.0f, 0.0f, 10.0f, 10.0f};
    CHECK(muiNode_SetLayoutValues(context, slider, &style,
                                  MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingBottom)) == mui_success,
          "padding");
    Interact(context, slider, true, false);
    muiNodeId thumb = Sized(context, slider, 20.0f, 20.0f);
    Layout(context, root);
    muiValueRange range = muiDefaultValueRange();
    range.axis = mui_rangeVertical;
    range.thumb = thumb;
    CHECK(muiNode_SetValueRange(context, slider, &range) == mui_success, "range");
    Scene scene = {context, root, slider, thumb};
    const muiPointerEvent press = {0,      1, mui_pointerMouse, mui_pointerPress, 0, 1, 10.0f,
                                   100.0f, 0};
    const muiPointerEvent move = {0, 1, mui_pointerMouse, mui_pointerMove, 0, 1, 10.0f, 110.0f, 0};
    CHECK(muiPointerInput(context, root, &press) == mui_success && Dispatch(&scene) == 1 &&
              Value(context, slider) == 0.0f,
          "below the thumb: down, at the minimum");
    CHECK(muiPointerInput(context, root, &move) == mui_success && Dispatch(&scene) == 1 &&
              Value(context, slider) == 50.0f,
          "the middle");
    muiDestroyContext(context);
}

static void TestEdges(void)
{
    // A node in a destroyed range's slot is no range.
    Scene scene;
    MakeScene(&scene, mui_textLeftToRight);
    muiContext* context = scene.context;
    muiValueRange range = muiDefaultValueRange();
    CHECK(muiDestroyNode(context, scene.slider) == mui_success, "gone");
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId again = s_nullNode;
    CHECK(muiCreateNode(context, &def, &again) == mui_success &&
              again.index1 == scene.slider.index1 &&
              muiNode_GetValueRange(context, again, &range) == mui_empty,
          "the slot reused");
    muiDestroyContext(context);
    // A thumb outside the range, or the range itself, is no thumb: the
    // value follows the pointer.
    MakeScene(&scene, mui_textLeftToRight);
    context = scene.context;
    range.thumb = scene.root;
    CHECK(muiNode_SetValueRange(context, scene.slider, &range) == mui_success, "outside");
    Mouse(&scene, mui_pointerPress, 1, 50.0f);
    Mouse(&scene, mui_pointerMove, 1, 110.0f);
    CHECK(Dispatch(&scene) == 2 && Value(context, scene.slider) == 50.0f, "no thumb outside");
    Mouse(&scene, mui_pointerRelease, 0, 110.0f);
    (void)Dispatch(&scene);
    range.thumb = scene.slider;
    CHECK(muiNode_SetValueRange(context, scene.slider, &range) == mui_success, "itself");
    Mouse(&scene, mui_pointerPress, 1, 50.0f);
    Mouse(&scene, mui_pointerMove, 1, 110.0f);
    CHECK(Dispatch(&scene) == 2 && Value(context, scene.slider) == 50.0f, "no thumb itself");
    Mouse(&scene, mui_pointerRelease, 0, 110.0f);
    (void)Dispatch(&scene);
    // Without a thumb, a press pages toward the value's place: at 50 it
    // lies at 110.
    range.thumb = s_nullNode;
    range.value = 50.0f;
    CHECK(muiNode_SetValueRange(context, scene.slider, &range) == mui_success, "no thumb");
    Mouse(&scene, mui_pointerPress, 1, 80.0f);
    Mouse(&scene, mui_pointerRelease, 0, 80.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, scene.slider) == 40.0f, "left of the value");
    // A thumb as long as the track: a drag leaves the value.
    range.thumb = scene.thumb;
    range.value = 30.0f;
    muiLayoutStyle wide = muiDefaultLayoutStyle();
    wide.sizing.width = (muiDimension){0.0f, 200.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, scene.thumb, &wide,
                                  MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success &&
              muiNode_SetValueRange(context, scene.slider, &range) == mui_success,
          "a full thumb");
    Layout(context, scene.root);
    Mouse(&scene, mui_pointerPress, 1, 50.0f);
    Mouse(&scene, mui_pointerMove, 1, 150.0f);
    CHECK(Dispatch(&scene) == 2 && Value(context, scene.slider) == 30.0f, "no travel");
    muiDestroyContext(context);
    // Right to left with padding of 30 at the start (the right) and 10 at
    // the end: the track runs from 10 to 190, the thumb at 170.
    MakeScene(&scene, mui_textRightToLeft);
    context = scene.context;
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.padding = (muiEdges){30.0f, 10.0f, 0.0f, 0.0f};
    style.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, scene.slider, &style,
                                  MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingEnd)) == mui_success,
          "padding");
    Layout(context, scene.root);
    // Right to left without a thumb, 20 lies at 154 (from 190, a fifth
    // of 180 leftward): a press at 100 is past it, up.
    muiValueRange plain = muiDefaultValueRange();
    plain.value = 20.0f;
    CHECK(muiNode_SetValueRange(context, scene.slider, &plain) == mui_success, "plain");
    Mouse(&scene, mui_pointerPress, 1, 100.0f);
    Mouse(&scene, mui_pointerRelease, 0, 100.0f);
    CHECK(Dispatch(&scene) == 1 && Value(context, scene.slider) == 30.0f, "leftward is up");
    range.thumb = scene.thumb;
    range.value = 0.0f;
    CHECK(muiNode_SetValueRange(context, scene.slider, &range) == mui_success, "thumb again");
    Mouse(&scene, mui_pointerPress, 1, 50.0f);
    Mouse(&scene, mui_pointerMove, 1, 100.0f);
    CHECK(Dispatch(&scene) == 2 && Value(context, scene.slider) == 50.0f,
          "from the track: 100 less half the thumb, half of 160 from 10");
    muiDestroyContext(context);
}

static void TestContract(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.ranges = 1;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId a = Sized(context, s_nullNode, 10.0f, 10.0f);
    muiNodeId b = Sized(context, s_nullNode, 10.0f, 10.0f);
    muiValueRange range = muiDefaultValueRange();
    muiValueRange bad = range;
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiNode_SetValueRange(NULL, a, &range) == mui_errorInvalid &&
              muiNode_SetValueRange(context, a, NULL) == mui_errorInvalid &&
              muiNode_SetValueRange(context, s_nullNode, &range) == mui_errorInvalid,
          "missing");
    const float nan = NAN;
    float* fields[] = {&bad.minimum, &bad.maximum, &bad.value, &bad.step, &bad.page};
    for (int i = 0; i < 5; i++)
    {
        bad = range;
        *fields[i] = nan;
        CHECK(muiNode_SetValueRange(context, a, &bad) == mui_errorInvalid, "not finite");
    }
    float* ends[] = {&bad.minimum, &bad.maximum, &bad.step, &bad.page};
    for (int i = 0; i < 4; i++)
    {
        bad = range;
        *ends[i] = i == 0 ? -INFINITY : INFINITY;
        CHECK(muiNode_SetValueRange(context, a, &bad) == mui_errorInvalid, "infinite");
    }
    bad = range;
    bad.maximum = -1.0f;
    CHECK(muiNode_SetValueRange(context, a, &bad) == mui_errorInvalid, "below the minimum");
    bad = range;
    bad.step = -1.0f;
    CHECK(muiNode_SetValueRange(context, a, &bad) == mui_errorInvalid, "a negative step");
    bad = range;
    bad.page = -1.0f;
    CHECK(muiNode_SetValueRange(context, a, &bad) == mui_errorInvalid, "a negative page");
    bad = range;
    bad.axis = 2;
    CHECK(muiNode_SetValueRange(context, a, &bad) == mui_errorInvalid, "an axis");
    CHECK(muiGetContextMisuse(context) == misuse + 15, "counted");
    // The limit; a node gone frees its entry.
    CHECK(muiNode_SetValueRange(context, a, &range) == mui_success &&
              muiNode_SetValueRange(context, a, &range) == mui_success &&
              muiNode_SetValueRange(context, b, &range) == mui_errorCapacity,
          "one range");
    CHECK(muiDestroyNode(context, a) == mui_success &&
              muiNode_SetValueRange(context, b, &range) == mui_success,
          "freed");
    CHECK(muiNode_SetValueRange(context, a, &range) == mui_errorStale &&
              muiNode_GetValueRange(context, a, &range) == mui_errorStale &&
              muiNode_SetRangeValue(context, a, 1.0f) == mui_errorStale &&
              muiNode_ClearValueRange(context, a) == mui_errorStale,
          "a node gone");
    range.thumb = a;
    CHECK(muiNode_SetValueRange(context, b, &range) == mui_errorStale, "a thumb gone");
    CHECK(muiNode_GetValueRange(NULL, b, &range) == mui_errorInvalid &&
              muiNode_GetValueRange(context, b, NULL) == mui_errorInvalid &&
              muiNode_GetValueRange(context, s_nullNode, &range) == mui_errorInvalid &&
              muiNode_SetRangeValue(NULL, b, 1.0f) == mui_errorInvalid &&
              muiNode_SetRangeValue(context, b, INFINITY) == mui_errorInvalid &&
              muiNode_SetRangeValue(context, s_nullNode, 1.0f) == mui_errorInvalid &&
              muiNode_ClearValueRange(NULL, b) == mui_errorInvalid &&
              muiNode_ClearValueRange(context, s_nullNode) == mui_errorInvalid,
          "reads and writes outside the contract");
    muiDestroyContext(context);
}

int main(void)
{
    TestModel();
    TestKeys();
    TestPointer();
    TestShapes();
    TestVertical();
    TestEdges();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
