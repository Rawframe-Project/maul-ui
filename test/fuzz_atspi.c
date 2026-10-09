// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes AT-SPI method calls, which any process on the session bus may
// send: any bytes are a D-Bus message as libdbus demarshals it off the
// wire, answered by an application whose one window shows a fixed tree
// of each kind of node (src/atspi*.c). The answer is sent to a
// connection that drops it, or is an error; nothing may touch memory it
// does not own, and libdbus, which aborts on an argument its API does not
// allow, must never be handed one. White box: the application is made
// without a bus, libdbus's message functions its own and its
// connection's stubbed.

#include "atspi.h"
#include "dbus_api.h"

#include "maul-ui/access_atspi.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

// libdbus's DBusError, which only libdbus reads: room enough for it.
typedef struct Error
{
    void* words[8];
} Error;

typedef struct Wire
{
    DBusMessage* (*demarshal)(const char* bytes, int length, Error* error);
    void (*errorInit)(Error* error);
    void (*errorFree)(Error* error);
} Wire;

static muiAtspiApp* s_app;
static Wire s_wire;
// The connection the stubs are handed: never read.
static int s_connection;

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static muiDBusBool Send(DBusConnection* connection, DBusMessage* message, uint32_t* serial)
{
    (void)connection;
    (void)message;
    if (serial != nullptr)
    {
        *serial = 1;
    }
    return 1;
}

static muiDBusBool SendWithReply(DBusConnection* connection, DBusMessage* message,
                                 DBusPendingCall** pending, int timeoutMs)
{
    (void)connection;
    (void)message;
    (void)timeoutMs;
    *pending = nullptr;
    return 0;
}

static const char* UniqueName(DBusConnection* connection)
{
    (void)connection;
    return ":1.7";
}

static bool Act(void* user, const muiAccessRequest* request)
{
    (void)user;
    (void)request;
    return true;
}

// The window 1: a focused button 2 that clicks, a label 3, a checked
// checkbox 4 labelled by 3 and controlling 9, which is not held, a group
// 5 at level 2 holding a text input 6 and a slider 7 at 30 of 0 to 100.
static void Show(muiAtspiAdapter* adapter)
{
    static muiAccessNode nodes[7];
    static const muiAccessLink links[2] = {{3, mui_relationLabelledBy}, {9, mui_relationControls}};
    static const uint64_t children[6] = {2, 3, 4, 5, 6, 7};
    const muiRole roles[7] = {mui_roleWindow, mui_roleButton,    mui_roleLabel, mui_roleCheckBox,
                              mui_roleGroup,  mui_roleTextInput, mui_roleSlider};
    const char* labels[7] = {"Main", "OK", "Hello", "Agree", "Box", "Name", "Volume"};
    for (uint32_t i = 0; i < 7; i++)
    {
        muiAccessTextKind kind = roles[i] == mui_roleLabel ? mui_accessValue : mui_accessLabel;
        nodes[i] = (muiAccessNode){.id = i + 1,
                                   .role = roles[i],
                                   .bounds = {0.0f, 0.0f, 100.0f, 20.0f},
                                   .transform = {1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 30.0f * (float)i}};
        nodes[i].text[kind] = labels[i];
        nodes[i].textLength[kind] = (uint32_t)strlen(labels[i]);
    }
    nodes[0].childCount = 4;
    nodes[0].firstChild = 0;
    nodes[4].childCount = 2;
    nodes[4].firstChild = 4;
    nodes[1].flags = mui_accessFocusable;
    nodes[1].actions = 1u << mui_actionClick;
    nodes[3].flags = mui_accessCheckable | mui_accessChecked;
    nodes[3].links = links;
    nodes[3].linkCount = 2;
    nodes[4].values.level = 2;
    nodes[5].actions = 1u << mui_actionSetValue;
    // A value of characters of one to three bytes, selected, on two lines
    // of three words, its characters placed 6 wide, for the Text
    // interface's offsets and extents.
    static const char value[] = "h\xC3\xA9llo w\xE2\x82\xACrld\nnext";
    static const uint32_t lines[2] = {0, 15};
    static const muiAccessWord words[3] = {{0, 6}, {7, 14}, {15, 19}};
    static const muiAccessLineBox boxes[2] = {{0.0f, 10.0f, 0, false}, {10.0f, 20.0f, 11, false}};
    static const uint32_t starts[16] = {0, 1, 3, 4, 5, 6, 7, 8, 11, 12, 13, 15, 16, 17, 18, 19};
    static muiAccessCluster clusters[15];
    for (uint32_t i = 0; i < 15; i++)
    {
        float x = 6.0f * (float)(i < 11 ? i : i - 11);
        clusters[i] = (muiAccessCluster){starts[i], i == 10 ? 14 : starts[i + 1], x, x + 6.0f};
    }
    nodes[5].text[mui_accessValue] = value;
    nodes[5].textLength[mui_accessValue] = sizeof value - 1;
    nodes[5].marks = (muiAccessTextMarks){.anchor = 7,
                                          .focus = 1,
                                          .selected = true,
                                          .lineStarts = lines,
                                          .lineCount = 2,
                                          .words = words,
                                          .wordCount = 3,
                                          .lineBoxes = boxes,
                                          .clusters = clusters,
                                          .clusterCount = 15};
    nodes[6].flags = mui_accessNumeric;
    nodes[6].actions = 1u << mui_actionSetValue | 1u << mui_actionIncrement;
    nodes[6].value = 30.0f;
    nodes[6].maximum = 100.0f;
    nodes[6].step = 1.0f;
    static const muiAccessNode* sent[7];
    for (uint32_t i = 0; i < 7; i++)
    {
        sent[i] = &nodes[i];
    }
    const muiAccessUpdate update = {sent, 7, children, 1, 2};
    Expect(muiAtspiAdapter_Apply(adapter, &update) == mui_success);
}

static void Start(void)
{
    const muiAtspiAppDef def = muiDefaultAtspiAppDef();
    s_app = calloc(1, sizeof *s_app);
    Expect(s_app != nullptr && muiLoadDBus(&s_app->dbus));
    s_app->allocator = def.allocator;
    s_app->windows = calloc(1, sizeof *s_app->windows);
    s_app->windowCapacity = 1;
    s_app->name = "fuzz";
    s_app->nameLength = 4;
    s_app->connection = (DBusConnection*)&s_connection;
    // Embedded, as the registry answers: the root's parent is its desktop.
    s_app->registered = true;
    memcpy(s_app->desktopName, ":1.0", sizeof ":1.0");
    memcpy(s_app->desktopPath, ATSPI_ROOT_PATH, sizeof ATSPI_ROOT_PATH);
    s_app->dbus.send = Send;
    s_app->dbus.sendWithReply = SendWithReply;
    s_app->dbus.uniqueName = UniqueName;
    void* library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    Expect(library != nullptr && s_app->windows != nullptr);
    *(void**)&s_wire.demarshal = dlsym(library, "dbus_message_demarshal");
    *(void**)&s_wire.errorInit = dlsym(library, "dbus_error_init");
    *(void**)&s_wire.errorFree = dlsym(library, "dbus_error_free");
    Expect(s_wire.demarshal != nullptr && s_wire.errorInit != nullptr &&
           s_wire.errorFree != nullptr);
    muiAtspiAdapterDef adapterDef = muiDefaultAtspiAdapterDef();
    adapterDef.action = Act;
    muiAtspiAdapter* adapter = nullptr;
    Expect(muiCreateAtspiAdapter(s_app, &adapterDef, &adapter) == mui_success);
    Show(adapter);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (s_app == nullptr)
    {
        Start();
    }
    if (size > INT32_MAX)
    {
        return 0;
    }
    Error error;
    s_wire.errorInit(&error);
    DBusMessage* message = s_wire.demarshal((const char*)data, (int)size, &error);
    s_wire.errorFree(&error);
    if (message != nullptr)
    {
        (void)muiAtspiAnswer(s_app, message);
        s_app->dbus.unrefMessage(message);
    }
    return 0;
}
