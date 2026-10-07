// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard of the Maul Window glue for text editing (record
// mui-0007), in maul-ui-window where Maul UI has its text component:
// copies and cuts written to the window's clipboard, and a paste asked
// of it and taken into an editing block when the window answers, as
// Maul Window reads the clipboard by request.

#ifndef MAUL_UI_WINDOW_CLIPBOARD_H
#define MAUL_UI_WINDOW_CLIPBOARD_H

#include "maul-ui-window/glue.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"
#include "maul-window/event.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Writes text to the glue's window's clipboard, as a
    /// muiClipboardWriteFunction whose user is the glue
    /// (muiTextEditInput). A write the window refuses (text past its
    /// clipboard limit, too many requests) is dropped, as a failed copy
    /// is on every platform.
    ///
    /// @param glue    The glue.
    /// @param text    UTF-8 text.
    /// @param length  Its length in bytes.
    /// @par Thread safety
    /// Main thread only, as Maul Window's requests.
    MUI_WINDOW_API void muiWindowGlue_WriteClipboard(void* glue, const char* text, size_t length);

    /// Asks the window for its clipboard's text, for a paste
    /// muiTextEditEvent asked for (outcome.paste); muiWindowGlue_Paste
    /// takes the answer.
    ///
    /// @param glue  The glue.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL glue or a
    ///         request the window refuses.
    /// @par Thread safety
    /// Main thread only, as Maul Window's requests.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowGlue_RequestPaste(muiWindowGlue* glue);

    /// Pastes the clipboard's text into an editing block when an event
    /// answers a clipboard read with it (mwin_eventRequestCompleted, a
    /// read done); other events pass.
    ///
    /// @param glue        The glue.
    /// @param service     The service.
    /// @param blockId     The editing block.
    /// @param event       The window's event.
    /// @param changedOut  Receives whether the text changed; may be NULL.
    /// @return `mui_success` for a paste, changed or not; `mui_empty` for
    ///         another event; `mui_errorInvalid` for a NULL argument;
    ///         `mui_errorCapacity` when memory for the text runs out; or
    ///         as muiTextBlock_Paste.
    /// @par Thread safety
    /// Main thread only, as Maul Window's calls.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowGlue_Paste(muiWindowGlue* glue,
                                                               muiTextService* service,
                                                               muiTextBlockId blockId,
                                                               const mwinEvent* event,
                                                               bool* changedOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_WINDOW_CLIPBOARD_H
