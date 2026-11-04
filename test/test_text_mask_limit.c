// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A password is laid out as a bullet of three bytes per cluster, so one
// of more one-byte clusters than a third of MUI_MAX_TEXT_BYTES has a mask
// past the limit: it measures as when memory runs out, with a failure
// counted, instead of as a mask past the limit or wrapped in 32 bits. The
// text is 716 MB and the run peaks near 1.4 GB, so the suite is labeled
// large: the pull request tiers leave it out, and test_text_mask meets
// the same bound with limits of a few bytes in every cell.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/font.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"

int main(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    muiContextDef contextDef = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&contextDef, &context) == mui_success, "context");
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    muiFontId fontId = {0, 0};
    CHECK(muiCreateFont(service, &font, &fontId) == mui_success &&
              muiTextService_SetDefaultFont(service, fontId) == mui_success,
          "the default font");

    const size_t length = MUI_MAX_TEXT_BYTES / 3 + 1;
    char* text = malloc(length);
    CHECK(text != NULL, "the text");
    if (text == NULL)
    {
        return 1;
    }
    memset(text, 'a', length);
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = text;
    blockDef.length = length;
    muiTextBlockId block = {0, 0};
    muiResult made = muiCreateTextBlock(service, &blockDef, &block);
    free(text);
    CHECK(made == mui_success, "a block of the text");
    muiTextEditRules rules = muiDefaultTextEditRules();
    rules.flags = mui_editPassword;
    CHECK(muiTextBlock_SetEditing(service, block, &rules) == mui_success, "a password");
    muiNodeDef node = muiDefaultNodeDef();
    node.hostKey = muiTextBlock_GetKey(block);
    muiNodeId nodeId = {0, 0};
    CHECK(muiCreateNode(context, &node, &nodeId) == mui_success, "node");

    muiTextHost host = {service, context};
    uint64_t failures = muiTextService_GetLayoutFailureCount(service);
    const muiMeasureAxis axis = {300.0f, mui_measureAtMost};
    muiSize size = muiMeasureText(&host, nodeId, node.hostKey, axis, axis);
    CHECK(isnan(size.width) && isnan(size.height) &&
              muiTextService_GetLayoutFailureCount(service) == failures + 1,
          "a password whose mask would pass the limit");

    muiDestroyContext(context);
    muiDestroyTextService(service);
    return s_failures == 0 ? 0 : 1;
}
