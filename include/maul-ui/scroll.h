// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling (record mui-0007): a scroll container's offset (layout.h's
// scrollAxes) and the extent its children reach. Offsets are logical: x
// runs from the inline start, so under right to left it grows leftward.
// Painting moves the children by the offset through a transform, and
// hit testing and navigation follow them.

#ifndef MAUL_UI_SCROLL_H
#define MAUL_UI_SCROLL_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Scrolls a node to an offset, within 0 and its extent less its
    /// padding box along each axis it scrolls (0 along any other), as its
    /// last muiComputeLayout measured them; layout keeps it within them as
    /// sizes change. The next draw list moves the children.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param x        The offset from the inline start, finite.
    /// @param y        The offset from the top, finite.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, an offset not finite or a call from a measure or
    ///         paint function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetScroll(muiContext* context, muiNodeId nodeId,
                                                      float x, float y);

    /// Reads a node's scroll offset; 0 for a node that does not scroll.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param xOut     Receives the offset from the inline start.
    /// @param yOut     Receives the offset from the top.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or
    ///         the null id; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetScroll(const muiContext* context, muiNodeId nodeId,
                                                      float* xOut, float* yOut);

    /// Reads the extent a scroll container's children reach, as its last
    /// muiComputeLayout measured it: from its padding box's start to the
    /// furthest end of its children's margin boxes plus its end padding,
    /// at least its padding box; for a scrollbar, the padding box over the
    /// extent is the thumb's share.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param extentOut  Receives the extent; 0 by 0 for a node that does
    ///                   not scroll or was not laid out as one.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or
    ///         the null id; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetScrollExtent(const muiContext* context,
                                                            muiNodeId nodeId, muiSize* extentOut);

    /// Scrolls each scrolling ancestor of a node, the nearest first, the
    /// least that brings the node's border box into its padding box, as
    /// CSSOM View's scrollIntoView with "nearest" does per axis: a node
    /// already inside stays; one past the start edge and no larger than
    /// the box aligns its start, one past the end its end; a larger one
    /// past either edge aligns the other, and one past both stays.
    /// Directional and sequential navigation does this to the node it
    /// focuses.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_ScrollIntoView(muiContext* context, muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_SCROLL_H
