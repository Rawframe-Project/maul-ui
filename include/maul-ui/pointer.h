// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pointer input (record mui-0007): the host's pointer events become hover
// and press states for styling, pointer capture, and records of presses,
// releases, clicks and cancels the host takes after each call.

#ifndef MAUL_UI_POINTER_H
#define MAUL_UI_POINTER_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // The device a pointer is.
    typedef uint8_t muiPointerKind;

    enum
    {
        // Hovers without a button down.
        mui_pointerMouse = 0,
        // Hovers only in contact, and captures to what it presses.
        mui_pointerTouch = 1,
        // Hovers in range, and captures to what it presses.
        mui_pointerPen = 2,
    };

    // The buttons a pointer holds, as bits: 1 << the button's index.
    typedef uint8_t muiPointerButtons;

    enum
    {
        // The index of a button: the primary (a mouse's left, a touch's
        // contact, a pen's tip), the secondary, the middle, back, forward.
        mui_buttonPrimary = 0,
        mui_buttonSecondary = 1,
        mui_buttonMiddle = 2,
        mui_buttonBack = 3,
        mui_buttonForward = 4,
    };

    // What happened to a pointer.
    typedef uint8_t muiPointerAction;

    enum
    {
        // It moved, or the buttons it holds changed without a press or a
        // release being reported.
        mui_pointerMove = 0,
        // A button went down.
        mui_pointerPress = 1,
        // A button went up.
        mui_pointerRelease = 2,
        // The system ended its stream: nothing it pressed is clicked.
        mui_pointerCancel = 3,
        // It left the surface the root is on.
        mui_pointerLeave = 4,
    };

    // One pointer event, as the host's windowing layer reported it.
    typedef struct muiPointerEvent
    {
        // When it happened, in nanoseconds on a monotonic clock.
        uint64_t timeNs;
        // The host's id for the pointer, the same while it exists.
        uint32_t pointer;
        muiPointerKind kind;
        muiPointerAction action;
        // For a press or a release, the index of the button.
        uint8_t button;
        // The buttons held after the event.
        muiPointerButtons buttons;
        // The point, in the space the root's rectangle is in.
        float x;
        float y;
    } muiPointerEvent;

    /// Takes a pointer event. Hit testing finds the node at the point, as
    /// muiHitTest does, unless the pointer is captured; then the pointer's
    /// hover is updated, and so is its press and capture, and records are
    /// posted for muiNextPointerRecord.
    ///
    /// A node is in mui_stateHovered while a pointer's topmost node is it
    /// or one of its descendants, as CSS's :hover, and in
    /// mui_statePressed while a pointer that pressed it or a descendant
    /// holds a button, wherever the pointer went since, as :active; both
    /// join the states the host sets, and the node is styled again at the
    /// next muiComputeLayout. A touch hovers only in contact. Hover reads
    /// the last layout: after one, a move to the same point updates it.
    ///
    /// @param context  The context.
    /// @param rootId   The root of the subtree under the pointer.
    /// @param event    The event: a known kind and action, a button below
    ///                 8, a point that is finite.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an event outside the above or a call from a
    ///         measure or paint function, which changes nothing;
    ///         `mui_errorStale` for a root that is gone; `mui_errorCapacity`
    ///         for a new pointer past the context's pointers limit.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiPointerInput(muiContext* context, muiNodeId rootId,
                                                    const muiPointerEvent* event);

    // What a pointer record reports.
    typedef uint8_t muiPointerRecordKind;

    enum
    {
        // A button went down on the node.
        mui_pointerRecordPress = 1,
        // A button went up over the node.
        mui_pointerRecordRelease = 2,
        // A press and its release: on the capture target, or else the
        // nearest common ancestor (or self) of where each happened. Every
        // button clicks; the record names it, so the host can keep
        // secondary clicks apart as the web's auxclick does.
        mui_pointerRecordClick = 3,
        // The pointer's stream was cancelled while it held a button.
        mui_pointerRecordCancel = 4,
        // The node lost the pointer's capture.
        mui_pointerRecordCaptureLost = 5,
        // A captured pointer moved.
        mui_pointerRecordMove = 6,
        // clickCount records were dropped here, past the limit.
        mui_pointerRecordDropped = 7,
    };

    // A record of pointer input, in the order it happened.
    typedef struct muiPointerRecord
    {
        muiPointerRecordKind kind;
        muiPointerKind pointerKind;
        // For a press, release or click, the button's index.
        uint8_t button;
        // The buttons held after the event.
        muiPointerButtons buttons;
        // Whether input the node leaves unused passes through to what
        // lies behind the UI, as muiHit's.
        bool passThrough;
        uint32_t pointer;
        // The presses in a quick series this press, release or click
        // belongs to: 2 for a double click; for a dropped record, how many
        // were dropped.
        uint32_t clickCount;
        // The node; the null id for a dropped record.
        muiNodeId node;
        // The point in the node's border box.
        float x;
        float y;
        uint64_t timeNs;
    } muiPointerRecord;

    /// Takes the oldest pointer record.
    ///
    /// @param context    The context.
    /// @param recordOut  Receives the record.
    /// @return `mui_success`; `mui_empty` when none is waiting;
    ///         `mui_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNextPointerRecord(muiContext* context,
                                                         muiPointerRecord* recordOut);

    /// Sends every later event of a pointer that holds a button to a node,
    /// wherever it is, until its last button is released, it is
    /// cancelled, the capture is released or the node is destroyed. It
    /// hovers the node's chain from the next event on. A node that had
    /// the capture gets a capture-lost record.
    ///
    /// @param context  The context.
    /// @param pointer  The host's id of the pointer.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, a pointer the context does not know or that holds
    ///         no button, or a call from a measure or paint function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiPointer_SetCapture(muiContext* context, uint32_t pointer,
                                                          muiNodeId nodeId);

    /// Releases a pointer's capture, with a capture-lost record; its next
    /// event hit tests again.
    ///
    /// @param context  The context.
    /// @param pointer  The host's id of the pointer.
    /// @return `mui_success`, also when it was not captured;
    ///         `mui_errorInvalid` for a NULL context, a pointer the context
    ///         does not know, or a call from a measure or paint function.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiPointer_ReleaseCapture(muiContext* context,
                                                              uint32_t pointer);

    // Where a pointer is and what it holds.
    typedef struct muiPointerState
    {
        muiPointerKind kind;
        muiPointerButtons buttons;
        // Its last point, in its root's space.
        float x;
        float y;
        // The node whose chain it hovers, the node it pressed while it
        // holds a button, and its capture target; the null id for none.
        muiNodeId hovered;
        muiNodeId pressed;
        muiNodeId captured;
    } muiPointerState;

    /// Reads a pointer's state.
    ///
    /// @param context   The context.
    /// @param pointer   The host's id of the pointer.
    /// @param stateOut  Receives the state.
    /// @return `mui_success`; `mui_empty` for a pointer the context does
    ///         not know (it left, lifted or was cancelled);
    ///         `mui_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiPointer_GetState(const muiContext* context, uint32_t pointer,
                                                        muiPointerState* stateOut);

    /// Sets when presses count as one series (a double click): a press
    /// of the same button within intervalNs of the last and within
    /// distance of it on each axis. The defaults are 500 ms and 2,
    /// Windows's; hosts pass the platform's setting.
    ///
    /// @param context     The context.
    /// @param intervalNs  The time between presses, in nanoseconds.
    /// @param distance    The distance on each axis, finite and at least 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, a
    ///         distance outside the above, or a call from a measure or
    ///         paint function.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetClickRule(muiContext* context, uint64_t intervalNs,
                                                    float distance);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_POINTER_H
