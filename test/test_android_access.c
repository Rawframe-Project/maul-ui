// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter's test, native half (record
// mui-0008): the adapter for the test activity's view
// (test/android/java/maul/ui/tests/TestActivity.java, which holds the
// checks), at 2 pixels a unit, with a tree:
// the window 1, "Main"; a focused button 2 that clicks; a generic 3
// around a label 4 whose name is past the Basic Multilingual Plane, of
// two words, its characters placed; a text field 5 "Name" holding "Ada", one word, being edited
// with the caret at its end, whose selection and text are set; a
// checked check box 7; a slider 8 at 30 of 100 that steps and is set,
// described and with a value text; a live heading 9. The host's action
// function records what it is asked, and the adapter's events are
// recorded in place of the provider's, as "type id changes;" each, a
// text event as "type id numbers before;".

#include "android.h"

#include "maul-ui/access_android.h"

#include <stdio.h>
#include <string.h>

static muiAccessRequest s_asked = {.action = 0xFF};
static char s_askedText[64];
static muiAccessNode s_nodes[9];
static const muiAccessNode* s_sent[9];
static uint64_t s_children[8];

static bool Act(void* user, const muiAccessRequest* request)
{
    (void)user;
    s_asked = *request;
    size_t kept = request->length < sizeof s_askedText ? request->length : 0;
    memcpy(s_askedText, request->text != NULL ? request->text : "", kept);
    s_askedText[kept] = '\0';
    return true;
}

static muiAccessNode* Add(uint32_t* count, uint64_t id, muiRole role, const char* label, float x,
                          float y, float width, float height)
{
    muiAccessNode* node = &s_nodes[*count];
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
    s_sent[(*count)++] = node;
    return node;
}

static void SetText(muiAccessNode* node, muiAccessTextKind kind, const char* text)
{
    node->text[kind] = text;
    node->textLength[kind] = (uint32_t)strlen(text);
}

static uint32_t Build(void)
{
    uint32_t count = 0;
    muiAccessNode* root = Add(&count, 1, mui_roleWindow, "Main", 0, 0, 300, 300);
    muiAccessNode* button = Add(&count, 2, mui_roleButton, "OK", 10, 10, 100, 40);
    button->flags = mui_accessFocusable;
    button->actions = 1u << mui_actionClick;
    muiAccessNode* generic = Add(&count, 3, mui_roleGeneric, NULL, 0, 50, 200, 20);
    static const muiAccessWord s_labelWords[2] = {{0, 2}, {3, 7}};
    static const muiAccessWord s_fieldWords[1] = {{0, 3}};
    // The label's characters: H 10 wide, i and the space 5, the emoji 20,
    // on one line 20 high.
    static const uint32_t s_labelLines[1] = {0};
    static const muiAccessLineBox s_labelBox[1] = {{0.0f, 20.0f, 0, false}};
    static const muiAccessCluster s_labelClusters[4] = {
        {0, 1, 0.0f, 10.0f}, {1, 2, 10.0f, 15.0f}, {2, 3, 15.0f, 20.0f}, {3, 7, 20.0f, 40.0f}};
    Add(&count, 4, mui_roleLabel, "Hi \xF0\x9F\x98\x80", 5, 0, 100, 20)->marks =
        (muiAccessTextMarks){.lineStarts = s_labelLines,
                             .lineCount = 1,
                             .words = s_labelWords,
                             .wordCount = 2,
                             .lineBoxes = s_labelBox,
                             .clusters = s_labelClusters,
                             .clusterCount = 4};
    muiAccessNode* field = Add(&count, 5, mui_roleTextInput, "Name", 10, 80, 200, 30);
    field->flags = mui_accessFocusable;
    field->actions = 1u << mui_actionSetSelection | 1u << mui_actionReplaceText;
    SetText(field, mui_accessValue, "Ada");
    field->marks = (muiAccessTextMarks){
        .anchor = 3, .focus = 3, .selected = true, .words = s_fieldWords, .wordCount = 1};
    Add(&count, 7, mui_roleCheckBox, "Agree", 10, 120, 100, 20)->flags =
        mui_accessCheckable | mui_accessChecked;
    muiAccessNode* slider = Add(&count, 8, mui_roleSlider, "Volume", 10, 150, 100, 20);
    slider->flags = mui_accessNumeric;
    slider->actions =
        1u << mui_actionIncrement | 1u << mui_actionDecrement | 1u << mui_actionSetValue;
    slider->value = 30.0f;
    slider->maximum = 100.0f;
    SetText(slider, mui_accessDescription, "Louder");
    SetText(slider, mui_accessValue, "30 percent");
    Add(&count, 9, mui_roleHeading, "Title", 10, 180, 100, 20)->values.live = mui_livePolite;
    const uint64_t children[7] = {2, 3, 5, 7, 8, 9, 4};
    memcpy(s_children, children, sizeof children);
    root->firstChild = 0;
    root->childCount = 6;
    generic->firstChild = 6;
    generic->childCount = 1;
    return count;
}

JNIEXPORT jlong JNICALL Java_maul_ui_tests_TestActivity_make(JNIEnv* env, jclass type,
                                                             jobject host);
JNIEXPORT jlong JNICALL Java_maul_ui_tests_TestActivity_make(JNIEnv* env, jclass type, jobject host)
{
    (void)type;
    muiAndroidAdapterDef def = muiDefaultAndroidAdapterDef();
    def.env = env;
    def.view = host;
    def.scale = 2.0f;
    def.action = Act;
    muiAndroidAdapter* adapter = NULL;
    if (muiCreateAndroidAdapter(&def, &adapter) != mui_success)
    {
        return 0;
    }
    const muiAccessUpdate update = {s_sent, Build(), s_children, 1, 2};
    if (muiAndroidAdapter_Apply(adapter, &update) != mui_success)
    {
        muiDestroyAndroidAdapter(adapter);
        return 0;
    }
    return (jlong)(intptr_t)adapter;
}

JNIEXPORT jobject JNICALL Java_maul_ui_tests_TestActivity_providerOf(JNIEnv* env, jclass type,
                                                                     jlong adapter);
JNIEXPORT jobject JNICALL Java_maul_ui_tests_TestActivity_providerOf(JNIEnv* env, jclass type,
                                                                     jlong adapter)
{
    (void)type;
    jobject provider = muiAndroidAdapter_GetRoot((muiAndroidAdapter*)(intptr_t)adapter);
    return (*env)->NewLocalRef(env, provider);
}

JNIEXPORT jint JNICALL Java_maul_ui_tests_TestActivity_virtualOf(JNIEnv* env, jclass type,
                                                                 jlong adapter, jlong node);
JNIEXPORT jint JNICALL Java_maul_ui_tests_TestActivity_virtualOf(JNIEnv* env, jclass type,
                                                                 jlong adapter, jlong node)
{
    (void)env;
    (void)type;
    return muiAndroidVirtualOf((muiAndroidAdapter*)(intptr_t)adapter, (uint64_t)node);
}

JNIEXPORT jint JNICALL Java_maul_ui_tests_TestActivity_askedAction(JNIEnv* env, jclass type);
JNIEXPORT jint JNICALL Java_maul_ui_tests_TestActivity_askedAction(JNIEnv* env, jclass type)
{
    (void)env;
    (void)type;
    return (jint)s_asked.action;
}

JNIEXPORT jlong JNICALL Java_maul_ui_tests_TestActivity_askedTarget(JNIEnv* env, jclass type);
JNIEXPORT jlong JNICALL Java_maul_ui_tests_TestActivity_askedTarget(JNIEnv* env, jclass type)
{
    (void)env;
    (void)type;
    return (jlong)s_asked.target;
}

JNIEXPORT jint JNICALL Java_maul_ui_tests_TestActivity_askedAnchor(JNIEnv* env, jclass type);
JNIEXPORT jint JNICALL Java_maul_ui_tests_TestActivity_askedAnchor(JNIEnv* env, jclass type)
{
    (void)env;
    (void)type;
    return (jint)s_asked.anchor;
}

JNIEXPORT jint JNICALL Java_maul_ui_tests_TestActivity_askedFocus(JNIEnv* env, jclass type);
JNIEXPORT jint JNICALL Java_maul_ui_tests_TestActivity_askedFocus(JNIEnv* env, jclass type)
{
    (void)env;
    (void)type;
    return (jint)s_asked.focus;
}

// The text asked for, as its UTF-8 bytes.
JNIEXPORT jbyteArray JNICALL Java_maul_ui_tests_TestActivity_askedText(JNIEnv* env, jclass type);
JNIEXPORT jbyteArray JNICALL Java_maul_ui_tests_TestActivity_askedText(JNIEnv* env, jclass type)
{
    (void)type;
    jsize length = (jsize)strlen(s_askedText);
    jbyteArray bytes = (*env)->NewByteArray(env, length);
    if (bytes != NULL)
    {
        (*env)->SetByteArrayRegion(env, bytes, 0, length, (const jbyte*)s_askedText);
    }
    return bytes;
}

JNIEXPORT jfloat JNICALL Java_maul_ui_tests_TestActivity_askedValue(JNIEnv* env, jclass type);
JNIEXPORT jfloat JNICALL Java_maul_ui_tests_TestActivity_askedValue(JNIEnv* env, jclass type)
{
    (void)env;
    (void)type;
    return s_asked.value;
}

static char s_told[512];

static void Record(const muiAndroidAdapter* adapter, jint virtualId, jint type, jint changes)
{
    (void)adapter;
    size_t used = strlen(s_told);
    (void)snprintf(s_told + used, sizeof(s_told) - used, "%d %d %d;", (int)type, (int)virtualId,
                   (int)changes);
}

static void RecordText(const muiAndroidAdapter* adapter, jint virtualId, jint type,
                       const jint numbers[3], const char* before, uint32_t length)
{
    (void)adapter;
    size_t used = strlen(s_told);
    (void)snprintf(s_told + used, sizeof(s_told) - used, "%d %d %d %d %d %.*s;", (int)type,
                   (int)virtualId, (int)numbers[0], (int)numbers[1], (int)numbers[2],
                   before != NULL ? (int)length : 1, before != NULL ? before : "-");
}

// The update of a step, applied: 1 the button renamed, 2 the label's
// value, 3 the slider's value text, 4 the check box unchecked, 5 the
// field renamed, 6 the button again unchanged, 7 the focus to the field,
// 8 the window focused, naming the check box its active descendant,
// 9 the field's "Ada" typed into, "Adam" with the caret at its end.
static void Step(muiAndroidAdapter* adapter, jint step)
{
    static const muiAccessWord s_typed[1] = {{0, 4}};
    muiAccessNode node = s_nodes[step == 1 || step == 6   ? 1
                                 : step == 2              ? 3
                                 : step == 3              ? 6
                                 : step == 4              ? 5
                                 : step == 5 || step == 9 ? 4
                                                          : 0];
    if (step == 1 || step == 6)
    {
        SetText(&node, mui_accessLabel, "Okay");
    }
    else if (step == 2)
    {
        // Its words no longer fit.
        SetText(&node, mui_accessValue, "Hey");
        node.marks = (muiAccessTextMarks){0};
    }
    else if (step == 3)
    {
        SetText(&node, mui_accessValue, "40 percent");
    }
    else if (step == 4)
    {
        node.flags = mui_accessCheckable;
    }
    else if (step == 5)
    {
        SetText(&node, mui_accessLabel, "Your name");
    }
    else if (step == 9)
    {
        SetText(&node, mui_accessLabel, "Your name");
        SetText(&node, mui_accessValue, "Adam");
        node.marks.anchor = 4;
        node.marks.focus = 4;
        node.marks.words = s_typed;
    }
    static const muiAccessLink s_active[1] = {{7, mui_relationActiveDescendant}};
    if (step == 8)
    {
        node.links = s_active;
        node.linkCount = 1;
    }
    const muiAccessUpdate update = {(const muiAccessNode*[]){&node}, 1, s_children, 0,
                                    step == 7   ? 5
                                    : step == 8 ? 1
                                                : 0};
    (void)muiAndroidAdapter_Apply(adapter, &update);
}

JNIEXPORT jstring JNICALL Java_maul_ui_tests_TestActivity_toldAfter(JNIEnv* env, jclass type,
                                                                    jlong adapter, jint step);
JNIEXPORT jstring JNICALL Java_maul_ui_tests_TestActivity_toldAfter(JNIEnv* env, jclass type,
                                                                    jlong adapter, jint step)
{
    (void)type;
    muiAndroidAdapter* made = (muiAndroidAdapter*)(intptr_t)adapter;
    muiAndroidTellFunction tell = made->tell;
    muiAndroidTellTextFunction tellText = made->tellText;
    made->tell = Record;
    made->tellText = RecordText;
    s_told[0] = '\0';
    Step(made, step);
    made->tell = tell;
    made->tellText = tellText;
    return (*env)->NewStringUTF(env, s_told);
}

JNIEXPORT void JNICALL Java_maul_ui_tests_TestActivity_removeButton(JNIEnv* env, jclass type,
                                                                    jlong adapter);
JNIEXPORT void JNICALL Java_maul_ui_tests_TestActivity_removeButton(JNIEnv* env, jclass type,
                                                                    jlong adapter)
{
    (void)env;
    (void)type;
    muiAccessNode root = s_nodes[0];
    root.firstChild = 1;
    root.childCount = 5;
    const muiAccessUpdate update = {(const muiAccessNode*[]){&root}, 1, s_children, 0, 0};
    (void)muiAndroidAdapter_Apply((muiAndroidAdapter*)(intptr_t)adapter, &update);
}

JNIEXPORT void JNICALL Java_maul_ui_tests_TestActivity_destroy(JNIEnv* env, jclass type,
                                                               jlong adapter);
JNIEXPORT void JNICALL Java_maul_ui_tests_TestActivity_destroy(JNIEnv* env, jclass type,
                                                               jlong adapter)
{
    (void)env;
    (void)type;
    muiDestroyAndroidAdapter((muiAndroidAdapter*)(intptr_t)adapter);
}
