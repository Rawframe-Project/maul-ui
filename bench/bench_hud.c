// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A HUD's frames (requirements section 11, "a thousands-element HUD is
// cheap"): twelve panels of 34 rows each, a row an icon, a label of host
// content and a bar with its fill, 2,053 nodes. Each frame changes 50
// rows at random: a label's text, a bar's fill or an icon's opacity;
// every 30 frames another panel is hovered, which eases its opacity over
// 250 ms. A frame is a layout and a draw list. Prints the median, 99th
// percentile and worst frame in microseconds over 2,000 frames, and per
// frame the nodes styled, sizes solved, host measures and nodes painted
// (muiGetWorkCounts), which do not depend on the machine.

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum
{
    PANELS = 12,
    ROWS = 34,
    ITEMS = PANELS * ROWS,
    FRAMES = 2000,
    CHANGES = 50,
    NODE_LIMIT = 4096
};

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static void Check(muiResult result, const char* what)
{
    if (result != mui_success)
    {
        fprintf(stderr, "%s failed: %d\n", what, (int)result);
        exit(1);
    }
}

// Each label's characters, by its key, 7 units each and 16 tall.
static uint32_t s_characters[ITEMS + 1];

static muiSize MeasureLabel(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                            muiMeasureAxis height)
{
    (void)user;
    (void)nodeId;
    (void)width;
    (void)height;
    return (muiSize){(float)s_characters[hostKey] * 7.0f, 16.0f};
}

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

static muiNodeId Add(muiContext* context, muiNodeId parent, const muiLayoutStyle* layout,
                     muiPropertyMask mask, uint64_t hostKey)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = hostKey;
    muiNodeId node = {0, 0};
    Check(muiCreateNode(context, &def, &node), "create");
    Check(muiNode_SetLayoutValues(context, node, layout, mask), "layout");
    if (parent.index1 != 0)
    {
        Check(muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}), "insert");
    }
    return node;
}

static void Paint(muiContext* context, muiNodeId node, muiColor color)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = color;
    Check(muiNode_SetVisualValues(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyBackground)),
          "background");
}

typedef struct Hud
{
    muiContext* context;
    muiNodeId root;
    muiNodeId panels[PANELS];
    muiNodeId icons[ITEMS];
    muiNodeId labels[ITEMS];
    muiNodeId fills[ITEMS];
} Hud;

// A panel's class: hovered, its opacity eases to 0.6 over 250 ms.
static muiStyleId MakePanelClass(muiContext* context)
{
    muiStyleId panel = {0, 0};
    Check(muiCreateStyle(context, &panel), "class");
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.opacity = 0.6f;
    Check(muiStyle_SetVisualValues(context, panel, mui_variantHovered, &visual,
                                   MUI_PROPERTY_BIT(mui_propertyOpacity)),
          "hovered");
    muiTransitionDef def = muiDefaultTransitionDef();
    muiTransitionId ease = {0, 0};
    Check(muiCreateTransition(context, &def, &ease), "transition");
    Check(muiStyle_SetTransition(context, panel, mui_variantBase, ease, mui_groupVisual,
                                 MUI_PROPERTY_BIT(mui_propertyOpacity)),
          "eased");
    return panel;
}

static void AddRow(Hud* hud, muiNodeId panel, uint32_t item)
{
    muiContext* context = hud->context;
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.alignItems = mui_alignCenter;
    row.container.columnGap = 6.0f;
    muiNodeId line =
        Add(context, panel, &row,
            MUI_PROPERTY_BIT(mui_propertyAlignItems) | MUI_PROPERTY_BIT(mui_propertyColumnGap), 0);
    muiLayoutStyle icon = muiDefaultLayoutStyle();
    icon.sizing.width = Length(16.0f);
    icon.sizing.height = Length(16.0f);
    hud->icons[item] =
        Add(context, line, &icon,
            MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight), 0);
    Paint(context, hud->icons[item], (muiColor){0.9f, 0.7f, 0.2f, 1.0f});
    muiLayoutStyle label = muiDefaultLayoutStyle();
    label.content = mui_contentHost;
    label.item.grow = 1.0f;
    hud->labels[item] =
        Add(context, line, &label,
            MUI_PROPERTY_BIT(mui_propertyContent) | MUI_PROPERTY_BIT(mui_propertyGrow), item + 1);
    s_characters[item + 1] = 4 + item % 12;
    muiLayoutStyle bar = muiDefaultLayoutStyle();
    bar.sizing.width = Length(80.0f);
    bar.sizing.height = Length(6.0f);
    muiNodeId track =
        Add(context, line, &bar,
            MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight), 0);
    Paint(context, track, (muiColor){0.2f, 0.2f, 0.2f, 1.0f});
    muiLayoutStyle fill = muiDefaultLayoutStyle();
    fill.sizing.width = (muiDimension){0.5f, 0.0f, mui_dimensionValue};
    hud->fills[item] = Add(context, track, &fill, MUI_PROPERTY_BIT(mui_propertyWidth), 0);
    Paint(context, hud->fills[item], (muiColor){0.3f, 0.8f, 0.3f, 1.0f});
}

static void MakeHud(Hud* hud)
{
    muiContextDef def = muiDefaultContextDef();
    def.limits.nodes = NODE_LIMIT;
    def.limits.drawCommands = NODE_LIMIT;
    Check(muiCreateContext(&def, &hud->context), "context");
    muiContext* context = hud->context;
    muiLayoutStyle root = muiDefaultLayoutStyle();
    root.sizing.width = Length(1280.0f);
    root.sizing.height = Length(720.0f);
    root.container.wrap = mui_wrapWrap;
    root.container.columnGap = 8.0f;
    hud->root =
        Add(context, (muiNodeId){0, 0}, &root,
            MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight) |
                MUI_PROPERTY_BIT(mui_propertyFlexWrap) | MUI_PROPERTY_BIT(mui_propertyColumnGap),
            0);
    muiStyleId panelClass = MakePanelClass(context);
    muiLayoutStyle panel = muiDefaultLayoutStyle();
    panel.container.direction = mui_flexColumn;
    panel.sizing.width = Length(300.0f);
    panel.padding = (muiEdges){8.0f, 8.0f, 8.0f, 8.0f};
    for (uint32_t p = 0; p < PANELS; p++)
    {
        hud->panels[p] =
            Add(context, hud->root, &panel,
                MUI_PROPERTY_BIT(mui_propertyFlexDirection) | MUI_PROPERTY_BIT(mui_propertyWidth) |
                    MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                    MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
                    MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
                    MUI_PROPERTY_BIT(mui_propertyPaddingBottom),
                0);
        Paint(context, hud->panels[p], (muiColor){0.1f, 0.1f, 0.12f, 0.8f});
        Check(muiNode_SetClasses(context, hud->panels[p], &panelClass, 1), "panel class");
        for (uint32_t r = 0; r < ROWS; r++)
        {
            AddRow(hud, hud->panels[p], p * ROWS + r);
        }
    }
}

static uint32_t Next(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

// The host's edits of a frame: 50 rows' text, fill or icon opacity, and
// every 30 frames the next panel hovered.
static void Change(Hud* hud, uint32_t frame, uint32_t* state)
{
    muiContext* context = hud->context;
    for (int i = 0; i < CHANGES; i++)
    {
        uint32_t item = Next(state) % ITEMS;
        switch (Next(state) % 3)
        {
        case 0:
            s_characters[item + 1] = 4 + Next(state) % 12;
            Check(muiNode_MarkContentChanged(context, hud->labels[item]), "text");
            break;
        case 1:
        {
            muiLayoutStyle fill = muiDefaultLayoutStyle();
            fill.sizing.width =
                (muiDimension){(float)(Next(state) % 101) * 0.01f, 0.0f, mui_dimensionValue};
            Check(muiNode_SetLayoutValues(context, hud->fills[item], &fill,
                                          MUI_PROPERTY_BIT(mui_propertyWidth)),
                  "fill");
            break;
        }
        default:
        {
            muiVisualStyle icon = muiDefaultVisualStyle();
            icon.opacity = (float)(Next(state) % 5 + 1) * 0.2f;
            Check(muiNode_SetVisualValues(context, hud->icons[item], &icon,
                                          MUI_PROPERTY_BIT(mui_propertyOpacity)),
                  "icon");
            break;
        }
        }
    }
    if (frame % 30 == 0)
    {
        uint32_t panel = frame / 30 % PANELS;
        Check(muiNode_SetStates(context, hud->panels[(panel + PANELS - 1) % PANELS], 0), "unhover");
        Check(muiNode_SetStates(context, hud->panels[panel], mui_stateHovered), "hover");
    }
}

static int Ascending(const void* a, const void* b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;
    return (x > y) - (x < y);
}

int main(void)
{
    static double frames[FRAMES];
    Hud hud;
    MakeHud(&hud);
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    uint32_t state = 12345;
    muiWorkCounts before = {0};
    for (uint32_t frame = 0; frame <= FRAMES; frame++)
    {
        // A frame at 120 Hz.
        const muiLayoutInput input = {
            1280.0f, 720.0f, MeasureLabel, NULL, (uint64_t)frame * 8333333ull, NULL, {0, 0, 0, 0}};
        double start = Seconds();
        Check(muiComputeLayout(hud.context, hud.root, &input), "layout");
        Check(muiBuildDrawList(hud.context, hud.root, &draw), "draw");
        double elapsed = (Seconds() - start) * 1e6;
        // The first frame lays everything out, and is not counted.
        if (frame == 0)
        {
            before = muiGetWorkCounts(hud.context);
        }
        else
        {
            frames[frame - 1] = elapsed;
        }
        Change(&hud, frame, &state);
    }
    muiWorkCounts after = muiGetWorkCounts(hud.context);
    qsort(frames, FRAMES, sizeof(double), Ascending);
    printf("hud, %u panels of %u rows, %u changes a frame, a panel eased every 30 frames:\n",
           PANELS, ROWS, CHANGES);
    printf("  %u frames, median %.1f, 99th %.1f, worst %.1f us\n", FRAMES, frames[FRAMES / 2],
           frames[FRAMES - 1 - FRAMES / 100], frames[FRAMES - 1]);
    printf("  a frame: %.1f styled, %.1f sized, %.1f measured, %.1f painted\n",
           (double)(after.styled - before.styled) / FRAMES,
           (double)(after.sized - before.sized) / FRAMES,
           (double)(after.measured - before.measured) / FRAMES,
           (double)(after.painted - before.painted) / FRAMES);
    muiDestroyContext(hud.context);
    return 0;
}
