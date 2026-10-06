// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Editing primitives over laid-out text (record mui-0006): positions in a
// node's text, from points and to carets, and the rectangles a range of
// it covers, the text laid out as muiPaintText paints it. Selection,
// input and undo are the caller's.

#ifndef MAUL_UI_TEXT_EDIT_H
#define MAUL_UI_TEXT_EDIT_H

#include "maul-ui/base.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text_block.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Which side of an offset a position keeps to, where the offset has
    // two places: the end of a wrapped line or the start of the next, and
    // either side of a change of direction.
    typedef uint8_t muiTextAffinity;

    enum
    {
        // With the text after the offset.
        mui_affinityDownstream = 0,
        // With the text before it.
        mui_affinityUpstream = 1,
    };

    // A place between grapheme clusters of a node's text: a byte offset
    // into its block's UTF-8 text, and its affinity.
    typedef struct muiTextPosition
    {
        uint32_t offset;
        muiTextAffinity affinity;
    } muiTextPosition;

    // Where a caret is drawn in the node's content box: its x, the top
    // and height of its line, and whether the text it sits on runs right
    // to left.
    typedef struct muiTextCaret
    {
        float x;
        float y;
        float height;
        bool rightToLeft;
    } muiTextCaret;

    /// Finds the position nearest a point of a node's text: the line at
    /// the point's y (the first above the text, the last below it), then
    /// the edge of the grapheme cluster nearer the point's x (the right
    /// one at the middle), a cluster several clusters share a glyph with
    /// taking an equal share of it; past a line's ends, that end, before
    /// any white space hanging past it. The position keeps to the cluster
    /// the point is on. Negative letter spacing can draw a cluster over
    /// the one before it; the point then finds the first, left to right.
    ///
    /// @param host         The text host the node's text is laid out with.
    /// @param nodeId       A node whose host key is a block's.
    /// @param width        The node's content box width, as painting is
    ///                     given.
    /// @param x            The point, from the content box's top left.
    /// @param y            Likewise.
    /// @param positionOut  Receives the position; unchanged on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         point not finite; `mui_errorStale` for a node, block or font
    ///         that is gone; `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextHitTest(const muiTextHost* host, muiNodeId nodeId,
                                                   float width, float x, float y,
                                                   muiTextPosition* positionOut);

    /// Finds where the caret of a position is drawn: at the leading edge
    /// of the cluster after it for downstream, the trailing edge of the
    /// cluster before it for upstream, on the line the affinity picks
    /// where a line wraps; an offset in white space hanging past a line's
    /// end sits at that end, and an offset inside a cluster at its start.
    ///
    /// @param host      The text host.
    /// @param nodeId    A node whose host key is a block's.
    /// @param width     The node's content box width.
    /// @param position  The position; an offset past the text is its end.
    /// @param caretOut  Receives the caret; unchanged on failure.
    /// @return As muiTextHitTest.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextGetCaret(const muiTextHost* host, muiNodeId nodeId,
                                                    float width, muiTextPosition position,
                                                    muiTextCaret* caretOut);

    /// Finds the rectangles a range of a node's text covers: on each line,
    /// one for each stretch of side by side clusters in the range, left to
    /// right, lines from the top; clusters on both sides of the range's
    /// ends are not covered.
    ///
    /// @param host      The text host.
    /// @param nodeId    A node whose host key is a block's.
    /// @param width     The node's content box width.
    /// @param start     The range's first byte.
    /// @param end       The byte after it; no rectangles when not after
    ///                  start.
    /// @param rects     Receives the rectangles, from the content box's top
    ///                  left; may be NULL when capacity is 0.
    /// @param capacity  How many rects holds.
    /// @param countOut  Receives how many rectangles there are, also when
    ///                  rects holds fewer.
    /// @return `mui_success`; `mui_errorCapacity` when rects holds fewer,
    ///         writing those that fit, or memory runs out; otherwise as
    ///         muiTextHitTest.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextGetRangeRects(const muiTextHost* host, muiNodeId nodeId,
                                                         float width, uint32_t start, uint32_t end,
                                                         muiRect* rects, uint32_t capacity,
                                                         uint32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TEXT_EDIT_H
