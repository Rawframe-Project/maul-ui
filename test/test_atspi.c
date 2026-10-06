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
// - actions listed and done, a range's value read and set, relations
//   and attributes;
// - events: states, names, values and announcements from updated
//   records; children added, hidden, moved and removed, the topmost
//   only; the focus last;
// - unknown objects and methods, a node removed, a window taken out.

#define _POSIX_C_SOURCE 200809L

#include "atspi.h"
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
    // The events received, "member kind detail1 from data; ..." with
    // paths cut to what follows /accessible/.
    char events[2048];
    size_t eventsLength;
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

static const char* Short(const char* path)
{
    const char* prefix = "/org/a11y/atspi/accessible/";
    return strncmp(path, prefix, strlen(prefix)) == 0 ? path + strlen(prefix) : path;
}

// What a signal's variant holds, as text.
static void DataOf(muiDBusIter* iter, char* out, size_t size)
{
    muiDBusIter variant;
    muiDBusIter reference;
    char name[256];
    char path[256] = "";
    const char* text = NULL;
    double number = 0.0;
    s_test.dbus.recurse(iter, &variant);
    int type = s_test.dbus.argType(&variant);
    out[0] = '\0';
    if (type == mui_dbusTypeString)
    {
        s_test.dbus.getBasic(&variant, (void*)&text);
        (void)snprintf(out, size, " %s", text);
    }
    else if (type == mui_dbusTypeDouble)
    {
        s_test.dbus.getBasic(&variant, &number);
        (void)snprintf(out, size, " %g", number);
    }
    else if (type == mui_dbusTypeUint32)
    {
        uint32_t role = 0;
        s_test.dbus.getBasic(&variant, &role);
        (void)snprintf(out, size, " %u", (unsigned)role);
    }
    else if (type == mui_dbusTypeStruct)
    {
        s_test.dbus.recurse(&variant, &reference);
        (void)(ReadString(&reference, name, sizeof(name)) && s_test.dbus.next(&reference) &&
               ReadString(&reference, path, sizeof(path)));
        (void)snprintf(out, size, " %s", Short(path));
    }
}

// Logs the application's events.
static muiDBusHandled Listen(DBusConnection* connection, DBusMessage* message, void* data)
{
    (void)connection;
    (void)data;
    const muiDBusApi* dbus = &s_test.dbus;
    const char* interface = dbus->interface(message);
    muiDBusIter iter;
    char kind[64] = "";
    int32_t detail1 = 0;
    char any[300] = "";
    if (dbus->messageType(message) != mui_dbusSignal || interface == NULL ||
        strcmp(interface, "org.a11y.atspi.Event.Object") != 0 || !dbus->iterInit(message, &iter))
    {
        return mui_dbusNotHandled;
    }
    (void)(ReadString(&iter, kind, sizeof(kind)) && dbus->next(&iter));
    dbus->getBasic(&iter, &detail1);
    (void)(dbus->next(&iter) && dbus->next(&iter));
    DataOf(&iter, any, sizeof(any));
    if (s_test.eventsLength >= sizeof(s_test.events) - 1)
    {
        return mui_dbusHandled;
    }
    s_test.eventsLength += (size_t)snprintf(
        s_test.events + s_test.eventsLength, sizeof(s_test.events) - s_test.eventsLength,
        "%s%s %s %d %s%s", s_test.eventsLength != 0 ? "; " : "", dbus->member(message), kind,
        (int)detail1, Short(dbus->path(message)), any);
    return mui_dbusHandled;
}

// Pumps until the events come, and a little more for any beyond them;
// whether they are exactly those.
static bool EventsAre(const char* expected)
{
    for (int i = 0; i < 2000 && s_test.eventsLength < strlen(expected); i++)
    {
        PumpBoth();
        Wait();
    }
    for (int i = 0; i < 20; i++)
    {
        PumpBoth();
        Wait();
    }
    bool same = strcmp(s_test.events, expected) == 0;
    if (!same)
    {
        fprintf(stderr, "events: %s\n", s_test.events);
    }
    s_test.events[0] = '\0';
    s_test.eventsLength = 0;
    return same;
}

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

// The window 1: a focused button 2 that clicks; a generic 3 around a
// label 4; a checked checkbox 0x1a labelled by 4, described by 2 and
// controlling 99, which is not held; a group 6, second of three at level
// 2 and politely live, with a text input 7 and a progress bar 9 in it;
// a slider 8 at 30 of 0 to 100.
static muiAccessUpdate Build(Built* built)
{
    *built = (Built){0};
    muiAccessNode* root = Add(built, 1, mui_roleWindow, "Main", 0, 0, 400, 300);
    muiAccessNode* ok = Add(built, 2, mui_roleButton, "OK", 10, 10, 100, 40);
    ok->flags = mui_accessFocusable;
    ok->actions = 1u << mui_actionClick;
    muiAccessNode* generic = Add(built, 3, mui_roleGeneric, NULL, 10, 60, 100, 20);
    (void)Add(built, 4, mui_roleLabel, "Hello", 0, 0, 100, 20);
    muiAccessNode* check = Add(built, 0x1a, mui_roleCheckBox, "Agree", 10, 90, 100, 20);
    check->flags = mui_accessCheckable | mui_accessChecked;
    static const muiAccessLink s_links[3] = {
        {4, mui_relationLabelledBy}, {2, mui_relationDescribedBy}, {99, mui_relationControls}};
    check->links = s_links;
    check->linkCount = 3;
    muiAccessNode* group = Add(built, 6, mui_roleGroup, "Box", 200, 100, 100, 50);
    group->values.level = 2;
    group->values.setPosition = 2;
    group->values.setSize = 3;
    group->values.live = mui_livePolite;
    // Text is set through the set-value action too, but is no range.
    Add(built, 7, mui_roleTextInput, "Go", 10, 10, 50, 20)->actions = 1u << mui_actionSetValue;
    muiAccessNode* progress = Add(built, 9, mui_roleProgressIndicator, "Load", 10, 30, 50, 10);
    progress->flags = mui_accessNumeric;
    progress->maximum = 1.0f;
    muiAccessNode* slider = Add(built, 8, mui_roleSlider, "Volume", 10, 200, 100, 20);
    slider->flags = mui_accessNumeric;
    slider->actions =
        1u << mui_actionSetValue | 1u << mui_actionIncrement | 1u << mui_actionDecrement;
    slider->value = 30.0f;
    slider->maximum = 100.0f;
    slider->step = 1.0f;
    List(built, root, (const uint64_t[]){2, 3, 0x1a, 6, 8}, 5);
    List(built, generic, (const uint64_t[]){4}, 1);
    List(built, group, (const uint64_t[]){7, 9}, 2);
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
    CHECK(IsError(Answer(GetChild(ROOT_PATH, -1)), "org.freedesktop.DBus.Error.InvalidArgs") &&
              IsError(Answer(GetChild("/org/a11y/atspi/accessible/w1n1", 99)),
                      "org.freedesktop.DBus.Error.InvalidArgs") &&
              ReferenceIs(Answer(GetChild("/org/a11y/atspi/accessible/w1n1", 1)), s_test.plugName,
                          "/org/a11y/atspi/accessible/w1n4"),
          "children by index: a negative one and one past the last refused");
    DBusMessage* parent = Call(ROOT_PATH, "org.freedesktop.DBus.Properties", "Get");
    muiDBusIter args;
    const char* accessible = "org.a11y.atspi.Accessible";
    const char* parentName = "Parent";
    s_test.dbus.iterInitAppend(parent, &args);
    (void)(s_test.dbus.appendBasic(&args, mui_dbusTypeString, (const void*)&accessible) &&
           s_test.dbus.appendBasic(&args, mui_dbusTypeString, (const void*)&parentName));
    CHECK(ReferenceIs(Answer(parent), s_test.dbus.uniqueName(s_test.registry), ROOT_PATH),
          "the root's parent is the desktop the registry gave");
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
    const char* agree = "/org/a11y/atspi/accessible/w1n1a";
    int32_t count = 0;
    int32_t index = -1;
    CHECK(ChildrenAre(window, "w1n2 w1n4 w1n1a w1n6 w1n8") &&
              PropertyOf(window, "org.a11y.atspi.Accessible", "ChildCount", mui_dbusTypeInt32,
                         &count) &&
              count == 5,
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

// GetAccessibleAtPoint on the window's root.
static DBusMessage* PointCall(int32_t x, int32_t y, uint32_t coordinates)
{
    DBusMessage* call =
        Call("/org/a11y/atspi/accessible/w1n1", "org.a11y.atspi.Component", "GetAccessibleAtPoint");
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, &x) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, &y) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeUint32, &coordinates));
    return call;
}

// Whether a node holds a point on the screen.
static bool Holds(const char* path, int32_t x, int32_t y)
{
    DBusMessage* call = Call(path, "org.a11y.atspi.Component", "Contains");
    muiDBusIter iter;
    uint32_t screen = 0;
    muiDBusBool holds = 0;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, &x) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, &y) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeUint32, &screen));
    DBusMessage* reply = Answer(call);
    bool ok = FirstOf(reply, mui_dbusTypeBoolean, &holds);
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return ok && holds != 0;
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
    CHECK(ExtentsAre("/org/a11y/atspi/accessible/w1n7", 2, 20, 20, 100, 40),
          "extents in a parent away from the window's corner");
    CHECK(ReferenceIs(Answer(PointCall(130, 175, 0)), s_test.plugName, label) &&
              !Holds(ok, 100 + 230, 50 + 40) && Holds(ok, 100 + 210, 50 + 40),
          "screen points taken into the window, a point past a box's edge");
    muiDBusBool done = 0;
    DBusMessage* reply =
        Answer(Call("/org/a11y/atspi/accessible/w1n1a", "org.a11y.atspi.Component", "GrabFocus"));
    CHECK(FirstOf(reply, mui_dbusTypeBoolean, &done) && done &&
              s_test.asked.action == mui_actionFocus && s_test.asked.target == 0x1a,
          "focusing asked of the host");
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
}

// A reply's strings, each array or struct inside taken in order, joined
// with spaces: names of actions, attributes, interfaces.
static void Flatten(muiDBusIter* iter, char* out, size_t size, size_t* length)
{
    for (int type = s_test.dbus.argType(iter); type != mui_dbusTypeInvalid;
         type = s_test.dbus.next(iter) ? s_test.dbus.argType(iter) : mui_dbusTypeInvalid)
    {
        muiDBusIter inner;
        const char* text = NULL;
        uint32_t number = 0;
        if (type == mui_dbusTypeArray || type == mui_dbusTypeStruct ||
            type == mui_dbusTypeDictEntry || type == mui_dbusTypeVariant)
        {
            s_test.dbus.recurse(iter, &inner);
            Flatten(&inner, out, size, length);
        }
        else if (type == mui_dbusTypeString || type == mui_dbusTypeObjectPath)
        {
            s_test.dbus.getBasic(iter, (void*)&text);
            *length += (size_t)snprintf(out + *length, size - *length, "%s%s",
                                        *length != 0 ? " " : "", text);
        }
        else if (type == mui_dbusTypeUint32)
        {
            s_test.dbus.getBasic(iter, &number);
            *length += (size_t)snprintf(out + *length, size - *length, "%s%u",
                                        *length != 0 ? " " : "", number);
        }
    }
}

// An Accessible or Action method's reply, flattened.
static bool ReplyIs(const char* path, const char* interface, const char* member,
                    const char* expected)
{
    DBusMessage* reply = Answer(Call(path, interface, member));
    char got[512] = "";
    size_t length = 0;
    muiDBusIter iter;
    if (reply != NULL && s_test.dbus.iterInit(reply, &iter))
    {
        Flatten(&iter, got, sizeof(got), &length);
    }
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    if (strcmp(got, expected) != 0)
    {
        fprintf(stderr, "%s of %s: %s\n", member, path, got);
    }
    return strcmp(got, expected) == 0;
}

static DBusMessage* Indexed(const char* path, const char* member, int32_t index)
{
    DBusMessage* call = Call(path, "org.a11y.atspi.Action", member);
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, &index);
    return call;
}

// Sets a range's current value: whether it was taken.
static bool SetValue(const char* path, double value)
{
    DBusMessage* call = Call(path, "org.freedesktop.DBus.Properties", "Set");
    muiDBusIter iter;
    muiDBusIter variant;
    const char* interface = "org.a11y.atspi.Value";
    const char* name = "CurrentValue";
    s_test.dbus.iterInitAppend(call, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&interface) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&name) &&
           s_test.dbus.openContainer(&iter, mui_dbusTypeVariant, "d", &variant) &&
           s_test.dbus.appendBasic(&variant, mui_dbusTypeDouble, &value) &&
           s_test.dbus.closeContainer(&iter, &variant));
    DBusMessage* reply = Answer(call);
    bool taken = reply != NULL && s_test.dbus.messageType(reply) == mui_dbusMethodReturn;
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return taken;
}

static void TestActionsAndValues(void)
{
    const char* ok = "/org/a11y/atspi/accessible/w1n2";
    const char* slider = "/org/a11y/atspi/accessible/w1n8";
    const char* label = "/org/a11y/atspi/accessible/w1n4";
    int32_t count = 0;
    muiDBusBool done = 0;
    CHECK(ReplyIs(ok, "org.a11y.atspi.Accessible", "GetInterfaces",
                  "org.a11y.atspi.Accessible org.a11y.atspi.Component org.a11y.atspi.Action") &&
              ReplyIs(slider, "org.a11y.atspi.Accessible", "GetInterfaces",
                      "org.a11y.atspi.Accessible org.a11y.atspi.Component org.a11y.atspi.Action "
                      "org.a11y.atspi.Value") &&
              ReplyIs(label, "org.a11y.atspi.Accessible", "GetInterfaces",
                      "org.a11y.atspi.Accessible org.a11y.atspi.Component"),
          "interfaces as the node has them");
    DBusMessage* reply = Answer(Indexed(ok, "DoAction", 0));
    CHECK(PropertyOf(ok, "org.a11y.atspi.Action", "NActions", mui_dbusTypeInt32, &count) &&
              count == 1 && FirstOf(reply, mui_dbusTypeBoolean, &done) && done &&
              s_test.asked.action == mui_actionClick && s_test.asked.target == 2,
          "a click listed and done");
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    CHECK(ReplyIs(slider, "org.a11y.atspi.Action", "GetActions", "increment   decrement  ") &&
              IsError(Answer(Indexed(slider, "GetName", 2)),
                      "org.freedesktop.DBus.Error.InvalidArgs") &&
              IsError(Answer(Call(label, "org.a11y.atspi.Action", "GetActions")),
                      "org.freedesktop.DBus.Error.UnknownMethod"),
          "actions by name, none past the last, none for a node without");
    double value = 0.0;
    double maximum = 0.0;
    CHECK(PropertyOf(slider, "org.a11y.atspi.Value", "CurrentValue", mui_dbusTypeDouble, &value) &&
              value == 30.0 &&
              PropertyOf(slider, "org.a11y.atspi.Value", "MaximumValue", mui_dbusTypeDouble,
                         &maximum) &&
              maximum == 100.0 && SetValue(slider, 55.0) &&
              s_test.asked.action == mui_actionSetValue && s_test.asked.value == 55.0f &&
              !SetValue(slider, 200.0) && !SetValue(label, 1.0),
          "a range's value read and set, a value past its range refused");
    s_test.asked = (muiAccessRequest){0};
    CHECK(
        !SetValue("/org/a11y/atspi/accessible/w1n9", 0.5) &&
            !SetValue("/org/a11y/atspi/accessible/w1n7", 0.0) &&
            s_test.asked.action == mui_actionClick && s_test.asked.target == 0 &&
            !PropertyOf(label, "org.a11y.atspi.Value", "CurrentValue", mui_dbusTypeDouble, &value),
        "a range the host does not set, a node that is no range");
    char relation[640];
    (void)snprintf(relation, sizeof(relation),
                   "2 %s /org/a11y/atspi/accessible/w1n4 18 %s /org/a11y/atspi/accessible/w1n2",
                   s_test.plugName, s_test.plugName);
    CHECK(ReplyIs("/org/a11y/atspi/accessible/w1n1a", "org.a11y.atspi.Accessible", "GetRelationSet",
                  relation) &&
              ReplyIs(ok, "org.a11y.atspi.Accessible", "GetRelationSet", ""),
          "relations of two kinds to held nodes, none to a node not held");
    CHECK(ReplyIs("/org/a11y/atspi/accessible/w1n6", "org.a11y.atspi.Accessible", "GetAttributes",
                  "level 2 posinset 2 setsize 3 live polite") &&
              ReplyIs(ok, "org.a11y.atspi.Accessible", "GetAttributes", ""),
          "attributes");
}

// Sends records, the children they list from their own array.
static bool Send(muiAtspiAdapter* adapter, const muiAccessNode* const* nodes, uint32_t count,
                 const uint64_t* children, uint64_t focus)
{
    const muiAccessUpdate update = {nodes, count, children, 0, focus};
    return muiAtspiAdapter_Apply(adapter, &update) == mui_success;
}

static void TestEvents(muiAtspiAdapter* adapter, Built* built)
{
    char expected[512];
    muiAccessNode check = built->nodes[4];
    check.flags = mui_accessCheckable;
    check.text[mui_accessLabel] = "Agreed";
    check.textLength[mui_accessLabel] = 6;
    muiAccessNode slider = built->nodes[8];
    slider.value = 40.0f;
    muiAccessNode group = built->nodes[5];
    group.text[mui_accessLabel] = "Box 2";
    group.textLength[mui_accessLabel] = 5;
    CHECK(Send(adapter, (const muiAccessNode*[]){&check, &slider, &group}, 3, built->children, 0) &&
              EventsAre("StateChanged checked 0 w1n1a; PropertyChange accessible-name 0 w1n1a "
                        "Agreed; PropertyChange accessible-value 0 w1n8 40; PropertyChange "
                        "accessible-name 0 w1n6 Box 2; Announcement  1 w1n6 Box 2"),
          "states, names, values and announcements");
    CHECK(!adapter->reshaped, "no walk for states, names and values");
    // Added to the group: a button 10, and a scroller 11 that clips,
    // holding a button 12.
    muiAccessNode added = {.id = 10, .role = mui_roleButton, .bounds = {0, 0, 10, 10}};
    muiAccessNode scroller = {.id = 11,
                              .role = mui_roleScrollView,
                              .flags = mui_accessClipsChildren,
                              .bounds = {0, 0, 50, 50},
                              .transform = {1, 0, 0, 1, 0, 0},
                              .firstChild = 4,
                              .childCount = 1};
    muiAccessNode inner = {.id = 12,
                           .role = mui_roleButton,
                           .bounds = {10, 10, 10, 10},
                           .transform = {1, 0, 0, 1, 0, 0}};
    const uint64_t groupChildren[5] = {7, 9, 10, 11, 12};
    group.firstChild = 0;
    group.childCount = 4;
    CHECK(Send(adapter, (const muiAccessNode*[]){&group, &added, &scroller, &inner}, 4,
               groupChildren, 0) &&
              EventsAre("ChildrenChanged add 2 w1n6 w1na; ChildrenChanged add 3 w1n6 w1nb"),
          "children added, the topmost told");
    muiAccessNode progress = built->nodes[7];
    progress.flags |= mui_accessHidden;
    CHECK(Send(adapter, (const muiAccessNode*[]){&progress}, 1, built->children, 0) &&
              EventsAre("ChildrenChanged remove -1 w1n6 w1n9"),
          "a child hidden");
    CHECK(Send(adapter, (const muiAccessNode*[]){&built->nodes[0]}, 1, built->children, 9) &&
              EventsAre("ChildrenChanged add 1 w1n6 w1n9; StateChanged focused 0 w1n2; "
                        "StateChanged focused 1 w1n9") &&
              Send(adapter, (const muiAccessNode*[]){&built->nodes[0]}, 1, built->children, 2) &&
              EventsAre("ChildrenChanged remove -1 w1n6 w1n9; StateChanged focused 0 w1n9; "
                        "StateChanged focused 1 w1n2"),
          "a hidden node shown while focused");
    // The button 12 moved out of the scroller, and back; the scroller
    // shrunk past it, and grown back.
    inner.bounds.x = 500.0f;
    CHECK(Send(adapter, (const muiAccessNode*[]){&inner}, 1, groupChildren, 0) &&
              EventsAre("ChildrenChanged remove -1 w1nb w1nc"),
          "a child moved out of a parent that clips");
    inner.bounds.x = 10.0f;
    CHECK(Send(adapter, (const muiAccessNode*[]){&inner}, 1, groupChildren, 0) &&
              EventsAre("ChildrenChanged add 0 w1nb w1nc"),
          "and back in");
    scroller.bounds.width = 5.0f;
    CHECK(Send(adapter, (const muiAccessNode*[]){&scroller}, 1, groupChildren, 0) &&
              EventsAre("ChildrenChanged remove -1 w1nb w1nc"),
          "a parent that clips shrunk");
    scroller.bounds.width = 50.0f;
    CHECK(Send(adapter, (const muiAccessNode*[]){&scroller}, 1, groupChildren, 0) &&
              EventsAre("ChildrenChanged add 0 w1nb w1nc"),
          "and grown");
    // The generic 3 named, so shown: the label 4 moves under it.
    muiAccessNode generic = built->nodes[2];
    generic.text[mui_accessLabel] = "Greeting";
    generic.textLength[mui_accessLabel] = 8;
    CHECK(Send(adapter, (const muiAccessNode*[]){&generic}, 1, built->children, 0) &&
              EventsAre("PropertyChange accessible-name 0 w1n3 Greeting; ChildrenChanged remove -1 "
                        "w1n1 w1n4; ChildrenChanged add 1 w1n1 w1n3"),
          "a child moved under a node newly shown, told once");
    generic.text[mui_accessLabel] = NULL;
    generic.textLength[mui_accessLabel] = 0;
    CHECK(Send(adapter, (const muiAccessNode*[]){&generic}, 1, built->children, 0) &&
              EventsAre("PropertyChange accessible-name 0 w1n3 ; ChildrenChanged remove -1 w1n1 "
                        "w1n3; ChildrenChanged add 1 w1n1 w1n4"),
          "a generic node's label gone, so flattened");
    generic.role = mui_roleGroup;
    CHECK(Send(adapter, (const muiAccessNode*[]){&generic}, 1, built->children, 0) &&
              EventsAre("PropertyChange accessible-role 0 w1n3 99; ChildrenChanged remove -1 w1n1 "
                        "w1n4; ChildrenChanged add 1 w1n1 w1n3"),
          "a generic node given a role, so shown with no label");
    CHECK(Send(adapter, (const muiAccessNode*[]){&built->nodes[0]}, 1, built->children, 0x1a) &&
              EventsAre("StateChanged focused 0 w1n2; StateChanged focused 1 w1n1a"),
          "the focus moved");
    // A new root above the window's old one, and back.
    muiAccessNode above = {.id = 20, .role = mui_roleWindow, .childCount = 1};
    const uint64_t aboveChildren[1] = {1};
    const muiAccessUpdate raise = {(const muiAccessNode*[]){&above}, 1, aboveChildren, 20, 0};
    const muiAccessUpdate lower = {(const muiAccessNode*[]){&built->nodes[0]}, 1, built->children,
                                   1, 0};
    muiResult raised = muiAtspiAdapter_Apply(adapter, &raise);
    CHECK(raised == mui_success &&
              EventsAre("ChildrenChanged remove -1 root w1n1; ChildrenChanged add 0 root w1n14") &&
              muiAtspiAdapter_Apply(adapter, &lower) == mui_success &&
              EventsAre("ChildrenChanged remove -1 root w1n14; StateChanged defunct 1 w1n14; "
                        "ChildrenChanged add 0 root w1n1"),
          "a new root above the old, and taken away");
    // The group taken out with what it holds: one removal and defunct.
    muiAccessNode root = built->nodes[0];
    const uint64_t rootChildren[4] = {2, 3, 0x1a, 8};
    root.firstChild = 0;
    root.childCount = 4;
    (void)snprintf(expected, sizeof(expected),
                   "ChildrenChanged remove -1 w1n1 w1n6; StateChanged defunct 1 w1n6");
    CHECK(Send(adapter, (const muiAccessNode*[]){&root}, 1, rootChildren, 0) && EventsAre(expected),
          "a subtree removed, its top told");
    // Back as it was built, for the tests after.
    const muiAccessUpdate update = {built->sent, built->nodeCount, built->children, 1, 2};
    CHECK(muiAtspiAdapter_Apply(adapter, &update) == mui_success &&
              EventsAre("PropertyChange accessible-role 0 w1n3 39; StateChanged checked 1 w1n1a; "
                        "PropertyChange accessible-name 0 w1n1a Agree; PropertyChange "
                        "accessible-value 0 w1n8 30; ChildrenChanged remove -1 w1n1 w1n3; "
                        "ChildrenChanged add 1 w1n1 w1n4; ChildrenChanged add 3 w1n1 w1n6; "
                        "StateChanged focused 0 w1n1a; StateChanged focused 1 w1n2"),
          "restored: a name gone told empty, removals before additions");
    // A label node named now by a label of its own, its text the same;
    // a value on it, which is no range, not told.
    muiAccessNode label = built->nodes[3];
    label.value = 5.0f;
    label.text[mui_accessLabel] = "Hi";
    label.textLength[mui_accessLabel] = 2;
    CHECK(Send(adapter, (const muiAccessNode*[]){&label}, 1, built->children, 0) &&
              EventsAre("PropertyChange accessible-name 0 w1n4 Hi") &&
              Send(adapter, (const muiAccessNode*[]){&built->nodes[3]}, 1, built->children, 0) &&
              EventsAre("PropertyChange accessible-name 0 w1n4 Hello"),
          "a name from another text; a value on a node that is no range");
    // Two siblings swapped: the one out of order told moved.
    const uint64_t swapped[5] = {2, 3, 0x1a, 8, 6};
    root.childCount = 5;
    CHECK(Send(adapter, (const muiAccessNode*[]){&root}, 1, swapped, 0) &&
              EventsAre("ChildrenChanged remove -1 w1n1 w1n6; ChildrenChanged add 4 w1n1 w1n6"),
          "siblings reordered");
    CHECK(Send(adapter, (const muiAccessNode*[]){&built->nodes[0]}, 1, built->children, 0) &&
              EventsAre("ChildrenChanged remove -1 w1n1 w1n8; ChildrenChanged add 4 w1n1 w1n8"),
          "and back");
}

static void TestGone(muiAtspiAdapter* adapter, Built* built)
{
    CHECK(IsError(Answer(Call("/org/a11y/atspi/accessible/w9n1", "org.a11y.atspi.Accessible",
                              "GetRole")),
                  "org.freedesktop.DBus.Error.UnknownObject") &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1nzz", "org.a11y.atspi.Accessible",
                                  "GetRole")),
                      "org.freedesktop.DBus.Error.UnknownObject") &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1n2x", "org.a11y.atspi.Accessible",
                                  "GetRole")),
                      "org.freedesktop.DBus.Error.UnknownObject") &&
              IsError(Answer(Call(ROOT_PATH, "org.a11y.atspi.Accessible", "Frobnicate")),
                      "org.freedesktop.DBus.Error.UnknownMethod"),
          "unknown objects and methods");
    muiAccessNode root = built->nodes[0];
    root.firstChild = 1;
    root.childCount = 4;
    const muiAccessNode* sent[1] = {&root};
    const muiAccessUpdate update = {sent, 1, built->children, 0, 0};
    CHECK(muiAtspiAdapter_Apply(adapter, &update) == mui_success &&
              EventsAre("ChildrenChanged remove -1 w1n1 w1n2; StateChanged defunct 1 w1n2; "
                        "StateChanged focused 1 w1n1") &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1n2", "org.a11y.atspi.Accessible",
                                  "GetRole")),
                      "org.freedesktop.DBus.Error.UnknownObject") &&
              ChildrenAre("/org/a11y/atspi/accessible/w1n1", "w1n4 w1n1a w1n6 w1n8"),
          "a node removed");
    muiAtspiAdapterDef def = muiDefaultAtspiAdapterDef();
    def.action = Act;
    muiAtspiAdapter* second = NULL;
    muiAtspiAdapter* third = NULL;
    muiAccessNode alone = {.id = 9, .role = mui_roleWindow};
    const muiAccessNode* aloneSent[1] = {&alone};
    const muiAccessUpdate aloneUpdate = {aloneSent, 1, NULL, 9, 9};
    CHECK(muiCreateAtspiAdapter(s_test.app, &def, &second) == mui_success &&
              ChildrenAre(ROOT_PATH, "w1n1"),
          "a window with no tree is no child");
    CHECK(muiCreateAtspiAdapter(s_test.app, &def, &third) == mui_success &&
              muiAtspiAdapter_Apply(third, &aloneUpdate) == mui_success &&
              EventsAre("ChildrenChanged add 1 root w3n9; StateChanged focused 1 w3n9") &&
              ChildrenAre(ROOT_PATH, "w1n1 w3n9"),
          "a third window, after the second with no tree");
    muiDestroyAtspiAdapter(adapter);
    CHECK(EventsAre("ChildrenChanged remove -1 root w1n1; StateChanged defunct 1 w1n1") &&
              ChildrenAre(ROOT_PATH, "w3n9") && RoleOf("/org/a11y/atspi/accessible/w3n9") == 23,
          "the first window taken out, the third kept");
    muiDestroyAtspiAdapter(second);
    muiDestroyAtspiAdapter(third);
    CHECK(EventsAre("ChildrenChanged remove -1 root w3n9; StateChanged defunct 1 w3n9") &&
              ChildrenAre(ROOT_PATH, "") &&
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
              s_test.dbus.addFilter(s_test.registry, Registry, NULL, NULL) &&
              s_test.dbus.addFilter(s_test.registry, Listen, NULL, NULL),
          "the registry");
    s_test.dbus.addMatch(s_test.registry, "type='signal',interface='org.a11y.atspi.Event.Object'",
                         NULL);
    muiAtspiAppDef def = muiDefaultAtspiAppDef();
    def.name = "maul test";
    def.allocator = (muiAllocator){Poisoned, Unpoisoned, NULL};
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
              muiAccessTree_Count(muiAtspiAdapter_GetTree(adapter)) == 9,
          "a window");
    CHECK(EventsAre("ChildrenChanged add 0 root w1n1; StateChanged focused 1 w1n2"),
          "a window's root added to the application's, its nodes not told; the focus");
    TestContract();
    TestRoot();
    TestNodes();
    TestComponent(adapter);
    TestActionsAndValues();
    TestEvents(adapter, &s_built);
    TestGone(adapter, &s_built);
    muiDestroyAtspiApp(s_test.app);
    s_test.dbus.close(s_test.registry);
    s_test.dbus.unrefConnection(s_test.registry);
    (void)kill(s_test.daemon, SIGTERM);
    return s_failures == 0 ? 0 : 1;
}
