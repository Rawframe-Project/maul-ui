// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The node store's dirty flags: what edits mark, how far marks travel,
// and that a pass leaves nothing behind.

#include "test_harness.h"
#include "tree.h"

enum
{
    CAPACITY = 16
};

typedef struct Fixture
{
    muiTreeNode nodes[CAPACITY];
    muiTree tree;
} Fixture;

static void Setup(Fixture* fixture)
{
    *fixture = (Fixture){0};
    muiTreeInit(&fixture->tree, fixture->nodes, CAPACITY);
}

// A chain root -> 1 -> 2 -> ... of depth nodes, swept clean; returns the
// slots in chainOut.
static void MakeChain(Fixture* fixture, uint32_t depth, uint32_t* chainOut)
{
    for (uint32_t i = 0; i < depth; i++)
    {
        chainOut[i] = muiTreeCreate(&fixture->tree, i);
        if (i > 0)
        {
            muiTreeInsert(&fixture->tree, chainOut[i - 1], chainOut[i], 0);
        }
    }
    muiTreeSweep(&fixture->tree, chainOut[0], mui_stageAll);
}

static void TestNewNodeRequestsEveryStage(void)
{
    Fixture fixture;
    Setup(&fixture);
    uint32_t node = muiTreeCreate(&fixture.tree, 0);
    const muiTreeDirty dirty = muiTreeAt(&fixture.tree, node)->dirty;
    CHECK(dirty.request == mui_stageAll && dirty.subtree == mui_stageAll, "all requested");
}

static void TestSweptTreeCostsNothing(void)
{
    Fixture fixture;
    Setup(&fixture);
    uint32_t chain[5];
    MakeChain(&fixture, 5, chain);
    CHECK(muiTreeSweep(&fixture.tree, chain[0], mui_stageAll) == 0, "static frame reaches none");
    for (uint32_t i = 0; i < 5; i++)
    {
        const muiTreeDirty dirty = muiTreeAt(&fixture.tree, chain[i])->dirty;
        CHECK(dirty.request == 0 && dirty.subtree == 0, "no flag left");
    }
}

static void TestMarkReachesOnlyThePathToTheRoot(void)
{
    Fixture fixture;
    Setup(&fixture);
    uint32_t chain[5];
    MakeChain(&fixture, 5, chain);
    uint32_t side = muiTreeCreate(&fixture.tree, 99);
    muiTreeInsert(&fixture.tree, chain[1], side, 0);
    muiTreeSweep(&fixture.tree, chain[0], mui_stageAll);

    muiTreeMark(&fixture.tree, chain[4], mui_stageLayout);
    CHECK(muiTreeAt(&fixture.tree, chain[4])->dirty.request == mui_stageLayout, "request");
    CHECK(muiTreeAt(&fixture.tree, chain[2])->dirty.request == 0, "ancestor not requested");
    CHECK(muiTreeAt(&fixture.tree, chain[0])->dirty.subtree == mui_stageLayout, "root owes");
    CHECK(muiTreeAt(&fixture.tree, side)->dirty.subtree == 0, "side branch untouched");
    CHECK(muiTreeSweep(&fixture.tree, chain[0], mui_stagePaint) == 0, "paint owes nothing");
    CHECK(muiTreeSweep(&fixture.tree, chain[0], mui_stageLayout) == 5, "the path, not the side");
}

static void TestSecondMarkStopsAtAMarkedAncestor(void)
{
    Fixture fixture;
    Setup(&fixture);
    uint32_t chain[5];
    MakeChain(&fixture, 5, chain);
    muiTreeMark(&fixture.tree, chain[2], mui_stageStyle);
    // Clearing the root alone shows whether the second mark climbs past
    // chain[2], which already owes style.
    muiTreeAt(&fixture.tree, chain[0])->dirty.subtree = 0;
    muiTreeMark(&fixture.tree, chain[4], mui_stageStyle);
    CHECK(muiTreeAt(&fixture.tree, chain[3])->dirty.subtree == mui_stageStyle, "below marked");
    CHECK(muiTreeAt(&fixture.tree, chain[0])->dirty.subtree == 0, "stopped at chain[2]");
}

static void TestInsertCarriesTheChildsDebtUp(void)
{
    Fixture fixture;
    Setup(&fixture);
    uint32_t chain[3];
    MakeChain(&fixture, 3, chain);
    uint32_t branch[3];
    for (uint32_t i = 0; i < 3; i++)
    {
        branch[i] = muiTreeCreate(&fixture.tree, 10 + i);
        if (i > 0)
        {
            muiTreeInsert(&fixture.tree, branch[i - 1], branch[i], 0);
        }
    }
    muiTreeSweep(&fixture.tree, branch[0], mui_stageAll);
    // Style, because the insertion's own marks request layout and paint
    // on the parent and would carry those up anyway.
    muiTreeMark(&fixture.tree, branch[2], mui_stageStyle);
    muiTreeInsert(&fixture.tree, chain[2], branch[0], 0);
    CHECK((muiTreeAt(&fixture.tree, chain[0])->dirty.subtree & mui_stageStyle) != 0,
          "root owes the branch's style");
    CHECK(muiTreeAt(&fixture.tree, branch[0])->dirty.request == mui_stageStyle, "child restyled");
    CHECK(muiTreeAt(&fixture.tree, chain[2])->dirty.request == (mui_stageLayout | mui_stagePaint),
          "parent relaid and repainted");
    muiTreeSweep(&fixture.tree, chain[0], mui_stageAll);
    CHECK(muiTreeAt(&fixture.tree, branch[2])->dirty.request == 0, "the sweep reached the leaf");
}

static void TestDetachMarksTheOldParent(void)
{
    Fixture fixture;
    Setup(&fixture);
    uint32_t chain[3];
    MakeChain(&fixture, 3, chain);
    muiTreeDetach(&fixture.tree, chain[2]);
    CHECK(muiTreeAt(&fixture.tree, chain[1])->dirty.request == (mui_stageLayout | mui_stagePaint),
          "old parent relaid");
    CHECK(muiTreeAt(&fixture.tree, chain[2])->dirty.request == mui_stageStyle,
          "the detached root restyled: it no longer reads its old ancestors' themes");
}

static void TestSweepVisitsSiblingsInOrderOnlyWhereOwed(void)
{
    Fixture fixture;
    Setup(&fixture);
    uint32_t root = muiTreeCreate(&fixture.tree, 0);
    uint32_t kids[4];
    for (uint32_t i = 0; i < 4; i++)
    {
        kids[i] = muiTreeCreate(&fixture.tree, i + 1);
        muiTreeInsert(&fixture.tree, root, kids[i], 0);
        uint32_t leaf = muiTreeCreate(&fixture.tree, 10 + i);
        muiTreeInsert(&fixture.tree, kids[i], leaf, 0);
    }
    CHECK(muiTreeSweep(&fixture.tree, root, mui_stageAll) == 9, "everything once");
    muiTreeMark(&fixture.tree, muiTreeAt(&fixture.tree, kids[1])->links.firstChild,
                mui_stageLayout);
    muiTreeMark(&fixture.tree, kids[3], mui_stageLayout);
    CHECK(muiTreeSweep(&fixture.tree, root, mui_stageLayout) == 4, "root, kid 1, its leaf, kid 3");
    CHECK(muiTreeSweep(&fixture.tree, root, mui_stageLayout) == 0, "then nothing");
}

static void TestDestroyFreesEverySlotOfTheSubtree(void)
{
    Fixture fixture;
    Setup(&fixture);
    uint32_t chain[4];
    MakeChain(&fixture, 4, chain);
    uint32_t extra = muiTreeCreate(&fixture.tree, 50);
    muiTreeInsert(&fixture.tree, chain[1], extra, 0);
    CHECK(fixture.tree.liveCount == 5, "five live");
    muiTreeDestroy(&fixture.tree, chain[1]);
    CHECK(fixture.tree.liveCount == 1, "root left");
    CHECK(muiTreeAt(&fixture.tree, chain[0])->links.childCount == 0, "root has no child");
    CHECK(fixture.tree.freeHead == chain[1], "the subtree's own root freed last");
}

int main(void)
{
    TestNewNodeRequestsEveryStage();
    TestSweptTreeCostsNothing();
    TestMarkReachesOnlyThePathToTheRoot();
    TestSecondMarkStopsAtAMarkedAncestor();
    TestInsertCarriesTheChildsDebtUp();
    TestDetachMarksTheOldParent();
    TestSweepVisitsSiblingsInOrderOnlyWhereOwed();
    TestDestroyFreesEverySlotOfTheSubtree();
    return s_failures == 0 ? 0 : 1;
}
