// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code calling back into the text service and the glyph atlas it
// serves (Maul conventions, Threads): from the allocator, FreeType's memory
// through it and the clipboard writer, every function that uses the service
// answers mui_errorBusy, or an empty answer where it returns no status,
// and counts one misuse; from the allocator while an atlas packs a glyph,
// every function that changes the atlas does. A call given a muiTextHost
// holds the context too: host code it runs changes it no more than the
// context's own host code may.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/context.h"
#include "maul-ui/event.h"
#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/glyph_image.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"

enum
{
    // The functions that use a service or change an atlas and return a
    // status.
    CALLS = 41,
};

// Calls every function that uses a service or changes an atlas and
// returns a status, each with arguments it refuses on their own; the
// results in the order of the public headers.
static int CallAll(muiTextService* service, const muiTextHost* host, muiGlyphAtlas* atlas,
                   muiResult results[CALLS])
{
    int count = 0;
    results[count++] = muiCreateFont(service, NULL, NULL);
    results[count++] = muiDestroyFont(service, (muiFontId){0});
    results[count++] = muiCreateFontFamily(service, NULL, NULL);
    results[count++] = muiDestroyFontFamily(service, (muiFontFamilyId){0});
    results[count++] = muiCreateGlyphAtlas(service, NULL, NULL);
    results[count++] = muiGlyphAtlas_GetGlyph(atlas, 0, 0, NAN, NAN, NAN, NULL);
    results[count++] =
        muiGlyphAtlas_GetColorGlyph(atlas, 0, 0, NAN, NAN, NAN, 0, (muiLinearColor){0}, NULL);
    results[count++] = muiGlyphAtlas_GetGlyphField(atlas, 0, 0, NAN, 0, NULL);
    results[count++] = muiGlyphAtlas_GetGlyphMultiField(atlas, 0, 0, NAN, 0, NULL);
    results[count++] = muiGlyphAtlas_TakeUpdates(atlas, NULL, 0, NULL);
    results[count++] = muiTextService_RenderGlyph(service, 0, 0, NAN, NAN, NULL, NULL, 0);
    results[count++] = muiTextService_RenderColorGlyph(service, 0, 0, NAN, NAN, 0,
                                                       (muiLinearColor){0}, NULL, NULL, 0);
    results[count++] = muiTextService_RenderGlyphField(service, 0, 0, NAN, 0, NULL, NULL, 0);
    results[count++] = muiTextService_RenderGlyphMultiField(service, 0, 0, NAN, 0, NULL, NULL, 0);
    results[count++] = muiCreateTextBlock(service, NULL, NULL);
    results[count++] = muiDestroyTextBlock(service, (muiTextBlockId){0});
    results[count++] = muiTextBlock_SetText(service, (muiTextBlockId){0}, NULL, 0);
    results[count++] = muiTextBlock_Replace(service, (muiTextBlockId){0}, 0, 0, NULL, 0);
    results[count++] = muiTextBlock_SetSpans(service, (muiTextBlockId){0}, NULL, 0);
    results[count++] = muiTextService_SetDefaultFont(service, (muiFontId){0});
    results[count++] = muiTextService_SetFallbackFonts(service, NULL, 0);
    results[count++] = muiNode_HitTestText(host, (muiNodeId){0}, NAN, NAN, NAN, NULL);
    results[count++] = muiNode_GetTextCaret(host, (muiNodeId){0}, NAN, (muiTextPosition){0}, NULL);
    results[count++] = muiNode_GetTextRangeRects(host, (muiNodeId){0}, NAN, 0, 0, NULL, 0, NULL);
    results[count++] = muiNode_MoveTextPosition(host, (muiNodeId){0}, NAN, (muiTextPosition){0},
                                                (muiTextMovement){0}, NAN, NULL);
    results[count++] =
        muiTextBlock_SetComposition(service, (muiTextBlockId){0}, 0, NULL, 0, NULL, 0);
    results[count++] = muiTextBlock_EndComposition(service, (muiTextBlockId){0});
    results[count++] = muiTextBlock_SetEditing(service, (muiTextBlockId){0}, NULL);
    results[count++] = muiTextBlock_Select(service, (muiTextBlockId){0}, (muiTextSelection){0});
    results[count++] = muiTextBlock_Type(service, (muiTextBlockId){0}, NULL, 0, NULL);
    results[count++] = muiTextBlock_Paste(service, (muiTextBlockId){0}, NULL, 0, NULL);
    results[count++] = muiTextBlock_Erase(service, (muiTextBlockId){0}, (muiTextDeletion){0}, NULL);
    results[count++] = muiTextBlock_EraseTo(service, (muiTextBlockId){0}, 0, NULL);
    results[count++] = muiTextBlock_Undo(service, (muiTextBlockId){0}, NULL);
    results[count++] = muiTextBlock_Redo(service, (muiTextBlockId){0}, NULL);
    results[count++] =
        muiTextBlock_Compose(service, (muiTextBlockId){0}, NULL, 0, 0, NULL, 0, NULL);
    results[count++] = muiNode_HandleEditEvent(host, (muiNodeId){0}, NULL, NULL, NULL);
    results[count++] = muiNode_MoveEditCaret(host, (muiNodeId){0}, (muiTextMovement){0}, false);
    results[count++] = muiNode_HandleEditPress(host, (muiNodeId){0}, NAN, NAN, 0, false);
    results[count++] = muiNode_HandleEditDrag(host, (muiNodeId){0}, NAN, NAN);
    results[count++] = muiPerformTextAccessAction(host, NULL, NULL);
    return count;
}

// The indexes in CallAll of the atlas's functions.
static const int s_atlasCalls[] = {5, 6, 7, 8, 9};

static bool IsAtlasCall(int index)
{
    for (size_t i = 0; i < sizeof s_atlasCalls / sizeof s_atlasCalls[0]; i++)
    {
        if (s_atlasCalls[i] == index)
        {
            return true;
        }
    }
    return false;
}

typedef struct Scene Scene;

// What host code saw when it called back in: armed for the service or
// for the atlas.
typedef struct Probe
{
    Scene* scene;
    bool armed;
    bool atlas;
    int runs;
    bool expected;
} Probe;

struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    muiTextBlockId block;
    muiNodeId node;
    muiGlyphAtlas* atlas;
    Probe alloc;
    Probe free;
    Probe hold;
};

// Calls back in and checks every answer: the busy object's functions
// refused, the other's not as busy; then the busy object's destroy, and
// for the service the four functions that return no status.
static void CallBack(Probe* probe)
{
    if (!probe->armed)
    {
        return;
    }
    probe->armed = false;
    probe->runs++;
    Scene* scene = probe->scene;
    uint64_t misuse = muiTextService_GetMisuse(scene->service);
    muiResult results[CALLS];
    int count = CallAll(scene->service, &scene->host, scene->atlas, results);
    bool expected = count == CALLS;
    uint64_t refused = 0;
    for (int i = 0; i < count; i++)
    {
        bool state = results[i] == mui_errorBusy;
        refused += state;
        expected = expected && (IsAtlasCall(i) == probe->atlas || !state) &&
                   (IsAtlasCall(i) != probe->atlas || state);
    }
    if (probe->atlas)
    {
        muiGlyphAtlas_BeginFrame(scene->atlas);
        muiDestroyGlyphAtlas(scene->atlas);
        refused += 2;
    }
    else
    {
        const muiMeasureAxis axis = {100.0f, mui_measureAtMost};
        uint64_t key = muiTextBlock_GetKey(scene->block);
        muiSize size = muiMeasureText(&scene->host, scene->node, key, axis, axis);
        float baseline = muiMeasureTextBaseline(&scene->host, scene->node, key, 100.0f, 100.0f);
        muiAccessContent content = {0};
        bool read = muiReadAccessText(&scene->host, scene->node, key, NULL, &content);
        muiPaintText(&scene->host, scene->node, key, 100.0f, 100.0f, NULL);
        muiDestroyTextService(scene->service);
        expected =
            expected && size.width == 0.0f && size.height == 0.0f && isnan(baseline) && !read;
        refused += 5;
    }
    // The calls not refused as busy refuse their arguments, counted too.
    expected = expected && muiTextService_GetMisuse(scene->service) >= misuse + refused;
    probe->expected = probe->expected && expected;
}

// Changes the context a call given a host reads: refused as busy and
// counted, the destroy doing nothing.
static void ChangeContext(Probe* probe)
{
    if (!probe->armed)
    {
        return;
    }
    probe->armed = false;
    probe->runs++;
    Scene* scene = probe->scene;
    uint64_t misuse = muiContext_GetMisuse(scene->context);
    muiDestroyContext(scene->context);
    muiResult destroyed = muiDestroyNode(scene->context, scene->node);
    probe->expected = probe->expected && destroyed == mui_errorBusy &&
                      muiContext_GetMisuse(scene->context) == misuse + 2;
}

static void* CallingAlloc(size_t size, size_t alignment, void* user)
{
    CallBack(&((Scene*)user)->alloc);
    ChangeContext(&((Scene*)user)->hold);
    return alignment <= alignof(max_align_t) ? malloc(size) : NULL;
}

static void CallingFree(void* memory, size_t size, size_t alignment, void* user)
{
    (void)size;
    (void)alignment;
    CallBack(&((Scene*)user)->free);
    free(memory);
}

static void Arm(Probe* probe, Scene* scene, bool atlas)
{
    *probe = (Probe){scene, true, atlas, 0, true};
}

// A service through the calling allocator with Ahem as its default font,
// an editable block on a node of a context, and an atlas; each step
// arming the allocator.
static void MakeScene(Scene* scene)
{
    *scene = (Scene){0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    def.allocator = (muiAllocator){CallingAlloc, CallingFree, scene};
    CHECK(muiCreateTextService(&def, &scene->service) == mui_success, "service");
    muiContextDef contextDef = muiDefaultContextDef();
    CHECK(muiCreateContext(&contextDef, &scene->context) == mui_success, "context");
    scene->host = (muiTextHost){scene->service, scene->context};
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    muiFontId fontId = {0, 0};
    Arm(&scene->alloc, scene, false);
    CHECK(muiCreateFont(scene->service, &font, &fontId) == mui_success &&
              muiTextService_SetDefaultFont(scene->service, fontId) == mui_success,
          "the default font");
    CHECK(scene->alloc.runs == 1 && scene->alloc.expected, "from the allocator while a font opens");
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = "ab cd";
    blockDef.length = 5;
    Arm(&scene->alloc, scene, false);
    CHECK(muiCreateTextBlock(scene->service, &blockDef, &scene->block) == mui_success, "block");
    CHECK(scene->alloc.runs == 1 && scene->alloc.expected,
          "from the allocator while a block is made");
    muiTextEditRules edit = muiDefaultTextEditRules();
    CHECK(muiTextBlock_SetEditing(scene->service, scene->block, &edit) == mui_success, "editing");
    muiNodeDef node = muiDefaultNodeDef();
    node.hostKey = muiTextBlock_GetKey(scene->block);
    CHECK(muiCreateNode(scene->context, &node, &scene->node) == mui_success, "node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(scene->context, scene->node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success &&
              muiNode_SetTextValues(scene->context, scene->node, &style,
                                    MUI_PROPERTY_BIT(mui_propertyFontSize)) == mui_success,
          "host content");
    const muiLayoutInput input = {
        1000.0f, 100.0f, muiMeasureText, &scene->host, 0, muiMeasureTextBaseline, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(scene->context, scene->node, &input) == mui_success, "laid out");
    muiGlyphAtlasDef atlasDef = muiDefaultGlyphAtlasDef();
    CHECK(muiCreateGlyphAtlas(scene->service, &atlasDef, &scene->atlas) == mui_success, "atlas");
}

// Outside host code no call is refused as busy, so each refusal below is.
static void TestControl(void)
{
    Scene scene;
    MakeScene(&scene);
    muiResult results[CALLS];
    int count = CallAll(scene.service, &scene.host, scene.atlas, results);
    bool none = count == CALLS;
    for (int i = 0; i < count; i++)
    {
        none = none && results[i] != mui_errorBusy;
    }
    CHECK(none, "no call refused as busy outside host code");
    muiDestroyGlyphAtlas(scene.atlas);
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
}

typedef struct Clipboard
{
    Probe probe;
    int writes;
    // Changing the context instead of calling back in.
    bool context;
} Clipboard;

static void WriteClipboard(void* user, const char* text, size_t length)
{
    (void)text;
    (void)length;
    Clipboard* clipboard = user;
    clipboard->writes++;
    if (clipboard->context)
    {
        ChangeContext(&clipboard->probe);
        return;
    }
    CallBack(&clipboard->probe);
}

static void TestHostCode(void)
{
    Scene scene;
    MakeScene(&scene);
    // The clipboard writer, from a copy.
    const muiTextSelection all = {0, {5, false}};
    CHECK(muiTextBlock_Select(scene.service, scene.block, all) == mui_success, "selected");
    Clipboard clipboard = {{&scene, true, false, 0, true}, 0, false};
    const muiTextEditInput input = {mui_keymapPc, WriteClipboard, &clipboard};
    muiEvent event = {0};
    event.kind = mui_eventKeyDown;
    event.code = 6;
    event.key = 'c';
    event.modifiers = mui_modControl;
    muiTextEditOutcome outcome;
    CHECK(muiNode_HandleEditEvent(&scene.host, scene.node, &event, &input, &outcome) ==
                  mui_success &&
              outcome.handled && clipboard.writes == 1,
          "copied");
    CHECK(clipboard.probe.runs == 1 && clipboard.probe.expected, "from the clipboard writer");
    // The allocator while a text changes, and its free while a block goes.
    Arm(&scene.alloc, &scene, false);
    CHECK(muiTextBlock_SetText(scene.service, scene.block, "a much longer text to hold", 26) ==
              mui_success,
          "a longer text");
    CHECK(scene.alloc.runs == 1 && scene.alloc.expected, "from the allocator");
    Arm(&scene.free, &scene, false);
    CHECK(muiDestroyTextBlock(scene.service, scene.block) == mui_success, "the block gone");
    CHECK(scene.free.runs == 1 && scene.free.expected, "from the free");
    // The allocator while the atlas adds a page for its first glyph.
    Arm(&scene.alloc, &scene, true);
    muiAtlasGlyph glyph;
    CHECK(muiGlyphAtlas_GetGlyph(scene.atlas, 0, 1, 10.0f, 0.0f, 0.0f, &glyph) == mui_success,
          "a glyph packed");
    CHECK(scene.alloc.runs == 1 && scene.alloc.expected, "from the allocator in the atlas");
    // Destroying is busy to the end.
    Arm(&scene.free, &scene, true);
    muiDestroyGlyphAtlas(scene.atlas);
    CHECK(scene.free.runs == 1 && scene.free.expected, "from a free while the atlas goes");
    scene.atlas = NULL;
    muiDestroyContext(scene.context);
    Arm(&scene.free, &scene, false);
    muiDestroyTextService(scene.service);
    CHECK(scene.free.runs == 1 && scene.free.expected, "from a free while the service goes");
}

// A text longer than any laid out so far, so the next call that lays it
// out grows the service's scratch through the allocator.
static void Lengthen(Scene* scene)
{
    static char s_text[2000];
    for (size_t i = 0; i < sizeof s_text; i++)
    {
        s_text[i] = i % 7 == 6 ? ' ' : (char)('a' + i % 26);
    }
    CHECK(muiTextBlock_SetText(scene->service, scene->block, s_text, sizeof s_text) == mui_success,
          "a longer text");
}

// Calls given a host that lay the text out, each with arguments it takes.
static muiResult CallWithHost(Scene* scene, int call)
{
    const muiTextHost* host = &scene->host;
    muiTextPosition at = {0, mui_affinityDownstream};
    muiTextCaret caret;
    muiRect rects[4];
    uint32_t count = 0;
    muiTextEditOutcome outcome;
    const muiMeasureAxis axis = {300.0f, mui_measureAtMost};
    uint64_t key = muiTextBlock_GetKey(scene->block);
    muiEvent event = {0};
    event.kind = mui_eventText;
    event.text = "z";
    event.length = 1;
    const muiTextEditInput input = {mui_keymapPc, NULL, NULL};
    const muiAccessRequest request = {.action = mui_actionReplaceText,
                                      .target = muiNode_GetAccessId(scene->node),
                                      .focus = 2,
                                      .text = "hello",
                                      .length = 5};
    switch (call)
    {
    case 0:
        return muiNode_HitTestText(host, scene->node, 300.0f, 5.0f, 5.0f, &at);
    case 1:
        return muiNode_GetTextCaret(host, scene->node, 300.0f, at, &caret);
    case 2:
        return muiNode_GetTextRangeRects(host, scene->node, 300.0f, 0, 100, rects, 4, &count);
    case 3:
        return muiNode_MoveTextPosition(host, scene->node, 300.0f, at, mui_moveNextCluster, 0.0f,
                                        &at);
    case 4:
        return muiNode_MoveEditCaret(host, scene->node, mui_moveLeft, false);
    case 5:
        return muiNode_HandleEditPress(host, scene->node, 5.0f, 5.0f, 1, false);
    case 6:
        return muiNode_HandleEditDrag(host, scene->node, 5.0f, 5.0f);
    case 7:
        return muiNode_HandleEditEvent(host, scene->node, &event, &input, &outcome);
    case 8:
        return muiPerformTextAccessAction(host, &request, &outcome);
    case 9:
        return muiMeasureText(&scene->host, scene->node, key, axis, axis).width > 0.0f
                   ? mui_success
                   : mui_errorState;
    default:
        return isnan(muiMeasureTextBaseline(&scene->host, scene->node, key, 300.0f, 300.0f))
                   ? mui_errorState
                   : mui_success;
    }
}

static void FreeScene(Scene* scene)
{
    muiDestroyGlyphAtlas(scene->atlas);
    muiDestroyContext(scene->context);
    muiDestroyTextService(scene->service);
}

// From the measure function the context runs, calling muiMeasureText:
// the context still busy after it returns.
static muiSize MeasureThenChange(void* user, muiNodeId nodeId, uint64_t hostKey,
                                 muiMeasureAxis width, muiMeasureAxis height)
{
    Scene* scene = user;
    muiSize size = muiMeasureText(&scene->host, nodeId, hostKey, width, height);
    scene->hold.expected =
        scene->hold.expected && muiDestroyNode(scene->context, scene->node) == mui_errorBusy;
    scene->hold.runs++;
    return size;
}

// Host code that a call given a host runs, while the call reads the
// context: the allocator as the text is laid out, the clipboard writer.
static void TestContextHeld(void)
{
    for (int call = 0; call <= 10; call++)
    {
        Scene scene;
        MakeScene(&scene);
        Lengthen(&scene);
        Arm(&scene.hold, &scene, false);
        CHECK(CallWithHost(&scene, call) == mui_success, "the call done");
        CHECK(scene.hold.runs == 1 && scene.hold.expected, "the context held");
        CHECK(muiDestroyNode(scene.context, scene.node) == mui_success,
              "the context free again after");
        FreeScene(&scene);
    }
    Scene scene;
    MakeScene(&scene);
    const muiTextSelection all = {0, {5, false}};
    CHECK(muiTextBlock_Select(scene.service, scene.block, all) == mui_success, "selected");
    Clipboard clipboard = {{&scene, true, false, 0, true}, 0, true};
    const muiTextEditInput input = {mui_keymapPc, WriteClipboard, &clipboard};
    muiEvent event = {0};
    event.kind = mui_eventKeyDown;
    event.code = 6;
    event.key = 'c';
    event.modifiers = mui_modControl;
    muiTextEditOutcome outcome;
    CHECK(muiNode_HandleEditEvent(&scene.host, scene.node, &event, &input, &outcome) ==
                  mui_success &&
              clipboard.probe.runs == 1 && clipboard.probe.expected,
          "held from the clipboard writer");
    // Held inside layout too, and still busy once muiMeasureText returns.
    Lengthen(&scene);
    CHECK(muiNode_MarkContentChanged(scene.context, scene.node) == mui_success, "measured again");
    scene.hold = (Probe){&scene, false, false, 0, true};
    const muiLayoutInput layout = {
        1000.0f, 100.0f, MeasureThenChange, &scene, 0, muiMeasureTextBaseline, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(scene.context, scene.node, &layout) == mui_success &&
              scene.hold.runs != 0 && scene.hold.expected,
          "busy after muiMeasureText in layout");
    FreeScene(&scene);
}

int main(void)
{
    TestControl();
    TestHostCode();
    TestContextHeld();
    return s_failures == 0 ? 0 : 1;
}
