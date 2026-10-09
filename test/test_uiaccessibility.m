// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UIAccessibility adapter against UIKit, an application in the iOS
// simulator (tools/run_ios_app.sh) with a window of its own: the test
// asks the objects as VoiceOver does:
// - the root's container in the view; a node with children a container
//   whose first element is the node's, the generic flattened; each
//   element's container;
// - labels, values (a toggle's state, a range's number), hints, traits,
//   container types; who is an element, a pane that only scrolls not;
// - frames on the screen through the view, and a scale;
// - activation, increment and decrement, scrolling by direction, the
//   escape gesture, VoiceOver's cursor arriving, asked of the host;
// - an object whose node went, and one whose adapter went, answering
//   nothing;
// - notifications, recorded in place of UIKit's: a new screen at the
//   focus, then at a node turned modal; the layout with the focus when
//   it moved, else with none; live names announced, queued when polite;
//   nothing for a rename that is not live, nor for an update that
//   changes nothing;
// - a text input's element through UITextInput: its text, caret and
//   units in UTF-16 by the tree's words and lines, the selection and
//   edits asked of the host, the input delegate told; the element remade
//   as the node starts and ends being edited.

#include "test_harness.h"
#include "uikit.h"

#include "maul-ui/access_uikit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// The window 1 at 10, 10: a focusable button 2 that clicks; a generic 3
// around a label 4; a group 5 of a checked check box 6 and a toggle
// button 9; a heading 7; a slider 8 at 30 of 0 to 100 the host steps;
// a nameless pane 10 that scrolls, around a label 11.
static muiAccessUpdate Build(Built* built)
{
    *built = (Built){0};
    muiAccessNode* root = Add(built, 1, mui_roleWindow, "Main", 10, 10, 300, 300);
    muiAccessNode* ok = Add(built, 2, mui_roleButton, "OK", 10, 10, 100, 40);
    ok->flags = mui_accessFocusable;
    ok->actions = 1u << mui_actionClick;
    muiAccessNode* generic = Add(built, 3, mui_roleGeneric, NULL, 0, 50, 200, 20);
    (void)Add(built, 4, mui_roleLabel, "Hello", 5, 0, 100, 20);
    muiAccessNode* group = Add(built, 5, mui_roleGroup, "Options", 10, 80, 200, 60);
    Add(built, 6, mui_roleCheckBox, "Agree", 0, 0, 100, 20)->flags =
        mui_accessCheckable | mui_accessChecked;
    Add(built, 9, mui_roleButton, "Bold", 0, 30, 50, 20)->flags = mui_accessCheckable;
    (void)Add(built, 7, mui_roleHeading, "Title", 10, 150, 100, 20);
    muiAccessNode* slider = Add(built, 8, mui_roleSlider, "Volume", 10, 180, 100, 20);
    slider->flags = mui_accessNumeric;
    slider->actions =
        1u << mui_actionIncrement | 1u << mui_actionDecrement | 1u << mui_actionScrollIntoView;
    slider->value = 30.0f;
    slider->maximum = 100.0f;
    slider->text[mui_accessDescription] = "Louder up";
    slider->textLength[mui_accessDescription] = 9;
    muiAccessNode* pane = Add(built, 10, mui_roleScrollView, NULL, 10, 210, 200, 40);
    pane->actions = 1u << mui_actionScrollUp | 1u << mui_actionScrollDown;
    (void)Add(built, 11, mui_roleLabel, "Note", 0, 0, 100, 20);
    List(built, root, (const uint64_t[]){2, 3, 5, 7, 8, 10}, 6);
    List(built, generic, (const uint64_t[]){4}, 1);
    List(built, group, (const uint64_t[]){6, 9}, 2);
    List(built, pane, (const uint64_t[]){11}, 1);
    return (muiAccessUpdate){built->sent, built->nodeCount, built->children, 1, 2};
}

static bool Same(CGRect a, CGRect b)
{
    return fabs(a.origin.x - b.origin.x) < 0.01 && fabs(a.origin.y - b.origin.y) < 0.01 &&
           fabs(a.size.width - b.size.width) < 0.01 && fabs(a.size.height - b.size.height) < 0.01;
}

static bool IsContainerOf(id object, uint64_t node)
{
    return [object isKindOfClass:[MUIAccessibilityContainer class]] &&
           ((MUIAccessibilityContainer*)object)->nodeId == node;
}

static bool IsElementOf(id object, uint64_t node)
{
    return [object isKindOfClass:[MUIAccessibilityElement class]] &&
           ((MUIAccessibilityElement*)object)->nodeId == node;
}

static void TestStructure(id root, UIView* view)
{
    NSArray* elements = [root accessibilityElements];
    CHECK(IsContainerOf(root, 1) && ![root isAccessibilityElement] &&
              [root accessibilityContainer] == view && [elements count] == 7,
          "the root: a container in the view");
    if ([elements count] != 7)
    {
        return;
    }
    CHECK(IsElementOf(elements[0], 1) && IsElementOf(elements[1], 2) &&
              IsElementOf(elements[2], 4) && IsContainerOf(elements[3], 5) &&
              IsElementOf(elements[4], 7) && IsElementOf(elements[5], 8) &&
              IsContainerOf(elements[6], 10),
          "its elements: the root's own, then the children, the generic flattened");
    id group = elements[3];
    NSArray* inGroup = [group accessibilityElements];
    CHECK([inGroup count] == 3 && IsElementOf(inGroup[0], 5) && IsElementOf(inGroup[1], 6) &&
              IsElementOf(inGroup[2], 9) && [group accessibilityContainer] == root &&
              [inGroup[0] accessibilityContainer] == group &&
              [inGroup[1] accessibilityContainer] == group &&
              [elements[1] accessibilityContainer] == root &&
              [elements[0] accessibilityContainer] == root,
          "a group: a container of its own element and its children");
    CHECK([group accessibilityElementCount] == 3 &&
              [group accessibilityElementAtIndex:1] == inGroup[1] &&
              [group accessibilityElementAtIndex:3] == nil &&
              [group indexOfAccessibilityElement:inGroup[2]] == 2 &&
              [group indexOfAccessibilityElement:root] == NSNotFound,
          "the container's indices");
    CHECK([group accessibilityContainerType] == UIAccessibilityContainerTypeSemanticGroup &&
              [[group accessibilityLabel] isEqualToString:@"Options"] &&
              [root accessibilityContainerType] == UIAccessibilityContainerTypeNone,
          "container types and names");
    id pane = elements[6];
    CHECK(![[pane accessibilityElements][0] isAccessibilityElement] &&
              [[pane accessibilityElements][1] isAccessibilityElement],
          "a pane that only scrolls is not an element; its label is");
}

static void TestAttributes(id root, UIView* view)
{
    NSArray* elements = [root accessibilityElements];
    if ([elements count] != 7)
    {
        return;
    }
    id button = elements[1];
    id label = elements[2];
    NSArray* inGroup = [elements[3] accessibilityElements];
    id heading = elements[4];
    id slider = elements[5];
    CHECK([button isAccessibilityElement] && [[button accessibilityLabel] isEqualToString:@"OK"] &&
              [button accessibilityTraits] == UIAccessibilityTraitButton &&
              [button accessibilityValue] == nil,
          "a button");
    CHECK([[label accessibilityLabel] isEqualToString:@"Hello"] &&
              [label accessibilityValue] == nil &&
              [label accessibilityTraits] == UIAccessibilityTraitStaticText,
          "a label, named by its value");
    if ([inGroup count] == 3)
    {
        CHECK([inGroup[1] accessibilityTraits] ==
                  (UIAccessibilityTraitButton | UIAccessibilityTraitSelected),
              "a checked check box, selected");
        UIAccessibilityTraits toggle = UIAccessibilityTraitButton;
        if (@available(iOS 17.0, *))
        {
            toggle = UIAccessibilityTraitToggleButton;
        }
        CHECK([inGroup[2] accessibilityTraits] == toggle &&
                  [[inGroup[2] accessibilityValue] isEqualToString:@"0"],
              "a toggle button, its state its value");
    }
    CHECK([heading accessibilityTraits] == UIAccessibilityTraitHeader, "a heading");
    CHECK([slider accessibilityTraits] == UIAccessibilityTraitAdjustable &&
              [[slider accessibilityValue] isEqualToString:@"30"] &&
              [[slider accessibilityHint] isEqualToString:@"Louder up"],
          "a range: adjustable, its number, its hint");
    CHECK(Same([button accessibilityFrame],
               UIAccessibilityConvertFrameToScreenCoordinates(CGRectMake(20, 20, 100, 40), view)) &&
              Same([label accessibilityFrame], UIAccessibilityConvertFrameToScreenCoordinates(
                                                   CGRectMake(15, 60, 100, 20), view)) &&
              Same([root accessibilityFrame],
                   UIAccessibilityConvertFrameToScreenCoordinates([view bounds], view)),
          "frames on the screen; a container's the view's");
}

static void TestActions(id root)
{
    NSArray* elements = [root accessibilityElements];
    if ([elements count] != 7)
    {
        return;
    }
    id button = elements[1];
    id label = elements[2];
    id slider = elements[5];
    id pane = elements[6];
    s_asked = (muiAccessRequest){0};
    CHECK([button accessibilityActivate] && s_asked.action == mui_actionClick &&
              s_asked.target == 2,
          "activation clicks");
    s_asked = (muiAccessRequest){.action = mui_actionBlur};
    CHECK(![label accessibilityActivate] && [slider accessibilityActivate] &&
              s_asked.action == mui_actionBlur,
          "no activation for a label; a range takes it without acting");
    [slider accessibilityIncrement];
    CHECK(s_asked.action == mui_actionIncrement && s_asked.target == 8, "increment");
    [slider accessibilityDecrement];
    CHECK(s_asked.action == mui_actionDecrement, "decrement");
    [slider accessibilityElementDidBecomeFocused];
    CHECK(s_asked.action == mui_actionScrollIntoView && s_asked.target == 8,
          "VoiceOver's cursor arriving scrolls the node in");
    CHECK([pane accessibilityScroll:UIAccessibilityScrollDirectionDown] &&
              s_asked.action == mui_actionScrollDown && s_asked.target == 10 &&
              [pane accessibilityScroll:UIAccessibilityScrollDirectionPrevious] &&
              s_asked.action == mui_actionScrollUp &&
              ![pane accessibilityScroll:UIAccessibilityScrollDirectionLeft] &&
              ![button accessibilityScroll:UIAccessibilityScrollDirectionDown],
          "scrolling by direction, where the node scrolls so");
    CHECK(![button accessibilityPerformEscape], "no escape");
}

static NSMutableArray* s_posted;

static NSString* WhoIs(id argument)
{
    if (argument == nil)
    {
        return @"nil";
    }
    if ([argument isKindOfClass:[MUIAccessibilityElement class]])
    {
        return [NSString
            stringWithFormat:@"%llu",
                             (unsigned long long)((MUIAccessibilityElement*)argument)->nodeId];
    }
    if ([argument isKindOfClass:[NSAttributedString class]])
    {
        NSAttributedString* text = argument;
        id queued = [text attribute:UIAccessibilitySpeechAttributeQueueAnnouncement
                            atIndex:0
                     effectiveRange:NULL];
        return [NSString
            stringWithFormat:@"%@ queued%@", [text string], [queued boolValue] ? @"" : @"?"];
    }
    return [NSString stringWithFormat:@"%@ now", argument];
}

static void Record(UIAccessibilityNotifications notification, id argument)
{
    NSString* what = notification == UIAccessibilityScreenChangedNotification   ? @"screen"
                     : notification == UIAccessibilityLayoutChangedNotification ? @"layout"
                     : notification == UIAccessibilityAnnouncementNotification  ? @"announce"
                                                                                : @"?";
    [s_posted addObject:[NSString stringWithFormat:@"%@ %@", what, WhoIs(argument)]];
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

static bool Send(muiUikitAdapter* adapter, const muiAccessNode* const* nodes, uint32_t count,
                 const uint64_t* children, uint64_t focus)
{
    const muiAccessUpdate update = {nodes, count, children, 0, focus};
    return muiUikitAdapter_Apply(adapter, &update) == mui_success;
}

static void TestNotifications(UIView* view)
{
    muiUikitAdapterDef def = muiDefaultUikitAdapterDef();
    def.view = (void*)view;
    def.action = Act;
    muiUikitAdapter* adapter = NULL;
    static Built s_built;
    muiAccessUpdate update = Build(&s_built);
    if (muiCreateUikitAdapter(&def, &adapter) != mui_success)
    {
        CHECK(false, "made for notifications");
        return;
    }
    adapter->post = Record;
    s_posted = [[NSMutableArray alloc] init];
    CHECK(muiUikitAdapter_Apply(adapter, &update) == mui_success && PostedAre(@"screen 2"),
          "the first tree: a new screen, at the focus");
    muiAccessNode slider = s_built.nodes[8];
    slider.text[mui_accessLabel] = "Loudness";
    slider.textLength[mui_accessLabel] = 8;
    CHECK(Send(adapter, (const muiAccessNode*[]){&slider}, 1, s_built.children, 0) &&
              PostedAre(@""),
          "a rename that is not live: nothing");
    muiAccessNode heading = s_built.nodes[7];
    heading.values.live = mui_livePolite;
    heading.text[mui_accessLabel] = "Topic";
    heading.textLength[mui_accessLabel] = 5;
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, s_built.children, 0) &&
              PostedAre(@"announce Topic queued"),
          "a polite name, queued");
    heading.values.live = mui_liveAssertive;
    heading.text[mui_accessLabel] = "Topic 2";
    heading.textLength[mui_accessLabel] = 7;
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, s_built.children, 0) &&
              PostedAre(@"announce Topic 2 now"),
          "an assertive name, at once");
    CHECK(Send(adapter, (const muiAccessNode*[]){&heading}, 1, s_built.children, 0) &&
              PostedAre(@""),
          "nothing changed");
    CHECK(Send(adapter, (const muiAccessNode*[]){&s_built.nodes[0]}, 1, s_built.children, 8) &&
              PostedAre(@"layout 8"),
          "the focus moved: the layout, with it");
    // The window focused, naming node 5 its active descendant: the focus
    // shown, as browsers show aria-activedescendant.
    static const muiAccessLink s_active[1] = {{5, mui_relationActiveDescendant}};
    muiAccessNode window = s_built.nodes[0];
    window.links = s_active;
    window.linkCount = 1;
    CHECK(Send(adapter, (const muiAccessNode*[]){&window}, 1, s_built.children, 1) &&
              PostedAre(@"layout 5"),
          "an active descendant: the focus shown");
    CHECK(Send(adapter, (const muiAccessNode*[]){&s_built.nodes[0]}, 1, s_built.children, 8) &&
              PostedAre(@"layout 8"),
          "none: the focus back");
    // A modal dialog 12 added under the root, then taken away.
    muiAccessNode root = s_built.nodes[0];
    const uint64_t more[7] = {2, 3, 5, 7, 8, 10, 12};
    root.firstChild = 0;
    root.childCount = 7;
    muiAccessNode dialog = {.id = 12,
                            .role = mui_roleDialog,
                            .flags = mui_accessModal,
                            .bounds = {0, 0, 100, 100},
                            .transform = {1, 0, 0, 1, 0, 0}};
    dialog.text[mui_accessLabel] = "Ask";
    dialog.textLength[mui_accessLabel] = 3;
    CHECK(Send(adapter, (const muiAccessNode*[]){&root, &dialog}, 2, more, 0) &&
              PostedAre(@"screen 12"),
          "a modal node: a new screen, at it");
    root.childCount = 6;
    CHECK(Send(adapter, (const muiAccessNode*[]){&root}, 1, more, 0) && PostedAre(@"layout nil"),
          "a node gone: the layout");
    [s_posted release];
    muiDestroyUikitAdapter(adapter);
}

// An input delegate writing down what it is told.
@interface TestTextDelegate : NSObject <UITextInputDelegate>
{
  @public
    NSMutableString* told;
}
@end

@implementation TestTextDelegate

- (void)selectionWillChange:(id<UITextInput>)textInput
{
    (void)textInput;
    [told appendString:@"selection will; "];
}

- (void)selectionDidChange:(id<UITextInput>)textInput
{
    (void)textInput;
    [told appendString:@"selection did; "];
}

- (void)textWillChange:(id<UITextInput>)textInput
{
    (void)textInput;
    [told appendString:@"text will; "];
}

- (void)textDidChange:(id<UITextInput>)textInput
{
    (void)textInput;
    [told appendString:@"text did; "];
}

@end

static NSInteger IndexOf(id<UITextInput> field, UITextPosition* position)
{
    return position != nil
               ? [field offsetFromPosition:[field beginningOfDocument] toPosition:position]
               : -1;
}

static UITextPosition* At(id<UITextInput> field, NSInteger index)
{
    return [field positionFromPosition:[field beginningOfDocument] offset:index];
}

static NSString* TextOf(id<UITextInput> field, UITextRange* range)
{
    return range != nil ? [field textInRange:range] : nil;
}

static void TestTextReading(id<UITextInput> field)
{
    UITextRange* all = [field textRangeFromPosition:[field beginningOfDocument]
                                         toPosition:[field endOfDocument]];
    UITextRange* selected = [field selectedTextRange];
    CHECK([TextOf(field, all) isEqualToString:@"h\u00e9llo \U0001D11E\nnext"] &&
              IndexOf(field, [field endOfDocument]) == 13 && [field hasText] &&
              [selected isEmpty] && IndexOf(field, [selected start]) == 2 && At(field, 13) != nil &&
              At(field, 14) == nil && ![field isSecureTextEntry],
          "a text input's text in UTF-16, its caret");
    id<UITextInputTokenizer> words = [field tokenizer];
    CHECK([TextOf(field, [words rangeEnclosingPosition:At(field, 2)
                                       withGranularity:UITextGranularityWord
                                           inDirection:UITextStorageDirectionForward])
              isEqualToString:@"h\u00e9llo \U0001D11E\n"] &&
              [TextOf(field, [words rangeEnclosingPosition:At(field, 13)
                                           withGranularity:UITextGranularityWord
                                               inDirection:UITextStorageDirectionBackward])
                  isEqualToString:@"next"] &&
              IndexOf(field, [words positionFromPosition:At(field, 2)
                                              toBoundary:UITextGranularityWord
                                             inDirection:UITextStorageDirectionForward]) == 9 &&
              IndexOf(field, [words positionFromPosition:At(field, 2)
                                              toBoundary:UITextGranularityWord
                                             inDirection:UITextStorageDirectionBackward]) == 0 &&
              [words isPosition:At(field, 9)
                     atBoundary:UITextGranularityWord
                    inDirection:UITextStorageDirectionForward] &&
              ![words isPosition:At(field, 2)
                      atBoundary:UITextGranularityWord
                     inDirection:UITextStorageDirectionForward] &&
              [words isPosition:At(field, 0)
                  withinTextUnit:UITextGranularityWord
                     inDirection:UITextStorageDirectionForward] &&
              ![words isPosition:At(field, 13)
                  withinTextUnit:UITextGranularityWord
                     inDirection:UITextStorageDirectionForward],
          "words by the tree's, a word to the next one's start");
    CHECK([TextOf(field, [words rangeEnclosingPosition:At(field, 0)
                                       withGranularity:UITextGranularitySentence
                                           inDirection:UITextStorageDirectionForward])
              isEqualToString:@"h\u00e9llo \U0001D11E\n"] &&
              [TextOf(field, [words rangeEnclosingPosition:At(field, 9)
                                           withGranularity:UITextGranularityLine
                                               inDirection:UITextStorageDirectionForward])
                  isEqualToString:@"next"] &&
              IndexOf(field, [field positionFromPosition:At(field, 2)
                                             inDirection:UITextLayoutDirectionDown
                                                  offset:1]) == 11 &&
              IndexOf(field, [field positionFromPosition:At(field, 11)
                                             inDirection:UITextLayoutDirectionUp
                                                  offset:1]) == 2 &&
              [field positionFromPosition:At(field, 11)
                              inDirection:UITextLayoutDirectionDown
                                   offset:1] == nil &&
              [TextOf(field, [field characterRangeByExtendingPosition:At(field, 6)
                                                          inDirection:UITextLayoutDirectionRight])
                  isEqualToString:@"\U0001D11E"],
          "sentences as paragraphs, lines, a surrogate pair whole");
}

static void TestTextRequests(id<UITextInput> field)
{
    s_asked = (muiAccessRequest){0};
    [field setSelectedTextRange:[field textRangeFromPosition:At(field, 1) toPosition:At(field, 6)]];
    CHECK(s_asked.action == mui_actionSetSelection && s_asked.target == 7 && s_asked.anchor == 1 &&
              s_asked.focus == 7,
          "a selection asked in bytes");
    [field insertText:@"a"];
    CHECK(s_asked.action == mui_actionReplaceText && s_asked.anchor == 3 && s_asked.focus == 3 &&
              strcmp(s_askedText, "a") == 0,
          "text typed at the caret");
    [field deleteBackward];
    CHECK(s_asked.action == mui_actionReplaceText && s_asked.anchor == 1 && s_asked.focus == 3 &&
              s_askedText[0] == '\0',
          "the character before the caret deleted");
    [field replaceRange:[field textRangeFromPosition:At(field, 0) toPosition:At(field, 1)]
               withText:@"H"];
    CHECK(s_asked.action == mui_actionReplaceText && s_asked.anchor == 0 && s_asked.focus == 1 &&
              strcmp(s_askedText, "H") == 0,
          "a range replaced");
}

// The heading 7 made a text input and back: its element remade each
// way, the layout told.
static void TestText(muiUikitAdapter* adapter, id root, const Built* built)
{
    static const char s_value[] = "h\xC3\xA9llo \xF0\x9D\x84\x9E\nnext";
    static const uint32_t s_lines[2] = {0, 12};
    static const muiAccessWord s_words[2] = {{0, 6}, {12, 16}};
    muiAccessNode input = built->nodes[7];
    input.role = mui_roleTextInput;
    input.text[mui_accessValue] = s_value;
    input.textLength[mui_accessValue] = sizeof s_value - 1;
    input.marks = (muiAccessTextMarks){.anchor = 3,
                                       .focus = 3,
                                       .selected = true,
                                       .lineStarts = s_lines,
                                       .lineCount = 2,
                                       .words = s_words,
                                       .wordCount = 2};
    input.actions = 1u << mui_actionSetSelection | 1u << mui_actionReplaceText;
    muiUikitPostFunction saved = adapter->post;
    adapter->post = Record;
    s_posted = [[NSMutableArray alloc] init];
    id heading = [[[root accessibilityElements] objectAtIndex:4] retain];
    CHECK(Send(adapter, (const muiAccessNode*[]){&input}, 1, built->children, 0) &&
              PostedAre(@"layout nil"),
          "a text input: the layout told");
    id field = [[root accessibilityElements] objectAtIndex:4];
    CHECK(IsElementOf(field, 7) && [field isKindOfClass:[MUIAccessibilityTextElement class]] &&
              [field conformsToProtocol:@protocol(UITextInput)] && field != heading &&
              [heading accessibilityLabel] == nil &&
              [field performSelector:@selector(accessibilityTextInputResponder)] == field,
          "a text element in place of the element");
    [heading release];
    TestTextReading(field);
    TestTextRequests(field);
    TestTextDelegate* delegate = [[TestTextDelegate alloc] init];
    delegate->told = [[NSMutableString alloc] init];
    [field setInputDelegate:delegate];
    input.marks.anchor = 7;
    CHECK(Send(adapter, (const muiAccessNode*[]){&input}, 1, built->children, 0) &&
              PostedAre(@"") && [delegate->told isEqualToString:@"selection will; selection did; "],
          "a selection told to the input delegate");
    [delegate->told release];
    [delegate release];
    CHECK(Send(adapter, (const muiAccessNode*[]){&built->nodes[7]}, 1, built->children, 0) &&
              PostedAre(@"layout nil") &&
              ![[[root accessibilityElements] objectAtIndex:4]
                  isKindOfClass:[MUIAccessibilityTextElement class]],
          "a heading again: an element again");
    [s_posted release];
    s_posted = nil;
    adapter->post = saved;
}

static void TestContract(UIView* view)
{
    muiUikitAdapterDef def = muiDefaultUikitAdapterDef();
    muiUikitAdapter* adapter = NULL;
    def.action = Act;
    CHECK(muiCreateUikitAdapter(&def, &adapter) == mui_errorInvalid && adapter == NULL, "no view");
    def.view = (void*)view;
    def.action = NULL;
    CHECK(muiCreateUikitAdapter(&def, &adapter) == mui_errorInvalid, "no action");
    def.action = Act;
    CHECK(muiCreateUikitAdapter(&def, &adapter) == mui_success &&
              muiUikitAdapter_GetRoot(adapter) == NULL,
          "an empty tree has no root");
    muiDestroyUikitAdapter(adapter);
    CHECK(muiUikitAdapter_Apply(NULL, NULL) == mui_errorInvalid &&
              muiUikitAdapter_GetTree(NULL) == NULL &&
              muiUikitAdapter_SetScale(NULL, 1.0f) == mui_errorInvalid &&
              muiUikitAdapter_GetRoot(NULL) == NULL,
          "NULL arguments");
    muiDestroyUikitAdapter(NULL);
}

static void RunTests(UIView* view)
{
    TestContract(view);
    muiUikitAdapterDef def = muiDefaultUikitAdapterDef();
    def.view = (void*)view;
    def.action = Act;
    muiUikitAdapter* adapter = NULL;
    static Built s_built;
    muiAccessUpdate update = Build(&s_built);
    CHECK(muiCreateUikitAdapter(&def, &adapter) == mui_success &&
              muiUikitAdapter_Apply(adapter, &update) == mui_success,
          "made");
    id root = (id)muiUikitAdapter_GetRoot(adapter);
    TestStructure(root, view);
    TestAttributes(root, view);
    TestActions(root);
    TestNotifications(view);
    TestText(adapter, root, &s_built);
    id button = [[[root accessibilityElements] objectAtIndex:1] retain];
    CHECK(muiUikitAdapter_SetScale(adapter, 2.0f) == mui_success &&
              Same([button accessibilityFrame], UIAccessibilityConvertFrameToScreenCoordinates(
                                                    CGRectMake(40, 40, 200, 80), view)),
          "a scale");
    // The button gone: its object answers nothing.
    muiAccessNode rootNode = s_built.nodes[0];
    rootNode.firstChild = 1;
    rootNode.childCount = 5;
    const muiAccessUpdate gone = {(const muiAccessNode*[]){&rootNode}, 1, s_built.children, 0, 0};
    CHECK(muiUikitAdapter_Apply(adapter, &gone) == mui_success &&
              [[root accessibilityElements] count] == 6 && ![button isAccessibilityElement] &&
              [button accessibilityContainer] == nil && [button accessibilityLabel] == nil &&
              ![button accessibilityActivate],
          "an object whose node went");
    [button release];
    [root retain];
    muiDestroyUikitAdapter(adapter);
    CHECK([[root accessibilityElements] count] == 0 && [root accessibilityContainer] == nil,
          "an object whose adapter went");
    [root release];
}

@interface TestSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, retain) UIWindow* window;
@end

@implementation TestSceneDelegate

- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)options
{
    (void)session;
    (void)options;
    UIWindow* window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
    UIViewController* controller = [[UIViewController alloc] init];
    [window setRootViewController:controller];
    [window makeKeyAndVisible];
    [self setWindow:window];
    [window release];
    UIView* view = [controller view];
    [controller release];
    dispatch_async(dispatch_get_main_queue(), ^{
      RunTests(view);
      printf("result: %d failures\n", s_failures);
    });
}

@end

@interface TestAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation TestAppDelegate

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options
{
    (void)application;
    (void)options;
    UISceneConfiguration* configuration =
        [UISceneConfiguration configurationWithName:@"Default" sessionRole:[session role]];
    [configuration setDelegateClass:[TestSceneDelegate class]];
    return configuration;
}

@end

int main(int argc, char* argv[])
{
    // The runner names the file to write to (tools/run_ios_app.sh).
    const char* out = getenv("MUI_TEST_OUT");
    if (out != NULL && freopen(out, "w", stdout) == NULL)
    {
        return 1;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("main\n");
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([TestAppDelegate class]));
    }
}
