// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtualization (record mui-0007): a scroll container that stands for
// many items of which only those near its viewport exist as nodes. The
// library keeps every item's extent, fixed or estimated until measured,
// works out after each layout the window of items that should exist (the
// viewport plus overscan on each side), and reports it by
// mui_notificationWindowChanged when it changes. The host realizes the
// window: it creates or reuses a child node of the list for each index
// in it, binds it (muiNode_SetItem), and destroys or pools the rest,
// resetting a reused node so nothing of one key outlives it. The library
// places bound items at their offsets along the axis, out of the flex
// flow and stretched across the list's content box, sizes the content
// to all the items so the scroll limits hold, and measures bound items
// into their extents.

#ifndef MAUL_UI_VIRTUAL_H
#define MAUL_UI_VIRTUAL_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Which way a list's items follow each other. The list should scroll
    // that way (maul-ui/layout.h's scroll axes).
    typedef uint8_t muiListAxis;

    enum
    {
        mui_listVertical = 0,
        // From the inline start: right to left under right to left.
        mui_listHorizontal = 1,
    };

    typedef struct muiVirtualList
    {
        uint32_t count;
        muiListAxis axis;
        // Every item's extent along the axis when fixed; otherwise each
        // item's estimate until it is measured. Finite and above 0.
        float extent;
        bool fixed;
        // Between items, and how far beyond the viewport on each side
        // items exist. Finite and at least 0.
        float gap;
        float overscan;
    } muiVirtualList;

    /// The default list: no items, vertical, estimated at 40 a piece, no
    /// gap, an overscan of 200.
    ///
    /// @return The list.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiVirtualList muiDefaultVirtualList(void);

    /// Makes a node a virtual list, or sets it anew, every estimated
    /// extent back to the estimate; its window is reported at the next
    /// layout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param list     The list, as described above.
    /// @return `mui_success`; `mui_errorCapacity` when the context holds
    ///         its limit of lists, or the items of an estimated list do not
    ///         fit what its limit of items leaves; `mui_errorInvalid` for a
    ///         NULL argument, the null id, a list outside the above, or a
    ///         call from a measure or paint function; `mui_errorStale` for a
    ///         node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetVirtualList(muiContext* context, muiNodeId nodeId,
                                                           const muiVirtualList* list);

    /// Makes a node no longer a virtual list; its items stay bound.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`, whether or not it was one;
    ///         `mui_errorInvalid` for a NULL context, the null id or a call
    ///         from a measure or paint function; `mui_errorStale` for a
    ///         node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_ClearVirtualList(muiContext* context, muiNodeId nodeId);

    /// Reads the window of a list's items that should exist, as the last
    /// layout found it: indices from first up to end, end excluded.
    ///
    /// @param context   The context.
    /// @param nodeId    The list.
    /// @param firstOut  Receives the first index.
    /// @param endOut    Receives the index past the last.
    /// @return `mui_success`; `mui_empty` for a node that is not a list or
    ///         has not been laid out since it was set; `mui_errorInvalid`
    ///         for a NULL argument or the null id; `mui_errorStale` for a
    ///         node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetVirtualWindow(const muiContext* context,
                                                             muiNodeId nodeId, uint32_t* firstOut,
                                                             uint32_t* endOut);

    /// Reads where an item of a list begins along its axis, from the start
    /// of the list's content box, and its extent, as last known.
    ///
    /// @param context    The context.
    /// @param nodeId     The list.
    /// @param index      The item, below the list's count.
    /// @param offsetOut  Receives its offset.
    /// @param extentOut  Receives its extent.
    /// @return `mui_success`; `mui_empty` for a node that is not a list;
    ///         `mui_errorInvalid` for a NULL argument, the null id or an
    ///         index past the count; `mui_errorStale` for a node that is
    ///         gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetVirtualItem(const muiContext* context,
                                                           muiNodeId nodeId, uint32_t index,
                                                           float* offsetOut, float* extentOut);

    /// Binds a child of a list to an item: the next layouts place it at
    /// the item's offset and measure it into the item's extent.
    ///
    /// @param context  The context.
    /// @param nodeId   The node, a child of a list.
    /// @param index    Its item.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, a node whose parent is not a list, an index past
    ///         the list's count, or a call from a measure or paint
    ///         function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetItem(muiContext* context, muiNodeId nodeId,
                                                    uint32_t index);

    /// Unbinds a node from its item; it joins its parent's flow again.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`, whether or not it was bound;
    ///         `mui_errorInvalid` for a NULL context, the null id or a call
    ///         from a measure or paint function; `mui_errorStale` for a
    ///         node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_ClearItem(muiContext* context, muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_VIRTUAL_H
