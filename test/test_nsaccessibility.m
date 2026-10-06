// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter against AppKit, in a window of its own:
// the test asks the objects as the accessibility server does for its
// clients (as Maul Window's test asks its view), since the client API
// needs a trusted process:
// - the root's parent is the view; children as shown, the generic
//   flattened; roles and subroles, a toggle button's;
// - titles, static text's value, a check box's state, a range's
//   numbers;
// - frames on the screen through the view, flipped, and a scale; the
//   node under a point; the focused node;
// - actions asked of the host, and which methods a node allows;
// - an object whose node went, and one whose adapter went, answering
//   nothing.

#include "ns.h"
#include "test_harness.h"

#include "maul-ui/access_ns.h"

#import <AppKit/AppKit.h>
#include <math.h>
#include <string.h>

@interface FlippedView : NSView
@end

@implementation FlippedView
- (BOOL)isFlipped
{
    return YES;
}
@end

static muiAccessRequest s_asked;

static bool Act(void* user, const muiAccessRequest* request)
{
    (void)user;
    s_asked = *request;
    return true;
}

typedef struct Built
{
    muiAccessNode nodes[10];
    const muiAccessNode* sent[10];
    uint64_t children[10];
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

// The window 1 at 10, 10: a focusable button 2 that clicks; a generic 3
// around a label 4; a checked check box 6; a heading 7; a slider 8 at 30
// of 0 to 100 the host sets and steps; a toggle button 9.
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
    (void)Add(built, 7, mui_roleHeading, "Title", 10, 110, 100, 20);
    muiAccessNode* slider = Add(built, 8, mui_roleSlider, "Volume", 10, 140, 100, 20);
    slider->flags = mui_accessNumeric;
    slider->actions =
        1u << mui_actionSetValue | 1u << mui_actionIncrement | 1u << mui_actionDecrement;
    slider->value = 30.0f;
    slider->maximum = 100.0f;
    Add(built, 9, mui_roleButton, "Bold", 10, 170, 50, 20)->flags = mui_accessCheckable;
    List(built, root, (const uint64_t[]){2, 3, 6, 7, 8, 9}, 6);
    List(built, generic, (const uint64_t[]){4}, 1);
    return (muiAccessUpdate){built->sent, built->nodeCount, built->children, 1, 2};
}

// The screen rectangle of a rectangle in the view's points.
static NSRect OnScreen(NSView* view, NSRect rect)
{
    return [[view window] convertRectToScreen:[view convertRect:rect toView:nil]];
}

static bool Same(NSRect a, NSRect b)
{
    return fabs(a.origin.x - b.origin.x) < 0.01 && fabs(a.origin.y - b.origin.y) < 0.01 &&
           fabs(a.size.width - b.size.width) < 0.01 && fabs(a.size.height - b.size.height) < 0.01;
}

static void TestTree(id root, NSView* view)
{
    NSArray* children = [root accessibilityChildren];
    CHECK([root accessibilityParent] == view && [root isAccessibilityElement] &&
              [[root accessibilityRole] isEqualToString:NSAccessibilityGroupRole] &&
              [[root accessibilityTitle] isEqualToString:@"Main"] && [children count] == 6,
          "the root: the view's child, a group, its children with the generic flattened");
    if ([children count] != 6)
    {
        return;
    }
    id button = children[0];
    id label = children[1];
    id check = children[2];
    id heading = children[3];
    id slider = children[4];
    id toggle = children[5];
    CHECK([[button accessibilityRole] isEqualToString:NSAccessibilityButtonRole] &&
              [[button accessibilityTitle] isEqualToString:@"OK"] &&
              [button accessibilityParent] == root,
          "a button");
    CHECK([[label accessibilityRole] isEqualToString:NSAccessibilityStaticTextRole] &&
              [label accessibilityTitle] == nil && [[label accessibilityValue] isEqual:@"Hello"],
          "static text, named by its value");
    CHECK([[check accessibilityRole] isEqualToString:NSAccessibilityCheckBoxRole] &&
              [[check accessibilityValue] isEqual:@1],
          "a check box's state");
    CHECK([[heading accessibilityRole] isEqualToString:@"AXHeading"], "a heading");
    CHECK([[slider accessibilityRole] isEqualToString:NSAccessibilitySliderRole] &&
              [[slider accessibilityValue] isEqual:@30] &&
              [[slider accessibilityMinValue] isEqual:@0] &&
              [[slider accessibilityMaxValue] isEqual:@100],
          "a range's numbers");
    CHECK([[toggle accessibilityRole] isEqualToString:NSAccessibilityCheckBoxRole] &&
              [[toggle accessibilitySubrole] isEqualToString:NSAccessibilityToggleSubrole] &&
              [[toggle accessibilityValue] isEqual:@0],
          "a toggle button");
    CHECK([button isAccessibilityFocused] && ![check isAccessibilityFocused] &&
              [root accessibilityFocusedUIElement] == button,
          "the focused node");
    NSRect frame = OnScreen(view, NSMakeRect(20, 20, 100, 40));
    CHECK(Same([button accessibilityFrame], frame) &&
              Same([label accessibilityFrame], OnScreen(view, NSMakeRect(15, 60, 100, 20))),
          "frames on the screen, through a flipped view");
    NSPoint middle = NSMakePoint(NSMidX(frame), NSMidY(frame));
    CHECK([root accessibilityHitTest:middle] == button &&
              [root accessibilityHitTest:NSMakePoint(NSMinX(frame) - 500, NSMinY(frame))] == nil,
          "the node under a point, none outside");
}

static void TestActions(id root)
{
    NSArray* children = [root accessibilityChildren];
    if ([children count] != 6)
    {
        return;
    }
    id button = children[0];
    id label = children[1];
    id check = children[2];
    id slider = children[4];
    s_asked = (muiAccessRequest){0};
    CHECK([button accessibilityPerformPress] && s_asked.action == mui_actionClick &&
              s_asked.target == 2,
          "press, the click action");
    s_asked = (muiAccessRequest){.action = mui_actionScrollRight};
    CHECK(![label accessibilityPerformPress] && s_asked.action == mui_actionScrollRight,
          "no press for static text");
    CHECK([slider accessibilityPerformIncrement] && s_asked.action == mui_actionIncrement &&
              [slider accessibilityPerformDecrement] && s_asked.action == mui_actionDecrement,
          "increment and decrement");
    [slider setAccessibilityValue:@55];
    CHECK(s_asked.action == mui_actionSetValue && s_asked.target == 8 && s_asked.value == 55.0f,
          "a range set");
    [button setAccessibilityFocused:YES];
    CHECK(s_asked.action == mui_actionFocus && s_asked.target == 2, "the focus asked for");
    CHECK([button isAccessibilitySelectorAllowed:@selector(accessibilityPerformPress)] &&
              ![label isAccessibilitySelectorAllowed:@selector(accessibilityPerformPress)] &&
              [button isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)] &&
              ![check isAccessibilitySelectorAllowed:@selector(setAccessibilityFocused:)] &&
              [slider isAccessibilitySelectorAllowed:@selector(accessibilityPerformIncrement)] &&
              ![button isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)] &&
              [slider isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)] &&
              [button isAccessibilitySelectorAllowed:@selector(accessibilityFrame)],
          "which methods a node allows");
}

static void TestContract(NSView* view)
{
    muiNsAdapterDef def = muiDefaultNsAdapterDef();
    muiNsAdapter* adapter = NULL;
    def.action = Act;
    CHECK(muiCreateNsAdapter(&def, &adapter) == mui_errorInvalid && adapter == NULL, "no view");
    def.view = (void*)view;
    def.action = NULL;
    CHECK(muiCreateNsAdapter(&def, &adapter) == mui_errorInvalid, "no action");
    CHECK(muiNsAdapter_Apply(NULL, NULL) == mui_errorInvalid &&
              muiNsAdapter_GetTree(NULL) == NULL &&
              muiNsAdapter_SetScale(NULL, 1.0f) == mui_errorInvalid &&
              muiNsAdapter_GetRoot(NULL) == NULL,
          "NULL arguments");
    muiDestroyNsAdapter(NULL);
}

int main(void)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 400, 300)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        FlippedView* view = [[FlippedView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
        [window setContentView:view];
        TestContract(view);
        muiNsAdapterDef def = muiDefaultNsAdapterDef();
        def.view = (void*)view;
        def.action = Act;
        muiNsAdapter* adapter = NULL;
        static Built s_built;
        muiAccessUpdate update = Build(&s_built);
        CHECK(muiCreateNsAdapter(&def, &adapter) == mui_success &&
                  muiNsAdapter_Apply(adapter, &update) == mui_success,
              "made");
        id root = (id)muiNsAdapter_GetRoot(adapter);
        TestTree(root, view);
        TestActions(root);
        id button = [[[root accessibilityChildren] firstObject] retain];
        CHECK(muiNsAdapter_SetScale(adapter, 2.0f) == mui_success &&
                  Same([button accessibilityFrame], OnScreen(view, NSMakeRect(40, 40, 200, 80))),
              "a scale");
        // The button gone: its object answers nothing.
        muiAccessNode rootNode = s_built.nodes[0];
        rootNode.firstChild = 1;
        rootNode.childCount = 5;
        const muiAccessUpdate gone = {(const muiAccessNode*[]){&rootNode}, 1, s_built.children, 0,
                                      0};
        CHECK(muiNsAdapter_Apply(adapter, &gone) == mui_success &&
                  [[root accessibilityChildren] count] == 5 && ![button isAccessibilityElement] &&
                  [button accessibilityParent] == nil &&
                  [[button accessibilityRole] isEqualToString:NSAccessibilityUnknownRole],
              "an object whose node went");
        [button release];
        [root retain];
        muiDestroyNsAdapter(adapter);
        CHECK([[root accessibilityChildren] count] == 0 && [root accessibilityParent] == nil,
              "an object whose adapter went");
        [root release];
        [view release];
        [window close];
    }
    return s_failures == 0 ? 0 : 1;
}
