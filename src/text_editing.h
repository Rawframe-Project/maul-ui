// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A block's editing state as the editor's commands share it (record
// mui-0006): the block of an id, brought up to date with changes made
// elsewhere, and its selection placed.

#ifndef MAUL_UI_SRC_TEXT_EDITING_H
#define MAUL_UI_SRC_TEXT_EDITING_H

#include "text_block.h"
#include "text_service.h"

#include "maul-ui/base.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"

#include <stdint.h>

// The units a press selects, and drags extend by.
enum
{
    MUI_GRAIN_CLUSTER = 0,
    MUI_GRAIN_WORD = 1,
    MUI_GRAIN_PARAGRAPH = 2
};

// An editing block by its id: `mui_success`; `mui_errorInvalid` for the
// null id or a block not editing; `mui_errorStale` for one that is gone.
// Text changed since the editor last saw it empties the history and
// keeps the selection within it.
muiResult muiEditingBlock(const muiTextService* service, muiTextBlockId blockId,
                          muiTextBlock** blockOut);

// Places a block's selection, which ends a run of typing or deleting
// undone together.
void muiPlaceSelection(muiTextBlock* block, muiTextSelection selection);

#endif // MAUL_UI_SRC_TEXT_EDITING_H
