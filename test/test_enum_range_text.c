// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text component's enumerations past their values, as test_enum_range
// checks the core's: every call and def field that takes a kind refuses
// -1, INT32_MIN and 256, and a set of bits the bits it does not name. A
// measure axis and an event, which the host passes on rather than writes,
// are read as no mode and no event the call acts on.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "ahem.inc"

static const int32_t s_kinds[] = {-1, INT32_MIN, 256};
static const uint32_t s_bits[] = {0x80000000u, 0xFFFFFFFFu, 0x100u};

typedef struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    muiFontId font;
    muiFontFamilyId family;
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
    muiFontFamilyDef family = muiDefaultFontFamilyDef();
    family.faces = &scene.font;
    family.faceCount = 1;
    CHECK(muiCreateFontFamily(scene.service, &family, &scene.family) == mui_success, "family");
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = "ab cd";
    blockDef.length = 5;
    CHECK(muiCreateTextBlock(scene.service, &blockDef, &scene.block) == mui_success, "block");
    muiTextEditRules edit = muiDefaultTextEditRules();
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &edit) == mui_success, "editing");
    muiNodeDef node = muiDefaultNodeDef();
    node.hostKey = muiTextBlock_GetKey(scene.block);
    CHECK(muiCreateNode(scene.context, &node, &scene.node) == mui_success, "node");
    return scene;
}

static void TestKinds(Scene* s)
{
    for (size_t k = 0; k < sizeof s_kinds / sizeof s_kinds[0]; k++)
    {
        const int32_t v = s_kinds[k];
        muiFontId face = {0, 0};
        CHECK(muiFontFamily_MatchFace(s->service, s->family, 400.0f, v, &face) == mui_errorInvalid,
              "a slant to match");
        muiFontDef font = muiDefaultFontDef();
        font.data = s_ahem;
        font.size = sizeof s_ahem;
        font.dataMode = v;
        muiFontId made = {0, 0};
        CHECK(muiCreateFont(s->service, &font, &made) == mui_errorInvalid, "a font's data mode");
        muiGlyphAtlasDef atlasDef = muiDefaultGlyphAtlasDef();
        atlasDef.format = v;
        muiGlyphAtlas* atlas = NULL;
        CHECK(muiCreateGlyphAtlas(s->service, &atlasDef, &atlas) == mui_errorInvalid,
              "an atlas's format");
        uint32_t start = 0;
        uint32_t end = 0;
        CHECK(muiTextBlock_FindDeletion(s->service, s->block, 1, v, &start, &end) ==
                  mui_errorInvalid,
              "a deletion to find");
        bool changed = true;
        CHECK(muiTextBlock_Erase(s->service, s->block, v, &changed) == mui_errorInvalid,
              "a deletion");
        muiTextPosition position = {0, mui_affinityDownstream};
        CHECK(muiNode_MoveTextPosition(&s->host, s->node, 100.0f, position, v, NAN, &position) ==
                  mui_errorInvalid,
              "a movement");
        CHECK(muiNode_MoveEditCaret(&s->host, s->node, v, false) == mui_errorInvalid,
              "a caret's movement");
        const muiTextPosition from = {0, v};
        CHECK(muiNode_MoveTextPosition(&s->host, s->node, 100.0f, from, mui_moveRight, NAN,
                                       &position) == mui_errorInvalid,
              "an affinity to move from");
        CHECK(muiTextBlock_Select(s->service, s->block, (muiTextSelection){0, {1, v}}) ==
                  mui_errorInvalid,
              "an affinity to select");
        for (int field = 0; field < 2; field++)
        {
            muiTextEditRules rules = muiDefaultTextEditRules();
            *(field == 0 ? &rules.filter : &rules.purpose) = v;
            CHECK(muiTextBlock_SetEditing(s->service, s->block, &rules) == mui_errorInvalid,
                  "a field's filter and purpose");
        }
        const muiCompositionSegment segment = {0, 1, v};
        CHECK(muiTextBlock_Compose(s->service, s->block, "x", 1, 1, &segment, 1, &changed) ==
                  mui_errorInvalid,
              "a composition's style");
        // A key on a keymap past the two.
        muiEvent event = {0};
        event.kind = mui_eventKeyDown;
        event.code = mui_codeHome;
        const muiTextEditInput input = {v, NULL, NULL};
        muiTextEditOutcome outcome;
        CHECK(muiNode_HandleEditEvent(&s->host, s->node, &event, &input, &outcome) ==
                  mui_errorInvalid,
              "a keymap");
        // An event kind and a key code past theirs are no event the editor
        // acts on.
        const muiTextEditInput pc = {mui_keymapPc, NULL, NULL};
        event.kind = v;
        CHECK(muiNode_HandleEditEvent(&s->host, s->node, &event, &pc, &outcome) == mui_success &&
                  !outcome.handled && !outcome.changed,
              "an event kind");
        event.kind = mui_eventKeyDown;
        event.code = v;
        CHECK(muiNode_HandleEditEvent(&s->host, s->node, &event, &pc, &outcome) == mui_success &&
                  !outcome.handled && !outcome.changed,
              "a key code");
        // A mode past the four measures as one that is not exact.
        const muiMeasureAxis width = {100.0f, v};
        const muiMeasureAxis height = {0.0f, mui_measureMaxContent};
        const muiMeasureAxis exact = {100.0f, mui_measureExact};
        muiSize size =
            muiMeasureText(&s->host, s->node, muiTextBlock_GetKey(s->block), width, height);
        muiSize atMost = muiMeasureText(&s->host, s->node, muiTextBlock_GetKey(s->block),
                                        (muiMeasureAxis){100.0f, mui_measureAtMost}, height);
        CHECK(size.width == atMost.width && size.height == atMost.height &&
                  muiMeasureText(&s->host, s->node, muiTextBlock_GetKey(s->block), exact, height)
                          .width == 100.0f,
              "a measure mode");
    }
}

static void TestBits(Scene* s)
{
    for (size_t k = 0; k < sizeof s_bits / sizeof s_bits[0]; k++)
    {
        muiTextEditRules rules = muiDefaultTextEditRules();
        rules.flags = s_bits[k];
        CHECK(muiTextBlock_SetEditing(s->service, s->block, &rules) == mui_errorInvalid,
              "a field's flags");
    }
}

int main(void)
{
    Scene scene = MakeScene();
    TestKinds(&scene);
    TestBits(&scene);
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
    return s_failures == 0 ? 0 : 1;
}
