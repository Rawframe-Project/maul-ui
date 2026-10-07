// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text editor (record mui-0006): typing undone word by word and
// deletions in runs, redo dropped by a new edit, the undo limit, a
// field's rules (read-only, password, single line, number filters, a
// maximum length), text changed elsewhere, and presses, drags and moves
// through laid-out text.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <string.h>

#include "ahem.inc"

static const muiNodeId s_nullNode = {0, 0};

typedef struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextHost host;
    muiTextBlockId block;
    muiNodeId node;
} Scene;

// A service with Ahem at size 10, a block of text editing under rules,
// and a root node 1000 wide showing it.
static Scene MakeScene(const char* text, muiTextEditFlags flags)
{
    Scene scene = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    CHECK(muiCreateTextService(&def, &scene.service) == mui_success, "service");
    muiFontDef font = muiDefaultFontDef();
    font.data = s_ahem;
    font.size = sizeof s_ahem;
    muiFontId fontId = {0, 0};
    CHECK(muiCreateFont(scene.service, &font, &fontId) == mui_success &&
              muiSetDefaultFont(scene.service, fontId) == mui_success,
          "the default font");
    muiContextDef context = muiDefaultContextDef();
    CHECK(muiCreateContext(&context, &scene.context) == mui_success, "context");
    scene.host = (muiTextHost){scene.service, scene.context};
    CHECK(muiCreateTextBlock(scene.service, text, strlen(text), &scene.block) == mui_success,
          "block");
    muiTextEditDef edit = muiDefaultTextEditDef();
    edit.flags = flags;
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &edit) == mui_success, "editing");
    muiNodeDef node = muiDefaultNodeDef();
    node.hostKey = muiTextBlock_GetKey(scene.block);
    scene.node = s_nullNode;
    CHECK(muiCreateNode(scene.context, &node, &scene.node) == mui_success, "node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 10.0f, mui_dimensionValue};
    CHECK(muiNode_SetLayoutValues(scene.context, scene.node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success &&
              muiNode_SetTextValues(scene.context, scene.node, &style,
                                    MUI_PROPERTY_BIT(mui_propertyFontSize)) == mui_success,
          "host content");
    return scene;
}

static void FreeScene(Scene* scene)
{
    muiDestroyContext(scene->context);
    muiDestroyTextService(scene->service);
}

static void Layout(Scene* scene)
{
    const muiLayoutInput layout = {1000.0f, 1000.0f, muiMeasureText, &scene->host,
                                   0,       NULL,    {0, 0, 0, 0}};
    CHECK(muiNode_MarkContentChanged(scene->context, scene->node) == mui_success &&
              muiComputeLayout(scene->context, scene->node, &layout) == mui_success,
          "laid out");
}

static bool Holds(const Scene* scene, const char* expected)
{
    const char* text = NULL;
    size_t length = 0;
    return muiTextBlock_GetText(scene->service, scene->block, &text, &length) == mui_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static muiTextSelection Selection(const Scene* scene)
{
    muiTextSelection selection = {0};
    CHECK(muiTextBlock_GetSelection(scene->service, scene->block, &selection) == mui_success,
          "a selection");
    return selection;
}

static bool Selects(const Scene* scene, uint32_t anchor, uint32_t caret)
{
    muiTextSelection selection = Selection(scene);
    return selection.anchor == anchor && selection.caret.offset == caret;
}

static void Type(Scene* scene, const char* text)
{
    CHECK(muiTextBlock_Type(scene->service, scene->block, text, strlen(text), NULL) == mui_success,
          "typed");
}

static void Paste(Scene* scene, const char* text)
{
    CHECK(muiTextBlock_Paste(scene->service, scene->block, text, strlen(text), NULL) == mui_success,
          "pasted");
}

static bool Undo(Scene* scene)
{
    bool changed = false;
    return muiTextBlock_Undo(scene->service, scene->block, &changed) == mui_success && changed;
}

static bool Redo(Scene* scene)
{
    bool changed = false;
    return muiTextBlock_Redo(scene->service, scene->block, &changed) == mui_success && changed;
}

static void Select(Scene* scene, uint32_t anchor, uint32_t caret)
{
    const muiTextSelection selection = {anchor, {caret, mui_affinityDownstream}};
    CHECK(muiTextBlock_Select(scene->service, scene->block, selection) == mui_success, "selected");
}

static void TestCalls(void)
{
    Scene scene = MakeScene("abc", 0);
    muiTextEditDef def = muiDefaultTextEditDef();
    CHECK(def.flags == 0 && def.filter == mui_filterNone && def.maxLength == 0 &&
              def.undoLimit == 100,
          "the default rules");
    CHECK(Selects(&scene, 3, 3), "the caret at the end");
    def.flags = 8;
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &def) == mui_errorInvalid,
          "a flag out of range");
    def.flags = 0;
    def.filter = 3;
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &def) == mui_errorInvalid,
          "a filter out of range");
    CHECK(muiTextBlock_Select(scene.service, scene.block,
                              (muiTextSelection){4, {0, mui_affinityDownstream}}) ==
              mui_errorInvalid,
          "past the text");
    def.filter = mui_filterNone;
    muiTextBlockId other = {0, 0};
    CHECK(muiCreateTextBlock(scene.service, "\xC3\xA9", 2, &other) == mui_success, "another");
    muiTextSelection selection;
    CHECK(muiTextBlock_GetSelection(scene.service, other, &selection) == mui_errorInvalid,
          "a block not editing");
    CHECK(muiTextBlock_SetEditing(scene.service, other, &def) == mui_success &&
              muiTextBlock_Select(scene.service, other,
                                  (muiTextSelection){1, {0, mui_affinityDownstream}}) ==
                  mui_errorInvalid,
          "inside a character");
    CHECK(muiTextBlock_SetEditing(scene.service, other, NULL) == mui_success &&
              muiTextBlock_GetSelection(scene.service, other, &selection) == mui_errorInvalid,
          "editing ended");
    CHECK(muiTextBlock_Type(scene.service, scene.block, NULL, 1, NULL) == mui_errorInvalid,
          "no text");
    CHECK(muiTextBlock_Erase(scene.service, scene.block, 2, NULL) == mui_errorInvalid,
          "a deletion out of range");
    CHECK(muiDestroyTextBlock(scene.service, other) == mui_success &&
              muiTextBlock_Type(scene.service, other, "a", 1, NULL) == mui_errorStale,
          "a block gone");
    FreeScene(&scene);
}

// Typing goes back a word at a time; a moved caret starts a new edit;
// a new edit drops what was undone.
static void TestTyping(void)
{
    Scene scene = MakeScene("", 0);
    const char* keys[] = {"a", "b", " ", "c", "d"};
    for (int i = 0; i < 5; i++)
    {
        Type(&scene, keys[i]);
    }
    CHECK(Holds(&scene, "ab cd") && Selects(&scene, 5, 5), "typed");
    CHECK(Undo(&scene) && Holds(&scene, "ab ") && Selects(&scene, 3, 3), "the second word");
    CHECK(Undo(&scene) && Holds(&scene, "") && Selects(&scene, 0, 0), "the first word");
    CHECK(!Undo(&scene), "nothing more");
    CHECK(Redo(&scene) && Redo(&scene) && Holds(&scene, "ab cd") && !Redo(&scene), "redone");
    Select(&scene, 0, 0);
    Type(&scene, "x");
    Type(&scene, "y");
    CHECK(Holds(&scene, "xyab cd"), "typed at the start");
    Select(&scene, 7, 7);
    Type(&scene, "z");
    CHECK(Undo(&scene) && Holds(&scene, "xyab cd"), "a moved caret, a new edit");
    CHECK(Undo(&scene) && Holds(&scene, "ab cd"), "the typing before it whole");
    Select(&scene, 5, 5);
    Type(&scene, "q");
    bool undo = false;
    bool redo = true;
    CHECK(muiTextBlock_GetUndoState(scene.service, scene.block, &undo, &redo) == mui_success &&
              undo && !redo && !Redo(&scene),
          "what was undone dropped");
    // Typing over a selection comes back with it.
    Select(&scene, 0, 2);
    Type(&scene, "X");
    CHECK(Holds(&scene, "X cdq"), "replaced");
    CHECK(Undo(&scene) && Holds(&scene, "ab cdq") && Selects(&scene, 0, 2), "the selection back");
    FreeScene(&scene);
}

// Backspaces and forward deletions go back in runs; a paste alone.
static void TestDeleting(void)
{
    Scene scene = MakeScene("hello world", 0);
    for (int i = 0; i < 3; i++)
    {
        CHECK(muiTextBlock_Erase(scene.service, scene.block, mui_deleteBackward, NULL) ==
                  mui_success,
              "backspace");
    }
    CHECK(Holds(&scene, "hello wo"), "three back");
    Select(&scene, 0, 0);
    for (int i = 0; i < 2; i++)
    {
        CHECK(muiTextBlock_Erase(scene.service, scene.block, mui_deleteForward, NULL) ==
                  mui_success,
              "delete");
    }
    CHECK(Holds(&scene, "llo wo"), "two forward");
    CHECK(Undo(&scene) && Holds(&scene, "hello wo") && Selects(&scene, 0, 0), "forward run");
    CHECK(Undo(&scene) && Holds(&scene, "hello world") && Selects(&scene, 11, 11), "back run");
    bool changed = true;
    CHECK(muiTextBlock_Erase(scene.service, scene.block, mui_deleteForward, &changed) ==
                  mui_success &&
              !changed,
          "nothing past the end");
    CHECK(muiTextBlock_EraseTo(scene.service, scene.block, 6, &changed) == mui_success && changed &&
              Holds(&scene, "hello "),
          "to a word's start");
    Paste(&scene, "there");
    Paste(&scene, "!");
    CHECK(Undo(&scene) && Holds(&scene, "hello there"), "a paste alone");
    FreeScene(&scene);
}

static void TestLimit(void)
{
    Scene scene = MakeScene("", 0);
    muiTextEditDef def = muiDefaultTextEditDef();
    def.undoLimit = 2;
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &def) == mui_success, "limited");
    Paste(&scene, "a");
    Paste(&scene, "b");
    Paste(&scene, "c");
    CHECK(Undo(&scene) && Undo(&scene) && !Undo(&scene) && Holds(&scene, "a"), "two kept");
    CHECK(Redo(&scene) && Redo(&scene) && Holds(&scene, "abc"), "and redone");
    def.undoLimit = 0;
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &def) == mui_success, "none");
    Paste(&scene, "d");
    CHECK(!Undo(&scene) && Holds(&scene, "abcd"), "nothing kept");
    FreeScene(&scene);
}

static void TestRules(void)
{
    Scene scene = MakeScene("secret", mui_editReadOnly | mui_editPassword);
    bool changed = true;
    CHECK(muiTextBlock_Type(scene.service, scene.block, "x", 1, &changed) == mui_success &&
              !changed && Holds(&scene, "secret"),
          "read-only");
    Select(&scene, 0, 6);
    const char* text = NULL;
    size_t length = 1;
    CHECK(muiTextBlock_GetSelectedText(scene.service, scene.block, &text, &length) == mui_success &&
              length == 0,
          "a password copies nothing");
    FreeScene(&scene);

    scene = MakeScene("", 0);
    Paste(&scene, "a\r\nb\nc\xE2\x80\xA8"
                  "d\x08\x7F");
    CHECK(Holds(&scene, "a b c d"), "a single line: breaks as spaces, controls gone");
    FreeScene(&scene);
    scene = MakeScene("", mui_editMultiline);
    Paste(&scene, "a\r\nb\x01");
    CHECK(Holds(&scene, "a\r\nb"), "lines kept");
    Select(&scene, 0, 4);
    CHECK(muiTextBlock_GetSelectedText(scene.service, scene.block, &text, &length) == mui_success &&
              length == 4 && memcmp(text, "a\r\nb", 4) == 0,
          "the selected text");
    FreeScene(&scene);
}

static void Filtered(muiTextFilter filter, const char* start, const char* typed, uint32_t caret,
                     const char* expected)
{
    Scene scene = MakeScene(start, 0);
    muiTextEditDef def = muiDefaultTextEditDef();
    def.filter = filter;
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &def) == mui_success, "a filter");
    Select(&scene, caret, caret);
    for (const char* at = typed; *at != '\0'; at++)
    {
        CHECK(muiTextBlock_Type(scene.service, scene.block, at, 1, NULL) == mui_success, "typed");
    }
    CHECK(Holds(&scene, expected), expected);
    FreeScene(&scene);
}

static void TestFilters(void)
{
    Filtered(mui_filterInteger, "", "-12a3.4-", 0, "-1234");
    Filtered(mui_filterInteger, "-5", "+3", 0, "-5");
    Filtered(mui_filterDecimal, "", "+1.2.3", 0, "+1.23");
    Filtered(mui_filterDecimal, "1.5", ".9", 1, "19.5");
    Filtered(mui_filterDecimal, "", "-.5", 0, "-.5");
    // A selection nothing typed may replace stays.
    Scene scene = MakeScene("12", 0);
    muiTextEditDef def = muiDefaultTextEditDef();
    def.filter = mui_filterInteger;
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &def) == mui_success, "a filter");
    Select(&scene, 0, 2);
    Type(&scene, "a");
    CHECK(Holds(&scene, "12") && Selects(&scene, 0, 2), "the selection kept");
    FreeScene(&scene);
}

static void TestLength(void)
{
    Scene scene = MakeScene("", 0);
    muiTextEditDef def = muiDefaultTextEditDef();
    def.maxLength = 3;
    CHECK(muiTextBlock_SetEditing(scene.service, scene.block, &def) == mui_success, "limited");
    // e and a combining acute are one cluster.
    Paste(&scene, "ae\xCC\x81"
                  "bc");
    CHECK(Holds(&scene, "ae\xCC\x81"
                        "b"),
          "cut to three clusters");
    bool changed = true;
    CHECK(muiTextBlock_Type(scene.service, scene.block, "d", 1, &changed) == mui_success &&
              !changed,
          "full");
    Select(&scene, 0, 1);
    Type(&scene, "xy");
    CHECK(Holds(&scene, "xe\xCC\x81"
                        "b"),
          "over a selection, as much as fits");
    FreeScene(&scene);
}

// Text changed elsewhere empties the history, the selection kept in it.
static void TestElsewhere(void)
{
    Scene scene = MakeScene("", 0);
    Type(&scene, "hello");
    CHECK(muiTextBlock_SetText(scene.service, scene.block, "hi", 2) == mui_success, "set");
    bool undo = true;
    bool redo = true;
    CHECK(muiTextBlock_GetUndoState(scene.service, scene.block, &undo, &redo) == mui_success &&
              !undo && !redo && Selects(&scene, 2, 2),
          "history gone, the caret kept within");
    FreeScene(&scene);
}

// Presses place the caret, select words and paragraphs, and drags extend
// by the same unit; moves collapse a selection to the edge they go to.
static void TestPointer(void)
{
    Scene scene = MakeScene("ab cd\nef gh", mui_editMultiline);
    Layout(&scene);
    const muiTextHost* host = &scene.host;
    CHECK(muiTextEditPress(host, scene.node, 21.0f, 5.0f, 1, false) == mui_success &&
              Selects(&scene, 2, 2),
          "a click between b and the space");
    CHECK(muiTextEditDrag(host, scene.node, 41.0f, 5.0f) == mui_success && Selects(&scene, 2, 4),
          "dragged a cluster at a time");
    CHECK(muiTextEditPress(host, scene.node, 35.0f, 5.0f, 2, false) == mui_success &&
              Selects(&scene, 3, 5),
          "a double click, a word");
    CHECK(muiTextEditDrag(host, scene.node, 5.0f, 5.0f) == mui_success && Selects(&scene, 5, 0),
          "dragged back by words, keeping the first");
    CHECK(muiTextEditDrag(host, scene.node, 31.0f, 5.0f) == mui_success && Selects(&scene, 3, 5),
          "back to the word's start, the word again");
    CHECK(muiTextEditPress(host, scene.node, 29.0f, 5.0f, 2, false) == mui_success &&
              Selects(&scene, 3, 5),
          "a double click at a word's start, that word");
    CHECK(muiTextEditPress(host, scene.node, 15.0f, 15.0f, 3, false) == mui_success &&
              Selects(&scene, 6, 11),
          "a triple click, the second paragraph");
    CHECK(muiTextEditPress(host, scene.node, 11.0f, 15.0f, 4, false) == mui_success &&
              Selects(&scene, 7, 7),
          "a fourth, one again");
    CHECK(muiTextEditPress(host, scene.node, 1.0f, 5.0f, 1, true) == mui_success &&
              Selects(&scene, 7, 0),
          "Shift and a press extend from the anchor");
    CHECK(muiTextEditPress(host, scene.node, NAN, 5.0f, 1, false) == mui_errorInvalid &&
              muiTextEditPress(host, scene.node, 1.0f, 5.0f, 0, false) == mui_errorInvalid,
          "no point, no click");
    Select(&scene, 1, 4);
    CHECK(muiTextEditMove(host, scene.node, mui_moveLeft, false) == mui_success &&
              Selects(&scene, 1, 1),
          "left collapses to the left edge");
    Select(&scene, 4, 1);
    CHECK(muiTextEditMove(host, scene.node, mui_moveRight, false) == mui_success &&
              Selects(&scene, 4, 4),
          "right to the right edge");
    CHECK(muiTextEditMove(host, scene.node, mui_moveRight, true) == mui_success &&
              Selects(&scene, 4, 5),
          "Shift extends");
    CHECK(muiTextEditMove(host, scene.node, mui_moveLineDown, false) == mui_success &&
              Selects(&scene, 11, 11),
          "down a line, at the same x");
    CHECK(muiTextEditMove(host, scene.node, 13, false) == mui_errorInvalid, "out of range");
    FreeScene(&scene);
}

// The clipboard the tests write to.
typedef struct Clipboard
{
    char text[64];
    size_t length;
    int writes;
} Clipboard;

static void WriteClipboard(void* user, const char* text, size_t length)
{
    Clipboard* clipboard = user;
    clipboard->length = length < sizeof clipboard->text ? length : sizeof clipboard->text;
    memcpy(clipboard->text, text, clipboard->length);
    clipboard->writes++;
}

// A key's event under a keymap: a letter's meaning, or a named key.
static muiTextEditOutcome Key(Scene* scene, muiKeymap keymap, Clipboard* clipboard, muiKeyCode code,
                              muiKey key, muiModifiers modifiers)
{
    muiEvent event = {0};
    event.kind = mui_eventKeyDown;
    event.code = code;
    event.key = key != 0 ? key : MUI_KEY_NAMED | code;
    event.modifiers = modifiers;
    const muiTextEditInput input = {keymap, WriteClipboard, clipboard};
    muiTextEditOutcome outcome = {true, true, true};
    CHECK(muiTextEditEvent(&scene->host, scene->node, &event, &input, &outcome) == mui_success,
          "an event");
    return outcome;
}

enum
{
    CODE_A = 4,
    CODE_C = 6,
    CODE_V = 25,
    CODE_X = 27,
    CODE_Y = 28,
    CODE_Z = 29,
    CODE_F1 = 58
};

static void TestPcKeys(void)
{
    Scene scene = MakeScene("ab cd", 0);
    Layout(&scene);
    Clipboard clipboard = {0};
    const muiModifiers ctrl = mui_modControl;
    const muiModifiers shift = mui_modShift;
    muiTextEditOutcome outcome = Key(&scene, mui_keymapPc, &clipboard, CODE_A, 'a', ctrl);
    CHECK(outcome.handled && !outcome.changed && !outcome.paste && Selects(&scene, 0, 5),
          "Control and A select all");
    Key(&scene, mui_keymapPc, &clipboard, CODE_C, 'c', ctrl);
    CHECK(clipboard.length == 5 && memcmp(clipboard.text, "ab cd", 5) == 0, "copied");
    outcome = Key(&scene, mui_keymapPc, &clipboard, CODE_X, 'x', ctrl);
    CHECK(outcome.changed && Holds(&scene, "") && clipboard.writes == 2, "cut");
    outcome = Key(&scene, mui_keymapPc, &clipboard, CODE_Z, 'z', ctrl);
    CHECK(outcome.handled && outcome.changed && Holds(&scene, "ab cd"), "undone");
    CHECK(Key(&scene, mui_keymapPc, &clipboard, CODE_Y, 'y', ctrl).changed && Holds(&scene, ""),
          "redone with Y");
    CHECK(Key(&scene, mui_keymapPc, &clipboard, CODE_Z, 'Z', ctrl | shift).changed == false &&
              Key(&scene, mui_keymapPc, &clipboard, CODE_Z, 'z', ctrl).changed &&
              Key(&scene, mui_keymapPc, &clipboard, CODE_Z, 'Z', ctrl | shift).changed &&
              Holds(&scene, ""),
          "redone with Shift and Z");
    Key(&scene, mui_keymapPc, &clipboard, CODE_Z, 'z', ctrl);
    outcome = Key(&scene, mui_keymapPc, &clipboard, CODE_V, 'v', ctrl);
    CHECK(outcome.handled && outcome.paste && !outcome.changed, "a paste asked for");
    CHECK(Key(&scene, mui_keymapPc, &clipboard, mui_codeInsert, 0, shift).paste &&
              Key(&scene, mui_keymapPc, &clipboard, mui_codeInsert, 0, ctrl).handled,
          "Shift and Insert, Control and Insert");
    Key(&scene, mui_keymapPc, &clipboard, mui_codeHome, 0, 0);
    CHECK(Selects(&scene, 0, 0), "Home");
    Key(&scene, mui_keymapPc, &clipboard, mui_codeArrowRight, 0, ctrl);
    CHECK(Selects(&scene, 3, 3), "Control and right to the next word");
    Key(&scene, mui_keymapPc, &clipboard, mui_codeEnd, 0, shift);
    CHECK(Selects(&scene, 3, 5), "Shift and End extend");
    Key(&scene, mui_keymapPc, &clipboard, mui_codeArrowUp, 0, 0);
    CHECK(Selects(&scene, 0, 0), "up on a single line, its start");
    Key(&scene, mui_keymapPc, &clipboard, mui_codeEnd, 0, 0);
    outcome = Key(&scene, mui_keymapPc, &clipboard, mui_codeBackspace, 0, ctrl);
    CHECK(outcome.changed && Holds(&scene, "ab "), "Control and Backspace, a word");
    CHECK(!Key(&scene, mui_keymapPc, &clipboard, mui_codeEnter, 0, 0).handled,
          "Enter left to the host on a single line");
    CHECK(!Key(&scene, mui_keymapPc, &clipboard, CODE_A, 'a', ctrl | mui_modAlt).handled &&
              !Key(&scene, mui_keymapPc, &clipboard, CODE_F1, 0, 0).handled &&
              !Key(&scene, mui_keymapPc, &clipboard, mui_codeArrowLeft, 0, mui_modAlt).handled,
          "AltGr, F1 and Alt and left not taken");
    FreeScene(&scene);
}

static void TestMacKeys(void)
{
    Scene scene = MakeScene("ab cd\nef", mui_editMultiline);
    Layout(&scene);
    Clipboard clipboard = {0};
    const muiModifiers command = mui_modMeta;
    const muiModifiers option = mui_modAlt;
    Select(&scene, 4, 4);
    Key(&scene, mui_keymapMac, &clipboard, mui_codeArrowLeft, 0, option);
    CHECK(Selects(&scene, 3, 3), "Option and left, a word back");
    Key(&scene, mui_keymapMac, &clipboard, mui_codeArrowRight, 0, command);
    CHECK(Selects(&scene, 5, 5), "Command and right, the line's end");
    Key(&scene, mui_keymapMac, &clipboard, mui_codeArrowDown, 0, command);
    CHECK(Selects(&scene, 8, 8), "Command and down, the text's end");
    CHECK(Key(&scene, mui_keymapMac, &clipboard, mui_codeEnter, 0, 0).changed &&
              Holds(&scene, "ab cd\nef\n"),
          "Enter breaks a line");
    Select(&scene, 5, 5);
    CHECK(Key(&scene, mui_keymapMac, &clipboard, mui_codeBackspace, 0, command).changed &&
              Holds(&scene, "\nef\n"),
          "Command and Backspace, to the line's start");
    Key(&scene, mui_keymapMac, &clipboard, CODE_Z, 'z', command);
    Select(&scene, 0, 0);
    CHECK(Key(&scene, mui_keymapMac, &clipboard, mui_codeDelete, 0, option).changed &&
              Holds(&scene, " cd\nef\n"),
          "Option and Delete, to the word's end");
    CHECK(Key(&scene, mui_keymapMac, &clipboard, CODE_A, 'a', command).handled &&
              Selects(&scene, 0, 7),
          "Command and A");
    CHECK(!Key(&scene, mui_keymapMac, &clipboard, CODE_A, 'a', mui_modControl).handled &&
              !Key(&scene, mui_keymapMac, &clipboard, mui_codeArrowLeft, 0, mui_modControl).handled,
          "Control left to the host");
    FreeScene(&scene);
}

// A password copies and cuts nothing; typed text types, control
// characters are the keys'; the pointer through events.
static void TestEvents(void)
{
    Scene scene = MakeScene("pass", mui_editPassword);
    Layout(&scene);
    Clipboard clipboard = {0};
    Key(&scene, mui_keymapPc, &clipboard, CODE_A, 'a', mui_modControl);
    Key(&scene, mui_keymapPc, &clipboard, CODE_C, 'c', mui_modControl);
    Key(&scene, mui_keymapPc, &clipboard, CODE_X, 'x', mui_modControl);
    CHECK(clipboard.writes == 0 && Holds(&scene, "pass"), "a password stays");
    const muiTextEditInput input = {mui_keymapPc, WriteClipboard, &clipboard};
    muiEvent event = {0};
    event.kind = mui_eventText;
    event.text = "word";
    event.length = 4;
    muiTextEditOutcome outcome;
    CHECK(muiTextEditEvent(&scene.host, scene.node, &event, &input, &outcome) == mui_success &&
              outcome.handled && outcome.changed && Holds(&scene, "word"),
          "typed text");
    event.text = "\t";
    event.length = 1;
    CHECK(muiTextEditEvent(&scene.host, scene.node, &event, &input, &outcome) == mui_success &&
              !outcome.handled && Holds(&scene, "word"),
          "a tab left to focus");
    muiPointerRecord record = {0};
    record.kind = mui_pointerRecordPress;
    record.clickCount = 2;
    record.x = 15.0f;
    record.y = 5.0f;
    event = (muiEvent){0};
    event.kind = mui_eventPointer;
    event.pointer = &record;
    CHECK(muiTextEditEvent(&scene.host, scene.node, &event, &input, &outcome) == mui_success &&
              outcome.handled && Selects(&scene, 0, 4),
          "a double press");
    record.kind = mui_pointerRecordRelease;
    CHECK(muiTextEditEvent(&scene.host, scene.node, &event, &input, &outcome) == mui_success &&
              !outcome.handled,
          "a release not taken");
    const muiTextEditInput bad = {2, NULL, NULL};
    CHECK(muiTextEditEvent(&scene.host, scene.node, &event, &bad, &outcome) == mui_errorInvalid,
          "a keymap out of range");
    FreeScene(&scene);
}

// A composition shown at the caret, over a selection, taken out, then
// committed as typing; undo waits for it.
static void TestComposition(void)
{
    Scene scene = MakeScene("ab", 0);
    bool changed = false;
    CHECK(muiTextBlock_Compose(scene.service, scene.block, "\xE3\x81\x8B", 3, 3, NULL, 0,
                               &changed) == mui_success &&
              changed && Holds(&scene, "ab\xE3\x81\x8B") && Selects(&scene, 5, 5),
          "shown at the caret");
    CHECK(!Undo(&scene), "undo waits");
    CHECK(muiTextBlock_Compose(scene.service, scene.block, "\xE3\x81\x8B\xE3\x81\xAA", 6, 3, NULL,
                               0, NULL) == mui_success &&
              Holds(&scene, "ab\xE3\x81\x8B\xE3\x81\xAA") && Selects(&scene, 5, 5),
          "replaced, the caret inside");
    CHECK(muiTextBlock_Compose(scene.service, scene.block, "x", 1, 2, NULL, 0, NULL) ==
                  mui_errorInvalid &&
              muiTextBlock_Compose(scene.service, scene.block, "\xE3\x81\x8B", 3, 1, NULL, 0,
                                   NULL) == mui_errorInvalid,
          "a caret out of place");
    Type(&scene, "\xE4\xBB\xAE");
    CHECK(Holds(&scene, "ab\xE4\xBB\xAE") && Selects(&scene, 5, 5), "committed as typing");
    CHECK(Undo(&scene) && Holds(&scene, "ab"), "and undone");
    Select(&scene, 0, 2);
    CHECK(muiTextBlock_Compose(scene.service, scene.block, "x", 1, 1, NULL, 0, NULL) ==
                  mui_success &&
              Holds(&scene, "x"),
          "over a selection");
    CHECK(muiTextBlock_Compose(scene.service, scene.block, NULL, 0, 0, NULL, 0, &changed) ==
                  mui_success &&
              changed && Holds(&scene, "") && Selects(&scene, 0, 0),
          "taken out");
    CHECK(Undo(&scene) && Holds(&scene, "ab") && Selects(&scene, 0, 2), "the selection back");
    FreeScene(&scene);
}

int main(void)
{
    TestCalls();
    TestTyping();
    TestDeleting();
    TestLimit();
    TestRules();
    TestFilters();
    TestLength();
    TestElsewhere();
    TestPointer();
    TestPcKeys();
    TestMacKeys();
    TestEvents();
    TestComposition();
    return s_failures == 0 ? 0 : 1;
}
