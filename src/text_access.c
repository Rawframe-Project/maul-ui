// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a text block reads as to accessibility (records mui-0006 and
// mui-0008, I123): its text as shown, its editing selection, its lines
// and its grapheme clusters where painting lays them out, and its words;
// and the requests that set the selection and replace text, in the
// offsets it reads as.

#include "allocator.h"
#include "text_block.h"
#include "text_blocks.h"
#include "text_boxes.h"
#include "text_editing.h"
#include "text_mask.h"
#include "text_service.h"

#include "maul-ui/access.h"
#include "maul-ui/layout.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"
#include "maul-unicode/segment.h"

// A line's clusters, after those kept, in the node's own space: the
// content box's place added; false when memory runs out.
static bool ReadClusters(const muiLaidText* laid, uint32_t index, muiRect content,
                         muiTextBlock* block, uint32_t* countInOut)
{
    muiTextBoxes boxes;
    muiTextService* service = laid->paragraph.service;
    uint32_t kept = *countInOut;
    if (!muiGetLineBoxes(laid, index, &boxes) ||
        !muiReserveKeeping(&service->allocator, &block->accessClusters,
                           (kept + boxes.count) * sizeof(muiAccessCluster),
                           kept * sizeof(muiAccessCluster)))
    {
        return false;
    }
    muiAccessCluster* clusters = block->accessClusters.data;
    for (uint32_t i = 0; i < boxes.count; i++)
    {
        const muiTextBox* box = &boxes.data[i];
        clusters[kept + i] =
            (muiAccessCluster){box->start, box->end, content.x + box->x0, content.x + box->x1};
    }
    *countInOut = kept + boxes.count;
    return true;
}

// The most clusters read of a text, so that a long one costs what is
// near the screen.
#define MOST_CLUSTERS 16384u

// Marks which lines' clusters to leave out: all but those within a
// shown height of shown, those overlapping shown first, then the rest
// top down, while their bytes, which bound their clusters, stay within
// MOST_CLUSTERS.
static void OmitFar(const muiLaidText* laid, muiRect content, muiRect shown,
                    muiAccessLineBox* lineBoxes)
{
    float nearTop = shown.y - shown.height;
    float nearBottom = shown.y + 2.0f * shown.height;
    uint32_t budget = MOST_CLUSTERS;
    for (uint32_t i = 0; i < laid->lineCount; i++)
    {
        lineBoxes[i].omitted = true;
    }
    for (int pass = 0; pass < 2; pass++)
    {
        float from = pass == 0 ? shown.y : nearTop;
        float to = pass == 0 ? shown.y + shown.height : nearBottom;
        for (uint32_t i = 0; i < laid->lineCount; i++)
        {
            const muiTextLine* line = &laid->lines[i];
            float top = content.y + line->top;
            uint32_t bytes = line->end - line->start;
            if (lineBoxes[i].omitted && top + line->height >= from && top <= to && bytes <= budget)
            {
                lineBoxes[i].omitted = false;
                budget -= bytes;
            }
        }
    }
}

// Where the lines of a node's text start and where the clusters of those
// near what is shown are, as painting lays them out in its content box,
// kept in the block; false when they cannot be laid out.
static bool ReadLines(const muiTextHost* host, muiNodeId nodeId, muiRect shown, muiTextBlock* block,
                      muiAccessTextMarks* marks)
{
    muiRect content = muiNode_GetContentRect(host->context, nodeId);
    muiLaidText laid;
    muiAllocator* allocator = &host->service->allocator;
    if (muiLayText(host, nodeId, content.width, &laid) != mui_success || laid.lineCount == 0 ||
        !muiReserve(allocator, &block->accessLines, laid.lineCount * sizeof(uint32_t)) ||
        !muiReserve(allocator, &block->accessLineBoxes, laid.lineCount * sizeof(muiAccessLineBox)))
    {
        return false;
    }
    uint32_t* starts = block->accessLines.data;
    muiAccessLineBox* lineBoxes = block->accessLineBoxes.data;
    OmitFar(&laid, content, shown, lineBoxes);
    uint32_t clusterCount = 0;
    for (uint32_t i = 0; i < laid.lineCount; i++)
    {
        const muiTextLine* line = &laid.lines[i];
        starts[i] = line->start;
        lineBoxes[i].top = content.y + line->top;
        lineBoxes[i].bottom = content.y + line->top + line->height;
        lineBoxes[i].firstCluster = clusterCount;
        if (!lineBoxes[i].omitted && !ReadClusters(&laid, i, content, block, &clusterCount))
        {
            return false;
        }
    }
    marks->lineStarts = starts;
    marks->lineCount = laid.lineCount;
    marks->lineBoxes = lineBoxes;
    marks->clusters = clusterCount != 0 ? block->accessClusters.data : nullptr;
    marks->clusterCount = clusterCount;
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

bool muiAccessTextOf(void* user, muiNodeId nodeId, uint64_t hostKey, const muiRect* shown,
                     muiAccessContent* contentOut)
{
    const muiTextHost* host = user;
    const muiTextBlockId blockId = {(uint32_t)hostKey, (uint32_t)(hostKey >> 32)};
    muiTextBlock* block = host != nullptr && host->service != nullptr
                              ? muiResolveTextBlock(host->service, blockId)
                              : nullptr;
    // A password reads as its mask.
    const muiTextBlock* read = block != nullptr ? muiShownBlock(host->service, block) : nullptr;
    if (read == nullptr || contentOut == nullptr)
    {
        return false;
    }
    // An empty block may have no buffer yet.
    const char* text = read->text.data != nullptr ? read->text.data : "";
    *contentOut = (muiAccessContent){.text = text, .length = read->length};
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
    if (shown != nullptr && !ReadLines(host, nodeId, *shown, block, marks))
    {
        *marks = (muiAccessTextMarks){
            .anchor = marks->anchor, .focus = marks->focus, .selected = marks->selected};
    }
    if (shown != nullptr && !muiIsMasked(block) && !ReadWords(host->service, block, marks))
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
