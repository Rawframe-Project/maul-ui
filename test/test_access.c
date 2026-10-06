// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility (record mui-0008): whole updates and updates of what
// changed, what the library derives, children leaving and coming back,
// actions, and the contract of the host's data and the roots.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/exit.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/range.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/virtual.h"
#include "maul-ui/visual.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const muiNodeId s_nullNode = {0, 0};

static muiNodeId Node(muiContext* context, muiNodeId parent)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    return node;
}

static void Size(muiContext* context, muiNodeId node, float width, float height)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.item.shrink = 0.0f;
    style.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    style.sizing.height = (muiDimension){0.0f, height, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyShrink) |
                                      MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "size");
}

static void Column(muiContext* context, muiNodeId node, bool scrolls)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.container.direction = mui_flexColumn;
    style.scrollAxes = scrolls ? mui_scrollVertical : mui_scrollNone;
    CHECK(muiNode_SetLayoutValues(context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                                      MUI_PROPERTY_BIT(mui_propertyScrollAxes)) == mui_success,
          "column");
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static void Text(muiContext* context, muiNodeId node, muiAccessTextKind kind, const char* text)
{
    CHECK(muiNode_SetAccessText(context, node, kind, text, strlen(text)) == mui_success, "text");
}

static muiAccessUpdate Build(muiContext* context, muiNodeId root)
{
    muiAccessUpdate update;
    CHECK(muiBuildAccessUpdate(context, root, &update) == mui_success, "build");
    return update;
}

// An update as text: a node a line, by slot, with its role, flags,
// actions, size, place, texts and children.
static const char* Dump(const muiAccessUpdate* update)
{
    static char s_text[4096];
    size_t at = 0;
    for (uint32_t i = 0; i < update->nodeCount && at < sizeof(s_text); i++)
    {
        const muiAccessNode* node = update->nodes[i];
        at += (size_t)snprintf(s_text + at, sizeof(s_text) - at, "%u r%u f%x a%x %gx%g @%g,%g",
                               (uint32_t)node->id, node->role, node->flags, node->actions,
                               (double)node->bounds.width, (double)node->bounds.height,
                               (double)node->transform.e, (double)node->transform.f);
        for (uint32_t kind = 0; kind < MUI_ACCESS_TEXTS && at < sizeof(s_text); kind++)
        {
            if (node->text[kind] != NULL)
            {
                at += (size_t)snprintf(s_text + at, sizeof(s_text) - at, " t%u=%s", kind,
                                       node->text[kind]);
            }
        }
        for (uint32_t k = 0; k < node->childCount && at < sizeof(s_text); k++)
        {
            at += (size_t)snprintf(s_text + at, sizeof(s_text) - at, "%s%u", k == 0 ? " [" : " ",
                                   (uint32_t)update->children[node->firstChild + k]);
        }
        at += (size_t)snprintf(s_text + at, sizeof(s_text) - at, "%s\n",
                               node->childCount != 0 ? "]" : "");
    }
    s_text[at < sizeof(s_text) ? at : sizeof(s_text) - 1] = '\0';
    return s_text;
}

// The node with an id in an update, or NULL.
static const muiAccessNode* Sent(const muiAccessUpdate* update, muiNodeId node)
{
    for (uint32_t i = 0; i < update->nodeCount; i++)
    {
        if (update->nodes[i]->id == muiAccessIdOf(node))
        {
            return update->nodes[i];
        }
    }
    return NULL;
}

static uint32_t Action(muiAccessAction action)
{
    return 1u << action;
}

// A root fitting a column: a button (100 by 40, "OK"), a label
// (100 by 20, "Hello"), and a group (200 by 50) holding a leaf (50 by
// 50).
typedef struct Scene
{
    muiContext* context;
    muiNodeId root;
    muiNodeId button;
    muiNodeId label;
    muiNodeId group;
    muiNodeId leaf;
} Scene;

static void MakeScene(Scene* scene)
{
    muiContextDef def = muiDefaultContextDef();
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiContext* context = scene->context;
    scene->root = Node(context, s_nullNode);
    Column(context, scene->root, false);
    scene->button = Node(context, scene->root);
    Size(context, scene->button, 100.0f, 40.0f);
    CHECK(muiNode_SetAccessRole(context, scene->button, mui_roleButton) == mui_success, "button");
    Text(context, scene->button, mui_accessLabel, "OK");
    scene->label = Node(context, scene->root);
    Size(context, scene->label, 100.0f, 20.0f);
    CHECK(muiNode_SetAccessRole(context, scene->label, mui_roleLabel) == mui_success, "label");
    Text(context, scene->label, mui_accessValue, "Hello");
    scene->group = Node(context, scene->root);
    Size(context, scene->group, 200.0f, 50.0f);
    scene->leaf = Node(context, scene->group);
    Size(context, scene->leaf, 50.0f, 50.0f);
}

static void TestWholeThenChanged(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    muiAccessUpdate update;
    CHECK(muiBuildAccessUpdate(context, scene.root, &update) == mui_empty, "not enabled");
    CHECK(muiAccess_Enable(context, scene.root) == mui_success, "enabled");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "1 r0 f0 a0 200x110 @0,0 [2 3 4]\n"
                                "2 r4 f0 a101 100x40 @0,0 t0=OK\n"
                                "3 r1 f0 a100 100x20 @0,40 t2=Hello\n"
                                "4 r0 f0 a100 200x50 @0,60 [5]\n"
                                "5 r0 f0 a100 50x50 @0,0\n") == 0,
          "the whole tree");
    CHECK(update.root == muiAccessIdOf(scene.root) && update.focus == muiAccessIdOf(scene.root),
          "the root, and the focus on it");
    update = Build(context, scene.root);
    CHECK(update.nodeCount == 0 && update.root == 0, "nothing changed");
    // A text: that node alone.
    Text(context, scene.label, mui_accessValue, "Hi");
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "3 r1 f0 a100 100x20 @0,40 t2=Hi\n") == 0, "a text");
    // Paint alone: marked, compared, not sent.
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){1.0f, 0.0f, 0.0f, 1.0f};
    CHECK(muiNode_SetVisualValues(context, scene.button, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "red");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(update.nodeCount == 0, "a color is nothing to assistive technology");
    // A size: that node, and the root that fits it; its child's place in
    // it is the same.
    Size(context, scene.group, 200.0f, 70.0f);
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "1 r0 f0 a0 200x130 @0,0 [2 3 4]\n"
                                "4 r0 f0 a100 200x70 @0,60 [5]\n") == 0,
          "a size");
    muiDestroyContext(context);
}

static void TestChildren(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    CHECK(muiAccess_Enable(context, scene.root) == mui_success, "enabled");
    Layout(context, scene.root);
    (void)Build(context, scene.root);
    // A child added: its parent's list and the child.
    muiNodeId added = Node(context, scene.group);
    Size(context, added, 10.0f, 10.0f);
    Layout(context, scene.root);
    muiAccessUpdate update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "4 r0 f0 a100 200x50 @0,60 [5 6]\n"
                                "6 r0 f0 a100 10x10 @50,0\n") == 0,
          "added");
    // Taken out: the list alone; put back where it was: both again, as
    // adapters let it go with the list that left it out.
    CHECK(muiNode_Detach(context, added) == mui_success, "detached");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "4 r0 f0 a100 200x50 @0,60 [5]\n") == 0, "taken out");
    muiNodeId inner = Node(context, added);
    Size(context, inner, 5.0f, 5.0f);
    CHECK(muiNode_InsertChild(context, scene.group, added, s_nullNode) == mui_success, "back");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "4 r0 f0 a100 200x50 @0,60 [5 6]\n"
                                "6 r0 f0 a100 10x10 @50,0 [7]\n"
                                "7 r0 f0 a100 5x5 @0,0\n") == 0,
          "back with its subtree");
    // Moved to another parent: both lists, and the subtree it carries.
    CHECK(muiNode_Detach(context, added) == mui_success &&
              muiNode_InsertChild(context, scene.root, added, s_nullNode) == mui_success,
          "moved");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "1 r0 f0 a0 200x120 @0,0 [2 3 4 6]\n"
                                "4 r0 f0 a100 200x50 @0,60 [5]\n"
                                "6 r0 f0 a100 10x10 @0,110 [7]\n"
                                "7 r0 f0 a100 5x5 @0,0\n") == 0,
          "moved with its subtree");
    // Destroyed: its parent's list.
    CHECK(muiDestroyNode(context, added) == mui_success, "destroyed");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "1 r0 f0 a0 200x110 @0,0 [2 3 4]\n") == 0, "destroyed");
    // A node in the slot it left is new.
    muiNodeId reused = Node(context, scene.root);
    Size(context, reused, 10.0f, 10.0f);
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(update.nodeCount == 2 && Sent(&update, reused) != NULL &&
              Sent(&update, scene.root) != NULL,
          "a new node in an old slot");
    muiDestroyContext(context);
}

static void TestDerived(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    CHECK(muiAccess_Enable(context, scene.root) == mui_success, "enabled");
    Layout(context, scene.root);
    (void)Build(context, scene.root);
    // Checked and selected only where they can be.
    CHECK(muiNode_SetStates(context, scene.button, mui_stateChecked | mui_stateSelected) ==
              mui_success,
          "states");
    Layout(context, scene.root);
    muiAccessUpdate update = Build(context, scene.root);
    CHECK(update.nodeCount == 0, "states it cannot have");
    CHECK(muiNode_SetAccessFlags(context, scene.button,
                                 mui_accessCheckable | mui_accessSelectable | mui_accessRequired) ==
              mui_success,
          "flags");
    update = Build(context, scene.root);
    const muiAccessNode* node = Sent(&update, scene.button);
    CHECK(node != NULL &&
              node->flags == (mui_accessCheckable | mui_accessSelectable | mui_accessRequired |
                              mui_accessChecked | mui_accessSelected),
          "checked and selected");
    // Disabled: no click.
    CHECK(muiNode_SetStates(context, scene.button, mui_stateDisabled) == mui_success, "disabled");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    node = Sent(&update, scene.button);
    CHECK(node != NULL && (node->flags & mui_accessDisabled) != 0 &&
              node->actions == Action(mui_actionScrollIntoView),
          "disabled, no click");
    CHECK(muiNode_SetStates(context, scene.button, 0) == mui_success, "enabled again");
    // Focusable, then focused: focus offered, then blur, and the focus.
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, scene.button, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    node = Sent(&update, scene.button);
    CHECK(node != NULL && (node->flags & mui_accessFocusable) != 0 &&
              (node->actions & Action(mui_actionFocus)) != 0,
          "focus offered");
    CHECK(muiFocus_Set(context, 0, scene.button, mui_focusByCode) == mui_success, "focused");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    node = Sent(&update, scene.button);
    CHECK(node != NULL && (node->actions & Action(mui_actionFocus)) == 0 &&
              (node->actions & Action(mui_actionBlur)) != 0 &&
              update.focus == muiAccessIdOf(scene.button),
          "focused, blur offered");
    // A modal layer.
    interaction.layer = mui_layerModal;
    CHECK(muiNode_SetInteractionValues(context, scene.group, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "modal");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    node = Sent(&update, scene.group);
    CHECK(node != NULL && (node->flags & mui_accessModal) != 0, "modal");
    // Exiting: hidden.
    CHECK(muiNode_BeginExit(context, scene.label) == mui_success, "exits");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    node = Sent(&update, scene.label);
    CHECK(node != NULL && (node->flags & mui_accessHidden) != 0, "exiting is hidden");
    muiDestroyContext(context);
}

static void TestScrollAndRange(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Node(context, s_nullNode);
    muiNodeId pane = Node(context, root);
    Column(context, pane, true);
    Size(context, pane, 200.0f, 100.0f);
    muiNodeId rows[3];
    for (int i = 0; i < 3; i++)
    {
        rows[i] = Node(context, pane);
        Size(context, rows[i], 200.0f, 60.0f);
    }
    muiNodeId slider = Node(context, root);
    Size(context, slider, 100.0f, 20.0f);
    muiValueRange range = muiDefaultValueRange();
    range.value = 50.0f;
    CHECK(muiNode_SetValueRange(context, slider, &range) == mui_success, "range");
    CHECK(muiAccess_Enable(context, root) == mui_success, "enabled");
    Layout(context, root);
    muiAccessUpdate update = Build(context, root);
    const muiAccessNode* node = Sent(&update, pane);
    uint32_t scrolls = Action(mui_actionScrollIntoView) | Action(mui_actionScrollUp) |
                       Action(mui_actionScrollDown) | Action(mui_actionSetScrollOffset);
    CHECK(node != NULL && node->flags == (mui_accessClipsChildren | mui_accessScrolls) &&
              node->actions == scrolls && node->scrollY == 0.0f && node->scrollYMax == 80.0f,
          "a scroll container");
    node = Sent(&update, slider);
    uint32_t values =
        Action(mui_actionIncrement) | Action(mui_actionDecrement) | Action(mui_actionSetValue);
    CHECK(node != NULL && node->flags == mui_accessNumeric &&
              node->actions == (values | Action(mui_actionScrollIntoView)) &&
              node->value == 50.0f && node->minimum == 0.0f && node->maximum == 100.0f &&
              node->step == 1.0f,
          "a range");
    // Scrolled: the container and its children, each place moved.
    CHECK(muiNode_SetScroll(context, pane, 0.0f, 30.0f) == mui_success, "scrolled");
    Layout(context, root);
    update = Build(context, root);
    CHECK(update.nodeCount == 4 && Sent(&update, pane)->scrollY == 30.0f &&
              Sent(&update, rows[0])->transform.f == -30.0f &&
              Sent(&update, rows[2])->transform.f == 90.0f,
          "scrolled");
    // A value set: the range alone; read only: no values to set.
    CHECK(muiNode_SetRangeValue(context, slider, 70.0f) == mui_success, "value");
    update = Build(context, root);
    CHECK(update.nodeCount == 1 && Sent(&update, slider)->value == 70.0f, "a value");
    CHECK(muiNode_SetAccessFlags(context, slider, mui_accessReadOnly) == mui_success, "read only");
    update = Build(context, root);
    CHECK(update.nodeCount == 1 &&
              Sent(&update, slider)->actions == Action(mui_actionScrollIntoView),
          "read only");
    muiDestroyContext(context);
}

static void TestVirtualItems(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Node(context, s_nullNode);
    muiNodeId list = Node(context, root);
    Column(context, list, true);
    Size(context, list, 200.0f, 100.0f);
    muiVirtualList virtualList = muiDefaultVirtualList();
    virtualList.count = 1000;
    virtualList.extent = 40.0f;
    virtualList.fixed = true;
    virtualList.overscan = 0.0f;
    CHECK(muiNode_SetVirtualList(context, list, &virtualList) == mui_success, "list");
    // Bound out of order, after a node of no item.
    muiNodeId other = Node(context, list);
    muiNodeId items[3];
    uint32_t indices[3] = {2, 0, 1};
    for (int i = 0; i < 3; i++)
    {
        items[i] = Node(context, list);
        CHECK(muiNode_SetItem(context, items[i], indices[i]) == mui_success, "bound");
    }
    CHECK(muiAccess_Enable(context, root) == mui_success, "enabled");
    Layout(context, root);
    muiAccessUpdate update = Build(context, root);
    const muiAccessNode* node = Sent(&update, list);
    CHECK(node != NULL && node->childCount == 4 &&
              update.children[node->firstChild] == muiAccessIdOf(other) &&
              update.children[node->firstChild + 1] == muiAccessIdOf(items[1]) &&
              update.children[node->firstChild + 2] == muiAccessIdOf(items[2]) &&
              update.children[node->firstChild + 3] == muiAccessIdOf(items[0]),
          "read by item");
    node = Sent(&update, items[0]);
    CHECK(node != NULL && node->setPosition == 3 && node->setSize == 1000, "third of 1000");
    CHECK(Sent(&update, other)->setPosition == 0, "no item");
    // Rebound: the list's order and the item.
    CHECK(muiNode_SetItem(context, items[0], 5) == mui_success, "rebound");
    Layout(context, root);
    update = Build(context, root);
    CHECK(Sent(&update, items[0]) != NULL && Sent(&update, items[0])->setPosition == 6, "sixth");
    muiDestroyContext(context);
}

typedef struct Heard
{
    muiNodeId node;
    muiPhase phase;
    muiEventKind kind;
    muiNavigation navigation;
    int count;
    muiContext* context;
    muiResult nested;
} Heard;

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Heard* heard = user;
    heard->count++;
    heard->node = nodeId;
    heard->phase = phase;
    heard->kind = event->kind;
    heard->navigation = event->navigation;
    const muiAccessRequest request = {mui_actionClick, muiAccessIdOf(event->target), 0, 0, 0};
    heard->nested = muiPerformAccessAction(heard->context, &request, NULL);
    // Handled where it was aimed.
    return phase == mui_phaseBubble && nodeId.index1 == event->target.index1;
}

static void TestActions(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    Heard heard = {.context = context};
    CHECK(muiSetEventFunction(context, Hear, &heard) == mui_success, "listening");
    Layout(context, scene.root);
    // A click: activation routed to the button, which handles it; an
    // action from inside the route is refused.
    muiAccessRequest request = {mui_actionClick, muiAccessIdOf(scene.button), 0, 0, 0};
    bool handled = false;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              heard.kind == mui_eventNavigation && heard.navigation == mui_navigateActivate &&
              heard.node.index1 == scene.button.index1 && heard.count == 3 &&
              heard.nested == mui_errorInvalid,
          "clicked");
    // Nothing to click on the label.
    request.target = muiAccessIdOf(scene.label);
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_empty && !handled,
          "not a button");
    // Focus and blur.
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, scene.button, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable");
    Layout(context, scene.root);
    request = (muiAccessRequest){mui_actionFocus, muiAccessIdOf(scene.button), 0, 0, 0};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiFocus_Get(context, 0).index1 == scene.button.index1,
          "focused");
    request.action = mui_actionFocus;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_empty, "focused already");
    request.action = mui_actionBlur;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiFocus_Get(context, 0).index1 == 0,
          "blurred");
    // A range: steps and a value, each reported as input's.
    muiValueRange range = muiDefaultValueRange();
    range.value = 50.0f;
    CHECK(muiNode_SetValueRange(context, scene.leaf, &range) == mui_success, "range");
    muiNotification record;
    while (muiNextNotification(context, &record) == mui_success)
    {
    }
    request = (muiAccessRequest){mui_actionIncrement, muiAccessIdOf(scene.leaf), 0, 0, 0};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled, "up");
    request.action = mui_actionSetValue;
    request.value = 20.4f;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled, "set");
    request.action = mui_actionDecrement;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled, "down");
    muiValueRange now;
    CHECK(muiNode_GetValueRange(context, scene.leaf, &now) == mui_success && now.value == 19.0f,
          "51, 20, 19");
    int changes = 0;
    while (muiNextNotification(context, &record) == mui_success)
    {
        changes += record.kind == mui_notificationRangeChanged;
    }
    CHECK(changes == 3, "each reported");
    // Expand: the host's, posted.
    CHECK(muiNode_SetAccessFlags(context, scene.group, mui_accessExpandable) == mui_success,
          "expandable");
    request = (muiAccessRequest){mui_actionExpand, muiAccessIdOf(scene.group), 0, 0, 0};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNextNotification(context, &record) == mui_success &&
              record.kind == mui_notificationAccessAction &&
              record.nodeId.index1 == scene.group.index1 && record.count == mui_actionExpand,
          "expand posted");
    request.action = mui_actionCollapse;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_empty, "not expanded");
    // Misuse.
    uint64_t misuse = muiGetContextMisuse(context);
    request =
        (muiAccessRequest){mui_actionSetScrollOffset + 1, muiAccessIdOf(scene.group), 0, 0, 0};
    CHECK(muiPerformAccessAction(context, &request, NULL) == mui_errorInvalid, "unknown");
    request = (muiAccessRequest){mui_actionSetValue, muiAccessIdOf(scene.leaf), INFINITY, 0, 0};
    CHECK(muiPerformAccessAction(context, &request, NULL) == mui_errorInvalid &&
              muiPerformAccessAction(context, NULL, NULL) == mui_errorInvalid &&
              muiPerformAccessAction(NULL, &request, NULL) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 3,
          "refused");
    muiDestroyContext(context);
}

static void TestScrollActions(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiEnvironment environment = muiDefaultEnvironment();
    environment.reducedMotion = true;
    CHECK(muiSetContextEnvironment(context, &environment) == mui_success, "at once");
    muiNodeId root = Node(context, s_nullNode);
    muiNodeId pane = Node(context, root);
    Column(context, pane, true);
    Size(context, pane, 200.0f, 100.0f);
    muiNodeId rows[5];
    for (int i = 0; i < 5; i++)
    {
        rows[i] = Node(context, pane);
        Size(context, rows[i], 200.0f, 60.0f);
    }
    Layout(context, root);
    float x = 0.0f;
    float y = 0.0f;
    muiAccessRequest request = {mui_actionScrollDown, muiAccessIdOf(pane), 0, 0, 0};
    bool handled = false;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, pane, &x, &y) == mui_success && y > 0.0f,
          "a page down");
    request.action = mui_actionScrollLeft;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_empty, "not across");
    request = (muiAccessRequest){mui_actionSetScrollOffset, muiAccessIdOf(pane), 0, 0, 1000.0f};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, pane, &x, &y) == mui_success && y == 200.0f,
          "to the end");
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && !handled,
          "there already");
    request = (muiAccessRequest){mui_actionScrollIntoView, muiAccessIdOf(rows[0]), 0, 0, 0};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, pane, &x, &y) == mui_success && y == 0.0f,
          "into view");
    muiDestroyContext(context);
}

static void TestHostData(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.accessNodes = 2;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId a = Node(context, s_nullNode);
    muiNodeId b = Node(context, s_nullNode);
    muiNodeId c = Node(context, s_nullNode);
    muiRole role = 99;
    muiAccessFlags flags = 99;
    const char* text = NULL;
    size_t length = 0;
    CHECK(muiNode_GetAccessRole(context, a, &role) == mui_success && role == mui_roleGeneric &&
              muiNode_GetAccessFlags(context, a, &flags) == mui_success && flags == 0 &&
              muiNode_GetAccessText(context, a, mui_accessLabel, &text, &length) == mui_empty,
          "none");
    // Clearing what is not there takes no room.
    CHECK(muiNode_SetAccessText(context, c, mui_accessLabel, NULL, 0) == mui_success, "cleared");
    CHECK(muiNode_SetAccessRole(context, a, mui_roleMarquee) == mui_success &&
              muiNode_SetAccessFlags(context, b, mui_accessBusy) == mui_success,
          "two");
    CHECK(muiNode_SetAccessRole(context, c, mui_roleButton) == mui_errorCapacity &&
              muiNode_SetAccessFlags(context, c, 0) == mui_errorCapacity &&
              muiNode_SetAccessText(context, c, mui_accessLabel, "x", 1) == mui_errorCapacity,
          "full");
    // A destroyed node's room, and its texts, come back.
    Text(context, b, mui_accessDescription, "gone soon");
    CHECK(muiDestroyNode(context, b) == mui_success &&
              muiNode_SetAccessRole(context, c, mui_roleButton) == mui_success,
          "room again");
    Text(context, a, mui_accessLabel, "Name");
    Text(context, a, mui_accessLabel, "Named");
    CHECK(muiNode_GetAccessText(context, a, mui_accessLabel, &text, &length) == mui_success &&
              length == 5 && strcmp(text, "Named") == 0,
          "replaced");
    CHECK(muiNode_SetAccessText(context, a, mui_accessLabel, "", 0) == mui_success &&
              muiNode_GetAccessText(context, a, mui_accessLabel, &text, &length) == mui_empty,
          "cleared");
    // Well-formed UTF-8 only, without NUL.
    static const char* const s_bad[] = {"\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80",
                                        "\xE2\x82", "\x80",         "\xF5\x80\x80\x80"};
    uint64_t misuse = muiGetContextMisuse(context);
    for (size_t i = 0; i < sizeof(s_bad) / sizeof(s_bad[0]); i++)
    {
        CHECK(muiNode_SetAccessText(context, a, mui_accessValue, s_bad[i], strlen(s_bad[i])) ==
                  mui_errorInvalid,
              "ill-formed");
    }
    CHECK(muiNode_SetAccessText(context, a, mui_accessValue, "a\0b", 3) == mui_errorInvalid &&
              muiNode_SetAccessText(context, a, mui_accessValue, NULL, 1) == mui_errorInvalid &&
              muiNode_SetAccessText(context, a, MUI_ACCESS_TEXTS, "a", 1) == mui_errorInvalid &&
              muiNode_SetAccessRole(context, a, MUI_ROLE_LAST + 1) == mui_errorInvalid &&
              muiNode_SetAccessFlags(context, a, mui_accessChecked) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 11,
          "refused");
    static const char s_good[] = "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xEF\xBF\xBF";
    CHECK(muiNode_SetAccessText(context, a, mui_accessValue, s_good, sizeof(s_good) - 1) ==
              mui_success,
          "well-formed");
    CHECK(muiNode_GetAccessRole(context, b, &role) == mui_errorStale &&
              muiNode_GetAccessRole(context, s_nullNode, &role) == mui_errorInvalid &&
              muiNode_SetAccessRole(context, b, mui_roleButton) == mui_errorStale &&
              muiNode_GetAccessText(context, a, MUI_ACCESS_TEXTS, &text, &length) ==
                  mui_errorInvalid &&
              muiNode_GetAccessFlags(NULL, a, &flags) == mui_errorInvalid,
          "reads");
    muiDestroyContext(context);
}

static void TestRoots(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.accessRoots = 1;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Node(context, s_nullNode);
    muiNodeId child = Node(context, root);
    muiNodeId other = Node(context, s_nullNode);
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiAccess_Enable(context, child) == mui_errorInvalid &&
              muiAccess_Enable(context, s_nullNode) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 2,
          "roots only");
    CHECK(muiAccess_Enable(context, root) == mui_success &&
              muiAccess_Enable(context, other) == mui_errorCapacity,
          "one root");
    Layout(context, root);
    muiAccessUpdate update = Build(context, root);
    CHECK(update.nodeCount == 2, "whole");
    // Enabled again: whole again.
    CHECK(muiAccess_Enable(context, root) == mui_success, "again");
    update = Build(context, root);
    CHECK(update.nodeCount == 2 && update.root == muiAccessIdOf(root), "whole again");
    CHECK(muiAccess_Disable(context, other) == mui_empty &&
              muiAccess_Disable(context, root) == mui_success &&
              muiBuildAccessUpdate(context, root, &update) == mui_empty,
          "disabled");
    // A destroyed root's place is taken back.
    CHECK(muiAccess_Enable(context, other) == mui_success &&
              muiDestroyNode(context, other) == mui_success &&
              muiAccess_Enable(context, root) == mui_success &&
              muiBuildAccessUpdate(context, other, &update) == mui_errorStale,
          "a gone root's place");
    CHECK(muiBuildAccessUpdate(context, root, NULL) == mui_errorInvalid &&
              muiBuildAccessUpdate(NULL, root, &update) == mui_errorInvalid,
          "refused");
    muiDestroyContext(context);
}

int main(void)
{
    TestWholeThenChanged();
    TestChildren();
    TestDerived();
    TestScrollAndRange();
    TestVirtualItems();
    TestActions();
    TestScrollActions();
    TestHostData();
    TestRoots();
    return s_failures == 0 ? 0 : 1;
}
