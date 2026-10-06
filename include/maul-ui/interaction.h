// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Interaction properties and hit testing (record mui-0007): whether a node
// and its children are hit by a point, whether input it leaves unused
// passes through to what lies behind the UI, and which node is topmost at
// a point, in reverse paint order.

#ifndef MAUL_UI_INTERACTION_H
#define MAUL_UI_INTERACTION_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Which of a node and its children a point can hit.
    typedef uint8_t muiHitMode;

    enum
    {
        // The node, in its rounded border box, and its children: CSS's
        // pointer-events auto.
        mui_hitAuto = 0,
        // Its children alone: a container that lets points through to
        // what lies under it where it has no children.
        mui_hitChildren = 1,
        // Neither: the node and its subtree are passed over.
        mui_hitNone = 2,
    };

    // A node's interaction values. Every field is a property
    // (mui_propertyHitMode, mui_propertyPassThrough), set like any other
    // through classes, states and direct writes, and not inherited.
    typedef struct muiInteractionStyle
    {
        muiHitMode hitMode;
        // Whether input the node is hit by but leaves unused passes to
        // what lies behind the UI, such as a game world: a HUD panel
        // that does not block clicks.
        bool passThrough;
    } muiInteractionStyle;

    /// Returns the default interaction values: hit in full, blocking.
    ///
    /// @return The values.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiInteractionStyle muiDefaultInteractionStyle(void);

    /// Sets interaction values of one variant of a class, as
    /// muiStyle_SetLayoutValues does layout ones.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @param variant  The variant.
    /// @param values   The values; only the fields mask names are read: a
    ///                 known hit mode.
    /// @param mask     The properties, within MUI_INTERACTION_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown variant or property bit, a value outside
    ///         the above or a call from a measure or paint function, which
    ///         changes nothing; `mui_errorStale` for an id whose class is
    ///         gone; `mui_errorCapacity` when the variant had no values and
    ///         the context's limit of property sets is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetInteractionValues(muiContext* context,
                                                                  muiStyleId styleId,
                                                                  muiVariant variant,
                                                                  const muiInteractionStyle* values,
                                                                  muiPropertyMask mask);

    /// Reads the interaction values one variant of a class sets.
    ///
    /// @param context    The context.
    /// @param styleId    The class.
    /// @param variant    The variant.
    /// @param valuesOut  Receives the set values, and
    ///                   muiDefaultInteractionStyle's for the rest.
    /// @param maskOut    Receives which interaction properties are set.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or an unknown variant; `mui_errorStale` for an id
    ///         whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetInteractionValues(const muiContext* context,
                                                                  muiStyleId styleId,
                                                                  muiVariant variant,
                                                                  muiInteractionStyle* valuesOut,
                                                                  muiPropertyMask* maskOut);

    /// Writes interaction properties of a node directly, as
    /// muiNode_SetLayoutValues does layout ones; neither its layout nor
    /// its paint is redone.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param values   The values, as muiStyle_SetInteractionValues takes
    ///                 them.
    /// @param mask     The properties, within MUI_INTERACTION_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown property bit, a value outside the above
    ///         or a call from a measure or paint function, which changes
    ///         nothing; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetInteractionValues(muiContext* context,
                                                                 muiNodeId nodeId,
                                                                 const muiInteractionStyle* values,
                                                                 muiPropertyMask mask);

    /// Reads a node's resolved interaction values: its direct writes, and
    /// for the other properties what its classes and states gave at the
    /// last muiComputeLayout that reached it.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param valuesOut  Receives the values.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetInteractionStyle(const muiContext* context,
                                                                muiNodeId nodeId,
                                                                muiInteractionStyle* valuesOut);

    // What a point hits: the node, the null id for none, the point in its
    // border box, and whether input it leaves unused passes through to
    // what lies behind the UI (always, when nothing is hit).
    typedef struct muiHit
    {
        muiNodeId node;
        float x;
        float y;
        bool passThrough;
    } muiHit;

    /// Finds the topmost node of a root's subtree at a point, as its last
    /// muiComputeLayout left it: the last in paint order whose rounded
    /// border box holds the point, inside the rounded clips of every
    /// ancestor that clips and of no node whose hit mode leaves it out.
    /// Opacity does not matter, as in CSS. Positions are those painting
    /// gives, the root at its own rectangle.
    ///
    /// @param context  The context.
    /// @param rootId   The root of the subtree.
    /// @param x        The point, in the space the root's rectangle is in.
    /// @param y        Likewise.
    /// @param hitOut   Receives what the point hits; unchanged on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or a point not finite; `mui_errorStale` for a root
    ///         that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiHitTest(const muiContext* context, muiNodeId rootId, float x,
                                               float y, muiHit* hitOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_INTERACTION_H
