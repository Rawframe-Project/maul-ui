// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layout: the authored values that size and place a node, the solver
// that computes rectangles from them (CSS Flexbox, record mui-0003), and
// the rectangles it publishes. Lengths are logical units; a node's
// rectangle is relative to its parent's border box, and a root's
// rectangle starts at 0, 0.

#ifndef MAUL_UI_LAYOUT_H
#define MAUL_UI_LAYOUT_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Whether a dimension is automatic or a Scale+Offset value.
    typedef uint8_t muiDimensionKind;

    enum
    {
        // Sized by the layout rules: content, stretching, flexing. As a
        // maximum, no limit; as a minimum, the automatic minimum size.
        mui_dimensionAuto = 0,
        // scale x the parent's content extent on the axis + offset; with an
        // indefinite parent extent, automatic.
        mui_dimensionValue = 1,
    };

    // A size along one axis. A zeroed dimension is automatic.
    typedef struct muiDimension
    {
        float scale;
        float offset;
        muiDimensionKind kind;
    } muiDimension;

    // A node's size, minimum and maximum, per axis. The size is the
    // border box.
    typedef struct muiSizing
    {
        muiDimension width;
        muiDimension height;
        muiDimension minWidth;
        muiDimension minHeight;
        muiDimension maxWidth;
        muiDimension maxHeight;
    } muiSizing;

    // The four sides of a box, in logical order: start and end follow the
    // inline direction.
    typedef struct muiEdges
    {
        float start;
        float end;
        float top;
        float bottom;
    } muiEdges;

    // The main axis of a container and the direction its children follow.
    typedef uint8_t muiFlexDirection;

    enum
    {
        mui_flexRow = 0,
        mui_flexRowReverse = 1,
        mui_flexColumn = 2,
        mui_flexColumnReverse = 3,
    };

    // How a container places its children along the main axis.
    typedef uint8_t muiJustify;

    enum
    {
        mui_justifyStart = 0,
        mui_justifyEnd = 1,
        mui_justifyCenter = 2,
        mui_justifySpaceBetween = 3,
        mui_justifySpaceAround = 4,
        mui_justifySpaceEvenly = 5,
    };

    // How children sit on the cross axis.
    typedef uint8_t muiAlign;

    enum
    {
        // For a child: the container's alignment. Not valid for a
        // container.
        mui_alignAuto = 0,
        mui_alignStretch = 1,
        mui_alignStart = 2,
        mui_alignEnd = 3,
        mui_alignCenter = 4,
    };

    // What a node lays out as a container.
    typedef struct muiFlexContainer
    {
        muiFlexDirection direction;
        muiJustify justify;
        muiAlign alignItems;
        // The space between rows, and between columns, of children.
        float rowGap;
        float columnGap;
    } muiFlexContainer;

    // How a node takes part in its parent's flex layout.
    typedef struct muiFlexItem
    {
        float grow;
        float shrink;
        muiDimension basis;
        muiAlign alignSelf;
    } muiFlexItem;

    // What a node without children holds.
    typedef uint8_t muiContentKind;

    enum
    {
        // Nothing: its content box is empty.
        mui_contentNone = 0,
        // The host's content, sized by the measure function given to
        // muiComputeLayout. A node with children ignores it.
        mui_contentHost = 1,
    };

    // Every authored value layout reads. Build it with
    // muiDefaultLayoutStyle.
    typedef struct muiLayoutStyle
    {
        muiSizing sizing;
        muiFlexContainer container;
        muiFlexItem item;
        muiEdges margin;
        muiEdges border;
        muiEdges padding;
        muiContentKind content;
    } muiLayoutStyle;

    // A rectangle: its origin and size.
    typedef struct muiRect
    {
        float x;
        float y;
        float width;
        float height;
    } muiRect;

    // A width and a height.
    typedef struct muiSize
    {
        float width;
        float height;
    } muiSize;

    // What the solver asks of host content along one axis.
    typedef uint8_t muiMeasureMode;

    enum
    {
        // The content box is exactly size.
        mui_measureExact = 0,
        // The content fits within size where it can, as text wraps to a
        // width.
        mui_measureAtMost = 1,
        // The content at its widest: size is unused.
        mui_measureMaxContent = 2,
        // The content at its narrowest, as text broken at every
        // opportunity: size is unused.
        mui_measureMinContent = 3,
    };

    // One axis of a measurement request.
    typedef struct muiMeasureAxis
    {
        float size;
        muiMeasureMode mode;
    } muiMeasureAxis;

    // Returns the content-box size of a node's host content. It runs inside
    // muiComputeLayout, on the calling thread, and may not change the
    // context; a call that would is refused as misuse.
    typedef muiSize (*muiMeasureFunction)(void* user, muiNodeId nodeId, uint64_t hostKey,
                                          muiMeasureAxis width, muiMeasureAxis height);

    // What muiComputeLayout lays a root out in.
    typedef struct muiLayoutInput
    {
        // The space the root fits into, at least 0.
        float availableWidth;
        float availableHeight;
        // Sizes host content; NULL sizes it as empty.
        muiMeasureFunction measure;
        void* measureUser;
    } muiLayoutInput;

    /// Returns the default layout style: CSS's initial values (row, no
    /// grow, shrink 1, automatic basis and sizes, stretch, start), no
    /// margins, borders or padding, and no content.
    ///
    /// @return The style.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiLayoutStyle muiDefaultLayoutStyle(void);

    /// Sets every authored layout value of a node. The node and its parent
    /// are laid out again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param style    The values: finite numbers, grow and shrink, padding,
    ///                 border and gaps at least 0, known enumerators, and
    ///                 alignItems not mui_alignAuto.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a value outside the above, or a call from a measure
    ///         function; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetLayoutStyle(muiContext* context, muiNodeId nodeId,
                                                           const muiLayoutStyle* style);

    /// Reads a node's authored layout values.
    ///
    /// @param context   The context.
    /// @param nodeId    The node.
    /// @param styleOut  Receives the values.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetLayoutStyle(const muiContext* context,
                                                           muiNodeId nodeId,
                                                           muiLayoutStyle* styleOut);

    /// Tells the solver a node's host content changed size, so it is
    /// measured again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the null
    ///         id or a call from a measure function; `mui_errorStale` for an
    ///         id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_MarkContentChanged(muiContext* context,
                                                               muiNodeId nodeId);

    /// Lays out a root and its subtree in the given space. Subtrees that
    /// did not change since the last call are not visited.
    ///
    /// @param context  The context.
    /// @param rootId   A root: a node without a parent.
    /// @param input    The space and the measure function.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a node with a parent, a negative or non-finite
    ///         space, or a call from a measure function; `mui_errorStale`
    ///         for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiComputeLayout(muiContext* context, muiNodeId rootId,
                                                     const muiLayoutInput* input);

    /// Returns a node's border box from the last muiComputeLayout that
    /// reached it, relative to its parent's border box.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The rectangle; all zero before any layout, for a stale id or
    ///         a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiRect muiNode_GetRect(const muiContext* context, muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_LAYOUT_H
