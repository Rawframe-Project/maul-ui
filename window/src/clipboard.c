// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard of the Maul Window glue for text editing. A paste's text
// is copied out of the window's context into memory of the glue's
// allocator for as long as the paste takes.

#include "maul-ui-window/clipboard.h"

#include "allocator.h"
#include "glue_state.h"

#include "maul-window/clipboard.h"

void muiWriteWindowClipboard(void* glue, const char* text, size_t length)
{
    const muiWindowGlue* own = glue;
    if (own != nullptr)
    {
        (void)mwinWindow_RequestClipboardWrite(own->windows, own->window, text, length, nullptr);
    }
}

muiResult muiWindowGlue_RequestPaste(muiWindowGlue* glue)
{
    if (glue == nullptr ||
        mwinWindow_RequestClipboardRead(glue->windows, glue->window, nullptr) != mwin_success)
    {
        return mui_errorInvalid;
    }
    return mui_success;
}

muiResult muiTextBlock_PasteFromWindow(muiWindowGlue* glue, muiTextService* service,
                                       muiTextBlockId blockId, const mwinEvent* event,
                                       bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    if (glue == nullptr || service == nullptr || event == nullptr)
    {
        return mui_errorInvalid;
    }
    const mwinCompletion* completion = &event->data.completion;
    if (event->type != mwin_eventRequestCompleted ||
        completion->kind != mwin_requestClipboardRead || completion->outcome != mwin_outcomeDone)
    {
        return mui_empty;
    }
    // A read whose text a later read replaced passes: the later one's
    // answer brings the clipboard's text.
    size_t length = 0;
    mwinResult status =
        mwinRequest_GetClipboardText(glue->windows, completion->request, nullptr, 0, &length);
    if (status == mwin_errorStale)
    {
        return mui_empty;
    }
    if (length == 0)
    {
        return status == mwin_success ? mui_success : mui_errorInvalid;
    }
    size_t size = length;
    char* text = muiWindowAllocate(&glue->allocator, size, 1);
    if (text == nullptr)
    {
        return mui_errorCapacity;
    }
    muiResult result = mui_errorInvalid;
    if (mwinRequest_GetClipboardText(glue->windows, completion->request, text, size, &length) ==
        mwin_success)
    {
        result = muiTextBlock_Paste(service, blockId, text, length, changedOut);
    }
    muiWindowRelease(&glue->allocator, text, size, 1);
    return result;
}
