// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter on a private bus: the test starts dbus-daemon,
// points AT_SPI_BUS_ADDRESS at it, and plays both the registry (answering
// Embed) and the client on a connection of its own, pumping it and the
// application on one thread:
// - registering: Embed called with the root, the desktop taken;
// - the application root: its name, role, windows and parent;
// - a window's nodes: children as shown, roles, names, parents, indexes,
//   states, and introspection;
// - extents in screen, window and parent pixels, the node under a point;
// - focusing asked of the host;
// - unknown objects and methods, a node removed, a window taken out.

#define _POSIX_C_SOURCE 200809L

#include "dbus_api.h"
#include "test_harness.h"

#include "maul-ui/access_atspi.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define ROOT_PATH "/org/a11y/atspi/accessible/root"

typedef struct Test
{
    muiDBusApi dbus;
    pid_t daemon;
    DBusConnection* registry;
    muiAtspiApp* app;
    // What Embed was given: the application's bus name and root's path.
    char plugName[256];
    char plugPath[256];
    // What the host was asked last.
    muiAccessRequest asked;
} Test;

static Test s_test;

// Starts a private bus, which forks and prints its address and pid.
static bool StartBus(char* address, size_t size)
{
    FILE* out = popen("dbus-daemon --session --fork --print-address=1 --print-pid=1", "r");
    char pid[32];
    bool ok = out != NULL && fgets(address, (int)size, out) != NULL &&
              fgets(pid, sizeof(pid), out) != NULL;
    if (out != NULL)
    {
        (void)pclose(out);
    }
    if (ok)
    {
        address[strcspn(address, "\n")] = '\0';
        s_test.daemon = (pid_t)atol(pid);
    }
    return ok && s_test.daemon > 0;
}

static void Wait(void)
{
    const struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, NULL);
}

// Pumps the application and the test's connection once.
static void PumpBoth(void)
{
    muiAtspiApp_Pump(s_test.app);
    (void)s_test.dbus.readWrite(s_test.registry, 0);
    while (s_test.dbus.dispatch(s_test.registry) == mui_dbusDataRemains)
    {
    }
    (void)s_test.dbus.readWrite(s_test.registry, 0);
}

static bool ReadString(muiDBusIter* iter, char* out, size_t size)
{
    const char* text = NULL;
    if (s_test.dbus.argType(iter) != mui_dbusTypeString &&
        s_test.dbus.argType(iter) != mui_dbusTypeObjectPath)
    {
        return false;
    }
    s_test.dbus.getBasic(iter, (void*)&text);
    (void)snprintf(out, size, "%s", text);
    return true;
}

// Answers Embed as the registry: the desktop is the registry's root.
static muiDBusHandled Registry(DBusConnection* connection, DBusMessage* message, void* data)
{
    (void)data;
    const muiDBusApi* dbus = &s_test.dbus;
    const char* member = dbus->member(message);
    if (dbus->messageType(message) != mui_dbusMethodCall || member == NULL ||
        strcmp(member, "Embed") != 0)
    {
        return mui_dbusNotHandled;
    }
    muiDBusIter iter;
    muiDBusIter plug;
    if (dbus->iterInit(message, &iter) && dbus->argType(&iter) == mui_dbusTypeStruct)
    {
        dbus->recurse(&iter, &plug);
        (void)(ReadString(&plug, s_test.plugName, sizeof(s_test.plugName)) && dbus->next(&plug) &&
               ReadString(&plug, s_test.plugPath, sizeof(s_test.plugPath)));
    }
    DBusMessage* reply = dbus->newMethodReturn(message);
    muiDBusIter out;
    muiDBusIter desktop;
    const char* name = dbus->uniqueName(connection);
    const char* path = ROOT_PATH;
    dbus->iterInitAppend(reply, &out);
    (void)(dbus->openContainer(&out, mui_dbusTypeStruct, NULL, &desktop) &&
           dbus->appendBasic(&desktop, mui_dbusTypeString, (const void*)&name) &&
           dbus->appendBasic(&desktop, mui_dbusTypeObjectPath, (const void*)&path) &&
           dbus->closeContainer(&out, &desktop));
    (void)dbus->send(connection, reply, NULL);
    dbus->unrefMessage(reply);
    return mui_dbusHandled;
}

static bool Act(void* user, const muiAccessRequest* request)
{
    (void)user;
    s_test.asked = *request;
    return true;
}

// A call to one of the application's objects, to fill in.
static DBusMessage* Call(const char* path, const char* interface, const char* member)
{
    return s_test.dbus.newMethodCall(s_test.plugName, path, interface, member);
}

// Sends a call and pumps until its answer comes; the reply, which the
// caller lets go, or NULL.
static DBusMessage* Answer(DBusMessage* call)
{
    const muiDBusApi* dbus = &s_test.dbus;
    DBusPendingCall* pending = NULL;
    DBusMessage* reply = NULL;
    if (call != NULL && dbus->sendWithReply(s_test.registry, call, &pending, 5000) &&
        pending != NULL)
    {
        for (int i = 0; i < 5000 && !dbus->completed(pending); i++)
        {
            PumpBoth();
            Wait();
        }
        reply = dbus->completed(pending) ? dbus->stealReply(pending) : NULL;
        dbus->unrefPending(pending);
    }
    if (call != NULL)
    {
        dbus->unrefMessage(call);
    }
    return reply;
}

static bool IsError(DBusMessage* reply, const char* name)
{
    bool is = reply != NULL && s_test.dbus.messageType(reply) == mui_dbusMessageError &&
              strcmp(s_test.dbus.errorName(reply), name) == 0;
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return is;
}

// The first argument of a reply as a basic value; false for another
// type or an error.
static bool FirstOf(DBusMessage* reply, int type, void* value)
{
    muiDBusIter iter;
    bool ok = reply != NULL && s_test.dbus.messageType(reply) == mui_dbusMethodReturn &&
              s_test.dbus.iterInit(reply, &iter) && s_test.dbus.argType(&iter) == type;
    if (ok)
    {
        s_test.dbus.getBasic(&iter, value);
    }
    return ok;
}

// The path a reference reply (so), or a variant holding one, names.
static bool ReferenceIs(DBusMessage* reply, const char* name, const char* path)
{
    muiDBusIter iter;
    muiDBusIter inner;
    muiDBusIter reference;
    char gotName[256] = "";
    char gotPath[256] = "";
    bool ok = reply != NULL && s_test.dbus.iterInit(reply, &iter);
    if (ok && s_test.dbus.argType(&iter) == mui_dbusTypeVariant)
    {
        s_test.dbus.recurse(&iter, &inner);
        iter = inner;
    }
    ok = ok && s_test.dbus.argType(&iter) == mui_dbusTypeStruct;
    if (ok)
    {
        s_test.dbus.recurse(&iter, &reference);
        ok = ReadString(&reference, gotName, sizeof(gotName)) && s_test.dbus.next(&reference) &&
             ReadString(&reference, gotPath, sizeof(gotPath));
    }
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return ok && strcmp(gotName, name) == 0 && strcmp(gotPath, path) == 0;
}

// A property's value, a variant holding a basic value.
static bool PropertyOf(const char* path, const char* interface, const char* name, int type,
                       void* value)
{
    DBusMessage* call = Call(path, "org.freedesktop.DBus.Properties", "Get");
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&interface) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&name));
    DBusMessage* reply = Answer(call);
    muiDBusIter variant;
    muiDBusIter inner;
    bool ok = reply != NULL && s_test.dbus.iterInit(reply, &variant) &&
              s_test.dbus.argType(&variant) == mui_dbusTypeVariant;
    if (ok)
    {
        s_test.dbus.recurse(&variant, &inner);
        ok = s_test.dbus.argType(&inner) == type;
        if (ok && type == mui_dbusTypeString)
        {
            const char* text = NULL;
            s_test.dbus.getBasic(&inner, (void*)&text);
            (void)snprintf(value, 256, "%s", text);
        }
        else if (ok)
        {
            s_test.dbus.getBasic(&inner, value);
        }
    }
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return ok;
}

static bool NameIs(const char* path, const char* expected)
{
    char name[256] = "";
    return PropertyOf(path, "org.a11y.atspi.Accessible", "Name", mui_dbusTypeString, name) &&
           strcmp(name, expected) == 0;
}

// The paths of an object's children, written as one string with spaces.
static bool ChildrenAre(const char* path, const char* expected)
{
    DBusMessage* reply = Answer(Call(path, "org.a11y.atspi.Accessible", "GetChildren"));
    char got[512] = "";
    size_t length = 0;
    muiDBusIter iter;
    muiDBusIter array;
    bool ok = reply != NULL && s_test.dbus.iterInit(reply, &iter) &&
              s_test.dbus.argType(&iter) == mui_dbusTypeArray;
    if (ok)
    {
        s_test.dbus.recurse(&iter, &array);
        while (s_test.dbus.argType(&array) == mui_dbusTypeStruct)
        {
            muiDBusIter reference;
            char name[256];
            char child[256];
            s_test.dbus.recurse(&array, &reference);
            if (ReadString(&reference, name, sizeof(name)) && s_test.dbus.next(&reference) &&
                ReadString(&reference, child, sizeof(child)))
            {
                length += (size_t)snprintf(got + length, sizeof(got) - length, "%s%s",
                                           length != 0 ? " " : "",
                                           child + strlen("/org/a11y/atspi/accessible/"));
            }
            (void)s_test.dbus.next(&array);
        }
    }
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    if (!ok || strcmp(got, expected) != 0)
    {
        fprintf(stderr, "children of %s: %s\n", path, got);
    }
    return ok && strcmp(got, expected) == 0;
}

static uint32_t RoleOf(const char* path)
{
    uint32_t role = 0;
    DBusMessage* reply = Answer(Call(path, "org.a11y.atspi.Accessible", "GetRole"));
    (void)FirstOf(reply, mui_dbusTypeUint32, &role);
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return role;
}

// Whether an object's state set has a state.
static bool HasState(const char* path, uint32_t state)
{
    DBusMessage* reply = Answer(Call(path, "org.a11y.atspi.Accessible", "GetState"));
    uint32_t words[2] = {0, 0};
    muiDBusIter iter;
    muiDBusIter array;
    if (reply != NULL && s_test.dbus.iterInit(reply, &iter) &&
        s_test.dbus.argType(&iter) == mui_dbusTypeArray)
    {
        s_test.dbus.recurse(&iter, &array);
        for (int i = 0; i < 2 && s_test.dbus.argType(&array) == mui_dbusTypeUint32; i++)
        {
            s_test.dbus.getBasic(&array, &words[i]);
            (void)s_test.dbus.next(&array);
        }
    }
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return (words[state / 32] & (1u << (state % 32))) != 0;
}

// An object's extents in a coordinate type.
static bool ExtentsAre(const char* path, uint32_t coordinates, int32_t x, int32_t y, int32_t width,
                       int32_t height)
{
    DBusMessage* call = Call(path, "org.a11y.atspi.Component", "GetExtents");
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)s_test.dbus.appendBasic(&iter, mui_dbusTypeUint32, &coordinates);
    DBusMessage* reply = Answer(call);
    int32_t got[4] = {-1, -1, -1, -1};
    muiDBusIter out;
    muiDBusIter tuple;
    if (reply != NULL && s_test.dbus.iterInit(reply, &out) &&
        s_test.dbus.argType(&out) == mui_dbusTypeStruct)
    {
        s_test.dbus.recurse(&out, &tuple);
        for (int i = 0; i < 4 && s_test.dbus.argType(&tuple) == mui_dbusTypeInt32; i++)
        {
            s_test.dbus.getBasic(&tuple, &got[i]);
            (void)s_test.dbus.next(&tuple);
        }
    }
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return got[0] == x && got[1] == y && got[2] == width && got[3] == height;
}

typedef struct Built
{
    muiAccessNode nodes[8];
    const muiAccessNode* sent[8];
    uint64_t children[8];
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

// The window 1: a focused button 2; a generic 3 around a label 4; a
// checked checkbox 5.
static muiAccessUpdate Build(Built* built)
{
    *built = (Built){0};
    muiAccessNode* root = Add(built, 1, mui_roleWindow, "Main", 0, 0, 400, 300);
    Add(built, 2, mui_roleButton, "OK", 10, 10, 100, 40)->flags = mui_accessFocusable;
    muiAccessNode* generic = Add(built, 3, mui_roleGeneric, NULL, 10, 60, 100, 20);
    (void)Add(built, 4, mui_roleLabel, "Hello", 0, 0, 100, 20);
    Add(built, 5, mui_roleCheckBox, "Agree", 10, 90, 100, 20)->flags =
        mui_accessCheckable | mui_accessChecked;
    List(built, root, (const uint64_t[]){2, 3, 5}, 3);
    List(built, generic, (const uint64_t[]){4}, 1);
    return (muiAccessUpdate){built->sent, built->nodeCount, built->children, 1, 2};
}

static DBusMessage* GetChild(const char* path, int32_t index)
{
    DBusMessage* call = Call(path, "org.a11y.atspi.Accessible", "GetChildAtIndex");
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, &index);
    return call;
}

static void TestRoot(void)
{
    char text[256] = "";
    int32_t id = 0;
    CHECK(NameIs(ROOT_PATH, "maul test") && RoleOf(ROOT_PATH) == 75 &&
              ChildrenAre(ROOT_PATH, "w1n1"),
          "the application root: name, role, window");
    CHECK(IsError(Answer(GetChild(ROOT_PATH, 1)), "org.freedesktop.DBus.Error.InvalidArgs") &&
              ReferenceIs(Answer(GetChild(ROOT_PATH, 0)), s_test.plugName,
                          "/org/a11y/atspi/accessible/w1n1"),
          "a window by index, none past the last");
    CHECK(ReferenceIs(Answer(Call(ROOT_PATH, "org.freedesktop.DBus.Properties", "Get")), "", "") ==
              false,
          "Get without arguments refused");
    CHECK(PropertyOf(ROOT_PATH, "org.a11y.atspi.Application", "ToolkitName", mui_dbusTypeString,
                     text) &&
              strcmp(text, "Maul UI") == 0,
          "the toolkit's name");
    DBusMessage* set = Call(ROOT_PATH, "org.freedesktop.DBus.Properties", "Set");
    muiDBusIter iter;
    muiDBusIter variant;
    const char* interface = "org.a11y.atspi.Application";
    const char* name = "Id";
    int32_t given = 42;
    s_test.dbus.iterInitAppend(set, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&interface) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&name) &&
           s_test.dbus.openContainer(&iter, mui_dbusTypeVariant, "i", &variant) &&
           s_test.dbus.appendBasic(&variant, mui_dbusTypeInt32, &given) &&
           s_test.dbus.closeContainer(&iter, &variant));
    DBusMessage* reply = Answer(set);
    CHECK(reply != NULL && s_test.dbus.messageType(reply) == mui_dbusMethodReturn &&
              PropertyOf(ROOT_PATH, "org.a11y.atspi.Application", "Id", mui_dbusTypeInt32, &id) &&
              id == 42,
          "the registry sets the id");
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    CHECK(ReferenceIs(Answer(Call(ROOT_PATH, "org.a11y.atspi.Accessible", "GetApplication")),
                      s_test.plugName, ROOT_PATH),
          "the application is the root");
}

static void TestNodes(void)
{
    const char* window = "/org/a11y/atspi/accessible/w1n1";
    const char* ok = "/org/a11y/atspi/accessible/w1n2";
    const char* agree = "/org/a11y/atspi/accessible/w1n5";
    int32_t count = 0;
    int32_t index = -1;
    CHECK(ChildrenAre(window, "w1n2 w1n4 w1n5") &&
              PropertyOf(window, "org.a11y.atspi.Accessible", "ChildCount", mui_dbusTypeInt32,
                         &count) &&
              count == 3,
          "the window's children as shown, the generic flattened");
    CHECK(NameIs(ok, "OK") && RoleOf(ok) == 43 &&
              NameIs("/org/a11y/atspi/accessible/w1n4", "Hello") && RoleOf(agree) == 7 &&
              RoleOf(window) == 23,
          "names and roles");
    CHECK(ReferenceIs(Answer(Call(ok, "org.a11y.atspi.Accessible", "GetParent")), "", "") == false,
          "a method the interface lacks");
    char parent[256];
    (void)snprintf(parent, sizeof(parent), "%s", window);
    DBusMessage* call = Call(ok, "org.freedesktop.DBus.Properties", "Get");
    muiDBusIter iter;
    const char* interface = "org.a11y.atspi.Accessible";
    const char* name = "Parent";
    s_test.dbus.iterInitAppend(call, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&interface) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&name));
    CHECK(ReferenceIs(Answer(call), s_test.plugName, parent), "a node's parent");
    call = Call(window, "org.freedesktop.DBus.Properties", "Get");
    s_test.dbus.iterInitAppend(call, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&interface) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&name));
    CHECK(ReferenceIs(Answer(call), s_test.plugName, ROOT_PATH), "a window's parent is the root");
    DBusMessage* reply = Answer(Call(agree, "org.a11y.atspi.Accessible", "GetIndexInParent"));
    CHECK(FirstOf(reply, mui_dbusTypeInt32, &index) && index == 2, "an index in the parent");
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    // Focusable 11, focused 12, enabled 8, showing 25, checked 4,
    // checkable 41.
    CHECK(HasState(ok, 11) && HasState(ok, 12) && HasState(ok, 8) && HasState(ok, 25) &&
              !HasState(agree, 12) && HasState(agree, 4) && HasState(agree, 41),
          "states");
    const char* xml = NULL;
    reply = Answer(Call(ok, "org.freedesktop.DBus.Introspectable", "Introspect"));
    CHECK(FirstOf(reply, mui_dbusTypeString, (void*)&xml) &&
              strstr(xml, "org.a11y.atspi.Component") != NULL,
          "introspection");
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
}

static void TestComponent(muiAtspiAdapter* adapter)
{
    const char* ok = "/org/a11y/atspi/accessible/w1n2";
    const char* label = "/org/a11y/atspi/accessible/w1n4";
    // A scale of 2, the window's client area at 100, 50.
    CHECK(muiAtspiAdapter_SetScale(adapter, 2.0f) == mui_success, "scale");
    muiAtspiAdapter_SetPlace(adapter, 100, 50);
    CHECK(ExtentsAre(ok, 0, 120, 70, 200, 80) && ExtentsAre(ok, 1, 20, 20, 200, 80) &&
              ExtentsAre(label, 2, 20, 120, 200, 40),
          "extents on the screen, in the window, in the parent");
    DBusMessage* call =
        Call("/org/a11y/atspi/accessible/w1n1", "org.a11y.atspi.Component", "GetAccessibleAtPoint");
    muiDBusIter iter;
    int32_t x = 130;
    int32_t y = 80;
    uint32_t screen = 0;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, &x) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, &y) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeUint32, &screen));
    CHECK(ReferenceIs(Answer(call), s_test.plugName, ok), "the node under a point");
    muiDBusBool done = 0;
    DBusMessage* reply =
        Answer(Call("/org/a11y/atspi/accessible/w1n5", "org.a11y.atspi.Component", "GrabFocus"));
    CHECK(FirstOf(reply, mui_dbusTypeBoolean, &done) && done &&
              s_test.asked.action == mui_actionFocus && s_test.asked.target == 5,
          "focusing asked of the host");
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
}

static void TestGone(muiAtspiAdapter* adapter, Built* built)
{
    CHECK(IsError(Answer(Call("/org/a11y/atspi/accessible/w9n1", "org.a11y.atspi.Accessible",
                              "GetRole")),
                  "org.freedesktop.DBus.Error.UnknownObject") &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1nzz", "org.a11y.atspi.Accessible",
                                  "GetRole")),
                      "org.freedesktop.DBus.Error.UnknownObject") &&
              IsError(Answer(Call(ROOT_PATH, "org.a11y.atspi.Accessible", "Frobnicate")),
                      "org.freedesktop.DBus.Error.UnknownMethod"),
          "unknown objects and methods");
    muiAccessNode root = built->nodes[0];
    root.firstChild = 1;
    root.childCount = 2;
    const muiAccessNode* sent[1] = {&root};
    const muiAccessUpdate update = {sent, 1, built->children, 0, 0};
    CHECK(muiAtspiAdapter_Apply(adapter, &update) == mui_success &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1n2", "org.a11y.atspi.Accessible",
                                  "GetRole")),
                      "org.freedesktop.DBus.Error.UnknownObject") &&
              ChildrenAre("/org/a11y/atspi/accessible/w1n1", "w1n4 w1n5"),
          "a node removed");
    muiDestroyAtspiAdapter(adapter);
    CHECK(ChildrenAre(ROOT_PATH, "") &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1n1", "org.a11y.atspi.Accessible",
                                  "GetRole")),
                      "org.freedesktop.DBus.Error.UnknownObject"),
          "a window taken out");
}

static void TestContract(void)
{
    muiAtspiAppDef def = muiDefaultAtspiAppDef();
    muiAtspiApp* app = NULL;
    CHECK(muiCreateAtspiApp(&def, &app) == mui_errorInvalid && app == NULL, "no name");
    muiAtspiAdapterDef adapterDef = muiDefaultAtspiAdapterDef();
    muiAtspiAdapter* adapter = NULL;
    CHECK(muiCreateAtspiAdapter(s_test.app, &adapterDef, &adapter) == mui_errorInvalid &&
              adapter == NULL,
          "no action");
    CHECK(muiAtspiApp_GetDescriptor(NULL) == -1 && !muiAtspiApp_IsRegistered(NULL) &&
              muiAtspiAdapter_Apply(NULL, NULL) == mui_errorInvalid &&
              muiAtspiAdapter_GetTree(NULL) == NULL &&
              muiAtspiAdapter_SetScale(NULL, 1.0f) == mui_errorInvalid,
          "NULL arguments");
    muiAtspiApp_Pump(NULL);
    muiAtspiAdapter_SetPlace(NULL, 0, 0);
    muiDestroyAtspiAdapter(NULL);
    muiDestroyAtspiApp(NULL);
}

int main(void)
{
    char address[512];
    if (!muiLoadDBus(&s_test.dbus) || !StartBus(address, sizeof(address)))
    {
        // A machine with no D-Bus cannot run this test.
        printf("SKIP: no libdbus-1 or dbus-daemon\n");
        return 0;
    }
    (void)setenv("AT_SPI_BUS_ADDRESS", address, 1);
    s_test.registry = s_test.dbus.openPrivate(address, NULL);
    CHECK(s_test.registry != NULL && s_test.dbus.busRegister(s_test.registry, NULL) &&
              s_test.dbus.requestName(s_test.registry, "org.a11y.atspi.Registry", 4, NULL) == 1 &&
              s_test.dbus.addFilter(s_test.registry, Registry, NULL, NULL),
          "the registry");
    muiAtspiAppDef def = muiDefaultAtspiAppDef();
    def.name = "maul test";
    CHECK(muiCreateAtspiApp(&def, &s_test.app) == mui_success, "the application");
    for (int i = 0; i < 5000 && !muiAtspiApp_IsRegistered(s_test.app); i++)
    {
        PumpBoth();
        Wait();
    }
    CHECK(muiAtspiApp_IsRegistered(s_test.app) && strcmp(s_test.plugPath, ROOT_PATH) == 0 &&
              muiAtspiApp_GetDescriptor(s_test.app) >= 0,
          "registered, the root embedded");
    muiAtspiAdapterDef adapterDef = muiDefaultAtspiAdapterDef();
    adapterDef.action = Act;
    muiAtspiAdapter* adapter = NULL;
    static Built s_built;
    muiAccessUpdate update = Build(&s_built);
    CHECK(muiCreateAtspiAdapter(s_test.app, &adapterDef, &adapter) == mui_success &&
              muiAtspiAdapter_Apply(adapter, &update) == mui_success &&
              muiAccessTree_Count(muiAtspiAdapter_GetTree(adapter)) == 5,
          "a window");
    TestContract();
    TestRoot();
    TestNodes();
    TestComponent(adapter);
    TestGone(adapter, &s_built);
    muiDestroyAtspiApp(s_test.app);
    s_test.dbus.close(s_test.registry);
    s_test.dbus.unrefConnection(s_test.registry);
    (void)kill(s_test.daemon, SIGTERM);
    return s_failures == 0 ? 0 : 1;
}
