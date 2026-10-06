// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hit testing laid-out text (record mui-0006): points to positions,
// positions to carets, and ranges to rectangles, over a node's lines read
// as boxes of grapheme clusters.

#include "text_boxes.h"

#include "maul-ui/text_edit.h"

#include <math.h>

muiResult muiTextHitTest(const muiTextHost* host, muiNodeId nodeId, float width, float x, float y,
                         muiTextPosition* positionOut)
{
    if (!muiIsTextHostValid(host) || positionOut == nullptr || !isfinite(x) || !isfinite(y))
    {
        return mui_errorInvalid;
    }
    muiLaidText laid;
    muiResult result = muiLayText(host, nodeId, width, &laid);
    if (result != mui_success)
    {
        return result;
    }
    if (laid.lineCount == 0)
    {
        *positionOut = (muiTextPosition){0, mui_affinityDownstream};
        return mui_success;
    }
    float row = floorf(y / laid.paragraph.lineHeight);
    uint32_t index = !(row >= 0.0f)                 ? 0
                     : row >= (float)laid.lineCount ? laid.lineCount - 1
                                                    : (uint32_t)row;
    return muiHitLine(&laid, index, x, positionOut) ? mui_success : mui_errorCapacity;
}

muiResult muiTextGetCaret(const muiTextHost* host, muiNodeId nodeId, float width,
                          muiTextPosition position, muiTextCaret* caretOut)
{
    if (!muiIsTextHostValid(host) || caretOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiLaidText laid;
    muiResult result = muiLayText(host, nodeId, width, &laid);
    if (result != mui_success)
    {
        return result;
    }
    const muiParagraph* paragraph = &laid.paragraph;
    bool rtl = paragraph->rtl;
    float start = muiAlignLine(paragraph, 0.0f, width);
    if (laid.lineCount == 0)
    {
        *caretOut = (muiTextCaret){start, 0.0f, paragraph->lineHeight, rtl};
        return mui_success;
    }
    uint32_t index = muiLineOfPosition(&laid, position);
    float x = 0.0f;
    bool runRtl = false;
    if (!muiCaretX(&laid, index, position, &x, &runRtl))
    {
        return mui_errorCapacity;
    }
    *caretOut =
        (muiTextCaret){x, (float)index * paragraph->lineHeight, paragraph->lineHeight, runRtl};
    return mui_success;
}

// Adds a rectangle for each stretch of a line's boxes from start up to
// end, those past capacity counted only; returns the count after them.
static uint32_t AddStretches(const muiTextBoxes* boxes, uint32_t start, uint32_t end, float top,
                             float height, muiRect* rects, uint32_t capacity, uint32_t count)
{
    bool open = false;
    for (uint32_t i = 0; i < boxes->count; i++)
    {
        const muiTextBox* box = &boxes->data[i];
        bool inside = box->start >= start && box->end <= end;
        if (inside && !open)
        {
            count++;
        }
        if (inside && count <= capacity)
        {
            muiRect* rect = &rects[count - 1];
            float left = open ? rect->x : box->x0;
            *rect = (muiRect){left, top, box->x1 - left, height};
        }
        open = inside;
    }
    return count;
}

muiResult muiTextGetRangeRects(const muiTextHost* host, muiNodeId nodeId, float width,
                               uint32_t start, uint32_t end, muiRect* rects, uint32_t capacity,
                               uint32_t* countOut)
{
    if (!muiIsTextHostValid(host) || countOut == nullptr || (rects == nullptr && capacity != 0))
    {
        return mui_errorInvalid;
    }
    muiLaidText laid;
    muiResult result = muiLayText(host, nodeId, width, &laid);
    if (result != mui_success)
    {
        return result;
    }
    uint32_t count = 0;
    for (uint32_t index = 0; index < laid.lineCount && start < end; index++)
    {
        const muiTextLine* line = &laid.lines[index];
        if (line->end <= start || line->start >= end)
        {
            continue;
        }
        muiTextBoxes boxes;
        if (!muiGetLineBoxes(&laid, index, &boxes))
        {
            return mui_errorCapacity;
        }
        count = AddStretches(&boxes, start, end, (float)index * laid.paragraph.lineHeight,
                             laid.paragraph.lineHeight, rects, capacity, count);
    }
    *countOut = count;
    return count <= capacity ? mui_success : mui_errorCapacity;
}
