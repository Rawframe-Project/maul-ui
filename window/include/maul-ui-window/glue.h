// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Maul Window glue (record mui-0007), the optional target
// maul-ui-window: one glue a window feeds the records the host drains
// from Maul Window to a Maul UI context, keeping what Maul Window does
// not (the cursor's place, the buttons held) and saying of each record
// whether the UI handled it, so that the host hands the rest to its
// game. Keys and text pass as they come, the codes, meanings and
// modifiers being the same; the cursor is a mouse pointer, a touch a
// touch pointer and the pen a pen pointer, their records dispatched at
// once; the wheel turns at the cursor's last place; a reset or a lost
// focus cancels every pointer. The core and the text component
// never depend on it; it is a static library, whose functions
// MUI_WINDOW_API marks.

#ifndef MAUL_UI_WINDOW_GLUE_H
#define MAUL_UI_WINDOW_GLUE_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"
#include "maul-window/event.h"

#include <stdbool.h>
#include <stdint.h>

#define MUI_WINDOW_API extern

// The touches a glue follows at once; a touch beyond them is left. With
// the mouse and the pen they fit the context's default pointers.
#define MUI_WINDOW_TOUCHES 10u

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiWindowGlue muiWindowGlue;

    // How a glue is made. Build it with muiDefaultWindowGlueDef.
    typedef struct muiWindowGlueDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        // The window whose records it takes; others it leaves.
        mwinContext* windows;
        mwinWindowId window;
        // The context and the root the window's input is for, in the
        // window's logical units: the root laid out in the window's size.
        muiContext* context;
        muiNodeId root;
        // The player the window's keyboard and mouse are, below
        // MUI_MAX_PLAYERS.
        uint8_t player;
    } muiWindowGlueDef;

    /// Returns the default glue def: no window, context or root, player 0.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_WINDOW_API muiWindowGlueDef muiDefaultWindowGlueDef(void);

    /// Makes a glue for a window.
    ///
    /// @param def      The glue: a valid cookie and allocator, a Maul
    ///                 Window context, a context, a root and a player.
    /// @param glueOut  Receives the glue; set to NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         def outside the above; `mui_errorCapacity` when memory runs
    ///         out.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiCreateWindowGlue(const muiWindowGlueDef* def,
                                                               muiWindowGlue** glueOut);

    /// Destroys a glue.
    ///
    /// @param glue  The glue, or NULL for nothing.
    /// @par Thread safety
    /// Safe from any thread; the glue is used by one thread at a time.
    MUI_WINDOW_API void muiDestroyWindowGlue(muiWindowGlue* glue);

    /// Takes a record the host drained from Maul Window. A record of its
    /// window goes to the context: a key or text to the player's focus
    /// (muiKeyInput, muiTextInput); the cursor as the player's mouse
    /// (muiPointerInput, its records dispatched with
    /// muiDispatchPointerRecord), Maul Window's button b its index b - 1;
    /// a touch as a touch pointer holding the primary button while in
    /// contact; the pen as a pen pointer, its tip in contact the primary
    /// button or button 5 while it erases, its barrel the secondary, as
    /// the W3C's Pointer Events number them; the wheel at the cursor's
    /// last place (muiWheelInput); a reset or a lost focus as a cancel of
    /// every pointer holding a button. A pointer's records are the UI's
    /// when a record it posts is handled, while the UI holds the pointer
    /// (pressed or captured), or where the point hits a node that does
    /// not pass input through (muiHitTest); a key, text or wheel is the
    /// UI's when routing handles it. Records of other windows and of
    /// other kinds are not the UI's.
    ///
    /// @param glue        The glue.
    /// @param event       The record.
    /// @param handledOut  Receives whether the UI handled it.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         call from a measure, paint or event function, or a record
    ///         Maul UI refuses (its own checks pass it on);
    ///         `mui_errorStale` for a root that is gone;
    ///         `mui_errorCapacity` for a pointer past the context's
    ///         limit.
    /// @par Thread safety
    /// Safe from any thread; the glue and its context are used by one
    /// thread at a time.
    MUI_NODISCARD MUI_WINDOW_API muiResult muiWindowGlue_HandleEvent(muiWindowGlue* glue,
                                                                     const mwinEvent* event,
                                                                     bool* handledOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_WINDOW_GLUE_H
