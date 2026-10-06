// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter's test, native half (record
// mui-0008): the adapter for the test activity's view
// (test/android/java/maul/ui/tests/TestActivity.java, which holds the
// checks), at 2 pixels a unit, with a tree:
// the window 1, "Main"; a focused button 2 that clicks; a generic 3
// around a label 4 whose name is past the Basic Multilingual Plane; a
// text field 5 "Name" holding "Ada"; a checked check box 7; a slider 8
// at 30 of 100 that steps and is set, described and with a value text;
// a live heading 9. The host's action function records what it is asked.

#include "android.h"

#include "maul-ui/access_android.h"

#include <string.h>

static muiAccessRequest s_asked = {.action = 0xFF};
static muiAccessNode s_nodes[9];
static const muiAccessNode* s_sent[9];
static uint64_t s_children[8];

static bool Act(void* user, const muiAccessRequest* request)
{
    (void)user;
    s_asked = *request;
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
    (void)Add(&count, 4, mui_roleLabel, "Hi \xF0\x9F\x98\x80", 5, 0, 100, 20);
    muiAccessNode* field = Add(&count, 5, mui_roleTextInput, "Name", 10, 80, 200, 30);
    field->flags = mui_accessFocusable;
    SetText(field, mui_accessValue, "Ada");
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

JNIEXPORT jfloat JNICALL Java_maul_ui_tests_TestActivity_askedValue(JNIEnv* env, jclass type);
JNIEXPORT jfloat JNICALL Java_maul_ui_tests_TestActivity_askedValue(JNIEnv* env, jclass type)
{
    (void)env;
    (void)type;
    return s_asked.value;
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
