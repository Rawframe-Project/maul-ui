// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Interaction properties and hit testing (record mui-0007): the topmost
// node at a point in reverse paint order, hit modes, clips and rounded
// corners, pass-through, interaction values through classes and states,
// and calls outside the contract refused.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static const muiNodeId s_nullNode = {0, 0};

#define HIT_MODE     MUI_PROPERTY_BIT(mui_propertyHitMode)
#define PASS_THROUGH MUI_PROPERTY_BIT(mui_propertyPassThrough)

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

// A node of a size under parent (none for a root), placed absolutely at
// x, y in its parent's border box.
static muiNodeId Place(muiContext* context, muiNodeId parent, float x, float y, float width,
                       float height)
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
    return node;
}

static void Layout(muiContext* context, muiNodeId root)
{
    const muiLayoutInput input = {1000.0f, 1000.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(context, root, &input) == mui_success, "layout");
}

// Whether a point hits a node at a point in its border box.
static bool Hits(muiContext* context, muiNodeId root, float x, float y, muiNodeId node, float nodeX,
                 float nodeY)
{
    muiHit hit = {{9, 9}, -1.0f, -1.0f, false};
    return muiHitTest(context, root, x, y, &hit) == mui_success && hit.node.index1 == node.index1 &&
           hit.node.generation == node.generation &&
           (node.index1 == 0 || (hit.x == nodeX && hit.y == nodeY));
}

static void SetHitMode(muiContext* context, muiNodeId node, muiHitMode mode)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.hitMode = mode;
    CHECK(muiNode_SetInteractionValues(context, node, &values, HIT_MODE) == mui_success,
          "hit mode");
}

static muiContext* MakeContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    return context;
}

static void TestOrder(void)
{
    // a at 10, 10 and b at 40, 40 overlap; b's child c is at 45, 45.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 100.0f, 100.0f);
    muiNodeId a = Place(context, root, 10.0f, 10.0f, 50.0f, 50.0f);
    muiNodeId b = Place(context, root, 40.0f, 40.0f, 50.0f, 50.0f);
    muiNodeId c = Place(context, b, 5.0f, 5.0f, 10.0f, 10.0f);
    Layout(context, root);
    CHECK(Hits(context, root, 5.0f, 5.0f, root, 5.0f, 5.0f) &&
              Hits(context, root, 20.0f, 20.0f, a, 10.0f, 10.0f) &&
              Hits(context, root, 45.0f, 45.0f, c, 0.0f, 0.0f) &&
              Hits(context, root, 55.0f, 55.0f, b, 15.0f, 15.0f) &&
              Hits(context, root, 89.5f, 50.0f, b, 49.5f, 10.0f),
          "the last painted first, children over their parent");
    CHECK(Hits(context, root, 90.0f, 50.0f, root, 90.0f, 50.0f) &&
              Hits(context, root, 200.0f, 5.0f, s_nullNode, 0.0f, 0.0f) &&
              Hits(context, root, -0.5f, 5.0f, s_nullNode, 0.0f, 0.0f),
          "boxes half open; nothing outside the root");
    muiHit miss = {{9, 9}, 1.0f, 1.0f, false};
    CHECK(muiHitTest(context, root, 200.0f, 200.0f, &miss) == mui_success && miss.passThrough &&
              miss.x == 0.0f,
          "nothing hit passes through");
    // A subtree root of its own: b at its own rectangle; the walk keeps
    // inside it, though b's parent has a sibling after it.
    muiNodeId outer = Place(context, s_nullNode, 0.0f, 0.0f, 100.0f, 100.0f);
    muiNodeId p = Place(context, outer, 0.0f, 0.0f, 50.0f, 50.0f);
    muiNodeId inner = Place(context, p, 0.0f, 0.0f, 10.0f, 10.0f);
    muiNodeId q = Place(context, outer, 60.0f, 60.0f, 20.0f, 20.0f);
    Layout(context, outer);
    CHECK(Hits(context, outer, 70.0f, 70.0f, q, 10.0f, 10.0f) &&
              Hits(context, inner, 70.0f, 70.0f, s_nullNode, 0.0f, 0.0f) &&
              Hits(context, p, 5.0f, 5.0f, inner, 5.0f, 5.0f),
          "a subtree's walk stays in it");
    CHECK(Hits(context, b, 46.0f, 46.0f, c, 1.0f, 1.0f) &&
              Hits(context, b, 20.0f, 20.0f, s_nullNode, 0.0f, 0.0f),
          "a subtree");
    // b lets points through where it has no children, or entirely.
    SetHitMode(context, b, mui_hitChildren);
    CHECK(Hits(context, root, 55.0f, 55.0f, a, 45.0f, 45.0f) &&
              Hits(context, root, 46.0f, 46.0f, c, 1.0f, 1.0f) &&
              Hits(context, root, 80.0f, 80.0f, root, 80.0f, 80.0f),
          "children alone");
    SetHitMode(context, b, mui_hitNone);
    CHECK(Hits(context, root, 46.0f, 46.0f, a, 36.0f, 36.0f), "none of the subtree");
    SetHitMode(context, b, mui_hitAuto);
    // Opacity does not matter.
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.opacity = 0.0f;
    CHECK(muiNode_SetVisualValues(context, b, &visual, MUI_PROPERTY_BIT(mui_propertyOpacity)) ==
                  mui_success &&
              Hits(context, root, 55.0f, 55.0f, b, 15.0f, 15.0f),
          "a transparent node is hit");
    // Pass-through is the hit node's own.
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.passThrough = true;
    muiHit hit = {{0, 0}, 0.0f, 0.0f, false};
    CHECK(muiNode_SetInteractionValues(context, a, &values, PASS_THROUGH) == mui_success &&
              muiHitTest(context, root, 20.0f, 20.0f, &hit) == mui_success && hit.passThrough &&
              muiHitTest(context, root, 55.0f, 55.0f, &hit) == mui_success && !hit.passThrough,
          "passing through");
    muiDestroyContext(context);
}

static void TestClips(void)
{
    // a clips at 10, 10 by 40 with rounded corners of 20; its child d
    // reaches past it to 80.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 100.0f, 100.0f);
    muiNodeId a = Place(context, root, 10.0f, 10.0f, 40.0f, 40.0f);
    muiNodeId d = Place(context, a, 20.0f, 20.0f, 50.0f, 50.0f);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.radius.topStart = Length(20.0f);
    CHECK(muiNode_SetVisualValues(context, a, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyRadiusTopStart)) == mui_success,
          "rounded");
    Layout(context, root);
    CHECK(Hits(context, root, 70.0f, 70.0f, d, 40.0f, 40.0f), "unclipped, the child past it");
    CHECK(Hits(context, root, 12.0f, 12.0f, root, 12.0f, 12.0f) &&
              Hits(context, root, 18.0f, 25.0f, a, 8.0f, 15.0f) &&
              Hits(context, root, 30.0f, 11.0f, a, 20.0f, 1.0f) &&
              Hits(context, root, 11.0f, 40.0f, a, 1.0f, 30.0f),
          "outside a rounded corner, inside it, past its square either way");
    visual.clip = true;
    CHECK(muiNode_SetVisualValues(context, a, &visual, MUI_PROPERTY_BIT(mui_propertyClip)) ==
              mui_success,
          "clipped");
    Layout(context, root);
    CHECK(Hits(context, root, 70.0f, 70.0f, root, 70.0f, 70.0f) &&
              Hits(context, root, 45.0f, 45.0f, d, 15.0f, 15.0f),
          "the child cut by the clip");
    muiDestroyContext(context);
}

static void TestStyled(void)
{
    // A class lets points through its nodes' children while hovered.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 100.0f, 100.0f);
    muiNodeId a = Place(context, root, 0.0f, 0.0f, 50.0f, 50.0f);
    muiStyleId style = {0, 0};
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.hitMode = mui_hitNone;
    values.passThrough = true;
    muiInteractionStyle read = muiDefaultInteractionStyle();
    muiPropertyMask mask = 0;
    CHECK(muiCreateStyle(context, &style) == mui_success &&
              muiStyle_SetInteractionValues(context, style, mui_variantHovered, &values,
                                            HIT_MODE) == mui_success &&
              muiStyle_GetInteractionValues(context, style, mui_variantHovered, &read, &mask) ==
                  mui_success &&
              read.hitMode == mui_hitNone && !read.passThrough && mask == HIT_MODE &&
              muiNode_SetClasses(context, a, &style, 1) == mui_success,
          "a class");
    Layout(context, root);
    CHECK(Hits(context, root, 5.0f, 5.0f, a, 5.0f, 5.0f), "not hovered");
    CHECK(muiNode_SetStates(context, a, mui_stateHovered) == mui_success, "hovered");
    Layout(context, root);
    CHECK(muiNode_GetInteractionStyle(context, a, &read) == mui_success &&
              read.hitMode == mui_hitNone && Hits(context, root, 5.0f, 5.0f, root, 5.0f, 5.0f),
          "hovered: passed over");
    // A class that sets pass-through alone leaves the hit mode its
    // default, though a sibling resolved before it has another.
    muiNodeId first = Place(context, root, 60.0f, 0.0f, 10.0f, 10.0f);
    muiNodeId second = Place(context, root, 80.0f, 0.0f, 10.0f, 10.0f);
    muiStyleId none = {0, 0};
    muiStyleId passing = {0, 0};
    muiInteractionStyle through = muiDefaultInteractionStyle();
    through.passThrough = true;
    CHECK(muiCreateStyle(context, &none) == mui_success &&
              muiCreateStyle(context, &passing) == mui_success &&
              muiStyle_SetInteractionValues(context, none, mui_variantBase, &values, HIT_MODE) ==
                  mui_success &&
              muiStyle_SetInteractionValues(context, passing, mui_variantBase, &through,
                                            PASS_THROUGH) == mui_success &&
              muiNode_SetClasses(context, first, &none, 1) == mui_success &&
              muiNode_SetClasses(context, second, &passing, 1) == mui_success,
          "two classes");
    Layout(context, root);
    CHECK(muiNode_GetInteractionStyle(context, second, &read) == mui_success &&
              read.hitMode == mui_hitAuto && read.passThrough &&
              Hits(context, root, 85.0f, 5.0f, second, 5.0f, 5.0f) &&
              Hits(context, root, 65.0f, 5.0f, root, 65.0f, 5.0f),
          "the defaults under a class's values");
    // A direct write wins over the class.
    SetHitMode(context, a, mui_hitAuto);
    Layout(context, root);
    CHECK(Hits(context, root, 5.0f, 5.0f, a, 5.0f, 5.0f), "written directly");
    CHECK(muiNode_ResetProperties(context, a, mui_groupInteraction, HIT_MODE) == mui_success,
          "reset");
    Layout(context, root);
    CHECK(Hits(context, root, 5.0f, 5.0f, root, 5.0f, 5.0f), "the class's again");
    muiDestroyContext(context);
}

static uint32_t Next(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

// Random trees of nodes at whole positions, each with a background and
// some of them layers, so painting draws a box per node, in paint order,
// that snapping keeps where layout put it: the node hit is the one of the
// last box holding the point.
static void TestAgainstPainting(void)
{
    uint32_t state = 5;
    for (int round = 0; round < 40; round++)
    {
        muiContext* context = MakeContext();
        muiNodeId nodes[24];
        nodes[0] = Place(context, s_nullNode, 0.0f, 0.0f, 120.0f, 120.0f);
        muiVisualStyle visual = muiDefaultVisualStyle();
        visual.background = (muiColor){0.5f, 0.5f, 0.5f, 1.0f};
        for (uint32_t i = 1; i < 24; i++)
        {
            muiNodeId parent = nodes[Next(&state) % i];
            nodes[i] =
                Place(context, parent, (float)(Next(&state) % 60), (float)(Next(&state) % 60),
                      (float)(1 + Next(&state) % 60), (float)(1 + Next(&state) % 60));
        }
        for (uint32_t i = 0; i < 24; i++)
        {
            CHECK(muiNode_SetVisualValues(context, nodes[i], &visual,
                                          MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
                  "background");
            // Some are layers, activation or overlay, in random order.
            uint32_t kind = Next(&state) % 8;
            if (i != 0 && kind < 2)
            {
                muiInteractionStyle values = muiDefaultInteractionStyle();
                values.layer = kind == 0 ? mui_layerActivation : mui_layerOverlay;
                CHECK(muiNode_SetInteractionValues(context, nodes[i], &values,
                                                   MUI_PROPERTY_BIT(mui_propertyLayer)) ==
                          mui_success,
                      "a layer");
            }
        }
        Layout(context, nodes[0]);
        const muiDrawInput input = {1, 1.0f, NULL, NULL};
        muiDrawList list;
        CHECK(muiBuildDrawList(context, nodes[0], &input) == mui_success &&
                  muiGetDrawList(context, &list) == mui_success,
              "painted");
        for (int k = 0; k < 50; k++)
        {
            float x = (float)(Next(&state) % 1300) * 0.1f;
            float y = (float)(Next(&state) % 1300) * 0.1f;
            const muiRect* box = NULL;
            for (uint32_t i = 0; i < list.commandCount; i++)
            {
                const muiRect* rect = &list.commands[i].box.rect;
                bool inside = x >= rect->x && y >= rect->y && x < rect->x + rect->width &&
                              y < rect->y + rect->height;
                box = inside ? rect : box;
            }
            muiHit hit = {{0, 0}, 0.0f, 0.0f, false};
            CHECK(muiHitTest(context, nodes[0], x, y, &hit) == mui_success &&
                      (box == NULL ? hit.node.index1 == 0
                                   : hit.node.index1 != 0 && fabsf(x - hit.x - box->x) < 1e-4f &&
                                         fabsf(y - hit.y - box->y) < 1e-4f &&
                                         muiNode_GetRect(context, hit.node).width == box->width),
                  "the last box painted at the point");
        }
        muiDestroyContext(context);
    }
}

#define LAYER MUI_PROPERTY_BIT(mui_propertyLayer)

static void SetLayer(muiContext* context, muiNodeId node, muiLayerKind kind)
{
    muiInteractionStyle values = muiDefaultInteractionStyle();
    values.layer = kind;
    CHECK(muiNode_SetInteractionValues(context, node, &values, LAYER) == mui_success, "layer");
}

// Gives a node a background, so it draws a box.
static void Paint(muiContext* context, muiNodeId node, float red)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){red, 0.0f, 0.0f, 1.0f};
    CHECK(muiNode_SetVisualValues(context, node, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "background");
}

static muiDrawList Build(muiContext* context, muiNodeId root, float scale)
{
    const muiDrawInput input = {1, scale, NULL, NULL};
    muiDrawList list = {0};
    CHECK(muiBuildDrawList(context, root, &input) == mui_success &&
              muiGetDrawList(context, &list) == mui_success,
          "painted");
    return list;
}

static void TestLayers(void)
{
    // p clips at 10, 10 with opacity a half; its child l, a layer, reaches
    // past it; s comes after p.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 200.0f, 200.0f);
    muiNodeId p = Place(context, root, 10.0f, 10.0f, 50.0f, 50.0f);
    muiNodeId l = Place(context, p, 20.0f, 20.0f, 100.0f, 100.0f);
    muiNodeId s = Place(context, root, 100.0f, 100.0f, 50.0f, 50.0f);
    Paint(context, root, 1.0f);
    Paint(context, p, 1.0f);
    Paint(context, l, 1.0f);
    Paint(context, s, 1.0f);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.clip = true;
    visual.opacity = 0.5f;
    CHECK(muiNode_SetVisualValues(context, p, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyClip) |
                                      MUI_PROPERTY_BIT(mui_propertyOpacity)) == mui_success,
          "clipped");
    Layout(context, root);
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 4 && list.commands[2].box.rect.x == 30.0f &&
              list.commands[2].clip != 0 && list.commands[2].box.fill.a == 0.5f,
          "in place, clipped and faded");
    SetLayer(context, l, mui_layerActivation);
    Layout(context, root);
    list = Build(context, root, 1.0f);
    const muiDrawCommand* last = &list.commands[3];
    CHECK(list.commandCount == 4 && list.commands[2].box.rect.x == 100.0f &&
              last->box.rect.x == 30.0f && last->box.rect.width == 100.0f && last->clip == 0 &&
              last->box.fill.a == 1.0f,
          "a layer after the content, unclipped and opaque");
    // Hits: l over s, the base where l is not.
    CHECK(Hits(context, root, 110.0f, 110.0f, l, 80.0f, 80.0f) &&
              Hits(context, root, 140.0f, 140.0f, s, 40.0f, 40.0f) &&
              Hits(context, root, 15.0f, 15.0f, p, 5.0f, 5.0f) &&
              Hits(context, l, 31.0f, 31.0f, l, 11.0f, 11.0f),
          "hits through a layer");
    muiDestroyContext(context);
}

static void CountPaint(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                       muiDrawSink* sink)
{
    (void)nodeId;
    (void)hostKey;
    (void)width;
    (void)height;
    (void)sink;
    (*(int*)user)++;
}

static void TestLayerRetained(void)
{
    // l, a layer of host content inside p, is copied from the last list
    // when only a sibling of p changes, not painted again.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 200.0f, 200.0f);
    muiNodeId p = Place(context, root, 10.0f, 10.0f, 50.0f, 50.0f);
    muiNodeId l = Place(context, p, 5.0f, 5.0f, 20.0f, 20.0f);
    muiNodeId s = Place(context, root, 100.0f, 100.0f, 50.0f, 50.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, l, &layout, MUI_PROPERTY_BIT(mui_propertyContent)) ==
              mui_success,
          "host content");
    SetLayer(context, l, mui_layerActivation);
    Layout(context, root);
    int calls = 0;
    const muiDrawInput input = {1, 1.0f, CountPaint, &calls};
    CHECK(muiBuildDrawList(context, root, &input) == mui_success && calls == 1, "painted");
    Paint(context, s, 0.5f);
    Layout(context, root);
    CHECK(muiBuildDrawList(context, root, &input) == mui_success && calls == 1, "copied");
    CHECK(muiNode_RaiseLayer(context, l) == mui_success &&
              muiBuildDrawList(context, root, &input) == mui_success && calls == 2,
          "raised: painted again");
    muiDestroyContext(context);
}

static void TestLayerOrder(void)
{
    // Four layers under the root, siblings in reverse of how they are
    // activated: an overlay first, then a, b and a modal m.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 200.0f, 200.0f);
    muiNodeId m = Place(context, root, 150.0f, 150.0f, 40.0f, 40.0f);
    muiNodeId b = Place(context, root, 20.0f, 20.0f, 40.0f, 40.0f);
    muiNodeId a = Place(context, root, 10.0f, 10.0f, 40.0f, 40.0f);
    muiNodeId overlay = Place(context, root, 0.0f, 0.0f, 20.0f, 20.0f);
    // a's child n, a layer of its own, activated after a.
    muiNodeId n = Place(context, a, 5.0f, 5.0f, 10.0f, 10.0f);
    const muiNodeId nodes[6] = {root, m, b, a, overlay, n};
    for (int i = 0; i < 6; i++)
    {
        Paint(context, nodes[i], 1.0f);
    }
    SetLayer(context, overlay, mui_layerOverlay);
    SetLayer(context, a, mui_layerActivation);
    SetLayer(context, b, mui_layerActivation);
    SetLayer(context, n, mui_layerActivation);
    Layout(context, root);
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 6, "six boxes");
    // m stays in the base; then a, b, n by activation, the overlay last.
    CHECK(list.commands[1].box.rect.x == 150.0f && list.commands[2].box.rect.x == 10.0f &&
              list.commands[3].box.rect.x == 20.0f && list.commands[4].box.rect.x == 15.0f &&
              list.commands[5].box.rect.x == 0.0f,
          "activation order, the overlay band last");
    // Raising a puts it over b, its child n staying where it was.
    CHECK(muiNode_RaiseLayer(context, a) == mui_success, "raised");
    list = Build(context, root, 1.0f);
    CHECK(list.commands[2].box.rect.x == 20.0f && list.commands[3].box.rect.x == 15.0f &&
              list.commands[4].box.rect.x == 10.0f && list.commands[5].box.rect.x == 0.0f,
          "raised over b");
    // The retained list is the bytes a whole build gives.
    muiDrawCommand kept[6];
    memcpy(kept, list.commands, sizeof kept);
    (void)Build(context, root, 2.0f);
    list = Build(context, root, 1.0f);
    CHECK(memcmp(kept, list.commands, sizeof kept) == 0, "retained as a whole build");
    // Hits from the top: the overlay, then a over b.
    CHECK(Hits(context, root, 5.0f, 5.0f, overlay, 5.0f, 5.0f) &&
              Hits(context, root, 25.0f, 25.0f, a, 15.0f, 15.0f) &&
              Hits(context, root, 22.0f, 22.0f, a, 12.0f, 12.0f) &&
              Hits(context, root, 55.0f, 55.0f, b, 35.0f, 35.0f),
          "hits from the top layer");
    // A modal layer: points it misses reach nothing below it.
    SetLayer(context, m, mui_layerModal);
    Layout(context, root);
    muiHit hit = {{0, 0}, 0.0f, 0.0f, true};
    CHECK(Hits(context, root, 160.0f, 160.0f, m, 10.0f, 10.0f) &&
              muiHitTest(context, root, 25.0f, 25.0f, &hit) == mui_success &&
              hit.node.index1 == m.index1 && hit.x == -125.0f && !hit.passThrough &&
              Hits(context, root, 5.0f, 5.0f, overlay, 5.0f, 5.0f),
          "a modal layer blocks below it, not the overlay band");
    // No longer a layer: in place again.
    SetLayer(context, m, mui_layerNone);
    SetLayer(context, a, mui_layerNone);
    Layout(context, root);
    list = Build(context, root, 1.0f);
    CHECK(list.commands[1].box.rect.x == 150.0f && list.commands[2].box.rect.x == 10.0f &&
              list.commands[3].box.rect.x == 20.0f && list.commands[4].box.rect.x == 15.0f,
          "a back in place, n still above it");
    muiDestroyContext(context);
}

static void TestLayerLimit(void)
{
    // Room for one layer: a second stays in place.
    muiContextDef def = muiDefaultContextDef();
    def.limits.layers = 1;
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 100.0f, 100.0f);
    // c comes before b in the tree, after it as a layer.
    muiNodeId a = Place(context, root, 10.0f, 0.0f, 10.0f, 10.0f);
    muiNodeId c = Place(context, root, 30.0f, 0.0f, 10.0f, 10.0f);
    muiNodeId b = Place(context, root, 20.0f, 0.0f, 10.0f, 10.0f);
    Paint(context, a, 1.0f);
    Paint(context, b, 1.0f);
    Paint(context, c, 1.0f);
    SetLayer(context, a, mui_layerActivation);
    SetLayer(context, b, mui_layerActivation);
    Layout(context, root);
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.commands[0].box.rect.x == 30.0f && list.commands[1].box.rect.x == 20.0f &&
              list.commands[2].box.rect.x == 10.0f &&
              muiNode_RaiseLayer(context, b) == mui_errorInvalid,
          "past the limit, in place");
    // A layer destroyed frees its room.
    CHECK(muiDestroyNode(context, a) == mui_success, "destroyed");
    SetLayer(context, c, mui_layerActivation);
    Layout(context, root);
    list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 2 && list.commands[0].box.rect.x == 20.0f &&
              list.commands[1].box.rect.x == 30.0f,
          "the room of a layer gone");
    muiDestroyContext(context);
}

static void TestLayerEdges(void)
{
    // a and b are layers; s is not, nor under one.
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 200.0f, 200.0f);
    muiNodeId a = Place(context, root, 10.0f, 10.0f, 50.0f, 50.0f);
    muiNodeId b = Place(context, root, 20.0f, 20.0f, 50.0f, 50.0f);
    muiNodeId s = Place(context, root, 100.0f, 100.0f, 50.0f, 50.0f);
    Paint(context, a, 1.0f);
    Paint(context, b, 1.0f);
    Paint(context, s, 1.0f);
    SetLayer(context, a, mui_layerActivation);
    SetLayer(context, b, mui_layerActivation);
    Layout(context, root);
    // Restyling a layer leaves its place: a stays under b.
    Paint(context, a, 0.5f);
    Layout(context, root);
    muiDrawList list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 3 && list.commands[1].box.rect.x == 10.0f &&
              list.commands[2].box.rect.x == 20.0f,
          "restyled, not raised");
    // A list or a hit test of a layer, or of a subtree no layer is in.
    list = Build(context, a, 1.0f);
    CHECK(list.commandCount == 1, "a layer's own list");
    list = Build(context, s, 1.0f);
    CHECK(list.commandCount == 1 && Hits(context, s, 30.0f, 30.0f, s_nullNode, 0.0f, 0.0f),
          "a subtree without layers");
    SetLayer(context, a, mui_layerModal);
    Layout(context, root);
    CHECK(Hits(context, a, 100.0f, 100.0f, s_nullNode, 0.0f, 0.0f),
          "a modal root of its own walk blocks nothing");
    // A layer destroyed: its slot, taken by a new node, is not walked as
    // a layer twice.
    CHECK(muiDestroyNode(context, a) == mui_success, "destroyed");
    muiNodeId d = Place(context, root, 5.0f, 5.0f, 10.0f, 10.0f);
    Paint(context, d, 1.0f);
    Layout(context, root);
    list = Build(context, root, 1.0f);
    CHECK(d.index1 == a.index1 && list.commandCount == 3, "the slot again, in place");
    SetLayer(context, d, mui_layerActivation);
    Layout(context, root);
    list = Build(context, root, 1.0f);
    CHECK(list.commandCount == 3 && list.commands[2].box.rect.x == 5.0f, "the slot as a layer");
    muiDestroyContext(context);
}

static void TestContract(void)
{
    muiContext* context = MakeContext();
    muiNodeId root = Place(context, s_nullNode, 0.0f, 0.0f, 10.0f, 10.0f);
    Layout(context, root);
    muiHit hit = {{9, 9}, 9.0f, 9.0f, false};
    CHECK(muiHitTest(NULL, root, 1.0f, 1.0f, &hit) == mui_errorInvalid &&
              muiHitTest(context, s_nullNode, 1.0f, 1.0f, &hit) == mui_errorInvalid &&
              muiHitTest(context, root, NAN, 1.0f, &hit) == mui_errorInvalid &&
              muiHitTest(context, root, 1.0f, INFINITY, &hit) == mui_errorInvalid &&
              muiHitTest(context, root, 1.0f, 1.0f, NULL) == mui_errorInvalid &&
              hit.node.index1 == 9 && hit.x == 9.0f,
          "hit tests outside the contract");
    muiInteractionStyle values = muiDefaultInteractionStyle();
    CHECK(values.hitMode == mui_hitAuto && !values.passThrough, "the defaults");
    values.hitMode = 3;
    muiStyleId style = {0, 0};
    CHECK(muiCreateStyle(context, &style) == mui_success &&
              muiNode_SetInteractionValues(context, root, &values, HIT_MODE) == mui_errorInvalid &&
              muiStyle_SetInteractionValues(context, style, mui_variantBase, &values, HIT_MODE) ==
                  mui_errorInvalid &&
              muiNode_SetInteractionValues(context, root, NULL, HIT_MODE) == mui_errorInvalid &&
              muiNode_SetInteractionValues(NULL, root, &values, HIT_MODE) == mui_errorInvalid &&
              muiNode_SetInteractionValues(context, root, &values,
                                           MUI_PROPERTY_BIT(mui_propertyExitLayout + 1)) ==
                  mui_errorInvalid &&
              muiStyle_SetInteractionValues(context, style, mui_variantBase, NULL, HIT_MODE) ==
                  mui_errorInvalid &&
              muiStyle_GetInteractionValues(context, style, mui_variantBase, NULL, NULL) ==
                  mui_errorInvalid,
          "values outside the contract");
    CHECK(muiNode_GetInteractionStyle(NULL, root, &values) == mui_errorInvalid &&
              muiNode_GetInteractionStyle(context, s_nullNode, &values) == mui_errorInvalid &&
              muiNode_GetInteractionStyle(context, root, NULL) == mui_errorInvalid,
          "reads outside the contract");
    CHECK(muiNode_RaiseLayer(NULL, root) == mui_errorInvalid &&
              muiNode_RaiseLayer(context, s_nullNode) == mui_errorInvalid &&
              muiNode_RaiseLayer(context, root) == mui_errorInvalid,
          "raising outside the contract");
    muiNodeId gone = root;
    CHECK(muiDestroyNode(context, root) == mui_success &&
              muiHitTest(context, gone, 1.0f, 1.0f, &hit) == mui_errorStale &&
              muiNode_GetInteractionStyle(context, gone, &values) == mui_errorStale &&
              muiNode_SetInteractionValues(context, gone, &values, PASS_THROUGH) == mui_errorStale,
          "a node gone");
    muiDestroyContext(context);
}

int main(void)
{
    TestOrder();
    TestClips();
    TestStyled();
    TestAgainstPainting();
    TestLayers();
    TestLayerRetained();
    TestLayerOrder();
    TestLayerLimit();
    TestLayerEdges();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
