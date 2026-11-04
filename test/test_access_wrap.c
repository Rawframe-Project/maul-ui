// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's apply serial wrapping (src/access_apply.c):
// after 2^32 applies the marks of earlier ones are cleared, every held
// node's, so a node listed long ago is not taken as listed now, nor one
// whose parents were found to end long ago as ending now. White box: the
// serial is set to its last value rather than counted there.

#include "access_tree_store.h"
#include "test_harness.h"

#include "maul-ui/access_tree.h"

#include <stdint.h>

int main(void)
{
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.nodes = 2;
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "a tree");
    // Two slots, both held: the root, marked as the new root, and the
    // child it lists, both at the first apply.
    muiAccessNode child = {.id = 2};
    muiAccessNode root = {.id = 1, .childCount = 1};
    const muiAccessNode* sent[2] = {&child, &root};
    const uint64_t children[1] = {2};
    muiAccessUpdate update = {sent, 2, children, 1, 1, 0};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success && tree->apply == 1 &&
              tree->held[0].listed == 1,
          "the first slot marked at the first apply");
    tree->apply = UINT32_MAX;
    const muiAccessNode* again[1] = {&root};
    update = (muiAccessUpdate){again, 1, children, 0, 1, 0};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success && tree->apply == 1,
          "the serial wrapped: the root lists its child again, once");
    muiDestroyAccessTree(tree);

    // Four slots: the root lists 2, which lists 3, which lists 4. Sending
    // 4 alone at the second apply walks up through 3, 2 and the root, each
    // held and not sent, so each keeps that it was found to end then. The
    // serial wraps at an apply that sends the root alone, and at the next,
    // 2 again, an update that unhooks 2 from the root and has 2 and 3 list
    // each other leaves a cycle no walk ends, whatever the second apply
    // found.
    def.nodes = 4;
    tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "a tree of four");
    muiAccessNode top = {.id = 1, .firstChild = 0, .childCount = 1};
    muiAccessNode middle = {.id = 2, .firstChild = 1, .childCount = 1};
    muiAccessNode lower = {.id = 3, .firstChild = 2, .childCount = 1};
    muiAccessNode leaf = {.id = 4};
    const muiAccessNode* chain[4] = {&top, &middle, &lower, &leaf};
    const uint64_t chainChildren[3] = {2, 3, 4};
    update = (muiAccessUpdate){chain, 4, chainChildren, 1, 1, 3};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "the chain");
    update = (muiAccessUpdate){&chain[3], 1, chainChildren, 0, 0, 3};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success && tree->apply == 2 &&
              tree->held[muiHeldSlotOf(tree, 2) - 1].ended == 2 &&
              tree->held[muiHeldSlotOf(tree, 3) - 1].ended == 2,
          "the nodes above the leaf found to end at the second apply");
    tree->apply = UINT32_MAX;
    update = (muiAccessUpdate){chain, 1, chainChildren, 0, 0, 3};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success && tree->apply == 1,
          "the serial wrapped at the root sent alone");
    muiAccessNode bare = {.id = 1};
    muiAccessNode down = {.id = 2, .firstChild = 0, .childCount = 1};
    muiAccessNode up = {.id = 3, .firstChild = 1, .childCount = 1};
    const muiAccessNode* loop[3] = {&bare, &down, &up};
    const uint64_t loopChildren[2] = {3, 2};
    update = (muiAccessUpdate){loop, 3, loopChildren, 0, 0, 2};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid && tree->apply == 2 &&
              muiAccessTree_GetNodeCount(tree) == 4 && muiAccessTree_GetParent(tree, 2) == 1,
          "at the serial's second turn, a cycle through nodes found to end then is refused");
    muiDestroyAccessTree(tree);
    return s_failures == 0 ? 0 : 1;
}
