// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ARIA adapter in headless Chrome, run by test/web_runner.cjs: the
// test makes a host element over the page's canvas, builds trees in it,
// and asks the runner to compare the browser's accessibility tree, and
// elements' boxes, with what it expects:
// - building deferred behind the enabling button, and pressing it;
// - roles, names (as labels or as text), states and values;
// - updates: names, states and values changed in place;
// - structure: a node hidden, added, moved under a node newly shown,
//   siblings reordered;
// - boxes nested relative to their parents, a scale, a moved parent;
// - a range made a plain slider; the adapter destroyed;
// - relations as ids; clicks, expanding and collapsing, a range set;
//   the focus both ways, and on from the enabling button; live names
//   announced through ariaNotify and through live regions; the host's
//   scroll put back;
// - text fields: inputs and a textarea holding their values, the
//   program's selection written, typing, a composition and a client's
//   selection asked of the host in bytes, other edits and caret keys
//   kept from the browser, roles beyond a text box's kept, a number
//   field asking for digits, a field no longer edited made a div.
// Under Node, with no page, it is skipped.

#include "aria.h"
#include "test_harness.h"

#include "maul-ui/access_aria.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
EM_JS(int, HasPage, (void), {
    return typeof document !== "undefined" ? 1 : 0;
});

// The host, as Maul Window's: smaller than the tree, so it can scroll.
EM_JS(void, MakeHost, (void), {
    const host = document.createElement("div");
    host.id = "host";
    host.style.cssText = "position:absolute;left:0;top:0;width:300px;height:200px;" +
                         "overflow:hidden";
    document.body.appendChild(host);
});

EM_JS(void, FocusOn, (const char* selector), {
    document.querySelector(UTF8ToString(selector)).focus();
});

EM_JS(void, Blur, (void), {
    if (document.activeElement) {
        document.activeElement.blur();
    }
});

// A press without the focus, as a screen reader in browse mode makes it;
// the DOM focus left where it was.
EM_JS(void, PressUnfocused, (const char* selector), {
    if (document.activeElement) {
        document.activeElement.blur();
    }
    document.querySelector(UTF8ToString(selector)).click();
});

// Whether a key pressed on an element was kept from the browser.
EM_JS(int, KeyKept, (const char* selector, const char* key), {
    const event = new KeyboardEvent("keydown", {key: UTF8ToString(key), bubbles: true,
                                                cancelable: true});
    document.querySelector(UTF8ToString(selector)).dispatchEvent(event);
    return event.defaultPrevented ? 1 : 0;
});

EM_JS(int, IsActive, (const char* selector), {
    return document.activeElement === document.querySelector(UTF8ToString(selector)) ? 1 : 0;
});

// What a range input does as a screen reader adjusts it.
EM_JS(void, SetRange, (const char* selector, double value), {
    const range = document.querySelector(UTF8ToString(selector));
    range.value = String(value);
    range.dispatchEvent(new Event("input", {bubbles: true}));
});

EM_JS(int, AttributeIs, (const char* selector, const char* name, const char* expected), {
    const value = document.querySelector(UTF8ToString(selector)).getAttribute(UTF8ToString(name));
    return value === (expected ? UTF8ToString(expected) : null) ? 1 : 0;
});

EM_JS(int, TextIs, (const char* selector, const char* expected), {
    return document.querySelector(UTF8ToString(selector)).textContent === UTF8ToString(expected)
               ? 1 : 0;
});

EM_JS(void, ScrollHost, (int top), {
    document.querySelector("#host").scrollTop = top;
});

EM_JS(int, HostTop, (void), {
    return document.querySelector("#host").scrollTop;
});

// ariaNotify recorded, whether or not the browser has it; or taken away.
EM_JS(void, HookNotify, (int present), {
    if (!present) {
        delete Element.prototype.ariaNotify;
        return;
    }
    globalThis.muiNotified = [];
    Element.prototype.ariaNotify = function(text, options) {
        globalThis.muiNotified.push(text + "/" + options.priority);
    };
});

EM_JS(int, NotifiedIs, (const char* expected), {
    return globalThis.muiNotified.join(" ") === UTF8ToString(expected) ? 1 : 0;
});

EM_JS(int, FieldIs, (const char* selector, const char* tag, const char* type,
                    const char* value), {
    const field = document.querySelector(UTF8ToString(selector));
    return field.tagName === UTF8ToString(tag) && (!type || field.type === UTF8ToString(type)) &&
           (!value || field.value === UTF8ToString(value)) ? 1 : 0;
});

EM_JS(int, SelectionIs, (const char* selector, int start, int end, int backward), {
    const field = document.querySelector(UTF8ToString(selector));
    return field.selectionStart === start && field.selectionEnd === end &&
           (start === end || (field.selectionDirection === "backward") === (backward !== 0))
               ? 1 : 0;
});

EM_JS(void, SelectIn, (const char* selector, int start, int end), {
    document.querySelector(UTF8ToString(selector)).setSelectionRange(start, end);
});

// What a browser fires as a client edits a field: whether it was kept
// from the browser.
EM_JS(int, InputKept, (const char* selector, const char* type, const char* data), {
    const event = new InputEvent("beforeinput", {inputType: UTF8ToString(type),
                                                 data: data ? UTF8ToString(data) : null,
                                                 bubbles: true, cancelable: true});
    document.querySelector(UTF8ToString(selector)).dispatchEvent(event);
    return event.defaultPrevented ? 1 : 0;
});

EM_JS(void, Compose, (const char* selector, const char* type, const char* data), {
    document.querySelector(UTF8ToString(selector))
        .dispatchEvent(new CompositionEvent(UTF8ToString(type), {data: UTF8ToString(data),
                                                                  bubbles: true}));
});

EM_JS(void, Input, (const char* selector), {
    document.querySelector(UTF8ToString(selector)).dispatchEvent(new Event("input",
                                                                           {bubbles: true}));
});

EM_JS(int, AnswerCount, (void), {
    return (globalThis.muiTestAnswers || []).length;
});

EM_JS(int, AnswerAt, (int index), {
    return globalThis.muiTestAnswers[index] ? 1 : 0;
});
// clang-format on

// Gives the runner a command and waits for its answer: whether it held.
static bool Ask(const char* command)
{
    int before = AnswerCount();
    printf("mui-test: %s\n", command);
    fflush(stdout);
    for (int i = 0; i < 4000 && AnswerCount() == before; i++)
    {
        emscripten_sleep(5);
    }
    return AnswerCount() > before && AnswerAt(before) != 0;
}

static bool TreeIs(const char* expected)
{
    char command[2048];
    (void)snprintf(command, sizeof(command), "ax #host %s", expected);
    return Ask(command);
}

static bool BoxIs(const char* id, int x, int y, int width, int height)
{
    char command[128];
    (void)snprintf(command, sizeof(command), "rect #%s %d %d %d %d", id, x, y, width, height);
    return Ask(command);
}

static muiAccessRequest s_asked;
static char s_text[64];

// Fills what it gives with garbage, so that memory read before it is
// written shows.
static void* Poisoned(size_t size, size_t alignment, void* context)
{
    (void)context;
    // Some C libraries take no alignment below a pointer's.
    size_t align = alignment < sizeof(void*) ? sizeof(void*) : alignment;
    size_t rounded = (size + align - 1) / align * align;
    void* memory = aligned_alloc(align, rounded);
    if (memory != NULL)
    {
        memset(memory, 0xA5, rounded);
    }
    return memory;
}

static void Unpoisoned(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static bool Act(void* user, const muiAccessRequest* request)
{
    (void)user;
    s_asked = *request;
    // The text is the adapter's only while the request is applied.
    if (request->text != NULL && request->length < sizeof(s_text))
    {
        memcpy(s_text, request->text, request->length);
        s_text[request->length] = '\0';
        s_asked.text = s_text;
    }
    return true;
}

typedef struct Built
{
    muiAccessNode nodes[12];
    const muiAccessNode* sent[12];
    uint64_t children[12];
    uint32_t nodeCount;
    uint32_t childCount;
} Built;

static muiAccessNode* Add(Built* built, uint64_t id, muiRole role, const char* label, float x,
                          float y, float width, float height)
{
    muiAccessNode* node = &built->nodes[built->nodeCount];
    *node = (muiAccessNode){.id = id,
                            .role = role,
                            .bounds = {0.0f, 0.0f, width, height},
                            .transform = {1.0f, 0.0f, 0.0f, 1.0f, x, y}};
    if (label != NULL)
    {
        muiAccessTextKind kind = role == mui_roleLabel ? mui_accessValue : mui_accessLabel;
        node->text[kind] = label;
        node->textLength[kind] = (uint32_t)strlen(label);
    }
    built->sent[built->nodeCount++] = node;
    return node;
}

static void List(Built* built, muiAccessNode* parent, const uint64_t* ids, uint32_t count)
{
    parent->firstChild = built->childCount;
    parent->childCount = count;
    memcpy(&built->children[built->childCount], ids, count * sizeof(uint64_t));
    built->childCount += count;
}

// The window 1 at 10, 10: a focusable button 2; a generic 3 at 0, 50
// around a label 4 at 5, 0; a checked checkbox 6; a heading 7 at level
// 2; a slider 8 the host sets, at 30 of 0 to 100; a progress bar 9 at a
// half.
static muiAccessUpdate Build(Built* built)
{
    *built = (Built){0};
    muiAccessNode* root = Add(built, 1, mui_roleWindow, "Main", 10, 10, 380, 280);
    muiAccessNode* ok = Add(built, 2, mui_roleButton, "OK", 10, 10, 100, 40);
    ok->flags = mui_accessFocusable;
    ok->actions = 1u << mui_actionClick;
    muiAccessNode* generic = Add(built, 3, mui_roleGeneric, NULL, 0, 50, 200, 20);
    (void)Add(built, 4, mui_roleLabel, "Hello", 5, 0, 100, 20);
    Add(built, 6, mui_roleCheckBox, "Agree", 10, 80, 100, 20)->flags =
        mui_accessCheckable | mui_accessChecked;
    Add(built, 7, mui_roleHeading, "Title", 10, 110, 100, 20)->values.level = 2;
    muiAccessNode* slider = Add(built, 8, mui_roleSlider, "Volume", 10, 140, 100, 20);
    slider->flags = mui_accessNumeric;
    slider->actions = 1u << mui_actionSetValue;
    slider->value = 30.0f;
    slider->maximum = 100.0f;
    slider->step = 1.0f;
    muiAccessNode* progress = Add(built, 9, mui_roleProgressIndicator, "Load", 10, 170, 100, 10);
    progress->flags = mui_accessNumeric;
    progress->value = 0.5f;
    progress->maximum = 1.0f;
    List(built, root, (const uint64_t[]){2, 3, 6, 7, 8, 9}, 6);
    List(built, generic, (const uint64_t[]){4}, 1);
    return (muiAccessUpdate){built->sent, built->nodeCount, built->children, 1, 2};
}

static bool Send(muiAriaAdapter* adapter, const muiAccessNode* const* nodes, uint32_t count,
                 const uint64_t* children)
{
    const muiAccessUpdate update = {nodes, count, children, 0, 0};
    return muiAriaAdapter_Apply(adapter, &update) == mui_success;
}

static void Sleep(void)
{
    emscripten_sleep(20);
}

// Relations, clicks, a range set, the focus both ways, announcements and
// the host's scroll.
static void TestActions(muiAriaAdapter* adapter, Built* built, muiAccessNode* root,
                        const uint64_t* rootChildren, const muiAccessNode* slider)
{
    // The check box labelled by the label, described by the heading,
    // controlling the slider and the hidden progress bar.
    (void)built;
    const muiAccessTree* tree = muiAriaAdapter_GetTree(adapter);
    // Copies of held records name texts the tree frees when it replaces
    // them: each sent copy names texts of its own.
    muiAccessNode check = *muiAccessTree_Find(tree, 6);
    check.text[mui_accessLabel] = "Agreed";
    static const muiAccessLink s_links[4] = {{4, mui_relationLabelledBy},
                                             {7, mui_relationDescribedBy},
                                             {8, mui_relationControls},
                                             {9, mui_relationControls}};
    check.links = s_links;
    check.linkCount = 4;
    CHECK(Send(adapter, (const muiAccessNode*[]){&check}, 1, rootChildren) &&
              AttributeIs("#mui0-6", "aria-labelledby", "mui0-4") &&
              AttributeIs("#mui0-6", "aria-describedby", "mui0-7") &&
              AttributeIs("#mui0-6", "aria-controls", "mui0-8 mui0-9"),
          "relations as ids, a target with no element named too");
    check.linkCount = 0;
    CHECK(Send(adapter, (const muiAccessNode*[]){&check}, 1, rootChildren) &&
              AttributeIs("#mui0-6", "aria-labelledby", NULL),
          "relations gone");
    // Clicks: the button's own; what expands, toggled.
    s_asked = (muiAccessRequest){0};
    CHECK(Ask("press #mui0-2") && s_asked.action == mui_actionClick && s_asked.target == 2,
          "a click");
    muiAccessNode added = *muiAccessTree_Find(tree, 10);
    added.text[mui_accessLabel] = "New";
    added.flags = mui_accessExpandable;
    added.actions = 1u << mui_actionExpand | 1u << mui_actionCollapse;
    CHECK(Send(adapter, (const muiAccessNode*[]){&added}, 1, rootChildren) &&
              Ask("press #mui0-a") && s_asked.action == mui_actionExpand && s_asked.target == 10,
          "a click expanding");
    added.flags |= mui_accessExpanded;
    CHECK(Send(adapter, (const muiAccessNode*[]){&added}, 1, rootChildren) &&
              AttributeIs("#mui0-a", "aria-expanded", "true") && Ask("press #mui0-a") &&
              s_asked.action == mui_actionCollapse,
          "and collapsing");
    s_asked = (muiAccessRequest){0};
    CHECK(Ask("press #mui0-7") && s_asked.target == 0, "no action for a heading");
    SetRange("#mui0-8", 55.0);
    CHECK(s_asked.action == mui_actionSetValue && s_asked.target == 8 && s_asked.value == 55.0f &&
              slider->value == 40.0f,
          "a range set, asked of the host");
    // The focus: outside the elements, the program's does not take the
    // DOM's; a client's focus asked of the host; then the program's moves
    // it, asking nothing.
    Blur();
    CHECK(IsActive("body"), "the DOM focus outside");
    FocusOn("#mui0-2");
    CHECK(s_asked.action == mui_actionFocus && s_asked.target == 2 && IsActive("#mui0-2"),
          "a client's focus asked of the host");
    CHECK(KeyKept("#mui0-2", "Tab") && !KeyKept("#mui0-2", "a"),
          "Tab kept from the browser, which would move the focus out; other keys not");
    s_asked = (muiAccessRequest){.action = mui_actionScrollRight};
    const muiAccessUpdate focus = {(const muiAccessNode*[]){root}, 1, rootChildren, 0, 8};
    CHECK(muiAriaAdapter_Apply(adapter, &focus) == mui_success && IsActive("#mui0-8") &&
              s_asked.action == mui_actionScrollRight,
          "the program's focus moved, asking nothing");
    // A live heading renamed: announced through ariaNotify, then through
    // the polite region.
    muiAccessNode heading = *muiAccessTree_Find(tree, 7);
    heading.values.live = mui_livePolite;
    heading.text[mui_accessLabel] = "Topic 2";
    heading.textLength[mui_accessLabel] = 7;
    HookNotify(1);
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, rootChildren) &&
              NotifiedIs("Topic 2/normal"),
          "announced through ariaNotify");
    heading.values.level = 4;
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, rootChildren) &&
              NotifiedIs("Topic 2/normal"),
          "nothing announced without a new name");
    heading.values.live = mui_liveAssertive;
    heading.text[mui_accessLabel] = "Topic 2b";
    heading.textLength[mui_accessLabel] = 8;
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, rootChildren) &&
              NotifiedIs("Topic 2/normal Topic 2b/high"),
          "an assertive name said with a high priority");
    HookNotify(0);
    heading.values.live = mui_liveAssertive;
    heading.text[mui_accessLabel] = "Topic 3";
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, rootChildren), "renamed");
    Sleep();
    CHECK(TextIs("#mui0-assertive", "Topic 3") && TextIs("#mui0-polite", ""),
          "announced through the assertive region");
    heading.values.level = 4;
    heading.text[mui_accessLabel] = "Topic 3";
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, rootChildren), "not renamed");
    Sleep();
    CHECK(TextIs("#mui0-assertive", "Topic 3"), "the region keeps the name a while");
    // A scroll event comes with the page's next rendering, whose time
    // depends on the compositor: waited for, up to a second.
    ScrollHost(40);
    for (int waited = 0; waited < 50 && HostTop() != 0; waited++)
    {
        Sleep();
    }
    CHECK(HostTop() == 0, "the host's scroll put back");
    emscripten_sleep(350);
    CHECK(TextIs("#mui0-assertive", ""), "the announcement taken away after a while");
    heading.values = (muiAccessValues){.level = 3};
    heading.text[mui_accessLabel] = "Topic";
    heading.textLength[mui_accessLabel] = 5;
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, rootChildren), "the heading back");
}

// A second adapter whose enabling button had the focus: the focus goes on
// to the program's focused node.
static void TestEnablingFocus(void)
{
    muiAriaAdapterDef def = muiDefaultAriaAdapterDef();
    def.host = "#host";
    def.action = Act;
    def.allocator = (muiAllocator){Poisoned, Unpoisoned, NULL};
    muiAriaAdapter* adapter = NULL;
    muiAccessNode root = {.id = 1, .role = mui_roleWindow, .childCount = 1};
    muiAccessNode button = {.id = 5, .role = mui_roleButton, .flags = mui_accessFocusable};
    const uint64_t children[1] = {5};
    const muiAccessUpdate update = {(const muiAccessNode*[]){&root, &button}, 2, children, 1, 5};
    CHECK(muiCreateAriaAdapter(&def, &adapter) == mui_success &&
              muiAriaAdapter_Apply(adapter, &update) == mui_success,
          "a second adapter");
    // An event for an element no node holds, or past the elements, asks
    // nothing: the page and the adapter disagree only by a bug.
    s_asked = (muiAccessRequest){.action = mui_actionScrollRight};
    muiAriaPerform(adapter, mui_ariaClicked, 3, 0.0);
    muiAriaPerform(adapter, mui_ariaClicked, UINT32_MAX, 0.0);
    CHECK(s_asked.action == mui_actionScrollRight, "no action for no node");
    FocusOn("#host button");
    CHECK(Ask("press #host button") && muiAriaAdapter_IsEnabled(adapter) && IsActive("#mui0-5"),
          "the focus on from the enabling button");
    muiDestroyAriaAdapter(adapter);
    // Pressed without the focus, the button gives it to the program's
    // focused node all the same; enabled by the program, nothing moves.
    CHECK(muiCreateAriaAdapter(&def, &adapter) == mui_success &&
              muiAriaAdapter_Apply(adapter, &update) == mui_success,
          "a third adapter");
    PressUnfocused("#host button");
    CHECK(muiAriaAdapter_IsEnabled(adapter) && IsActive("#mui0-5"),
          "the focus on from a press that did not focus the button");
    muiDestroyAriaAdapter(adapter);
    CHECK(muiCreateAriaAdapter(&def, &adapter) == mui_success &&
              muiAriaAdapter_Apply(adapter, &update) == mui_success,
          "a fourth adapter");
    PressUnfocused("body");
    muiAriaAdapter_Enable(adapter);
    CHECK(muiAriaAdapter_IsEnabled(adapter) && IsActive("body"),
          "enabled by the program, the DOM focus left where it was");
    muiDestroyAriaAdapter(adapter);
}

// Whether the host was asked for a text request: an action on node 2 over
// bytes anchor to focus, with a text or none.
static bool AskedText(muiAccessAction action, uint32_t anchor, uint32_t focus, const char* text)
{
    return s_asked.action == action && s_asked.target == 2 && s_asked.anchor == anchor &&
           s_asked.focus == focus &&
           (text == NULL ? s_asked.text == NULL
                         : s_asked.text != NULL && strcmp(s_asked.text, text) == 0 &&
                               s_asked.length == strlen(text));
}

// A window 1 with a text input 2 holding "a", an emoji and "b" (a code
// point of 4 bytes, 2 UTF-16 units), selected backward over the emoji
// and focused; a multiline text input 3 and a password input 4.
static void TestFields(void)
{
    muiAriaAdapterDef def = muiDefaultAriaAdapterDef();
    def.host = "#host";
    def.action = Act;
    def.allocator = (muiAllocator){Poisoned, Unpoisoned, NULL};
    muiAriaAdapter* adapter = NULL;
    const uint32_t edits =
        1u << mui_actionFocus | 1u << mui_actionSetSelection | 1u << mui_actionReplaceText;
    static const char s_emoji[] = "a\U0001F600b";
    muiAccessNode root = {.id = 1, .role = mui_roleWindow, .childCount = 3};
    muiAccessNode name = {.id = 2,
                          .role = mui_roleTextInput,
                          .flags = mui_accessFocusable,
                          .actions = edits,
                          .bounds = {0, 0, 100, 20},
                          .text = {[mui_accessLabel] = "Name", [mui_accessValue] = s_emoji},
                          .textLength = {[mui_accessLabel] = 4, [mui_accessValue] = 6},
                          .marks = {.anchor = 5, .focus = 1, .selected = true}};
    muiAccessNode notes = {.id = 3,
                           .role = mui_roleMultilineTextInput,
                           .actions = edits,
                           .text = {[mui_accessLabel] = "Notes", [mui_accessValue] = "one\ntwo"},
                           .textLength = {[mui_accessLabel] = 5, [mui_accessValue] = 7}};
    muiAccessNode secret = {.id = 4, .role = mui_rolePasswordInput, .actions = edits};
    const uint64_t children[3] = {2, 3, 4};
    const muiAccessUpdate update = {(const muiAccessNode*[]){&root, &name, &notes, &secret}, 4,
                                    children, 1, 2};
    CHECK(muiCreateAriaAdapter(&def, &adapter) == mui_success &&
              muiAriaAdapter_Apply(adapter, &update) == mui_success,
          "an adapter of fields");
    FocusOn("#host button");
    CHECK(Ask("press #host button") && IsActive("#mui0-2"), "enabled, the field focused");
    CHECK(FieldIs("#mui0-2", "INPUT", "text", s_emoji) &&
              FieldIs("#mui0-3", "TEXTAREA", NULL, "one\ntwo") &&
              FieldIs("#mui0-4", "INPUT", "password", "") && AttributeIs("#mui0-2", "role", NULL),
          "inputs and a textarea holding their values, with no role of their own");
    CHECK(SelectionIs("#mui0-2", 1, 3, 1), "the program's selection written in UTF-16 units");
    s_asked = (muiAccessRequest){0};
    CHECK(InputKept("#mui0-2", "insertText", "x") && AskedText(mui_actionReplaceText, 1, 5, "x") &&
              FieldIs("#mui0-2", "INPUT", NULL, s_emoji),
          "typing kept from the field, asked of the host over the selection in bytes");
    s_asked = (muiAccessRequest){.action = mui_actionScrollRight};
    CHECK(InputKept("#mui0-2", "deleteContentBackward", NULL) &&
              InputKept("#mui0-2", "insertFromPaste", NULL) &&
              s_asked.action == mui_actionScrollRight,
          "other edits kept from the field, asking nothing: the program's keys make them");
    CHECK(KeyKept("#mui0-2", "ArrowLeft") && KeyKept("#mui0-2", "End") &&
              !KeyKept("#mui0-2", "a") && !KeyKept("#mui0-2", "Backspace"),
          "caret keys kept from the browser; the program moves the caret");
    Input("#mui0-2");
    CHECK(s_asked.action == mui_actionScrollRight, "a field's input no range set");
    // A client's selection, sent as the selection changes.
    SelectIn("#mui0-2", 0, 4);
    for (int waited = 0; waited < 50 && s_asked.action == mui_actionScrollRight; waited++)
    {
        Sleep();
    }
    CHECK(AskedText(mui_actionSetSelection, 0, 6, NULL), "a client's selection asked in bytes");
    // The program's own, written, asking nothing.
    name.marks = (muiAccessTextMarks){.anchor = 6, .focus = 6, .selected = true};
    s_asked = (muiAccessRequest){.action = mui_actionScrollRight};
    CHECK(Send(adapter, (const muiAccessNode*[]){&name}, 1, children) &&
              SelectionIs("#mui0-2", 4, 4, 0),
          "the program's caret written");
    Sleep();
    CHECK(s_asked.action == mui_actionScrollRight, "asking nothing back");
    // A composition shown as it runs, asked for whole at its end.
    name.text[mui_accessValue] = "ab";
    name.textLength[mui_accessValue] = 2;
    name.marks = (muiAccessTextMarks){.anchor = 1, .focus = 2, .selected = true};
    CHECK(Send(adapter, (const muiAccessNode*[]){&name}, 1, children) &&
              FieldIs("#mui0-2", "INPUT", NULL, "ab") && SelectionIs("#mui0-2", 1, 2, 0),
          "a new value and selection");
    Compose("#mui0-2", "compositionstart", "");
    CHECK(!InputKept("#mui0-2", "insertCompositionText", "u"), "a composition shown as it runs");
    SelectIn("#mui0-2", 2, 2);
    Sleep();
    CHECK(s_asked.action == mui_actionScrollRight, "its selection not the program's");
    Compose("#mui0-2", "compositionend", "\xC3\xBC");
    CHECK(AskedText(mui_actionReplaceText, 1, 2, "\xC3\xBC"),
          "asked for whole over the selection it started on");
    // Roles beyond a text box's kept, each input of its type, a number
    // field asking for digits.
    CHECK(AttributeIs("#mui0-4", "role", NULL) && AttributeIs("#mui0-4", "inputmode", NULL),
          "a password field with no role");
    secret.role = mui_roleNumberInput;
    CHECK(Send(adapter, (const muiAccessNode*[]){&secret}, 1, children) &&
              FieldIs("#mui0-4", "INPUT", "text", NULL) &&
              AttributeIs("#mui0-4", "role", "spinbutton") &&
              AttributeIs("#mui0-4", "inputmode", "decimal"),
          "a number field: a text input, a spin button, asking for digits");
    secret.role = mui_roleSearchInput;
    CHECK(Send(adapter, (const muiAccessNode*[]){&secret}, 1, children) &&
              FieldIs("#mui0-4", "INPUT", "search", NULL) &&
              AttributeIs("#mui0-4", "role", "searchbox") &&
              AttributeIs("#mui0-4", "inputmode", NULL),
          "a search field");
    secret.role = mui_roleEditableComboBox;
    CHECK(Send(adapter, (const muiAccessNode*[]){&secret}, 1, children) &&
              FieldIs("#mui0-4", "INPUT", "text", NULL) &&
              AttributeIs("#mui0-4", "role", "combobox"),
          "an editable combo box");
    // No longer edited: a div again.
    name.actions = 1u << mui_actionFocus;
    CHECK(Send(adapter, (const muiAccessNode*[]){&name}, 1, children) &&
              FieldIs("#mui0-2", "DIV", NULL, NULL) && AttributeIs("#mui0-2", "role", "textbox"),
          "a field no longer edited made a div");
    muiDestroyAriaAdapter(adapter);
}

static void TestContract(void)
{
    muiAriaAdapterDef def = muiDefaultAriaAdapterDef();
    muiAriaAdapter* adapter = NULL;
    def.action = Act;
    CHECK(muiCreateAriaAdapter(&def, &adapter) == mui_errorInvalid && adapter == NULL, "no host");
    def.host = "#nowhere";
    CHECK(muiCreateAriaAdapter(&def, &adapter) == mui_errorPlatform && adapter == NULL,
          "no element for the host");
    def.host = "#host";
    def.action = NULL;
    CHECK(muiCreateAriaAdapter(&def, &adapter) == mui_errorInvalid, "no action");
    CHECK(muiAriaAdapter_Apply(NULL, NULL) == mui_errorInvalid &&
              muiAriaAdapter_GetTree(NULL) == NULL &&
              muiAriaAdapter_SetScale(NULL, 1.0f) == mui_errorInvalid &&
              !muiAriaAdapter_IsEnabled(NULL),
          "NULL arguments");
    muiAriaAdapter_Enable(NULL);
    muiDestroyAriaAdapter(NULL);
}

int main(void)
{
    if (!HasPage())
    {
        printf("SKIP: no page; run through test/web_runner.cjs\n");
        return 0;
    }
    MakeHost();
    TestContract();
    muiAriaAdapterDef def = muiDefaultAriaAdapterDef();
    def.host = "#host";
    def.action = Act;
    def.allocator = (muiAllocator){Poisoned, Unpoisoned, NULL};
    muiAriaAdapter* adapter = NULL;
    static Built s_built;
    muiAccessUpdate update = Build(&s_built);
    CHECK(muiCreateAriaAdapter(&def, &adapter) == mui_success &&
              muiAriaAdapter_Apply(adapter, &update) == mui_success &&
              !muiAriaAdapter_IsEnabled(adapter),
          "made, deferred");
    CHECK(TreeIs("button \"Enable accessibility\" focusable=true"), "only the enabling button");
    CHECK(Ask("press #host button") && muiAriaAdapter_IsEnabled(adapter) && IsActive("#mui0-2"),
          "pressed, the focus on to the program's");
    CHECK(
        TreeIs("group \"Main\" | "
               "  button \"OK\" focusable=true | "
               "  StaticText \"Hello\" | "
               "  checkbox \"Agree\" checked=true | "
               "  heading \"Title\" level=2 | "
               "  slider \"Volume\" valuemin=0 valuemax=100 valuetext=30 focusable=true value=30 | "
               "  progressbar \"Load\" valuemin=0 valuemax=1 value=0.5"),
        "the tree");
    CHECK(BoxIs("mui0-1", 10, 10, 380, 280) && BoxIs("mui0-2", 20, 20, 100, 40) &&
              BoxIs("mui0-4", 15, 60, 100, 20),
          "boxes, nested, a flattened node's offset kept");
    // In place: a check box unchecked and renamed, the slider's value,
    // the heading's level.
    Built* built = &s_built;
    muiAccessNode check = built->nodes[4];
    check.flags = mui_accessCheckable;
    check.text[mui_accessLabel] = "Agreed";
    check.textLength[mui_accessLabel] = 6;
    muiAccessNode heading = built->nodes[5];
    heading.values.level = 3;
    heading.text[mui_accessLabel] = "Topic";
    heading.textLength[mui_accessLabel] = 5;
    muiAccessNode slider = built->nodes[6];
    slider.value = 40.0f;
    CHECK(Send(adapter, (const muiAccessNode*[]){&check, &heading, &slider}, 3, built->children) &&
              TreeIs("group \"Main\" | "
                     "  button \"OK\" focusable=true | "
                     "  StaticText \"Hello\" | "
                     "  checkbox \"Agreed\" checked=false | "
                     "  heading \"Topic\" level=3 | "
                     "  slider \"Volume\" valuemin=0 valuemax=100 valuetext=40 focusable=true "
                     "value=40 | "
                     "  progressbar \"Load\" valuemin=0 valuemax=1 value=0.5"),
          "names, states and values changed in place");
    // The progress bar hidden; a button 10 added; the generic named, so
    // shown, the label under it; the check box and heading swapped.
    muiAccessNode progress = built->nodes[7];
    progress.flags |= mui_accessHidden;
    muiAccessNode added = {.id = 10,
                           .role = mui_roleButton,
                           .bounds = {0, 0, 50, 20},
                           .transform = {1, 0, 0, 1, 200, 10},
                           .text = {[mui_accessLabel] = "New"},
                           .textLength = {[mui_accessLabel] = 3}};
    muiAccessNode generic = built->nodes[2];
    generic.text[mui_accessLabel] = "Greeting";
    generic.textLength[mui_accessLabel] = 8;
    muiAccessNode root = built->nodes[0];
    const uint64_t rootChildren[8] = {2, 3, 7, 6, 8, 9, 10, 4};
    root.firstChild = 0;
    root.childCount = 7;
    generic.firstChild = 7;
    CHECK(Send(adapter, (const muiAccessNode*[]){&root, &progress, &added, &generic}, 4,
               rootChildren) &&
              TreeIs("group \"Main\" | "
                     "  button \"OK\" focusable=true | "
                     "  group \"Greeting\" | "
                     "    StaticText \"Hello\" | "
                     "  heading \"Topic\" level=3 | "
                     "  checkbox \"Agreed\" checked=false | "
                     "  slider \"Volume\" valuemin=0 valuemax=100 valuetext=40 focusable=true "
                     "value=40 | "
                     "  button \"New\""),
          "hidden, added, shown, reordered");
    CHECK(BoxIs("mui0-a", 210, 20, 50, 20) && BoxIs("mui0-4", 15, 60, 100, 20), "boxes kept");
    CHECK(muiAriaAdapter_SetScale(adapter, 2.0f) == mui_success &&
              BoxIs("mui0-2", 40, 40, 200, 80) && BoxIs("mui0-4", 30, 120, 200, 40),
          "a scale");
    root.transform.e = 30.0f;
    CHECK(Send(adapter, (const muiAccessNode*[]){&root}, 1, rootChildren) &&
              BoxIs("mui0-1", 60, 20, 760, 560) && BoxIs("mui0-2", 80, 40, 200, 80),
          "a parent moved, its children with it");
    TestActions(adapter, built, &root, rootChildren, &slider);
    // The slider no longer set by the host: a plain slider.
    slider.actions = 0;
    CHECK(Send(adapter, (const muiAccessNode*[]){&slider}, 1, rootChildren) &&
              TreeIs("group \"Main\" | "
                     "  button \"OK\" focusable=true | "
                     "  group \"Greeting\" | "
                     "    StaticText \"Hello\" | "
                     "  heading \"Topic\" level=3 | "
                     "  checkbox \"Agreed\" checked=false | "
                     "  slider \"Volume\" valuemin=0 valuemax=100 value=40 | "
                     "  button \"New\" expanded=true"),
          "a range made a plain slider");
    (void)s_asked;
    muiDestroyAriaAdapter(adapter);
    CHECK(TreeIs(""), "destroyed");
    TestEnablingFocus();
    TestFields();
    printf("mui-test: exit %d\n", s_failures == 0 ? 0 : 1);
    return 0;
}
