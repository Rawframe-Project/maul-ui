// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every text Maul UI holds is at most MUI_MAX_TEXT_BYTES, so its offsets
// fit 32 bits. Each call that takes a string, as a parameter or in a
// record, refuses one past that bound with mui_errorLimit before it reads
// a byte or changes the block: the strings here are one byte long, so a
// call that read them would trip AddressSanitizer, and the block's text
// is all selected, so a call that took the selection out first would
// change it. Where size_t is wider than 32 bits, a length of 2^32 + 1
// checks that no call cuts the length to 32 bits first, where it would
// read as 1.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/font.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"

typedef struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    muiFontId font;
    muiTextBlockId block;
    muiNodeId node;
} Scene;

static Scene MakeScene(void)
{
    Scene scene = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    CHECK(muiCreateTextService(&def, &scene.service) == mui_success, "service");
    muiContextDef contextDef = muiDefaultContextDef();
    CHECK(muiCreateContext(&contextDef, &scene.context) == mui_success, "context");
    scene.host = (muiTextHost){scene.service, scene.context};
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    CHECK(muiCreateFont(scene.service, &font, &scene.font) == mui_success &&
              muiTextService_SetDefaultFont(scene.service, scene.font) == mui_success,
          "the default font");
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = "ab";
    blockDef.length = 2;
    CHECK(muiCreateTextBlock(scene.service, &blockDef, &scene.block) == mui_success, "block");
    muiTextEditRules edit = muiDefaultTextEditRules();
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &edit) == mui_success, "editing");
    muiNodeDef node = muiDefaultNodeDef();
    node.hostKey = muiTextBlock_GetKey(scene.block);
    CHECK(muiCreateNode(scene.context, &node, &scene.node) == mui_success, "node");
    return scene;
}

// Selects the whole text, as a user about to type over it would.
static void SelectAll(Scene* s)
{
    CHECK(muiTextBlock_Select(s->service, s->block,
                              (muiTextSelection){0, {2, mui_affinityDownstream}}) == mui_success,
          "the text selected");
}

// Whether the block still holds its two bytes.
static bool Kept(const Scene* s)
{
    const char* text = NULL;
    size_t length = 0;
    return muiTextBlock_GetText(s->service, s->block, &text, &length) == mui_success &&
           length == 2 && memcmp(text, "ab", 2) == 0;
}

static void TestLength(Scene* s, size_t length)
{
    // One byte on the heap, so a read past it is a heap overflow.
    char* one = malloc(1);
    CHECK(one != NULL, "a byte");
    if (one == NULL)
    {
        return;
    }
    one[0] = 'x';
    muiTextBlockDef def = muiDefaultTextBlockDef();
    def.text = one;
    def.length = length;
    muiTextBlockId made = {0, 0};
    CHECK(muiCreateTextBlock(s->service, &def, &made) == mui_errorLimit, "a block's creation");
    CHECK(muiTextBlock_SetText(s->service, s->block, one, length) == mui_errorLimit && Kept(s),
          "a block's text");
    CHECK(muiTextBlock_Replace(s->service, s->block, 0, 1, one, length) == mui_errorLimit &&
              Kept(s),
          "a replacement");
    SelectAll(s);
    bool changed = true;
    CHECK(muiTextBlock_Type(s->service, s->block, one, length, &changed) == mui_errorLimit &&
              !changed && Kept(s),
          "typing");
    SelectAll(s);
    changed = true;
    CHECK(muiTextBlock_Paste(s->service, s->block, one, length, &changed) == mui_errorLimit &&
              !changed && Kept(s),
          "a paste");
    SelectAll(s);
    changed = true;
    CHECK(muiTextBlock_Compose(s->service, s->block, one, length, 0, NULL, 0, &changed) ==
                  mui_errorLimit &&
              !changed && Kept(s),
          "a composition typed");
    SelectAll(s);
    CHECK(muiTextBlock_SetComposition(s->service, s->block, 0, one, length, NULL, 0) ==
                  mui_errorLimit &&
              Kept(s),
          "a composition set");
    CHECK(muiNode_SetAccessText(s->context, s->node, mui_accessValue, one, length) ==
              mui_errorLimit,
          "an accessibility text");
    const muiTextEvent routed = {0, one, length, 0};
    bool handled = true;
    CHECK(muiNode_RouteText(s->context, s->node, &routed, &handled) == mui_errorLimit && !handled,
          "a routed text");
    // The editor handed a text event, and a replacement an assistive
    // technology asks for.
    muiEvent event = {0};
    event.kind = mui_eventText;
    event.text = one;
    event.length = length;
    const muiTextEditInput input = {mui_keymapPc, NULL, NULL};
    muiTextEditOutcome outcome;
    SelectAll(s);
    CHECK(muiNode_HandleEditEvent(&s->host, s->node, &event, &input, &outcome) == mui_errorLimit &&
              !outcome.changed && Kept(s),
          "a text event");
    muiAccessRequest request = {0};
    request.action = mui_actionReplaceText;
    request.target = muiNode_GetAccessId(s->node);
    request.anchor = 0;
    request.focus = 2;
    request.text = one;
    request.length = length;
    CHECK(muiPerformTextAccessAction(&s->host, &request, &outcome) == mui_errorLimit &&
              !outcome.changed && Kept(s),
          "a replacement asked for");
    free(one);
}

// A composition refused for its segments, or for not fitting the block
// once it replaces the selection, leaves the selection and the text: the
// checks come before the selection is taken out.
static void TestComposeRefused(Scene* s)
{
    muiCompositionSegment segments[MUI_MAX_COMPOSITION_SEGMENTS + 1] = {0};
    for (uint32_t i = 0; i <= MUI_MAX_COMPOSITION_SEGMENTS; i++)
    {
        segments[i] = (muiCompositionSegment){0, 1, mui_compositionPlain};
    }
    const muiCompositionSegment past = {5, 1, mui_compositionPlain};
    bool changed = true;
    SelectAll(s);
    CHECK(muiTextBlock_Compose(s->service, s->block, "x", 1, 0, &past, 1, &changed) ==
                  mui_errorInvalid &&
              !changed && Kept(s),
          "a segment past the composition");
    changed = true;
    SelectAll(s);
    CHECK(muiTextBlock_Compose(s->service, s->block, "x", 1, 0, segments,
                               MUI_MAX_COMPOSITION_SEGMENTS + 1, &changed) == mui_errorLimit &&
              !changed && Kept(s),
          "too many segments");
    // The longest text there is, over one byte of two: within the bound
    // alone, past it with the byte left.
    char* one = malloc(1);
    CHECK(one != NULL, "a byte");
    if (one == NULL)
    {
        return;
    }
    one[0] = 'x';
    changed = true;
    CHECK(muiTextBlock_Select(s->service, s->block,
                              (muiTextSelection){0, {1, mui_affinityDownstream}}) == mui_success &&
              muiTextBlock_Compose(s->service, s->block, one, MUI_MAX_TEXT_BYTES, 0, NULL, 0,
                                   &changed) == mui_errorLimit &&
              !changed && Kept(s),
          "a composition the block cannot hold");
    free(one);
    bool undo = true;
    bool redo = true;
    CHECK(muiTextBlock_GetUndoState(s->service, s->block, &undo, &redo) == mui_success && !undo &&
              !redo,
          "nothing to undo");
}

int main(void)
{
    Scene scene = MakeScene();
    TestLength(&scene, (size_t)MUI_MAX_TEXT_BYTES + 1);
    TestComposeRefused(&scene);
#if SIZE_MAX > UINT32_MAX
    TestLength(&scene, ((size_t)1 << 32) + 1);
#endif
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
    return s_failures == 0 ? 0 : 1;
}
