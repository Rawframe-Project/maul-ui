// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Compositions and text carets of the Maul Window glue, on Maul Window's
// headless test backend, in Ahem at 10: a field at 20, 30 in the root,
// its content 6 in past its padding and border, holding "abc". A preedit
// goes into the block with its caret and segments, a style Maul UI does
// not know refused, a hidden caret reported, an empty preedit ending
// the composition; then the window's caret at a position of the text,
// carried through the content box and the field's place. Then the block
// edits: a preedit shown at its caret, at the end where the method hides
// it, taken out by an empty one; a paste asked for, other events passing
// the paste by, and a read answered with nothing pasting nothing.

#include "test_harness.h"

#include "maul-ui-window/clipboard.h"
#include "maul-ui-window/composition.h"
#include "maul-ui-window/glue.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_style.h"
#include "maul-window/context.h"
#include "maul-window/test.h"
#include "maul-window/window.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ahem.inc"

static const muiNodeId s_nullNode = {0, 0};

#define PADDING                                                                                    \
    (MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |       \
     MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom))
#define BORDER                                                                                     \
    (MUI_PROPERTY_BIT(mui_propertyBorderStart) | MUI_PROPERTY_BIT(mui_propertyBorderEnd) |         \
     MUI_PROPERTY_BIT(mui_propertyBorderTop) | MUI_PROPERTY_BIT(mui_propertyBorderBottom))

typedef struct Test
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    muiTextBlockId block;
    muiNodeId root;
    muiNodeId field;
    mwinWindowId window;
    muiWindowGlue* glue;
    // The glue's memory: the bytes live, counted.
    size_t live;
    // The caret the first frame placed in the window.
    mwinRect placed;
    int frame;
    bool done;
} Test;

static void MakeScene(Test* test)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    CHECK(muiCreateTextService(&def, &test->service) == mui_success, "service");
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    font.dataMode = mui_fontDataBorrow;
    muiFontId ahem = {0};
    CHECK(muiCreateFont(test->service, &font, &ahem) == mui_success &&
              muiSetDefaultFont(test->service, ahem) == mui_success,
          "Ahem");
    muiContextDef context = muiDefaultContextDef();
    CHECK(muiCreateContext(&context, &test->context) == mui_success, "context");
    test->host = (muiTextHost){test->service, test->context};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = "abc";
    blockDef.length = 3;
    CHECK(muiCreateTextBlock(test->service, &blockDef, &test->block) == mui_success, "block");

    muiNodeDef node = muiDefaultNodeDef();
    CHECK(muiCreateNode(test->context, &node, &test->root) == mui_success, "root");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding = (muiEdges){20.0f, 0.0f, 30.0f, 0.0f};
    CHECK(muiNode_SetLayoutValues(test->context, test->root, &layout, PADDING) == mui_success,
          "the root's padding");
    node.hostKey = muiTextBlock_GetKey(test->block);
    CHECK(muiCreateNode(test->context, &node, &test->field) == mui_success &&
              muiNode_InsertChild(test->context, test->root, test->field, s_nullNode) ==
                  mui_success,
          "the field");
    layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    layout.padding = (muiEdges){5.0f, 5.0f, 5.0f, 5.0f};
    layout.border = (muiEdges){1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(muiNode_SetLayoutValues(test->context, test->field, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent) | PADDING | BORDER) ==
              mui_success,
          "the field's box");
    muiTextStyle text = muiDefaultTextStyle();
    text.size = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiNode_SetTextValues(test->context, test->field, &text,
                                MUI_PROPERTY_BIT(mui_propertyFontSize)) == mui_success,
          "the field's text");
    const muiLayoutInput input = {640.0f, 480.0f, muiMeasureText, &test->host,
                                  0,      NULL,   {0, 0, 0, 0}};
    CHECK(muiComputeLayout(test->context, test->root, &input) == mui_success, "layout");
}

static bool TextIs(const Test* test, const char* expected)
{
    const char* text = NULL;
    size_t length = 0;
    return muiTextBlock_GetText(test->service, test->block, &text, &length) == mui_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static void TestComposition(Test* test)
{
    const mwinPreeditSegment segments[] = {
        {0, 1, mwin_preeditTarget},
        {1, 2, mwin_preeditUnderline},
    };
    mwinPreeditEvent preedit = {
        .text = "xyz",
        .length = 3,
        .caret = 2,
        .segments = segments,
        .segmentCount = 2,
    };
    int32_t caret = 0;
    uint32_t start = 0;
    uint32_t length = 0;
    CHECK(muiWindowSetComposition(test->service, test->block, 1, &preedit, &caret) == mui_success &&
              TextIs(test, "axyzbc") && caret == 3,
          "the preedit after the a, its caret 2 into it");
    CHECK(muiTextBlock_GetComposition(test->service, test->block, &start, &length) == mui_success &&
              start == 1 && length == 3,
          "the composition where the preedit is");
    // A style Maul UI does not know: the segments pass as they come.
    const mwinPreeditSegment unknown = {0, 1, 9};
    preedit.segments = &unknown;
    preedit.segmentCount = 1;
    caret = 7;
    CHECK(muiWindowSetComposition(test->service, test->block, 1, &preedit, &caret) ==
                  mui_errorInvalid &&
              caret == 7 && TextIs(test, "axyzbc"),
          "an unknown style refused, the composition and caret kept");
    preedit = (mwinPreeditEvent){.text = "w", .length = 1, .caret = -1};
    CHECK(muiWindowSetComposition(test->service, test->block, 1, &preedit, &caret) == mui_success &&
              TextIs(test, "awbc") && caret == -1,
          "a hidden caret");
    preedit = (mwinPreeditEvent){.caret = 0};
    caret = 7;
    CHECK(muiWindowSetComposition(test->service, test->block, 1, &preedit, &caret) == mui_success &&
              TextIs(test, "abc") && caret == -1 &&
              muiTextBlock_GetComposition(test->service, test->block, &start, &length) ==
                  mui_success &&
              length == 0,
          "an empty preedit ends the composition");
    CHECK(muiWindowSetComposition(test->service, test->block, 1, NULL, &caret) == mui_errorInvalid,
          "a NULL preedit");
}

// The caret before the b: in the field's content box where Maul UI puts
// it, then past the content's 6 and the field's 20, 30.
static void TestCaret(Test* test)
{
    muiRect content = muiNode_GetContentRect(test->context, test->field);
    CHECK(content.x == 6.0f && content.y == 6.0f, "the content box 6 in");
    muiTextCaret caret = {0};
    const muiTextPosition position = {1, mui_affinityDownstream};
    CHECK(muiTextGetCaret(&test->host, test->field, content.width, position, &caret) ==
                  mui_success &&
              caret.x == 10.0f && caret.height > 0.0f,
          "Ahem's caret 10 in");
    mwinRect placed = {0};
    CHECK(muiWindowGlue_SetTextCaret(test->glue, &test->host, test->field, position, &placed) ==
                  mui_success &&
              placed.x == 36.0f && placed.y == 36.0f + caret.y && placed.width == 0.0f &&
              placed.height == caret.height,
          "the window's caret past the content and the field's place");
    test->placed = placed;
    CHECK(muiWindowGlue_SetTextCaret(test->glue, NULL, test->field, position, &placed) ==
              mui_errorInvalid,
          "a NULL host");
}

// Blocks aligned by hand, as not every C library has aligned_alloc.
static void* Allocate(size_t size, size_t alignment, void* context)
{
    size_t room = alignment > sizeof(void*) ? alignment : sizeof(void*);
    unsigned char* raw = malloc(size + room + sizeof(void*));
    if (raw == NULL)
    {
        return NULL;
    }
    uintptr_t start = (uintptr_t)(raw + sizeof(void*));
    unsigned char* block = raw + sizeof(void*) + (room - start % room) % room;
    memcpy(block - sizeof(void*), &raw, sizeof raw);
    *(size_t*)context += size;
    return block;
}

static void Release(void* block, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    void* raw = NULL;
    memcpy(&raw, (unsigned char*)block - sizeof(void*), sizeof raw);
    *(size_t*)context -= size;
    free(raw);
}

static mwinResult Init(mwinContext* windows, void* user)
{
    Test* test = user;
    mwinWindowDef window = mwinDefaultWindowDef();
    window.size = (mwinSize){640.0f, 480.0f};
    CHECK(mwinCreateWindow(windows, &window, &test->window, NULL) == mwin_success, "a window");
    MakeScene(test);
    muiWindowGlueDef def = muiDefaultWindowGlueDef();
    def.windows = windows;
    def.window = test->window;
    def.context = test->context;
    def.root = test->root;
    def.allocator = (muiAllocator){Allocate, Release, &test->live};
    CHECK(muiCreateWindowGlue(&def, &test->glue) == mui_success, "a glue");
    return mwin_success;
}

static void TestEditing(Test* test)
{
    muiTextEditDef edit = muiDefaultTextEditDef();
    edit.purpose = mui_purposeEmail;
    CHECK(muiTextBlock_SetText(test->service, test->block, "abc", 3) == mui_success &&
              muiTextBlock_SetEditing(test->service, test->block, &edit) == mui_success,
          "editing");
    mwinPreeditEvent preedit = {.text = "xy", .length = 2, .caret = -1};
    bool changed = false;
    muiTextSelection selection;
    CHECK(muiWindowCompose(test->service, test->block, &preedit, &changed) == mui_success &&
              changed && TextIs(test, "abcxy") &&
              muiTextBlock_GetSelection(test->service, test->block, &selection) == mui_success &&
              selection.caret.offset == 5,
          "a preedit, its hidden caret at its end");
    preedit.caret = 3;
    CHECK(muiWindowCompose(test->service, test->block, &preedit, NULL) == mui_errorInvalid &&
              muiWindowCompose(test->service, test->block, NULL, NULL) == mui_errorInvalid,
          "a caret past the preedit, no preedit");
    preedit = (mwinPreeditEvent){.text = "", .length = 0, .caret = -1};
    CHECK(muiWindowCompose(test->service, test->block, &preedit, &changed) == mui_success &&
              changed && TextIs(test, "abc"),
          "taken out");
    muiWindowGlue_WriteClipboard(test->glue, "abc", 3);
    muiWindowGlue_WriteClipboard(NULL, "abc", 3);
    CHECK(muiWindowGlue_RequestPaste(test->glue) == mui_success &&
              muiWindowGlue_RequestPaste(NULL) == mui_errorInvalid,
          "a paste asked for");
    mwinEvent event = {.type = mwin_eventKeyDown, .window = test->window};
    CHECK(muiWindowGlue_Paste(test->glue, test->service, test->block, &event, &changed) ==
                  mui_empty &&
              !changed,
          "another event passes");
    event.type = mwin_eventRequestCompleted;
    event.data.completion =
        (mwinCompletion){.kind = mwin_requestClipboardRead, .outcome = mwin_outcomeDenied};
    CHECK(muiWindowGlue_Paste(test->glue, test->service, test->block, &event, NULL) == mui_empty,
          "a refused read passes");
    event.data.completion.outcome = mwin_outcomeDone;
    CHECK(muiWindowGlue_Paste(test->glue, test->service, test->block, &event, &changed) ==
                  mui_success &&
              !changed && TextIs(test, "abc"),
          "nothing read, nothing pasted");
    CHECK(muiWindowGlue_Paste(test->glue, NULL, test->block, &event, NULL) == mui_errorInvalid &&
              muiWindowGlue_Paste(test->glue, test->service, test->block, NULL, NULL) ==
                  mui_errorInvalid,
          "no service or event");
    CHECK(muiWindowGlue_Paste(NULL, test->service, test->block, &event, NULL) == mui_errorInvalid,
          "no glue");
    CHECK(muiWindowGlue_RequestKeyboard(test->glue, &test->host, test->field) == mui_success,
          "the keyboard asked for");
    CHECK(muiWindowGlue_RequestKeyboard(NULL, &test->host, test->field) == mui_errorInvalid &&
              muiWindowGlue_RequestKeyboard(test->glue, NULL, test->field) == mui_errorInvalid,
          "the keyboard asked for without a glue or a host");
}

static mwinFrameResult Frame(mwinContext* windows, void* user)
{
    Test* test = user;
    bool enabled = false;
    mwinRect caret = {0};
    bool visible = false;
    mwinInputPurpose purpose = mwin_purposeText;
    // The platform carries out the requests as a frame ends, so each
    // frame reads what the one before asked for.
    switch (test->frame++)
    {
    case 0:
        TestComposition(test);
        TestCaret(test);
        TestEditing(test);
        return mwin_frameContinue;
    case 1:
        CHECK(mwinTestGetTextInput(windows, test->window, &enabled, &caret) == mwin_success &&
                  enabled && caret.x == test->placed.x && caret.y == test->placed.y &&
                  caret.height == test->placed.height,
              "the platform's caret where the field's is");
        CHECK(mwinTestGetVirtualKeyboard(windows, test->window, &visible, &purpose) ==
                      mwin_success &&
                  visible && purpose == mwin_purposeEmail,
              "the keyboard shown for the field's purpose");
        CHECK(muiWindowGlue_RequestKeyboard(test->glue, &test->host, s_nullNode) == mui_success,
              "the keyboard let go");
        return mwin_frameContinue;
    default:
        CHECK(mwinTestGetVirtualKeyboard(windows, test->window, &visible, &purpose) ==
                      mwin_success &&
                  !visible,
              "the keyboard hidden");
        test->done = true;
        return mwin_frameStop;
    }
}

int main(void)
{
    Test test = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.init = Init;
    def.frame = Frame;
    def.user = &test;
    CHECK(mwinRun(&def) == mwin_success && test.done, "the program ran");
    muiDestroyWindowGlue(test.glue);
    CHECK(test.live == 0, "the glue's memory all given back");
    muiDestroyContext(test.context);
    muiDestroyTextService(test.service);
    return s_failures == 0 ? 0 : 1;
}
