// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hit testing (record mui-0007). The walk is preorder, as painting's, so
// the last node whose rounded border box holds the point is the topmost;
// a subtree whose hit mode is none, or below a clipping node the point is
// outside, is passed over whole. Origins add up in doubles, so walking
// back up takes off exactly what walking down added.

#include "context.h"
#include "layout_node.h"
#include "paint.h"
#include "tree.h"

#include "maul-ui/interaction.h"

#include <math.h>

// Whether a point, from a rounded box's top left, is in it: in the box,
// half open, and inside the circle of any corner whose square it is in.
static bool IsInside(float x, float y, float width, float height, muiCorners radii)
{
    if (!(x >= 0.0f && y >= 0.0f && x < width && y < height))
    {
        return false;
    }
    float right = width - x;
    float bottom = height - y;
    const float corners[4][3] = {{radii.topLeft, x, y},
                                 {radii.topRight, right, y},
                                 {radii.bottomRight, right, bottom},
                                 {radii.bottomLeft, x, bottom}};
    for (int i = 0; i < 4; i++)
    {
        float radius = corners[i][0];
        float dx = radius - corners[i][1];
        float dy = radius - corners[i][2];
        if (dx > 0.0f && dy > 0.0f && dx * dx + dy * dy > radius * radius)
        {
            return false;
        }
    }
    return true;
}

// One walk: the point, and the topmost node found so far with the point
// in its border box.
typedef struct Walk
{
    const muiContext* context;
    float x;
    float y;
    uint32_t found;
    float foundX;
    float foundY;
} Walk;

// Tests a node at its origin; whether the point may hit its children.
static bool Visit(Walk* walk, uint32_t slot, double originX, double originY)
{
    const muiContext* context = walk->context;
    muiHitMode mode = context->interaction[slot - 1].hitMode;
    if (mode == mui_hitNone)
    {
        return false;
    }
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiVisualStyle* visual = &context->visual[slot - 1];
    float x = walk->x - (float)originX;
    float y = walk->y - (float)originY;
    muiRect rect = {0.0f, 0.0f, layout->rect.width, layout->rect.height};
    bool inside =
        IsInside(x, y, rect.width, rect.height, muiCornersOf(&visual->radius, rect, layout->rtl));
    if (inside && mode == mui_hitAuto)
    {
        walk->found = slot;
        walk->foundX = x;
        walk->foundY = y;
    }
    return inside || !visual->clip;
}

muiResult muiHitTest(const muiContext* context, muiNodeId rootId, float x, float y, muiHit* hitOut)
{
    if (context == nullptr || hitOut == nullptr || rootId.index1 == 0 || !isfinite(x) ||
        !isfinite(y))
    {
        return mui_errorInvalid;
    }
    const muiTree* tree = &context->tree;
    uint32_t root = muiTreeResolve(tree, rootId);
    if (root == 0)
    {
        return mui_errorStale;
    }
    Walk walk = {context, x, y, 0, 0.0f, 0.0f};
    // The origin of the parent of the node at.
    double parentX = 0.0;
    double parentY = 0.0;
    for (uint32_t at = root; at != 0;)
    {
        const muiRect* rect = &context->layout[at - 1].rect;
        double originX = parentX + (double)rect->x;
        double originY = parentY + (double)rect->y;
        const muiTreeNode* node = muiTreeAt(tree, at);
        if (Visit(&walk, at, originX, originY) && node->links.firstChild != 0)
        {
            parentX = originX;
            parentY = originY;
            at = node->links.firstChild;
            continue;
        }
        // On to the next sibling, or up to the first ancestor with one.
        while (at != root && muiTreeAt(tree, at)->links.next == 0)
        {
            at = muiTreeAt(tree, at)->links.parent;
            parentX -= (double)context->layout[at - 1].rect.x;
            parentY -= (double)context->layout[at - 1].rect.y;
        }
        at = at == root ? 0 : muiTreeAt(tree, at)->links.next;
    }
    *hitOut = (muiHit){.passThrough = true};
    if (walk.found != 0)
    {
        *hitOut = (muiHit){muiTreeIdOf(tree, walk.found), walk.foundX, walk.foundY,
                           context->interaction[walk.found - 1].passThrough};
    }
    return mui_success;
}
