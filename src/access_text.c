// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A value text's marks, checked (record mui-0008, I123).

#include "access_text.h"

// Whether offset is within the text at a character's start: the end, or
// a byte that does not continue a UTF-8 sequence.
static bool IsStart(const char* text, uint32_t length, uint32_t offset)
{
    return offset == length || (offset < length && ((unsigned char)text[offset] & 0xC0u) != 0x80u);
}

bool muiAccessSelectionFits(const muiAccessTextMarks* marks, const char* text, uint32_t length)
{
    return !marks->selected ||
           (IsStart(text, length, marks->anchor) && IsStart(text, length, marks->focus));
}

bool muiAccessLinesFit(const muiAccessTextMarks* marks, const char* text, uint32_t length)
{
    if (marks->lineCount == 0)
    {
        return true;
    }
    if (marks->lineStarts == nullptr || marks->lineStarts[0] != 0)
    {
        return false;
    }
    for (uint32_t i = 1; i < marks->lineCount; i++)
    {
        uint32_t start = marks->lineStarts[i];
        if (start <= marks->lineStarts[i - 1] || !IsStart(text, length, start))
        {
            return false;
        }
    }
    return true;
}

bool muiAccessWordsFit(const muiAccessTextMarks* marks, const char* text, uint32_t length)
{
    if (marks->wordCount != 0 && marks->words == nullptr)
    {
        return false;
    }
    uint32_t after = 0;
    for (uint32_t i = 0; i < marks->wordCount; i++)
    {
        muiAccessWord word = marks->words[i];
        if (word.start < after || word.end <= word.start || !IsStart(text, length, word.start) ||
            !IsStart(text, length, word.end))
        {
            return false;
        }
        after = word.end;
    }
    return true;
}
