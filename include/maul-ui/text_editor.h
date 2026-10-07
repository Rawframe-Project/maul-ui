// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text editing (record mui-0006): a block opted into editing keeps a
// selection, an undo history and its field's rules, and takes typing,
// pastes, deletions, undo and redo; moves, presses and drags place the
// selection through a node's laid-out text. Keys, clipboard and focus
// are the host's to map onto these.

#ifndef MAUL_UI_TEXT_EDITOR_H
#define MAUL_UI_TEXT_EDITOR_H

#include "maul-ui/base.h"
#include "maul-ui/node.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A field's rules, as bits.
    typedef uint8_t muiTextEditFlags;

    enum
    {
        // Lines break: Enter's line break goes in. Without it, each line
        // break typed or pasted becomes a space, keeping words apart.
        mui_editMultiline = 1,
        // Selection, moves and copying work; edits do nothing.
        mui_editReadOnly = 2,
        // Copying gives nothing.
        mui_editPassword = 4,
    };

    // What a field's text may be, checked as it is typed or pasted.
    typedef uint8_t muiTextFilter;

    enum
    {
        mui_filterNone = 0,
        // A sign at the start, then digits.
        mui_filterInteger = 1,
        // A sign at the start, then digits with at most one point.
        mui_filterDecimal = 2,
    };

    typedef struct muiTextEditDef
    {
        muiTextEditFlags flags;
        muiTextFilter filter;
        // The most grapheme clusters the text may hold, typing and pastes
        // cut to fit; 0 for no limit.
        uint32_t maxLength;
        // The most edits undo keeps, the oldest dropped first; 0 keeps
        // none.
        uint32_t undoLimit;
    } muiTextEditDef;

    // A selection: from the anchor to the caret, empty where they meet.
    typedef struct muiTextSelection
    {
        uint32_t anchor;
        muiTextPosition caret;
    } muiTextSelection;

    /// Returns a single-line field's rules: no filter, no limit, and 100
    /// edits to undo.
    ///
    /// @return The rules.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiTextEditDef muiDefaultTextEditDef(void);

    /// Opts a block into editing with a field's rules, or out with NULL.
    /// Either way the history empties and the caret goes to the text's
    /// end. Changing the text otherwise (muiTextBlock_SetText,
    /// muiTextBlock_Replace, a composition) empties the history too, the
    /// selection kept within the text.
    ///
    /// @param service  The service.
    /// @param blockId  The block.
    /// @param def      The rules, or NULL.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service, the
    ///         null id, or a flag or filter out of range; `mui_errorStale`
    ///         for a block that is gone.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_SetEditing(muiTextService* service,
                                                            muiTextBlockId blockId,
                                                            const muiTextEditDef* def);

    /// Reads an editing block's selection.
    ///
    /// @param service       The service.
    /// @param blockId       The block.
    /// @param selectionOut  Receives the selection.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or a block not editing; `mui_errorStale` for a block
    ///         that is gone. Nothing is written on failure.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_GetSelection(const muiTextService* service,
                                                              muiTextBlockId blockId,
                                                              muiTextSelection* selectionOut);

    /// Sets an editing block's selection, which ends a run of typing
    /// undone together.
    ///
    /// @param service    The service.
    /// @param blockId    The block.
    /// @param selection  The selection: offsets within the text, at the
    ///                   starts of characters.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service, the
    ///         null id, a block not editing or an offset out of place;
    ///         `mui_errorStale` for a block that is gone.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_Select(muiTextService* service,
                                                        muiTextBlockId blockId,
                                                        muiTextSelection selection);

    /// Types text over an editing block's selection, under its rules, the
    /// caret after it. Typing undoes word by word: a run of it continuing
    /// where the last ended is one edit until a word starts after white
    /// space.
    ///
    /// @param service     The service.
    /// @param blockId     The block.
    /// @param text        UTF-8 text; may be NULL when length is 0.
    /// @param length      Its length in bytes.
    /// @param changedOut  Receives whether the text changed; may be NULL.
    /// @return `mui_success`, changed or not (read-only, filtered out, at
    ///         the limit); `mui_errorInvalid` for a NULL service or text,
    ///         the null id, a block not editing or text past the block's
    ///         limit; `mui_errorStale` for a block that is gone;
    ///         `mui_errorCapacity` when memory runs out, which changes
    ///         nothing.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_Type(muiTextService* service,
                                                      muiTextBlockId blockId, const char* text,
                                                      size_t length, bool* changedOut);

    /// Pastes text over an editing block's selection, as muiTextBlock_Type
    /// but undone alone.
    ///
    /// @param service     The service.
    /// @param blockId     The block.
    /// @param text        UTF-8 text; may be NULL when length is 0.
    /// @param length      Its length in bytes.
    /// @param changedOut  Receives whether the text changed; may be NULL.
    /// @return As muiTextBlock_Type.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_Paste(muiTextService* service,
                                                       muiTextBlockId blockId, const char* text,
                                                       size_t length, bool* changedOut);

    /// Deletes an editing block's selection, or with none, what the
    /// deletion removes beside the caret (muiTextBlock_FindDeletion). A
    /// run of deletions one way, each where the last left the caret, is
    /// undone together.
    ///
    /// @param service     The service.
    /// @param blockId     The block.
    /// @param deletion    Which way.
    /// @param changedOut  Receives whether the text changed; may be NULL.
    /// @return As muiTextBlock_Type; `mui_errorInvalid` for a deletion out
    ///         of range.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_Erase(muiTextService* service,
                                                       muiTextBlockId blockId,
                                                       muiTextDeletion deletion, bool* changedOut);

    /// Deletes an editing block's selection, or with none, the text from
    /// the caret to an offset: a word or a line, found with muiTextMove.
    ///
    /// @param service     The service.
    /// @param blockId     The block.
    /// @param offset      The other end, at the start of a character;
    ///                    past the text is its end.
    /// @param changedOut  Receives whether the text changed; may be NULL.
    /// @return As muiTextBlock_Type; `mui_errorInvalid` for an offset
    ///         inside a character.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_EraseTo(muiTextService* service,
                                                         muiTextBlockId blockId, uint32_t offset,
                                                         bool* changedOut);

    /// Reads the selected text of an editing block, for the host's
    /// clipboard: a cut is this, then muiTextBlock_Erase. A password's
    /// is empty.
    ///
    /// @param service    The service.
    /// @param blockId    The block.
    /// @param textOut    Receives the text, valid until the block changes.
    /// @param lengthOut  Receives its length in bytes.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or a block not editing; `mui_errorStale` for a block
    ///         that is gone. Nothing is written on failure.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_GetSelectedText(const muiTextService* service,
                                                                 muiTextBlockId blockId,
                                                                 const char** textOut,
                                                                 size_t* lengthOut);

    /// Undoes an editing block's last edit, the selection back as it was
    /// before it; nothing on a read-only block or during a composition.
    ///
    /// @param service     The service.
    /// @param blockId     The block.
    /// @param changedOut  Receives whether the text changed; may be NULL.
    /// @return As muiTextBlock_Type.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_Undo(muiTextService* service,
                                                      muiTextBlockId blockId, bool* changedOut);

    /// Redoes an editing block's last undone edit, the selection as it was
    /// after it; as muiTextBlock_Undo otherwise. An edit made after an
    /// undo drops what was undone.
    ///
    /// @param service     The service.
    /// @param blockId     The block.
    /// @param changedOut  Receives whether the text changed; may be NULL.
    /// @return As muiTextBlock_Type.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_Redo(muiTextService* service,
                                                      muiTextBlockId blockId, bool* changedOut);

    /// Reads whether an editing block has an edit to undo and one to redo.
    ///
    /// @param service  The service.
    /// @param blockId  The block.
    /// @param undoOut  Receives whether muiTextBlock_Undo would undo one.
    /// @param redoOut  Receives whether muiTextBlock_Redo would redo one.
    /// @return As muiTextBlock_GetSelection.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_GetUndoState(const muiTextService* service,
                                                              muiTextBlockId blockId, bool* undoOut,
                                                              bool* redoOut);

    /// Moves the caret of a node's editing block through its laid-out
    /// text (muiTextMove), extending the selection or collapsing it there;
    /// lines up and down keep the x the first of them started from.
    ///
    /// @param host      The text host.
    /// @param nodeId    A node whose host key is an editing block's.
    /// @param movement  The movement.
    /// @param extend    Whether the anchor stays.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         movement out of range or a block not editing;
    ///         `mui_errorStale` for a node, block or font that is gone;
    ///         `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextEditMove(const muiTextHost* host, muiNodeId nodeId,
                                                    muiTextMovement movement, bool extend);

    /// Places the caret of a node's editing block where a press is: one
    /// click at the point, two selecting the word there, three the
    /// paragraph; with extend, the selection grows to it from the anchor.
    /// Drags then extend by the same unit.
    ///
    /// @param host        The text host.
    /// @param nodeId      A node whose host key is an editing block's.
    /// @param x           The point, in the node's border box, as pointer
    ///                    records give it.
    /// @param y           The point's y.
    /// @param clickCount  The press's place in a quick series: 1, 2, 3; a
    ///                    fourth goes round to 1, as on every platform.
    /// @param extend      Whether the anchor stays.
    /// @return As muiTextEditMove; `mui_errorInvalid` for a point not
    ///         finite or a click count of 0.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextEditPress(const muiTextHost* host, muiNodeId nodeId,
                                                     float x, float y, uint32_t clickCount,
                                                     bool extend);

    /// Extends the selection of a node's editing block to where a drag
    /// is, by the unit its press selected, keeping what the press
    /// selected.
    ///
    /// @param host    The text host.
    /// @param nodeId  A node whose host key is an editing block's.
    /// @param x       The point, in the node's border box.
    /// @param y       The point's y.
    /// @return As muiTextEditPress.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextEditDrag(const muiTextHost* host, muiNodeId nodeId,
                                                    float x, float y);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TEXT_EDITOR_H
