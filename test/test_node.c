// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Building and editing the node tree through the public API.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/node.h"

static const muiNodeId s_null = {0, 0};

static muiContext* MakeContext(uint32_t nodes)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.nodes = nodes;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create context");
    return context;
}

static muiNodeId MakeNode(muiContext* context, uint64_t hostKey)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = hostKey;
    muiNodeId node = s_null;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "create node");
    return node;
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// Whether parent's children, first to last and last to first, are
// exactly expected.
static bool ChildrenAre(const muiContext* context, muiNodeId parent, const muiNodeId* expected,
                        uint32_t count)
{
    if (muiNode_GetChildCount(context, parent) != count)
    {
        return false;
    }
    muiNodeId at = muiNode_GetFirstChild(context, parent);
    for (uint32_t i = 0; i < count; i++, at = muiNode_GetNextSibling(context, at))
    {
        if (!Same(at, expected[i]) || !Same(muiNode_GetParent(context, at), parent))
        {
            return false;
        }
    }
    if (at.index1 != 0)
    {
        return false;
    }
    at = muiNode_GetLastChild(context, parent);
    for (uint32_t i = count; i > 0; i--, at = muiNode_GetPreviousSibling(context, at))
    {
        if (!Same(at, expected[i - 1]))
        {
            return false;
        }
    }
    return at.index1 == 0;
}

static void TestCreatedNodeIsALiveRootWithItsKey(void)
{
    muiContext* context = MakeContext(4);
    muiNodeId node = MakeNode(context, 0x1234567890ull);
    CHECK(muiNode_IsValid(context, node), "valid");
    CHECK(muiNode_GetHostKey(context, node) == 0x1234567890ull, "host key");
    CHECK(Same(muiNode_GetParent(context, node), s_null), "a root");
    CHECK(muiNode_GetChildCount(context, node) == 0, "no children");
    muiDestroyContext(context);
}

static void TestNodeLimitIsCapacity(void)
{
    muiContext* context = MakeContext(2);
    MakeNode(context, 1);
    MakeNode(context, 2);
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {7, 7};
    CHECK(muiCreateNode(context, &def, &node) == mui_errorCapacity, "limit reached");
    CHECK(Same(node, s_null), "out cleared");
    CHECK(muiGetContextMisuse(context) == 0, "capacity is not misuse");
    muiDestroyContext(context);
}

static void TestInsertAppendsAndInsertsBefore(void)
{
    muiContext* context = MakeContext(8);
    muiNodeId parent = MakeNode(context, 0);
    muiNodeId a = MakeNode(context, 1);
    muiNodeId b = MakeNode(context, 2);
    muiNodeId c = MakeNode(context, 3);
    muiNodeId d = MakeNode(context, 4);
    CHECK(muiNode_InsertChild(context, parent, b, s_null) == mui_success, "append b");
    CHECK(muiNode_InsertChild(context, parent, d, s_null) == mui_success, "append d");
    CHECK(muiNode_InsertChild(context, parent, a, b) == mui_success, "a before b");
    CHECK(muiNode_InsertChild(context, parent, c, d) == mui_success, "c before d");
    const muiNodeId expected[] = {a, b, c, d};
    CHECK(ChildrenAre(context, parent, expected, 4), "order a b c d");
    muiDestroyContext(context);
}

static void TestDetachKeepsTheOthersInOrder(void)
{
    muiContext* context = MakeContext(8);
    muiNodeId parent = MakeNode(context, 0);
    muiNodeId kids[3];
    for (int i = 0; i < 3; i++)
    {
        kids[i] = MakeNode(context, (uint64_t)i);
        CHECK(muiNode_InsertChild(context, parent, kids[i], s_null) == mui_success, "append");
    }
    CHECK(muiNode_Detach(context, kids[1]) == mui_success, "detach middle");
    const muiNodeId ends[] = {kids[0], kids[2]};
    CHECK(ChildrenAre(context, parent, ends, 2), "ends remain");
    CHECK(Same(muiNode_GetParent(context, kids[1]), s_null), "detached is a root");
    CHECK(Same(muiNode_GetNextSibling(context, kids[1]), s_null), "no sibling left");
    CHECK(muiNode_Detach(context, kids[1]) == mui_success, "detaching a root is nothing");
    CHECK(muiNode_Detach(context, kids[0]) == mui_success, "detach first");
    CHECK(muiNode_Detach(context, kids[2]) == mui_success, "detach last");
    CHECK(ChildrenAre(context, parent, NULL, 0), "empty");
    CHECK(muiNode_InsertChild(context, parent, kids[1], s_null) == mui_success, "reinsert");
    CHECK(ChildrenAre(context, parent, &kids[1], 1), "one child again");
    muiDestroyContext(context);
}

static void TestDestroyTakesTheSubtree(void)
{
    muiContext* context = MakeContext(8);
    muiNodeId root = MakeNode(context, 0);
    muiNodeId branch = MakeNode(context, 1);
    muiNodeId leafA = MakeNode(context, 2);
    muiNodeId leafB = MakeNode(context, 3);
    muiNodeId sibling = MakeNode(context, 4);
    CHECK(muiNode_InsertChild(context, root, branch, s_null) == mui_success, "branch");
    CHECK(muiNode_InsertChild(context, root, sibling, s_null) == mui_success, "sibling");
    CHECK(muiNode_InsertChild(context, branch, leafA, s_null) == mui_success, "leaf a");
    CHECK(muiNode_InsertChild(context, branch, leafB, s_null) == mui_success, "leaf b");
    CHECK(muiDestroyNode(context, branch) == mui_success, "destroy branch");
    CHECK(!muiNode_IsValid(context, branch), "branch gone");
    CHECK(!muiNode_IsValid(context, leafA) && !muiNode_IsValid(context, leafB), "leaves gone");
    CHECK(ChildrenAre(context, root, &sibling, 1), "sibling stays");
    muiDestroyContext(context);
}

static void TestStaleIdsAreRefusedWithoutMisuse(void)
{
    muiContext* context = MakeContext(4);
    muiNodeId parent = MakeNode(context, 0);
    muiNodeId gone = MakeNode(context, 1);
    CHECK(muiDestroyNode(context, gone) == mui_success, "destroy");
    CHECK(muiDestroyNode(context, gone) == mui_errorStale, "destroy again");
    CHECK(muiNode_Detach(context, gone) == mui_errorStale, "detach");
    CHECK(muiNode_InsertChild(context, parent, gone, s_null) == mui_errorStale, "insert stale");
    CHECK(muiNode_InsertChild(context, gone, parent, s_null) == mui_errorStale, "into stale");
    muiNodeId child = MakeNode(context, 2);
    CHECK(muiNode_InsertChild(context, parent, child, gone) == mui_errorStale, "before stale");
    CHECK(muiNode_GetHostKey(context, gone) == 0, "no key");
    CHECK(Same(muiNode_GetParent(context, gone), s_null), "no parent");
    CHECK(muiGetContextMisuse(context) == 0, "stale is not misuse");
    muiDestroyContext(context);
}

static void TestReusedSlotGetsANewGeneration(void)
{
    muiContext* context = MakeContext(4);
    muiNodeId first = MakeNode(context, 1);
    muiNodeId second = MakeNode(context, 2);
    CHECK(muiDestroyNode(context, first) == mui_success, "destroy first");
    CHECK(muiDestroyNode(context, second) == mui_success, "destroy second");
    muiNodeId again = MakeNode(context, 3);
    CHECK(again.index1 == second.index1, "last freed slot reused first");
    CHECK(again.generation != second.generation, "new generation");
    CHECK(!muiNode_IsValid(context, second), "old id stays stale");
    CHECK(muiNode_GetHostKey(context, again) == 3, "new key");
    muiNodeId next = MakeNode(context, 4);
    CHECK(next.index1 == first.index1, "then the slot freed before it");
    muiDestroyContext(context);
}

static void TestBadEditsAreMisuse(void)
{
    muiContext* context = MakeContext(8);
    muiNodeId root = MakeNode(context, 0);
    muiNodeId child = MakeNode(context, 1);
    muiNodeId grandchild = MakeNode(context, 2);
    muiNodeId other = MakeNode(context, 3);
    CHECK(muiNode_InsertChild(context, root, child, s_null) == mui_success, "child");
    CHECK(muiNode_InsertChild(context, child, grandchild, s_null) == mui_success, "grandchild");
    uint64_t misuse = muiGetContextMisuse(context);

    CHECK(muiNode_InsertChild(context, other, child, s_null) == mui_errorInvalid, "has parent");
    CHECK(muiNode_InsertChild(context, grandchild, root, s_null) == mui_errorInvalid, "cycle");
    CHECK(muiNode_InsertChild(context, root, root, s_null) == mui_errorInvalid, "itself");
    CHECK(muiNode_InsertChild(context, root, other, grandchild) == mui_errorInvalid,
          "before a grandchild");
    CHECK(muiNode_InsertChild(context, s_null, other, s_null) == mui_errorInvalid, "null parent");
    CHECK(muiNode_InsertChild(context, root, s_null, s_null) == mui_errorInvalid, "null child");
    CHECK(muiNode_Detach(context, s_null) == mui_errorInvalid, "detach null");
    CHECK(muiDestroyNode(context, s_null) == mui_errorInvalid, "destroy null");
    muiNodeDef def = muiDefaultNodeDef();
    def.cookie = 0;
    muiNodeId node = s_null;
    CHECK(muiCreateNode(context, &def, &node) == mui_errorInvalid, "bad cookie");
    CHECK(muiCreateNode(context, NULL, &node) == mui_errorInvalid, "NULL def");
    CHECK(muiGetContextMisuse(context) == misuse + 10, "each counted once");

    const muiNodeId one[] = {child};
    CHECK(ChildrenAre(context, root, one, 1), "tree unchanged");
    CHECK(Same(muiNode_GetParent(context, other), s_null), "other still a root");
    muiDestroyContext(context);
}

static void TestNullContextIsRefused(void)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_null;
    CHECK(muiCreateNode(NULL, &def, &node) == mui_errorInvalid, "create");
    CHECK(muiDestroyNode(NULL, node) == mui_errorInvalid, "destroy");
    CHECK(muiNode_InsertChild(NULL, node, node, node) == mui_errorInvalid, "insert");
    CHECK(muiNode_Detach(NULL, node) == mui_errorInvalid, "detach");
    CHECK(!muiNode_IsValid(NULL, node), "valid");
    CHECK(muiNode_GetChildCount(NULL, node) == 0, "count");
    CHECK(Same(muiNode_GetFirstChild(NULL, node), s_null), "first child");
}

static void TestSameEditsGiveSameIds(void)
{
    muiNodeId ids[2][6];
    for (int run = 0; run < 2; run++)
    {
        muiContext* context = MakeContext(8);
        muiNodeId root = MakeNode(context, 0);
        for (int i = 0; i < 4; i++)
        {
            ids[run][i] = MakeNode(context, (uint64_t)i);
            CHECK(muiNode_InsertChild(context, root, ids[run][i], s_null) == mui_success, "add");
        }
        CHECK(muiDestroyNode(context, ids[run][1]) == mui_success, "destroy");
        CHECK(muiDestroyNode(context, ids[run][3]) == mui_success, "destroy");
        ids[run][4] = MakeNode(context, 4);
        ids[run][5] = MakeNode(context, 5);
        muiDestroyContext(context);
    }
    for (int i = 0; i < 6; i++)
    {
        CHECK(Same(ids[0][i], ids[1][i]), "same id in both runs");
    }
}

int main(void)
{
    TestCreatedNodeIsALiveRootWithItsKey();
    TestNodeLimitIsCapacity();
    TestInsertAppendsAndInsertsBefore();
    TestDetachKeepsTheOthersInOrder();
    TestDestroyTakesTheSubtree();
    TestStaleIdsAreRefusedWithoutMisuse();
    TestReusedSlotGetsANewGeneration();
    TestBadEditsAreMisuse();
    TestNullContextIsRefused();
    TestSameEditsGiveSameIds();
    return s_failures == 0 ? 0 : 1;
}
