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
//   nothing;
// - notifications, recorded in place of AppKit's: titles and values
//   changed, static text's value, live names announced on the window at
//   their priority, the focus, an element destroyed, the layout; none
//   for an update that changes nothing.

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
static char s_askedText[64];

static bool Act(void* user, const muiAccessRequest* request)
{
    (void)user;
    s_asked = *request;
    size_t kept = request->length < sizeof s_askedText ? request->length : 0;
    memcpy(s_askedText, request->text != NULL ? request->text : "", kept);
    s_askedText[kept] = '\0';
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

static NSMutableArray* s_posted;

static void Record(id element, NSAccessibilityNotificationName name, NSDictionary* info)
{
    NSString* who =
        [element isKindOfClass:[MUIAccessibilityNode class]]
            ? [NSString
                  stringWithFormat:@"%llu",
                                   (unsigned long long)((MUIAccessibilityNode*)element)->nodeId]
        : [element isKindOfClass:[NSWindow class]] ? @"window"
                                                   : @"?";
    NSString* said =
        info != nil ? [NSString stringWithFormat:@" %@/%@", info[NSAccessibilityAnnouncementKey],
                                                 info[NSAccessibilityPriorityKey]]
                    : @"";
    [s_posted addObject:[NSString stringWithFormat:@"%@ %@%@", name, who, said]];
}

static bool PostedAre(NSString* expected)
{
    NSString* got = [s_posted componentsJoinedByString:@"; "];
    bool same = [got isEqualToString:expected];
    if (!same)
    {
        printf("posted: %s\n", [got UTF8String]);
    }
    [s_posted removeAllObjects];
    return same;
}

static bool Send(muiNsAdapter* adapter, const muiAccessNode* node, const uint64_t* children,
                 uint64_t focus)
{
    const muiAccessUpdate update = {(const muiAccessNode*[]){node}, 1, children, 0, focus};
    return muiNsAdapter_Apply(adapter, &update) == mui_success;
}

static void TestNotifications(muiNsAdapter* adapter, const Built* built)
{
    muiNsPostFunction saved = adapter->post;
    adapter->post = Record;
    s_posted = [[NSMutableArray alloc] init];
    muiAccessNode check = built->nodes[4];
    check.flags = mui_accessCheckable;
    check.text[mui_accessLabel] = "Agreed";
    check.textLength[mui_accessLabel] = 6;
    CHECK(Send(adapter, &check, built->children, 0) &&
              PostedAre(@"AXTitleChanged 6; AXValueChanged 6"),
          "a title and a value changed");
    muiAccessNode label = built->nodes[3];
    label.text[mui_accessValue] = "Hi";
    label.textLength[mui_accessValue] = 2;
    CHECK(Send(adapter, &label, built->children, 0) && PostedAre(@"AXValueChanged 4"),
          "static text renamed: its value");
    muiAccessNode slider = built->nodes[6];
    slider.value = 40.0f;
    CHECK(Send(adapter, &slider, built->children, 0) && PostedAre(@"AXValueChanged 8"),
          "a range's number");
    CHECK(Send(adapter, &slider, built->children, 0) && PostedAre(@""), "nothing changed");
    muiAccessNode heading = built->nodes[5];
    heading.values.live = mui_livePolite;
    heading.text[mui_accessLabel] = "Topic";
    heading.textLength[mui_accessLabel] = 5;
    CHECK(Send(adapter, &heading, built->children, 0) &&
              PostedAre(@"AXTitleChanged 7; AXAnnouncementRequested window Topic/50"),
          "a polite name announced");
    heading.values.live = mui_liveAssertive;
    heading.text[mui_accessLabel] = "Topic 2";
    heading.textLength[mui_accessLabel] = 7;
    CHECK(Send(adapter, &heading, built->children, 0) &&
              PostedAre(@"AXTitleChanged 7; AXAnnouncementRequested window Topic 2/90"),
          "an assertive name announced");
    CHECK(Send(adapter, &built->nodes[0], built->children, 6) &&
              PostedAre(@"AXLayoutChanged 1; AXFocusedUIElementChanged 6"),
          "the focus moved, which may show a hidden node");
    // A node 10 added, then gone before any client asked for it: the
    // layout only; then the toggle button, which a client saw, gone.
    muiAccessNode root = built->nodes[0];
    muiAccessNode added = {.id = 10, .role = mui_roleButton};
    const uint64_t more[7] = {2, 3, 6, 7, 8, 9, 10};
    root.firstChild = 0;
    root.childCount = 7;
    const muiAccessUpdate grow = {(const muiAccessNode*[]){&root, &added}, 2, more, 0, 0};
    CHECK(muiNsAdapter_Apply(adapter, &grow) == mui_success && PostedAre(@"AXLayoutChanged 1"),
          "a node added: the layout");
    root.childCount = 6;
    CHECK(Send(adapter, &root, more, 0) && PostedAre(@"AXLayoutChanged 1"),
          "a node no client saw gone: the layout only");
    root.childCount = 5;
    CHECK(Send(adapter, &root, more, 0) && PostedAre(@"AXUIElementDestroyed 9; AXLayoutChanged 1"),
          "a node a client saw gone: destroyed");
    root.childCount = 6;
    muiAccessNode toggle = built->nodes[7];
    const muiAccessUpdate back = {(const muiAccessNode*[]){&root, &toggle}, 2, more, 0, 2};
    CHECK(muiNsAdapter_Apply(adapter, &back) == mui_success &&
              PostedAre(@"AXLayoutChanged 1; AXFocusedUIElementChanged 2"),
          "back, the layout before the focus");
    // The window focused, naming the check box its active descendant: the
    // check box is the focus shown, as browsers show aria-activedescendant.
    static const muiAccessLink s_active[1] = {{6, mui_relationActiveDescendant}};
    muiAccessNode window = root;
    window.links = s_active;
    window.linkCount = 1;
    // The tree's focus moves too, which may show a hidden node: the
    // layout first.
    CHECK(Send(adapter, &window, more, 1) &&
              PostedAre(@"AXLayoutChanged 1; AXFocusedUIElementChanged 6"),
          "an active descendant: the focus shown");
    CHECK(Send(adapter, &root, more, 2) &&
              PostedAre(@"AXLayoutChanged 1; AXFocusedUIElementChanged 2"),
          "none: the focus back");
    [s_posted release];
    adapter->post = saved;
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

// The heading 7 made a text input being edited, "héllo 𝄞\nnext" on two
// lines, the caret after é: its characters in UTF-16, its lines, ranges
// and selection, set through the host.
static void TestText(muiNsAdapter* adapter, id root, const Built* built)
{
    static const char s_value[] = "h\xC3\xA9llo \xF0\x9D\x84\x9E\nnext";
    static const uint32_t s_lines[2] = {0, 12};
    muiAccessNode input = built->nodes[5];
    input.role = mui_roleTextInput;
    input.text[mui_accessValue] = s_value;
    input.textLength[mui_accessValue] = sizeof s_value - 1;
    input.marks = (muiAccessTextMarks){
        .anchor = 3, .focus = 3, .selected = true, .lineStarts = s_lines, .lineCount = 2};
    input.actions = 1u << mui_actionSetSelection | 1u << mui_actionReplaceText;
    const muiAccessUpdate update = {(const muiAccessNode*[]){&input}, 1, built->children, 0, 0};
    muiNsPostFunction saved = adapter->post;
    adapter->post = Record;
    s_posted = [[NSMutableArray alloc] init];
    CHECK(muiNsAdapter_Apply(adapter, &update) == mui_success, "a text input");
    [s_posted removeAllObjects];
    id field = [[root accessibilityChildren] objectAtIndex:3];
    CHECK(
        [field accessibilityNumberOfCharacters] == 13 &&
            NSEqualRanges([field accessibilitySelectedTextRange], NSMakeRange(2, 0)) &&
            [field accessibilityInsertionPointLineNumber] == 0 &&
            [field accessibilityLineForIndex:9] == 1 &&
            NSEqualRanges([field accessibilityRangeForLine:1], NSMakeRange(9, 4)) &&
            NSEqualRanges([field accessibilityRangeForIndex:6], NSMakeRange(6, 2)) &&
            [[field accessibilityStringForRange:NSMakeRange(1, 4)] isEqualToString:@"\u00e9llo"] &&
            [field accessibilityStringForRange:NSMakeRange(10, 9)] == nil,
        "characters in UTF-16, a surrogate pair whole; lines and ranges");
    CHECK([field isAccessibilitySelectorAllowed:@selector(setAccessibilitySelectedTextRange:)] &&
              [field isAccessibilitySelectorAllowed:@selector(accessibilityRangeForLine:)] &&
              ![[[root accessibilityChildren] firstObject]
                  isAccessibilitySelectorAllowed:@selector(accessibilityRangeForLine:)],
          "text methods on text alone");
    s_asked = (muiAccessRequest){0};
    [field setAccessibilitySelectedTextRange:NSMakeRange(1, 5)];
    CHECK(s_asked.action == mui_actionSetSelection && s_asked.target == 7 && s_asked.anchor == 1 &&
              s_asked.focus == 7,
          "a selection asked in bytes");
    [field setAccessibilitySelectedText:@"a"];
    CHECK(s_asked.action == mui_actionReplaceText && s_asked.anchor == 3 && s_asked.focus == 3 &&
              strcmp(s_askedText, "a") == 0,
          "text put at the caret");
    [field setAccessibilityValue:@"new"];
    CHECK(s_asked.action == mui_actionReplaceText && s_asked.anchor == 0 &&
              s_asked.focus == sizeof s_value - 1 && strcmp(s_askedText, "new") == 0,
          "the whole text set");
    input.marks.anchor = 7;
    CHECK(muiNsAdapter_Apply(adapter, &update) == mui_success &&
              PostedAre(@"AXSelectedTextChanged 7") &&
              [[field accessibilitySelectedText] isEqualToString:@"llo "],
          "a selection told and read");
    [s_posted release];
    adapter->post = saved;
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
        TestNotifications(adapter, &s_built);
        TestText(adapter, root, &s_built);
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
