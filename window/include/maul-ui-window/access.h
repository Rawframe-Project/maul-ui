// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility through the Maul Window glue (record mui-0008), in
// maul-ui-window where Maul UI builds its accessibility tree's consumer:
// one access a glue makes the adapter built for its window's platform
// over the window's native handles, feeds it the root's updates and
// hands its root to the window (mwinRequestAccessibilityRoot) as it
// changes. A window of a platform with no adapter built, or of Maul
// Window's test backend, keeps the tree alone, so a program can check
// what its clients would be told without them.

#ifndef MAUL_UI_WINDOW_ACCESS_H
#define MAUL_UI_WINDOW_ACCESS_H

#include "maul-ui-window/glue.h"
#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiWindowAccess muiWindowAccess;

    /// What a platform's client asks of a node, as the adapters' action
    /// functions take it: whether it was done.
    typedef bool (*muiWindowAccessActionFunction)(void* user, const muiAccessRequest* request);

    // How an access is made. Build it with muiDefaultWindowAccessDef.
    typedef struct muiWindowAccessDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        // The glue whose window, context and root the tree is of; it
        // outlives the access.
        muiWindowGlue* glue;
        // The most nodes the tree holds, at least 1.
        uint32_t nodes;
        // The program's AT-SPI application (a muiAtspiApp*), which a
        // window of X11 or Wayland joins; NULL keeps those windows'
        // trees alone. The program pumps it (muiAtspiApp_Pump).
        void* atspiApp;
        // On the web, whether the elements wait for a button that
        // enables them, and its label (as muiAriaAdapterDef's).
        bool ariaDeferred;
        const char* ariaEnableLabel;
        // What a client asks of a node; NULL has the context do it
        // (muiPerformAccessAction).
        muiWindowAccessActionFunction action;
        void* user;
    } muiWindowAccessDef;

    /// Returns the default access def: no glue, 4096 nodes, no AT-SPI
    /// application, on the web the elements waiting for a button labelled
    /// "Enable accessibility" (as muiDefaultAriaAdapterDef), the context
    /// doing what clients ask.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_WINDOW_API muiWindowAccessDef muiDefaultWindowAccessDef(void);

    /// Makes a glue's access: enables the root's updates
    /// (muiAccess_Enable) and makes the adapter for the window's platform
    /// (UI Automation on Win32, NSAccessibility on macOS, UIAccessibility
    /// on iOS, Android's over the activity's view, ARIA in the element
    /// Maul Window keeps over the canvas, AT-SPI on X11 and Wayland with
    /// an application), or a tree alone. A program may
    /// make it once mwin_eventAccessibilityRequested arrives, to build
    /// no tree before a client asks where the platform says so.
    ///
    /// @param def        The access: a valid cookie and allocator, a glue
    ///                   and at least 1 node.
    /// @param accessOut  Receives the access; set to NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         def outside the above, or a window with no surface;
    ///         `mui_errorCapacity` when the context has its limit of
    ///         enabled roots, the AT-SPI application its windows, or
    ///         memory runs out; as the adapter's creation otherwise.
    /// @par Thread safety
    /// Main thread only, as the adapters.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiCreateWindowAccess(const muiWindowAccessDef* def,
                                                                 muiWindowAccess** accessOut);

    /// Destroys an access: hands the window no root, lets the adapter
    /// go and stops the root's updates (muiAccess_Disable).
    ///
    /// @param access  The access, or NULL for nothing.
    /// @par Thread safety
    /// Main thread only.
    MUI_WINDOW_API void muiDestroyWindowAccess(muiWindowAccess* access);

    /// Sends the root's changes since the last call (muiBuildAccessUpdate)
    /// to the adapter, and hands the window the adapter's root if it
    /// changed. The program calls it each frame after layout.
    ///
    /// @param access  The access.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL access, or a
    ///         root request Maul Window refuses; as muiBuildAccessUpdate
    ///         and the adapter's update otherwise.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowAccess_Update(muiWindowAccess* access);

    /// Takes a record the host drained: a scale change of the window
    /// rescales the adapter's bounds where they are in pixels (UI
    /// Automation, Android, AT-SPI), and a move places an AT-SPI window
    /// on the screen. Others it leaves.
    ///
    /// @param access  The access.
    /// @param event   The record.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowAccess_HandleEvent(muiWindowAccess* access,
                                                                       const mwinEvent* event);

    /// Returns the tree the access holds, the adapter's or its own.
    ///
    /// @param access  The access.
    /// @return The tree; NULL for a NULL access.
    /// @par Thread safety
    /// Safe from any thread; the access is used by one thread at a time.
    MUI_WINDOW_API const muiAccessTree* muiWindowAccess_GetTree(const muiWindowAccess* access);

    /// Returns whether the access made a platform's adapter, rather than
    /// keeping a tree alone.
    ///
    /// @param access  The access.
    /// @return Whether it did; false for a NULL access.
    /// @par Thread safety
    /// Safe from any thread; the access is used by one thread at a time.
    MUI_WINDOW_API bool muiWindowAccess_HasAdapter(const muiWindowAccess* access);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_WINDOW_ACCESS_H
