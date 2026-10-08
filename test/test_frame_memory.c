// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Frames allocate nothing (record mui-0001): a scene of every part, its
// context and its text service each on a counting allocator, is run
// through warm-up frames, then frame after frame of pointer moves,
// presses and wheel scrolling, focus moving, a hover's transition
// running, an editing field's caret moving, layout and painting; neither
// allocator counts an allocation more after the warm-up.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/font.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"
#include "maul-ui/text_style.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "liberation_sans.inc"

enum
{
    ROWS = 30,
    FRAMES = 120,
    MS = 1000000
};

typedef struct Counter
{
    uint64_t allocations;
} Counter;

// Any alignment, as Windows has no aligned_alloc: the block from malloc
// is kept just before the aligned one.
static void* Allocate(size_t size, size_t alignment, void* context)
{
    ((Counter*)context)->allocations++;
    size_t room = alignment > sizeof(void*) ? alignment : sizeof(void*);
    unsigned char* raw = malloc(size + room + sizeof(void*));
    if (raw == nullptr)
    {
        return nullptr;
    }
    uintptr_t start = (uintptr_t)(raw + sizeof(void*));
    unsigned char* aligned = raw + sizeof(void*) + (room - start % room) % room;
    memcpy(aligned - sizeof(void*), &raw, sizeof raw);
    return aligned;
}

static void Release(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    if (memory != nullptr)
    {
        void* raw = nullptr;
        memcpy(&raw, (unsigned char*)memory - sizeof(void*), sizeof raw);
        free(raw);
    }
}

typedef struct Scene
{
    Counter contextMemory;
    Counter textMemory;
    muiContext* context;
    muiTextService* service;
    muiTextHost host;
    muiNodeId root;
    muiNodeId field;
    muiNodeId scroller;
    uint64_t timeNs;
} Scene;

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

// A node showing text, a child of parent.
static muiNodeId Label(Scene* scene, muiNodeId parent, const char* text, muiTextBlockId* blockOut)
{
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = text;
    blockDef.length = strlen(text);
    CHECK(muiCreateTextBlock(scene->service, &blockDef, blockOut) == mui_success, "a block");
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(*blockOut);
    muiNodeId node = {0, 0};
    CHECK(muiCreateNode(scene->context, &def, &node) == mui_success &&
              muiNode_InsertChild(scene->context, parent, node, (muiNodeId){0, 0}) == mui_success,
          "a label");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(scene->context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
          "host content");
    return node;
}

static muiStyleId RowClass(muiContext* context)
{
    const muiStyleDef styleDef = muiDefaultStyleDef();
    muiStyleId row = {0, 0};
    CHECK(muiCreateStyle(context, &styleDef, &row) == mui_success, "a row class");
    muiVisualStyle visual = muiDefaultVisualStyle();
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    visual.background = (muiColor){0.9f, 0.9f, 0.9f, 1.0f};
    CHECK(muiStyle_SetVisualValues(context, row, mui_variantBase, &visual, background) ==
              mui_success,
          "a background");
    visual.background = (muiColor){0.6f, 0.7f, 1.0f, 1.0f};
    CHECK(muiStyle_SetVisualValues(context, row, mui_variantHovered, &visual, background) ==
              mui_success,
          "lighter when hovered");
    muiTransitionDef def = muiDefaultTransitionDef();
    def.durationNs = 150 * (uint64_t)MS;
    muiTransitionId fade = {0, 0};
    CHECK(muiCreateTransition(context, &def, &fade) == mui_success &&
              muiStyle_SetTransition(context, row, mui_variantBase, fade, mui_groupVisual,
                                     background) == mui_success,
          "over 150 ms");
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    CHECK(muiStyle_SetInteractionValues(context, row, mui_variantBase, &interaction,
                                        MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
          "focusable");
    return row;
}

static void MakeScene(Scene* scene)
{
    *scene = (Scene){0};
    muiContextDef contextDef = muiDefaultContextDef();
    contextDef.allocator = (muiAllocator){Allocate, Release, &scene->contextMemory};
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    serviceDef.allocator = (muiAllocator){Allocate, Release, &scene->textMemory};
    muiFontDef font = muiDefaultFontDef();
    font.data = s_liberationSans;
    font.size = sizeof s_liberationSans;
    font.dataMode = mui_fontDataBorrow;
    muiFontId fontId = {0, 0};
    CHECK(muiCreateContext(&contextDef, &scene->context) == mui_success &&
              muiCreateTextService(&serviceDef, &scene->service) == mui_success &&
              muiCreateFont(scene->service, &font, &fontId) == mui_success &&
              muiSetDefaultFont(scene->service, fontId) == mui_success,
          "a context, a service and a font");
    scene->host = (muiTextHost){scene->service, scene->context};
    muiContext* context = scene->context;
    muiNodeDef def = muiDefaultNodeDef();
    CHECK(muiCreateNode(context, &def, &scene->root) == mui_success &&
              muiCreateNode(context, &def, &scene->scroller) == mui_success,
          "a root and a scroller");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.sizing.width = Length(400.0f);
    layout.sizing.height = Length(300.0f);
    const muiPropertyMask column = MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                                   MUI_PROPERTY_BIT(mui_propertyWidth) |
                                   MUI_PROPERTY_BIT(mui_propertyHeight);
    CHECK(muiNode_SetLayoutValues(context, scene->root, &layout, column) == mui_success,
          "a column");
    muiTextBlockId block = {0, 0};
    (void)Label(scene, scene->root, "Settings, in a heading", &block);
    scene->field = Label(scene, scene->root, "Name: Ada Lovelace", &block);
    muiTextEditDef edit = muiDefaultTextEditDef();
    CHECK(muiTextBlock_SetEditing(scene->service, block, &edit) == mui_success, "editing");
    layout.sizing.height = Length(150.0f);
    layout.scrollAxes = mui_scrollVertical;
    CHECK(muiNode_InsertChild(context, scene->root, scene->scroller, (muiNodeId){0, 0}) ==
                  mui_success &&
              muiNode_SetLayoutValues(context, scene->scroller, &layout,
                                      column | MUI_PROPERTY_BIT(mui_propertyScrollAxes)) ==
                  mui_success,
          "a scroll container");
    muiStyleId row = RowClass(context);
    for (uint32_t i = 0; i < ROWS; i++)
    {
        char text[32] = "Row 00, a choice to make";
        text[4] = (char)('0' + i / 10);
        text[5] = (char)('0' + i % 10);
        muiNodeId node = Label(scene, scene->scroller, text, &block);
        CHECK(muiNode_SetClasses(context, node, &row, 1) == mui_success, "a row");
    }
}

// Lays out and paints a frame, and takes what it reported.
static void Frame(Scene* scene)
{
    scene->timeNs += 16 * (uint64_t)MS;
    const muiLayoutInput layoutInput = {400.0f,        300.0f,  muiMeasureText, &scene->host,
                                        scene->timeNs, nullptr, {0, 0, 0, 0}};
    const muiDrawInput drawInput = {1, 1.0f, muiPaintText, &scene->host};
    CHECK(muiComputeLayout(scene->context, scene->root, &layoutInput) == mui_success &&
              muiBuildDrawList(scene->context, scene->root, &drawInput) == mui_success,
          "a frame");
    muiPointerRecord record;
    while (muiNextPointerRecord(scene->context, &record) == mui_success)
    {
    }
    muiNotification notification;
    while (muiNextNotification(scene->context, &notification) == mui_success)
    {
    }
}

// One frame's input, of every kind, then the frame.
static void Step(Scene* scene, uint32_t i)
{
    muiContext* context = scene->context;
    bool handled = false;
    muiPointerEvent move = {.timeNs = scene->timeNs,
                            .pointer = 1,
                            .kind = mui_pointerMouse,
                            .action = mui_pointerMove,
                            .x = 20.0f + (float)(i % 5) * 30.0f,
                            .y = 60.0f + (float)(i * 7 % 140)};
    CHECK(muiPointerInput(context, scene->root, &move) == mui_success, "a move");
    if (i % 10 == 0)
    {
        muiPointerEvent press = move;
        press.action = mui_pointerPress;
        press.buttons = 1;
        muiPointerEvent release = move;
        release.action = mui_pointerRelease;
        CHECK(muiPointerInput(context, scene->root, &press) == mui_success &&
                  muiPointerInput(context, scene->root, &release) == mui_success,
              "a click");
    }
    const muiWheelEvent wheel = {
        .timeNs = scene->timeNs, .x = 50.0f, .y = 120.0f, .deltaY = i % 8 < 4 ? 12.0f : -12.0f};
    const muiNavigationEvent next = {.timeNs = scene->timeNs, .action = mui_navigateNext};
    CHECK(muiWheelInput(context, scene->root, &wheel, &handled) == mui_success &&
              muiNavigationInput(context, scene->root, &next, &handled) == mui_success,
          "a wheel and a tab");
    CHECK(muiTextEditMove(&scene->host, scene->field, i % 2 == 0 ? mui_moveLeft : mui_moveRight,
                          i % 4 == 0) == mui_success,
          "the caret moved");
    Frame(scene);
}

int main(void)
{
    Scene scene;
    MakeScene(&scene);
    for (uint32_t i = 0; i < FRAMES; i++)
    {
        Step(&scene, i);
    }
    const Counter context = scene.contextMemory;
    const Counter text = scene.textMemory;
    for (uint32_t i = 0; i < FRAMES; i++)
    {
        Step(&scene, i);
    }
    CHECK(scene.contextMemory.allocations == context.allocations, "the context allocates nothing");
    CHECK(scene.textMemory.allocations == text.allocations, "the text service allocates nothing");
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
    return s_failures == 0 ? 0 : 1;
}
