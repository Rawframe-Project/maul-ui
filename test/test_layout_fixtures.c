// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs the layout fixture corpus (test/layout/*.txt): builds each tree,
// lays it out, and compares every rectangle with Chrome's within 1/32
// unit, twice Chrome's 1/64 px step.

#include "test_harness.h"

#include "generated/layout_fixtures.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

#include <math.h>
#include <stdio.h>

enum
{
    MAX_NODES = 64
};

static const float TOLERANCE = 1.0f / 32.0f;

// Host content: the fixture node whose index is the host key.
static muiSize MeasureFixture(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                              muiMeasureAxis height)
{
    (void)nodeId;
    (void)width;
    (void)height;
    const LayoutFixture* fixture = user;
    return fixture->nodes[hostKey].content;
}

// Its baseline: Chrome renders host content as a box of its size at the
// top of the content box, whose baseline is its bottom.
static float BaselineFixture(void* user, muiNodeId nodeId, uint64_t hostKey, float width,
                             float height)
{
    (void)nodeId;
    (void)width;
    (void)height;
    const LayoutFixture* fixture = user;
    return fixture->nodes[hostKey].content.height;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= TOLERANCE;
}

static void CheckRect(const LayoutFixture* fixture, int index, muiRect got)
{
    muiRect want = fixture->nodes[index].expected;
    bool same = Near(got.x, want.x) && Near(got.y, want.y) && Near(got.width, want.width) &&
                Near(got.height, want.height);
    if (!same)
    {
        fprintf(stderr, "%s node %d: got %g %g %g %g, Chrome %g %g %g %g\n", fixture->name, index,
                (double)got.x, (double)got.y, (double)got.width, (double)got.height, (double)want.x,
                (double)want.y, (double)want.width, (double)want.height);
    }
    CHECK(same, "rectangle matches Chrome");
}

// Builds the fixture's tree, the root first; parents come before their
// children in preorder, so each node's parent is the last one a level up.
static bool Build(muiContext* context, const LayoutFixture* fixture, muiNodeId* ids)
{
    muiNodeId parents[MAX_NODES] = {0};
    for (int i = 0; i < fixture->count; i++)
    {
        const LayoutFixtureNode* node = &fixture->nodes[i];
        muiNodeDef def = muiDefaultNodeDef();
        def.hostKey = (uint64_t)i;
        if (muiCreateNode(context, &def, &ids[i]) != mui_success ||
            muiNode_SetLayoutStyle(context, ids[i], &node->style) != mui_success)
        {
            return false;
        }
        if (node->depth > 0 && muiNode_InsertChild(context, parents[node->depth - 1], ids[i],
                                                   (muiNodeId){0, 0}) != mui_success)
        {
            return false;
        }
        parents[node->depth] = ids[i];
    }
    return true;
}

static void RunFixture(const LayoutFixture* fixture)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.nodes = MAX_NODES;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "create context");
    muiNodeId ids[MAX_NODES];
    CHECK(fixture->count <= MAX_NODES && Build(context, fixture, ids), fixture->name);
    muiLayoutInput input = {
        .availableWidth = fixture->availableWidth,
        .availableHeight = fixture->availableHeight,
        .measure = MeasureFixture,
        .measureUser = (void*)fixture,
        .baseline = BaselineFixture,
    };
    CHECK(muiComputeLayout(context, ids[0], &input) == mui_success, "layout");
    for (int i = 0; i < fixture->count; i++)
    {
        CheckRect(fixture, i, muiNode_GetRect(context, ids[i]));
    }
    muiDestroyContext(context);
}

int main(void)
{
    for (size_t i = 0; i < sizeof(s_layoutFixtures) / sizeof(s_layoutFixtures[0]); i++)
    {
        RunFixture(&s_layoutFixtures[i]);
    }
    return s_failures == 0 ? 0 : 1;
}
