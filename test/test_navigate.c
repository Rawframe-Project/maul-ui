// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Directional navigation (record mui-0007): Android's focus search over a
// grid, the beam, weighted distances and ties, links that win, stop or
// fall back, layers as scopes, and calls outside the contract.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"

#include <stddef.h>

static const muiNodeId s_nullNode = {0, 0};

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// A node at x, y of a size under parent (none for a root) that takes
// focus as mode says.
static muiNodeId Box(muiContext* context, muiNodeId parent, float x, float y, float width,
                     float height, muiFocusMode mode)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = Length(width);
    layout.sizing.height = Length(height);
    layout.placement.position = parent.index1 != 0 ? mui_positionAbsolute : mui_positionFlow;
    layout.placement.inset.start = Length(x);
    layout.placement.inset.top = Length(y);
    CHECK(muiNode_SetLayoutValues(context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight) |
                                      MUI_PROPERTY_BIT(mui_propertyPosition) |
                                      MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                                      MUI_PROPERTY_BIT(mui_propertyInsetTop)) == mui_success,
          "placed");
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.focusMode = mode;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focus mode");
    return node;
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

static muiContext* MakeContext(uint32_t neighbors)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.neighbors = neighbors;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    return context;
}

// Whether moving from a node toward a direction reaches another.
static bool Moves(muiContext* context, muiNodeId root, muiNodeId from, muiDirection direction,
                  muiNodeId to)
{
    if (muiFocus_Set(context, 0, from, mui_focusByCode) != mui_success)
    {
        return false;
    }
    muiResult result = muiFocus_MoveToward(context, root, 0, direction);
    return to.index1 == 0 ? result == mui_empty && Same(muiFocus_Get(context, 0), from)
                          : result == mui_success && Same(muiFocus_Get(context, 0), to);
}

static void TestGrid(void)
{
    // A 3 by 3 grid of 40 by 40 buttons, 10 apart.
    muiContext* context = MakeContext(16);
    muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 500.0f, 500.0f, mui_focusNone);
    muiNodeId cells[9];
    for (int i = 0; i < 9; i++)
    {
        cells[i] = Box(context, root, (float)(i % 3) * 50.0f, (float)(i / 3) * 50.0f, 40.0f, 40.0f,
                       mui_focusAll);
    }
    Layout(context, root);
    CHECK(Moves(context, root, cells[4], mui_directionUp, cells[1]) &&
              Moves(context, root, cells[4], mui_directionDown, cells[7]) &&
              Moves(context, root, cells[4], mui_directionLeft, cells[3]) &&
              Moves(context, root, cells[4], mui_directionRight, cells[5]),
          "the center's neighbors");
    CHECK(Moves(context, root, cells[0], mui_directionLeft, s_nullNode) &&
              Moves(context, root, cells[0], mui_directionUp, s_nullNode) &&
              Moves(context, root, cells[8], mui_directionRight, s_nullNode) &&
              Moves(context, root, cells[8], mui_directionDown, s_nullNode),
          "edges: no wrap");
    CHECK(muiFocus_Set(context, 0, cells[4], mui_focusByPointer) == mui_success &&
              muiFocus_MoveToward(context, root, 0, mui_directionRight) == mui_success &&
              (muiNode_GetStates(context, cells[5]) & mui_stateFocusVisible) != 0,
          "shown");
    // No focus: as Tab, the first.
    CHECK(muiFocus_Set(context, 0, s_nullNode, mui_focusByCode) == mui_success &&
              muiFocus_MoveToward(context, root, 0, mui_directionUp) == mui_success &&
              Same(muiFocus_Get(context, 0), cells[0]),
          "from nothing");
    muiDestroyContext(context);
}

static void TestBeam(void)
{
    muiContext* context = MakeContext(16);
    muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
    muiNodeId s = Box(context, root, 100.0f, 100.0f, 10.0f, 10.0f, mui_focusAll);
    // Right: a far box in the beam beats a near one outside it.
    muiNodeId farIn = Box(context, root, 300.0f, 100.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId nearOut = Box(context, root, 120.0f, 150.0f, 10.0f, 10.0f, mui_focusAll);
    // Down: the far one in the beam loses to a near one outside it whose
    // far edge it does not pass; a box below s's bottom but overlapping
    // it along the way loses to the beam.
    muiNodeId belowIn = Box(context, root, 100.0f, 300.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId belowOut = Box(context, root, 150.0f, 125.0f, 10.0f, 10.0f, mui_focusAll);
    Layout(context, root);
    CHECK(Moves(context, root, s, mui_directionRight, farIn), "right: the beam wins");
    CHECK(Moves(context, root, s, mui_directionDown, belowOut),
          "down: a near box outside the beam wins over a far one in it");
    muiDestroyContext(context);
    // Down again, a box in the beam within the other's far edge wins.
    context = MakeContext(16);
    root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
    s = Box(context, root, 100.0f, 100.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId tall = Box(context, root, 150.0f, 115.0f, 10.0f, 200.0f, mui_focusAll);
    muiNodeId inBeam = Box(context, root, 100.0f, 160.0f, 10.0f, 10.0f, mui_focusAll);
    // Overlapping s along the way, outside its beam: not past it.
    muiNodeId overlap = Box(context, root, 130.0f, 105.0f, 10.0f, 10.0f, mui_focusAll);
    Layout(context, root);
    CHECK(Moves(context, root, s, mui_directionDown, inBeam), "within the far edge");
    (void)tall;
    (void)overlap;
    (void)nearOut;
    (void)belowIn;
    muiDestroyContext(context);
}

static void TestDistance(void)
{
    muiContext* context = MakeContext(16);
    muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
    muiNodeId s = Box(context, root, 100.0f, 100.0f, 10.0f, 10.0f, mui_focusAll);
    // Right, both outside the beam: 13 * 20^2 + 40^2 = 6800 against
    // 13 * 10^2 + 80^2 = 7700.
    muiNodeId a = Box(context, root, 130.0f, 140.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId b = Box(context, root, 120.0f, 180.0f, 10.0f, 10.0f, mui_focusAll);
    Layout(context, root);
    CHECK(Moves(context, root, s, mui_directionRight, a), "the lesser weighted distance");
    (void)b;
    muiDestroyContext(context);
    // Ties go to the earlier in tree order; boxes behind, or not past the
    // source's far edge, are no candidates.
    context = MakeContext(16);
    root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
    s = Box(context, root, 100.0f, 100.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId up = Box(context, root, 130.0f, 70.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId down = Box(context, root, 130.0f, 130.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId behind = Box(context, root, 50.0f, 100.0f, 10.0f, 10.0f, mui_focusAll);
    Layout(context, root);
    CHECK(Moves(context, root, s, mui_directionRight, up), "a tie: tree order");
    CHECK(Moves(context, root, s, mui_directionLeft, behind) &&
              Moves(context, root, behind, mui_directionLeft, s_nullNode),
          "behind");
    // A wide box holding s's span is no candidate up or down from it.
    muiNodeId wide = Box(context, root, 0.0f, 95.0f, 300.0f, 20.0f, mui_focusAll);
    Layout(context, root);
    CHECK(Moves(context, root, s, mui_directionUp, up) &&
              Moves(context, root, s, mui_directionDown, down),
          "a box around the source");
    (void)wide;
    muiDestroyContext(context);
}

static void TestKinds(void)
{
    // Only nodes Tab reaches: not pointer-only, disabled or none.
    muiContext* context = MakeContext(16);
    muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
    muiNodeId s = Box(context, root, 0.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId pointer = Box(context, root, 20.0f, 0.0f, 10.0f, 10.0f, mui_focusPointer);
    muiNodeId none = Box(context, root, 40.0f, 0.0f, 10.0f, 10.0f, mui_focusNone);
    muiNodeId disabled = Box(context, root, 60.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId last = Box(context, root, 80.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    CHECK(muiNode_SetStates(context, disabled, mui_stateDisabled) == mui_success, "disabled");
    Layout(context, root);
    CHECK(Moves(context, root, s, mui_directionRight, last), "passed over");
    (void)pointer;
    (void)none;
    muiDestroyContext(context);
}

static void TestLinks(void)
{
    muiContext* context = MakeContext(2);
    muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
    muiNodeId a = Box(context, root, 0.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId b = Box(context, root, 20.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId c = Box(context, root, 0.0f, 200.0f, 10.0f, 10.0f, mui_focusPointer);
    muiNodeId off = Box(context, root, 0.0f, 400.0f, 10.0f, 10.0f, mui_focusNone);
    Layout(context, root);
    CHECK(muiNode_SetNeighbor(context, a, mui_directionRight, c) == mui_success &&
              Same(muiNode_GetNeighbor(context, a, mui_directionRight), c) &&
              muiNode_GetNeighbor(context, a, mui_directionLeft).index1 == 0 &&
              Moves(context, root, a, mui_directionRight, c),
          "a link wins, even to a node Tab passes over");
    CHECK(muiNode_SetNeighbor(context, a, mui_directionRight, a) == mui_success &&
              Moves(context, root, a, mui_directionRight, s_nullNode),
          "a link to itself stops");
    CHECK(muiNode_SetNeighbor(context, a, mui_directionRight, off) == mui_success &&
              Moves(context, root, a, mui_directionRight, b),
          "a link to a node that takes no focus: geometry");
    CHECK(muiNode_SetNeighbor(context, a, mui_directionRight, s_nullNode) == mui_success &&
              muiNode_GetNeighbor(context, a, mui_directionRight).index1 == 0 &&
              muiNode_SetNeighbor(context, a, mui_directionRight, s_nullNode) == mui_success,
          "removed, twice");
    // Room for two.
    CHECK(muiNode_SetNeighbor(context, a, mui_directionDown, off) == mui_success &&
              muiNode_SetNeighbor(context, b, mui_directionDown, off) == mui_success &&
              muiNode_SetNeighbor(context, b, mui_directionDown, c) == mui_success &&
              muiNode_SetNeighbor(context, c, mui_directionUp, a) == mui_errorCapacity,
          "full; changing a link takes no room");
    // A destroyed target falls back; a destroyed node's room comes back.
    CHECK(muiDestroyNode(context, c) == mui_success &&
              Moves(context, root, b, mui_directionDown, s_nullNode),
          "a target gone");
    CHECK(muiDestroyNode(context, a) == mui_success &&
              muiNode_SetNeighbor(context, off, mui_directionUp, b) == mui_success &&
              Same(muiNode_GetNeighbor(context, off, mui_directionUp), b),
          "room taken back");
    // A node made in a's slot has none of a's links.
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId again = s_nullNode;
    CHECK(muiCreateNode(context, &def, &again) == mui_success && again.index1 == a.index1 &&
              muiNode_GetNeighbor(context, again, mui_directionDown).index1 == 0,
          "the slot again");
    muiDestroyContext(context);
}

static void TestLinkOfSlot(void)
{
    // A node made in a destroyed node's slot has none of its links.
    muiContext* context = MakeContext(16);
    muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
    muiNodeId a = Box(context, root, 0.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId b = Box(context, root, 0.0f, 20.0f, 10.0f, 10.0f, mui_focusAll);
    CHECK(muiNode_SetNeighbor(context, a, mui_directionDown, a) == mui_success &&
              muiDestroyNode(context, a) == mui_success,
          "a linked, destroyed");
    muiNodeId again = Box(context, root, 0.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    Layout(context, root);
    CHECK(again.index1 == a.index1 &&
              muiNode_GetNeighbor(context, again, mui_directionDown).index1 == 0 &&
              Moves(context, root, again, mui_directionDown, b),
          "its slot again, unlinked");
    muiDestroyContext(context);
}

static void SetLayer(muiContext* context, muiNodeId node, muiLayerKind kind)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.layer = kind;
    CHECK(muiNode_SetInteractionValues(context, node, &values,
                                       MUI_PROPERTY_BIT(mui_propertyLayer)) == mui_success,
          "layer");
}

static void TestLayers(void)
{
    // A menu layer m beside the base's a and b; its m1 lies between them.
    muiContext* context = MakeContext(16);
    muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
    muiNodeId a = Box(context, root, 0.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId m = Box(context, root, 20.0f, 0.0f, 100.0f, 100.0f, mui_focusNone);
    muiNodeId m1 = Box(context, m, 0.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId m2 = Box(context, m, 0.0f, 50.0f, 10.0f, 10.0f, mui_focusAll);
    muiNodeId b = Box(context, root, 200.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    SetLayer(context, m, mui_layerActivation);
    Layout(context, root);
    CHECK(Moves(context, root, a, mui_directionRight, b) &&
              Moves(context, root, m1, mui_directionRight, s_nullNode) &&
              Moves(context, root, m1, mui_directionDown, m2) &&
              Moves(context, root, m2, mui_directionUp, m1),
          "each in its own layer, at its place");
    // Made modal, from the base it starts in the menu, and a link from
    // the menu out to the covered base falls back to geometry.
    SetLayer(context, m, mui_layerModal);
    Layout(context, root);
    CHECK(muiNode_SetNeighbor(context, m1, mui_directionDown, b) == mui_success &&
              Moves(context, root, m1, mui_directionDown, m2),
          "a link to a covered node");
    CHECK(muiFocus_MoveToward(context, root, 1, mui_directionRight) == mui_success &&
              Same(muiFocus_Get(context, 1), m1),
          "from no focus into the modal layer");
    muiDestroyContext(context);
}

// Android's FocusFinder, transcribed with its integer rectangles, as the
// oracle for random boxes.
typedef struct Rect
{
    int left;
    int top;
    int right;
    int bottom;
} Rect;

static bool OracleIsCandidate(Rect s, Rect d, muiDirection direction)
{
    switch (direction)
    {
    case mui_directionLeft:
        return (s.right > d.right || s.left >= d.right) && s.left > d.left;
    case mui_directionRight:
        return (s.left < d.left || s.right <= d.left) && s.right < d.right;
    case mui_directionUp:
        return (s.bottom > d.bottom || s.top >= d.bottom) && s.top > d.top;
    default:
        return (s.top < d.top || s.bottom <= d.top) && s.bottom < d.bottom;
    }
}

static bool OracleBeamsOverlap(muiDirection direction, Rect a, Rect b)
{
    return direction == mui_directionLeft || direction == mui_directionRight
               ? b.bottom > a.top && b.top < a.bottom
               : b.right > a.left && b.left < a.right;
}

static bool OracleIsToDirectionOf(muiDirection direction, Rect s, Rect d)
{
    switch (direction)
    {
    case mui_directionLeft:
        return s.left >= d.right;
    case mui_directionRight:
        return s.right <= d.left;
    case mui_directionUp:
        return s.top >= d.bottom;
    default:
        return s.bottom <= d.top;
    }
}

static long long OracleMajor(muiDirection direction, Rect s, Rect d)
{
    int raw = direction == mui_directionLeft    ? s.left - d.right
              : direction == mui_directionRight ? d.left - s.right
              : direction == mui_directionUp    ? s.top - d.bottom
                                                : d.top - s.bottom;
    return raw > 0 ? raw : 0;
}

static long long OracleFar(muiDirection direction, Rect s, Rect d)
{
    int raw = direction == mui_directionLeft    ? s.left - d.left
              : direction == mui_directionRight ? d.right - s.right
              : direction == mui_directionUp    ? s.top - d.top
                                                : d.bottom - s.bottom;
    return raw > 1 ? raw : 1;
}

static long long OracleMinor(muiDirection direction, Rect s, Rect d)
{
    int delta = direction == mui_directionLeft || direction == mui_directionRight
                    ? (s.top + (s.bottom - s.top) / 2) - (d.top + (d.bottom - d.top) / 2)
                    : (s.left + (s.right - s.left) / 2) - (d.left + (d.right - d.left) / 2);
    return delta < 0 ? -delta : delta;
}

static long long OracleWeighted(muiDirection direction, Rect s, Rect d)
{
    long long major = OracleMajor(direction, s, d);
    long long minor = OracleMinor(direction, s, d);
    return 13 * major * major + minor * minor;
}

static bool OracleBeamBeats(muiDirection direction, Rect s, Rect r1, Rect r2)
{
    bool in1 = OracleBeamsOverlap(direction, s, r1);
    bool in2 = OracleBeamsOverlap(direction, s, r2);
    if (in2 || !in1)
    {
        return false;
    }
    if (!OracleIsToDirectionOf(direction, s, r2))
    {
        return true;
    }
    if (direction == mui_directionLeft || direction == mui_directionRight)
    {
        return true;
    }
    return OracleMajor(direction, s, r1) < OracleFar(direction, s, r2);
}

static bool OracleIsBetter(muiDirection direction, Rect s, Rect r1, Rect r2)
{
    if (!OracleIsCandidate(s, r1, direction))
    {
        return false;
    }
    if (!OracleIsCandidate(s, r2, direction))
    {
        return true;
    }
    if (OracleBeamBeats(direction, s, r1, r2))
    {
        return true;
    }
    if (OracleBeamBeats(direction, s, r2, r1))
    {
        return false;
    }
    return OracleWeighted(direction, s, r1) < OracleWeighted(direction, s, r2);
}

typedef struct Random
{
    uint32_t state;
} Random;

static uint32_t NextRandom(Random* random, uint32_t below)
{
    random->state = random->state * 1664525u + 1013904223u;
    return (random->state >> 8) % below;
}

enum
{
    RANDOM_BOXES = 14
};

static void TestRandom(void)
{
    uint32_t moves = 0;
    for (uint32_t seed = 1; seed <= 200; seed++)
    {
        Random random = {seed};
        muiContext* context = MakeContext(16);
        muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 1000.0f, 1000.0f, mui_focusNone);
        muiNodeId nodes[RANDOM_BOXES];
        Rect rects[RANDOM_BOXES];
        bool reached[RANDOM_BOXES];
        for (int i = 0; i < RANDOM_BOXES; i++)
        {
            // Even places and sizes, empty ones too, so centers are whole as
            // Android's are and edges often touch.
            int x = (int)NextRandom(&random, 50) * 2;
            int y = (int)NextRandom(&random, 50) * 2;
            int width = 2 * (int)NextRandom(&random, 16);
            int height = 2 * (int)NextRandom(&random, 16);
            reached[i] = NextRandom(&random, 5) != 0;
            nodes[i] = Box(context, root, (float)x, (float)y, (float)width, (float)height,
                           reached[i] ? mui_focusAll : mui_focusNone);
            rects[i] = (Rect){x, y, x + width, y + height};
        }
        Layout(context, root);
        bool agree = true;
        for (int from = 0; agree && from < RANDOM_BOXES; from++)
        {
            for (muiDirection direction = 0; agree && direction <= mui_directionRight; direction++)
            {
                if (!reached[from])
                {
                    continue;
                }
                int best = -1;
                for (int i = 0; i < RANDOM_BOXES; i++)
                {
                    if (i != from && reached[i] &&
                        (best < 0 ? OracleIsCandidate(rects[from], rects[i], direction)
                                  : OracleIsBetter(direction, rects[from], rects[i], rects[best])))
                    {
                        best = i;
                    }
                }
                agree = Moves(context, root, nodes[from], direction,
                              best < 0 ? s_nullNode : nodes[best]);
                moves += best >= 0 ? 1 : 0;
            }
        }
        CHECK(agree, "moves agree with Android's focus search");
        muiDestroyContext(context);
    }
    CHECK(moves > 3000, "most searches find a node");
}

static void TestContract(void)
{
    muiContext* context = MakeContext(16);
    muiNodeId root = Box(context, s_nullNode, 0.0f, 0.0f, 100.0f, 100.0f, mui_focusNone);
    muiNodeId a = Box(context, root, 0.0f, 0.0f, 10.0f, 10.0f, mui_focusAll);
    Layout(context, root);
    uint64_t misuse = muiGetContextMisuse(context);
    CHECK(muiFocus_MoveToward(NULL, root, 0, mui_directionUp) == mui_errorInvalid &&
              muiFocus_MoveToward(context, s_nullNode, 0, mui_directionUp) == mui_errorInvalid &&
              muiFocus_MoveToward(context, root, MUI_MAX_PLAYERS, mui_directionUp) ==
                  mui_errorInvalid &&
              muiFocus_MoveToward(context, root, 0, mui_directionRight + 1) == mui_errorInvalid &&
              muiNode_SetNeighbor(NULL, a, mui_directionUp, a) == mui_errorInvalid &&
              muiNode_SetNeighbor(context, s_nullNode, mui_directionUp, a) == mui_errorInvalid &&
              muiNode_SetNeighbor(context, a, mui_directionRight + 1, a) == mui_errorInvalid &&
              muiGetContextMisuse(context) == misuse + 5,
          "outside the contract");
    CHECK(muiNode_GetNeighbor(NULL, a, mui_directionUp).index1 == 0 &&
              muiNode_GetNeighbor(context, s_nullNode, mui_directionUp).index1 == 0 &&
              muiNode_GetNeighbor(context, a, mui_directionRight + 1).index1 == 0,
          "reads outside the contract");
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId gone = s_nullNode;
    CHECK(muiCreateNode(context, &def, &gone) == mui_success &&
              muiDestroyNode(context, gone) == mui_success &&
              muiNode_SetNeighbor(context, gone, mui_directionUp, a) == mui_errorStale &&
              muiNode_SetNeighbor(context, a, mui_directionUp, gone) == mui_errorStale &&
              muiNode_GetNeighbor(context, gone, mui_directionUp).index1 == 0,
          "nodes gone");
    CHECK(muiDestroyNode(context, root) == mui_success &&
              muiFocus_MoveToward(context, root, 0, mui_directionUp) == mui_errorStale,
          "a root gone");
    muiDestroyContext(context);
}

int main(void)
{
    TestGrid();
    TestBeam();
    TestDistance();
    TestKinds();
    TestLinks();
    TestLinkOfSlot();
    TestLayers();
    TestRandom();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
