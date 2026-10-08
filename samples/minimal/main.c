// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Uses the installed library as a consumer would, in C17: it checks that
// the linked library is the version it was compiled against, creates a
// context and a text service (FreeType and HarfBuzz inside the library),
// lays out a node with a background and paints it, and checks the one
// command the list holds.

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/visual.h"

#include <stdbool.h>
#include <stdio.h>

int main(void)
{
    muiVersion version = muiGetVersion();
    if (version.major != MUI_VERSION_MAJOR || version.minor != MUI_VERSION_MINOR ||
        version.patch != MUI_VERSION_PATCH)
    {
        fprintf(stderr, "linked %u.%u.%u, compiled against %d.%d.%d\n", version.major,
                version.minor, version.patch, MUI_VERSION_MAJOR, MUI_VERSION_MINOR,
                MUI_VERSION_PATCH);
        return 1;
    }
    muiContextDef contextDef = muiDefaultContextDef();
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    muiContext* context = NULL;
    muiTextService* service = NULL;
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0, 0};
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = (muiColor){0.2f, 0.4f, 0.8f, 1.0f};
    muiDrawList list = {0};
    bool drawn = false;
    if (muiCreateContext(&contextDef, &context) == mui_success &&
        muiCreateTextService(&serviceDef, &service) == mui_success &&
        muiCreateNode(context, &nodeDef, &root) == mui_success &&
        muiNode_SetVisualValues(context, root, &visual, MUI_PROPERTY_BIT(mui_propertyBackground)) ==
            mui_success)
    {
        muiTextHost host = {service, context};
        const muiLayoutInput layout = {320.0f, 200.0f, muiMeasureText, &host,
                                       0,      NULL,   {0, 0, 0, 0}};
        const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
        drawn = muiComputeLayout(context, root, &layout) == mui_success &&
                muiBuildDrawList(context, root, &draw) == mui_success &&
                muiGetDrawList(context, &list) == mui_success && list.commandCount == 1;
    }
    muiDestroyTextService(service);
    muiDestroyContext(context);
    if (!drawn)
    {
        fprintf(stderr, "the node was not drawn\n");
        return 1;
    }
    printf("maul-ui %u.%u.%u: one command drawn\n", version.major, version.minor, version.patch);
    return 0;
}
