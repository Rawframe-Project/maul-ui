// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a text block reads as to accessibility (records mui-0006 and
// mui-0008, I123): its text as shown, its editing selection, its lines as
// painting breaks them, and its words.

#include "allocator.h"
#include "text_block.h"
#include "text_blocks.h"
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
    *contentOut = (muiAccessContent){.text = shown->text.data, .length = shown->length};
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
