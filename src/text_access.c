// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a text block reads as to accessibility (records mui-0006 and
// mui-0008, I123): its text as shown, its editing selection, its lines as
// painting breaks them, and its words; and the requests that set the
// selection and replace text, in the offsets it reads as.

#include "allocator.h"
#include "text_block.h"
#include "text_blocks.h"
#include "text_editing.h"
#include "text_lines.h"
#include "text_mask.h"
#include "text_paragraph.h"
#include "text_service.h"

#include "maul-ui/access.h"
#include "maul-ui/layout.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"
#include "maul-unicode/segment.h"

// Where the lines of a node's text start as painting breaks them at its
// content box's width, kept in the block; false when they cannot be laid
// out.
static bool ReadLines(const muiTextHost* host, muiNodeId nodeId, uint64_t hostKey,
                      muiTextBlock* block, muiAccessTextMarks* marks)
{
    muiParagraph paragraph;
    if (!muiPrepareParagraph(host, nodeId, hostKey, &paragraph))
    {
        return false;
    }
    float width = muiNode_GetContentRect(host->context, nodeId).width;
    uint32_t count = 0;
    if (!muiLayLines(&paragraph, muiParagraphBreakMode(&paragraph, mui_measureAtMost), width,
                     &count) ||
        count == 0 ||
        !muiReserve(&host->service->allocator, &block->accessLines, count * sizeof(uint32_t)))
    {
        return false;
    }
    const muiTextLine* lines = host->service->lines.data;
    uint32_t* starts = block->accessLines.data;
    for (uint32_t i = 0; i < count; i++)
    {
        starts[i] = lines[i].start;
    }
    marks->lineStarts = starts;
    marks->lineCount = count;
    return true;
}

// A text's words, the word segments with a letter or a number, kept in
// the block; false when memory runs out.
static bool ReadWords(muiTextService* service, muiTextBlock* block, muiAccessTextMarks* marks)
{
    const char* text = block->text.data;
    muniSegmentIterator iterator;
    if (block->length == 0 ||
        muniInitWordIterator(&iterator, text, block->length, false) != muni_success)
    {
        return true;
    }
    uint32_t count = 0;
    size_t start = 0;
    size_t end = 0;
    while (muniNextSegmentBreak(&iterator, &end) == muni_success)
    {
        if (end > start && muiIsWordSegment(text, start, end))
        {
            if (!muiReserveKeeping(&service->allocator, &block->accessWords,
                                   (count + 1) * sizeof(muiAccessWord),
                                   count * sizeof(muiAccessWord)))
            {
                return false;
            }
            ((muiAccessWord*)block->accessWords.data)[count++] =
                (muiAccessWord){(uint32_t)start, (uint32_t)end};
        }
        start = end;
    }
    marks->words = count != 0 ? block->accessWords.data : nullptr;
    marks->wordCount = count;
    return true;
}

bool muiAccessTextOf(void* user, muiNodeId nodeId, uint64_t hostKey, bool boundaries,
                     muiAccessContent* contentOut)
{
    const muiTextHost* host = user;
    const muiTextBlockId blockId = {(uint32_t)hostKey, (uint32_t)(hostKey >> 32)};
    muiTextBlock* block = host != nullptr && host->service != nullptr
                              ? muiResolveTextBlock(host->service, blockId)
                              : nullptr;
    // A password reads as its mask.
    const muiTextBlock* shown = block != nullptr ? muiShownBlock(host->service, block) : nullptr;
    if (shown == nullptr || contentOut == nullptr)
    {
        return false;
    }
    // An empty block may have no buffer yet.
    const char* text = shown->text.data != nullptr ? shown->text.data : "";
    *contentOut = (muiAccessContent){.text = text, .length = shown->length};
    muiAccessTextMarks* marks = &contentOut->marks;
    if (block->editing.on)
    {
        const muiTextSelection* selection = &block->editing.selection;
        marks->anchor = muiMaskOffset(block, selection->anchor);
        marks->focus = muiMaskOffset(block, selection->caret.offset);
        marks->selected = true;
    }
    // Lines and words left out when they cannot be read: the text is
    // still read. A password's bullets are no words.
    if (boundaries && !ReadLines(host, nodeId, hostKey, block, marks))
    {
        *marks = (muiAccessTextMarks){
            .anchor = marks->anchor, .focus = marks->focus, .selected = marks->selected};
    }
    if (boundaries && !muiIsMasked(block) && !ReadWords(host->service, block, marks))
    {
        marks->words = nullptr;
        marks->wordCount = 0;
    }
    return true;
}

// Whether an offset of the text as shown lies at a character's start.
static bool IsShownStart(const muiTextBlock* shown, uint32_t offset)
{
    const unsigned char* text = shown->text.data;
    return offset == shown->length || (offset < shown->length && (text[offset] & 0xC0u) != 0x80u);
}

muiResult muiTextPerformAccessAction(const muiTextHost* host, const muiAccessRequest* request,
                                     muiTextEditOutcome* outcomeOut)
{
    if (host == nullptr || host->service == nullptr || host->context == nullptr ||
        request == nullptr || outcomeOut == nullptr)
    {
        return muiRefuseEdit(host != nullptr ? host->service : nullptr);
    }
    *outcomeOut = (muiTextEditOutcome){false, false, false, false};
    if (request->action != mui_actionSetSelection && request->action != mui_actionReplaceText)
    {
        return mui_empty;
    }
    muiNodeId nodeId = muiNodeIdOfAccess(request->target);
    uint64_t key = muiNode_GetHostKey(host->context, nodeId);
    const muiTextBlockId blockId = {(uint32_t)key, (uint32_t)(key >> 32)};
    muiTextBlock* block = key != 0 ? muiResolveTextBlock(host->service, blockId) : nullptr;
    if (block == nullptr)
    {
        return mui_errorStale;
    }
    const muiTextBlock* shown = muiShownBlock(host->service, block);
    if (shown == nullptr)
    {
        return mui_errorCapacity;
    }
    if (!block->editing.on || !IsShownStart(shown, request->anchor) ||
        !IsShownStart(shown, request->focus) ||
        (request->action == mui_actionReplaceText && request->text == nullptr &&
         request->length != 0))
    {
        return muiRefuseEdit(host->service);
    }
    const muiTextSelection before = block->editing.selection;
    const muiTextSelection selection = {
        muiUnmaskOffset(block, request->anchor),
        {muiUnmaskOffset(block, request->focus), mui_affinityDownstream}};
    muiTextEditOutcome outcome = {true, false, false, false};
    muiResult result = muiTextBlock_Select(host->service, blockId, selection);
    // Text replaced goes in as a paste does: under the block's rules, an
    // edit undone alone.
    if (result == mui_success && request->action == mui_actionReplaceText)
    {
        result = muiTextBlock_Paste(host->service, blockId, request->text, request->length,
                                    &outcome.changed);
    }
    const muiTextSelection* after = &block->editing.selection;
    outcome.selected = after->anchor != before.anchor ||
                       after->caret.offset != before.caret.offset ||
                       after->caret.affinity != before.caret.affinity;
    *outcomeOut = outcome;
    return result;
}
