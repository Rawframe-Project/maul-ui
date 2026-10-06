// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Routed input (record mui-0007). The route is the target and its
// ancestors, written down before the first call; the host's function
// hears it from the top down, then from the target up, until it handles
// it. Unhandled keys and navigation then do what the library does by
// default: move the focus.

#include "maul-ui/event.h"

#include "context.h"
#include "focus.h"
#include "tree.h"

#include "maul-ui/focus.h"

static bool IsNull(muiNodeId nodeId)
{
    return nodeId.index1 == 0;
}

// Whether input may be fed now: not from a measure, paint or event
// function.
static bool MayFeed(const muiContext* context)
{
    return !muiIsInHostCall(context) && !context->events.dispatching;
}

// Routes an event to its target; true when the host handled it.
static bool Route(muiContext* context, uint32_t target, const muiEvent* event)
{
    muiEventStore* store = &context->events;
    if (store->function == nullptr || target == 0)
    {
        return false;
    }
    const muiTree* tree = &context->tree;
    uint32_t count = 0;
    for (uint32_t at = target; at != 0; at = muiTreeAt(tree, at)->links.parent)
    {
        store->route[count++] = muiTreeIdOf(tree, at);
    }
    store->dispatching = true;
    bool handled = false;
    for (uint32_t i = count; i > 0 && !handled; i--)
    {
        if (muiTreeResolve(tree, store->route[i - 1]) != 0)
        {
            handled = store->function(store->user, store->route[i - 1], mui_phaseTunnel, event);
        }
    }
    for (uint32_t i = 0; i < count && !handled; i++)
    {
        if (muiTreeResolve(tree, store->route[i]) != 0)
        {
            handled = store->function(store->user, store->route[i], mui_phaseBubble, event);
        }
    }
    store->dispatching = false;
    return handled;
}

// The node a player's input goes to under root: its focus, the top modal
// layer when that covers the focus, or root.
static uint32_t TargetOf(const muiContext* context, uint32_t root, uint8_t player)
{
    uint32_t focus = 0;
    uint32_t scope = muiFocusScope(context, root, player, &focus);
    return focus != 0 ? focus : scope;
}

// Checks what every input call checks; the root's slot, or 0 with the
// status to return.
static uint32_t Admit(muiContext* context, muiNodeId rootId, uint8_t player, bool valid,
                      muiResult* statusOut)
{
    if (!valid || IsNull(rootId) || player >= MUI_MAX_PLAYERS || !MayFeed(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t root = muiTreeResolve(&context->tree, rootId);
    *statusOut = root != 0 ? mui_success : mui_errorStale;
    return root;
}

muiResult muiSetEventFunction(muiContext* context, muiEventFunction function, void* user)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!MayFeed(context))
    {
        return muiRefuse(context);
    }
    context->events.function = function;
    context->events.user = user;
    return mui_success;
}

// Moves a player's focus as an unhandled key or action asks: sequentially
// or toward a direction; whether it moved.
static bool MoveFocus(muiContext* context, muiNodeId rootId, uint8_t player, bool sequential,
                      bool backward, muiDirection direction)
{
    muiResult result = sequential ? muiFocus_Move(context, rootId, player, backward)
                                  : muiFocus_MoveToward(context, rootId, player, direction);
    return result == mui_success;
}

// The direction an arrow key names, or 4 for another key.
static uint32_t ArrowOf(muiKeyCode code)
{
    switch (code)
    {
    case mui_codeArrowUp:
        return mui_directionUp;
    case mui_codeArrowDown:
        return mui_directionDown;
    case mui_codeArrowLeft:
        return mui_directionLeft;
    case mui_codeArrowRight:
        return mui_directionRight;
    default:
        return 4;
    }
}

muiResult muiKeyInput(muiContext* context, muiNodeId rootId, const muiKeyEvent* event,
                      bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    bool valid = event != nullptr && handledOut != nullptr;
    uint32_t root = Admit(context, rootId, valid ? event->player : 0, valid, &status);
    if (root == 0)
    {
        return status;
    }
    if (event->down)
    {
        context->focus.showsByCode[event->player] = true;
    }
    uint32_t target = TargetOf(context, root, event->player);
    const muiEvent routed = {
        .kind = event->down ? mui_eventKeyDown : mui_eventKeyUp,
        .player = event->player,
        .repeat = event->repeat,
        .modifiers = event->modifiers,
        .code = event->code,
        .key = event->key,
        .timeNs = event->timeNs,
        .target = muiTreeIdOf(&context->tree, target),
    };
    bool handled = Route(context, target, &routed);
    const muiModifiers held =
        event->modifiers & (mui_modShift | mui_modControl | mui_modAlt | mui_modMeta);
    if (!handled && event->down)
    {
        uint32_t arrow = ArrowOf(event->code);
        if (event->code == mui_codeTab && (held & ~mui_modShift) == 0)
        {
            handled = MoveFocus(context, rootId, event->player, true, held != 0, 0);
        }
        else if (arrow != 4 && held == 0)
        {
            handled = MoveFocus(context, rootId, event->player, false, false, (muiDirection)arrow);
        }
    }
    *handledOut = handled;
    return mui_success;
}

muiResult muiTextInput(muiContext* context, muiNodeId rootId, const muiTextEvent* event,
                       bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    bool valid =
        event != nullptr && handledOut != nullptr && (event->text != nullptr || event->length == 0);
    uint32_t root = Admit(context, rootId, valid ? event->player : 0, valid, &status);
    if (root == 0)
    {
        return status;
    }
    uint32_t target = TargetOf(context, root, event->player);
    const muiEvent routed = {
        .kind = mui_eventText,
        .player = event->player,
        .length = event->length,
        .timeNs = event->timeNs,
        .target = muiTreeIdOf(&context->tree, target),
        .text = event->text,
    };
    *handledOut = Route(context, target, &routed);
    return mui_success;
}

muiResult muiNavigationInput(muiContext* context, muiNodeId rootId, const muiNavigationEvent* event,
                             bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    bool valid = event != nullptr && handledOut != nullptr && event->action <= mui_navigateCancel;
    uint32_t root = Admit(context, rootId, valid ? event->player : 0, valid, &status);
    if (root == 0)
    {
        return status;
    }
    context->focus.showsByCode[event->player] = true;
    uint32_t target = TargetOf(context, root, event->player);
    const muiEvent routed = {
        .kind = mui_eventNavigation,
        .player = event->player,
        .navigation = event->action,
        .timeNs = event->timeNs,
        .target = muiTreeIdOf(&context->tree, target),
    };
    bool handled = Route(context, target, &routed);
    if (!handled && event->action <= mui_navigateRight)
    {
        // The directions are numbered as muiDirection's.
        handled =
            MoveFocus(context, rootId, event->player, false, false, (muiDirection)event->action);
    }
    else if (!handled && event->action <= mui_navigatePrevious)
    {
        handled = MoveFocus(context, rootId, event->player, true,
                            event->action == mui_navigatePrevious, 0);
    }
    *handledOut = handled;
    return mui_success;
}

muiResult muiDispatchPointerRecord(muiContext* context, const muiPointerRecord* record,
                                   bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (record == nullptr || handledOut == nullptr || !MayFeed(context))
    {
        return muiRefuse(context);
    }
    uint32_t target = IsNull(record->node) ? 0 : muiTreeResolve(&context->tree, record->node);
    const muiEvent routed = {
        .kind = mui_eventPointer,
        .timeNs = record->timeNs,
        .target = record->node,
        .pointer = record,
    };
    *handledOut = Route(context, target, &routed);
    return mui_success;
}
