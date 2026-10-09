// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A value text's marks (record mui-0008, I123): its selection, lines and
// words, checked against the text the build reads and the tree takes;
// and the boundaries of its units and its offsets in the units platforms
// count, which the adapters share (research 143).

#ifndef MAUL_UI_SRC_ACCESS_TEXT_H
#define MAUL_UI_SRC_ACCESS_TEXT_H

#include "maul-ui/access.h"

#include <stdbool.h>
#include <stdint.h>

// Whether each part of marks fits a text of length bytes: offsets within
// it at the starts of characters; lines ascending from 0, words in order
// and apart, each with bytes in it. None given fits.
bool muiAccessSelectionFits(const muiAccessTextMarks* marks, const char* text, uint32_t length);
bool muiAccessLinesFit(const muiAccessTextMarks* marks, const char* text, uint32_t length);
bool muiAccessWordsFit(const muiAccessTextMarks* marks, const char* text, uint32_t length);

// The units a value text is read and moved by.
typedef enum muiAccessUnit
{
    mui_unitCharacter,
    mui_unitWord,
    mui_unitLine,
    mui_unitParagraph,
    mui_unitDocument,
} muiAccessUnit;

// A node's value text as the boundaries read it: empty for none.
typedef struct muiAccessText
{
    const char* bytes;
    uint32_t length;
    const muiAccessTextMarks* marks;
} muiAccessText;

muiAccessText muiAccessValueOf(const muiAccessNode* node);

// The first boundary of a unit after a byte offset, the text's end past
// the last; the last before it, 0 before the first. A character is a
// code point; a word runs from its start to the next word's, as AT-SPI
// and UI Automation read words; a line from its start to the next's, a
// paragraph to just after its hard break (UAX #14's mandatory breaks);
// none given, a text is one line. The text's ends are boundaries of
// every unit.
uint32_t muiAccessBoundaryAfter(const muiAccessText* text, muiAccessUnit unit, uint32_t at);
uint32_t muiAccessBoundaryBefore(const muiAccessText* text, muiAccessUnit unit, uint32_t at);

// Counts of a text's code points and of its UTF-16 units before a byte
// offset; the byte offset where a count of either ends, clamped to the
// text, and at a character's start (a UTF-16 count inside a surrogate
// pair at the pair's).
uint32_t muiAccessPointsBefore(const char* text, uint32_t offset);
uint32_t muiAccessUtf16Before(const char* text, uint32_t offset);
uint32_t muiAccessByteOfPoints(const muiAccessText* text, uint32_t points);
uint32_t muiAccessByteOfUtf16(const muiAccessText* text, uint32_t units);

#endif // MAUL_UI_SRC_ACCESS_TEXT_H
