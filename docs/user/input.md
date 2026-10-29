# Input

## Contents

- [Routing](#routing)
- [Pointers](#pointers)
- [Focus and keys](#focus-and-keys)
- [Gamepads](#gamepads)
- [Drags, drops and ranges](#drags-drops-and-ranges)

## Routing

The program hands the context its platform's input. The context finds the nodes it concerns and routes it to one function of the program's, at each node from the root down to the target (tunnel), then back up (bubble). Whatever no node handles is the program's again: a game behind its interface takes the clicks and keys the interface leaves.

```c
// What the program keeps: the button it watches and its clicks.
typedef struct App
{
    muiNodeId ok;
    int clicks;
} App;

// The function routed events reach, at each node from the root down to
// the target, then back up; true stops them. The OK button clicked, or
// activated by a gamepad's confirm button or by assistive technology, is
// the program's.
static bool OnEvent(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    App* app = user;
    bool clicked =
        event->kind == mui_eventPointer && event->pointer->kind == mui_pointerRecordClick;
    bool activated =
        event->kind == mui_eventNavigation && event->navigation == mui_navigateActivate;
    if (phase == mui_phaseBubble && (clicked || activated) && nodeId.index1 == app->ok.index1 &&
        nodeId.generation == app->ok.generation)
    {
        app->clicks++;
        return true;
    }
    return false;
}
```

`muiContext_SetEventFunction(context, OnEvent, &app)` sets it. A button takes its activation too. A gamepad's confirm button (`muiNode_RouteNavigation`) and assistive technology's press arrive as activation, not as a click, so a button that heeds only clicks is one they cannot press.

The event function may change the tree, the focus and pointer capture, and the event still reaches the nodes it was routed to, in order. It may not feed more input from inside the route.

## Pointers

Pointer events go in after the frame's layout, which hit testing reads. Each makes records, which the program takes and routes:

```c
// Takes a platform's pointer event after the frame's layout, then
// routes the records it made: presses, releases, clicks, drags. Whether
// the interface used it, a record handled or a node under the pointer
// that does not let input through; the rest is the game's, behind it.
static bool Pointer(muiContext* context, muiNodeId root, const muiPointerEvent* event)
{
    if (muiNode_RoutePointer(context, root, event) != mui_success)
    {
        return false;
    }
    bool handled = false;
    muiPointerRecord record;
    while (muiContext_TakePointerRecord(context, &record) == mui_success)
    {
        bool taken = false;
        (void)muiContext_DispatchPointerRecord(context, &record, &taken);
        handled = handled || taken;
    }
    muiHit hit;
    return handled || (muiNode_HitTest(context, root, event->x, event->y, &hit) == mui_success &&
                       !hit.passThrough);
}
```

Moving over a node puts it and its ancestors in the hovered state, and a press puts them in the pressed state while the button is held, as CSS's `:hover` and `:active`. A press and release on the same node make a click, and two close in time a double click (`muiContext_SetClickRule`).

- A node can capture a pointer (`muiNode_CapturePointer`) to keep its moves; touches and pens capture to what they pressed.
- A node whose interaction style lets input pass through, such as a HUD over a game world, is hit and hovered but leaves the press to what lies behind it.
- A node's hit mode can make it, or its whole subtree, invisible to the pointer.
- Hit testing follows rounded corners, clips, scrolling and scales, so what is hit is what was drawn at the point.

## Focus and keys

The focus is the node a player's keys and gamepad go to, one per player for local multiplayer. A press on a node that takes the focus focuses it; code moves it with `muiContext_SetFocus`, and keys move it:

```c
// Lets a node take the focus from keys and pointers alike.
static muiResult MakeFocusable(muiContext* context, muiNodeId node)
{
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    return muiNode_SetInteractionValues(context, node, &interaction,
                                        MUI_PROPERTY_BIT(mui_propertyFocusMode));
}
```

```c
// Hands a key to the focus under the root. Unhandled, Tab moves the
// focus, arrows move it toward their side or scroll, and the rest is
// the game's.
static bool Key(muiContext* context, muiNodeId root, muiKeyCode code, muiModifiers modifiers,
                bool down)
{
    const muiKeyEvent event = {
        .key = MUI_KEY_NAMED | code, .code = code, .modifiers = modifiers, .down = down};
    bool handled = false;
    return muiNode_RouteKey(context, root, &event, &handled) == mui_success && handled;
}
```

Unhandled, Tab and Shift with Tab move the focus in tree order, or in the tab order the interaction style gives. The arrows move it toward the nearest node on their side, as a gamepad's stick does, or scroll the container holding it. `muiNode_SetNeighbor` overrides where a direction goes from a node.

The focused node is in the focused state, and in the focus visible state when keys moved the focus there, so a style can draw a ring then and not after a click. Typed text goes to the focus with `muiNode_RouteText`, and a text field there takes it ([Text editing](text-editing.md)).

Dialogs and menus are layers (`layer` in the interaction style). A modal layer keeps the focus and the pointer inside it, and Tab cycles within it. When a dialog closes or exits, the focus goes back to what the player had focused before it opened.

## Gamepads

`muiNode_RouteNavigation` takes a player's directions, activate, cancel, and next and previous. The directions move the focus by what is on screen, a slider takes them as steps, and activate reaches the focused node as its activation. The library keeps no clock, so holding a direction repeats only if the program repeats it; the [Maul Window](maul-window.md) glue maps a gamepad's d-pad, stick, face buttons and shoulders this way, with repeat.

## Drags, drops and ranges

- **Drags.** A node with `drags` set in its interaction style captures a press that moves past the drag threshold (`muiContext_SetDragThreshold`) and gets drag start, move and end records with the offset from the press. Escape, a cancel or a lost capture cancels the drag.
- **Drag and drop.** While dragging, the program offers a kind of thing and a key of its own (`muiContext_OfferDrop`). Nodes that accept that kind (`accepts`) get enter, leave and drop records. The program draws the preview, an overlay node it moves by the drag's offsets.
- **Ranges.** A node with a value range (`muiNode_SetValueRange`) is a slider or a scrollbar: a minimum, a maximum, a step, a page and an axis. Focused, it takes the arrow keys, Page Up and Down, Home and End along its axis; pressing its track pages toward the point; dragging it moves the value with the pointer. Changes from input are reported by `mui_notificationRangeChanged`, and the program places the thumb from the value.
