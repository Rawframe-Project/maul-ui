// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Style: classes of typed property values, the node types and node
// states that pick them, and a node's direct writes (record mui-0004).
// A node's values resolve in fixed layers, each later one winning: the
// defaults, every class's base values in order, the state variants (the
// states in the order of muiState, weakest first, and the classes in
// order within each), and the node's direct writes. A node's classes are
// its type's, then its own.

#ifndef MAUL_UI_STYLE_H
#define MAUL_UI_STYLE_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A style class, in the shape of every id (family record 0016).
    typedef struct muiStyleId
    {
        uint32_t index1;
        uint32_t generation;
    } muiStyleId;

    // A node type: an ordered list of classes that every node of the type
    // takes before its own.
    typedef struct muiNodeTypeId
    {
        uint32_t index1;
        uint32_t generation;
    } muiNodeTypeId;

    // One value a style can set, named after the muiLayoutStyle field it
    // sets.
    typedef uint8_t muiProperty;

    enum
    {
        // Dimensions.
        mui_propertyWidth = 0,
        mui_propertyHeight = 1,
        mui_propertyMinWidth = 2,
        mui_propertyMinHeight = 3,
        mui_propertyMaxWidth = 4,
        mui_propertyMaxHeight = 5,
        // A number of 0 or more.
        mui_propertyAspectRatio = 6,
        // Enumerators of the layout types.
        mui_propertyFlexDirection = 7,
        mui_propertyFlexWrap = 8,
        mui_propertyJustify = 9,
        mui_propertyAlignItems = 10,
        mui_propertyAlignContent = 11,
        // Numbers of 0 or more.
        mui_propertyRowGap = 12,
        mui_propertyColumnGap = 13,
        mui_propertyGrow = 14,
        mui_propertyShrink = 15,
        // A dimension.
        mui_propertyBasis = 16,
        // An enumerator.
        mui_propertyAlignSelf = 17,
        // Finite numbers.
        mui_propertyMarginStart = 18,
        mui_propertyMarginEnd = 19,
        mui_propertyMarginTop = 20,
        mui_propertyMarginBottom = 21,
        // An enumerator: muiEdgeMask bits.
        mui_propertyMarginAuto = 22,
        // Numbers of 0 or more.
        mui_propertyBorderStart = 23,
        mui_propertyBorderEnd = 24,
        mui_propertyBorderTop = 25,
        mui_propertyBorderBottom = 26,
        mui_propertyPaddingStart = 27,
        mui_propertyPaddingEnd = 28,
        mui_propertyPaddingTop = 29,
        mui_propertyPaddingBottom = 30,
        // An enumerator.
        mui_propertyPosition = 31,
        // Dimensions.
        mui_propertyInsetStart = 32,
        mui_propertyInsetEnd = 33,
        mui_propertyInsetTop = 34,
        mui_propertyInsetBottom = 35,
        // Numbers from 0 to 1.
        mui_propertyAnchorX = 36,
        mui_propertyAnchorY = 37,
        // Enumerators.
        mui_propertyTextDirection = 38,
        mui_propertyContent = 39,
        mui_propertyCount = 40,
    };

    // A set of properties, one bit each.
    typedef uint64_t muiPropertyMask;

#define MUI_PROPERTY_BIT(property) ((muiPropertyMask)1 << (property))
// Every layout property.
#define MUI_LAYOUT_PROPERTIES ((muiPropertyMask)0xFFFFFFFFFFull)

    // The states a node can be in, as bits, weakest first: a later
    // state's variant wins over an earlier one's.
    typedef uint8_t muiState;

    enum
    {
        mui_stateChecked = 1,
        mui_stateSelected = 2,
        mui_stateFocused = 4,
        mui_stateHovered = 8,
        mui_statePressed = 16,
        mui_stateDisabled = 32,
        mui_stateExiting = 64,
    };

    // Which values of a class a call reads or writes: its base values, or
    // the variant one state brings.
    typedef uint8_t muiVariant;

    enum
    {
        mui_variantBase = 0,
        mui_variantChecked = 1,
        mui_variantSelected = 2,
        mui_variantFocused = 3,
        mui_variantHovered = 4,
        mui_variantPressed = 5,
        mui_variantDisabled = 6,
        mui_variantExiting = 7,
        mui_variantCount = 8,
    };

    enum
    {
        // The classes a node or a node type lists.
        MUI_MAX_CLASSES = 8
    };

    /// Creates a style class with no values set.
    ///
    /// @param context     The context.
    /// @param styleIdOut  Receives the class; set to the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         call from a measure function; `mui_errorCapacity` when the
    ///         context's style limit is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateStyle(muiContext* context, muiStyleId* styleIdOut);

    /// Destroys a style class. Nodes and node types that list it skip it,
    /// and every node is styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure function; `mui_errorStale`
    ///         for an id whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyStyle(muiContext* context, muiStyleId styleId);

    /// Sets layout properties in one variant of a class from the fields of
    /// values. Every node is styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @param variant  The variant.
    /// @param values   The values; only the fields mask names are read, and
    ///                 each must be one muiNode_SetLayoutStyle allows.
    /// @param mask     The properties, within MUI_LAYOUT_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown variant or property bit, a value outside
    ///         the above or a call from a measure function, which changes
    ///         nothing; `mui_errorStale` for an id whose class is gone;
    ///         `mui_errorCapacity` when the variant had no values and the
    ///         context's limit of property sets is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetLayoutValues(muiContext* context,
                                                             muiStyleId styleId, muiVariant variant,
                                                             const muiLayoutStyle* values,
                                                             muiPropertyMask mask);

    /// Unsets properties in one variant of a class. Every node is styled
    /// again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @param variant  The variant.
    /// @param mask     The properties, within MUI_LAYOUT_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, an unknown variant or property bit or a call from a
    ///         measure function; `mui_errorStale` for an id whose class is
    ///         gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_ResetProperties(muiContext* context,
                                                             muiStyleId styleId, muiVariant variant,
                                                             muiPropertyMask mask);

    /// Reads the values one variant of a class sets.
    ///
    /// @param context    The context.
    /// @param styleId    The class.
    /// @param variant    The variant.
    /// @param valuesOut  Receives the set values, and muiDefaultLayoutStyle's
    ///                   for the rest.
    /// @param maskOut    Receives which properties are set.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or an unknown variant; `mui_errorStale` for an id
    ///         whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetLayoutValues(const muiContext* context,
                                                             muiStyleId styleId, muiVariant variant,
                                                             muiLayoutStyle* valuesOut,
                                                             muiPropertyMask* maskOut);

    /// Creates a node type with an ordered list of classes.
    ///
    /// @param context    The context.
    /// @param classes    count classes, kept as given; a class destroyed
    ///                   later is skipped. NULL when count is 0.
    /// @param count      At most MUI_MAX_CLASSES.
    /// @param typeIdOut  Receives the type; set to the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a count
    ///         over the limit or a call from a measure function;
    ///         `mui_errorCapacity` when the context's node type limit is
    ///         reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateNodeType(muiContext* context,
                                                      const muiStyleId* classes, uint32_t count,
                                                      muiNodeTypeId* typeIdOut);

    /// Destroys a node type. Its nodes are left with no type, and every
    /// node is styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param typeId   The type.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure function; `mui_errorStale`
    ///         for an id whose type is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyNodeType(muiContext* context, muiNodeTypeId typeId);

    /// Replaces a node type's classes. Every node is styled again at the
    /// next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param typeId   The type.
    /// @param classes  count classes, as muiCreateNodeType takes them.
    /// @param count    At most MUI_MAX_CLASSES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, NULL classes with a count, a count over the limit or
    ///         a call from a measure function; `mui_errorStale` for an id
    ///         whose type is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNodeType_SetClasses(muiContext* context,
                                                           muiNodeTypeId typeId,
                                                           const muiStyleId* classes,
                                                           uint32_t count);

    /// Sets a node's type. The node is styled again at the next
    /// muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param typeId   The type; the null id for none.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null node id or a call from a measure function;
    ///         `mui_errorStale` for a node or a type that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetType(muiContext* context, muiNodeId nodeId,
                                                    muiNodeTypeId typeId);

    /// Replaces a node's own classes, which follow its type's. The node is
    /// styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param classes  count classes, kept as given; a class destroyed later
    ///                 is skipped. NULL when count is 0.
    /// @param count    At most MUI_MAX_CLASSES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, NULL classes with a count, a count over the limit or
    ///         a call from a measure function; `mui_errorStale` for a node
    ///         that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetClasses(muiContext* context, muiNodeId nodeId,
                                                       const muiStyleId* classes, uint32_t count);

    /// Sets the states a node is in. The node is styled again at the next
    /// muiComputeLayout when they change.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param states   muiState bits.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, unknown bits or a call from a measure function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetStates(muiContext* context, muiNodeId nodeId,
                                                      muiState states);

    /// Returns the states a node is in.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return Its muiState bits; 0 for a stale id or a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiState muiNode_GetStates(const muiContext* context, muiNodeId nodeId);

    /// Writes layout properties of a node directly from the fields of
    /// values: they win over every class until reset, at once. The node and
    /// its parent are laid out again at the next muiComputeLayout.
    /// muiNode_SetLayoutStyle writes every layout property.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param values   The values; only the fields mask names are read, and
    ///                 each must be one muiNode_SetLayoutStyle allows.
    /// @param mask     The properties, within MUI_LAYOUT_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown property bit, a value outside the above
    ///         or a call from a measure function, which changes nothing;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetLayoutValues(muiContext* context, muiNodeId nodeId,
                                                            const muiLayoutStyle* values,
                                                            muiPropertyMask mask);

    /// Ends direct writes of a node's properties: they take their classes'
    /// values again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param mask     The properties, within MUI_LAYOUT_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, an unknown property bit or a call from a measure
    ///         function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_ResetProperties(muiContext* context, muiNodeId nodeId,
                                                            muiPropertyMask mask);

    /// Returns which properties of a node are written directly.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The properties; 0 for a stale id or a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiPropertyMask muiNode_GetDirectProperties(const muiContext* context,
                                                        muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_STYLE_H
