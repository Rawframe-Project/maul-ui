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
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
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

// A password input's value is never the host's own text, which would be
// the password: assistive technology reads only its mask, which the text
// service gives (a bullet a cluster).
static void TestPasswordValue(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    CHECK(muiAccess_Enable(context, scene.root) == mui_success, "enabled");
    Layout(context, scene.root);
    CHECK(muiNode_SetAccessRole(context, scene.label, mui_rolePasswordInput) == mui_success,
          "a password input");
    Text(context, scene.label, mui_accessValue, "hunter2");
    muiAccessUpdate update = Build(context, scene.root);
    CHECK(strstr(Dump(&update), "hunter2") == NULL && strstr(Dump(&update), "t2=") == NULL,
          "its host's value text left out");
    // A label again reads its text.
    CHECK(muiNode_SetAccessRole(context, scene.label, mui_roleLabel) == mui_success, "a label");
    update = Build(context, scene.root);
    CHECK(strstr(Dump(&update), "t2=hunter2") != NULL, "a label's value read");
    muiDestroyContext(context);
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
    // The same text, or role, again: nothing.
    Text(context, scene.label, mui_accessValue, "Hi");
    CHECK(muiNode_SetAccessRole(context, scene.label, mui_roleLabel) == mui_success, "same role");
    update = Build(context, scene.root);
    CHECK(update.nodeCount == 0, "nothing new");
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
    CHECK(muiNode_InsertChild(context, scene.group, added, s_nullNode) == mui_success, "back");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "4 r0 f0 a100 200x50 @0,60 [5 6]\n"
                                "6 r0 f0 a100 10x10 @50,0\n") == 0,
          "back, unchanged");
    // Out and back with a subtree of its own.
    CHECK(muiNode_Detach(context, added) == mui_success, "out again");
    Layout(context, scene.root);
    (void)Build(context, scene.root);
    muiNodeId inner = Node(context, added);
    Size(context, inner, 5.0f, 5.0f);
    CHECK(muiNode_InsertChild(context, scene.group, added, s_nullNode) == mui_success, "back");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(strcmp(Dump(&update), "4 r0 f0 a100 200x50 @0,60 [5 6]\n"
                                "6 r0 f0 a100 10x10 @50,0 [7]\n"
                                "7 r0 f0 a100 5x5 @0,0\n") == 0,
          "back with its subtree");
    // Reordered: the list.
    CHECK(muiNode_Detach(context, added) == mui_success &&
              muiNode_InsertChild(context, scene.group, added, scene.leaf) == mui_success,
          "first");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    CHECK(Sent(&update, scene.group) != NULL &&
              update.children[Sent(&update, scene.group)->firstChild] == muiAccessIdOf(added),
          "reordered");
    // Moved to another parent: both lists, and the subtree it carries.
    CHECK(muiNode_Detach(context, added) == mui_success &&
              muiNode_InsertChild(context, scene.root, added, s_nullNode) == mui_success,
          "moved");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    // The leaf, which the reorder put second, moves back to the start.
    CHECK(strcmp(Dump(&update), "1 r0 f0 a0 200x120 @0,0 [2 3 4 6]\n"
                                "4 r0 f0 a100 200x50 @0,60 [5]\n"
                                "5 r0 f0 a100 50x50 @0,0\n"
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
    // Destroyed and its slot taken, at its place, between updates: the
    // list names the new node.
    CHECK(muiDestroyNode(context, reused) == mui_success, "destroyed");
    muiNodeId again = Node(context, scene.root);
    Size(context, again, 10.0f, 10.0f);
    CHECK(again.index1 == reused.index1, "the same slot");
    Layout(context, scene.root);
    update = Build(context, scene.root);
    const muiAccessNode* root = Sent(&update, scene.root);
    CHECK(root != NULL &&
              update.children[root->firstChild + root->childCount - 1] == muiAccessIdOf(again),
          "the new node listed");
    muiDestroyContext(context);
}

static void TestRelationsAndValues(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    CHECK(muiAccess_Enable(context, scene.root) == mui_success, "enabled");
    Layout(context, scene.root);
    (void)Build(context, scene.root);
    // Two relations, in order of kind whatever the order set.
    const muiNodeId described[2] = {scene.label, scene.group};
    CHECK(muiNode_SetAccessRelation(context, scene.button, mui_relationDescribedBy, described, 2) ==
                  mui_success &&
              muiNode_SetAccessRelation(context, scene.button, mui_relationLabelledBy, &scene.label,
                                        1) == mui_success,
          "named");
    muiAccessUpdate update = Build(context, scene.root);
    const muiAccessNode* node = Sent(&update, scene.button);
    CHECK(update.nodeCount == 1 && node != NULL && node->linkCount == 3 &&
              node->links[0].kind == mui_relationLabelledBy &&
              node->links[0].target == muiAccessIdOf(scene.label) &&
              node->links[1].kind == mui_relationDescribedBy &&
              node->links[1].target == muiAccessIdOf(scene.label) &&
              node->links[2].target == muiAccessIdOf(scene.group),
          "links");
    muiNodeId read[2];
    uint32_t count = 0;
    CHECK(muiNode_GetAccessRelation(context, scene.button, mui_relationDescribedBy, NULL, 0,
                                    &count) == mui_errorCapacity &&
              count == 2 &&
              muiNode_GetAccessRelation(context, scene.button, mui_relationDescribedBy, read, 2,
                                        &count) == mui_success &&
              read[1].index1 == scene.group.index1 &&
              muiNode_GetAccessRelation(context, scene.leaf, mui_relationControls, read, 2,
                                        &count) == mui_success &&
              count == 0,
          "read back");
    // The same again: nothing.
    CHECK(muiNode_SetAccessRelation(context, scene.button, mui_relationDescribedBy, described, 2) ==
              mui_success,
          "again");
    update = Build(context, scene.root);
    CHECK(update.nodeCount == 0, "the same");
    // Replaced, then cleared.
    CHECK(muiNode_SetAccessRelation(context, scene.button, mui_relationDescribedBy, &scene.group,
                                    1) == mui_success &&
              muiNode_SetAccessRelation(context, scene.button, mui_relationLabelledBy, NULL, 0) ==
                  mui_success,
          "replaced and cleared");
    update = Build(context, scene.root);
    node = Sent(&update, scene.button);
    CHECK(node != NULL && node->linkCount == 1 && node->links[0].kind == mui_relationDescribedBy &&
              node->links[0].target == muiAccessIdOf(scene.group),
          "one left");
    // Another node, as many: a change.
    CHECK(muiNode_SetAccessRelation(context, scene.button, mui_relationDescribedBy, &scene.leaf,
                                    1) == mui_success,
          "another");
    update = Build(context, scene.root);
    node = Sent(&update, scene.button);
    CHECK(node != NULL && node->links[0].target == muiAccessIdOf(scene.leaf), "the leaf");
    // Values.
    muiAccessValues values = muiDefaultAccessValues();
    CHECK(muiNode_GetAccessValues(context, scene.leaf, &values) == mui_success &&
              values.level == 0 && values.live == mui_liveOff,
          "none");
    values.level = 2;
    values.rowCount = 10;
    values.columnIndex = 3;
    values.live = mui_livePolite;
    values.popup = mui_popupMenu;
    values.sort = mui_sortDescending;
    values.invalid = mui_invalidSpelling;
    values.current = mui_currentPage;
    CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_success, "values");
    update = Build(context, scene.root);
    node = Sent(&update, scene.label);
    CHECK(update.nodeCount == 1 && node != NULL && node->values.level == 2 &&
              node->values.rowCount == 10 && node->values.columnIndex == 3 &&
              node->values.live == mui_livePolite && node->values.popup == mui_popupMenu &&
              node->values.sort == mui_sortDescending &&
              node->values.invalid == mui_invalidSpelling &&
              node->values.current == mui_currentPage,
          "values sent");
    CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_success, "again");
    update = Build(context, scene.root);
    CHECK(update.nodeCount == 0, "the same values");
    // Each value changed alone is a change.
    for (int field = 0; field < 15; field++)
    {
        muiAccessValues next = values;
        uint32_t* counts[9] = {&next.level,       &next.setPosition, &next.setSize,
                               &next.rowCount,    &next.columnCount, &next.rowIndex,
                               &next.columnIndex, &next.rowSpan,     &next.columnSpan};
        uint8_t* kinds[6] = {&next.live, &next.popup,   &next.orientation,
                             &next.sort, &next.invalid, &next.current};
        if (field < 9)
        {
            *counts[field] += 1;
        }
        else
        {
            *kinds[field - 9] = *kinds[field - 9] == 0 ? 1 : 0;
        }
        CHECK(muiNode_SetAccessValues(context, scene.label, &next) == mui_success, "one value");
        update = Build(context, scene.root);
        CHECK(update.nodeCount == 1, "one value changed");
        CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_success, "back");
        (void)Build(context, scene.root);
    }
    // Misuse.
    uint64_t misuse = muiGetContextMisuse(context);
    const muiNodeId none = s_nullNode;
    CHECK(muiNode_SetAccessRelation(context, scene.button, MUI_ACCESS_RELATIONS, &scene.label, 1) ==
                  mui_errorInvalid &&
              muiNode_SetAccessRelation(context, scene.button, mui_relationControls, NULL, 1) ==
                  mui_errorInvalid &&
              muiNode_SetAccessRelation(context, scene.button, mui_relationControls, &none, 1) ==
                  mui_errorInvalid &&
              muiNode_SetAccessRelation(context, scene.button, mui_relationControls, described,
                                        UINT16_MAX + 1) == mui_errorInvalid,
          "relations refused");
    values = muiDefaultAccessValues();
    values.live = mui_liveAssertive + 1;
    CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_errorInvalid, "live");
    values = muiDefaultAccessValues();
    values.popup = mui_popupDialog + 1;
    CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_errorInvalid, "popup");
    values = muiDefaultAccessValues();
    values.orientation = mui_orientationVertical + 1;
    CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_errorInvalid,
          "orientation");
    values = muiDefaultAccessValues();
    values.sort = mui_sortOther + 1;
    CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_errorInvalid, "sort");
    values = muiDefaultAccessValues();
    values.invalid = mui_invalidSpelling + 1;
    CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_errorInvalid, "invalid");
    values = muiDefaultAccessValues();
    values.current = mui_currentTime + 1;
    CHECK(muiNode_SetAccessValues(context, scene.label, &values) == mui_errorInvalid &&
              muiNode_SetAccessValues(context, scene.label, NULL) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 11,
          "values refused");
    // A gone target.
    muiNodeId gone = Node(context, s_nullNode);
    CHECK(muiDestroyNode(context, gone) == mui_success &&
              muiNode_SetAccessRelation(context, scene.button, mui_relationControls, &gone, 1) ==
                  mui_errorStale &&
              muiNode_GetAccessRelation(context, gone, mui_relationControls, read, 2, &count) ==
                  mui_errorStale &&
              muiNode_GetAccessValues(context, gone, &values) == mui_errorStale,
          "gone");
    muiDestroyContext(context);
}

// Host content's text by key: key 1 reads "One", key 2 whatever s_read
// says, key 3 ill-formed text, key 5 a text it says it did not read,
// key 6 an empty one; others nothing.
static const char* s_read = "Two";
static int s_reads;
static muiContext* s_reader;
static muiResult s_editFromReader;
static muiResult s_disableFromReader;
static muiResult s_functionFromReader;

static bool ReadText(void* user, muiNodeId nodeId, uint64_t hostKey, bool boundaries,
                     muiAccessContent* contentOut)
{
    (void)boundaries;
    (void)user;
    s_reads++;
    s_editFromReader = muiNode_SetAccessRole(s_reader, nodeId, mui_roleButton);
    s_disableFromReader = muiAccess_Disable(s_reader, nodeId);
    s_functionFromReader = muiSetAccessTextFunction(s_reader, NULL, NULL);
    const char* text = hostKey == 1   ? "One"
                       : hostKey == 2 ? s_read
                       : hostKey == 3 ? "\xC0\x80"
                       : hostKey == 5 ? "Five"
                       : hostKey == 6 ? ""
                                      : NULL;
    if (text == NULL)
    {
        return false;
    }
    contentOut->text = text;
    contentOut->length = strlen(text);
    return hostKey != 5;
}

static muiNodeId Content(muiContext* context, muiNodeId parent, uint64_t key)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = key;
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success &&
              muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success,
          "node");
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, node, &style, MUI_PROPERTY_BIT(mui_propertyContent)) ==
              mui_success,
          "content");
    return node;
}

static void TestContentText(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    s_reader = context;
    muiNodeId root = Node(context, s_nullNode);
    muiNodeId one = Content(context, root, 1);
    muiNodeId two = Content(context, root, 2);
    muiNodeId bad = Content(context, root, 3);
    muiNodeId none = Content(context, root, 4);
    muiNodeId own = Content(context, root, 1);
    muiNodeId unread = Content(context, root, 5);
    muiNodeId empty = Content(context, root, 6);
    muiNodeId plain = Node(context, root);
    CHECK(muiNode_SetAccessRole(context, own, mui_roleHeading) == mui_success, "a heading");
    Text(context, two, mui_accessLabel, "Second");
    CHECK(muiSetAccessTextFunction(context, ReadText, NULL) == mui_success &&
              muiAccess_Enable(context, root) == mui_success,
          "reading");
    muiAccessUpdate update = Build(context, root);
    CHECK(Sent(&update, one)->role == mui_roleLabel &&
              strcmp(Sent(&update, one)->text[mui_accessValue], "One") == 0 &&
              Sent(&update, own)->role == mui_roleHeading &&
              strcmp(Sent(&update, own)->text[mui_accessValue], "One") == 0 &&
              Sent(&update, two)->textLength[mui_accessValue] == 3,
          "read, the host's role kept");
    CHECK(Sent(&update, bad)->role == mui_roleGeneric &&
              Sent(&update, bad)->text[mui_accessValue] == NULL &&
              Sent(&update, none)->text[mui_accessValue] == NULL &&
              Sent(&update, plain)->text[mui_accessValue] == NULL &&
              Sent(&update, unread)->text[mui_accessValue] == NULL &&
              Sent(&update, empty)->text[mui_accessValue] == NULL &&
              Sent(&update, empty)->role == mui_roleGeneric,
          "ill-formed, nothing, not content, not read, empty");
    CHECK(s_editFromReader == mui_errorInvalid && s_disableFromReader == mui_errorInvalid &&
              s_functionFromReader == mui_errorInvalid && s_reads == 10,
          "read once each, again for the boundaries of those sent; editing, disabling and "
          "replacing the reader refused");
    // The host's value wins, unread.
    Text(context, one, mui_accessValue, "Uno");
    s_reads = 0;
    update = Build(context, root);
    CHECK(update.nodeCount == 1 && strcmp(Sent(&update, one)->text[mui_accessValue], "Uno") == 0 &&
              s_reads == 0,
          "the host's value");
    // Content changed: read again, sent when it differs.
    CHECK(muiNode_MarkContentChanged(context, two) == mui_success, "marked");
    update = Build(context, root);
    CHECK(update.nodeCount == 0, "the same text");
    s_read = "Deux";
    CHECK(muiNode_MarkContentChanged(context, two) == mui_success, "marked again");
    update = Build(context, root);
    CHECK(update.nodeCount == 1 && Sent(&update, two)->textLength[mui_accessValue] == 4,
          "a new text");
    // A new function: everything again.
    CHECK(muiSetAccessTextFunction(context, NULL, NULL) == mui_success, "none");
    update = Build(context, root);
    CHECK(update.root == muiAccessIdOf(root) && Sent(&update, two)->text[mui_accessValue] == NULL,
          "whole, unread");
    CHECK(muiSetAccessTextFunction(NULL, NULL, NULL) == mui_errorInvalid, "refused");
    s_read = "Two";
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

static void TestDerivedMore(void)
{
    // One layer at most: the second modal node roots none.
    muiContextDef def = muiDefaultContextDef();
    def.limits.layers = 1;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Node(context, s_nullNode);
    muiNodeId button = Node(context, root);
    muiNodeId first = Node(context, root);
    muiNodeId second = Node(context, root);
    muiNodeId plain = Node(context, root);
    muiNodeId pointer = Node(context, root);
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, button, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable");
    interaction.focusMode = mui_focusPointer;
    CHECK(muiNode_SetInteractionValues(context, pointer, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable by pointer and code");
    CHECK(muiNode_SetAccessFlags(context, plain,
                                 mui_accessClickable | mui_accessExpandable | mui_accessExpanded) ==
              mui_success,
          "clickable, expanded");
    CHECK(muiAccess_Enable(context, root) == mui_success, "enabled");
    Layout(context, root);
    muiAccessUpdate update = Build(context, root);
    CHECK((Sent(&update, button)->flags & mui_accessFocusable) != 0 &&
              (Sent(&update, pointer)->flags & mui_accessFocusable) != 0,
          "focusable either way");
    CHECK(Sent(&update, plain)->actions == (Action(mui_actionClick) | Action(mui_actionCollapse) |
                                            Action(mui_actionScrollIntoView)),
          "clicked by flag; collapsed, not expanded");
    interaction.layer = mui_layerModal;
    CHECK(muiNode_SetInteractionValues(context, first, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success &&
              muiNode_SetInteractionValues(context, second, &interaction,
                                           MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "two modal");
    Layout(context, root);
    update = Build(context, root);
    // The second roots no layer: nothing changed for it.
    CHECK((Sent(&update, first)->flags & mui_accessModal) != 0 && Sent(&update, second) == NULL,
          "one layer, one modal");
    // The modal layer covers the button: no focus for it.
    CHECK((Sent(&update, button)->flags & mui_accessFocusable) == 0 &&
              (Sent(&update, button)->actions & Action(mui_actionFocus)) == 0,
          "covered");
    // The layer no longer modal, then modal and exiting: the button is
    // focusable again each time.
    interaction.layer = mui_layerActivation;
    CHECK(muiNode_SetInteractionValues(context, first, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "not modal");
    Layout(context, root);
    update = Build(context, root);
    CHECK(Sent(&update, button) != NULL &&
              (Sent(&update, button)->flags & mui_accessFocusable) != 0,
          "uncovered by its kind");
    interaction.layer = mui_layerModal;
    CHECK(muiNode_SetInteractionValues(context, first, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "modal again");
    Layout(context, root);
    (void)Build(context, root);
    CHECK(muiNode_BeginExit(context, first) == mui_success, "exits");
    Layout(context, root);
    update = Build(context, root);
    CHECK(Sent(&update, button) != NULL &&
              (Sent(&update, button)->flags & mui_accessFocusable) != 0,
          "uncovered by its exit");
    // A focus in another tree: the root has it.
    muiNodeId elsewhere = Node(context, s_nullNode);
    interaction.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, elsewhere, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable elsewhere");
    Layout(context, elsewhere);
    CHECK(muiFocus_Set(context, 0, elsewhere, mui_focusByCode) == mui_success, "focus away");
    update = Build(context, root);
    CHECK(update.focus == muiAccessIdOf(root), "the focus not in this tree");
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
              node->step == 1.0f && node->values.orientation == mui_orientationHorizontal,
          "a range, horizontal");
    // The host's orientation wins.
    muiAccessValues vertical = muiDefaultAccessValues();
    vertical.orientation = mui_orientationVertical;
    CHECK(muiNode_SetAccessValues(context, slider, &vertical) == mui_success, "vertical");
    update = Build(context, root);
    CHECK(Sent(&update, slider)->values.orientation == mui_orientationVertical, "the host's");
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

static void TestPositionAlone(void)
{
    // An estimated list: an item measured 0 high above a bound item.
    // Removing it moves the item's position, not its place.
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Node(context, s_nullNode);
    muiNodeId list = Node(context, root);
    Column(context, list, true);
    Size(context, list, 200.0f, 100.0f);
    muiVirtualList virtualList = muiDefaultVirtualList();
    virtualList.count = 10;
    virtualList.overscan = 0.0f;
    CHECK(muiNode_SetVirtualList(context, list, &virtualList) == mui_success, "list");
    muiNodeId empty = Node(context, list);
    Size(context, empty, 200.0f, 0.0f);
    muiNodeId item = Node(context, list);
    Size(context, item, 200.0f, 40.0f);
    CHECK(muiNode_SetItem(context, empty, 0) == mui_success &&
              muiNode_SetItem(context, item, 1) == mui_success,
          "bound");
    CHECK(muiAccess_Enable(context, root) == mui_success, "enabled");
    Layout(context, root);
    Layout(context, root);
    muiAccessUpdate update = Build(context, root);
    CHECK(Sent(&update, item)->values.setPosition == 2 && Sent(&update, item)->transform.f == 0.0f,
          "second, at the top");
    // One taken out above, one put in at the end: the same count.
    CHECK(muiNode_RemoveVirtualItems(context, list, 0, 1) == mui_success &&
              muiNode_InsertVirtualItems(context, list, 9, 1) == mui_success,
          "removed above, added below");
    Layout(context, root);
    update = Build(context, root);
    const muiAccessNode* node = Sent(&update, item);
    CHECK(node != NULL && node->values.setPosition == 1 && node->transform.f == 0.0f,
          "first, where it was");
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
    // Bound out of order, after two nodes of no item.
    muiNodeId other = Node(context, list);
    muiNodeId another = Node(context, list);
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
    CHECK(node != NULL && node->childCount == 5 &&
              update.children[node->firstChild] == muiAccessIdOf(other) &&
              update.children[node->firstChild + 1] == muiAccessIdOf(another) &&
              update.children[node->firstChild + 2] == muiAccessIdOf(items[1]) &&
              update.children[node->firstChild + 3] == muiAccessIdOf(items[2]) &&
              update.children[node->firstChild + 4] == muiAccessIdOf(items[0]),
          "read by item");
    node = Sent(&update, items[0]);
    CHECK(node != NULL && node->values.setPosition == 3 && node->values.setSize == 1000,
          "third of 1000");
    CHECK(Sent(&update, other)->values.setPosition == 0, "no item");
    // The host's position wins.
    muiAccessValues values = muiDefaultAccessValues();
    values.setPosition = 7;
    CHECK(muiNode_SetAccessValues(context, items[2], &values) == mui_success, "seventh");
    update = Build(context, root);
    CHECK(Sent(&update, items[2])->values.setPosition == 7 &&
              Sent(&update, items[2])->values.setSize == 0,
          "the host's position");
    // Rebound: the list's order and the item.
    CHECK(muiNode_SetItem(context, items[0], 5) == mui_success, "rebound");
    Layout(context, root);
    update = Build(context, root);
    CHECK(Sent(&update, items[0]) != NULL && Sent(&update, items[0])->values.setPosition == 6,
          "sixth");
    // Fewer items than an index bound: that node has no place, and is
    // read with the unbound.
    virtualList.count = 3;
    CHECK(muiNode_SetVirtualList(context, list, &virtualList) == mui_success, "three");
    Layout(context, root);
    update = Build(context, root);
    node = Sent(&update, list);
    CHECK(Sent(&update, items[0]) != NULL && Sent(&update, items[0])->values.setPosition == 0 &&
              node != NULL && update.children[node->firstChild + 2] == muiAccessIdOf(items[0]),
          "past the count");
    // A list whose first two children are bound the wrong way round.
    muiNodeId pair = Node(context, root);
    Column(context, pair, true);
    Size(context, pair, 200.0f, 100.0f);
    virtualList.count = 10;
    CHECK(muiNode_SetVirtualList(context, pair, &virtualList) == mui_success, "a second list");
    muiNodeId second = Node(context, pair);
    muiNodeId first = Node(context, pair);
    CHECK(muiNode_SetItem(context, second, 1) == mui_success &&
              muiNode_SetItem(context, first, 0) == mui_success,
          "bound 1, then 0");
    Layout(context, root);
    update = Build(context, root);
    node = Sent(&update, pair);
    CHECK(node != NULL && node->childCount == 2 &&
              update.children[node->firstChild] == muiAccessIdOf(first) &&
              update.children[node->firstChild + 1] == muiAccessIdOf(second),
          "the first two read by item");
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
    const muiAccessRequest request = {.action = mui_actionClick,
                                      .target = muiAccessIdOf(event->target)};
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
    muiAccessRequest request = {.action = mui_actionClick, .target = muiAccessIdOf(scene.button)};
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
    request = (muiAccessRequest){.action = mui_actionFocus, .target = muiAccessIdOf(scene.button)};
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
    request =
        (muiAccessRequest){.action = mui_actionIncrement, .target = muiAccessIdOf(scene.leaf)};
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
    request = (muiAccessRequest){.action = mui_actionExpand, .target = muiAccessIdOf(scene.group)};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNextNotification(context, &record) == mui_success &&
              record.kind == mui_notificationAccessAction &&
              record.node.index1 == scene.group.index1 && record.count == mui_actionExpand,
          "expand posted");
    request.action = mui_actionCollapse;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_empty, "not expanded");
    // Misuse.
    uint64_t misuse = muiGetContextMisuse(context);
    request = (muiAccessRequest){.action = mui_actionReplaceText + 1,
                                 .target = muiAccessIdOf(scene.group)};
    CHECK(muiPerformAccessAction(context, &request, NULL) == mui_errorInvalid, "unknown");
    request = (muiAccessRequest){
        .action = mui_actionSetValue, .target = muiAccessIdOf(scene.leaf), .value = INFINITY};
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
    muiAccessRequest request = {.action = mui_actionScrollDown, .target = muiAccessIdOf(pane)};
    bool handled = false;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, pane, &x, &y) == mui_success && y > 0.0f,
          "a page down");
    request.action = mui_actionScrollLeft;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_empty, "not across");
    request = (muiAccessRequest){
        .action = mui_actionSetScrollOffset, .target = muiAccessIdOf(pane), .y = 1000.0f};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, pane, &x, &y) == mui_success && y == 200.0f,
          "to the end");
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && !handled,
          "there already");
    request = (muiAccessRequest){.action = mui_actionScrollUp, .target = muiAccessIdOf(pane)};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, pane, &x, &y) == mui_success && y < 200.0f,
          "a page up");
    request =
        (muiAccessRequest){.action = mui_actionScrollIntoView, .target = muiAccessIdOf(rows[0])};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, pane, &x, &y) == mui_success && y == 0.0f,
          "into view");
    // Across, right to left: the offset grows toward the left.
    muiNodeId across = Node(context, root);
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.scrollAxes = mui_scrollHorizontal;
    style.textDirection = mui_textRightToLeft;
    CHECK(muiNode_SetLayoutValues(context, across, &style,
                                  MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                                      MUI_PROPERTY_BIT(mui_propertyTextDirection)) == mui_success,
          "across");
    Size(context, across, 100.0f, 50.0f);
    muiNodeId cells[3];
    for (int i = 0; i < 3; i++)
    {
        cells[i] = Node(context, across);
        Size(context, cells[i], 80.0f, 50.0f);
    }
    CHECK(muiAccess_Enable(context, root) == mui_success, "enabled");
    Layout(context, root);
    (void)Build(context, root);
    request = (muiAccessRequest){.action = mui_actionScrollLeft, .target = muiAccessIdOf(across)};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, across, &x, &y) == mui_success && x > 0.0f,
          "a page left, right to left");
    Layout(context, root);
    muiAccessUpdate update = Build(context, root);
    const muiAccessNode* cell = Sent(&update, cells[0]);
    muiRect rect = muiNode_GetRect(context, cells[0]);
    CHECK(cell != NULL && cell->transform.e == rect.x + x, "moved right by the offset");
    request.action = mui_actionScrollRight;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_success && handled &&
              muiNode_GetScroll(context, across, &x, &y) == mui_success && x == 0.0f,
          "a page right");
    muiDestroyContext(context);
}

static bool TransformIs(const muiAccessNode* node, muiDrawTransform t)
{
    return node != NULL && node->transform.a == t.a && node->transform.b == t.b &&
           node->transform.c == t.c && node->transform.d == t.d && node->transform.e == t.e &&
           node->transform.f == t.f;
}

// A local scale reaches a node's transform about its origin, and a change
// of it is sent (record mui-0005).
static void TestScale(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Node(context, s_nullNode);
    Size(context, root, 200.0f, 100.0f);
    muiNodeId panel = Node(context, root);
    Size(context, panel, 100.0f, 40.0f);
    muiNodeId inner = Node(context, panel);
    Size(context, inner, 20.0f, 10.0f);
    CHECK(muiAccess_Enable(context, root) == mui_success, "enabled");
    Layout(context, root);
    muiAccessUpdate update = Build(context, root);
    CHECK(TransformIs(Sent(&update, panel), (muiDrawTransform){1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}),
          "unscaled: a translation");
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.scale = (muiLocalScale){0.5f, 0.25f, 0.5f, 0.5f};
    CHECK(muiNode_SetVisualValues(context, panel, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyScaleX) |
                                      MUI_PROPERTY_BIT(mui_propertyScaleY)) == mui_success,
          "scaled");
    Layout(context, root);
    update = Build(context, root);
    CHECK(TransformIs(Sent(&update, panel),
                      (muiDrawTransform){0.5f, 0.0f, 0.0f, 0.25f, 25.0f, 15.0f}),
          "about its centre (50, 20)");
    CHECK(Sent(&update, inner) == NULL, "its child unchanged, in its own space");
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
    CHECK(muiNode_SetAccessText(context, c, mui_accessLabel, NULL, 0) == mui_success &&
              muiNode_SetAccessRelation(context, c, mui_relationControls, NULL, 0) == mui_success,
          "cleared");
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
    static const char* const s_bad[] = {
        "\xC0\x80",         "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82",    "\x80",
        "\xF5\x80\x80\x80", "\xE0\x80\x80", "\xF0\x80\x80\x80", "\xE2\x82\x41"};
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
              muiGetContextMisuse(context) == misuse + 14,
          "refused");
    // A sequence cut short by the length, its rest in memory after it.
    CHECK(muiNode_SetAccessText(context, a, mui_accessValue, "\xE2\x82\xAC", 2) == mui_errorInvalid,
          "cut short");
    // A node in a destroyed node's slot has none of its data.
    CHECK(muiNode_SetAccessRole(context, c, mui_roleLink) == mui_success &&
              muiDestroyNode(context, c) == mui_success,
          "c gone");
    muiNodeId d = Node(context, s_nullNode);
    CHECK(d.index1 == c.index1 && muiNode_GetAccessRole(context, d, &role) == mui_success &&
              role == mui_roleGeneric,
          "not c's role");
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

typedef struct Counter
{
    size_t live;
    bool fail;
} Counter;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    Counter* counter = context;
    if (counter->fail || alignment > alignof(max_align_t))
    {
        return NULL;
    }
    counter->live += size;
    return malloc(size);
}

static void Release(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    Counter* counter = context;
    counter->live -= size;
    free(memory);
}

static void TestMemory(void)
{
    Counter counter = {0};
    muiContextDef def = muiDefaultContextDef();
    def.allocator = (muiAllocator){Allocate, Release, &counter};
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    size_t created = counter.live;
    muiNodeId root = Node(context, s_nullNode);
    // Memory running out: nothing enabled, no text taken.
    counter.fail = true;
    muiAccessUpdate update;
    CHECK(muiAccess_Enable(context, root) == mui_errorCapacity &&
              muiBuildAccessUpdate(context, root, &update) == mui_empty &&
              muiNode_SetAccessText(context, root, mui_accessLabel, "x", 1) == mui_errorCapacity,
          "out of memory");
    counter.fail = false;
    CHECK(muiAccess_Enable(context, root) == mui_success && counter.live > created,
          "the copies allocated");
    CHECK(muiAccess_Disable(context, root) == mui_success && counter.live == created, "and freed");
    Text(context, root, mui_accessLabel, "Root");
    CHECK(counter.live == created + 5, "a text and its NUL");
    muiNodeId targets[2] = {root, root};
    CHECK(muiNode_SetAccessRelation(context, root, mui_relationFlowTo, targets, 2) == mui_success &&
              counter.live == created + 5 + 2 * sizeof(muiAccessLink) &&
              muiNode_SetAccessRelation(context, root, mui_relationFlowTo, targets, 1) ==
                  mui_success &&
              counter.live == created + 5 + sizeof(muiAccessLink),
          "links replaced");
    counter.fail = true;
    CHECK(muiNode_SetAccessRelation(context, root, mui_relationFlowTo, targets, 2) ==
                  mui_errorCapacity &&
              counter.live == created + 5 + sizeof(muiAccessLink),
          "kept when memory runs out");
    counter.fail = false;
    muiDestroyContext(context);
    CHECK(counter.live == 0, "all of it freed");
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

// A full table drops a destroyed node's entry for a new one, the last
// entry moving into its place: every node keeps its own data.
static void TestCompacted(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.accessNodes = 3;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId a = Node(context, s_nullNode);
    muiNodeId b = Node(context, s_nullNode);
    muiNodeId c = Node(context, s_nullNode);
    muiNodeId d = Node(context, s_nullNode);
    Text(context, a, mui_accessLabel, "A");
    Text(context, b, mui_accessLabel, "B");
    Text(context, c, mui_accessLabel, "C");
    CHECK(muiDestroyNode(context, a) == mui_success, "the first gone");
    Text(context, d, mui_accessLabel, "D");
    Text(context, c, mui_accessDescription, "moved");
    const char* text = NULL;
    size_t length = 0;
    bool kept = true;
    const muiNodeId nodes[] = {b, c, d};
    const char* names[] = {"B", "C", "D"};
    for (int i = 0; i < 3; i++)
    {
        kept = kept &&
               muiNode_GetAccessText(context, nodes[i], mui_accessLabel, &text, &length) ==
                   mui_success &&
               strcmp(text, names[i]) == 0;
    }
    CHECK(kept &&
              muiNode_GetAccessText(context, c, mui_accessDescription, &text, &length) ==
                  mui_success &&
              strcmp(text, "moved") == 0 &&
              muiNode_GetAccessText(context, b, mui_accessDescription, &text, &length) == mui_empty,
          "each node's own texts, the moved one edited in place");
    muiDestroyContext(context);
}

// What every call refuses: no context, a destroyed node, no root, and
// what reads asks for no room.
static void TestRefusals(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId gone = Node(context, s_nullNode);
    CHECK(muiDestroyNode(context, gone) == mui_success, "a node gone");
    muiAccessValues values = muiDefaultAccessValues();
    muiNodeId targets[1] = {gone};
    uint32_t count = 0;
    CHECK(muiNode_SetAccessRole(NULL, gone, mui_roleButton) == mui_errorInvalid &&
              muiNode_SetAccessText(NULL, gone, mui_accessLabel, "a", 1) == mui_errorInvalid &&
              muiNode_SetAccessFlags(NULL, gone, 0) == mui_errorInvalid &&
              muiNode_SetAccessRelation(NULL, gone, mui_relationControls, NULL, 0) ==
                  mui_errorInvalid &&
              muiNode_SetAccessValues(NULL, gone, &values) == mui_errorInvalid &&
              muiAccess_Enable(NULL, gone) == mui_errorInvalid &&
              muiAccess_Disable(NULL, gone) == mui_errorInvalid,
          "no context");
    const char* text = NULL;
    size_t length = 0;
    muiAccessFlags flags = 0;
    CHECK(muiNode_SetAccessText(context, gone, mui_accessLabel, "a", 1) == mui_errorStale &&
              muiNode_SetAccessFlags(context, gone, 0) == mui_errorStale &&
              muiNode_SetAccessRelation(context, gone, mui_relationControls, NULL, 0) ==
                  mui_errorStale &&
              muiNode_SetAccessValues(context, gone, &values) == mui_errorStale &&
              muiNode_GetAccessText(context, gone, mui_accessLabel, &text, &length) ==
                  mui_errorStale &&
              muiNode_GetAccessFlags(context, gone, &flags) == mui_errorStale,
          "a destroyed node");
    muiNodeId node = Node(context, s_nullNode);
    CHECK(muiAccess_Disable(context, s_nullNode) == mui_errorInvalid &&
              muiNode_GetAccessRelation(context, node, mui_relationControls, targets, 1, NULL) ==
                  mui_errorInvalid &&
              muiNode_GetAccessRelation(context, node, mui_relationControls, NULL, 1, &count) ==
                  mui_errorInvalid &&
              muiNode_GetAccessValues(context, node, NULL) == mui_errorInvalid,
          "no root, nowhere to write");
    muiDestroyContext(context);
}

int main(void)
{
    TestWholeThenChanged();
    TestPasswordValue();
    TestChildren();
    TestRelationsAndValues();
    TestContentText();
    TestDerived();
    TestDerivedMore();
    TestScrollAndRange();
    TestVirtualItems();
    TestPositionAlone();
    TestActions();
    TestScrollActions();
    TestScale();
    TestHostData();
    TestMemory();
    TestRoots();
    TestCompacted();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
