// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a failure leaves in a call's outputs (Maul conventions, Errors and
// assertions): a call whose first parameter is the context clears every
// output on any failure, before it checks the context, so a NULL context,
// a busy one and a stale id all leave zeros; a count a too-small array
// needs is the one output a failure defines.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/event.h"
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

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Every output a call can write, filled with a pattern no call writes
// before each call and read back after it.
typedef struct Outputs
{
    muiNodeId node;
    muiStyleId style;
    muiNodeTypeId type;
    muiThemeId theme;
    muiTokenId token;
    muiTokenId alias;
    muiTransitionId transition;
    muiVariant variant;
    muiCondition condition;
    muiPropertyMask mask;
    muiLayoutStyle layout;
    muiVisualStyle visual;
    muiTextStyle text;
    muiComputedTextStyle computed;
    muiInteractionStyle interaction;
    muiTokenValue value;
    muiNotification notification;
    muiPointerRecord record;
    muiPointerState state;
    muiPopup popup;
    muiPopupSide side;
    muiValueRange range;
    muiScrollThumb thumb;
    muiSize extent;
    muiRect rect;
    muiDrawList list;
    muiHit hit;
    muiAccessUpdate update;
    muiAccessValues accessValues;
    muiAccessFlags flags;
    muiRole role;
    const char* string;
    size_t length;
    uint32_t count;
    uint32_t first;
    uint32_t end;
    float x;
    float y;
    bool handled;
} Outputs;

static void Fill(Outputs* outputs)
{
    memset(outputs, 0xA5, sizeof *outputs);
}

static bool IsZero(const void* memory, size_t size)
{
    const unsigned char* bytes = memory;
    for (size_t i = 0; i < size; i++)
    {
        if (bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}

#define CLEARED(field) IsZero(&outputs.field, sizeof outputs.field)

// Calls every query of a node or a class that writes outputs, on a node
// and a class that may be gone, and checks each failure clears them.
static void CheckQueries(const muiContext* context, muiNodeId node, muiStyleId style,
                         muiResult expected, const char* why)
{
    Outputs outputs;
    char message[160];
#define EXPECT(call, ...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        Fill(&outputs);                                                                            \
        muiResult result_ = (call);                                                                \
        snprintf(message, sizeof message, "%s: %s clears its outputs", why, #call);                \
        CHECK(result_ == expected && (__VA_ARGS__), message);                                      \
    } while (0)

    EXPECT(muiNode_GetAccessRole(context, node, &outputs.role), CLEARED(role));
    EXPECT(muiNode_GetAccessText(context, node, mui_accessLabel, &outputs.string, &outputs.length),
           CLEARED(string) && CLEARED(length));
    EXPECT(muiNode_GetAccessFlags(context, node, &outputs.flags), CLEARED(flags));
    EXPECT(
        muiNode_GetAccessRelation(context, node, mui_relationLabelledBy, NULL, 0, &outputs.count),
        CLEARED(count));
    EXPECT(muiNode_GetAccessValues(context, node, &outputs.accessValues), CLEARED(accessValues));
    EXPECT(muiNode_GetInteractionStyle(context, node, &outputs.interaction), CLEARED(interaction));
    EXPECT(muiNode_HitTest(context, node, 0.0f, 0.0f, &outputs.hit), CLEARED(hit));
    EXPECT(muiNode_GetLayoutStyle(context, node, &outputs.layout), CLEARED(layout));
    EXPECT(muiNode_MapToRoot(context, node, 0.0f, 0.0f, &outputs.x, &outputs.y),
           CLEARED(x) && CLEARED(y));
    EXPECT(muiNode_GetPopup(context, node, &outputs.popup), CLEARED(popup));
    EXPECT(muiNode_GetPopupSide(context, node, &outputs.side), CLEARED(side));
    EXPECT(muiNode_GetValueRange(context, node, &outputs.range), CLEARED(range));
    EXPECT(muiNode_GetScroll(context, node, &outputs.x, &outputs.y), CLEARED(x) && CLEARED(y));
    EXPECT(muiNode_GetScrollExtent(context, node, &outputs.extent), CLEARED(extent));
    EXPECT(muiNode_GetScrollThumb(context, node, mui_axisHorizontal, 100.0f, 10.0f, &outputs.thumb),
           CLEARED(thumb));
    EXPECT(
        muiStyle_GetLayoutValues(context, style, mui_variantBase, &outputs.layout, &outputs.mask),
        CLEARED(layout) && CLEARED(mask));
    EXPECT(muiStyle_GetCondition(context, style, mui_variantCondition0, &outputs.condition),
           CLEARED(condition));
    EXPECT(muiStyle_GetInteractionValues(context, style, mui_variantBase, &outputs.interaction,
                                         &outputs.mask),
           CLEARED(interaction) && CLEARED(mask));
    EXPECT(muiStyle_GetTextValues(context, style, mui_variantBase, &outputs.text, &outputs.mask),
           CLEARED(text) && CLEARED(mask));
    EXPECT(muiNode_GetTextStyle(context, node, &outputs.computed), CLEARED(computed));
    EXPECT(muiNode_GetTheme(context, node, &outputs.theme), CLEARED(theme));
    EXPECT(
        muiStyle_GetToken(context, style, mui_variantBase, mui_propertyBackground, &outputs.token),
        CLEARED(token));
    EXPECT(muiStyle_GetTransition(context, style, mui_variantBase, mui_propertyBackground,
                                  &outputs.transition),
           CLEARED(transition));
    EXPECT(muiNode_GetVirtualWindow(context, node, &outputs.first, &outputs.end),
           CLEARED(first) && CLEARED(end));
    EXPECT(muiNode_GetVirtualItem(context, node, 0, &outputs.x, &outputs.y),
           CLEARED(x) && CLEARED(y));
    EXPECT(muiNode_GetBoundVirtualItem(context, node, &outputs.count), CLEARED(count));
    EXPECT(
        muiStyle_GetVisualValues(context, style, mui_variantBase, &outputs.visual, &outputs.mask),
        CLEARED(visual) && CLEARED(mask));
    EXPECT(muiNode_GetVisualStyle(context, node, &outputs.visual), CLEARED(visual));
#undef EXPECT
}

static void TestNullContextClearsOutputs(void)
{
    CheckQueries(NULL, (muiNodeId){1, 1}, (muiStyleId){1, 1}, mui_errorInvalid, "a NULL context");

    Outputs outputs;
    Fill(&outputs);
    const muiTokenId token = {1, 1};
    CHECK(muiToken_GetValue(NULL, token, &outputs.value, &outputs.alias) == mui_errorInvalid &&
              CLEARED(value) && CLEARED(alias),
          "muiToken_GetValue clears its outputs");
    Fill(&outputs);
    CHECK(muiTheme_GetToken(NULL, (muiThemeId){1, 1}, token, &outputs.value, &outputs.alias) ==
                  mui_errorInvalid &&
              CLEARED(value) && CLEARED(alias),
          "muiTheme_GetToken clears its outputs");
    Fill(&outputs);
    CHECK(muiNode_GetTokenValue(NULL, (muiNodeId){1, 1}, token, &outputs.value) ==
                  mui_errorInvalid &&
              CLEARED(value),
          "muiNode_GetTokenValue clears its output");
    Fill(&outputs);
    CHECK(muiContext_GetDrawList(NULL, &outputs.list) == mui_errorInvalid && CLEARED(list),
          "muiContext_GetDrawList clears its output");
    Fill(&outputs);
    CHECK(muiDrawSink_GetVisibleRect(NULL, &outputs.rect) == mui_errorInvalid && CLEARED(rect),
          "muiDrawSink_GetVisibleRect clears its output");
    Fill(&outputs);
    CHECK(muiPointer_GetState(NULL, 1, &outputs.state) == mui_errorInvalid && CLEARED(state),
          "muiPointer_GetState clears its output");
}

// A NULL context, and a context busy in its measure function, refuse
// every call that changes it; each clears its outputs first.
static void CheckChanges(muiContext* context, muiResult expected, const char* why)
{
    Outputs outputs;
    char message[160];
#define EXPECT(call, ...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        Fill(&outputs);                                                                            \
        muiResult result_ = (call);                                                                \
        snprintf(message, sizeof message, "%s: %s clears its outputs", why, #call);                \
        CHECK(result_ == expected && (__VA_ARGS__), message);                                      \
    } while (0)

    const muiNodeDef nodeDef = muiDefaultNodeDef();
    const muiStyleDef styleDef = muiDefaultStyleDef();
    const muiNodeTypeDef typeDef = muiDefaultNodeTypeDef();
    const muiThemeDef themeDef = muiDefaultThemeDef();
    const muiTokenDef tokenDef = muiDefaultTokenDef();
    const muiTransitionDef transitionDef = muiDefaultTransitionDef();
    const muiCondition condition = muiDefaultCondition();
    const muiKeyEvent key = {.key = 'a', .code = 4, .down = true};
    const muiTextEvent text = {.text = "a", .length = 1};
    const muiNavigationEvent navigation = {.action = mui_navigateNext};
    const muiWheelEvent wheel = {.deltaY = 1.0f};
    const muiPointerRecord record = {.kind = mui_pointerRecordPress};
    const muiAccessRequest request = {.action = mui_actionFocus};
    const muiNodeId root = {1, 1};

    EXPECT(muiCreateNode(context, &nodeDef, &outputs.node), CLEARED(node));
    EXPECT(muiCreateStyle(context, &styleDef, &outputs.style), CLEARED(style));
    EXPECT(muiCreateNodeType(context, &typeDef, &outputs.type), CLEARED(type));
    EXPECT(muiCreateTheme(context, &themeDef, &outputs.theme), CLEARED(theme));
    EXPECT(muiCreateToken(context, &tokenDef, &outputs.token), CLEARED(token));
    EXPECT(muiCreateTransition(context, &transitionDef, &outputs.transition), CLEARED(transition));
    EXPECT(muiStyle_AddCondition(context, (muiStyleId){1, 1}, &condition, &outputs.variant),
           CLEARED(variant));
    EXPECT(muiContext_TakeNotification(context, &outputs.notification), CLEARED(notification));
    EXPECT(muiContext_TakePointerRecord(context, &outputs.record), CLEARED(record));
    EXPECT(muiNode_RouteKey(context, root, &key, &outputs.handled), CLEARED(handled));
    EXPECT(muiNode_RouteText(context, root, &text, &outputs.handled), CLEARED(handled));
    EXPECT(muiNode_RouteNavigation(context, root, &navigation, &outputs.handled), CLEARED(handled));
    EXPECT(muiNode_RouteWheel(context, root, &wheel, &outputs.handled), CLEARED(handled));
    EXPECT(muiContext_DispatchPointerRecord(context, &record, &outputs.handled), CLEARED(handled));
    EXPECT(muiNode_BuildAccessUpdate(context, root, &outputs.update), CLEARED(update));
    EXPECT(muiContext_PerformAccessAction(context, &request, &outputs.handled), CLEARED(handled));
#undef EXPECT
}

static void TestNullContextClearsChangesOutputs(void)
{
    CheckChanges(NULL, mui_errorInvalid, "a NULL context");
}

typedef struct Busy
{
    muiContext* context;
    bool ran;
} Busy;

static muiSize MeasureWhileBusy(void* user, muiNodeId nodeId, uint64_t hostKey,
                                muiMeasureAxis width, muiMeasureAxis height)
{
    (void)nodeId;
    (void)hostKey;
    (void)width;
    (void)height;
    Busy* busy = user;
    if (!busy->ran)
    {
        busy->ran = true;
        CheckChanges(busy->context, mui_errorBusy, "a busy context");
    }
    return (muiSize){10.0f, 10.0f};
}

static void TestBusyContextClearsOutputs(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId root = {0};
    CHECK(muiCreateNode(context, &nodeDef, &root) == mui_success, "root");
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, root, &style, MUI_LAYOUT_PROPERTIES) == mui_success,
          "host content");
    Busy busy = {context, false};
    muiLayoutInput input = {.availableWidth = 100.0f,
                            .availableHeight = 100.0f,
                            .measure = MeasureWhileBusy,
                            .user = &busy};
    CHECK(muiNode_ComputeLayout(context, root, &input) == mui_success, "layout");
    CHECK(busy.ran, "the measure function ran");
    muiDestroyContext(context);
}

static void TestStaleIdsClearOutputs(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiStyleDef styleDef = muiDefaultStyleDef();
    muiNodeId node = {0};
    muiStyleId style = {0};
    CHECK(muiCreateNode(context, &nodeDef, &node) == mui_success, "node");
    CHECK(muiCreateStyle(context, &styleDef, &style) == mui_success, "style");
    CHECK(muiDestroyNode(context, node) == mui_success, "node gone");
    CHECK(muiDestroyStyle(context, style) == mui_success, "style gone");
    CheckQueries(context, node, style, mui_errorStale, "a stale id");
    muiDestroyContext(context);
}

static void TestCapacityFailureGivesTheCount(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    muiNodeId nodes[3] = {0};
    for (int i = 0; i < 3; i++)
    {
        CHECK(muiCreateNode(context, &nodeDef, &nodes[i]) == mui_success, "node");
    }
    CHECK(muiNode_SetAccessRelation(context, nodes[0], mui_relationControls, &nodes[1], 2) ==
              mui_success,
          "relation");
    muiNodeId targets[1] = {0};
    uint32_t count = 0;
    CHECK(muiNode_GetAccessRelation(context, nodes[0], mui_relationControls, targets, 1, &count) ==
                  mui_errorCapacity &&
              count == 2,
          "a capacity failure gives the count needed");
    muiDestroyContext(context);
}

int main(void)
{
    TestNullContextClearsOutputs();
    TestNullContextClearsChangesOutputs();
    TestBusyContextClearsOutputs();
    TestStaleIdsClearOutputs();
    TestCapacityFailureGivesTheCount();
    return s_failures == 0 ? 0 : 1;
}
