// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's snippets after its first program (docs/guide.md), each as
// written there (tools/check_guide.py checks it, family record 0019),
// run and their results checked: refusals named and counted, and a
// context in a counted allocator within its limits, a frame taking no
// memory.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/exit.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/popup.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/theme.h"
#include "maul-ui/token.h"
#include "maul-ui/transition.h"
#include "maul-ui/virtual.h"
#include "maul-ui/visual.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Section 2: results, ids and refusals.

// What a context says of calls that went wrong.
static void Refusals(muiContext* context)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    if (muiCreateNode(context, &def, &node) != mui_success)
    {
        return;
    }
    (void)muiDestroyNode(context, node);
    // A later node may take the slot; the old id never names it.
    muiResult stale = muiNode_InsertChild(context, node, node, (muiNodeId){0, 0});
    // A null id where a node is needed is the program's bug: refused,
    // and counted.
    muiResult invalid = muiNode_InsertChild(context, (muiNodeId){0, 0}, node, (muiNodeId){0, 0});
    printf("%s, %s, %llu refused\n", muiResultName(stale), muiResultName(invalid),
           (unsigned long long)muiGetContextMisuse(context));
}

// Section 2: defs, allocators and limits.

// The bytes the library holds, counted.
static size_t s_held;

static void* Alloc(size_t size, size_t alignment, void* user)
{
    (void)user;
    // The library asks for no more alignment than malloc gives.
    void* memory = alignment <= alignof(max_align_t) ? malloc(size) : NULL;
    s_held += memory != NULL ? size : 0;
    return memory;
}

static void Free(void* memory, size_t size, size_t alignment, void* user)
{
    (void)alignment;
    (void)user;
    s_held -= size;
    free(memory);
}

// A context for a small panel: room for 64 nodes and 256 draw commands,
// taken when it is made, from the counted allocator.
static muiContext* SmallContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.allocator = (muiAllocator){Alloc, Free, NULL};
    def.limits.nodes = 64;
    def.limits.drawCommands = 256;
    muiContext* context = NULL;
    return muiCreateContext(&def, &context) == mui_success ? context : NULL;
}

// Section 3: nodes and the tree.

// Counts a node's children.
static uint32_t CountChildren(const muiContext* context, muiNodeId parent)
{
    uint32_t count = 0;
    for (muiNodeId child = muiNode_GetFirstChild(context, parent); child.index1 != 0;
         child = muiNode_GetNextSibling(context, child))
    {
        count++;
    }
    return count;
}

// Moves a node to the front of its parent's children, as a list sorted
// again does.
static muiResult MoveFirst(muiContext* context, muiNodeId node)
{
    muiNodeId parent = muiNode_GetParent(context, node);
    muiNodeId first = muiNode_GetFirstChild(context, parent);
    if (first.index1 == node.index1)
    {
        return mui_success;
    }
    muiResult result = muiNode_Detach(context, node);
    return result == mui_success ? muiNode_InsertChild(context, parent, node, first) : result;
}

// Section 4: classes, states and node types.

// A button class: padded and blue, lighter while hovered, darker while
// pressed; and a node type of buttons, which lists it.
static muiResult MakeButtonType(muiContext* context, muiStyleId* classOut, muiNodeTypeId* typeOut)
{
    muiResult result = muiCreateStyle(context, classOut);
    muiStyleId button = *classOut;
    if (result != mui_success)
    {
        return result;
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding = (muiEdges){12.0f, 12.0f, 6.0f, 6.0f};
    const muiPropertyMask padding =
        MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
        MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom);
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    muiVisualStyle normal = muiDefaultVisualStyle();
    normal.background = (muiColor){0.2f, 0.4f, 0.8f, 1.0f};
    muiVisualStyle hovered = normal;
    hovered.background = (muiColor){0.3f, 0.5f, 0.9f, 1.0f};
    muiVisualStyle pressed = normal;
    pressed.background = (muiColor){0.1f, 0.3f, 0.6f, 1.0f};
    if ((result = muiStyle_SetLayoutValues(context, button, mui_variantBase, &layout, padding)) !=
            mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantBase, &normal,
                                           background)) != mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantHovered, &hovered,
                                           background)) != mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantPressed, &pressed,
                                           background)) != mui_success)
    {
        return result;
    }
    return muiCreateNodeType(context, &button, 1, typeOut);
}

// Section 4: conditions.

// A class that stacks a row's children on small viewports, a phone's.
static muiResult MakeStacking(muiContext* context, muiStyleId* classOut)
{
    muiResult result = muiCreateStyle(context, classOut);
    muiCondition small = muiDefaultCondition();
    small.viewports = mui_viewportSmall;
    muiVariant variant = mui_variantBase;
    if (result == mui_success)
    {
        result = muiStyle_AddCondition(context, *classOut, &small, &variant);
    }
    muiLayoutStyle stacked = muiDefaultLayoutStyle();
    stacked.container.direction = mui_flexColumn;
    return result == mui_success
               ? muiStyle_SetLayoutValues(context, *classOut, variant, &stacked,
                                          MUI_PROPERTY_BIT(mui_propertyFlexDirection))
               : result;
}

// Section 4: tokens and themes.

// An accent color token the button class paints with, and a theme that
// makes it orange in the subtree it is set on.
static muiResult UseAccent(muiContext* context, muiStyleId button, muiNodeId warning)
{
    muiTokenValue value = {.type = mui_tokenColor, .color = {0.2f, 0.4f, 0.8f, 1.0f}};
    muiTokenId accent = {0, 0};
    muiThemeId alert = {0, 0};
    muiResult result = muiCreateToken(context, &value, &accent);
    if (result == mui_success)
    {
        result =
            muiStyle_SetToken(context, button, mui_variantBase, mui_propertyBackground, accent);
    }
    if (result == mui_success)
    {
        result = muiCreateTheme(context, &alert);
    }
    value.color = (muiColor){0.9f, 0.5f, 0.1f, 1.0f};
    if (result == mui_success)
    {
        result = muiTheme_SetTokenValue(context, alert, accent, &value);
    }
    return result == mui_success ? muiNode_SetTheme(context, warning, alert) : result;
}

// Section 7: the draw list.

// Where a box command lands on the surface, in device pixels: its rect
// through its transform, at the list's scale. A renderer's walk does
// this for each command, in order, in the command's clip.
static muiRect DeviceRect(const muiDrawList* list, uint32_t index)
{
    const muiDrawCommand* command = &list->commands[index];
    const muiDrawTransform* to = &list->transforms[command->transform];
    const muiRect r = command->box.rect;
    float scale = list->header.scale;
    // Scale and translation only, as layout makes them; a renderer takes
    // the whole affine transform.
    return (muiRect){(to->a * r.x + to->e) * scale, (to->d * r.y + to->f) * scale,
                     to->a * r.width * scale, to->d * r.height * scale};
}

// Section 7: painting host content.

// A gauge the program draws itself: a node whose host key is a fill
// level in thousandths, painted as a bar of that much of its width.
static void PaintGauge(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                       muiDrawSink* sink)
{
    (void)user;
    (void)nodeId;
    const muiColor green = {0.2f, 0.7f, 0.3f, 1.0f};
    float filled = width * (float)hostKey / 1000.0f;
    (void)muiDrawSink_AddRect(sink, (muiRect){0.0f, 0.0f, filled, height}, green);
}

// Section 8: routed events.

// What the program keeps: the button it watches and its clicks.
typedef struct App
{
    muiNodeId ok;
    int clicks;
} App;

// The function routed events reach, at each node from the root down to
// the target, then back up; true stops them. The OK button clicked, or
// activated by a gamepad's confirm button or by assistive technology, is
// the program's.
static bool OnEvent(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    App* app = user;
    bool clicked =
        event->kind == mui_eventPointer && event->pointer->kind == mui_pointerRecordClick;
    bool activated =
        event->kind == mui_eventNavigation && event->navigation == mui_navigateActivate;
    if (phase == mui_phaseBubble && (clicked || activated) && nodeId.index1 == app->ok.index1 &&
        nodeId.generation == app->ok.generation)
    {
        app->clicks++;
        return true;
    }
    return false;
}

// Section 8: pointers.

// Takes a platform's pointer event after the frame's layout, then
// routes the records it made: presses, releases, clicks, drags. Whether
// the interface used it, a record handled or a node under the pointer
// that does not let input through; the rest is the game's, behind it.
static bool Pointer(muiContext* context, muiNodeId root, const muiPointerEvent* event)
{
    if (muiPointerInput(context, root, event) != mui_success)
    {
        return false;
    }
    bool handled = false;
    muiPointerRecord record;
    while (muiNextPointerRecord(context, &record) == mui_success)
    {
        bool taken = false;
        (void)muiDispatchPointerRecord(context, &record, &taken);
        handled = handled || taken;
    }
    muiHit hit;
    return handled ||
           (muiHitTest(context, root, event->x, event->y, &hit) == mui_success && !hit.passThrough);
}

// Section 8: keys and focus.

// Lets a node take the focus from keys and pointers alike.
static muiResult MakeFocusable(muiContext* context, muiNodeId node)
{
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    return muiNode_SetInteractionValues(context, node, &interaction,
                                        MUI_PROPERTY_BIT(mui_propertyFocusMode));
}

// Hands a key to the focus under the root. Unhandled, Tab moves the
// focus, arrows move it toward their side or scroll, and the rest is
// the game's.
static bool Key(muiContext* context, muiNodeId root, muiKeyCode code, muiModifiers modifiers,
                bool down)
{
    const muiKeyEvent event = {
        .key = MUI_KEY_NAMED | code, .code = code, .modifiers = modifiers, .down = down};
    bool handled = false;
    return muiKeyInput(context, root, &event, &handled) == mui_success && handled;
}

// Section 9: virtual lists.

// A list of 10,000 rows 24 units high that scrolls, of which only the
// rows near its viewport exist as nodes.
static muiResult MakeList(muiContext* context, muiNodeId list)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.scrollAxes = mui_scrollVertical;
    muiVirtualList items = muiDefaultVirtualList();
    items.count = 10000;
    items.extent = 24.0f;
    items.fixed = true;
    muiResult result =
        muiNode_SetLayoutValues(context, list, &layout, MUI_PROPERTY_BIT(mui_propertyScrollAxes));
    return result == mui_success ? muiNode_SetVirtualList(context, list, &items) : result;
}

// Makes the rows the list's window asks for: a node bound to each index
// from first up to end. The old rows go and the window's are made anew;
// a program reuses them instead, binding a row that left to an index
// that came and resetting what it showed.
static muiResult Realize(muiContext* context, muiNodeId list)
{
    uint32_t first = 0;
    uint32_t end = 0;
    muiResult result = muiNode_GetVirtualWindow(context, list, &first, &end);
    for (muiNodeId row = muiNode_GetFirstChild(context, list);
         result == mui_success && row.index1 != 0; row = muiNode_GetFirstChild(context, list))
    {
        result = muiDestroyNode(context, row);
    }
    muiNodeDef def = muiDefaultNodeDef();
    for (uint32_t index = first; result == mui_success && index < end; index++)
    {
        muiNodeId row = {0, 0};
        result = muiCreateNode(context, &def, &row);
        if (result == mui_success)
        {
            result = muiNode_InsertChild(context, list, row, (muiNodeId){0, 0});
        }
        if (result == mui_success)
        {
            result = muiNode_SetItem(context, row, index);
        }
    }
    return result;
}

// A frame with a list: layout says when the list's window changed, the
// rows are made, and layout places them.
static muiResult ListFrame(muiContext* context, muiNodeId root, muiNodeId list,
                           const muiLayoutInput* input)
{
    muiResult result = muiComputeLayout(context, root, input);
    bool changed = false;
    muiNotification notification;
    while (result == mui_success && muiNextNotification(context, &notification) == mui_success)
    {
        changed = changed || (notification.kind == mui_notificationWindowChanged &&
                              notification.nodeId.index1 == list.index1);
    }
    if (result == mui_success && changed)
    {
        result = Realize(context, list);
        result = result == mui_success ? muiComputeLayout(context, root, input) : result;
    }
    return result;
}

// Section 9: popups.

// Opens a menu below a button: absolutely placed, so it takes no room
// in the tree, painted in the overlay layer above everything, and put
// beside its anchor after each layout. A press outside it or Escape
// dismisses it.
static muiResult OpenMenu(muiContext* context, muiNodeId menu, muiNodeId button)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.placement.position = mui_positionAbsolute;
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.layer = mui_layerOverlay;
    muiPopup popup = muiDefaultPopup();
    popup.anchor = button;
    popup.side = mui_popupBelow;
    popup.gap = 4.0f;
    muiResult result =
        muiNode_SetLayoutValues(context, menu, &layout, MUI_PROPERTY_BIT(mui_propertyPosition));
    if (result == mui_success)
    {
        result = muiNode_SetInteractionValues(context, menu, &interaction,
                                              MUI_PROPERTY_BIT(mui_propertyLayer));
    }
    return result == mui_success ? muiNode_SetPopup(context, menu, &popup) : result;
}

// Section 10: transitions and exits.

// A class whose nodes' opacity moves over 150 ms, easing out, and which
// fades them to nothing while they leave.
static muiResult MakeFading(muiContext* context, muiStyleId* classOut)
{
    muiTransitionDef def = muiDefaultTransitionDef();
    def.kind = mui_transitionTimed;
    def.durationNs = 150000000;
    def.easing = mui_easingEaseOut;
    muiTransitionId fade = {0, 0};
    muiVisualStyle gone = muiDefaultVisualStyle();
    gone.opacity = 0.0f;
    const muiPropertyMask opacity = MUI_PROPERTY_BIT(mui_propertyOpacity);
    muiResult result = muiCreateStyle(context, classOut);
    if (result == mui_success)
    {
        result = muiCreateTransition(context, &def, &fade);
    }
    if (result == mui_success)
    {
        result = muiStyle_SetTransition(context, *classOut, mui_variantBase, fade, mui_groupVisual,
                                        opacity);
    }
    return result == mui_success
               ? muiStyle_SetVisualValues(context, *classOut, mui_variantExiting, &gone, opacity)
               : result;
}

// Destroys the nodes whose exits finished, as a frame's notifications
// say. A node leaves with muiNode_BeginExit; the library never destroys
// it.
static void DestroyExited(muiContext* context)
{
    muiNotification notification;
    while (muiNextNotification(context, &notification) == mui_success)
    {
        if (notification.kind == mui_notificationExitFinished)
        {
            (void)muiDestroyNode(context, notification.nodeId);
        }
    }
}

// Section 11: accessibility.

// Tells assistive technology what a node is: a button, and its name.
static muiResult Describe(muiContext* context, muiNodeId button, const char* name)
{
    muiResult result = muiNode_SetAccessRole(context, button, mui_roleButton);
    return result == mui_success
               ? muiNode_SetAccessText(context, button, mui_accessLabel, name, strlen(name))
               : result;
}

// A frame's accessibility, once the root is enabled: the update of what
// changed since the last, for the platform's adapter, then the requests
// it queued from assistive technology, applied on the program's thread.
static muiResult AccessFrame(muiContext* context, muiNodeId root, muiAccessUpdate* updateOut,
                             const muiAccessRequest* requests, uint32_t requestCount)
{
    muiResult result = muiBuildAccessUpdate(context, root, updateOut);
    for (uint32_t i = 0; result == mui_success && i < requestCount; i++)
    {
        // A node that does not take the action, or is gone, is skipped.
        muiResult applied = muiPerformAccessAction(context, &requests[i], NULL);
        result = applied == mui_errorInvalid ? applied : mui_success;
    }
    return result;
}

// Section 12: testing a program.

// A test a program can run with no window and no GPU: its screen laid
// out and drawn twice with nothing changed, the second frame doing no
// work at all.
static bool StillFrameIsFree(muiContext* context, muiNodeId root)
{
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    if (muiComputeLayout(context, root, &layout) != mui_success ||
        muiBuildDrawList(context, root, &draw) != mui_success)
    {
        return false;
    }
    muiWorkCounts before = muiGetWorkCounts(context);
    if (muiComputeLayout(context, root, &layout) != mui_success ||
        muiBuildDrawList(context, root, &draw) != mui_success)
    {
        return false;
    }
    muiWorkCounts after = muiGetWorkCounts(context);
    return after.styled == before.styled && after.sized == before.sized &&
           after.measured == before.measured && after.painted == before.painted;
}

static void TestRefusals(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    Refusals(context);
    CHECK(muiGetContextMisuse(context) == 1, "one refusal counted, the stale id not");
    muiDestroyContext(context);
}

static void TestLimits(void)
{
    muiContext* context = SmallContext();
    CHECK(context != NULL && s_held > 0, "the limits' memory taken at once");
    size_t held = s_held;
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    muiNodeId node = {0, 0};
    bool made = muiCreateNode(context, &def, &root) == mui_success;
    for (int i = 1; i < 64; i++)
    {
        made = made && muiCreateNode(context, &def, &node) == mui_success &&
               muiNode_InsertChild(context, root, node, (muiNodeId){0, 0}) == mui_success;
    }
    CHECK(made && muiCreateNode(context, &def, &node) == mui_errorCapacity && s_held == held,
          "64 nodes, the 65th refused, nothing more taken");
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    CHECK(muiComputeLayout(context, root, &layout) == mui_success &&
              muiBuildDrawList(context, root, &draw) == mui_success && s_held == held,
          "a frame laid out and drawn taking nothing");
    muiDestroyContext(context);
    CHECK(s_held == 0, "everything given back");
}

// Adds a child of no style to a parent.
static muiNodeId Child(muiContext* context, muiNodeId parent)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    CHECK(muiCreateNode(context, &def, &node) == mui_success &&
              muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}) == mui_success,
          "a child");
    return node;
}

static void TestTree(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success, "a root");
    muiNodeId a = Child(context, root);
    Child(context, root);
    muiNodeId c = Child(context, root);
    CHECK(CountChildren(context, root) == 3 && MoveFirst(context, c) == mui_success &&
              muiNode_GetFirstChild(context, root).index1 == c.index1 &&
              muiNode_GetNextSibling(context, c).index1 == a.index1 &&
              CountChildren(context, root) == 3 && MoveFirst(context, c) == mui_success,
          "three children, the last moved first");
    muiDestroyContext(context);
}

static bool SameColor(muiColor a, muiColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static muiColor Background(muiContext* context, muiNodeId root, muiNodeId node)
{
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    muiVisualStyle visual = muiDefaultVisualStyle();
    CHECK(muiComputeLayout(context, root, &layout) == mui_success &&
              muiNode_GetVisualStyle(context, node, &visual) == mui_success,
          "styled");
    return visual.background;
}

static void TestStyles(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success, "a root");
    muiNodeId ok = Child(context, root);
    muiNodeId row = Child(context, root);
    muiNodeId warning = Child(context, row);
    muiStyleId button = {0, 0};
    muiNodeTypeId buttons = {0, 0};
    CHECK(MakeButtonType(context, &button, &buttons) == mui_success &&
              muiNode_SetType(context, ok, buttons) == mui_success &&
              muiNode_SetType(context, warning, buttons) == mui_success,
          "two buttons");
    const muiColor blue = {0.2f, 0.4f, 0.8f, 1.0f};
    const muiColor lighter = {0.3f, 0.5f, 0.9f, 1.0f};
    const muiColor darker = {0.1f, 0.3f, 0.6f, 1.0f};
    CHECK(SameColor(Background(context, root, ok), blue), "blue");
    CHECK(muiNode_SetStates(context, ok, mui_stateHovered) == mui_success &&
              SameColor(Background(context, root, ok), lighter),
          "lighter while hovered");
    CHECK(muiNode_SetStates(context, ok, mui_stateHovered | mui_statePressed) == mui_success &&
              SameColor(Background(context, root, ok), darker),
          "darker while pressed, which wins");
    muiRect box = muiNode_GetRect(context, ok);
    muiRect content = muiNode_GetContentRect(context, ok);
    CHECK(box.height == 12.0f && content.width == box.width - 24.0f, "padded");
    muiStyleId stacking = {0, 0};
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    CHECK(MakeStacking(context, &stacking) == mui_success &&
              muiNode_SetClasses(context, row, &stacking, 1) == mui_success,
          "a stacking row");
    (void)Background(context, root, row);
    CHECK(muiNode_GetLayoutStyle(context, row, &layout) == mui_success &&
              layout.container.direction == mui_flexRow,
          "a row on a medium viewport");
    muiEnvironment phone = muiDefaultEnvironment();
    phone.viewport = mui_viewportSmall;
    CHECK(muiSetContextEnvironment(context, &phone) == mui_success, "a phone");
    (void)Background(context, root, row);
    CHECK(muiNode_GetLayoutStyle(context, row, &layout) == mui_success &&
              layout.container.direction == mui_flexColumn,
          "a column on a small one");
    CHECK(UseAccent(context, button, row) == mui_success, "an accent");
    CHECK(muiNode_SetStates(context, ok, 0) == mui_success &&
              SameColor(Background(context, root, ok), blue) &&
              SameColor(Background(context, root, warning), (muiColor){0.9f, 0.5f, 0.1f, 1.0f}),
          "the accent, orange under the theme");
    muiDestroyContext(context);
}

static void TestDrawing(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    nodeDef.hostKey = 250;
    muiNodeId gauge = {0, 0};
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, 200.0f, mui_dimensionValue};
    layout.sizing.height = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    layout.content = mui_contentHost;
    CHECK(muiCreateNode(context, &nodeDef, &gauge) == mui_success &&
              muiNode_SetLayoutValues(context, gauge, &layout, MUI_LAYOUT_PROPERTIES) ==
                  mui_success,
          "a gauge a quarter full");
    const muiLayoutInput layoutInput = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 2.0f, PaintGauge, NULL};
    muiDrawList list = {0};
    CHECK(muiComputeLayout(context, gauge, &layoutInput) == mui_success &&
              muiBuildDrawList(context, gauge, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.commandCount == 1 &&
              list.commands[0].kind == mui_drawBox,
          "its bar, a box");
    muiRect device = DeviceRect(&list, 0);
    CHECK(device.x == 0.0f && device.y == 0.0f && device.width == 100.0f && device.height == 20.0f,
          "50 units wide, 100 device pixels at a scale of 2");
    muiDestroyContext(context);
}

static muiNodeId Button(muiContext* context, muiNodeId parent)
{
    muiNodeId node = Child(context, parent);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = (muiDimension){0.0f, 100.0f, mui_dimensionValue};
    layout.sizing.height = (muiDimension){0.0f, 40.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, node, &layout, MUI_LAYOUT_PROPERTIES) == mui_success &&
              MakeFocusable(context, node) == mui_success,
          "a button");
    return node;
}

static muiPointerEvent Mouse(muiPointerAction action, float x, float y)
{
    muiPointerEvent event = {0};
    event.kind = mui_pointerMouse;
    event.action = action;
    event.buttons = action == mui_pointerPress ? 1u : 0u;
    event.x = x;
    event.y = y;
    return event;
}

static void TestInput(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    muiLayoutStyle full = muiDefaultLayoutStyle();
    full.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    full.sizing.height = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success &&
              muiNode_SetLayoutValues(context, root, &full, MUI_LAYOUT_PROPERTIES) == mui_success,
          "a root");
    App app = {Button(context, root), 0};
    muiNodeId cancel = Button(context, root);
    CHECK(muiSetEventFunction(context, OnEvent, &app) == mui_success, "routed");
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiComputeLayout(context, root, &layout) == mui_success, "laid out");
    muiPointerEvent move = Mouse(mui_pointerMove, 50.0f, 20.0f);
    muiPointerEvent press = Mouse(mui_pointerPress, 50.0f, 20.0f);
    muiPointerEvent release = Mouse(mui_pointerRelease, 50.0f, 20.0f);
    CHECK(Pointer(context, root, &move) &&
              (muiNode_GetStates(context, app.ok) & mui_stateHovered) != 0,
          "hovered");
    CHECK(Pointer(context, root, &press) && Pointer(context, root, &release) && app.clicks == 1,
          "clicked");
    CHECK(muiFocus_Get(context, 0).index1 == app.ok.index1, "a press focuses");
    CHECK(Key(context, root, mui_codeTab, 0, true) &&
              muiFocus_Get(context, 0).index1 == cancel.index1 &&
              Key(context, root, mui_codeTab, mui_modShift, true) &&
              muiFocus_Get(context, 0).index1 == app.ok.index1,
          "Tab forward, Shift and Tab back");
    muiPointerEvent away = Mouse(mui_pointerMove, 700.0f, 500.0f);
    CHECK(Pointer(context, root, &away), "the root, opaque, uses a pointer over it");
    muiDestroyContext(context);
}

static uint32_t Rows(const muiContext* context, muiNodeId list, uint32_t* firstOut)
{
    uint32_t end = 0;
    CHECK(muiNode_GetVirtualWindow(context, list, firstOut, &end) == mui_success, "a window");
    uint32_t rows = 0;
    for (muiNodeId row = muiNode_GetFirstChild(context, list); row.index1 != 0;
         row = muiNode_GetNextSibling(context, row))
    {
        uint32_t index = 0;
        CHECK(muiNode_GetItem(context, row, &index) == mui_success && index >= *firstOut &&
                  index < end,
              "a row in the window");
        rows++;
    }
    CHECK(rows == end - *firstOut, "every index of the window a row");
    return rows;
}

static void TestLists(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    muiLayoutStyle full = muiDefaultLayoutStyle();
    full.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    full.sizing.height = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    full.container.direction = mui_flexColumn;
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success &&
              muiNode_SetLayoutValues(context, root, &full, MUI_LAYOUT_PROPERTIES) == mui_success,
          "a root");
    muiNodeId button = Button(context, root);
    muiNodeId list = Child(context, root);
    muiLayoutStyle grow = muiDefaultLayoutStyle();
    grow.item.grow = 1.0f;
    grow.item.basis = (muiDimension){0.0f, 0.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, list, &grow,
                                  MUI_PROPERTY_BIT(mui_propertyGrow) |
                                      MUI_PROPERTY_BIT(mui_propertyBasis)) == mui_success &&
              MakeList(context, list) == mui_success,
          "a list filling the rest");
    const muiLayoutInput layout = {400.0f, 340.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    uint32_t first = 0;
    CHECK(ListFrame(context, root, list, &layout) == mui_success, "a frame");
    uint32_t rows = Rows(context, list, &first);
    CHECK(first == 0 && rows > 12 && rows < 40, "a viewport's rows and the overscan's");
    CHECK(muiNode_SetScroll(context, list, 0.0f, 24.0f * 5000.0f) == mui_success &&
              ListFrame(context, root, list, &layout) == mui_success,
          "scrolled to row 5,000");
    rows = Rows(context, list, &first);
    CHECK(first < 5000 && first + rows > 5000 && rows < 40, "the rows around it");
    muiNodeId menu = Child(context, root);
    CHECK(OpenMenu(context, menu, button) == mui_success &&
              ListFrame(context, root, list, &layout) == mui_success,
          "a menu open");
    muiRect below = muiNode_GetRect(context, menu);
    CHECK(below.y == 44.0f, "4 below the button");
    muiPointerEvent press = Mouse(mui_pointerPress, 300.0f, 300.0f);
    muiPointerEvent release = Mouse(mui_pointerRelease, 300.0f, 300.0f);
    CHECK(Pointer(context, root, &press) && Pointer(context, root, &release), "a press outside");
    muiNotification notification;
    bool dismissed = false;
    while (muiNextNotification(context, &notification) == mui_success)
    {
        dismissed = dismissed || (notification.kind == mui_notificationPopupDismissed &&
                                  notification.nodeId.index1 == menu.index1);
    }
    CHECK(dismissed, "the menu dismissed");
    muiDestroyContext(context);
}

static float Opacity(muiContext* context, muiNodeId root, muiNodeId node, uint64_t timeNs)
{
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, timeNs, NULL, {0, 0, 0, 0}};
    muiVisualStyle visual = muiDefaultVisualStyle();
    CHECK(muiComputeLayout(context, root, &layout) == mui_success, "laid out");
    if (muiNode_GetVisualStyle(context, node, &visual) != mui_success)
    {
        return -1.0f;
    }
    return visual.opacity;
}

static void TestExits(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success, "a root");
    muiNodeId toast = Child(context, root);
    muiStyleId fading = {0, 0};
    CHECK(MakeFading(context, &fading) == mui_success &&
              muiNode_SetClasses(context, toast, &fading, 1) == mui_success &&
              Opacity(context, root, toast, 1000000000) == 1.0f,
          "a toast shown");
    CHECK(muiNode_BeginExit(context, toast) == mui_success &&
              Opacity(context, root, toast, 1000000000) == 1.0f,
          "leaving");
    float half = Opacity(context, root, toast, 1075000000);
    CHECK(half > 0.0f && half < 1.0f && muiIsUpdatePending(context, root), "half way out");
    DestroyExited(context);
    CHECK(muiNode_IsValid(context, toast), "still there");
    CHECK(Opacity(context, root, toast, 1150000000) == 0.0f, "gone from sight");
    DestroyExited(context);
    CHECK(!muiNode_IsValid(context, toast), "destroyed once its exit finished");
    muiDestroyContext(context);
}

static void TestAccess(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success, "a root");
    App app = {Button(context, root), 0};
    CHECK(muiSetEventFunction(context, OnEvent, &app) == mui_success &&
              Describe(context, app.ok, "OK") == mui_success &&
              muiAccess_Enable(context, root) == mui_success,
          "a described button, its root enabled");
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    muiAccessUpdate update;
    CHECK(muiComputeLayout(context, root, &layout) == mui_success &&
              AccessFrame(context, root, &update, NULL, 0) == mui_success &&
              update.root == muiAccessIdOf(root),
          "the first update, the whole tree");
    bool found = false;
    for (uint32_t i = 0; i < update.nodeCount; i++)
    {
        const muiAccessNode* node = update.nodes[i];
        found = found || (node->id == muiAccessIdOf(app.ok) && node->role == mui_roleButton &&
                          node->textLength[mui_accessLabel] == 2 &&
                          memcmp(node->text[mui_accessLabel], "OK", 2) == 0);
    }
    CHECK(found, "the button, named OK");
    const muiAccessRequest click = {.action = mui_actionClick, .target = muiAccessIdOf(app.ok)};
    CHECK(AccessFrame(context, root, &update, &click, 1) == mui_success && app.clicks == 1,
          "clicked by assistive technology");
    muiDestroyContext(context);
}

static void TestStill(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success, "a root");
    Button(context, root);
    Button(context, root);
    CHECK(StillFrameIsFree(context, root), "a still frame free");
    muiDestroyContext(context);
}

int main(void)
{
    TestRefusals();
    TestLimits();
    TestTree();
    TestStyles();
    TestDrawing();
    TestInput();
    TestLists();
    TestExits();
    TestAccess();
    TestStill();
    return s_failures == 0 ? 0 : 1;
}
