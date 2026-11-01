// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every public call that allocates, run with its allocation number n
// failing for n = 1, 2, ... until the call needs fewer: each failure gives
// mui_errorCapacity (or the empty answer a call without a status gives,
// counted as a failure of its service), changes none of what the call
// would change, leaks nothing, and leaves the objects usable: the call
// run again succeeds.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"
#include "bitmap.inc"
#include "color.inc"
#include "liberation_sans.inc"

// The memory of every object: allocation failAt fails (from 1, counting
// from when it was set; 0 for none), and every one after it too when the
// memory stays out.
typedef struct Memory
{
    uint64_t count;
    uint64_t failAt;
    bool staysOut;
    int64_t live;
} Memory;

static void* Alloc(size_t size, size_t alignment, void* user)
{
    Memory* memory = user;
    ++memory->count;
    if (memory->failAt != 0 &&
        (memory->count == memory->failAt || (memory->staysOut && memory->count > memory->failAt)))
    {
        return NULL;
    }
    // The library asks for no more than malloc's alignment.
    void* block = alignment <= alignof(max_align_t) ? malloc(size) : NULL;
    memory->live += block != NULL;
    return block;
}

static void Free(void* block, size_t size, size_t alignment, void* user)
{
    (void)size;
    (void)alignment;
    Memory* memory = user;
    memory->live -= block != NULL;
    free(block);
}

typedef struct World
{
    Memory memory;
    muiContext* context;
    muiTextService* service;
    muiTextHost host;
    muiFontId ahem;
    muiFontId liberation;
    muiFontId color;
    muiFontId bitmap;
    muiTextBlockId block;
    muiNodeId root;
    muiNodeId node;
    muiNodeId label;
    muiAccessTree* tree;
    muiGlyphAtlas* atlas;
    muiGlyphAtlas* colorAtlas;
    muiGlyphAtlas* fieldAtlas;
    // What a create step made, destroyed with the world.
    muiContext* madeContext;
    muiTextService* madeService;
    muiAccessTree* madeTree;
    muiGlyphAtlas* madeAtlas;
    unsigned char pixels[64 * 64 * 4];
    // What the step answered, beside its status.
    uint64_t output;
} World;

// Mixes a value into a hash, field by field: a struct's padding holds
// what its storage held before.
static uint64_t Mix(uint64_t hash, uint64_t value)
{
    return (hash ^ value) * 1099511628211u;
}

static uint64_t Bits(float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t MixRect(uint64_t hash, muiRect rect)
{
    return Mix(Mix(Mix(Mix(hash, Bits(rect.x)), Bits(rect.y)), Bits(rect.width)),
               Bits(rect.height));
}

static uint64_t MixPosition(uint64_t hash, muiTextPosition position)
{
    return Mix(Mix(hash, position.offset), (uint64_t)position.affinity);
}

static char s_long[3000];

static muiAllocator AllocatorOf(World* world)
{
    return (muiAllocator){Alloc, Free, &world->memory};
}

static muiFontId Font(World* world, const unsigned char* data, size_t size)
{
    muiFontDef def = muiDefaultFontDef();
    def.data = data;
    def.size = size;
    muiFontId font = {0, 0};
    CHECK(muiCreateFont(world->service, &def, &font) == mui_success, "a font");
    return font;
}

static muiNodeId Node(World* world, muiNodeId parent, float width, float height, uint64_t key)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = key;
    muiNodeId node = {0, 0};
    CHECK(muiCreateNode(world->context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(world->context, parent, node, (muiNodeId){0, 0}) == mui_success,
              "insert");
    }
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.width = (muiDimension){0.0f, width, mui_dimensionValue};
    style.sizing.height = (muiDimension){0.0f, height, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(world->context, node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "style");
    return node;
}

static void LayOut(World* world)
{
    const muiLayoutInput input = {
        400.0f, 300.0f, muiMeasureText, &world->host, 0, muiMeasureTextBaseline, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(world->context, world->root, &input) == mui_success, "laid out");
}

// A context of a root, a text field and a labelled node, a service with
// three fonts, a tree and three atlases, all in one memory.
static void MakeWorld(World* world)
{
    memset(world, 0, sizeof *world);
    muiContextDef contextDef = muiDefaultContextDef();
    contextDef.allocator = AllocatorOf(world);
    // Room for a few nodes, quick to make each time.
    contextDef.limits.nodes = 16;
    CHECK(muiCreateContext(&contextDef, &world->context) == mui_success, "context");
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    serviceDef.allocator = AllocatorOf(world);
    CHECK(muiCreateTextService(&serviceDef, &world->service) == mui_success, "service");
    world->host = (muiTextHost){world->service, world->context};
    world->ahem = Font(world, s_ahem, sizeof s_ahem);
    world->liberation = Font(world, s_liberationSans, sizeof s_liberationSans);
    world->color = Font(world, s_color, sizeof s_color);
    world->bitmap = Font(world, s_bitmap, sizeof s_bitmap);
    CHECK(muiTextService_SetDefaultFont(world->service, world->ahem) == mui_success,
          "default font");
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = "ab cd";
    blockDef.length = 5;
    CHECK(muiCreateTextBlock(world->service, &blockDef, &world->block) == mui_success, "block");
    muiTextEditRules rules = muiDefaultTextEditRules();
    rules.flags |= mui_editMultiline;
    CHECK(muiTextBlock_SetEditing(world->service, world->block, &rules) == mui_success, "editing");
    world->root = Node(world, (muiNodeId){0, 0}, 400.0f, 300.0f, 0);
    world->node = Node(world, world->root, 300.0f, 200.0f, muiTextBlock_GetKey(world->block));
    world->label = Node(world, world->root, 50.0f, 20.0f, 0);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(world->context, world->node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success &&
              muiNode_SetTextValues(world->context, world->node, &style,
                                    MUI_PROPERTY_BIT(mui_propertyFontSize)) == mui_success,
          "host content");
    CHECK(muiNode_SetAccessRole(world->context, world->node, mui_roleMultilineTextInput) ==
                  mui_success &&
              muiNode_SetAccessRole(world->context, world->label, mui_roleLabel) == mui_success,
          "roles");
    LayOut(world);
    muiAccessTreeDef treeDef = muiDefaultAccessTreeDef();
    treeDef.allocator = AllocatorOf(world);
    CHECK(muiCreateAccessTree(&treeDef, &world->tree) == mui_success, "tree");
    muiGlyphAtlasDef atlasDef = muiDefaultGlyphAtlasDef();
    CHECK(muiCreateGlyphAtlas(world->service, &atlasDef, &world->atlas) == mui_success, "atlas");
    atlasDef.format = mui_atlasColor;
    CHECK(muiCreateGlyphAtlas(world->service, &atlasDef, &world->colorAtlas) == mui_success,
          "colour atlas");
    atlasDef.format = mui_atlasFourChannel;
    CHECK(muiCreateGlyphAtlas(world->service, &atlasDef, &world->fieldAtlas) == mui_success,
          "field atlas");
}

static void FreeWorld(World* world)
{
    world->memory.failAt = 0;
    muiDestroyGlyphAtlas(world->madeAtlas);
    muiDestroyGlyphAtlas(world->fieldAtlas);
    muiDestroyGlyphAtlas(world->colorAtlas);
    muiDestroyGlyphAtlas(world->atlas);
    muiDestroyAccessTree(world->madeTree);
    muiDestroyAccessTree(world->tree);
    muiDestroyTextService(world->madeService);
    muiDestroyContext(world->madeContext);
    muiDestroyContext(world->context);
    muiDestroyTextService(world->service);
}

// A status for a call without one: mui_errorCapacity when the service
// counted a failure during it.
static muiResult Counted(const World* world, uint64_t failures, bool answered)
{
    return muiTextService_GetLayoutFailureCount(world->service) != failures ? mui_errorCapacity
           : answered                                                       ? mui_success
                                                                            : mui_errorState;
}

typedef struct Step
{
    const char* name;
    // Prepares a world for the step, outside the failing allocations.
    void (*prepare)(World* world);
    muiResult (*run)(World* world);
    // Whether a failure keeps what the call did, as a layout or a draw
    // list does, rather than changing nothing.
    bool keeps;
} Step;

// A text longer than any laid out, so the next layout grows the scratch.
static void Lengthen(World* world)
{
    CHECK(muiTextBlock_SetText(world->service, world->block, s_long, sizeof s_long) ==
                  mui_success &&
              muiNode_MarkContentChanged(world->context, world->node) == mui_success,
          "a longer text");
}

// The long text with the caret at its end, the field scrolled to it.
static void Scrolled(World* world)
{
    Lengthen(world);
    const muiTextSelection end = {sizeof s_long, {sizeof s_long, mui_affinityDownstream}};
    CHECK(muiTextBlock_Select(world->service, world->block, end) == mui_success, "at the end");
}

static void Typed(World* world)
{
    bool changed = false;
    CHECK(muiTextBlock_Type(world->service, world->block, "xyz", 3, &changed) == mui_success &&
              changed,
          "typed");
}

static void Enabled(World* world)
{
    CHECK(muiNode_EnableAccess(world->context, world->root, true) == mui_success, "enabled");
}

static muiResult CreateContext(World* world)
{
    muiContextDef def = muiDefaultContextDef();
    def.allocator = AllocatorOf(world);
    return muiCreateContext(&def, &world->madeContext);
}

static muiResult SetAccessText(World* world)
{
    const char* text = "a label of some length";
    return muiNode_SetAccessText(world->context, world->label, mui_accessLabel, text, strlen(text));
}

static muiResult Enable(World* world)
{
    return muiNode_EnableAccess(world->context, world->root, true);
}

static muiResult SetRelation(World* world)
{
    return muiNode_SetAccessRelation(world->context, world->node, mui_relationLabelledBy,
                                     &world->label, 1);
}

static muiResult CreateTree(World* world)
{
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.allocator = AllocatorOf(world);
    return muiCreateAccessTree(&def, &world->madeTree);
}

static muiAccessUpdate s_update;

static void Built(World* world)
{
    Enabled(world);
    Lengthen(world);
    LayOut(world);
    CHECK(muiNode_BuildAccessUpdate(world->context, world->root, &s_update) == mui_success,
          "built");
}

// Applies the update built; the same update applied again after a
// failure, as Apply's header says.
static muiResult Apply(World* world)
{
    return muiAccessTree_Apply(world->tree, &s_update, NULL);
}

static muiResult CreateService(World* world)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    def.allocator = AllocatorOf(world);
    return muiCreateTextService(&def, &world->madeService);
}

static muiResult CreateFont(World* world)
{
    muiFontDef def = muiDefaultFontDef();
    def.data = s_liberationSans;
    def.size = sizeof s_liberationSans;
    muiFontId font;
    return muiCreateFont(world->service, &def, &font);
}

static muiResult CreateFamily(World* world)
{
    muiFontFamilyDef def = muiDefaultFontFamilyDef();
    const muiFontId faces[] = {world->ahem, world->liberation};
    def.faces = faces;
    def.faceCount = 2;
    muiFontFamilyId family;
    return muiCreateFontFamily(world->service, &def, &family);
}

static muiResult CreateBlock(World* world)
{
    muiTextBlockDef def = muiDefaultTextBlockDef();
    def.text = s_long;
    def.length = sizeof s_long;
    muiTextBlockId block;
    return muiCreateTextBlock(world->service, &def, &block);
}

static muiResult SetText(World* world)
{
    return muiTextBlock_SetText(world->service, world->block, s_long, sizeof s_long);
}

static muiResult Replace(World* world)
{
    return muiTextBlock_Replace(world->service, world->block, 1, 2, s_long, 500);
}

static muiResult SetSpans(World* world)
{
    muiTextSpan spans[3];
    for (uint32_t i = 0; i < 3; i++)
    {
        spans[i] =
            (muiTextSpan){i, 1, MUI_PROPERTY_BIT(mui_propertyFontSize), muiDefaultTextStyle()};
        spans[i].style.size = (muiDimension){0.0f, 12.0f + (float)i, mui_dimensionValue};
    }
    return muiTextBlock_SetSpans(world->service, world->block, spans, 3);
}

// The long text with sizes of its own over its first characters, so it
// is shaped and broken in run styles.
static void Spanned(World* world)
{
    Lengthen(world);
    CHECK(SetSpans(world) == mui_success, "spans");
}

// The long text as a password, shown as bullets.
static void Masked(World* world)
{
    Lengthen(world);
    muiTextEditRules rules = muiDefaultTextEditRules();
    rules.flags |= mui_editMultiline | mui_editPassword;
    CHECK(muiTextBlock_SetEditing(world->service, world->block, &rules) == mui_success,
          "a password");
}

// The long text with a composition shown at its start, underlined when
// it is painted.
static void Composing(World* world)
{
    Lengthen(world);
    CHECK(muiTextBlock_Select(world->service, world->block,
                              (muiTextSelection){0, {0, mui_affinityDownstream}}) == mui_success &&
              muiTextBlock_Compose(world->service, world->block, "\xE3\x81\x8B", 3, 3, NULL, 0,
                                   NULL) == mui_success,
          "composing");
}

// A composition at the start of a short first line, the caret in it,
// longer lines below: painting the composition's lines needs more boxes
// than following the caret did.
static void ComposingShortLine(World* world)
{
    char text[502] = "x\n";
    memcpy(text + 2, s_long, 500);
    CHECK(muiTextBlock_SetText(world->service, world->block, text, sizeof text) == mui_success &&
              muiNode_MarkContentChanged(world->context, world->node) == mui_success &&
              muiTextBlock_Select(world->service, world->block,
                                  (muiTextSelection){0, {0, mui_affinityDownstream}}) ==
                  mui_success &&
              muiTextBlock_Compose(world->service, world->block, "\xE3\x81\x8B", 3, 3, NULL, 0,
                                   NULL) == mui_success,
          "composing on a short line");
}

// The long text with its first ten bytes selected.
static void Selected(World* world)
{
    Lengthen(world);
    const muiTextSelection some = {0, {10, mui_affinityDownstream}};
    CHECK(muiTextBlock_Select(world->service, world->block, some) == mui_success, "selected");
}

static muiResult Type(World* world)
{
    bool changed;
    return muiTextBlock_Type(world->service, world->block, s_long, 700, &changed);
}

static muiResult Paste(World* world)
{
    bool changed;
    return muiTextBlock_Paste(world->service, world->block, s_long, 900, &changed);
}

static muiResult Erase(World* world)
{
    bool changed;
    return muiTextBlock_Erase(world->service, world->block, mui_deleteBackward, &changed);
}

static muiResult Undo(World* world)
{
    bool changed;
    return muiTextBlock_Undo(world->service, world->block, &changed);
}

static void Undone(World* world)
{
    Typed(world);
    bool changed = false;
    CHECK(muiTextBlock_Undo(world->service, world->block, &changed) == mui_success && changed,
          "undone");
}

static muiResult Redo(World* world)
{
    bool changed;
    return muiTextBlock_Redo(world->service, world->block, &changed);
}

static muiResult Compose(World* world)
{
    bool changed;
    return muiTextBlock_Compose(world->service, world->block, s_long, 300, 3, NULL, 0, &changed);
}

// Typed text, then part of it selected, so that a composition replaces
// the selection with a history to keep.
static void SelectedAfterTyping(World* world)
{
    Typed(world);
    const muiTextSelection some = {2, {6, mui_affinityDownstream}};
    CHECK(muiTextBlock_Select(world->service, world->block, some) == mui_success, "selected");
}

// Typed text, then an input method's composition shown at the caret:
// typing or deleting takes it out in the same change, so a failure must
// leave it shown.
static void ComposedAfterTyping(World* world)
{
    Typed(world);
    bool changed = false;
    CHECK(muiTextBlock_Compose(world->service, world->block, "\xE3\x81\x8B", 3, 3, NULL, 0,
                               &changed) == mui_success &&
              changed,
          "composing");
}

static muiResult SetComposition(World* world)
{
    return muiTextBlock_SetComposition(world->service, world->block, 1, s_long, 300, NULL, 0);
}

static muiResult Measure(World* world)
{
    uint64_t failures = muiTextService_GetLayoutFailureCount(world->service);
    const muiMeasureAxis axis = {300.0f, mui_measureAtMost};
    muiSize size =
        muiMeasureText(&world->host, world->node, muiTextBlock_GetKey(world->block), axis, axis);
    return Counted(world, failures, size.width > 0.0f);
}

static muiResult Baseline(World* world)
{
    uint64_t failures = muiTextService_GetLayoutFailureCount(world->service);
    float baseline = muiMeasureTextBaseline(&world->host, world->node,
                                            muiTextBlock_GetKey(world->block), 300.0f, 200.0f);
    return Counted(world, failures, !isnan(baseline));
}

static muiResult Layout(World* world)
{
    const muiLayoutInput input = {
        400.0f, 300.0f, muiMeasureText, &world->host, 0, muiMeasureTextBaseline, {0, 0, 0, 0}};
    muiResult status = muiNode_ComputeLayout(world->context, world->root, &input);
    const muiRect rects[] = {muiNode_GetRect(world->context, world->node),
                             muiNode_GetRect(world->context, world->label)};
    world->output = MixRect(MixRect(0, rects[0]), rects[1]);
    return status;
}

// The text node sized by its text, so a failed measure shows.
static void Shrunk(World* world)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    CHECK(muiNode_SetLayoutValues(world->context, world->node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyHeight)) == mui_success,
          "sized by its text");
    Lengthen(world);
}

// The root's children aligned by their baselines, so a failed baseline
// shows in where the label goes.
static void Aligned(World* world)
{
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.container.alignItems = mui_alignBaseline;
    CHECK(muiNode_SetLayoutValues(world->context, world->root, &style,
                                  MUI_PROPERTY_BIT(mui_propertyAlignItems)) == mui_success,
          "aligned by baselines");
    // Minimums of 0 beside its fixed size: the text is never measured, so
    // its baseline is the first thing that lays it out.
    style.sizing.minWidth = (muiDimension){0.0f, 0.0f, mui_dimensionValue};
    style.sizing.minHeight = (muiDimension){0.0f, 0.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(world->context, world->node, &style,
                                  MUI_PROPERTY_BIT(mui_propertyMinWidth) |
                                      MUI_PROPERTY_BIT(mui_propertyMinHeight)) == mui_success,
          "minimums of 0");
    Lengthen(world);
}

static muiResult Draw(World* world)
{
    const muiDrawInput input = {0, 1.0f, muiPaintText, &world->host};
    muiResult status = muiNode_BuildDrawList(world->context, world->root, &input);
    muiDrawList list = {0};
    if (muiContext_GetDrawList(world->context, &list) == mui_success)
    {
        // Commands are zeroed before they are written, padding and all.
        const unsigned char* bytes = (const unsigned char*)list.commands;
        world->output = 0;
        for (size_t i = 0; i < list.commandCount * sizeof *list.commands; i++)
        {
            world->output = Mix(world->output, bytes[i]);
        }
    }
    return status;
}

static muiResult HitTest(World* world)
{
    muiTextPosition at = {0, mui_affinityDownstream};
    muiResult status = muiNode_HitTestText(&world->host, world->node, 300.0f, 50.0f, 50.0f, &at);
    world->output = MixPosition(0, at);
    return status;
}

static muiResult Caret(World* world)
{
    muiTextCaret caret = {0};
    muiResult status = muiNode_GetTextCaret(
        &world->host, world->node, 300.0f, (muiTextPosition){2000, mui_affinityDownstream}, &caret);
    world->output =
        Mix(Mix(Mix(Mix(0, Bits(caret.x)), Bits(caret.y)), Bits(caret.height)), caret.rightToLeft);
    return status;
}

static muiResult RangeRects(World* world)
{
    muiRect rects[64] = {0};
    uint32_t count = 0;
    muiResult status =
        muiNode_GetTextRangeRects(&world->host, world->node, 300.0f, 0, 30, rects, 64, &count);
    world->output = Mix(0, count);
    for (uint32_t i = 0; i < 64; i++)
    {
        world->output = MixRect(world->output, rects[i]);
    }
    return status;
}

static muiResult Move(World* world)
{
    muiTextPosition at = {0, mui_affinityDownstream};
    muiResult status = muiNode_MoveTextPosition(&world->host, world->node, 300.0f,
                                                (muiTextPosition){1000, mui_affinityDownstream},
                                                mui_moveLineDown, 0.0f, &at);
    world->output = MixPosition(0, at);
    return status;
}

// One cluster right on screen, at a width that lays each paragraph on
// one line, on the last: longer than the first, the caret's, whose boxes
// the layout makes to follow it, so the move grows the boxes itself.
static muiResult MoveRight(World* world)
{
    muiTextPosition at = {0, mui_affinityDownstream};
    muiResult status = muiNode_MoveTextPosition(
        &world->host, world->node, 100000.0f,
        (muiTextPosition){sizeof s_long - 5, mui_affinityDownstream}, mui_moveRight, 0.0f, &at);
    world->output = MixPosition(0, at);
    return status;
}

// A line up from where the caret is drawn, no x given, from the line
// MoveRight moves on.
static muiResult MoveUpFromCaret(World* world)
{
    muiTextPosition at = {0, mui_affinityDownstream};
    muiResult status =
        muiNode_MoveTextPosition(&world->host, world->node, 100000.0f,
                                 (muiTextPosition){sizeof s_long - 5, mui_affinityDownstream},
                                 mui_moveLineUp, (float)NAN, &at);
    world->output = MixPosition(0, at);
    return status;
}

static muiResult EditMoveRight(World* world)
{
    return muiNode_MoveEditCaret(&world->host, world->node, mui_moveRight, false);
}

static muiResult EditMove(World* world)
{
    return muiNode_MoveEditCaret(&world->host, world->node, mui_moveLeft, false);
}

static muiResult EditMoveDown(World* world)
{
    return muiNode_MoveEditCaret(&world->host, world->node, mui_moveLineDown, false);
}

static muiResult EditPress(World* world)
{
    return muiNode_HandleEditPress(&world->host, world->node, 50.0f, 50.0f, 2, false);
}

static muiResult EditDrag(World* world)
{
    CHECK(muiNode_HandleEditPress(&world->host, world->node, 5.0f, 5.0f, 1, false) == mui_success,
          "pressed");
    return mui_success;
}

static void Pressed(World* world)
{
    Lengthen(world);
    (void)EditDrag(world);
    Lengthen(world);
}

static muiResult Drag(World* world)
{
    return muiNode_HandleEditDrag(&world->host, world->node, 80.0f, 80.0f);
}

static muiResult EditEvent(World* world)
{
    muiEvent event = {0};
    event.kind = mui_eventText;
    event.text = "z";
    event.length = 1;
    const muiTextEditInput input = {mui_keymapPc, NULL, NULL};
    muiTextEditOutcome outcome;
    return muiNode_HandleEditEvent(&world->host, world->node, &event, &input, &outcome);
}

static muiResult AccessAction(World* world)
{
    const muiAccessRequest request = {.action = mui_actionReplaceText,
                                      .target = muiNode_GetAccessId(world->node),
                                      .focus = 2,
                                      .text = s_long,
                                      .length = 800};
    muiTextEditOutcome outcome;
    return muiPerformTextAccessAction(&world->host, &request, &outcome);
}

// The same on a password, its offsets in the bullets it is shown as,
// three bytes each: the caret after the first.
static muiResult AccessActionMasked(World* world)
{
    const muiAccessRequest request = {.action = mui_actionReplaceText,
                                      .target = muiNode_GetAccessId(world->node),
                                      .focus = 3,
                                      .text = s_long,
                                      .length = 800};
    muiTextEditOutcome outcome;
    return muiPerformTextAccessAction(&world->host, &request, &outcome);
}

static muiResult Render(World* world)
{
    muiGlyphImage image;
    return muiTextService_RenderGlyph(world->service, muiFont_GetKey(world->liberation), 36, 40.0f,
                                      0.25f, &image, world->pixels, sizeof world->pixels);
}

static muiResult RenderColor(World* world)
{
    muiGlyphImage image;
    return muiTextService_RenderColorGlyph(world->service, muiFont_GetKey(world->color), 1, 40.0f,
                                           0.0f, 0, (muiLinearColor){1.0f, 1.0f, 1.0f, 1.0f},
                                           &image, world->pixels, sizeof world->pixels);
}

// MaulColor's glyph 33, a composite of two shapes, and 22, a linear
// gradient: surfaces, masks and stops of the service's own scratch.
static muiResult RenderPainted(World* world, uint32_t glyph)
{
    muiGlyphImage image;
    return muiTextService_RenderColorGlyph(world->service, muiFont_GetKey(world->color), glyph,
                                           10.0f, 0.0f, 0, (muiLinearColor){1.0f, 1.0f, 1.0f, 1.0f},
                                           &image, world->pixels, sizeof world->pixels);
}

static muiResult RenderComposite(World* world)
{
    return RenderPainted(world, 33);
}

static muiResult RenderGradient(World* world)
{
    return RenderPainted(world, 22);
}

// MaulBitmap's glyph 1, a PNG decoded into the service's scratch.
static muiResult RenderBitmap(World* world)
{
    muiGlyphImage image;
    return muiTextService_RenderColorGlyph(world->service, muiFont_GetKey(world->bitmap), 1, 10.0f,
                                           0.0f, 0, (muiLinearColor){1.0f, 1.0f, 1.0f, 1.0f},
                                           &image, world->pixels, sizeof world->pixels);
}

static muiResult RenderField(World* world)
{
    muiGlyphImage image;
    return muiTextService_RenderGlyphField(world->service, muiFont_GetKey(world->liberation), 36,
                                           30.0f, 4, &image, world->pixels, sizeof world->pixels);
}

static muiResult RenderMultiField(World* world)
{
    muiGlyphImage image;
    return muiTextService_RenderGlyphMultiField(world->service, muiFont_GetKey(world->liberation),
                                                36, 30.0f, 4, &image, world->pixels,
                                                sizeof world->pixels);
}

static muiResult CreateAtlas(World* world)
{
    muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
    return muiCreateGlyphAtlas(world->service, &def, &world->madeAtlas);
}

static muiResult AtlasGet(World* world)
{
    muiAtlasGlyph glyph;
    return muiGlyphAtlas_GetGlyph(world->atlas, muiFont_GetKey(world->liberation), 36, 40.0f, 0.0f,
                                  0.0f, &glyph);
}

static muiResult AtlasGetColor(World* world)
{
    muiAtlasGlyph glyph;
    return muiGlyphAtlas_GetColorGlyph(world->colorAtlas, muiFont_GetKey(world->color), 1, 40.0f,
                                       0.0f, 0.0f, 0, (muiLinearColor){1.0f, 1.0f, 1.0f, 1.0f},
                                       &glyph);
}

static muiResult AtlasGetField(World* world)
{
    muiAtlasGlyph glyph;
    return muiGlyphAtlas_GetGlyphField(world->atlas, muiFont_GetKey(world->liberation), 36, 30.0f,
                                       4, &glyph);
}

static muiResult AtlasGetMultiField(World* world)
{
    muiAtlasGlyph glyph;
    return muiGlyphAtlas_GetGlyphMultiField(world->fieldAtlas, muiFont_GetKey(world->liberation),
                                            36, 30.0f, 4, &glyph);
}

static const Step s_steps[] = {
    {"muiCreateContext", NULL, CreateContext, false},
    {"muiNode_SetAccessText", NULL, SetAccessText, false},
    {"muiNode_EnableAccess", NULL, Enable, false},
    {"muiNode_SetAccessRelation", Enabled, SetRelation, false},
    {"muiCreateAccessTree", NULL, CreateTree, false},
    {"muiAccessTree_Apply", Built, Apply, false},
    {"muiCreateTextService", NULL, CreateService, false},
    {"muiCreateFont", NULL, CreateFont, false},
    {"muiCreateFontFamily", NULL, CreateFamily, false},
    {"muiCreateTextBlock", NULL, CreateBlock, false},
    {"muiTextBlock_SetText", NULL, SetText, false},
    {"muiTextBlock_Replace", NULL, Replace, false},
    {"muiTextBlock_SetSpans", NULL, SetSpans, false},
    {"muiTextBlock_Type", NULL, Type, false},
    {"muiTextBlock_Paste", NULL, Paste, false},
    {"muiTextBlock_Erase", Typed, Erase, false},
    {"muiTextBlock_Undo", Typed, Undo, false},
    {"muiTextBlock_Redo", Undone, Redo, false},
    {"muiTextBlock_Compose", NULL, Compose, false},
    {"muiTextBlock_Compose over a selection", SelectedAfterTyping, Compose, false},
    {"muiTextBlock_Type over a composition", ComposedAfterTyping, Type, false},
    {"muiTextBlock_Paste over a composition", ComposedAfterTyping, Paste, false},
    {"muiTextBlock_Erase beside a composition", ComposedAfterTyping, Erase, false},
    {"muiTextBlock_SetComposition", NULL, SetComposition, false},
    {"muiMeasureText", Lengthen, Measure, false},
    {"muiMeasureTextBaseline", Lengthen, Baseline, false},
    {"muiNode_ComputeLayout", Shrunk, Layout, true},
    {"muiNode_ComputeLayout by baselines", Aligned, Layout, true},
    {"muiNode_ComputeLayout in run styles", Spanned, Layout, true},
    {"muiNode_ComputeLayout of a password", Masked, Layout, true},
    {"muiNode_BuildDrawList", Lengthen, Draw, true},
    {"muiNode_BuildDrawList scrolled", Scrolled, Draw, true},
    {"muiNode_BuildDrawList in run styles", Spanned, Draw, true},
    {"muiNode_BuildDrawList of a password", Masked, Draw, true},
    {"muiNode_BuildDrawList of a composition", Composing, Draw, true},
    {"muiNode_BuildDrawList of a composition on a short line", ComposingShortLine, Draw, true},
    {"muiNode_HitTestText", Lengthen, HitTest, false},
    {"muiNode_GetTextCaret", Lengthen, Caret, false},
    {"muiNode_GetTextRangeRects", Lengthen, RangeRects, false},
    {"muiNode_MoveTextPosition", Lengthen, Move, false},
    {"muiNode_HitTestText scrolled", Scrolled, HitTest, false},
    {"muiNode_GetTextCaret scrolled", Scrolled, Caret, false},
    {"muiNode_GetTextRangeRects scrolled", Scrolled, RangeRects, false},
    {"muiNode_MoveTextPosition scrolled", Scrolled, Move, false},
    {"muiNode_MoveTextPosition right", Lengthen, MoveRight, false},
    {"muiNode_MoveTextPosition up from the caret", Lengthen, MoveUpFromCaret, false},
    {"muiNode_MoveEditCaret right over a selection", Selected, EditMoveRight, false},
    {"muiNode_MoveEditCaret", Lengthen, EditMove, false},
    {"muiNode_MoveEditCaret from the end", Scrolled, EditMove, false},
    {"muiNode_MoveEditCaret down", Lengthen, EditMoveDown, false},
    {"muiNode_MoveEditCaret in run styles", Spanned, EditMove, false},
    {"muiNode_HandleEditPress", Lengthen, EditPress, false},
    {"muiNode_HandleEditDrag", Pressed, Drag, false},
    {"muiNode_HandleEditEvent", Lengthen, EditEvent, false},
    {"muiPerformTextAccessAction", Lengthen, AccessAction, false},
    {"muiPerformTextAccessAction on a password", Masked, AccessActionMasked, false},
    {"muiTextService_RenderGlyph", NULL, Render, false},
    {"muiTextService_RenderColorGlyph", NULL, RenderColor, false},
    {"muiTextService_RenderColorGlyph, a composite", NULL, RenderComposite, false},
    {"muiTextService_RenderColorGlyph, a gradient", NULL, RenderGradient, false},
    {"muiTextService_RenderColorGlyph, a bitmap", NULL, RenderBitmap, false},
    {"muiTextService_RenderGlyphField", NULL, RenderField, false},
    {"muiTextService_RenderGlyphMultiField", NULL, RenderMultiField, false},
    {"muiCreateGlyphAtlas", NULL, CreateAtlas, false},
    {"muiGlyphAtlas_GetGlyph", NULL, AtlasGet, false},
    {"muiGlyphAtlas_GetColorGlyph", NULL, AtlasGetColor, false},
    {"muiGlyphAtlas_GetGlyphField", NULL, AtlasGetField, false},
    {"muiGlyphAtlas_GetGlyphMultiField", NULL, AtlasGetMultiField, false},
};

// What the step could change: the block's text, selection, history and
// composition.
static uint64_t Digest(const World* world)
{
    const char* text = NULL;
    size_t length = 0;
    uint64_t hash = 14695981039346656037u;
    if (muiTextBlock_GetText(world->service, world->block, &text, &length) == mui_success)
    {
        for (size_t i = 0; i < length; i++)
        {
            hash = (hash ^ (unsigned char)text[i]) * 1099511628211u;
        }
    }
    muiTextSelection selection = {0};
    (void)muiTextBlock_GetSelection(world->service, world->block, &selection);
    hash = (hash ^ selection.anchor) * 1099511628211u;
    hash = (hash ^ selection.caret.offset) * 1099511628211u;
    hash = (hash ^ length) * 1099511628211u;
    bool undo = false;
    bool redo = false;
    (void)muiTextBlock_GetUndoState(world->service, world->block, &undo, &redo);
    hash = (hash ^ ((uint64_t)undo << 1 | (uint64_t)redo)) * 1099511628211u;
    uint32_t start = 0;
    uint32_t shown = 0;
    (void)muiTextBlock_GetComposition(world->service, world->block, &start, &shown);
    hash = (hash ^ start) * 1099511628211u;
    hash = (hash ^ shown) * 1099511628211u;
    return hash;
}

// What the step changed and answered.
static uint64_t Outcome(const World* world)
{
    return Mix(Digest(world), world->output);
}

// The step's world digest and result with nothing failing.
static uint64_t Clean(const Step* step, muiResult* resultOut, uint64_t* countOut)
{
    World world;
    MakeWorld(&world);
    if (step->prepare != NULL)
    {
        step->prepare(&world);
    }
    world.memory.count = 0;
    *resultOut = step->run(&world);
    *countOut = world.memory.count;
    uint64_t digest = Outcome(&world);
    FreeWorld(&world);
    CHECK(world.memory.live == 0, "nothing leaked");
    return digest;
}

static void Sweep(const Step* step, bool staysOut, bool verbose)
{
    muiResult clean = mui_success;
    uint64_t count = 0;
    uint64_t done = Clean(step, &clean, &count);
    CHECK(clean == mui_success, step->name);
    uint32_t refused = 0;
    uint32_t absorbed = 0;
    uint32_t wrong = 0;
    for (uint64_t failAt = 1; failAt <= count; failAt++)
    {
        World world;
        MakeWorld(&world);
        if (step->prepare != NULL)
        {
            step->prepare(&world);
        }
        uint64_t before = Digest(&world);
        world.memory.count = 0;
        world.memory.failAt = failAt;
        world.memory.staysOut = staysOut;
        muiResult result = step->run(&world);
        world.memory.failAt = 0;
        uint64_t after = Digest(&world);
        uint64_t answered = Outcome(&world);
        muiResult again = result == mui_errorCapacity ? step->run(&world) : result;
        uint64_t last = Outcome(&world);
        FreeWorld(&world);
        // Refused and unchanged, then done as a clean run does it; or the
        // failure absorbed with the clean run's outcome.
        bool good = result == mui_errorCapacity
                        ? (after == before || step->keeps) && again == mui_success && last == done
                        : result == mui_success && answered == done;
        refused += result == mui_errorCapacity && good;
        absorbed += result == mui_success && good;
        wrong += !good;
        if (!good || verbose)
        {
            fprintf(stderr, "%s: allocation %u of %u failing: %d, %s, then %d, %s\n", step->name,
                    (unsigned)failAt, (unsigned)count, (int)result,
                    after == before    ? "unchanged"
                    : answered == done ? "as done"
                                       : "changed",
                    (int)again, last == done ? "as done" : "not as done");
        }
        if (world.memory.live != 0)
        {
            fprintf(stderr, "%s: allocation %u failing leaks %lld\n", step->name, (unsigned)failAt,
                    (long long)world.memory.live);
        }
        CHECK(good, step->name);
        CHECK(world.memory.live == 0, "nothing leaked");
    }
    if (verbose || wrong != 0)
    {
        fprintf(stderr, "%s%s: %u allocations, %u refused, %u absorbed, %u wrong\n", step->name,
                staysOut ? ", memory staying out" : "", (unsigned)count, refused, absorbed, wrong);
    }
}

// What a read of the long text gives: its length, its lines, clusters
// and words, and whether it was read.
typedef struct Read
{
    bool read;
    size_t length;
    uint32_t lines;
    uint32_t clusters;
    uint32_t words;
    uint64_t allocations;
} Read;

static Read ReadText(uint64_t failAt)
{
    World world;
    MakeWorld(&world);
    Lengthen(&world);
    LayOut(&world);
    world.memory.count = 0;
    world.memory.failAt = failAt;
    muiAccessContent content = {0};
    const muiRect shown = {0.0f, 0.0f, 300.0f, 200.0f};
    Read read = {0};
    read.read = muiReadAccessText(&world.host, world.node, muiTextBlock_GetKey(world.block), &shown,
                                  &content);
    read.allocations = world.memory.count;
    world.memory.failAt = 0;
    read.length = content.length;
    read.lines = content.marks.lineCount;
    read.clusters = content.marks.clusterCount;
    read.words = content.marks.wordCount;
    FreeWorld(&world);
    CHECK(world.memory.live == 0, "nothing leaked");
    return read;
}

// muiReadAccessText reads the text whatever memory there is, and leaves
// out the lines and words it has no memory for, as its header says: with
// each of its allocations failing in turn, the text is whole, and its
// lines, with their clusters, and its words are whole or none.
static void SweepAccessText(void)
{
    const Read clean = ReadText(0);
    CHECK(clean.read && clean.length == sizeof s_long && clean.lines > 1 && clean.clusters > 0 &&
              clean.words > 0,
          "the long text read");
    bool held = true;
    for (uint64_t failAt = 1; failAt <= clean.allocations; failAt++)
    {
        Read read = ReadText(failAt);
        held = held && read.read && read.length == clean.length &&
               ((read.lines == clean.lines && read.clusters == clean.clusters) ||
                (read.lines == 0 && read.clusters == 0)) &&
               (read.words == clean.words || read.words == 0);
    }
    CHECK(clean.allocations > 0 && held, "the text read; lines and words whole or none");
}

int main(int argc, char** argv)
{
    for (size_t i = 0; i < sizeof s_long; i++)
    {
        s_long[i] = i % 7 == 6 ? ' ' : i % 211 == 210 ? '\n' : (char)('a' + i % 26);
    }
    int only = argc > 1 ? atoi(argv[1]) : -1;
    for (int i = 0; i < (int)(sizeof s_steps / sizeof s_steps[0]); i++)
    {
        if (only < 0 || only == i)
        {
            Sweep(&s_steps[i], false, only >= 0);
            Sweep(&s_steps[i], true, only >= 0);
        }
    }
    if (only < 0)
    {
        SweepAccessText();
    }
    return s_failures == 0 ? 0 : 1;
}
