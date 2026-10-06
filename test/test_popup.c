// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Popups (record mui-0007): placement beside an anchor after layout,
// each side and alignment, flipping, clamping, margins, right to left,
// scrolling, nesting, and the table's contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/popup.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <math.h>
#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

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

// Places a node absolutely at x, y in its parent.
static void At(muiContext* context, muiNodeId node, float x, float y)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.placement.position = mui_positionAbsolute;
    style.placement.inset.start = (muiDimension){0.0f, x, mui_dimensionValue};
    style.placement.inset.top = (muiDimension){0.0f, y, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyPosition) |
                                      MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                                      MUI_PROPERTY_BIT(mui_propertyInsetTop)) == mui_success,
          "at");
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

// A root of 400 by 300, an anchor of 100 by 40, and a popup of 120 by 60
// at 0, 0, both absolute.
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId anchor;
    muiNodeId popup;
} Scene;

static void MakeScene(Scene* scene, float x, float y)
{
    muiContextDef def = muiDefaultContextDef();
    *scene = (Scene){0};
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiContext* context = scene->context;
    scene->root = Sized(context, s_nullNode, 400.0f, 300.0f);
    scene->anchor = Sized(context, scene->root, 100.0f, 40.0f);
    At(context, scene->anchor, x, y);
    scene->popup = Sized(context, scene->root, 120.0f, 60.0f);
    At(context, scene->popup, 0.0f, 0.0f);
}

static muiPopup PopupOf(muiNodeId anchor, muiPopupSide side, muiPopupAlign align, float gap)
{
    muiPopup popup = muiDefaultPopup();
    popup.anchor = anchor;
    popup.side = side;
    popup.align = align;
    popup.gap = gap;
    return popup;
}

// Sets the scene's popup, lays out, and says whether it went to x, y on
// side.
static bool Placed(const Scene* scene, const muiPopup* popup, float x, float y, muiPopupSide side)
{
    CHECK(muiNode_SetPopup(scene->context, scene->popup, popup) == mui_success, "set");
    Layout(scene->context, scene->root);
    muiRect rect = muiNode_GetRect(scene->context, scene->popup);
    muiPopupSide placed = mui_popupCenter;
    bool sided = muiNode_GetPopupSide(scene->context, scene->popup, &placed) == mui_success;
    return rect.x == x && rect.y == y && sided && placed == side;
}

static void TestSides(void)
{
    Scene scene;
    MakeScene(&scene, 150.0f, 100.0f);
    muiNodeId a = scene.anchor;
    muiPopup p = PopupOf(a, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 150.0f, 144.0f, mui_popupBelow), "below, start edges");
    p.align = mui_popupAlignCenter;
    CHECK(Placed(&scene, &p, 140.0f, 144.0f, mui_popupBelow), "below, centered");
    p.align = mui_popupAlignEnd;
    CHECK(Placed(&scene, &p, 130.0f, 144.0f, mui_popupBelow), "below, end edges");
    p = PopupOf(a, mui_popupAbove, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 150.0f, 36.0f, mui_popupAbove), "above");
    p = PopupOf(a, mui_popupStart, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 26.0f, 100.0f, mui_popupStart), "start, top edges");
    p.align = mui_popupAlignCenter;
    CHECK(Placed(&scene, &p, 26.0f, 90.0f, mui_popupStart), "start, centered");
    p.align = mui_popupAlignEnd;
    CHECK(Placed(&scene, &p, 26.0f, 80.0f, mui_popupStart), "start, bottom edges");
    p = PopupOf(a, mui_popupEnd, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 254.0f, 100.0f, mui_popupEnd), "end");
    p = PopupOf(a, mui_popupCenter, mui_popupAlignEnd, 4.0f);
    CHECK(Placed(&scene, &p, 140.0f, 90.0f, mui_popupCenter), "center");
    // Set anew: not placed until the next layout.
    muiPopupSide side = mui_popupBelow;
    CHECK(muiNode_SetPopup(scene.context, scene.popup, &p) == mui_success &&
              muiNode_GetPopupSide(scene.context, scene.popup, &side) == mui_empty,
          "set anew");
    Layout(scene.context, scene.root);
    // Again with nothing changed: the same place.
    Layout(scene.context, scene.root);
    CHECK(muiNode_GetRect(scene.context, scene.popup).x == 140.0f &&
              muiNode_GetRect(scene.context, scene.popup).y == 90.0f,
          "steady");
    muiDestroyContext(scene.context);
}

static void TestFlipAndClamp(void)
{
    // Near the bottom: below overflows, above has more room.
    Scene scene;
    MakeScene(&scene, 150.0f, 230.0f);
    muiPopup p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 150.0f, 166.0f, mui_popupAbove), "flipped above");
    muiDestroyContext(scene.context);
    // Near the top: above overflows, below has more room.
    MakeScene(&scene, 150.0f, 20.0f);
    p = PopupOf(scene.anchor, mui_popupAbove, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 150.0f, 64.0f, mui_popupBelow), "flipped below");
    muiDestroyContext(scene.context);
    // Room enough below, more above: no flip.
    MakeScene(&scene, 150.0f, 180.0f);
    p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 150.0f, 224.0f, mui_popupBelow), "fits below");
    muiDestroyContext(scene.context);
    // Near the start and the end.
    MakeScene(&scene, 10.0f, 100.0f);
    p = PopupOf(scene.anchor, mui_popupStart, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 114.0f, 100.0f, mui_popupEnd), "flipped to the end");
    muiDestroyContext(scene.context);
    MakeScene(&scene, 290.0f, 100.0f);
    p = PopupOf(scene.anchor, mui_popupEnd, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 166.0f, 100.0f, mui_popupStart), "flipped to the start");
    // Clamped across: start edges at 290 would overflow the end.
    p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 280.0f, 144.0f, mui_popupBelow), "clamped");
    p.margin = 10.0f;
    CHECK(Placed(&scene, &p, 270.0f, 144.0f, mui_popupBelow), "clamped within the margin");
    muiDestroyContext(scene.context);
    // Tall: no more room above than below, so no flip; clamped up.
    MakeScene(&scene, 150.0f, 130.0f);
    CHECK(muiNode_SetLayoutValues(
              scene.context, scene.popup,
              &(muiLayoutStyle){.sizing.height = {0.0f, 200.0f, mui_dimensionValue}},
              MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "tall");
    p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 150.0f, 100.0f, mui_popupBelow), "no flip, clamped");
    p.side = mui_popupAbove;
    CHECK(Placed(&scene, &p, 150.0f, 0.0f, mui_popupAbove), "no flip up, clamped");
    p.side = mui_popupBelow;
    // Wider than the root: its start edge kept.
    CHECK(muiNode_SetLayoutValues(
              scene.context, scene.popup,
              &(muiLayoutStyle){.sizing.width = {0.0f, 500.0f, mui_dimensionValue}},
              MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "wide");
    CHECK(Placed(&scene, &p, 0.0f, 100.0f, mui_popupBelow), "wide, from the start");
    CHECK(muiNode_SetLayoutValues(
              scene.context, scene.popup,
              &(muiLayoutStyle){.sizing.width = {0.0f, 400.5f, mui_dimensionValue}},
              MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "a little wider than the root");
    CHECK(Placed(&scene, &p, 0.0f, 100.0f, mui_popupBelow), "from the start too");
    muiDestroyContext(scene.context);
    // End edges near the start: clamped to it.
    MakeScene(&scene, 10.0f, 100.0f);
    p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignEnd, 4.0f);
    CHECK(Placed(&scene, &p, 0.0f, 144.0f, mui_popupBelow), "clamped to the start");
    muiDestroyContext(scene.context);
    // Centered on an anchor at the top, within a margin.
    MakeScene(&scene, 150.0f, 0.0f);
    p = PopupOf(scene.anchor, mui_popupCenter, mui_popupAlignStart, 0.0f);
    p.margin = 10.0f;
    CHECK(Placed(&scene, &p, 140.0f, 10.0f, mui_popupCenter), "clamped within the margin");
    muiDestroyContext(scene.context);
}

static void TestRightToLeft(void)
{
    Scene scene;
    MakeScene(&scene, 150.0f, 100.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(scene.context, scene.anchor, &style,
                                  MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "right to left");
    muiPopup p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 130.0f, 144.0f, mui_popupBelow), "start edges on the right");
    p.align = mui_popupAlignEnd;
    CHECK(Placed(&scene, &p, 150.0f, 144.0f, mui_popupBelow), "end edges on the left");
    p = PopupOf(scene.anchor, mui_popupStart, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 254.0f, 100.0f, mui_popupStart), "start on the right");
    p = PopupOf(scene.anchor, mui_popupEnd, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, 26.0f, 100.0f, mui_popupEnd), "end on the left");
    // Wider than the root: its start edge, the right, kept.
    CHECK(muiNode_SetLayoutValues(
              scene.context, scene.popup,
              &(muiLayoutStyle){.sizing.width = {0.0f, 500.0f, mui_dimensionValue}},
              MUI_PROPERTY_BIT(mui_propertyWidth)) == mui_success,
          "wide");
    p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(Placed(&scene, &p, -100.0f, 144.0f, mui_popupBelow), "wide, from the right");
    CHECK(muiNode_SetLayoutValues(
              scene.context, scene.popup,
              &(muiLayoutStyle){.sizing.height = {0.0f, 400.0f, mui_dimensionValue}},
              MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "tall");
    CHECK(Placed(&scene, &p, -100.0f, 0.0f, mui_popupBelow), "tall, from the top");
    muiDestroyContext(scene.context);
}

static void TestScrolledAndNested(void)
{
    // The anchor in a list scrolled by 100, the popup in a box at 30, 20.
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Sized(context, s_nullNode, 400.0f, 300.0f);
    muiNodeId list = Sized(context, root, 400.0f, 100.0f);
    At(context, list, 0.0f, 0.0f);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollVertical;
    style.container.direction = mui_flexColumn;
    CHECK(muiNode_SetLayoutValues(context, list, &style,
                                  MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                                      MUI_PROPERTY_BIT(mui_propertyFlexDirection)) == mui_success,
          "list");
    (void)Sized(context, list, 400.0f, 150.0f);
    muiNodeId anchor = Sized(context, list, 100.0f, 40.0f);
    (void)Sized(context, list, 400.0f, 300.0f);
    muiNodeId box = Sized(context, root, 50.0f, 50.0f);
    At(context, box, 30.0f, 20.0f);
    muiNodeId popup = Sized(context, box, 120.0f, 60.0f);
    At(context, popup, 0.0f, 0.0f);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){1.0f, 0.0f, 0.0f, 1.0f};
    CHECK(muiNode_SetVisualValues(context, popup, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "painted");
    Layout(context, root);
    CHECK(muiNode_SetScroll(context, list, 0.0f, 100.0f) == mui_success, "scrolled");
    muiPopup p = PopupOf(anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(muiNode_SetPopup(context, popup, &p) == mui_success, "set");
    Layout(context, root);
    CHECK(muiNode_GetRect(context, popup).x == -30.0f && muiNode_GetRect(context, popup).y == 74.0f,
          "below the scrolled anchor, from the box");
    const muiDrawInput input = {1, 1.0f, NULL, NULL};
    muiDrawList drawn;
    CHECK(muiBuildDrawList(context, root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.commandCount == 1 &&
              drawn.commands[0].box.rect.y == 94.0f,
          "drawn there");
    CHECK(muiNode_SetScroll(context, list, 0.0f, 120.0f) == mui_success, "scrolled on");
    Layout(context, root);
    CHECK(muiNode_GetRect(context, popup).y == 54.0f, "following it");
    CHECK(muiBuildDrawList(context, root, &input) == mui_success &&
              muiGetDrawList(context, &drawn) == mui_success && drawn.commandCount == 1 &&
              drawn.commands[0].box.rect.y == 74.0f,
          "drawn where it follows");
    // A popup anchored inside that popup, set first: placed after it.
    muiNodeId item = Sized(context, popup, 50.0f, 20.0f);
    At(context, item, 10.0f, 10.0f);
    muiNodeId sub = Sized(context, root, 80.0f, 30.0f);
    At(context, sub, 0.0f, 0.0f);
    CHECK(muiNode_ClearPopup(context, popup) == mui_success, "cleared");
    p = PopupOf(item, mui_popupEnd, mui_popupAlignStart, 0.0f);
    CHECK(muiNode_SetPopup(context, sub, &p) == mui_success, "the nested one first");
    p = PopupOf(anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(muiNode_SetPopup(context, popup, &p) == mui_success, "then its holder");
    CHECK(muiNode_SetScroll(context, list, 0.0f, 100.0f) == mui_success, "scrolled back");
    Layout(context, root);
    // The popup at 0, 94 on the surface, its item at 10, 104 to 60, 124.
    CHECK(muiNode_GetRect(context, sub).x == 60.0f && muiNode_GetRect(context, sub).y == 104.0f,
          "beside the item where its popup went");
    muiDestroyContext(context);
}

static void TestUnplaced(void)
{
    Scene scene;
    MakeScene(&scene, 150.0f, 100.0f);
    muiContext* context = scene.context;
    muiPopupSide side = mui_popupBelow;
    muiPopup p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success &&
              muiNode_GetPopupSide(context, scene.popup, &side) == mui_empty,
          "not placed before layout");
    // An anchor inside the popup: not placed.
    muiNodeId inside = Sized(context, scene.popup, 10.0f, 10.0f);
    p.anchor = inside;
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success, "inside");
    Layout(context, scene.root);
    CHECK(muiNode_GetPopupSide(context, scene.popup, &side) == mui_empty &&
              muiNode_GetRect(context, scene.popup).x == 0.0f,
          "not placed");
    // An anchor in another tree: not placed.
    muiNodeId other = Sized(context, s_nullNode, 10.0f, 10.0f);
    p.anchor = other;
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success, "elsewhere");
    Layout(context, scene.root);
    CHECK(muiNode_GetPopupSide(context, scene.popup, &side) == mui_empty, "not placed either");
    // The root, and a node in another tree: not placed.
    p.anchor = scene.anchor;
    CHECK(muiNode_SetPopup(context, scene.root, &p) == mui_success &&
              muiNode_SetPopup(context, other, &(muiPopup){.anchor = inside}) == mui_success,
          "the root and elsewhere");
    muiNodeId lone = Sized(context, other, 10.0f, 10.0f);
    CHECK(muiNode_SetPopup(context, lone, &p) == mui_success, "in the other tree");
    Layout(context, scene.root);
    CHECK(muiNode_GetPopupSide(context, scene.root, &side) == mui_empty &&
              muiNode_GetRect(context, scene.root).x == 0.0f &&
              muiNode_GetPopupSide(context, lone, &side) == mui_empty,
          "neither placed");
    CHECK(muiNode_ClearPopup(context, scene.root) == mui_success &&
              muiNode_ClearPopup(context, other) == mui_success &&
              muiNode_ClearPopup(context, lone) == mui_success,
          "cleared");
    // Anchors in a cycle: both placed, no hang.
    muiNodeId second = Sized(context, scene.root, 50.0f, 50.0f);
    At(context, second, 0.0f, 0.0f);
    muiNodeId inSecond = Sized(context, second, 10.0f, 10.0f);
    p.anchor = inSecond;
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success, "one way");
    p.anchor = inside;
    CHECK(muiNode_SetPopup(context, second, &p) == mui_success, "the other way");
    Layout(context, scene.root);
    CHECK(muiNode_GetPopupSide(context, scene.popup, &side) == mui_success &&
              muiNode_GetPopupSide(context, second, &side) == mui_success,
          "both placed");
    // The anchor destroyed: not placed, the popup where it was.
    muiRect before = muiNode_GetRect(context, scene.popup);
    CHECK(muiDestroyNode(context, second) == mui_success, "gone");
    Layout(context, scene.root);
    CHECK(muiNode_GetRect(context, scene.popup).x == before.x, "left where it was");
    muiDestroyContext(context);
}

// A mouse press and release at x, y.
static void Click(muiContext* context, muiNodeId root, float x, float y)
{
    static uint64_t s_time = 0;
    s_time += 1000000000ull;
    const muiPointerEvent press = {s_time, 1, mui_pointerMouse, mui_pointerPress, 0, 1, x, y, 0};
    const muiPointerEvent release = {s_time + 1, 1, mui_pointerMouse, mui_pointerRelease, 0, 0, x,
                                     y,          0};
    CHECK(muiPointerInput(context, root, &press) == mui_success &&
              muiPointerInput(context, root, &release) == mui_success,
          "click");
    muiPointerRecord record = {0};
    while (muiNextPointerRecord(context, &record) == mui_success)
    {
    }
}

static bool Key(muiContext* context, muiNodeId root, muiKeyCode code)
{
    const muiKeyEvent event = {.code = code, .down = true};
    bool handled = false;
    CHECK(muiKeyInput(context, root, &event, &handled) == mui_success, "key");
    return handled;
}

// The popups dismissed since last asked, in order, their reasons in
// reasons: how many, at most four.
static int Dismissed(muiContext* context, muiNodeId* nodes, muiDismissReason* reasons)
{
    int count = 0;
    muiNotification record = {0};
    while (muiNextNotification(context, &record) == mui_success)
    {
        if (record.kind == mui_notificationPopupDismissed && count < 4)
        {
            nodes[count] = record.nodeId;
            reasons[count] = (muiDismissReason)record.count;
            count++;
        }
    }
    return count;
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void Focusable(muiContext* context, muiNodeId node)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable");
}

static void TestLightDismiss(void)
{
    Scene scene;
    MakeScene(&scene, 150.0f, 100.0f);
    muiContext* context = scene.context;
    muiNodeId nodes[4];
    muiDismissReason reasons[4];
    muiPopup p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 4.0f);
    CHECK(p.lightDismiss && muiNode_SetPopup(context, scene.popup, &p) == mui_success,
          "light by default");
    Layout(context, scene.root);
    (void)Dismissed(context, nodes, reasons);
    Click(context, scene.root, 200.0f, 170.0f);
    Click(context, scene.root, 160.0f, 110.0f);
    CHECK(Dismissed(context, nodes, reasons) == 0, "inside, and on the anchor: kept");
    Click(context, scene.root, 10.0f, 10.0f);
    CHECK(Dismissed(context, nodes, reasons) == 1 && Same(nodes[0], scene.popup) &&
              reasons[0] == mui_dismissPress,
          "outside: dismissed");
    Click(context, scene.root, 10.0f, 10.0f);
    CHECK(Dismissed(context, nodes, reasons) == 0, "once");
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success, "set anew");
    Click(context, scene.root, 500.0f, 10.0f);
    CHECK(Dismissed(context, nodes, reasons) == 1, "on nothing: dismissed again");
    p.lightDismiss = false;
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success, "stays open");
    Click(context, scene.root, 10.0f, 10.0f);
    CHECK(Dismissed(context, nodes, reasons) == 0 && !Key(context, scene.root, mui_codeEscape),
          "not by a press or Escape");
    // Destroyed: nothing to dismiss.
    p.lightDismiss = true;
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success &&
              muiDestroyNode(context, scene.popup) == mui_success,
          "gone");
    Click(context, scene.root, 10.0f, 10.0f);
    CHECK(Dismissed(context, nodes, reasons) == 0 && !Key(context, scene.root, mui_codeEscape),
          "nothing");
    muiDestroyContext(context);
}

static void TestEscape(void)
{
    Scene scene;
    MakeScene(&scene, 150.0f, 100.0f);
    muiContext* context = scene.context;
    muiNodeId other = Sized(context, scene.root, 50.0f, 50.0f);
    muiNodeId nodes[4];
    muiDismissReason reasons[4];
    // Set in the order other, popup, other, popup: popup is the last,
    // though other is first in the table.
    muiPopup p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 0.0f);
    CHECK(muiNode_SetPopup(context, other, &p) == mui_success &&
              muiNode_SetPopup(context, scene.popup, &p) == mui_success &&
              muiNode_SetPopup(context, other, &p) == mui_success &&
              muiNode_SetPopup(context, scene.popup, &p) == mui_success,
          "two");
    Layout(context, scene.root);
    (void)Dismissed(context, nodes, reasons);
    CHECK(Key(context, scene.root, mui_codeEscape) && Dismissed(context, nodes, reasons) == 1 &&
              Same(nodes[0], scene.popup) && reasons[0] == mui_dismissEscape,
          "the last set");
    CHECK(Key(context, scene.root, mui_codeEscape) && Dismissed(context, nodes, reasons) == 1 &&
              Same(nodes[0], other),
          "then the other");
    CHECK(!Key(context, scene.root, mui_codeEscape), "then none");
    muiDestroyContext(context);
}

static void TestNestedDismiss(void)
{
    // A menu below the anchor, a submenu at the end of an item in it.
    Scene scene;
    MakeScene(&scene, 150.0f, 50.0f);
    muiContext* context = scene.context;
    muiNodeId item = Sized(context, scene.popup, 120.0f, 20.0f);
    At(context, item, 0.0f, 0.0f);
    muiNodeId sub = Sized(context, scene.root, 80.0f, 60.0f);
    At(context, sub, 0.0f, 0.0f);
    muiNodeId nodes[4];
    muiDismissReason reasons[4];
    muiPopup p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 0.0f);
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success, "menu");
    p = PopupOf(item, mui_popupEnd, mui_popupAlignStart, 0.0f);
    CHECK(muiNode_SetPopup(context, sub, &p) == mui_success, "submenu");
    Layout(context, scene.root);
    // The menu at 150, 90 to 270, 150; the submenu at 270, 90 to 350, 150.
    (void)Dismissed(context, nodes, reasons);
    Click(context, scene.root, 300.0f, 100.0f);
    CHECK(Dismissed(context, nodes, reasons) == 0, "in the submenu: both kept");
    Click(context, scene.root, 200.0f, 140.0f);
    CHECK(Dismissed(context, nodes, reasons) == 1 && Same(nodes[0], sub),
          "in the menu: the submenu dismissed");
    CHECK(muiNode_SetPopup(context, sub, &p) == mui_success, "submenu again");
    Click(context, scene.root, 10.0f, 290.0f);
    CHECK(Dismissed(context, nodes, reasons) == 2 && Same(nodes[0], sub) &&
              Same(nodes[1], scene.popup),
          "outside: the submenu first");
    // Anchored inside each other: both dismissed, no hang.
    muiNodeId inSub = Sized(context, sub, 10.0f, 10.0f);
    p = PopupOf(inSub, mui_popupBelow, mui_popupAlignStart, 0.0f);
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success, "a cycle");
    p = PopupOf(item, mui_popupEnd, mui_popupAlignStart, 0.0f);
    CHECK(muiNode_SetPopup(context, sub, &p) == mui_success, "and back");
    Layout(context, scene.root);
    Click(context, scene.root, 395.0f, 295.0f);
    CHECK(Dismissed(context, nodes, reasons) == 2, "both");
    muiDestroyContext(context);
}

static void TestFocusDismiss(void)
{
    Scene scene;
    MakeScene(&scene, 150.0f, 100.0f);
    muiContext* context = scene.context;
    muiNodeId inside = Sized(context, scene.popup, 20.0f, 20.0f);
    muiNodeId outside = Sized(context, scene.root, 20.0f, 20.0f);
    Focusable(context, inside);
    Focusable(context, outside);
    Focusable(context, scene.anchor);
    muiNodeId nodes[4];
    muiDismissReason reasons[4];
    muiPopup p = PopupOf(scene.anchor, mui_popupBelow, mui_popupAlignStart, 0.0f);
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success, "set");
    Layout(context, scene.root);
    (void)Dismissed(context, nodes, reasons);
    CHECK(muiFocus_Set(context, 0, inside, mui_focusByCode) == mui_success &&
              muiFocus_Set(context, 0, scene.anchor, mui_focusByNavigation) == mui_success &&
              Dismissed(context, nodes, reasons) == 0,
          "inside, and to the anchor: kept");
    CHECK(muiFocus_Set(context, 0, outside, mui_focusByPointer) == mui_success &&
              Dismissed(context, nodes, reasons) == 0,
          "by a pointer: the press decides");
    CHECK(muiFocus_Set(context, 0, (muiNodeId){0, 0}, mui_focusByCode) == mui_success &&
              Dismissed(context, nodes, reasons) == 0,
          "cleared: kept");
    CHECK(muiFocus_Set(context, 0, outside, mui_focusByCode) == mui_success &&
              Dismissed(context, nodes, reasons) == 1 && reasons[0] == mui_dismissFocus,
          "by code, outside: dismissed");
    // Tab from inside to the next, outside.
    CHECK(muiNode_SetPopup(context, scene.popup, &p) == mui_success &&
              muiFocus_Set(context, 0, inside, mui_focusByCode) == mui_success,
          "again, focused inside");
    (void)Dismissed(context, nodes, reasons);
    CHECK(Key(context, scene.root, mui_codeTab) && Dismissed(context, nodes, reasons) == 1 &&
              reasons[0] == mui_dismissFocus,
          "tabbed out: dismissed");
    muiDestroyContext(context);
}

static void TestContract(void)
{
    muiContextDef def = muiDefaultContextDef();
    CHECK(def.limits.popups == 16, "16 by default");
    def.limits.popups = 2;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Sized(context, s_nullNode, 400.0f, 300.0f);
    muiNodeId a = Sized(context, root, 10.0f, 10.0f);
    muiNodeId b = Sized(context, root, 10.0f, 10.0f);
    muiNodeId c = Sized(context, root, 10.0f, 10.0f);
    muiPopup p = PopupOf(root, mui_popupCenter, mui_popupAlignStart, 0.0f);
    muiPopup read = muiDefaultPopup();
    CHECK(read.side == mui_popupBelow && read.align == mui_popupAlignStart && read.gap == 0.0f &&
              read.margin == 0.0f && read.anchor.index1 == 0 && read.lightDismiss,
          "default");
    CHECK(muiNode_GetPopup(context, a, &read) == mui_empty, "none yet");
    CHECK(muiNode_SetPopup(context, a, &p) == mui_success &&
              muiNode_GetPopup(context, a, &read) == mui_success && read.side == mui_popupCenter &&
              read.anchor.index1 == root.index1,
          "read back");
    uint64_t misuse = muiGetContextMisuse(context);
    muiPopup bad = p;
    bad.anchor = s_nullNode;
    CHECK(muiNode_SetPopup(context, b, &bad) == mui_errorInvalid, "no anchor");
    bad = p;
    bad.anchor = b;
    CHECK(muiNode_SetPopup(context, b, &bad) == mui_errorInvalid, "its own anchor");
    bad = p;
    bad.side = 5;
    CHECK(muiNode_SetPopup(context, b, &bad) == mui_errorInvalid, "a side");
    bad = p;
    bad.align = 3;
    CHECK(muiNode_SetPopup(context, b, &bad) == mui_errorInvalid, "an alignment");
    const float lengths[] = {-1.0f, NAN, INFINITY};
    for (int i = 0; i < 3; i++)
    {
        bad = p;
        bad.gap = lengths[i];
        CHECK(muiNode_SetPopup(context, b, &bad) == mui_errorInvalid, "a gap");
        bad = p;
        bad.margin = lengths[i];
        CHECK(muiNode_SetPopup(context, b, &bad) == mui_errorInvalid, "a margin");
    }
    CHECK(muiNode_SetPopup(context, b, NULL) == mui_errorInvalid &&
              muiNode_SetPopup(context, s_nullNode, &p) == mui_errorInvalid &&
              muiNode_SetPopup(NULL, b, &p) == mui_errorInvalid,
          "nulls");
    CHECK(muiGetContextMisuse(context) == misuse + 12, "counted, the context's NULL aside");
    muiPopupSide side = mui_popupBelow;
    CHECK(muiNode_GetPopup(context, a, NULL) == mui_errorInvalid &&
              muiNode_GetPopup(NULL, a, &read) == mui_errorInvalid &&
              muiNode_GetPopup(context, s_nullNode, &read) == mui_errorInvalid &&
              muiNode_GetPopupSide(context, a, NULL) == mui_errorInvalid &&
              muiNode_GetPopupSide(NULL, a, &side) == mui_errorInvalid &&
              muiNode_GetPopupSide(context, s_nullNode, &side) == mui_errorInvalid &&
              muiNode_GetPopupSide(context, b, &side) == mui_empty &&
              muiNode_ClearPopup(NULL, a) == mui_errorInvalid &&
              muiNode_ClearPopup(context, s_nullNode) == mui_errorInvalid,
          "reads");
    // Full at two; a destroyed popup's room is taken back.
    CHECK(muiNode_SetPopup(context, b, &p) == mui_success &&
              muiNode_SetPopup(context, c, &p) == mui_errorCapacity,
          "full");
    CHECK(muiNode_SetPopup(context, b, &p) == mui_success, "set anew, no room needed");
    muiNodeId gone = b;
    CHECK(muiDestroyNode(context, b) == mui_success &&
              muiNode_SetPopup(context, c, &p) == mui_success,
          "room taken back");
    CHECK(muiNode_GetPopup(context, gone, &read) == mui_errorStale &&
              muiNode_SetPopup(context, gone, &p) == mui_errorStale &&
              muiNode_ClearPopup(context, gone) == mui_errorStale,
          "stale");
    p.anchor = gone;
    CHECK(muiNode_SetPopup(context, a, &p) == mui_errorStale, "a stale anchor");
    CHECK(muiNode_ClearPopup(context, a) == mui_success &&
              muiNode_GetPopup(context, a, &read) == mui_empty &&
              muiNode_ClearPopup(context, a) == mui_success,
          "cleared, twice");
    muiDestroyContext(context);
}

int main(void)
{
    TestSides();
    TestFlipAndClamp();
    TestRightToLeft();
    TestScrolledAndNested();
    TestUnplaced();
    TestLightDismiss();
    TestEscape();
    TestNestedDismiss();
    TestFocusDismiss();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
