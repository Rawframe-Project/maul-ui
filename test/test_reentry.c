// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code calling back into the context it serves (Maul conventions,
// Threads): from the measure, baseline, paint and access text functions and
// the allocator, every function that changes the context answers
// mui_errorBusy and counts one misuse, its destroy among them. The event
// function may call them all but the ones that feed input, dispatch,
// perform an access action, set the event function or destroy the context,
// and each call it makes is busy in turn.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
#include "maul-ui/exit.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/popup.h"
#include "maul-ui/range.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/text_style.h"
#include "maul-ui/theme.h"
#include "maul-ui/token.h"
#include "maul-ui/transition.h"
#include "maul-ui/virtual.h"
#include "maul-ui/visual.h"

#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const muiNodeId s_null = {0, 0};

enum
{
    // The functions that change a context, its destroy aside.
    CALLS = 91,
};

// Calls every function that changes a context, each with arguments it
// refuses on their own, so a state error is the busy refusal alone; the
// results in the order of the public headers.
static int CallAll(muiContext* context, muiResult results[CALLS])
{
    int count = 0;
    results[count++] = muiSetAccessTextFunction(context, NULL, NULL);
    results[count++] = muiNode_MarkAccessChanged(context, (muiNodeId){0});
    results[count++] = muiNode_EnableAccess(context, (muiNodeId){0}, true);
    results[count++] = muiNode_EnableAccess(context, (muiNodeId){0}, false);
    results[count++] = muiNode_BuildAccessUpdate(context, (muiNodeId){0}, NULL);
    results[count++] = muiContext_PerformAccessAction(context, NULL, NULL);
    results[count++] = muiNode_SetAccessRole(context, (muiNodeId){0}, (muiRole){0});
    results[count++] =
        muiNode_SetAccessText(context, (muiNodeId){0}, (muiAccessTextKind){0}, NULL, 0);
    results[count++] = muiNode_SetAccessFlags(context, (muiNodeId){0}, (muiAccessFlags){0});
    results[count++] =
        muiNode_SetAccessRelation(context, (muiNodeId){0}, (muiAccessRelation){0}, NULL, 0);
    results[count++] = muiNode_SetAccessValues(context, (muiNodeId){0}, NULL);
    results[count++] = muiContext_TakeNotification(context, NULL);
    results[count++] = muiNode_BuildDrawList(context, (muiNodeId){0}, NULL);
    results[count++] = muiSetEventFunction(context, NULL, NULL);
    results[count++] = muiNode_RouteKey(context, (muiNodeId){0}, NULL, NULL);
    results[count++] = muiNode_RouteText(context, (muiNodeId){0}, NULL, NULL);
    results[count++] = muiNode_RouteNavigation(context, (muiNodeId){0}, NULL, NULL);
    results[count++] = muiNode_RouteWheel(context, (muiNodeId){0}, NULL, NULL);
    results[count++] = muiContext_DispatchPointerRecord(context, NULL, NULL);
    results[count++] = muiNode_BeginExit(context, (muiNodeId){0});
    results[count++] = muiNode_CancelExit(context, (muiNodeId){0});
    results[count++] = muiContext_SetFocus(context, 0, (muiNodeId){0}, (muiFocusCause){0});
    results[count++] = muiNode_MoveFocus(context, (muiNodeId){0}, 0, false);
    results[count++] = muiNode_MoveFocusToward(context, (muiNodeId){0}, 0, (muiDirection){0});
    results[count++] =
        muiNode_SetNeighbor(context, (muiNodeId){0}, (muiDirection){0}, (muiNodeId){0});
    results[count++] =
        muiStyle_SetInteractionValues(context, (muiStyleId){0}, (muiVariant){0}, NULL, 0);
    results[count++] = muiNode_SetInteractionValues(context, (muiNodeId){0}, NULL, 0);
    results[count++] = muiNode_RaiseLayer(context, (muiNodeId){0});
    results[count++] =
        muiNode_SetLayoutValues(context, (muiNodeId){0}, NULL, MUI_LAYOUT_PROPERTIES);
    results[count++] = muiNode_MarkContentChanged(context, (muiNodeId){0});
    results[count++] = muiNode_ComputeLayout(context, (muiNodeId){0}, NULL);
    results[count++] = muiCreateNode(context, NULL, NULL);
    results[count++] = muiDestroyNode(context, (muiNodeId){0});
    results[count++] = muiNode_InsertChild(context, (muiNodeId){0}, (muiNodeId){0}, (muiNodeId){0});
    results[count++] = muiNode_Detach(context, (muiNodeId){0});
    results[count++] = muiNode_RoutePointer(context, (muiNodeId){0}, NULL);
    results[count++] = muiContext_TakePointerRecord(context, NULL);
    results[count++] = muiPointer_SetCapture(context, 0, (muiNodeId){0});
    results[count++] = muiPointer_ReleaseCapture(context, 0);
    results[count++] = muiContext_SetClickRule(context, 0, NAN);
    results[count++] = muiPointer_Offer(context, 0, 0, 0);
    results[count++] = muiContext_SetDragThreshold(context, NAN, NAN);
    results[count++] = muiNode_SetPopup(context, (muiNodeId){0}, NULL);
    results[count++] = muiNode_ClearPopup(context, (muiNodeId){0});
    results[count++] = muiNode_SetValueRange(context, (muiNodeId){0}, NULL);
    results[count++] = muiNode_SetRangeValue(context, (muiNodeId){0}, NAN);
    results[count++] = muiNode_ClearValueRange(context, (muiNodeId){0});
    results[count++] = muiNode_SetScroll(context, (muiNodeId){0}, NAN, NAN);
    results[count++] = muiNode_ScrollIntoView(context, (muiNodeId){0});
    results[count++] = muiContext_SetScrollRule(context, NULL);
    results[count++] = muiContext_SetEnvironment(context, NULL);
    results[count++] = muiCreateStyle(context, NULL, NULL);
    results[count++] = muiDestroyStyle(context, (muiStyleId){0});
    results[count++] = muiStyle_SetLayoutValues(context, (muiStyleId){0}, (muiVariant){0}, NULL, 0);
    results[count++] = muiStyle_ResetProperties(context, (muiStyleId){0}, (muiVariant){0},
                                                (muiPropertyGroup){0}, 0);
    results[count++] = muiStyle_AddCondition(context, (muiStyleId){0}, NULL, NULL);
    results[count++] = muiStyle_SetCondition(context, (muiStyleId){0}, (muiVariant){0}, NULL);
    results[count++] = muiStyle_ClearConditions(context, (muiStyleId){0});
    results[count++] = muiCreateNodeType(context, NULL, NULL);
    results[count++] = muiDestroyNodeType(context, (muiNodeTypeId){0});
    results[count++] = muiNodeType_SetClasses(context, (muiNodeTypeId){0}, NULL, 0);
    results[count++] = muiNode_SetType(context, (muiNodeId){0}, (muiNodeTypeId){0});
    results[count++] = muiNode_SetClasses(context, (muiNodeId){0}, NULL, 0);
    results[count++] = muiNode_SetStates(context, (muiNodeId){0}, (muiState){0});
    results[count++] = muiNode_SetLayoutValues(context, (muiNodeId){0}, NULL, 0);
    results[count++] = muiNode_ResetProperties(context, (muiNodeId){0}, (muiPropertyGroup){0}, 0);
    results[count++] = muiStyle_SetTextValues(context, (muiStyleId){0}, (muiVariant){0}, NULL, 0);
    results[count++] = muiNode_SetTextValues(context, (muiNodeId){0}, NULL, 0);
    results[count++] = muiCreateTheme(context, NULL, NULL);
    results[count++] = muiDestroyTheme(context, (muiThemeId){0});
    results[count++] = muiTheme_SetTokenValue(context, (muiThemeId){0}, (muiTokenId){0}, NULL);
    results[count++] =
        muiTheme_SetTokenAlias(context, (muiThemeId){0}, (muiTokenId){0}, (muiTokenId){0});
    results[count++] = muiTheme_ResetToken(context, (muiThemeId){0}, (muiTokenId){0});
    results[count++] = muiNode_SetTheme(context, (muiNodeId){0}, (muiThemeId){0});
    results[count++] = muiCreateToken(context, NULL, NULL);
    results[count++] = muiDestroyToken(context, (muiTokenId){0});
    results[count++] = muiToken_SetValue(context, (muiTokenId){0}, NULL);
    results[count++] = muiToken_SetAlias(context, (muiTokenId){0}, (muiTokenId){0});
    results[count++] = muiStyle_SetToken(context, (muiStyleId){0}, (muiVariant){0},
                                         (muiProperty){0}, (muiTokenId){0});
    results[count++] = muiCreateTransition(context, NULL, NULL);
    results[count++] = muiDestroyTransition(context, (muiTransitionId){0});
    results[count++] = muiStyle_SetTransition(context, (muiStyleId){0}, (muiVariant){0},
                                              (muiTransitionId){0}, (muiPropertyGroup){0}, 0);
    results[count++] = muiNode_SetVirtualList(context, (muiNodeId){0}, NULL);
    results[count++] = muiNode_ClearVirtualList(context, (muiNodeId){0});
    results[count++] = muiNode_BindVirtualItem(context, (muiNodeId){0}, 0);
    results[count++] = muiNode_UnbindVirtualItem(context, (muiNodeId){0});
    results[count++] = muiNode_InsertVirtualItems(context, (muiNodeId){0}, 0, 0);
    results[count++] = muiNode_RemoveVirtualItems(context, (muiNodeId){0}, 0, 0);
    results[count++] = muiNode_MoveVirtualItem(context, (muiNodeId){0}, 0, 0);
    results[count++] = muiStyle_SetVisualValues(context, (muiStyleId){0}, (muiVariant){0}, NULL, 0);
    results[count++] = muiNode_SetVisualValues(context, (muiNodeId){0}, NULL, 0);
    return count;
}

// The indexes in CallAll of the calls the event function may not make.
static const int s_refusedInRoute[] = {5, 13, 14, 15, 16, 17, 18, 35};

static bool IsRefusedInRoute(int index)
{
    for (size_t i = 0; i < sizeof s_refusedInRoute / sizeof s_refusedInRoute[0]; i++)
    {
        if (s_refusedInRoute[i] == index)
        {
            return true;
        }
    }
    return false;
}

// What host code saw when it called back in.
typedef struct Probe
{
    muiContext* context;
    bool armed;
    // Whether calls are made as the event function's: those it may make
    // are expected not to be refused as busy.
    bool routing;
    int runs;
    bool expected;
} Probe;

// Calls back in and checks every answer, and that destroying is refused:
// the context stays alive and the misuse count grows by each refusal.
static void CallBack(Probe* probe)
{
    if (!probe->armed || probe->context == NULL)
    {
        return;
    }
    // One run a probe: a call back in may itself reach host code.
    probe->armed = false;
    probe->runs++;
    muiContext* context = probe->context;
    uint64_t misuse = muiContext_GetMisuse(context);
    muiResult results[CALLS];
    int count = CallAll(context, results);
    int refused = 0;
    bool expected = count == CALLS;
    for (int i = 0; i < count; i++)
    {
        bool state = results[i] == mui_errorBusy;
        refused += state;
        expected = expected && state == (!probe->routing || IsRefusedInRoute(i));
    }
    muiDestroyContext(context);
    expected = expected && muiContext_GetMisuse(context) >= misuse + (uint64_t)refused + 1;
    probe->expected = probe->expected && expected;
}

static muiContext* MakeContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    return context;
}

static muiNodeId MakeNode(muiContext* context, muiNodeId parent, uint64_t hostKey,
                          const muiLayoutStyle* style)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = hostKey;
    muiNodeId node = s_null;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_null) == mui_success, "child");
    }
    if (style != NULL)
    {
        CHECK(muiNode_SetLayoutValues(context, node, style, MUI_LAYOUT_PROPERTIES) == mui_success,
              "style");
    }
    return node;
}

// A row aligning by baseline around one node of host content.
static muiNodeId MakeScene(muiContext* context, muiNodeId* contentOut)
{
    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.alignItems = mui_alignBaseline;
    muiNodeId root = MakeNode(context, s_null, 0, &row);
    muiLayoutStyle content = muiDefaultLayoutStyle();
    content.content = mui_contentHost;
    *contentOut = MakeNode(context, root, 7, &content);
    return root;
}

// Outside host code no call is refused as busy, so each refusal below is.
static void TestControl(void)
{
    muiContext* context = MakeContext();
    muiResult results[CALLS];
    int count = CallAll(context, results);
    bool none = count == CALLS;
    for (int i = 0; i < count; i++)
    {
        none = none && results[i] != mui_errorBusy;
    }
    CHECK(none, "no call refused as busy outside host code");
    muiDestroyContext(context);
}

static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)nodeId;
    (void)hostKey;
    (void)width;
    (void)height;
    Probe* probes = user;
    CallBack(&probes[0]);
    return (muiSize){20.0f, 10.0f};
}

static float Baseline(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height)
{
    (void)nodeId;
    (void)hostKey;
    (void)width;
    Probe* probes = user;
    CallBack(&probes[1]);
    return height;
}

static void TestLayout(void)
{
    muiContext* context = MakeContext();
    muiNodeId content = s_null;
    muiNodeId root = MakeScene(context, &content);
    Probe probes[2] = {{context, true, false, 0, true}, {context, true, false, 0, true}};
    const muiLayoutInput input = {100.0f, 100.0f, Measure, probes, 0, Baseline, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "laid out");
    CHECK(probes[0].runs == 1 && probes[0].expected, "from the measure function");
    CHECK(probes[1].runs == 1 && probes[1].expected, "from the baseline function");
    CHECK(muiNode_IsValid(context, content) && !muiNode_IsValid(context, s_null),
          "the context alive");
    muiDestroyContext(context);
}

static void Paint(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                  muiDrawSink* sink)
{
    (void)nodeId;
    (void)hostKey;
    (void)width;
    (void)height;
    (void)sink;
    CallBack(user);
}

static void TestPaint(void)
{
    muiContext* context = MakeContext();
    muiNodeId content = s_null;
    muiNodeId root = MakeScene(context, &content);
    const muiLayoutInput layout = {100.0f, 100.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    CHECK(muiNode_ComputeLayout(context, root, &layout) == mui_success, "laid out");
    Probe probe = {context, true, false, 0, true};
    const muiDrawInput input = {1, 1.0f, Paint, &probe};
    CHECK(muiNode_BuildDrawList(context, root, &input) == mui_success, "drawn");
    CHECK(probe.runs == 1 && probe.expected, "from the paint function");
    muiDestroyContext(context);
}

static bool ReadText(void* user, muiNodeId nodeId, uint64_t hostKey, const muiRect* shown,
                     muiAccessContent* contentOut)
{
    (void)nodeId;
    (void)hostKey;
    (void)shown;
    (void)contentOut;
    CallBack(user);
    return false;
}

static void TestAccessText(void)
{
    muiContext* context = MakeContext();
    muiNodeId content = s_null;
    muiNodeId root = MakeScene(context, &content);
    Probe probe = {context, true, false, 0, true};
    CHECK(muiSetAccessTextFunction(context, ReadText, &probe) == mui_success &&
              muiNode_EnableAccess(context, root, true) == mui_success,
          "reading");
    muiAccessUpdate update = {0};
    CHECK(muiNode_BuildAccessUpdate(context, root, &update) == mui_success, "built");
    CHECK(probe.runs == 1 && probe.expected, "from the access text function");
    muiDestroyContext(context);
}

// An allocator calling back in when armed, from its allocation or its
// free.
typedef struct Calling
{
    Probe alloc;
    Probe free;
} Calling;

static void* CallingAlloc(size_t size, size_t alignment, void* user)
{
    CallBack(&((Calling*)user)->alloc);
    return alignment <= alignof(max_align_t) ? malloc(size) : NULL;
}

static void CallingFree(void* memory, size_t size, size_t alignment, void* user)
{
    (void)size;
    (void)alignment;
    CallBack(&((Calling*)user)->free);
    free(memory);
}

static void TestAllocator(void)
{
    Calling calling = {{NULL, false, false, 0, true}, {NULL, false, false, 0, true}};
    muiContextDef def = muiDefaultContextDef();
    def.allocator = (muiAllocator){CallingAlloc, CallingFree, &calling};
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId node = MakeNode(context, s_null, 0, NULL);
    calling.alloc = (Probe){context, true, false, 0, true};
    CHECK(muiNode_SetAccessText(context, node, mui_accessLabel, "One", 3) == mui_success,
          "a text copied");
    CHECK(calling.alloc.runs == 1 && calling.alloc.expected, "from the allocation");
    calling.free = (Probe){context, true, false, 0, true};
    CHECK(muiNode_SetAccessText(context, node, mui_accessLabel, "Two", 3) == mui_success,
          "a text replaced");
    CHECK(calling.free.runs == 1 && calling.free.expected, "from the free");
    // Destroying is busy to the end: a call back in from its frees is
    // refused, and the destroy goes on.
    calling.free = (Probe){context, true, false, 0, true};
    muiDestroyContext(context);
    CHECK(calling.free.runs == 1 && calling.free.expected, "from a free while destroying");
}

// The event function: what it may call, what it may not, and calls it
// makes are busy in turn (the allocator's from an access text copy).
typedef struct Routing
{
    Probe probe;
    Calling* calling;
    muiNodeId node;
    muiResult edit;
    muiResult nested;
} Routing;

static bool Route(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    (void)nodeId;
    (void)event;
    Routing* routing = user;
    if (phase != mui_phaseBubble || !routing->probe.armed)
    {
        return false;
    }
    routing->edit = muiNode_SetAccessRole(routing->probe.context, routing->node, mui_roleButton);
    routing->calling->alloc = (Probe){routing->probe.context, true, false, 0, true};
    routing->nested =
        muiNode_SetAccessText(routing->probe.context, routing->node, mui_accessLabel, "One", 3);
    CallBack(&routing->probe);
    return true;
}

static void TestEventFunction(void)
{
    Calling calling = {{NULL, false, false, 0, true}, {NULL, false, false, 0, true}};
    muiContextDef def = muiDefaultContextDef();
    def.allocator = (muiAllocator){CallingAlloc, CallingFree, &calling};
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId content = s_null;
    muiNodeId root = MakeScene(context, &content);
    Routing routing = {
        {context, true, true, 0, true}, &calling, content, mui_errorInvalid, mui_errorInvalid};
    CHECK(muiSetEventFunction(context, Route, &routing) == mui_success, "listening");
    const muiNavigationEvent activate = {0, mui_navigateActivate, 0};
    bool handled = false;
    CHECK(muiNode_RouteNavigation(context, root, &activate, &handled) == mui_success && handled,
          "routed");
    CHECK(routing.edit == mui_success && routing.nested == mui_success,
          "the event function edits the tree");
    CHECK(routing.probe.runs == 1 && routing.probe.expected,
          "the event function refused input, dispatch, actions, the function and the destroy");
    CHECK(calling.alloc.runs == 1 && calling.alloc.expected, "its edit busy in turn");
    CHECK(muiNode_IsValid(context, content), "the context alive");
    muiDestroyContext(context);
}

int main(void)
{
    TestControl();
    TestLayout();
    TestPaint();
    TestAccessText();
    TestAllocator();
    TestEventFunction();
    return s_failures == 0 ? 0 : 1;
}
