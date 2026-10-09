// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A value text's marks (record mui-0008, I123): its selection, lines and
// words, checked against the text the build reads and the tree takes.

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

#endif // MAUL_UI_SRC_ACCESS_TEXT_H
