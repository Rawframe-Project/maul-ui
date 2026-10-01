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
        // created.
        uint32_t nodes;
    } muiLimits;

    // How a context is made. Build it with muiDefaultContextDef.
    typedef struct muiContextDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        muiLimits limits;
    } muiContextDef;

    /// Returns the default context def: 4,096 nodes and the C library's
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
    ///                    2^31 - 1.
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
