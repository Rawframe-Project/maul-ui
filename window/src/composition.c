// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Compositions and carets of the Maul Window glue (record mui-0007). The
// segments pass as they are: Maul Window's preedit segments are Maul
// UI's composition segments, member for member.

#include "maul-ui-window/composition.h"

#include "glue_state.h"

#include "maul-ui/layout.h"

#include <stddef.h>

static_assert(MWIN_MAX_PREEDIT_SEGMENTS == MUI_MAX_COMPOSITION_SEGMENTS, "the segment limits");
static_assert(mwin_preeditPlain == mui_compositionPlain &&
                  mwin_preeditUnderline == mui_compositionUnderline &&
                  mwin_preeditTarget == mui_compositionTarget &&
                  mwin_preeditConverted == mui_compositionConverted,
              "the styles");
static_assert(mwin_purposeText == mui_purposeText && mwin_purposeNumber == mui_purposeNumber &&
                  mwin_purposeEmail == mui_purposeEmail &&
                  mwin_purposePassword == mui_purposePassword && mwin_purposeUrl == mui_purposeUrl,
              "the purposes");
static_assert(sizeof(mwinPreeditSegment) == sizeof(muiCompositionSegment) &&
                  offsetof(mwinPreeditSegment, start) == offsetof(muiCompositionSegment, start) &&
                  offsetof(mwinPreeditSegment, length) == offsetof(muiCompositionSegment, length) &&
                  offsetof(mwinPreeditSegment, style) == offsetof(muiCompositionSegment, style),
              "the segments");

// A preedit's segments as a composition's; NULL for none.
static const muiCompositionSegment* SegmentsOf(const mwinPreeditEvent* preedit,
                                               muiCompositionSegment* segments)
{
    uint32_t count = preedit->segmentCount;
    for (uint32_t i = 0; i < count && i < MUI_MAX_COMPOSITION_SEGMENTS; i++)
    {
        const mwinPreeditSegment* segment = &preedit->segments[i];
        segments[i] = (muiCompositionSegment){segment->start, segment->length, segment->style};
    }
    return count != 0 && preedit->segments != nullptr ? segments : nullptr;
}

muiResult muiWindowCompose(muiTextService* service, muiTextBlockId blockId,
                           const mwinPreeditEvent* preedit, bool* changedOut)
{
    if (preedit == nullptr || preedit->caret > (int32_t)preedit->length)
    {
        return mui_errorInvalid;
    }
    muiCompositionSegment segments[MUI_MAX_COMPOSITION_SEGMENTS];
    // A caret the method hides goes to the composition's end.
    uint32_t caret = preedit->caret >= 0 ? (uint32_t)preedit->caret : preedit->length;
    return muiTextBlock_Compose(service, blockId, preedit->text, preedit->length, caret,
                                SegmentsOf(preedit, segments), preedit->segmentCount, changedOut);
}

muiResult muiWindowSetComposition(muiTextService* service, muiTextBlockId blockId, uint32_t offset,
                                  const mwinPreeditEvent* preedit, int32_t* caretOut)
{
    if (preedit == nullptr)
    {
        return mui_errorInvalid;
    }
    muiCompositionSegment segments[MUI_MAX_COMPOSITION_SEGMENTS];
    uint32_t count = preedit->segmentCount;
    muiResult status =
        muiTextBlock_SetComposition(service, blockId, offset, preedit->text, preedit->length,
                                    SegmentsOf(preedit, segments), count);
    uint32_t start = 0;
    uint32_t length = 0;
    if (status == mui_success && caretOut != nullptr)
    {
        status = muiTextBlock_GetComposition(service, blockId, &start, &length);
        if (status == mui_success)
        {
            *caretOut = length != 0 && preedit->caret >= 0
                            ? (int32_t)(start + (uint32_t)preedit->caret)
                            : -1;
        }
    }
    return status;
}

muiResult muiWindowGlue_SetTextCaret(muiWindowGlue* glue, const muiTextHost* host, muiNodeId nodeId,
                                     muiTextPosition position, mwinRect* placedOut)
{
    if (glue == nullptr || host == nullptr)
    {
        return mui_errorInvalid;
    }
    muiRect content = muiNode_GetContentRect(host->context, nodeId);
    muiTextCaret caret;
    muiResult status = muiTextGetCaret(host, nodeId, content.width, position, &caret);
    if (status != mui_success)
    {
        return status;
    }
    const muiRect rect = {content.x + caret.x, content.y + caret.y, 0.0f, caret.height};
    return muiWindowGlue_SetCaret(glue, nodeId, rect, placedOut);
}

muiResult muiWindowGlue_RequestKeyboard(muiWindowGlue* glue, const muiTextHost* host,
                                        muiNodeId nodeId)
{
    if (glue == nullptr || host == nullptr)
    {
        return mui_errorInvalid;
    }
    bool visible = nodeId.index1 != 0;
    muiInputPurpose purpose = mui_purposeText;
    if (visible)
    {
        uint64_t key = muiNode_GetHostKey(host->context, nodeId);
        const muiTextBlockId blockId = {(uint32_t)key, (uint32_t)(key >> 32)};
        if (muiTextBlock_GetInputPurpose(host->service, blockId, &purpose) != mui_success)
        {
            purpose = mui_purposeText;
        }
    }
    if (mwinRequestVirtualKeyboard(glue->windows, glue->window, visible, purpose, nullptr) !=
        mwin_success)
    {
        return mui_errorInvalid;
    }
    return mui_success;
}
