// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility through the Maul Window glue on Maul Window's headless
// test backend, whose windows have no platform: the access keeps the
// tree alone. A root holding a button named Play: an update sends both,
// a rename sends it again, the scale and a move are taken, and the
// access destroyed stops the root's updates, so another can be made.
// Defs outside the contract are refused.

#include "test_harness.h"

#include "maul-ui-window/access.h"
#include "maul-ui-window/glue.h"
#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-window/context.h"
#include "maul-window/test.h"
#include "maul-window/window.h"

#include <string.h>

static const muiNodeId s_nullNode = {0, 0};

typedef struct Test
{
    muiContext* context;
    muiNodeId root;
    muiNodeId button;
    mwinWindowId window;
    muiWindowGlue* glue;
    bool done;
} Test;

static void Name(Test* test, const char* name)
{
    CHECK(muiNode_SetAccessText(test->context, test->button, mui_accessLabel, name, strlen(name)) ==
              mui_success,
          "the button's name");
}

static void MakeTree(Test* test)
{
    muiContextDef def = muiDefaultContextDef();
    CHECK(muiCreateContext(&def, &test->context) == mui_success, "context");
    muiNodeDef node = muiDefaultNodeDef();
    CHECK(muiCreateNode(test->context, &node, &test->root) == mui_success &&
              muiCreateNode(test->context, &node, &test->button) == mui_success &&
              muiNode_InsertChild(test->context, test->root, test->button, s_nullNode) ==
                  mui_success,
          "a root and a button");
    CHECK(muiNode_SetAccessRole(test->context, test->button, mui_roleButton) == mui_success,
          "a button's role");
    Name(test, "Play");
}

static void Layout(Test* test)
{
    const muiLayoutInput input = {640.0f, 480.0f, NULL, NULL, 0, NULL};
    CHECK(muiComputeLayout(test->context, test->root, &input) == mui_success, "layout");
}

static bool NameIs(const muiWindowAccess* access, muiNodeId node, const char* expected)
{
    char name[16] = {0};
    size_t length = 0;
    return muiAccessTree_GetName(muiWindowAccess_GetTree(access), muiAccessIdOf(node), name,
                                 sizeof name, &length) == mui_success &&
           length == strlen(expected) && memcmp(name, expected, length) == 0;
}

static muiWindowAccess* Make(Test* test)
{
    muiWindowAccessDef def = muiDefaultWindowAccessDef();
    def.glue = test->glue;
    muiWindowAccess* access = NULL;
    CHECK(muiCreateWindowAccess(&def, &access) == mui_success && access != NULL, "an access");
    return access;
}

static void TestTree(Test* test)
{
    muiWindowAccess* access = Make(test);
    CHECK(!muiWindowAccess_HasAdapter(access), "no platform: the tree alone");
    const muiAccessTree* tree = muiWindowAccess_GetTree(access);
    CHECK(tree != NULL && muiAccessTree_Count(tree) == 0, "empty before an update");
    Layout(test);
    CHECK(muiWindowAccess_Update(access) == mui_success && muiAccessTree_Count(tree) == 2 &&
              muiAccessTree_GetRoot(tree) == muiAccessIdOf(test->root) &&
              NameIs(access, test->button, "Play"),
          "an update sends the root and the button");
    Name(test, "Stop");
    Layout(test);
    CHECK(muiWindowAccess_Update(access) == mui_success && NameIs(access, test->button, "Stop"),
          "a rename sent again");
    mwinEvent scale = {.type = mwin_eventScaleChanged, .window = test->window};
    scale.data.scale.scale = 2.0f;
    const mwinEvent moved = {.type = mwin_eventMoved, .window = test->window};
    CHECK(muiWindowAccess_HandleEvent(access, &scale) == mui_success &&
              muiWindowAccess_HandleEvent(access, &moved) == mui_success,
          "a scale and a move taken");
    CHECK(muiWindowAccess_HandleEvent(access, NULL) == mui_errorInvalid &&
              muiWindowAccess_HandleEvent(NULL, &moved) == mui_errorInvalid &&
              muiWindowAccess_Update(NULL) == mui_errorInvalid,
          "NULL refused");
    muiDestroyWindowAccess(access);
    muiAccessUpdate update;
    CHECK(muiBuildAccessUpdate(test->context, test->root, &update) == mui_empty,
          "destroyed, the root's updates stop");
    access = Make(test);
    Layout(test);
    CHECK(muiWindowAccess_Update(access) == mui_success &&
              muiAccessTree_Count(muiWindowAccess_GetTree(access)) == 2,
          "another access sends the whole tree");
    muiDestroyWindowAccess(access);
    muiDestroyWindowAccess(NULL);
}

static void TestRefused(Test* test)
{
    muiWindowAccessDef def = muiDefaultWindowAccessDef();
    // The web's adapter refuses no label: the default carries its own.
    CHECK(def.ariaDeferred && def.ariaEnableLabel != NULL &&
              strcmp(def.ariaEnableLabel, "Enable accessibility") == 0,
          "the default as the web adapter's");
    muiWindowAccess* access = (muiWindowAccess*)&def;
    CHECK(muiCreateWindowAccess(&def, &access) == mui_errorInvalid && access == NULL,
          "no glue, the output cleared");
    def.glue = test->glue;
    def.nodes = 0;
    CHECK(muiCreateWindowAccess(&def, &access) == mui_errorInvalid, "no nodes");
    def = muiDefaultWindowAccessDef();
    def.glue = test->glue;
    def.cookie = 0;
    CHECK(muiCreateWindowAccess(&def, &access) == mui_errorInvalid, "no cookie");
    CHECK(muiCreateWindowAccess(NULL, &access) == mui_errorInvalid &&
              muiWindowAccess_GetTree(NULL) == NULL && !muiWindowAccess_HasAdapter(NULL),
          "NULL refused");
}

static mwinResult Init(mwinContext* windows, void* user)
{
    Test* test = user;
    mwinWindowDef window = mwinDefaultWindowDef();
    window.size = (mwinSize){640.0f, 480.0f};
    CHECK(mwinCreateWindow(windows, &window, &test->window, NULL) == mwin_success, "a window");
    MakeTree(test);
    muiWindowGlueDef def = muiDefaultWindowGlueDef();
    def.windows = windows;
    def.window = test->window;
    def.context = test->context;
    def.root = test->root;
    CHECK(muiCreateWindowGlue(&def, &test->glue) == mui_success, "a glue");
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* windows, void* user)
{
    (void)windows;
    Test* test = user;
    TestTree(test);
    TestRefused(test);
    test->done = true;
    return mwin_frameStop;
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
    muiDestroyContext(test.context);
    return s_failures == 0 ? 0 : 1;
}
