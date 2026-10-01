// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context: the root object that owns a tree of nodes and every
// result computed over it.

#ifndef MAUL_UI_CONTEXT_H
#define MAUL_UI_CONTEXT_H

#include "maul-ui/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // The owner of every node. A context is used by one thread at a time.
    typedef struct muiContext muiContext;

    // The named limits of a context. A request past one is refused with
    // mui_errorCapacity.
    typedef struct muiLimits
    {
        // Nodes that exist at once. The context reserves them when it is
        // created, as it reserves every limit.
        uint32_t nodes;
        // Style classes and node types that exist at once.
        uint32_t styles;
        uint32_t nodeTypes;
        // The variants of all classes together that have values set: a
        // class's base values, and each state variant it sets, are one.
        uint32_t propertySets;
        // Notifications waiting to be taken; past it, they are counted in a
        // mui_notificationDropped record.
        uint32_t notifications;
    } muiLimits;

    // How a context is made. Build it with muiDefaultContextDef.
    typedef struct muiContextDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        muiLimits limits;
    } muiContextDef;

    /// Returns the default context def: 4,096 nodes, 256 styles, 64 node
    /// types, 1,024 property sets, 64 notifications and the C library's
    /// allocator.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiContextDef muiDefaultContextDef(void);

    /// Creates a context and reserves the memory its limits name.
    ///
    /// @param def         The context: a valid cookie, an allocator with both
    ///                    functions or neither, a node limit from 1 to
    ///                    2^31 - 1 and other limits from 0 to 2^31 - 1.
    /// @param contextOut  Receives the context; set to NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad
    ///         cookie, a half-set allocator or a limit out of range;
    ///         `mui_errorCapacity` when the allocator cannot give the
    ///         memory.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_NODISCARD MUI_API muiResult muiCreateContext(const muiContextDef* def,
                                                     muiContext** contextOut);

    /// Destroys a context, every node in it and its memory. Every id it gave
    /// out becomes meaningless.
    ///
    /// @param context  The context, or NULL for nothing.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API void muiDestroyContext(muiContext* context);

    // What a notification reports.
    typedef uint8_t muiNotificationKind;

    enum
    {
        // A node's conditions flipped back and forth with its own layout,
        // the same two sizes in turn; its conditions are held at their
        // last outcome until the host edits the node or its size leaves
        // those two (record mui-0004).
        mui_notificationOscillation = 1,
        // count notifications were dropped here, past the limit.
        mui_notificationDropped = 2,
    };

    // A record of something the host learns after the call that caused
    // it, in the order it happened (family record 0018).
    typedef struct muiNotification
    {
        muiNotificationKind kind;
        // The node it is about; the null id for mui_notificationDropped.
        muiNodeId nodeId;
        // For mui_notificationDropped, how many were dropped.
        uint32_t count;
    } muiNotification;

    /// Takes the oldest notification.
    ///
    /// @param context          The context.
    /// @param notificationOut  Receives it.
    /// @return `mui_success`; `mui_empty` when none is waiting;
    ///         `mui_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNextNotification(muiContext* context,
                                                        muiNotification* notificationOut);

    /// Returns how many calls the context has refused as invalid input
    /// (`mui_errorInvalid`): a count release builds can watch to catch a
    /// host's bugs. Stale ids are not misuse.
    ///
    /// @param context  The context.
    /// @return The count; 0 for a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API uint64_t muiGetContextMisuse(const muiContext* context);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_CONTEXT_H
