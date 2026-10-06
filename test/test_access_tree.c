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
    // A child two nodes list stays while one of them does.
    muiAccessNode top = {.id = 1, .childCount = 2};
    muiAccessNode left = {.id = 3, .firstChild = 2, .childCount = 1};
    muiAccessNode right = {.id = 4, .firstChild = 3, .childCount = 1};
    muiAccessNode shared = {.id = 5};
    const muiAccessNode* four[4] = {&top, &left, &right, &shared};
    const uint64_t lists[4] = {3, 4, 5, 5};
    update = Update(four, 4, lists, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "shared");
    top.childCount = 1;
    top.firstChild = 0;
    const muiAccessNode* topOnly[1] = {&top};
    const uint64_t rightOnly[1] = {4};
    update = Update(topOnly, 1, rightOnly, 0, 0);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
              muiAccessTree_Find(tree, 3) == NULL && muiAccessTree_Find(tree, 5) != NULL &&
              muiAccessTree_GetParent(tree, 5) == 4,
          "the shared child kept");
    // A child made the root: no parent, the old root gone.
    muiAccessNode bare = {.id = 1};
    const muiAccessNode* reroot[2] = {&bare, &right};
    const uint64_t rightsChild[1] = {5};
    right.firstChild = 0;
    update = Update(reroot, 2, rightsChild, 4, 4);
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
    TestMemory();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
