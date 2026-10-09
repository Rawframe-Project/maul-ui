// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UIAccessibility adapter's text element (record mui-0008, research
// 144): a node being edited adopts UITextInput, through which VoiceOver
// reads its text and moves by character, word and line, in UTF-16 as
// UIKit counts, on the boundaries every adapter shares (the tokenizer is
// the adapter's own); the selection and the text are set through the
// host. The keyboard edits the host's view, the first responder: marked
// text is not the element's. Character geometry is not given yet: a
// range's rectangle is the node's, a point the text's start.

#include "access_record.h"
#include "access_text.h"
#include "uikit.h"

#include <string.h>

// A position: a UTF-16 offset into the value text.
@interface MUITextPosition : UITextPosition
{
  @public
    NSUInteger index;
}
@end

@implementation MUITextPosition
@end

// A range between two positions, in order.
@interface MUITextRange : UITextRange
{
  @public
    NSUInteger from;
    NSUInteger to;
}
@end

static MUITextPosition* PositionAt(NSUInteger index)
{
    MUITextPosition* position = [[[MUITextPosition alloc] init] autorelease];
    position->index = index;
    return position;
}

static MUITextRange* RangeFrom(NSUInteger from, NSUInteger to)
{
    MUITextRange* range = [[[MUITextRange alloc] init] autorelease];
    range->from = from < to ? from : to;
    range->to = from < to ? to : from;
    return range;
}

@implementation MUITextRange

- (UITextPosition*)start
{
    return PositionAt(from);
}

- (UITextPosition*)end
{
    return PositionAt(to);
}

- (BOOL)isEmpty
{
    return from == to;
}

@end

// The tokenizer: the units of the element's text by the shared
// boundaries; the element holds it, and it the element unretained.
@interface MUITextTokenizer : NSObject <UITextInputTokenizer>
{
  @public
    MUIAccessibilityTextElement* element;
}
@end

static const muiAccessNode* TextNodeOf(const MUIAccessibilityElement* element)
{
    const muiAccessNode* node = element->adapter != nullptr
                                    ? muiAccessTree_Find(element->adapter->tree, element->nodeId)
                                    : nullptr;
    return node != nullptr && muiAccessIsEdited(node) ? node : nullptr;
}

static NSUInteger UnitsBefore(const muiAccessText* text, uint32_t byte)
{
    return muiAccessUtf16Before(text->bytes, byte);
}

// A UTF-16 offset's byte, clamped to the text, at a character's start.
static uint32_t ByteAt(const muiAccessText* text, NSUInteger units)
{
    return muiAccessByteOfUtf16(text, (uint32_t)MIN(units, (NSUInteger)UINT32_MAX));
}

// A position's byte; false for one not the adapter's.
static bool ByteOf(const muiAccessText* text, UITextPosition* position, uint32_t* byteOut)
{
    if (![position isKindOfClass:[MUITextPosition class]])
    {
        return false;
    }
    *byteOut = ByteAt(text, ((MUITextPosition*)position)->index);
    return true;
}

static bool BytesOf(const muiAccessText* text, UITextRange* range, uint32_t* startOut,
                    uint32_t* endOut)
{
    if (![range isKindOfClass:[MUITextRange class]])
    {
        return false;
    }
    *startOut = ByteAt(text, ((MUITextRange*)range)->from);
    *endOut = ByteAt(text, ((MUITextRange*)range)->to);
    return true;
}

static MUITextRange* RangeOfBytes(const muiAccessText* text, uint32_t start, uint32_t end)
{
    return RangeFrom(UnitsBefore(text, start), UnitsBefore(text, end));
}

static muiAccessUnit UnitOf(UITextGranularity granularity)
{
    switch (granularity)
    {
    case UITextGranularityCharacter:
        return mui_unitCharacter;
    case UITextGranularityWord:
        return mui_unitWord;
    case UITextGranularityLine:
        return mui_unitLine;
    // The tree carries no sentences: the next larger unit.
    case UITextGranularitySentence:
    case UITextGranularityParagraph:
        return mui_unitParagraph;
    case UITextGranularityDocument:
        return mui_unitDocument;
    }
    return mui_unitDocument;
}

// Forward, right and down go after a position; backward, left and up
// before it.
static bool Onward(UITextDirection direction)
{
    return direction == (UITextDirection)UITextStorageDirectionForward ||
           direction == (UITextDirection)UITextLayoutDirectionRight ||
           direction == (UITextDirection)UITextLayoutDirectionDown;
}

// Whether a byte is a boundary of a unit: a unit runs to the next one's
// start, so one either way.
static bool IsBoundary(const muiAccessText* text, muiAccessUnit unit, uint32_t at)
{
    return at == 0 || at == text->length ||
           muiAccessBoundaryAfter(text, unit, muiAccessBoundaryBefore(text, unit, at)) == at;
}

// The character on a side of a byte, by its start; false for none, or
// for one in no unit: before the first word, or any word when none are.
static bool CharacterBeside(const muiAccessText* text, muiAccessUnit unit, uint32_t at, bool onward,
                            uint32_t* startOut)
{
    if (onward ? at >= text->length : at == 0)
    {
        return false;
    }
    *startOut = onward ? at : muiAccessBoundaryBefore(text, mui_unitCharacter, at);
    const muiAccessTextMarks* marks = text->marks;
    return unit != mui_unitWord || (marks->wordCount != 0 && *startOut >= marks->words[0].start);
}

@implementation MUITextTokenizer

- (UITextRange*)rangeEnclosingPosition:(UITextPosition*)position
                       withGranularity:(UITextGranularity)granularity
                           inDirection:(UITextDirection)direction
{
    const muiAccessNode* node = element != nil ? TextNodeOf(element) : nullptr;
    uint32_t at = 0;
    uint32_t character = 0;
    if (node == nullptr)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    muiAccessUnit unit = UnitOf(granularity);
    if (!ByteOf(&text, position, &at) ||
        !CharacterBeside(&text, unit, at, Onward(direction), &character))
    {
        return nil;
    }
    uint32_t start = IsBoundary(&text, unit, character)
                         ? character
                         : muiAccessBoundaryBefore(&text, unit, character);
    return RangeOfBytes(&text, start, muiAccessBoundaryAfter(&text, unit, start));
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position
                             toBoundary:(UITextGranularity)granularity
                            inDirection:(UITextDirection)direction
{
    const muiAccessNode* node = element != nil ? TextNodeOf(element) : nullptr;
    uint32_t at = 0;
    if (node == nullptr)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    bool onward = Onward(direction);
    if (!ByteOf(&text, position, &at) || (onward ? at == text.length : at == 0))
    {
        return nil;
    }
    muiAccessUnit unit = UnitOf(granularity);
    uint32_t next =
        onward ? muiAccessBoundaryAfter(&text, unit, at) : muiAccessBoundaryBefore(&text, unit, at);
    return PositionAt(UnitsBefore(&text, next));
}

- (BOOL)isPosition:(UITextPosition*)position
        atBoundary:(UITextGranularity)granularity
       inDirection:(UITextDirection)direction
{
    (void)direction;
    const muiAccessNode* node = element != nil ? TextNodeOf(element) : nullptr;
    uint32_t at = 0;
    if (node == nullptr)
    {
        return NO;
    }
    muiAccessText text = muiAccessValueOf(node);
    return ByteOf(&text, position, &at) && IsBoundary(&text, UnitOf(granularity), at);
}

- (BOOL)isPosition:(UITextPosition*)position
    withinTextUnit:(UITextGranularity)granularity
       inDirection:(UITextDirection)direction
{
    const muiAccessNode* node = element != nil ? TextNodeOf(element) : nullptr;
    uint32_t at = 0;
    uint32_t character = 0;
    if (node == nullptr)
    {
        return NO;
    }
    muiAccessText text = muiAccessValueOf(node);
    return ByteOf(&text, position, &at) &&
           CharacterBeside(&text, UnitOf(granularity), at, Onward(direction), &character);
}

@end

static bool Has(const muiAccessNode* node, muiAccessAction action)
{
    return (node->actions & (1u << action)) != 0;
}

// Asks the host to put text in place of a byte range; whether it did.
static bool Replace(const MUIAccessibilityElement* element, const muiAccessNode* node,
                    uint32_t start, uint32_t end, NSString* value)
{
    const char* bytes = [value UTF8String];
    if (bytes == nullptr || !Has(node, mui_actionReplaceText))
    {
        return false;
    }
    const muiAccessRequest request = {.action = mui_actionReplaceText,
                                      .target = node->id,
                                      .anchor = start,
                                      .focus = end,
                                      .text = bytes,
                                      .length = (uint32_t)strlen(bytes)};
    return element->adapter->action(element->adapter->user, &request);
}

// The selection's bytes, in order.
static void SelectionOf(const muiAccessNode* node, uint32_t* startOut, uint32_t* endOut)
{
    uint32_t anchor = node->marks.anchor;
    uint32_t focus = node->marks.focus;
    *startOut = anchor < focus ? anchor : focus;
    *endOut = anchor < focus ? focus : anchor;
}

// The line a byte is on, by the lines' starts; 0 when none are given.
static uint32_t LineOf(const muiAccessTextMarks* marks, uint32_t at)
{
    uint32_t line = 0;
    for (uint32_t i = 1; i < marks->lineCount && marks->lineStarts[i] <= at; i++)
    {
        line = i;
    }
    return line;
}

// A line's first byte and the byte of its last place: the next line's
// first character's start but for the last line, whose is the text's end.
static void LineBytes(const muiAccessText* text, uint32_t line, uint32_t* startOut,
                      uint32_t* lastOut)
{
    const muiAccessTextMarks* marks = text->marks;
    *startOut = marks->lineCount != 0 ? marks->lineStarts[line] : 0;
    *lastOut = line + 1 < marks->lineCount
                   ? muiAccessBoundaryBefore(text, mui_unitCharacter, marks->lineStarts[line + 1])
                   : text->length;
}

@implementation MUIAccessibilityTextElement

- (void)dealloc
{
    if (tokenizer != nil)
    {
        tokenizer->element = nil;
    }
    [tokenizer release];
    [inputDelegate release];
    [super dealloc];
}

- (NSString*)textInRange:(UITextRange*)range
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t start = 0;
    uint32_t end = 0;
    if (node == nullptr)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    if (!BytesOf(&text, range, &start, &end))
    {
        return nil;
    }
    NSString* string = [[NSString alloc] initWithBytes:text.bytes + start
                                                length:end - start
                                              encoding:NSUTF8StringEncoding];
    return [string autorelease];
}

- (void)replaceRange:(UITextRange*)range withText:(NSString*)value
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t start = 0;
    uint32_t end = 0;
    if (node == nullptr)
    {
        return;
    }
    muiAccessText text = muiAccessValueOf(node);
    if (BytesOf(&text, range, &start, &end))
    {
        (void)Replace(self, node, start, end, value);
    }
}

- (BOOL)shouldChangeTextInRange:(UITextRange*)range replacementText:(NSString*)value
{
    (void)range;
    (void)value;
    const muiAccessNode* node = TextNodeOf(self);
    return node != nullptr && Has(node, mui_actionReplaceText);
}

// The selection; at the caret when nothing is selected; none when the
// text is not being edited.
- (UITextRange*)selectedTextRange
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr || !node->marks.selected)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    uint32_t start = 0;
    uint32_t end = 0;
    SelectionOf(node, &start, &end);
    return RangeOfBytes(&text, start, end);
}

- (void)setSelectedTextRange:(UITextRange*)range
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t start = 0;
    uint32_t end = 0;
    if (node == nullptr || !Has(node, mui_actionSetSelection))
    {
        return;
    }
    muiAccessText text = muiAccessValueOf(node);
    if (BytesOf(&text, range, &start, &end))
    {
        const muiAccessRequest request = {
            .action = mui_actionSetSelection, .target = node->id, .anchor = start, .focus = end};
        (void)adapter->action(adapter->user, &request);
    }
}

- (UITextRange*)markedTextRange
{
    return nil;
}

- (NSDictionary*)markedTextStyle
{
    return nil;
}

- (void)setMarkedTextStyle:(NSDictionary*)style
{
    (void)style;
}

- (void)setMarkedText:(NSString*)markedText selectedRange:(NSRange)selectedRange
{
    (void)markedText;
    (void)selectedRange;
}

- (void)unmarkText
{
}

- (UITextPosition*)beginningOfDocument
{
    return PositionAt(0);
}

- (UITextPosition*)endOfDocument
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr)
    {
        return PositionAt(0);
    }
    muiAccessText text = muiAccessValueOf(node);
    return PositionAt(UnitsBefore(&text, text.length));
}

- (UITextRange*)textRangeFromPosition:(UITextPosition*)fromPosition
                           toPosition:(UITextPosition*)toPosition
{
    if (![fromPosition isKindOfClass:[MUITextPosition class]] ||
        ![toPosition isKindOfClass:[MUITextPosition class]])
    {
        return nil;
    }
    return RangeFrom(((MUITextPosition*)fromPosition)->index,
                     ((MUITextPosition*)toPosition)->index);
}

// A position moved by UTF-16 units; none past either end.
- (UITextPosition*)positionFromPosition:(UITextPosition*)position offset:(NSInteger)offset
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr || ![position isKindOfClass:[MUITextPosition class]])
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    NSInteger index = (NSInteger)((MUITextPosition*)position)->index;
    NSInteger units = (NSInteger)UnitsBefore(&text, text.length);
    if (offset < -index || offset > units - index)
    {
        return nil;
    }
    return PositionAt((NSUInteger)(index + offset));
}

// Left and right by UTF-16 units; up and down by lines, at the same
// count of units into the line, or its end.
- (UITextPosition*)positionFromPosition:(UITextPosition*)position
                            inDirection:(UITextLayoutDirection)direction
                                 offset:(NSInteger)offset
{
    bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    NSInteger step = back ? -offset : offset;
    if (direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionRight)
    {
        return [self positionFromPosition:position offset:step];
    }
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t at = 0;
    if (node == nullptr)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    if (!ByteOf(&text, position, &at))
    {
        return nil;
    }
    NSInteger lines = text.marks->lineCount != 0 ? (NSInteger)text.marks->lineCount : 1;
    uint32_t line = LineOf(text.marks, at);
    NSInteger target = (NSInteger)line + step;
    if (target < 0 || target >= lines)
    {
        return nil;
    }
    uint32_t start = 0;
    uint32_t last = 0;
    LineBytes(&text, line, &start, &last);
    NSUInteger column = UnitsBefore(&text, at) - UnitsBefore(&text, start);
    LineBytes(&text, (uint32_t)target, &start, &last);
    NSUInteger first = UnitsBefore(&text, start);
    return PositionAt(MIN(first + column, UnitsBefore(&text, last)));
}

- (NSComparisonResult)comparePosition:(UITextPosition*)position toPosition:(UITextPosition*)other
{
    if (![position isKindOfClass:[MUITextPosition class]] ||
        ![other isKindOfClass:[MUITextPosition class]])
    {
        return NSOrderedSame;
    }
    NSUInteger a = ((MUITextPosition*)position)->index;
    NSUInteger b = ((MUITextPosition*)other)->index;
    return a < b ? NSOrderedAscending : a > b ? NSOrderedDescending : NSOrderedSame;
}

- (NSInteger)offsetFromPosition:(UITextPosition*)from toPosition:(UITextPosition*)toPosition
{
    if (![from isKindOfClass:[MUITextPosition class]] ||
        ![toPosition isKindOfClass:[MUITextPosition class]])
    {
        return 0;
    }
    return (NSInteger)((MUITextPosition*)toPosition)->index -
           (NSInteger)((MUITextPosition*)from)->index;
}

- (UITextPosition*)positionWithinRange:(UITextRange*)range
                   farthestInDirection:(UITextLayoutDirection)direction
{
    if (![range isKindOfClass:[MUITextRange class]])
    {
        return nil;
    }
    bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    return back ? [range start] : [range end];
}

- (UITextRange*)characterRangeByExtendingPosition:(UITextPosition*)position
                                      inDirection:(UITextLayoutDirection)direction
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t at = 0;
    if (node == nullptr)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    if (!ByteOf(&text, position, &at))
    {
        return nil;
    }
    bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    uint32_t other = back ? muiAccessBoundaryBefore(&text, mui_unitCharacter, at)
                          : muiAccessBoundaryAfter(&text, mui_unitCharacter, at);
    return RangeOfBytes(&text, back ? other : at, back ? at : other);
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition*)position
                                          inDirection:(UITextStorageDirection)direction
{
    (void)position;
    (void)direction;
    return NSWritingDirectionNatural;
}

- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange*)range
{
    (void)writingDirection;
    (void)range;
}

// Without character geometry, a range is the node's rectangle in the
// view.
- (CGRect)firstRectForRange:(UITextRange*)range
{
    (void)range;
    return TextNodeOf(self) != nullptr ? muiUikitViewRectOf(adapter, nodeId) : CGRectNull;
}

- (CGRect)caretRectForPosition:(UITextPosition*)position
{
    (void)position;
    return TextNodeOf(self) != nullptr ? muiUikitViewRectOf(adapter, nodeId) : CGRectNull;
}

- (NSArray*)selectionRectsForRange:(UITextRange*)range
{
    (void)range;
    return @[];
}

// Without character geometry, a point is the text's start.
- (UITextPosition*)closestPositionToPoint:(CGPoint)point
{
    (void)point;
    return TextNodeOf(self) != nullptr ? PositionAt(0) : nil;
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange*)range
{
    (void)point;
    return TextNodeOf(self) != nullptr && [range isKindOfClass:[MUITextRange class]] ? [range start]
                                                                                     : nil;
}

- (UITextRange*)characterRangeAtPoint:(CGPoint)point
{
    (void)point;
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    return RangeOfBytes(&text, 0, muiAccessBoundaryAfter(&text, mui_unitCharacter, 0));
}

- (id<UITextInputDelegate>)inputDelegate
{
    return inputDelegate;
}

// Held, not weak as UIKit's own: the adapter's code has no weak
// references, and the delegate does not hold the element.
- (void)setInputDelegate:(id<UITextInputDelegate>)delegate
{
    [delegate retain];
    [inputDelegate release];
    inputDelegate = delegate;
}

- (id<UITextInputTokenizer>)tokenizer
{
    if (tokenizer == nil)
    {
        tokenizer = [[MUITextTokenizer alloc] init];
        tokenizer->element = self;
    }
    return tokenizer;
}

- (UIView*)textInputView
{
    return adapter != nullptr ? adapter->view : nil;
}

- (BOOL)hasText
{
    const muiAccessNode* node = TextNodeOf(self);
    return node != nullptr && muiAccessValueOf(node).length != 0;
}

// Text typed in place of the selection.
- (void)insertText:(NSString*)value
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t start = 0;
    uint32_t end = 0;
    if (node != nullptr && node->marks.selected)
    {
        SelectionOf(node, &start, &end);
        (void)Replace(self, node, start, end, value);
    }
}

// The selection deleted, or the character before the caret.
- (void)deleteBackward
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t start = 0;
    uint32_t end = 0;
    if (node == nullptr || !node->marks.selected)
    {
        return;
    }
    SelectionOf(node, &start, &end);
    muiAccessText text = muiAccessValueOf(node);
    if (start == end)
    {
        start = muiAccessBoundaryBefore(&text, mui_unitCharacter, end);
    }
    if (start != end)
    {
        (void)Replace(self, node, start, end, @"");
    }
}

- (BOOL)isSecureTextEntry
{
    const muiAccessNode* node = TextNodeOf(self);
    return node != nullptr && node->role == mui_rolePasswordInput;
}

// The object text calls go to (iOS 18.1): the element itself.
- (id<UITextInput>)accessibilityTextInputResponder
{
    return self;
}

@end

void muiUikitTellText(MUIAccessibilityTextElement* element, const muiAccessNode* old,
                      const muiAccessNode* node)
{
    id<UITextInputDelegate> delegate = element->inputDelegate;
    if (delegate == nil)
    {
        return;
    }
    bool selection = old->marks.selected != node->marks.selected ||
                     old->marks.anchor != node->marks.anchor ||
                     old->marks.focus != node->marks.focus;
    bool text = muiRecordTextDiffers(old, node, mui_accessValue);
    if (text)
    {
        [delegate textWillChange:element];
        [delegate textDidChange:element];
    }
    if (selection)
    {
        [delegate selectionWillChange:element];
        [delegate selectionDidChange:element];
    }
}
