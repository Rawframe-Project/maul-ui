// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hit testing laid-out text (record mui-0006): each line laid out as
// painting lays it out, as boxes of grapheme clusters left to right, a
// glyph several clusters share cut into equal parts; positions are box
// edges.

#include "text_paragraph.h"

#include "maul-ui/text_edit.h"
#include "maul-unicode/bidi.h"
#include "maul-unicode/segment.h"

#include <math.h>
#include <string.h>

// A grapheme cluster on a line: from x0 to x1, the letter spacing after
// it included, and its bytes from start up to end.
typedef struct Box
{
    float x0;
    float x1;
    uint32_t start;
    uint32_t end;
    bool rtl;
} Box;

// A node's paragraph broken as painting breaks it, at a width.
typedef struct Laid
{
    muiParagraph paragraph;
    const muiTextLine* lines;
    uint32_t lineCount;
    float width;
} Laid;

static muiResult Lay(const muiTextHost* host, muiNodeId nodeId, float width, Laid* out)
{
    uint64_t hostKey = muiNode_GetHostKey(host->context, nodeId);
    uint64_t failures = host->service->failures;
    if (!muiPrepareParagraph(host, nodeId, hostKey, &out->paragraph))
    {
        return host->service->failures != failures ? mui_errorCapacity : mui_errorStale;
    }
    muiParagraph* paragraph = &out->paragraph;
    if (!muiBreakParagraph(paragraph, muiParagraphBreakMode(paragraph, mui_measureAtMost), width,
                           &out->lineCount))
    {
        return mui_errorCapacity;
    }
    out->lines = paragraph->service->lines.data;
    out->width = width;
    return mui_success;
}

// The boxes being written: the buffer, and how many it holds.
typedef struct Boxes
{
    muiTextService* service;
    const muiTextBlock* block;
    Box* data;
    uint32_t count;
    uint32_t capacity;
} Boxes;

enum
{
    // The most grapheme clusters a glyph's cluster is cut into; one with
    // more is one box.
    MAX_PARTS = 32
};

// Adds a cluster's box, cut into one per grapheme cluster when a glyph
// spans several, left to right: for right-to-left text the last in the
// text first.
static void AddCluster(Boxes* boxes, float x0, float x1, uint32_t start, uint32_t end, bool rtl)
{
    const char* text = boxes->block->text.data;
    // bounds[k] up to bounds[k + 1] are part k's bytes.
    uint32_t bounds[MAX_PARTS + 1] = {start, end};
    uint32_t parts = 1;
    size_t breaks[MAX_PARTS];
    size_t found = 0;
    if (end - start > 1 &&
        muniFindGraphemeBreaks(text + start, end - start, breaks, MAX_PARTS, &found) ==
            muni_success &&
        found > 1)
    {
        parts = (uint32_t)found;
        for (uint32_t k = 0; k < parts; k++)
        {
            bounds[k + 1] = start + (uint32_t)breaks[k];
        }
    }
    float share = (x1 - x0) / (float)parts;
    for (uint32_t v = 0; v < parts && boxes->count < boxes->capacity; v++)
    {
        uint32_t k = rtl ? parts - 1 - v : v;
        float left = x0 + share * (float)v;
        float right = v + 1 == parts ? x1 : x0 + share * (float)(v + 1);
        boxes->data[boxes->count++] = (Box){left, right, bounds[k], bounds[k + 1], rtl};
    }
}

// Adds the boxes of an item's clusters from start up to end, from pen x;
// returns the pen after them.
static float AddSegment(const muiParagraph* paragraph, const muiLineGlyphs* source,
                        const muiTextItem* item, uint32_t start, uint32_t end, float pen,
                        Boxes* boxes)
{
    const muiShapedGlyph* shaped = source->glyphs + item->firstGlyph;
    float scale = muiItemScale(paragraph, item);
    bool rtl = (item->level & 1) != 0;
    uint32_t first = 0;
    uint32_t last = 0;
    muiSegmentGlyphs(shaped, item, start, end, &first, &last);
    // In right-to-left text the cluster after one in the text is to its
    // left: the one last added.
    uint32_t left = end;
    for (uint32_t i = first; i < last;)
    {
        uint32_t cluster = shaped[i].cluster;
        float x0 = pen;
        for (; i < last && shaped[i].cluster == cluster; i++)
        {
            pen += (float)shaped[i].advance * scale;
        }
        pen += paragraph->scale.spacing;
        uint32_t following = rtl ? left : (i < last ? shaped[i].cluster : end);
        AddCluster(boxes, x0, pen, cluster, following, rtl);
        left = cluster;
    }
    return pen;
}

// Writes a line's boxes left to right into the service's scratch; false
// when memory runs out.
static bool LineBoxes(const Laid* laid, uint32_t index, Boxes* out)
{
    const muiParagraph* paragraph = &laid->paragraph;
    const muiTextLine* line = &laid->lines[index];
    muiTextService* service = paragraph->service;
    muiLineGlyphs source;
    size_t runCount = 0;
    // A box for each byte at most, as each holds one or more.
    size_t capacity = (size_t)(line->end - line->start) + 1u;
    if (!muiGetLineGlyphs(paragraph, line, &source) ||
        !muiReserve(&service->allocator, &service->hitBoxes, capacity * sizeof(Box)) ||
        !muiReorderLine(paragraph, line, &runCount))
    {
        return false;
    }
    *out = (Boxes){service, paragraph->block, service->hitBoxes.data, 0, (uint32_t)capacity};
    const muniBidiRun* runs = service->runs.data;
    float pen = muiAlignLine(paragraph, source.width, laid->width);
    for (size_t r = 0; r < runCount; r++)
    {
        uint32_t start = line->start + (uint32_t)runs[r].start;
        uint32_t end = start + (uint32_t)runs[r].length;
        bool odd = (runs[r].level & 1) != 0;
        for (uint32_t k = 0; k < source.itemCount; k++)
        {
            const muiTextItem* item = &source.items[odd ? source.itemCount - 1 - k : k];
            if (item->end <= start || item->start >= end)
            {
                continue;
            }
            uint32_t from = item->start > start ? item->start : start;
            uint32_t to = item->end < end ? item->end : end;
            pen = AddSegment(paragraph, &source, item, from, to, pen, out);
        }
    }
    return true;
}

// The position at a box's left or right edge.
static muiTextPosition EdgeOf(const Box* box, bool right)
{
    // The edge a cluster starts at in its direction is its start.
    bool leading = right == box->rtl;
    return leading ? (muiTextPosition){box->start, mui_affinityDownstream}
                   : (muiTextPosition){box->end, mui_affinityUpstream};
}

static bool IsHostValid(const muiTextHost* host)
{
    return host != nullptr && host->service != nullptr && host->context != nullptr;
}

muiResult muiTextHitTest(const muiTextHost* host, muiNodeId nodeId, float width, float x, float y,
                         muiTextPosition* positionOut)
{
    if (!IsHostValid(host) || positionOut == nullptr || !isfinite(x) || !isfinite(y))
    {
        return mui_errorInvalid;
    }
    Laid laid;
    muiResult result = Lay(host, nodeId, width, &laid);
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
    Boxes boxes;
    if (!LineBoxes(&laid, index, &boxes))
    {
        return mui_errorCapacity;
    }
    if (boxes.count == 0)
    {
        *positionOut = (muiTextPosition){laid.lines[index].start, mui_affinityDownstream};
        return mui_success;
    }
    // The last box starting at or before x, else the first; past its
    // middle, its right edge.
    const Box* box = &boxes.data[0];
    for (uint32_t i = 1; i < boxes.count && x >= boxes.data[i].x0; i++)
    {
        box = &boxes.data[i];
    }
    bool right = x - box->x0 >= box->x1 - x;
    *positionOut = EdgeOf(box, right);
    return mui_success;
}

// The line a position is on: the first whose next line starts after it,
// or the one before when it keeps upstream to that line's end.
static uint32_t LineOf(const Laid* laid, muiTextPosition position)
{
    uint32_t index = 0;
    while (index + 1 < laid->lineCount && position.offset >= laid->lines[index].next)
    {
        index++;
    }
    bool upstream = position.affinity == mui_affinityUpstream;
    if (upstream && index > 0 && position.offset == laid->lines[index].start)
    {
        index--;
    }
    return index;
}

// The x of a position on a line's boxes; false when no box has it.
static bool EdgeX(const Boxes* boxes, muiTextPosition position, bool upstream, float* xOut,
                  bool* rtlOut)
{
    for (uint32_t i = 0; i < boxes->count; i++)
    {
        const Box* box = &boxes->data[i];
        bool at = upstream ? box->end == position.offset
                           : box->start <= position.offset && position.offset < box->end;
        if (at)
        {
            // Downstream: the leading edge; upstream: the trailing one.
            *xOut = upstream == box->rtl ? box->x0 : box->x1;
            *rtlOut = box->rtl;
            return true;
        }
    }
    return false;
}

muiResult muiTextGetCaret(const muiTextHost* host, muiNodeId nodeId, float width,
                          muiTextPosition position, muiTextCaret* caretOut)
{
    if (!IsHostValid(host) || caretOut == nullptr)
    {
        return mui_errorInvalid;
    }
    Laid laid;
    muiResult result = Lay(host, nodeId, width, &laid);
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
    uint32_t index = LineOf(&laid, position);
    Boxes boxes;
    if (!LineBoxes(&laid, index, &boxes))
    {
        return mui_errorCapacity;
    }
    bool upstream = position.affinity == mui_affinityUpstream;
    float x = start;
    bool runRtl = rtl;
    if (!EdgeX(&boxes, position, upstream, &x, &runRtl) &&
        !EdgeX(&boxes, position, !upstream, &x, &runRtl) && boxes.count != 0)
    {
        // Past the clusters: the line's end in the paragraph's
        // direction, or its start before them.
        bool atEnd = position.offset >= laid.lines[index].end;
        bool left = atEnd == rtl;
        x = left ? boxes.data[0].x0 : boxes.data[boxes.count - 1].x1;
    }
    *caretOut =
        (muiTextCaret){x, (float)index * paragraph->lineHeight, paragraph->lineHeight, runRtl};
    return mui_success;
}

// Adds a rectangle for each stretch of a line's boxes from start up to
// end, those past capacity counted only; returns the count after them.
static uint32_t AddStretches(const Boxes* boxes, uint32_t start, uint32_t end, float top,
                             float height, muiRect* rects, uint32_t capacity, uint32_t count)
{
    bool open = false;
    for (uint32_t i = 0; i < boxes->count; i++)
    {
        const Box* box = &boxes->data[i];
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
    if (!IsHostValid(host) || countOut == nullptr || (rects == nullptr && capacity != 0))
    {
        return mui_errorInvalid;
    }
    Laid laid;
    muiResult result = Lay(host, nodeId, width, &laid);
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
        Boxes boxes;
        if (!LineBoxes(&laid, index, &boxes))
        {
            return mui_errorCapacity;
        }
        count = AddStretches(&boxes, start, end, (float)index * laid.paragraph.lineHeight,
                             laid.paragraph.lineHeight, rects, capacity, count);
    }
    *countOut = count;
    return count <= capacity ? mui_success : mui_errorCapacity;
}
