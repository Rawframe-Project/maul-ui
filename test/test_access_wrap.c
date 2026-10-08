// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's apply serial wrapping (src/access_apply.c):
// after 2^32 applies the marks of earlier ones are cleared, every held
// node's, so a node listed long ago is not taken as listed now. White
// box: the serial is set to its last value rather than counted there.

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
    muiAccessUpdate update = {sent, 2, children, 1, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success && tree->apply == 1 &&
              tree->held[0].listed == 1,
          "the first slot marked at the first apply");
    tree->apply = UINT32_MAX;
    const muiAccessNode* again[1] = {&root};
    update = (muiAccessUpdate){again, 1, children, 0, 1};
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success && tree->apply == 1,
          "the serial wrapped: the root lists its child again, once");
    muiDestroyAccessTree(tree);
    return s_failures == 0 ? 0 : 1;
}
