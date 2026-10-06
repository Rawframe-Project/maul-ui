// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's consumer (record mui-0008): applying the
// core's updates, the changes reported, nodes leaving with their
// subtrees, updates refused whole, and the tree written as text.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/context.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const muiNodeId s_nullNode = {0, 0};

static muiNodeId Node(muiContext* context, muiNodeId parent, float width, float height)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.item.shrink = 0.0f;
    style.container.direction = mui_flexColumn;
    style.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    style.sizing.height = (muiDimension){0.0f, height, mui_dimensionValue};
    muiPropertyMask mask =
        MUI_PROPERTY_BIT(mui_propertyShrink) | MUI_PROPERTY_BIT(mui_propertyFlexDirection);
    mask |= width >= 0.0f ? MUI_PROPERTY_BIT(mui_propertyWidth) : 0;
    mask |= height >= 0.0f ? MUI_PROPERTY_BIT(mui_propertyHeight) : 0;
    CHECK(muiNode_SetLayoutValues(context, node, &style, mask) == mui_success, "style");
    return node;
}

static void Text(muiContext* context, muiNodeId node, muiAccessTextKind kind, const char* text)
{
    CHECK(muiNode_SetAccessText(context, node, kind, text, strlen(text)) == mui_success, "text");
}

// What applying reported, as text: "+id", "~id", "-id", "focus a b".
typedef struct Log
{
    char text[1024];
    size_t length;
} Log;

static void Note(Log* log, const char* what, uint64_t a, uint64_t b)
{
    char line[64];
    int count = b == UINT64_MAX
                    ? snprintf(line, sizeof(line), "%s%u ", what, (unsigned)a)
                    : snprintf(line, sizeof(line), "%s%u>%u ", what, (unsigned)a, (unsigned)b);
    if (count > 0 && log->length + (size_t)count < sizeof(log->text))
    {
        memcpy(log->text + log->length, line, (size_t)count + 1);
        log->length += (size_t)count;
    }
}

static void Added(void* user, const muiAccessTree* tree, uint64_t id)
{
    CHECK(muiAccessTree_Find(tree, id) != NULL, "held when told");
    Note(user, "+", id, UINT64_MAX);
}

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    CHECK(muiAccessTree_Find(tree, old->id) != NULL, "held when told");
    Note(user, "~", old->id, UINT64_MAX);
}

static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    CHECK(muiAccessTree_Find(tree, old->id) == NULL, "gone when told");
    Note(user, "-", old->id, UINT64_MAX);
}

static void FocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    CHECK(muiAccessTree_GetFocus(tree) == focus, "the focus");
    Note(user, "focus ", old, focus);
}

// A context and a tree kept from it.
typedef struct Scene
{
    muiContext* context;
    muiAccessTree* tree;
    muiNodeId root;
    Log log;
    muiAccessChanges changes;
} Scene;

static void MakeScene(Scene* scene)
{
    *scene = (Scene){0};
    muiContextDef def = muiDefaultContextDef();
    CHECK(muiCreateContext(&def, &scene->context) == mui_success, "context");
    muiAccessTreeDef treeDef = muiDefaultAccessTreeDef();
    CHECK(muiCreateAccessTree(&treeDef, &scene->tree) == mui_success, "tree");
    scene->root = Node(scene->context, s_nullNode, -1.0f, -1.0f);
    scene->changes = (muiAccessChanges){&scene->log, Added, Updated, Removed, FocusMoved};
}

static void FreeScene(Scene* scene)
{
    muiDestroyAccessTree(scene->tree);
    muiDestroyContext(scene->context);
}

// Lays out, builds and applies; what was reported, then cleared.
static const char* Sync(Scene* scene)
{
    static char s_reported[1024];
    const muiLayoutInput input = {400.0f, 300.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(scene->context, scene->root, &input) == mui_success, "layout");
    muiAccessUpdate update;
    CHECK(muiBuildAccessUpdate(scene->context, scene->root, &update) == mui_success, "build");
    scene->log.length = 0;
    scene->log.text[0] = '\0';
    CHECK(muiAccessTree_Apply(scene->tree, &update, &scene->changes) == mui_success, "applied");
    memcpy(s_reported, scene->log.text, scene->log.length + 1);
    return s_reported;
}

static const char* Written(const muiAccessTree* tree)
{
    static char s_text[2048];
    size_t length = 0;
    CHECK(muiAccessTree_Write(tree, s_text, sizeof(s_text), &length) == mui_success, "written");
    return s_text;
}

static void TestUpdatesApplied(void)
{
    Scene scene;
    MakeScene(&scene);
    muiContext* context = scene.context;
    muiNodeId button = Node(context, scene.root, 100.0f, 40.0f);
    CHECK(muiNode_SetAccessRole(context, button, mui_roleButton) == mui_success, "button");
    Text(context, button, mui_accessLabel, "OK");
    muiNodeId label = Node(context, scene.root, 100.0f, 20.0f);
    CHECK(muiNode_SetAccessRole(context, label, mui_roleLabel) == mui_success, "label");
    Text(context, label, mui_accessValue, "Say \"hi\"\n\\\x7F");
    muiNodeId group = Node(context, scene.root, 200.0f, 50.0f);
    muiNodeId leaf = Node(context, group, 50.0f, 50.0f);
    CHECK(muiAccess_Enable(context, scene.root) == mui_success, "enabled");
    CHECK(strcmp(Sync(&scene), "+1 +2 +3 +4 +5 focus 0>1 ") == 0,
          "all added, the focus on the root");
    CHECK(strcmp(Written(scene.tree),
                 "generic #1 200x110 @0,0\n"
                 "  button #2 actions=257 100x40 @0,0 label=\"OK\"\n"
                 "  label #3 actions=256 100x20 @0,40 value=\"Say \\\"hi\\\"\\x0A\\\\\\x7F\"\n"
                 "  generic #4 actions=256 200x50 @0,60\n"
                 "    generic #5 actions=256 50x50 @0,0\n") == 0,
          "the tree");
    CHECK(muiAccessTree_Count(scene.tree) == 5 &&
              muiAccessTree_GetRoot(scene.tree) == muiAccessIdOf(scene.root) &&
              muiAccessTree_GetFocus(scene.tree) == muiAccessIdOf(scene.root) &&
              muiAccessTree_GetParent(scene.tree, muiAccessIdOf(leaf)) == muiAccessIdOf(group) &&
              muiAccessTree_GetParent(scene.tree, muiAccessIdOf(scene.root)) == 0,
          "read");
    uint32_t count = 0;
    const uint64_t* children =
        muiAccessTree_GetChildren(scene.tree, muiAccessIdOf(scene.root), &count);
    CHECK(count == 3 && children[2] == muiAccessIdOf(group) &&
              muiAccessTree_Find(scene.tree, muiAccessIdOf(group))->firstChild == 0,
          "children, not where the update had them");
    // A text: that node updated; the old record told.
    Text(context, label, mui_accessValue, "Hi");
    CHECK(strcmp(Sync(&scene), "~3 ") == 0 &&
              strcmp(muiAccessTree_Find(scene.tree, muiAccessIdOf(label))->text[mui_accessValue],
                     "Hi") == 0 &&
              muiAccessTree_GetParent(scene.tree, muiAccessIdOf(label)) ==
                  muiAccessIdOf(scene.root),
          "updated, its parent kept");
    // Taken out: its list updated, and it removed.
    CHECK(muiNode_Detach(context, leaf) == mui_success, "detached");
    CHECK(strcmp(Sync(&scene), "~4 -5 ") == 0 && muiAccessTree_Count(scene.tree) == 4, "removed");
    // Back, with a child: both added.
    muiNodeId inner = Node(context, leaf, 10.0f, 10.0f);
    CHECK(muiNode_InsertChild(context, group, leaf, s_nullNode) == mui_success, "back");
    CHECK(strcmp(Sync(&scene), "+5 +6 ~4 ") == 0, "back with its child");
    // Moved: nothing removed, both lists updated.
    CHECK(muiNode_Detach(context, leaf) == mui_success &&
              muiNode_InsertChild(context, scene.root, leaf, s_nullNode) == mui_success,
          "moved");
    (void)Sync(&scene);
    CHECK(muiAccessTree_Count(scene.tree) == 6 &&
              muiAccessTree_GetParent(scene.tree, muiAccessIdOf(leaf)) ==
                  muiAccessIdOf(scene.root) &&
              muiAccessTree_GetParent(scene.tree, muiAccessIdOf(inner)) == muiAccessIdOf(leaf),
          "moved whole");
    // Destroyed with its child: both removed.
    CHECK(muiDestroyNode(context, leaf) == mui_success, "destroyed");
    CHECK(strcmp(Sync(&scene), "~1 -5 -6 ") == 0 && muiAccessTree_Count(scene.tree) == 4,
          "a subtree removed");
    // The focus.
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    CHECK(muiNode_SetInteractionValues(context, button, &interaction,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable");
    (void)Sync(&scene);
    CHECK(muiFocus_Set(context, 0, button, mui_focusByCode) == mui_success, "focused");
    CHECK(strcmp(Sync(&scene), "~2 focus 1>2 ") == 0, "focus moved");
    // Enabled again: everything sent, everything updated, none lost.
    CHECK(muiAccess_Enable(context, scene.root) == mui_success, "again");
    CHECK(strcmp(Sync(&scene), "~1 ~2 ~3 ~4 ") == 0 && muiAccessTree_Count(scene.tree) == 4,
          "whole again");
    // No changes told: applied all the same.
    Text(context, label, mui_accessValue, "Bye");
    muiAccessUpdate update;
    CHECK(muiBuildAccessUpdate(context, scene.root, &update) == mui_success &&
              muiAccessTree_Apply(scene.tree, &update, NULL) == mui_success &&
              muiAccessTree_Find(scene.tree, muiAccessIdOf(label))->textLength[mui_accessValue] ==
                  3,
          "untold");
    FreeScene(&scene);
}

static void TestNewRoot(void)
{
    // A whole update of another root lets the old tree go.
    Scene scene;
    MakeScene(&scene);
    (void)Node(scene.context, scene.root, 10.0f, 10.0f);
    CHECK(muiAccess_Enable(scene.context, scene.root) == mui_success, "enabled");
    (void)Sync(&scene);
    muiNodeId other = Node(scene.context, s_nullNode, 10.0f, 10.0f);
    muiNodeId old = scene.root;
    scene.root = other;
    CHECK(muiAccess_Enable(scene.context, other) == mui_success, "the other");
    CHECK(strcmp(Sync(&scene), "+3 -1 -2 focus 1>3 ") == 0 &&
              muiAccessTree_Count(scene.tree) == 1 &&
              muiAccessTree_GetRoot(scene.tree) == muiAccessIdOf(other) &&
              muiAccessTree_Find(scene.tree, muiAccessIdOf(old)) == NULL,
          "the old tree let go");
    FreeScene(&scene);
}

// Updates made by hand: nodes 1 to 3, 1 listing 2.
static void TestRefused(void)
{
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.nodes = 2;
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    muiAccessNode nodes[3] = {{.id = 1, .childCount = 1}, {.id = 2}, {.id = 3}};
    const muiAccessNode* sent[3] = {&nodes[0], &nodes[1], &nodes[2]};
    const uint64_t children[1] = {2};
    muiAccessUpdate update = {sent, 2, children, 0, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "no root first");
    update.root = 3;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "a root not sent");
    update.root = 1;
    update.focus = 3;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "a focus unknown");
    update.focus = 1;
    update.nodeCount = 1;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "a child unknown");
    update.nodeCount = 3;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorCapacity, "three of two");
    const muiAccessNode* twice[2] = {&nodes[0], &nodes[0]};
    update = (muiAccessUpdate){twice, 2, children, 1, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "sent twice");
    muiAccessNode zero = {.id = 0};
    const muiAccessNode* none[1] = {&zero};
    update = (muiAccessUpdate){none, 1, NULL, 1, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "the id 0");
    update = (muiAccessUpdate){sent, 1, NULL, 1, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "children not given");
    CHECK(muiAccessTree_Count(tree) == 0 && muiAccessTree_GetRoot(tree) == 0, "nothing taken");
    update = (muiAccessUpdate){sent, 2, children, 1, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success && muiAccessTree_Count(tree) == 2,
          "taken");
    CHECK(muiAccessTree_Apply(NULL, &update, NULL) == mui_errorInvalid &&
              muiAccessTree_Apply(tree, NULL, NULL) == mui_errorInvalid,
          "arguments");
    muiDestroyAccessTree(tree);
    muiDestroyAccessTree(NULL);
}

// A hand-made update.
static muiAccessUpdate Update(const muiAccessNode* const* nodes, uint32_t count,
                              const uint64_t* children, uint64_t root, uint64_t focus)
{
    return (muiAccessUpdate){nodes, count, children, root, focus};
}

static void TestHandMade(void)
{
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.nodes = 8;
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    // More nodes than the tree holds, whatever else.
    muiAccessNode many[17];
    const muiAccessNode* manySent[17];
    for (uint32_t i = 0; i < 17; i++)
    {
        many[i] = (muiAccessNode){.id = 100 + i};
        manySent[i] = &many[i];
    }
    muiAccessUpdate update = Update(manySent, 17, NULL, 100, 100);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorCapacity, "too many");
    update = Update(NULL, 1, NULL, 1, 1);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "no nodes");
    muiAccessNode one = {.id = 1};
    muiAccessNode zero = {.id = 0};
    const muiAccessNode* withZero[2] = {&one, &zero};
    update = Update(withZero, 2, NULL, 1, 1);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "the id 0");
    const muiAccessNode* twice[2] = {&one, &one};
    update = Update(twice, 2, NULL, 1, 1);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "sent twice");
    // A length without a text is none.
    muiAccessNode root = {.id = 1, .childCount = 1, .textLength = {5}};
    muiAccessNode child = {.id = 2};
    const muiAccessNode* both[2] = {&root, &child};
    const uint64_t rootsChild[1] = {2};
    update = Update(both, 2, rootsChild, 1, 2);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
              muiAccessTree_Find(tree, 1)->textLength[0] == 0,
          "no text, no length");
    // No focus sent: it stays.
    const muiAccessNode* justChild[1] = {&child};
    update = Update(justChild, 1, NULL, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
              muiAccessTree_GetFocus(tree) == 2,
          "the focus kept");
    // A node sent that nothing lists goes at once.
    muiAccessNode stray = {.id = 9};
    const muiAccessNode* strays[1] = {&stray};
    update = Update(strays, 1, NULL, 0, 0);
    Log log = {0};
    const muiAccessChanges changes = {&log, Added, Updated, Removed, FocusMoved};
    CHECK(muiAccessTree_Apply(tree, &update, &changes) == mui_success &&
              muiAccessTree_Find(tree, 9) == NULL && muiAccessTree_Count(tree) == 2 &&
              strcmp(log.text, "-9 ") == 0,
          "a stray let go, told only as gone");
    // New nodes past the room left, though no more are sent than it
    // holds.
    muiAccessNode wide = {.id = 1, .childCount = 7};
    muiAccessNode news[7];
    const muiAccessNode* wideSent[8] = {&wide};
    uint64_t wideChildren[7];
    for (uint32_t i = 0; i < 7; i++)
    {
        news[i] = (muiAccessNode){.id = 20 + i};
        wideSent[i + 1] = &news[i];
        wideChildren[i] = 20 + i;
    }
    update = Update(wideSent, 8, wideChildren, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorCapacity &&
              muiAccessTree_Count(tree) == 2,
          "no room");
    // Lists that would not leave a tree are refused, nothing taken.
    muiAccessNode top = {.id = 1, .childCount = 2};
    muiAccessNode left = {.id = 3, .firstChild = 2, .childCount = 1};
    muiAccessNode right = {.id = 4, .firstChild = 3, .childCount = 1};
    muiAccessNode shared = {.id = 5};
    const muiAccessNode* four[4] = {&top, &left, &right, &shared};
    const uint64_t lists[4] = {3, 4, 5, 5};
    update = Update(four, 4, lists, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid &&
              muiAccessTree_Count(tree) == 2,
          "a child listed twice");
    right.childCount = 0;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
              muiAccessTree_Count(tree) == 4 && muiAccessTree_Find(tree, 2) == NULL &&
              muiAccessTree_GetFocus(tree) == 1,
          "a tree");
    // Moved from 3 to 4, both sent.
    const muiAccessNode* both34[2] = {&left, &right};
    const uint64_t fives[1] = {5};
    left.childCount = 0;
    right.childCount = 1;
    right.firstChild = 0;
    update = Update(both34, 2, fives, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
              muiAccessTree_GetParent(tree, 5) == 4 && muiAccessTree_Count(tree) == 4,
          "moved");
    // Taken by 3 while 4, not sent, still lists it.
    const muiAccessNode* leftOnly[1] = {&left};
    left.childCount = 1;
    left.firstChild = 0;
    update = Update(leftOnly, 1, fives, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid &&
              muiAccessTree_GetParent(tree, 5) == 4,
          "two parents");
    // The root listed.
    const uint64_t theRoot[1] = {1};
    update = Update(leftOnly, 1, theRoot, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "the root listed");
    // A node under itself: alone, two new ones, or two held ones that
    // the root no longer lists.
    muiAccessNode self = {.id = 8, .childCount = 1};
    const muiAccessNode* selfSent[2] = {&self, &top};
    const uint64_t selfList[3] = {8, 3, 4};
    top.firstChild = 1;
    update = Update(selfSent, 2, selfList, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "under itself");
    muiAccessNode six = {.id = 6, .childCount = 1};
    muiAccessNode seven = {.id = 7, .firstChild = 1, .childCount = 1};
    const muiAccessNode* ring[2] = {&six, &seven};
    const uint64_t ringLists[2] = {7, 6};
    update = Update(ring, 2, ringLists, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "a ring of new nodes");
    muiAccessNode bareTop = {.id = 1};
    muiAccessNode ringLeft = {.id = 3, .childCount = 1};
    muiAccessNode ringRight = {.id = 4, .firstChild = 1, .childCount = 1};
    const muiAccessNode* heldRing[3] = {&bareTop, &ringLeft, &ringRight};
    const uint64_t heldLists[2] = {4, 3};
    update = Update(heldRing, 3, heldLists, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid &&
              muiAccessTree_Count(tree) == 4 && muiAccessTree_GetParent(tree, 3) == 1,
          "a ring of held nodes");
    // A child made the root: no parent, the old root gone.
    muiAccessNode bare = {.id = 1};
    const muiAccessNode* reroot[2] = {&bare, &right};
    right.firstChild = 0;
    update = Update(reroot, 2, fives, 4, 4);
    char text[256];
    size_t length = 0;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
              muiAccessTree_GetRoot(tree) == 4 && muiAccessTree_GetParent(tree, 4) == 0 &&
              muiAccessTree_Find(tree, 1) == NULL &&
              muiAccessTree_Write(tree, text, sizeof(text), &length) == mui_success &&
              strcmp(text, "generic #4 0x0 @0,0\n  generic #5 0x0 @0,0\n") == 0,
          "rerooted");
    muiDestroyAccessTree(tree);
}

static uint64_t Next(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void TestChurn(void)
{
    // A root whose children come and go, with ids anywhere: every child
    // listed is found, every one dropped is not.
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.nodes = 64;
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    uint64_t state = 88172645463325252ULL;
    uint64_t kept[63];
    uint32_t keptCount = 0;
    muiAccessNode nodes[64];
    const muiAccessNode* sent[64];
    uint64_t children[63];
    bool ok = true;
    for (int round = 0; round < 5000 && ok; round++)
    {
        uint64_t dropped[63];
        uint32_t droppedCount = 0;
        uint32_t count = 0;
        for (uint32_t i = 0; i < keptCount; i++)
        {
            if (Next(&state) % 3 == 0)
            {
                dropped[droppedCount++] = kept[i];
            }
            else
            {
                children[count++] = kept[i];
            }
        }
        uint32_t sentCount = 1;
        while (count < 63 && Next(&state) % 4 != 0)
        {
            uint64_t id = Next(&state) | 1;
            children[count++] = id;
            nodes[sentCount] = (muiAccessNode){.id = id};
            sentCount++;
        }
        nodes[0] = (muiAccessNode){.id = 2, .childCount = count};
        for (uint32_t i = 0; i < sentCount; i++)
        {
            sent[i] = &nodes[i];
        }
        muiAccessUpdate update = {sent, sentCount, children, round == 0 ? 2 : 0, 2};
        ok = muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
             muiAccessTree_Count(tree) == count + 1;
        for (uint32_t i = 0; i < count; i++)
        {
            ok = ok && muiAccessTree_Find(tree, children[i]) != NULL;
            kept[i] = children[i];
        }
        for (uint32_t i = 0; i < droppedCount; i++)
        {
            ok = ok && muiAccessTree_Find(tree, dropped[i]) == NULL;
        }
        keptCount = count;
    }
    CHECK(ok, "churned");
    muiDestroyAccessTree(tree);
}

// A hand-made tree, for what platforms see.
typedef struct Built
{
    muiAccessNode nodes[32];
    const muiAccessNode* sent[32];
    uint64_t children[64];
    uint32_t nodeCount;
    uint32_t childCount;
} Built;

static muiAccessNode* Add(Built* built, uint64_t id, muiRole role, muiRect bounds, float x, float y)
{
    muiAccessNode* node = &built->nodes[built->nodeCount];
    *node = (muiAccessNode){
        .id = id, .role = role, .bounds = bounds, .transform = {1.0f, 0.0f, 0.0f, 1.0f, x, y}};
    built->sent[built->nodeCount++] = node;
    return node;
}

static void List(Built* built, muiAccessNode* parent, const uint64_t* ids, uint32_t count)
{
    parent->firstChild = built->childCount;
    parent->childCount = count;
    memcpy(&built->children[built->childCount], ids, count * sizeof(uint64_t));
    built->childCount += count;
}

static void Label(muiAccessNode* node, muiAccessTextKind kind, const char* text)
{
    node->text[kind] = text;
    node->textLength[kind] = (uint32_t)strlen(text);
}

// The window 1 placed at 10,20: 2 generic around a button 3 and a
// labelled group 4 with a label 5 in it; 6 hidden with 7 in it; a list 8
// clipping rows 9 to 16, 20 high from -60 to 80, in a port 50 high; a
// button 17 named by what is inside it, doubled in size; a checkbox 22
// labelled by 5 and 3, turned a quarter.
static void BuildScene(Built* built)
{
    *built = (Built){0};
    const muiRect none = {0.0f, 0.0f, 0.0f, 0.0f};
    muiAccessNode* window = Add(built, 1, mui_roleWindow, (muiRect){0, 0, 400, 300}, 10, 20);
    muiAccessNode* generic = Add(built, 2, mui_roleGeneric, none, 0, 0);
    Label(Add(built, 3, mui_roleButton, none, 0, 0), mui_accessLabel, "OK");
    muiAccessNode* group = Add(built, 4, mui_roleGeneric, none, 0, 0);
    Label(group, mui_accessLabel, "Group");
    Label(Add(built, 5, mui_roleLabel, none, 0, 0), mui_accessValue, "Inner");
    muiAccessNode* hidden = Add(built, 6, mui_roleGroup, none, 0, 0);
    hidden->flags = mui_accessHidden;
    (void)Add(built, 7, mui_roleButton, none, 0, 0);
    muiAccessNode* list = Add(built, 8, mui_roleList, (muiRect){0, 0, 100, 50}, 0, 100);
    list->flags = mui_accessClipsChildren | mui_accessScrolls;
    uint64_t rows[8];
    for (uint32_t i = 0; i < 8; i++)
    {
        rows[i] = 9 + i;
        (void)Add(built, 9 + i, mui_roleListItem, (muiRect){0, 0, 100, 20}, 0,
                  -60.0f + 20.0f * (float)i);
    }
    muiAccessNode* save = Add(built, 17, mui_roleButton, (muiRect){0, 0, 30, 10}, 200, 0);
    save->transform.a = 2.0f;
    save->transform.d = 2.0f;
    Label(Add(built, 18, mui_roleImage, none, 0, 0), mui_accessLabel, "Save");
    muiAccessNode* secret = Add(built, 19, mui_roleLabel, none, 0, 0);
    secret->flags = mui_accessHidden;
    Label(secret, mui_accessValue, "secret");
    muiAccessNode* around = Add(built, 20, mui_roleGeneric, none, 0, 0);
    Label(Add(built, 21, mui_roleLabel, none, 0, 0), mui_accessValue, "file");
    muiAccessNode* check = Add(built, 22, mui_roleCheckBox, (muiRect){0, 0, 40, 10}, 300, 0);
    check->transform = (muiDrawTransform){0.0f, 1.0f, -1.0f, 0.0f, 300.0f, 0.0f};
    static const muiAccessLink s_labellers[3] = {
        {5, mui_relationLabelledBy}, {7, mui_relationDescribedBy}, {3, mui_relationLabelledBy}};
    check->links = s_labellers;
    check->linkCount = 3;
    List(built, window, (const uint64_t[]){2, 6, 8, 17, 22}, 5);
    List(built, generic, (const uint64_t[]){3, 4}, 2);
    List(built, group, (const uint64_t[]){5}, 1);
    List(built, hidden, (const uint64_t[]){7}, 1);
    List(built, list, rows, 8);
    List(built, save, (const uint64_t[]){18, 19, 20}, 3);
    List(built, around, (const uint64_t[]){21}, 1);
}

static bool ShownAre(const muiAccessTree* tree, uint64_t id, const uint64_t* expected,
                     uint32_t count)
{
    uint64_t shown[16];
    uint32_t got = 0;
    return muiAccessTree_GetShownChildren(tree, id, shown, 16, &got) == mui_success &&
           got == count && memcmp(shown, expected, count * sizeof(uint64_t)) == 0;
}

static void Focus(muiAccessTree* tree, uint64_t id)
{
    muiAccessUpdate update = {NULL, 0, NULL, 0, id};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "focused");
}

static void TestShown(void)
{
    static Built s_built;
    BuildScene(&s_built);
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    muiAccessUpdate update = {s_built.sent, s_built.nodeCount, s_built.children, 1, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "built");
    // The generic 2 flattened, the hidden 6 out, the labelled 4 kept;
    // rows wholly out of the port out but the first past each edge.
    CHECK(ShownAre(tree, 1, (const uint64_t[]){3, 4, 8, 17, 22}, 5) &&
              ShownAre(tree, 4, (const uint64_t[]){5}, 1) &&
              ShownAre(tree, 8, (const uint64_t[]){11, 12, 13, 14, 15}, 5) &&
              ShownAre(tree, 17, (const uint64_t[]){18, 21}, 2) &&
              ShownAre(tree, 6, (const uint64_t[]){7}, 1),
          "shown children");
    CHECK(muiAccessTree_IsShown(tree, 1) && !muiAccessTree_IsShown(tree, 2) &&
              muiAccessTree_IsShown(tree, 3) && muiAccessTree_IsShown(tree, 5) &&
              !muiAccessTree_IsShown(tree, 6) && !muiAccessTree_IsShown(tree, 7) &&
              !muiAccessTree_IsShown(tree, 9) && !muiAccessTree_IsShown(tree, 10) &&
              muiAccessTree_IsShown(tree, 11) && muiAccessTree_IsShown(tree, 15) &&
              !muiAccessTree_IsShown(tree, 16) && muiAccessTree_IsShown(tree, 21) &&
              !muiAccessTree_IsShown(tree, 20) && !muiAccessTree_IsShown(tree, 99) &&
              !muiAccessTree_IsShown(NULL, 1),
          "shown");
    CHECK(muiAccessTree_GetShownParent(tree, 3) == 1 &&
              muiAccessTree_GetShownParent(tree, 5) == 4 &&
              muiAccessTree_GetShownParent(tree, 7) == 1 &&
              muiAccessTree_GetShownParent(tree, 21) == 17 &&
              muiAccessTree_GetShownParent(tree, 12) == 8 &&
              muiAccessTree_GetShownParent(tree, 1) == 0 &&
              muiAccessTree_GetShownParent(tree, 99) == 0 &&
              muiAccessTree_GetShownParent(NULL, 3) == 0,
          "shown parents");
    // The focus is never left out for itself.
    Focus(tree, 10);
    CHECK(ShownAre(tree, 8, (const uint64_t[]){10, 11, 12, 13, 14, 15}, 6) &&
              muiAccessTree_IsShown(tree, 10) && !muiAccessTree_IsShown(tree, 9),
          "a clipped focus");
    Focus(tree, 2);
    CHECK(ShownAre(tree, 1, (const uint64_t[]){2, 8, 17, 22}, 4) &&
              muiAccessTree_IsShown(tree, 2) && muiAccessTree_GetShownParent(tree, 3) == 2,
          "a generic focus");
    Focus(tree, 6);
    CHECK(ShownAre(tree, 1, (const uint64_t[]){3, 4, 6, 8, 17, 22}, 6) &&
              muiAccessTree_IsShown(tree, 6) && muiAccessTree_IsShown(tree, 7),
          "a hidden focus, shown with what is in it");
    Focus(tree, 7);
    CHECK(!muiAccessTree_IsShown(tree, 7), "a focus a hidden node holds");
    // Room for some.
    uint64_t two[2];
    uint32_t count = 0;
    CHECK(muiAccessTree_GetShownChildren(tree, 1, two, 2, &count) == mui_errorCapacity &&
              count == 5 && two[0] == 3 && two[1] == 4 &&
              muiAccessTree_GetShownChildren(tree, 1, NULL, 0, &count) == mui_errorCapacity &&
              count == 5 && muiAccessTree_GetShownChildren(tree, 99, two, 2, &count) == mui_empty &&
              count == 0 &&
              muiAccessTree_GetShownChildren(tree, 5, two, 2, &count) == mui_success && count == 0,
          "room");
    CHECK(muiAccessTree_GetShownChildren(NULL, 1, two, 2, &count) == mui_errorInvalid &&
              muiAccessTree_GetShownChildren(tree, 1, two, 2, NULL) == mui_errorInvalid &&
              muiAccessTree_GetShownChildren(tree, 1, NULL, 2, &count) == mui_errorInvalid,
          "arguments");
    muiDestroyAccessTree(tree);
}

static bool NameIs(const muiAccessTree* tree, uint64_t id, const char* expected)
{
    char name[64];
    size_t length = 0;
    return muiAccessTree_GetName(tree, id, name, sizeof(name), &length) == mui_success &&
           length == strlen(expected) && strcmp(name, expected) == 0;
}

static bool BoundsAre(const muiAccessTree* tree, uint64_t id, muiRect expected)
{
    muiRect bounds = {0};
    return muiAccessTree_GetBounds(tree, id, &bounds) == mui_success && bounds.x == expected.x &&
           bounds.y == expected.y && bounds.width == expected.width &&
           bounds.height == expected.height;
}

static void TestNamesAndBounds(void)
{
    static Built s_built;
    BuildScene(&s_built);
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    muiAccessUpdate update = {s_built.sent, s_built.nodeCount, s_built.children, 1, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "built");
    // A label; a label node's value; what is inside a button, the hidden
    // left out; the nodes that label, the describing one not.
    CHECK(NameIs(tree, 3, "OK") && NameIs(tree, 4, "Group") && NameIs(tree, 5, "Inner") &&
              NameIs(tree, 17, "Save file") && NameIs(tree, 22, "Inner OK"),
          "names");
    char name[8] = "xxxxxxx";
    size_t length = 9;
    CHECK(muiAccessTree_GetName(tree, 1, name, sizeof(name), &length) == mui_empty && length == 0 &&
              name[0] == '\0' &&
              muiAccessTree_GetName(tree, 8, name, sizeof(name), &length) == mui_empty &&
              muiAccessTree_GetName(tree, 99, name, sizeof(name), &length) == mui_empty,
          "no name");
    // Cut short on a whole character.
    muiAccessNode accented = {.id = 30, .text = {"a\xC3\xA9"}, .textLength = {3}};
    const muiAccessNode* sent[2] = {&accented, s_built.sent[0]};
    uint64_t children[6] = {2, 6, 8, 17, 22, 30};
    muiAccessNode window = *s_built.sent[0];
    window.firstChild = 0;
    window.childCount = 6;
    sent[1] = &window;
    update = (muiAccessUpdate){sent, 2, children, 0, 0};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "accented");
    CHECK(muiAccessTree_GetName(tree, 30, name, 3, &length) == mui_errorCapacity && length == 3 &&
              strcmp(name, "a") == 0 &&
              muiAccessTree_GetName(tree, 30, name, 4, &length) == mui_success &&
              strcmp(name, "a\xC3\xA9") == 0 &&
              muiAccessTree_GetName(tree, 17, name, 6, &length) == mui_errorCapacity &&
              length == 9 && strcmp(name, "Save ") == 0 &&
              muiAccessTree_GetName(tree, 17, NULL, 0, &length) == mui_errorCapacity && length == 9,
          "cut");
    CHECK(muiAccessTree_GetName(NULL, 3, name, 8, &length) == mui_errorInvalid &&
              muiAccessTree_GetName(tree, 3, name, 8, NULL) == mui_errorInvalid &&
              muiAccessTree_GetName(tree, 3, NULL, 8, &length) == mui_errorInvalid,
          "arguments");
    // Bounds through every transform, the root's included: moved,
    // scaled, and turned a quarter.
    CHECK(BoundsAre(tree, 1, (muiRect){10, 20, 400, 300}) &&
              BoundsAre(tree, 12, (muiRect){10, 120, 100, 20}) &&
              BoundsAre(tree, 9, (muiRect){10, 60, 100, 20}) &&
              BoundsAre(tree, 17, (muiRect){210, 20, 60, 20}) &&
              BoundsAre(tree, 22, (muiRect){300, 20, 10, 40}),
          "bounds");
    muiRect bounds;
    CHECK(muiAccessTree_GetBounds(tree, 99, &bounds) == mui_empty &&
              muiAccessTree_GetBounds(NULL, 1, &bounds) == mui_errorInvalid &&
              muiAccessTree_GetBounds(tree, 1, NULL) == mui_errorInvalid,
          "bounds' arguments");
    muiDestroyAccessTree(tree);
}

typedef struct Counter
{
    size_t live;
    int allowed;
} Counter;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    Counter* counter = context;
    if (counter->allowed == 0 || alignment > alignof(max_align_t))
    {
        return NULL;
    }
    counter->allowed--;
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
    // Memory running out partway: nothing taken, nothing kept.
    Counter counter = {0, 1};
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.allocator = (muiAllocator){Allocate, Release, &counter};
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    size_t created = counter.live;
    muiAccessNode nodes[2] = {{.id = 1, .childCount = 1, .text = {"A"}, .textLength = {1}},
                              {.id = 2, .text = {"B"}, .textLength = {1}}};
    const muiAccessNode* sent[2] = {&nodes[0], &nodes[1]};
    const uint64_t children[1] = {2};
    muiAccessUpdate update = {sent, 2, children, 1, 1};
    for (int allowed = 0; allowed < 3; allowed++)
    {
        counter.allowed = allowed;
        CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorCapacity &&
                  counter.live == created && muiAccessTree_Count(tree) == 0,
              "out of memory");
    }
    counter.allowed = 3;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success && counter.live > created,
          "two texts and a list");
    muiDestroyAccessTree(tree);
    CHECK(counter.live == 0, "all freed");
    counter.allowed = 0;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_errorCapacity && tree == NULL, "no block");
}

static void TestContract(void)
{
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    muiAccessTree* tree = NULL;
    def.nodes = 0;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_errorInvalid, "no nodes");
    def = muiDefaultAccessTreeDef();
    def.cookie = 0;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_errorInvalid &&
              muiCreateAccessTree(NULL, &tree) == mui_errorInvalid,
          "a def");
    def = muiDefaultAccessTreeDef();
    CHECK(muiCreateAccessTree(&def, NULL) == mui_errorInvalid, "no out");
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    size_t length = 99;
    char small[4];
    CHECK(muiAccessTree_Write(tree, small, sizeof(small), &length) == mui_success && length == 0 &&
              small[0] == '\0',
          "empty");
    muiAccessNode node = {.id = 7, .role = mui_roleMarquee};
    const muiAccessNode* sent[1] = {&node};
    muiAccessUpdate update = {sent, 1, NULL, 7, 7};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "one");
    CHECK(muiAccessTree_Write(tree, small, sizeof(small), &length) == mui_errorCapacity &&
              length == strlen("marquee #7 0x0 @0,0\n") && strcmp(small, "mar") == 0,
          "cut short");
    CHECK(muiAccessTree_Write(tree, NULL, 0, &length) == mui_errorCapacity &&
              muiAccessTree_Write(tree, NULL, 1, &length) == mui_errorInvalid &&
              muiAccessTree_Write(NULL, small, 4, &length) == mui_errorInvalid &&
              muiAccessTree_Write(tree, small, 4, NULL) == mui_errorInvalid,
          "write's arguments");
    uint32_t count = 9;
    CHECK(muiAccessTree_Find(tree, 8) == NULL && muiAccessTree_Find(NULL, 7) == NULL &&
              muiAccessTree_GetChildren(tree, 8, &count) == NULL && count == 0 &&
              muiAccessTree_GetParent(tree, 8) == 0 && muiAccessTree_Count(NULL) == 0 &&
              muiAccessTree_GetRoot(NULL) == 0 && muiAccessTree_GetFocus(NULL) == 0,
          "reads");
    CHECK(strcmp(muiAccessRoleName(mui_roleButton), "button") == 0 &&
              strcmp(muiAccessRoleName(MUI_ROLE_LAST), "marquee") == 0 &&
              strcmp(muiAccessRoleName(MUI_ROLE_LAST + 1), "unknown") == 0,
          "role names");
    muiDestroyAccessTree(tree);
}

int main(void)
{
    TestUpdatesApplied();
    TestNewRoot();
    TestRefused();
    TestHandMade();
    TestChurn();
    TestShown();
    TestNamesAndBounds();
    TestMemory();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
