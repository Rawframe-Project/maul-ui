// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Compositions and carets of the Maul Window glue (record mui-0007), in
// maul-ui-window where Maul UI has its text component: an input method's
// preedit set into a text block, and the candidate window placed at a
// position of a node's text. Maul Window's preedit styles and segment
// limit are Maul UI's composition styles and limit. An editing block
// takes a preedit through muiWindowCompose, and its field's purpose
// picks the on-screen keyboard (muiWindowGlue_RequestKeyboard).

#ifndef MAUL_UI_WINDOW_COMPOSITION_H
#define MAUL_UI_WINDOW_COMPOSITION_H

#include "maul-ui-window/glue.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Sets a block's composition from an input method's preedit
    /// (muiTextBlock_SetComposition): its text, and its segments as
    /// they come; an empty preedit removes the composition and ends it,
    /// the committed text arriving as text input after it.
    ///
    /// @param service   The service.
    /// @param blockId   The block.
    /// @param offset    Where a new composition goes, as
    ///                  muiTextBlock_SetComposition takes it.
    /// @param preedit   The record's preedit (data.preedit).
    /// @param caretOut  Receives where the input method's caret is in the
    ///                  block, or -1 where it hides it or the composition
    ///                  ended. May be NULL. Unchanged on failure.
    /// @return As muiTextBlock_SetComposition, with `mui_errorInvalid` for
    ///         a NULL preedit.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowSetComposition(muiTextService* service,
                                                                   muiTextBlockId blockId,
                                                                   uint32_t offset,
                                                                   const mwinPreeditEvent* preedit,
                                                                   int32_t* caretOut);

    /// Shows an input method's preedit in an editing block
    /// (muiTextBlock_Compose): its text, its caret, at the end where the
    /// method hides it, and its segments as they come; an empty preedit
    /// takes the composition out, the committed text arriving as text
    /// input after it.
    ///
    /// @param service     The service.
    /// @param blockId     The block.
    /// @param preedit     The record's preedit (data.preedit).
    /// @param changedOut  Receives whether the text changed; may be NULL.
    /// @return As muiTextBlock_Compose, with `mui_errorInvalid` for a NULL
    ///         preedit.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowCompose(muiTextService* service,
                                                            muiTextBlockId blockId,
                                                            const mwinPreeditEvent* preedit,
                                                            bool* changedOut);

    /// Asks the window to accept text with its caret at a position of a
    /// node's text (muiTextGetCaret in the node's content box, then
    /// muiWindowGlue_SetCaret), the caret a rectangle as tall as its line
    /// and of no width.
    ///
    /// @param glue       The glue.
    /// @param host       The text host the node's block is in.
    /// @param nodeId     A node whose host key is a block's.
    /// @param position   The position.
    /// @param placedOut  As muiWindowGlue_SetCaret's.
    /// @return As muiTextGetCaret and muiWindowGlue_SetCaret.
    /// @par Thread safety
    /// Main thread only, as Maul Window's requests.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowGlue_SetTextCaret(muiWindowGlue* glue,
                                                                      const muiTextHost* host,
                                                                      muiNodeId nodeId,
                                                                      muiTextPosition position,
                                                                      mwinRect* placedOut);

    /// Asks for the window's on-screen keyboard, where the platform has
    /// one, laid out for what a node's editing block takes
    /// (muiTextBlock_GetInputPurpose; plain text for a block not
    /// editing), or, with the null id, to hide it
    /// (mwinRequestVirtualKeyboard). Ask as the focus enters and leaves a
    /// field.
    ///
    /// @param glue    The glue.
    /// @param host    The text host the node's block is in.
    /// @param nodeId  The node, or the null id to hide the keyboard.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         request Maul Window refuses.
    /// @par Thread safety
    /// Main thread only, as Maul Window's requests.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowGlue_RequestKeyboard(muiWindowGlue* glue,
                                                                         const muiTextHost* host,
                                                                         muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_WINDOW_COMPOSITION_H
