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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ROOT_PATH "/org/a11y/atspi/accessible/root"

#if defined(__SANITIZE_THREAD__)
#define UNDER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define UNDER_TSAN 1
#endif
#endif

#ifdef UNDER_TSAN
// libdbus takes its own locks in orders ThreadSanitizer reports as
// inverted: finding the accessibility bus on the session bus, then
// registering on it, then closing. The library uses libdbus from one
// thread, where no such order can deadlock; races are still reported.
const char* __tsan_default_suppressions(void);
const char* __tsan_default_suppressions(void)
{
    return "deadlock:libdbus-1.so\n";
}
#endif

typedef struct Test
{
    muiDBusApi dbus;
    pid_t daemon;
    DBusConnection* registry;
    muiAtspiApp* app;
    // What Embed was given: the application's bus name and root's path.
    char plugName[256];
    char plugPath[256];
    // What the host was asked last, and the text it was given, which
    // lives only while it is asked; what it answers.
    muiAccessRequest asked;
    char askedText[64];
    bool refuse;
    // The events received, "member kind detail1 from data; ..." with
    // paths cut to what follows /accessible/.
    char events[2048];
    size_t eventsLength;
    // Whether the registry answers Embed with the desktop's path as a
    // string rather than an object path.
    bool pathAsString;
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
           dbus->appendBasic(&desktop,
                             s_test.pathAsString ? mui_dbusTypeString : mui_dbusTypeObjectPath,
                             (const void*)&path) &&
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

// Logs the application's events, of objects and of windows.
static muiDBusHandled Listen(DBusConnection* connection, DBusMessage* message, void* data)
{
    (void)connection;
    (void)data;
    const muiDBusApi* dbus = &s_test.dbus;
    const char* interface = dbus->interface(message);
    muiDBusIter iter;
    char kind[64] = "";
    int32_t detail1 = 0;
    int32_t detail2 = 0;
    char any[300] = "";
    if (dbus->messageType(message) != mui_dbusSignal || interface == NULL ||
        (strcmp(interface, "org.a11y.atspi.Event.Object") != 0 &&
         strcmp(interface, "org.a11y.atspi.Event.Window") != 0) ||
        !dbus->iterInit(message, &iter))
    {
        return mui_dbusNotHandled;
    }
    (void)(ReadString(&iter, kind, sizeof(kind)) && dbus->next(&iter));
    dbus->getBasic(&iter, &detail1);
    (void)dbus->next(&iter);
    dbus->getBasic(&iter, &detail2);
    (void)dbus->next(&iter);
    DataOf(&iter, any, sizeof(any));
    char details[32];
    // The second detail, a text change's length, only where it is given.
    (void)(detail2 != 0 ? snprintf(details, sizeof details, "%d/%d", (int)detail1, (int)detail2)
                        : snprintf(details, sizeof details, "%d", (int)detail1));
    if (s_test.eventsLength >= sizeof(s_test.events) - 1)
    {
        return mui_dbusHandled;
    }
    s_test.eventsLength += (size_t)snprintf(
        s_test.events + s_test.eventsLength, sizeof(s_test.events) - s_test.eventsLength,
        "%s%s %s %s %s%s", s_test.eventsLength != 0 ? "; " : "", dbus->member(message), kind,
        details, Short(dbus->path(message)), any);
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

// Whether Flatten writes signed integers too.
static bool s_flattenInts;

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
    size_t kept = request->length < sizeof s_test.askedText ? request->length : 0;
    memcpy(s_test.askedText, request->text != NULL ? request->text : "", kept);
    s_test.askedText[kept] = '\0';
    return !s_test.refuse;
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
// 2, politely live and described as settings, with a text input 7 and
// a progress bar 9 in it;
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
    group->text[mui_accessDescription] = "Settings";
    group->textLength[mui_accessDescription] = 8;
    // Text is set through the set-value action too, but is no range.
    muiAccessNode* go = Add(built, 7, mui_roleTextInput, "Go", 10, 10, 50, 20);
    go->actions = 1u << mui_actionSetValue;
    go->flags = mui_accessRequired;
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
    int32_t index = 0;
    reply = Answer(Call(ROOT_PATH, "org.a11y.atspi.Accessible", "GetIndexInParent"));
    CHECK(FirstOf(reply, mui_dbusTypeInt32, &index) && index == -1,
          "the root's index in its parent, -1 as AT-SPI asks");
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    // A generic node the window flattens away is none of its children.
    index = 0;
    reply = Answer(
        Call("/org/a11y/atspi/accessible/w1n3", "org.a11y.atspi.Accessible", "GetIndexInParent"));
    CHECK(FirstOf(reply, mui_dbusTypeInt32, &index) && index == -1, "a flattened node's index, -1");
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
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
    // checkable 41, required 33.
    CHECK(HasState(ok, 11) && HasState(ok, 12) && HasState(ok, 8) && HasState(ok, 25) &&
              !HasState(ok, 33) && !HasState(agree, 12) && HasState(agree, 4) &&
              HasState(agree, 41) && HasState("/org/a11y/atspi/accessible/w1n7", 33),
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

// Whether a node holds a point in a coordinate type.
static bool HoldsIn(const char* path, int32_t x, int32_t y, uint32_t screen)
{
    DBusMessage* call = Call(path, "org.a11y.atspi.Component", "Contains");
    muiDBusIter iter;
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

// Whether a node holds a point on the screen.
static bool Holds(const char* path, int32_t x, int32_t y)
{
    return HoldsIn(path, x, y, 0);
}

// A Component method's two numbers, asked with a coordinate type or
// none.
static bool PairIs(const char* path, const char* member, int coordinates, int32_t a, int32_t b)
{
    DBusMessage* call = Call(path, "org.a11y.atspi.Component", member);
    muiDBusIter iter;
    uint32_t type = (uint32_t)coordinates;
    s_test.dbus.iterInitAppend(call, &iter);
    if (coordinates >= 0)
    {
        (void)s_test.dbus.appendBasic(&iter, mui_dbusTypeUint32, &type);
    }
    DBusMessage* reply = Answer(call);
    int32_t got[2] = {-1, -1};
    muiDBusIter out;
    muiDBusIter tuple;
    // Two ints, and nothing after them.
    bool pair = false;
    if (reply != NULL && s_test.dbus.iterInit(reply, &out) &&
        s_test.dbus.argType(&out) == mui_dbusTypeStruct)
    {
        s_test.dbus.recurse(&out, &tuple);
        for (int i = 0; i < 2 && s_test.dbus.argType(&tuple) == mui_dbusTypeInt32; i++)
        {
            s_test.dbus.getBasic(&tuple, &got[i]);
            (void)s_test.dbus.next(&tuple);
        }
        pair = s_test.dbus.argType(&tuple) == mui_dbusTypeInvalid;
    }
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    return pair && got[0] == a && got[1] == b;
}

// How a node is drawn, by the Component interface: a widget's layer, no
// MDI order, opaque; its size and position; points in its parent's
// coordinates; scrolling to it asked of the host; calls with no point or
// an unknown member refused.
static void TestComponentReads(void)
{
    const char* ok = "/org/a11y/atspi/accessible/w1n2";
    const char* input = "/org/a11y/atspi/accessible/w1n7";
    uint32_t layer = 0;
    int16_t order = 0;
    double alpha = 0.0;
    DBusMessage* reply = Answer(Call(ok, "org.a11y.atspi.Component", "GetLayer"));
    CHECK(FirstOf(reply, mui_dbusTypeUint32, &layer) && layer == 3, "a widget's layer");
    s_test.dbus.unrefMessage(reply);
    reply = Answer(Call(ok, "org.a11y.atspi.Component", "GetMDIZOrder"));
    CHECK(FirstOf(reply, mui_dbusTypeInt16, &order) && order == -1, "no MDI order");
    s_test.dbus.unrefMessage(reply);
    reply = Answer(Call(ok, "org.a11y.atspi.Component", "GetAlpha"));
    CHECK(FirstOf(reply, mui_dbusTypeDouble, &alpha) && alpha == 1.0, "opaque");
    s_test.dbus.unrefMessage(reply);
    CHECK(PairIs(ok, "GetSize", -1, 200, 80) && PairIs(ok, "GetPosition", 1, 20, 20) &&
              PairIs(input, "GetPosition", 2, 20, 20),
          "a size, and a position in the window and in the parent");
    CHECK(HoldsIn(input, 30, 30, 2) && !HoldsIn(input, 130, 30, 2),
          "points in the parent's coordinates");
    // A client's point at the edge of int32's range is held by no node,
    // its sum with the parent's origin never overflowing (found by
    // fuzz_atspi).
    CHECK(!HoldsIn(input, INT32_MAX - 1, 30, 2) && !HoldsIn(input, 30, INT32_MAX, 2),
          "points past the range, in the parent's coordinates");
    muiDBusBool done = 0;
    reply = Answer(Call(input, "org.a11y.atspi.Component", "ScrollTo"));
    CHECK(FirstOf(reply, mui_dbusTypeBoolean, &done) && done &&
              s_test.asked.action == mui_actionScrollIntoView && s_test.asked.target == 7,
          "scrolling to a node asked of the host");
    s_test.dbus.unrefMessage(reply);
    CHECK(IsError(Answer(Call(ok, "org.a11y.atspi.Component", "Contains")),
                  "org.freedesktop.DBus.Error.InvalidArgs") &&
              IsError(Answer(Call(ok, "org.a11y.atspi.Component", "Fold")),
                      "org.freedesktop.DBus.Error.UnknownMethod"),
          "no point, or an unknown member");
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
    CHECK(!Holds(ok, INT32_MIN + 1, 70) && !Holds(ok, 130, INT32_MIN),
          "points past the range on the screen, less the window's place");
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
        else if (type == mui_dbusTypeInt32 && s_flattenInts)
        {
            int32_t signed32 = 0;
            s_test.dbus.getBasic(iter, &signed32);
            *length += (size_t)snprintf(out + *length, size - *length, "%s%d",
                                        *length != 0 ? " " : "", (int)signed32);
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

// GetAll of an interface's properties, its strings and unsigned numbers
// flattened.
static bool AllAre(const char* path, const char* interface, const char* expected)
{
    DBusMessage* call = Call(path, "org.freedesktop.DBus.Properties", "GetAll");
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&interface);
    DBusMessage* reply = Answer(call);
    char got[512] = "";
    size_t length = 0;
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
        fprintf(stderr, "GetAll %s of %s: %s\n", interface, path, got);
    }
    return strcmp(got, expected) == 0;
}

// Get of a property, its error's name when it fails.
static bool GetFails(const char* path, const char* interface, const char* name, const char* error)
{
    DBusMessage* call = Call(path, "org.freedesktop.DBus.Properties", "Get");
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    (void)(s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&interface) &&
           s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&name));
    return IsError(Answer(call), error);
}

// The properties a screen reader reads beside a node's name: its
// description, the texts no node has, the application's versions, and
// GetAll over each interface; what an object lacks is refused.
static void TestProperties(void)
{
    const char* window = "/org/a11y/atspi/accessible/w1n1";
    const char* ok = "/org/a11y/atspi/accessible/w1n2";
    const char* agree = "/org/a11y/atspi/accessible/w1n1a";
    const char* slider = "/org/a11y/atspi/accessible/w1n8";
    const char* accessible = "org.a11y.atspi.Accessible";
    char text[256] = "";
    CHECK(PropertyOf("/org/a11y/atspi/accessible/w1n6", accessible, "Description",
                     mui_dbusTypeString, text) &&
              strcmp(text, "Settings") == 0,
          "a node's own description");
    // A node described by another tells it as a relation, not here.
    CHECK(PropertyOf(agree, accessible, "Description", mui_dbusTypeString, text) &&
              strcmp(text, "") == 0 &&
              PropertyOf(window, accessible, "Description", mui_dbusTypeString, text) &&
              strcmp(text, "") == 0,
          "none, empty");
    bool empty = true;
    const char* none[] = {"Locale", "AccessibleId", "HelpText"};
    for (int i = 0; i < 3; i++)
    {
        empty = empty && PropertyOf(ok, accessible, none[i], mui_dbusTypeString, text) &&
                strcmp(text, "") == 0;
    }
    CHECK(empty, "the locale, an id and help text, empty");
    char version[32];
    (void)snprintf(version, sizeof version, "%d.%d.%d", MUI_VERSION_MAJOR, MUI_VERSION_MINOR,
                   MUI_VERSION_PATCH);
    uint32_t interfaceVersion = 0;
    CHECK(PropertyOf(ROOT_PATH, "org.a11y.atspi.Application", "AtspiVersion", mui_dbusTypeString,
                     text) &&
              strcmp(text, "2.1") == 0 &&
              PropertyOf(ROOT_PATH, "org.a11y.atspi.Application", "Version", mui_dbusTypeString,
                         text) &&
              strcmp(text, version) == 0 &&
              PropertyOf(ROOT_PATH, "org.a11y.atspi.Application", "ToolkitVersion",
                         mui_dbusTypeString, text) &&
              strcmp(text, version) == 0 &&
              PropertyOf(ROOT_PATH, "org.a11y.atspi.Application", "InterfaceVersion",
                         mui_dbusTypeUint32, &interfaceVersion) &&
              interfaceVersion == 1,
          "the application's versions");
    char all[768];
    // Empty texts flatten to nothing between their spaces.
    (void)snprintf(all, sizeof all,
                   "Name OK Description  Parent %s %s ChildCount Locale  "
                   "AccessibleId  HelpText ",
                   s_test.plugName, window);
    CHECK(AllAre(ok, accessible, all), "every Accessible property of a node");
    (void)snprintf(all, sizeof all,
                   "ToolkitName Maul UI Version %s ToolkitVersion %s "
                   "AtspiVersion 2.1 InterfaceVersion 1 Id",
                   version, version);
    CHECK(AllAre(ROOT_PATH, "org.a11y.atspi.Application", all), "the application's");
    CHECK(AllAre(ok, "org.a11y.atspi.Action", "NActions") &&
              AllAre(slider, "org.a11y.atspi.Value",
                     "MinimumValue MaximumValue MinimumIncrement CurrentValue Text "),
          "an action's and a range's");
    CHECK(AllAre(ok, "org.a11y.atspi.Value", "") && AllAre(ok, "org.a11y.atspi.Nothing", ""),
          "none of an interface the node lacks, or of one unknown");
    CHECK(GetFails(ok, accessible, "Colour", "org.freedesktop.DBus.Error.UnknownProperty") &&
              GetFails(ok, "org.a11y.atspi.Value", "CurrentValue",
                       "org.freedesktop.DBus.Error.UnknownProperty") &&
              GetFails(ok, "org.a11y.atspi.Application", "ToolkitName",
                       "org.freedesktop.DBus.Error.UnknownProperty"),
          "an unknown property, a range's of a button, the application's of a node");
    CHECK(IsError(Answer(Call(ok, "org.freedesktop.DBus.Properties", "GetAll")),
                  "org.freedesktop.DBus.Error.InvalidArgs"),
          "GetAll without an interface refused");
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
                      "org.a11y.atspi.Accessible org.a11y.atspi.Component org.a11y.atspi.Text"),
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
    CHECK(ReplyIs(ok, "org.a11y.atspi.Accessible", "GetRoleName", "push button") &&
              ReplyIs(slider, "org.a11y.atspi.Accessible", "GetLocalizedRoleName", "slider"),
          "role names");
    const char* readers[] = {"GetLocalizedName", "GetDescription", "GetKeyBinding"};
    const char* expected[] = {"click", "", ""};
    bool read = true;
    for (int i = 0; i < 3; i++)
    {
        reply = Answer(Indexed(ok, readers[i], 0));
        const char* text = NULL;
        read = read && FirstOf(reply, mui_dbusTypeString, (void*)&text) &&
               strcmp(text, expected[i]) == 0;
        if (reply != NULL)
        {
            s_test.dbus.unrefMessage(reply);
        }
    }
    CHECK(read &&
              IsError(Answer(Indexed(ok, "GetDescription", 1)),
                      "org.freedesktop.DBus.Error.InvalidArgs") &&
              IsError(Answer(Indexed(ok, "Undo", 0)), "org.freedesktop.DBus.Error.UnknownMethod"),
          "an action's localized name, no description or key binding; none past the last; an "
          "unknown member");
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
    // States past the first 32 bits, indeterminate 32 and read only 43
    // (found by a mutant reading them from the wrong word).
    check.flags |= mui_accessMixed;
    CHECK(Send(adapter, (const muiAccessNode*[]){&check}, 1, built->children, 0) &&
              EventsAre("StateChanged indeterminate 1 w1n1a"),
          "mixed");
    check.flags &= ~(muiAccessFlags)mui_accessMixed;
    CHECK(Send(adapter, (const muiAccessNode*[]){&check}, 1, built->children, 0) &&
              EventsAre("StateChanged indeterminate 0 w1n1a"),
          "and not");
    slider.flags |= mui_accessReadOnly;
    CHECK(Send(adapter, (const muiAccessNode*[]){&slider}, 1, built->children, 0) &&
              EventsAre("StateChanged read-only 1 w1n8"),
          "made read only");
    slider.flags &= ~(muiAccessFlags)mui_accessReadOnly;
    CHECK(Send(adapter, (const muiAccessNode*[]){&slider}, 1, built->children, 0) &&
              EventsAre("StateChanged read-only 0 w1n8"),
          "and back");
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
              EventsAre("ChildrenChanged remove -1 root w1n1; ChildrenChanged add 0 root w1n14; "
                        "Activate  0 w1n14") &&
              muiAtspiAdapter_Apply(adapter, &lower) == mui_success &&
              EventsAre("ChildrenChanged remove -1 root w1n14; StateChanged defunct 1 w1n14; "
                        "ChildrenChanged add 0 root w1n1; Activate  0 w1n1"),
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

// A focused container's active descendant is the focus clients see:
// told focused in its place, with active-descendant-changed on the
// container; another one moves it, and none gives it back.
static void TestActiveDescendant(muiAtspiAdapter* adapter, Built* built)
{
    const char* group = "/org/a11y/atspi/accessible/w1n6";
    const muiAccessLink first[1] = {{7, mui_relationActiveDescendant}};
    const muiAccessLink second[1] = {{9, mui_relationActiveDescendant}};
    muiAccessNode box = built->nodes[5];
    const muiAccessNode* sent[1] = {&box};
    box.links = first;
    box.linkCount = 1;
    CHECK(Send(adapter, sent, 1, built->children, 6) &&
              EventsAre("StateChanged focused 0 w1n2; StateChanged focused 1 w1n7; "
                        "ActiveDescendantChanged  0 w1n6 w1n7") &&
              HasState("/org/a11y/atspi/accessible/w1n7", 12) && !HasState(group, 12),
          "the active descendant focused in its container's place");
    box.links = second;
    CHECK(Send(adapter, sent, 1, built->children, 6) &&
              EventsAre("StateChanged focused 0 w1n7; StateChanged focused 1 w1n9; "
                        "ActiveDescendantChanged  0 w1n6 w1n9"),
          "another active descendant");
    box.linkCount = 0;
    CHECK(Send(adapter, sent, 1, built->children, 6) &&
              EventsAre("StateChanged focused 0 w1n9; StateChanged focused 1 w1n6") &&
              HasState(group, 12),
          "none: the container focused");
    sent[0] = &built->nodes[5];
    CHECK(Send(adapter, sent, 1, built->children, 2) &&
              EventsAre("StateChanged focused 0 w1n6; StateChanged focused 1 w1n2"),
          "the focus back where it was");
}

// A Text method's reply, its integers too, flattened; its arguments
// integers of the types named, 'i' or 'u'.
static bool TextIs(const char* member, const char* types, const int32_t* args, const char* expected)
{
    DBusMessage* call = Call("/org/a11y/atspi/accessible/w1n7", "org.a11y.atspi.Text", member);
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    for (size_t i = 0; types[i] != '\0'; i++)
    {
        int type = types[i] == 'i' ? mui_dbusTypeInt32 : mui_dbusTypeUint32;
        (void)s_test.dbus.appendBasic(&iter, type, &args[i]);
    }
    DBusMessage* reply = Answer(call);
    char got[512] = "";
    size_t length = 0;
    s_flattenInts = true;
    if (reply != NULL && s_test.dbus.iterInit(reply, &iter))
    {
        Flatten(&iter, got, sizeof(got), &length);
    }
    s_flattenInts = false;
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    if (strcmp(got, expected) != 0)
    {
        fprintf(stderr, "%s: %s\n", member, got);
    }
    return strcmp(got, expected) == 0;
}

// The text input 7 given "héllo wörld\nnext", the caret after h: two
// lines, three words. Characters, words, lines, paragraphs; the older
// boundary methods; no sentences.
static void TestTextReads(muiAtspiAdapter* adapter, Built* built, muiAccessNode* input)
{
    static const char s_value[] = "h\xC3\xA9llo w\xC3\xB6rld\nnext";
    static const uint32_t s_lines[2] = {0, 14};
    static const muiAccessWord s_words[3] = {{0, 6}, {7, 13}, {14, 18}};
    input->text[mui_accessValue] = s_value;
    input->textLength[mui_accessValue] = sizeof s_value - 1;
    input->marks = (muiAccessTextMarks){1, 1, true, s_lines, 2, s_words, 3};
    CHECK(Send(adapter, (const muiAccessNode*[]){input}, 1, built->children, 0) &&
              EventsAre("TextChanged insert 0/16 w1n7 h\xC3\xA9llo w\xC3\xB6rld\nnext; "
                        "TextCaretMoved  1 w1n7"),
          "the text inserted, the caret placed");
    int32_t count = 0;
    int32_t caret = 0;
    CHECK(PropertyOf("/org/a11y/atspi/accessible/w1n7", "org.a11y.atspi.Text", "CharacterCount",
                     mui_dbusTypeInt32, &count) &&
              count == 16 &&
              PropertyOf("/org/a11y/atspi/accessible/w1n7", "org.a11y.atspi.Text", "CaretOffset",
                         mui_dbusTypeInt32, &caret) &&
              caret == 1,
          "sixteen characters, the caret at 1");
    CHECK(TextIs("GetText", "ii", (const int32_t[]){0, -1}, s_value) &&
              TextIs("GetText", "ii", (const int32_t[]){1, 5}, "\xC3\xA9llo") &&
              TextIs("GetText", "ii", (const int32_t[]){5, 2}, "") &&
              TextIs("GetCharacterAtOffset", "i", (const int32_t[]){1}, "233") &&
              TextIs("GetCharacterAtOffset", "i", (const int32_t[]){16}, "0"),
          "text by character");
    CHECK(TextIs("GetStringAtOffset", "iu", (const int32_t[]){0, 0}, "h 0 1") &&
              TextIs("GetStringAtOffset", "iu", (const int32_t[]){2, 1}, "h\xC3\xA9llo  0 6") &&
              TextIs("GetStringAtOffset", "iu", (const int32_t[]){5, 1}, "h\xC3\xA9llo  0 6") &&
              TextIs("GetStringAtOffset", "iu", (const int32_t[]){13, 3}, "next 12 16") &&
              TextIs("GetStringAtOffset", "iu", (const int32_t[]){3, 3},
                     "h\xC3\xA9llo w\xC3\xB6rld\n 0 12") &&
              TextIs("GetStringAtOffset", "iu", (const int32_t[]){12, 4}, "next 12 16") &&
              TextIs("GetStringAtOffset", "iu", (const int32_t[]){3, 2}, "0 0"),
          "characters, words, lines and paragraphs; no sentences");
    CHECK(TextIs("GetTextAtOffset", "iu", (const int32_t[]){7, 1}, "w\xC3\xB6rld\n 6 12") &&
              TextIs("GetTextBeforeOffset", "iu", (const int32_t[]){7, 1}, "h\xC3\xA9llo  0 6") &&
              TextIs("GetTextAfterOffset", "iu", (const int32_t[]){7, 1}, "next 12 16") &&
              TextIs("GetTextBeforeOffset", "iu", (const int32_t[]){2, 1}, "0 0") &&
              TextIs("GetTextAfterOffset", "iu", (const int32_t[]){13, 5}, "16 16") &&
              TextIs("GetTextAtOffset", "iu", (const int32_t[]){7, 2}, "0 0"),
          "the older boundaries: word and line starts");
}

// Selecting, editing and the rest of the interface: selections; text
// changed as deleted and inserted with the caret after; attributes and
// geometry not given yet, setting the selection not offered.
static void TestTextChanges(muiAtspiAdapter* adapter, Built* built, muiAccessNode* input)
{
    input->marks.anchor = 7;
    input->marks.focus = 13;
    CHECK(Send(adapter, (const muiAccessNode*[]){input}, 1, built->children, 0) &&
              EventsAre("TextCaretMoved  11 w1n7; TextSelectionChanged  0 w1n7") &&
              TextIs("GetNSelections", "", NULL, "1") &&
              TextIs("GetSelection", "i", (const int32_t[]){0}, "6 11") &&
              TextIs("GetSelection", "i", (const int32_t[]){1}, "0 0"),
          "a word selected");
    static const char s_edited[] = "h\xC3\xA9llo w\xC3\xB6rld!";
    input->text[mui_accessValue] = s_edited;
    input->textLength[mui_accessValue] = sizeof s_edited - 1;
    input->marks = (muiAccessTextMarks){14, 14, true, NULL, 0, NULL, 0};
    CHECK(Send(adapter, (const muiAccessNode*[]){input}, 1, built->children, 0) &&
              EventsAre("TextChanged delete 11/5 w1n7 \nnext; TextChanged insert 11/1 w1n7 !; "
                        "TextCaretMoved  12 w1n7; TextSelectionChanged  0 w1n7") &&
              TextIs("GetNSelections", "", NULL, "0") &&
              TextIs("GetStringAtOffset", "iu", (const int32_t[]){3, 3},
                     "h\xC3\xA9llo w\xC3\xB6rld! 0 12") &&
              TextIs("GetStringAtOffset", "iu", (const int32_t[]){3, 1}, "3 3"),
          "an edit, then one line and no words given");
    CHECK(TextIs("SetCaretOffset", "i", (const int32_t[]){2}, "") &&
              TextIs("GetCharacterExtents", "iu", (const int32_t[]){2, 0}, "0 0 0 0") &&
              TextIs("GetOffsetAtPoint", "iiu", (const int32_t[]){5, 5, 0}, "-1") &&
              TextIs("GetAttributes", "i", (const int32_t[]){2}, "0 12") &&
              TextIs("GetAttributeValue", "i", (const int32_t[]){2}, ""),
          "geometry and attributes not given, the caret not set");
    CHECK(IsError(Answer(Call("/org/a11y/atspi/accessible/w1n7", "org.a11y.atspi.Text", "Nothing")),
                  "org.freedesktop.DBus.Error.UnknownMethod") &&
              IsError(
                  Answer(Call("/org/a11y/atspi/accessible/w1n2", "org.a11y.atspi.Text", "GetText")),
                  "org.freedesktop.DBus.Error.UnknownMethod"),
          "no such method; a button has no text");
    *input = built->nodes[6];
    CHECK(Send(adapter, (const muiAccessNode*[]){input}, 1, built->children, 0) &&
              EventsAre("TextChanged delete 0/12 w1n7 h\xC3\xA9llo w\xC3\xB6rld!"),
          "emptied, no caret");
}

// Whether the host was last asked to select or replace, at bytes, with
// text; for the action 0, whether it was asked nothing.
static bool AskedText(muiAccessAction action, uint32_t anchor, uint32_t focus, const char* text)
{
    bool same = s_test.asked.action == action && s_test.asked.target == (action != 0 ? 7u : 0u) &&
                s_test.asked.anchor == anchor && s_test.asked.focus == focus &&
                strcmp(s_test.askedText, text) == 0;
    if (!same)
    {
        fprintf(stderr, "asked %d %u-%u '%s'\n", (int)s_test.asked.action,
                (unsigned)s_test.asked.anchor, (unsigned)s_test.asked.focus, s_test.askedText);
    }
    s_test.asked = (muiAccessRequest){0};
    s_test.askedText[0] = '\0';
    return same;
}

static bool Edited(const char* member, const char* types, const int32_t* ints, const char* text,
                   const char* expected)
{
    DBusMessage* call =
        Call("/org/a11y/atspi/accessible/w1n7", "org.a11y.atspi.EditableText", member);
    muiDBusIter iter;
    s_test.dbus.iterInitAppend(call, &iter);
    for (size_t i = 0; types[i] != '\0'; i++)
    {
        (void)(types[i] == 's'
                   ? s_test.dbus.appendBasic(&iter, mui_dbusTypeString, (const void*)&text)
                   : s_test.dbus.appendBasic(&iter, mui_dbusTypeInt32, ints++));
    }
    DBusMessage* reply = Answer(call);
    muiDBusBool done = 0;
    bool got = reply != NULL && s_test.dbus.messageType(reply) == mui_dbusMethodReturn;
    char answer[8] = "none";
    if (got && FirstOf(reply, mui_dbusTypeBoolean, &done))
    {
        (void)snprintf(answer, sizeof answer, "%s", done ? "true" : "false");
    }
    if (reply != NULL)
    {
        s_test.dbus.unrefMessage(reply);
    }
    if (!got || strcmp(answer, expected) != 0)
    {
        fprintf(stderr, "%s: %s\n", member, got ? answer : "an error");
    }
    return got && strcmp(answer, expected) == 0;
}

// The caret and selection set and the text replaced, asked of the host
// in bytes; EditableText only where the text may be replaced.
static void TestTextRequests(muiAtspiAdapter* adapter, Built* built, muiAccessNode* input)
{
    static const char s_value[] = "h\xC3\xA9llo";
    input->text[mui_accessValue] = s_value;
    input->textLength[mui_accessValue] = sizeof s_value - 1;
    input->marks = (muiAccessTextMarks){.anchor = 1, .focus = 1, .selected = true};
    input->actions |= 1u << mui_actionSetSelection | 1u << mui_actionReplaceText;
    CHECK(Send(adapter, (const muiAccessNode*[]){input}, 1, built->children, 0) &&
              ReplyIs("/org/a11y/atspi/accessible/w1n7", "org.a11y.atspi.Accessible",
                      "GetInterfaces",
                      "org.a11y.atspi.Accessible org.a11y.atspi.Component "
                      "org.a11y.atspi.Text org.a11y.atspi.EditableText"),
          "editable text");
    (void)EventsAre("TextChanged insert 0/5 w1n7 h\xC3\xA9llo; TextCaretMoved  1 w1n7");
    CHECK(TextIs("SetCaretOffset", "i", (const int32_t[]){2}, "") &&
              AskedText(mui_actionSetSelection, 3, 3, "") &&
              TextIs("SetSelection", "iii", (const int32_t[]){0, 4, 1}, "") &&
              AskedText(mui_actionSetSelection, 5, 1, "") &&
              TextIs("SetSelection", "iii", (const int32_t[]){1, 0, 2}, "") &&
              AskedText(0, 0, 0, "") && TextIs("AddSelection", "ii", (const int32_t[]){0, 9}, "") &&
              AskedText(mui_actionSetSelection, 0, 6, "") &&
              TextIs("RemoveSelection", "i", (const int32_t[]){0}, "") && AskedText(0, 0, 0, ""),
          "the caret and a selection asked in bytes, none to remove");
    input->marks.anchor = 3;
    CHECK(Send(adapter, (const muiAccessNode*[]){input}, 1, built->children, 0) &&
              TextIs("AddSelection", "ii", (const int32_t[]){0, 1}, "") && AskedText(0, 0, 0, "") &&
              TextIs("RemoveSelection", "i", (const int32_t[]){0}, "") &&
              AskedText(mui_actionSetSelection, 1, 1, ""),
          "one selection: none added, removed to the caret");
    (void)EventsAre("TextSelectionChanged  0 w1n7");
    CHECK(Edited("SetTextContents", "s", NULL, "abc", "true") &&
              AskedText(mui_actionReplaceText, 0, 6, "abc") &&
              Edited("InsertText", "isi", (const int32_t[]){2, 2}, "x\xC3\xA9z", "true") &&
              AskedText(mui_actionReplaceText, 3, 3, "x") &&
              Edited("InsertText", "isi", (const int32_t[]){9, -1}, "yz", "true") &&
              AskedText(mui_actionReplaceText, 6, 6, "yz") &&
              Edited("DeleteText", "ii", (const int32_t[]){1, 3}, NULL, "true") &&
              AskedText(mui_actionReplaceText, 1, 4, "") &&
              Edited("DeleteText", "ii", (const int32_t[]){4, -1}, NULL, "true") &&
              AskedText(mui_actionReplaceText, 5, 6, ""),
          "text set, inserted and deleted, asked in bytes");
    s_test.refuse = true;
    CHECK(Edited("SetTextContents", "s", NULL, "abc", "false") &&
              TextIs("SetCaretOffset", "i", (const int32_t[]){2}, "") &&
              Edited("CutText", "ii", (const int32_t[]){0, 1}, NULL, "false") &&
              Edited("PasteText", "i", (const int32_t[]){0}, NULL, "false") &&
              Edited("CopyText", "ii", (const int32_t[]){0, 1}, NULL, "none") &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1n7", "org.a11y.atspi.EditableText",
                                  "Nothing")),
                      "org.freedesktop.DBus.Error.UnknownMethod"),
          "refused; the clipboard not offered");
    s_test.refuse = false;
    s_test.asked = (muiAccessRequest){0};
    input->actions &= ~(1u << mui_actionReplaceText | 1u << mui_actionSetSelection);
    CHECK(Send(adapter, (const muiAccessNode*[]){input}, 1, built->children, 0) &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1n7", "org.a11y.atspi.EditableText",
                                  "DeleteText")),
                      "org.freedesktop.DBus.Error.UnknownMethod") &&
              TextIs("SetCaretOffset", "i", (const int32_t[]){2}, "") && AskedText(0, 0, 0, ""),
          "not editable: no EditableText, the caret not asked");
    *input = built->nodes[6];
    CHECK(Send(adapter, (const muiAccessNode*[]){input}, 1, built->children, 0), "back");
    (void)EventsAre("TextChanged delete 0/5 w1n7 h\xC3\xA9llo");
}

static void TestText(muiAtspiAdapter* adapter, Built* built)
{
    muiAccessNode input = built->nodes[6];
    TestTextReads(adapter, built, &input);
    TestTextChanges(adapter, built, &input);
    TestTextRequests(adapter, built, &input);
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
              EventsAre("ChildrenChanged remove -1 w1n1 w1n2; StateChanged defunct 1 w1n2") &&
              IsError(Answer(Call("/org/a11y/atspi/accessible/w1n2", "org.a11y.atspi.Accessible",
                                  "GetRole")),
                      "org.freedesktop.DBus.Error.UnknownObject") &&
              ChildrenAre("/org/a11y/atspi/accessible/w1n1", "w1n4 w1n1a w1n6 w1n8"),
          "a node removed");
    muiAtspiAdapterDef def = muiDefaultAtspiAdapterDef();
    def.action = Act;
    muiAtspiAdapter* second = NULL;
    muiAtspiAdapter* third = NULL;
    // A window whose root is a group with no name, focused itself, and a
    // switch in it.
    const uint64_t aloneChildren[1] = {10};
    muiAccessNode alone = {.id = 9,
                           .role = mui_roleGroup,
                           .bounds = {0.0f, 0.0f, 50.0f, 50.0f},
                           .transform = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
                           .childCount = 1};
    muiAccessNode toggle = {.id = 10,
                            .role = mui_roleSwitch,
                            .flags = mui_accessFocusable | mui_accessCheckable,
                            .bounds = {0.0f, 0.0f, 10.0f, 10.0f},
                            .transform = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}};
    const muiAccessNode* aloneSent[2] = {&alone, &toggle};
    const muiAccessUpdate aloneUpdate = {aloneSent, 2, aloneChildren, 9, 9};
    CHECK(muiCreateAtspiAdapter(s_test.app, &def, &second) == mui_success &&
              ChildrenAre(ROOT_PATH, "w1n1"),
          "a window with no tree is no child");
    CHECK(muiCreateAtspiAdapter(s_test.app, &def, &third) == mui_success &&
              muiAtspiAdapter_Apply(third, &aloneUpdate) == mui_success &&
              EventsAre("ChildrenChanged add 1 root w3n9; Activate  0 w3n9") &&
              ChildrenAre(ROOT_PATH, "w1n1 w3n9"),
          "a third window, after the second with no tree, active");
    // Its root is the window: a frame, titled by the application, active
    // and, unable to take focus, not focused; the switch a toggle button
    // (AT-SPI 2.56's switch role is past what older clients know).
    CHECK(RoleOf("/org/a11y/atspi/accessible/w3n9") == 23, "a window's root a frame");
    CHECK(NameIs("/org/a11y/atspi/accessible/w3n9", "maul test"), "titled by the application");
    CHECK(HasState("/org/a11y/atspi/accessible/w3n9", 1) &&
              !HasState("/org/a11y/atspi/accessible/w3n9", 12),
          "active, not focused");
    CHECK(ChildrenAre("/org/a11y/atspi/accessible/w3n9", "w3na") &&
              RoleOf("/org/a11y/atspi/accessible/w3na") == 62,
          "the switch a toggle button");
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

// Whether a state's bit is set in a state set's words.
static bool Bit(const uint32_t words[2], uint32_t state)
{
    return (words[state / 32] & (1u << (state % 32))) != 0;
}

// A state set from a node's flags: collapsed only when expandable and
// not expanded, multiselectable when the flag says so (found by mutants
// inverting each). Collapsed is 5, expandable 9, expanded 10 and
// multiselectable 18.
static void TestStateSets(void)
{
    uint32_t words[2];
    muiAccessNode node = {.id = 1, .role = mui_roleList};
    muiAtspiRecordStatesOf(&node, words);
    CHECK(!Bit(words, 5) && !Bit(words, 9) && !Bit(words, 10) && !Bit(words, 18), "none of them");
    node.flags = mui_accessExpandable;
    muiAtspiRecordStatesOf(&node, words);
    CHECK(Bit(words, 5) && Bit(words, 9) && !Bit(words, 10), "expandable, collapsed");
    node.flags = mui_accessExpandable | mui_accessExpanded;
    muiAtspiRecordStatesOf(&node, words);
    CHECK(!Bit(words, 5) && Bit(words, 9) && Bit(words, 10), "expanded");
    node.flags = mui_accessMultiselectable;
    muiAtspiRecordStatesOf(&node, words);
    CHECK(Bit(words, 18), "multiselectable");
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

// Allocations that fail after a number succeed, given in context.
static void* Failing(size_t size, size_t alignment, void* context)
{
    int* left = context;
    if (*left <= 0)
    {
        return NULL;
    }
    (*left)--;
    return Poisoned(size, alignment, NULL);
}

// What creating an application or a window meets: no memory, no bus at
// the address named, a window more than the application holds, and an
// application destroyed while its registry has not answered.
static void TestLimits(const char* address)
{
    muiAtspiAppDef def = muiDefaultAtspiAppDef();
    def.name = "limits";
    int left = 0;
    def.allocator = (muiAllocator){Failing, Unpoisoned, &left};
    muiAtspiApp* app = NULL;
    CHECK(muiCreateAtspiApp(&def, &app) == mui_errorCapacity && app == NULL,
          "no memory for the application");
    def.allocator = (muiAllocator){Poisoned, Unpoisoned, NULL};
    (void)setenv("AT_SPI_BUS_ADDRESS", "unix:path=/nonexistent/maul-ui-bus", 1);
    CHECK(muiCreateAtspiApp(&def, &app) == mui_errorPlatform && app == NULL,
          "no bus at the address named");
    (void)setenv("AT_SPI_BUS_ADDRESS", address, 1);
    def.windows = 1;
    left = 1;
    def.allocator = (muiAllocator){Failing, Unpoisoned, &left};
    CHECK(muiCreateAtspiApp(&def, &app) == mui_success, "an application of one window");
    muiAtspiAdapterDef adapterDef = muiDefaultAtspiAdapterDef();
    adapterDef.action = Act;
    muiAtspiAdapter* first = NULL;
    muiAtspiAdapter* second = NULL;
    CHECK(muiCreateAtspiAdapter(app, &adapterDef, &first) == mui_errorCapacity && first == NULL,
          "no memory for a window");
    left = 1;
    CHECK(muiCreateAtspiAdapter(app, &adapterDef, &first) == mui_errorCapacity && first == NULL,
          "no memory for its tree");
    left = 1000;
    CHECK(muiCreateAtspiAdapter(app, &adapterDef, &first) == mui_success &&
              muiCreateAtspiAdapter(app, &adapterDef, &second) == mui_errorCapacity &&
              second == NULL,
          "one window, none more");
    muiDestroyAtspiAdapter(first);
    // Its Embed not answered yet: destroying cancels the call.
    muiDestroyAtspiApp(app);
    // A desktop path sent as a string is not taken: only what libdbus
    // checked as an object path is ever sent as one (found by
    // fuzz_atspi, libdbus aborting on a path it does not allow).
    def = muiDefaultAtspiAppDef();
    def.name = "Odd";
    muiAtspiApp* odd = NULL;
    muiAtspiApp* plain = NULL;
    s_test.pathAsString = true;
    CHECK(muiCreateAtspiApp(&def, &odd) == mui_success, "an application");
    for (int i = 0; i < 200; i++)
    {
        muiAtspiApp_Pump(odd);
        PumpBoth();
        Wait();
    }
    s_test.pathAsString = false;
    CHECK(!muiAtspiApp_IsRegistered(odd), "a path as a string: not embedded");
    CHECK(muiCreateAtspiApp(&def, &plain) == mui_success, "another");
    for (int i = 0; i < 200; i++)
    {
        muiAtspiApp_Pump(plain);
        PumpBoth();
        Wait();
    }
    CHECK(muiAtspiApp_IsRegistered(plain), "an object path: embedded, as long a wait");
    muiDestroyAtspiApp(plain);
    muiDestroyAtspiApp(odd);
}

// The session bus's org.a11y.Bus, served by a child process while an
// application blocks asking it: GetAddress answers the test's bus. A
// process, not a thread, so that libdbus's locks are never shared.
static muiDBusHandled AnswerAddress(DBusConnection* connection, DBusMessage* message, void* data)
{
    int* asked = data;
    const muiDBusApi* dbus = &s_test.dbus;
    const char* member = dbus->member(message);
    if (dbus->messageType(message) != mui_dbusMethodCall || member == NULL ||
        strcmp(member, "GetAddress") != 0)
    {
        return mui_dbusNotHandled;
    }
    DBusMessage* reply = dbus->newMethodReturn(message);
    muiDBusIter out;
    const char* address = getenv("DBUS_SESSION_BUS_ADDRESS");
    dbus->iterInitAppend(reply, &out);
    (void)dbus->appendBasic(&out, mui_dbusTypeString, (const void*)&address);
    (void)dbus->send(connection, reply, NULL);
    dbus->unrefMessage(reply);
    (*asked)++;
    return mui_dbusHandled;
}

// The child: owns the name, tells the parent through a pipe, answers
// until asked once or ten seconds pass; its exit status how many asked.
static void ServeAddress(int ready)
{
    const muiDBusApi* dbus = &s_test.dbus;
    int asked = 0;
    DBusConnection* connection = dbus->openPrivate(getenv("DBUS_SESSION_BUS_ADDRESS"), NULL);
    bool ok = connection != NULL && dbus->busRegister(connection, NULL) &&
              dbus->requestName(connection, "org.a11y.Bus", 4, NULL) == 1 &&
              dbus->addFilter(connection, AnswerAddress, &asked, NULL);
    if (write(ready, ok ? "1" : "0", 1) != 1)
    {
        ok = false;
    }
    for (int i = 0; ok && i < 10000 && asked == 0; i++)
    {
        (void)dbus->readWrite(connection, 1);
        while (dbus->dispatch(connection) == mui_dbusDataRemains)
        {
        }
    }
    // The answer written before the process ends.
    (void)dbus->readWrite(connection, 100);
    _exit(asked);
}

// With no AT_SPI_BUS_ADDRESS, as on a desktop, an application asks the
// session bus's org.a11y.Bus for the accessibility bus, then registers.
static void TestDiscovery(const char* address)
{
    (void)unsetenv("AT_SPI_BUS_ADDRESS");
    (void)setenv("DBUS_SESSION_BUS_ADDRESS", address, 1);
    int pipes[2];
    CHECK(pipe(pipes) == 0, "a pipe");
    pid_t child = fork();
    if (child == 0)
    {
        ServeAddress(pipes[1]);
    }
    // The parent's end closed first, so that a child that dies before it
    // writes ends the read rather than leaving it waiting.
    (void)close(pipes[1]);
    char ready = '0';
    CHECK(child > 0 && read(pipes[0], &ready, 1) == 1 && ready == '1', "a session bus service");
    (void)close(pipes[0]);
    muiAtspiAppDef def = muiDefaultAtspiAppDef();
    def.name = "discovered";
    muiAtspiApp* found = NULL;
    CHECK(muiCreateAtspiApp(&def, &found) == mui_success, "an application finding its bus");
    for (int i = 0; i < 5000 && found != NULL && !muiAtspiApp_IsRegistered(found); i++)
    {
        muiAtspiApp_Pump(found);
        PumpBoth();
        Wait();
    }
    int status = 0;
    CHECK(found != NULL && muiAtspiApp_IsRegistered(found) && child > 0 &&
              waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 1,
          "the session bus asked once, the application registered on what it gave");
    muiDestroyAtspiApp(found);
    (void)setenv("AT_SPI_BUS_ADDRESS", address, 1);
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
    s_test.dbus.addMatch(s_test.registry, "type='signal',interface='org.a11y.atspi.Event.Window'",
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
    CHECK(
        EventsAre("ChildrenChanged add 0 root w1n1; Activate  0 w1n1; StateChanged focused 1 w1n2"),
        "a window's root added to the application's and active, its nodes not told; the focus");
    TestContract();
    TestStateSets();
    TestRoot();
    TestNodes();
    TestProperties();
    TestComponent(adapter);
    TestComponentReads();
    TestActionsAndValues();
    TestEvents(adapter, &s_built);
    TestActiveDescendant(adapter, &s_built);
    TestText(adapter, &s_built);
    TestGone(adapter, &s_built);
    TestDiscovery(address);
    TestLimits(address);
    muiDestroyAtspiApp(s_test.app);
    s_test.dbus.close(s_test.registry);
    s_test.dbus.unrefConnection(s_test.registry);
    (void)kill(s_test.daemon, SIGTERM);
    return s_failures == 0 ? 0 : 1;
}
