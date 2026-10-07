// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Work counts (record mui-0001): a static frame styles, sizes, measures
// and paints nothing; a label's changed content measures it and sizes
// what it moves without styling; a class's changed colour restyles (every
// node, as a class edit does, record mui-0004) and paints without layout.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <stdio.h>

static const muiNodeId s_nullNode = {0, 0};

// The label's content: its width, and the calls that asked it.
typedef struct Label
{
    float width;
    int calls;
} Label;

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)nodeId;
    (void)hostKey;
    (void)width;
    (void)height;
    Label* label = user;
    label->calls++;
    return (muiSize){label->width, 20.0f};
}

static muiNodeId Node(muiContext* context, muiNodeId parent, uint64_t hostKey,
                      const muiLayoutStyle* layout, muiPropertyMask mask)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = hostKey;
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success &&
              (parent.index1 == 0 ||
               muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success) &&
              muiNode_SetLayoutValues(context, node, layout, mask) == mui_success,
          "a node");
    return node;
}

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

// A frame: layout, then a draw list.
static void Frame(muiContext* context, muiNodeId root, Label* label)
{
    const muiLayoutInput layout = {200.0f, 200.0f, Measure, label, 0, NULL};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    muiDrawList list;
    CHECK(muiComputeLayout(context, root, &layout) == mui_success &&
              muiBuildDrawList(context, root, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success,
          "a frame");
}

// The work a frame did.
static muiWorkCounts Counted(muiContext* context, muiNodeId root, Label* label)
{
    muiWorkCounts before = muiGetWorkCounts(context);
    Frame(context, root, label);
    muiWorkCounts after = muiGetWorkCounts(context);
    return (muiWorkCounts){after.styled - before.styled, after.sized - before.sized,
                           after.measured - before.measured, after.painted - before.painted};
}

static void Print(const char* what, muiWorkCounts counts)
{
    printf("%s: styled %llu, sized %llu, measured %llu, painted %llu\n", what,
           (unsigned long long)counts.styled, (unsigned long long)counts.sized,
           (unsigned long long)counts.measured, (unsigned long long)counts.painted);
}

int main(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "a context");
    if (context == NULL)
    {
        return 1;
    }
    CHECK(muiGetWorkCounts(NULL).styled == 0 && muiGetWorkCounts(NULL).painted == 0,
          "NULL counts nothing");
    // A column of a panel coloured by a class, a host-measured label and
    // a box.
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = Length(200.0f);
    layout.sizing.height = Length(200.0f);
    layout.container.direction = mui_flexColumn;
    muiNodeId root =
        Node(context, s_nullNode, 0, &layout,
             MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight) |
                 MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    layout = muiDefaultLayoutStyle();
    layout.sizing.height = Length(30.0f);
    muiNodeId panel = Node(context, root, 0, &layout, MUI_PROPERTY_BIT(mui_propertyHeight));
    muiStyleId style = {0};
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){1.0f, 0.0f, 0.0f, 1.0f};
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    CHECK(muiCreateStyle(context, &style) == mui_success &&
              muiStyle_SetVisualValues(context, style, mui_variantBase, &visual, background) ==
                  mui_success &&
              muiNode_SetClasses(context, panel, &style, 1) == mui_success,
          "the panel's class");
    layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    layout.item.alignSelf = mui_alignStart;
    muiNodeId label =
        Node(context, root, 7, &layout,
             MUI_PROPERTY_BIT(mui_propertyContent) | MUI_PROPERTY_BIT(mui_propertyAlignSelf));
    layout = muiDefaultLayoutStyle();
    layout.sizing.height = Length(30.0f);
    muiNodeId box = Node(context, root, 0, &layout, MUI_PROPERTY_BIT(mui_propertyHeight));
    CHECK(muiNode_SetVisualValues(context, box, &visual, background) == mui_success, "the box");
    Label content = {60.0f, 0};

    muiWorkCounts first = Counted(context, root, &content);
    Print("first frame", first);
    CHECK(first.styled == 4 && first.sized > 0 && first.measured > 0 && first.painted == 4,
          "the first frame styles and paints every node");

    muiWorkCounts still = Counted(context, root, &content);
    Print("static frame", still);
    CHECK(still.styled == 0 && still.sized == 0 && still.measured == 0 && still.painted == 0,
          "a static frame does no work");

    content.width = 90.0f;
    CHECK(muiNode_MarkContentChanged(context, label) == mui_success, "the label changed");
    int calls = content.calls;
    muiWorkCounts relabel = Counted(context, root, &content);
    Print("label changed", relabel);
    CHECK(relabel.styled == 0 && relabel.sized > 0 &&
              relabel.measured == (uint64_t)(content.calls - calls) && relabel.measured > 0 &&
              relabel.painted > 0 && relabel.painted < 4,
          "a changed label measures and lays out, styles nothing, paints a part");

    visual.background = (muiColor){0.0f, 0.0f, 1.0f, 1.0f};
    CHECK(muiStyle_SetVisualValues(context, style, mui_variantBase, &visual, background) ==
              mui_success,
          "the class recoloured");
    muiWorkCounts recolour = Counted(context, root, &content);
    Print("class recoloured", recolour);
    CHECK(recolour.styled > 0 && recolour.sized == 0 && recolour.measured == 0 &&
              recolour.painted > 0 && recolour.painted < 4,
          "a recoloured class styles and paints, without layout");

    muiDestroyContext(context);
    return s_failures == 0 ? 0 : 1;
}
