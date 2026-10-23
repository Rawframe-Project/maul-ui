# Interaction

A UI over a game or an application answers input from a mouse, touch, keys and gamepads. It finds what a point is over, tracks hover, press and focus, routes input through layers such as dialogs and menus, and lets input it leaves unused reach what lies behind it. The web, WPF, Flutter, Godot, Unity and Roblox agree on most of this. Maul UI owns the tree and its layout, so it answers these questions itself instead of each host repeating them. Input arrives as the library's own event records, and nothing calls the host back except the one event function during routing.

## Contents

- [Interaction properties](#interaction-properties)
- [Layers](#layers)
- [Hit testing](#hit-testing)
- [Pointers](#pointers)
- [Focus](#focus)
- [Routing](#routing)
- [Drags, drops and ranges](#drags-drops-and-ranges)
- [Popups](#popups)
- [Exits](#exits)

## Interaction properties

Interaction values are the fourth property group (`maul-ui/interaction.h`), styled through classes, variants, conditions and direct writes like any other, and not inherited. They say:

- what a point can hit: the node in its rounded border box and its children (the default), its children alone, or nothing of its subtree;
- whether input the node is hit by but leaves unused passes through to what lies behind the UI;
- the layer kind, the focus mode and tab order, whether the node takes drags, which kinds of dropped thing it accepts, and how it leaves the flow while exiting.

## Layers

A layer kind makes a node the root of an activation layer (a dialog or a menu), a modal one, or a part of the overlay band (popups and tooltips) ([layer.c](../../src/layer.c)). The context holds the nodes that root a layer, up to its `layers` limit, in paint order: activation layers in the order they became layers or were raised (`muiNode_RaiseLayer`), then the overlay band, each in the same order. A node that becomes a layer past the limit stays in its parent's layer until its kind changes again. The [Draw list](draw-list.md#paint-order-and-layers) page says how layers paint.

## Hit testing

`muiHitTest` finds the topmost node of a root's subtree at a point: the last in paint order whose rounded border box holds it, inside the rounded clip of every clipping ancestor, under no node whose hit mode leaves it out ([hit.c](../../src/hit.c)). Boxes are half open, and opacity does not matter, as in CSS. The result is the node, the point in its border box and its pass-through flag. A point that hits nothing passes through.

- The walk is preorder over the tree's links with no stack. The point goes into each node's space in doubles as the walk climbs down and back out as it climbs up: places are taken off and added back exactly, and a local scale is divided out and multiplied back. A node scaled to nothing on an axis is hit nowhere, nor is its subtree.
- Layers are tried from the top down before the content they are not in. A point a modal layer's subtree misses hits the modal layer's root, blocked, with no pass-through and nothing below it.
- `muiNode_MapToRoot` goes the other way, carrying a point of a node's border box through its own and its ancestors' places, local scales and scroll offsets, in doubles, so a point a hit found maps back to where it was.

Positions are summed as painting sums them but in doubles, so they can differ from painted ones by a float's rounding, which no pointer resolves. A randomized test checks that the node hit is the one whose box was painted last at the point. A walk is linear in the nodes whose subtree the point can reach, and a layer's place is found by adding its ancestors' places, linear in its depth.

## Pointers

The host passes its pointer events as `muiPointerEvent` records (time, pointer id, kind, action, button, held buttons, point) to `muiPointerInput`, which hit tests and updates the pointer ([pointer.c](../../src/pointer.c)).

- **Hover and press.** A node is hovered while a pointer's topmost node is it or a descendant, and pressed while a pointer that pressed it or a descendant holds a button, as CSS's `:hover` and `:active`. A touch hovers only in contact. Per node the library counts the pointers whose hover chain, and press chain, holds it, apart from the host's own states, and styling sees their union. Only a count leaving or reaching 0 restyles a node. Inserting, detaching or destroying a subtree a pointer's chain runs through takes that pointer's counts off first and puts them back after.
- **Capture.** A touch or a pen captures to what it pressed, and `muiPointer_SetCapture` captures any pointer holding a button. A captured pointer hovers its target and its records go there, until its last release, a cancel, a release of the capture or the target's destruction, each with a capture-lost record.
- **Records.** Presses, releases, clicks, cancels, lost captures and captured moves wait in a ring of `limits.pointerRecords`, taken with `muiNextPointerRecord`. Each has its node, the point in its border box, the buttons, the click count and pass-through. A full ring counts what it drops into one record, as the notification queue does. Records keep input free of callbacks into the host.
- **Clicks.** A release clicks the capture target, or else the nearest common ancestor of the press's node and the release's, as UI Events does. Every button clicks and names itself. The click count grows while presses of one button follow within an interval and distance (500 ms and 2 units unless `muiSetClickRule` says otherwise).

## Focus

Focus is kept per player slot, up to `MUI_MAX_PLAYERS` (8), for local multiplayer ([focus.c](../../src/focus.c)). The focus mode (none; by pointer and code only; also by sequential navigation) and the tab order (0 for tree order; 1 to 255 first, ascending) decide which nodes take it. Disabled and exiting nodes, and nodes a modal layer covers, take none.

- **Shown or not.** `muiFocus_Set` takes a cause: navigation shows the focus, a pointer hides it, and code follows the player's last cause, as CSS's `:focus-visible` heuristics do. The node is `mui_stateFocused`, and `mui_stateFocusVisible` while shown.
- **Pointer presses** focus the nearest node from the target up that takes focus, for the event's player, or take the focus away.
- **Given back.** A node becoming a layer records, per player, what the player focuses outside it, as HTML's dialogs and popovers record their previously focused element. When a subtree begins to exit or is destroyed with a player's focus inside, the player gets back the opener of the outermost layer between the focus and the subtree's root, if it still exists and takes focus. A press that took the focus out first leaves nothing to give back, as HTML's light dismiss does. Detaching is a move, not a close, so it keeps the focus.
- **Sequential navigation** (`muiFocus_Move`) runs over the layer that holds the focus, without the layers inside it, and wraps. With no focus under the root, it starts in the top modal layer or the root's content.
- **Directional navigation** (`muiFocus_MoveToward`) moves up, down, left or right on the screen, which right to left does not flip ([navigate.c](../../src/navigate.c)). A link the focused node has for the direction (`muiNode_SetNeighbor`) wins, and a link to itself stops the move. Otherwise it follows Android's focus search exactly, over the laid-out boxes of the nodes Tab reaches in the same scope. A node must lie past the focus in the direction. One overlapping the focus across the direction beats one that does not. Else the least `13 * major^2 + minor^2` wins, the gap along the direction and the distance between centers across it, and ties go to tree order. There is no wrap.
- **Notifications** report each player's focus gained and lost, whatever moved it. A focused node that is destroyed, or stops taking focus, loses it.

## Routing

`muiKeyInput`, `muiTextInput` and `muiNavigationInput` go to a player's focus under the root, or to the top modal layer when one covers the focus, or to the root when there is none. `muiDispatchPointerRecord` and `muiWheelInput` go to a node ([event.c](../../src/event.c)).

One host function (`muiSetEventFunction`) hears each event at every node on the route, from the top of the target's tree down to the target and then back up, and returns whether it handled the event, which ends the route. The route is written down first, in a buffer of one id per node reserved with the context, so a function that edits the tree changes neither who hears the event nor in what order, and nodes gone since are passed over. The function may edit the tree, focus and capture, but not feed input.

Keys carry the USB HID code, the layout's key (a code point, or `MUI_KEY_NAMED` with the code) and modifiers, as Maul Window's do. Unhandled keys have defaults: Tab and Shift+Tab and navigation next and previous move focus sequentially; the arrows without modifiers and the four navigation directions move it directionally or scroll ([Scrolling and lists](scrolling.md#keys-and-the-wheel)). A move counts as handled. Each call reports whether the UI handled the input, so the host hands the rest to the game behind it.

## Drags, drops and ranges

- **Drags.** A node takes drags by the interaction property `drags`. A press on it, or below it where no nearer node takes them, becomes a drag once it moves past the threshold on either axis: 4 units for a mouse, as Windows has it, and 8 for touch and pens, Android's touch slop (`muiSetDragThreshold`). The node captures the pointer and gets drag start, move and end records with the offset from the press, and the release clicks nothing. A cancel, a lost capture and an unhandled Escape end the drag cancelled.
- **Drag and drop** within the application: a node accepts kinds of thing by the interaction property `accepts`, a mask of the application's bits. While a pointer drags, the host offers a kind and a key of its own (`muiPointer_Offer`). The drag then targets the nearest node from the one under the pointer up that accepts the kind, with enter and leave records as the target changes and a drop record when it ends over one. The host draws the preview and styles the source and target from the records.
- **Range values** (`maul-ui/range.h`): a node may hold a minimum, a maximum, a value on the minimum plus whole steps, a page, an axis and an optional thumb node ([range.c](../../src/range.c)). A focused range takes ARIA's slider keys along its axis, and gamepad directions likewise; across its axis, keys navigate. A press on its track pages toward the point, and a drag of it moves the value with the pointer, keeping where the thumb was grabbed. Changes from input are reported by `mui_notificationRangeChanged`; changes from code are not.

## Popups

A node may have a popup record: an anchor node, a side (below, above, start, end, center), an alignment, a gap and a margin ([popup.c](../../src/popup.c)). Each layout places it: its border box goes beside the anchor's on the surface, through scrolling. Per axis it flips to the opposite side when it overflows the root's box less the margin and the other side has more room, then is clamped into that box, keeping its start edge when it cannot fit, as Wayland's positioner without resizing. A popup anchored inside another is placed after it. The side used is readable, for an arrow. The layer kind still decides how it paints.

A popup record stands for an open popup. Light dismissal is on by default and off for manual popups, as HTML's popovers. It reports and never closes: `mui_notificationPopupDismissed` names the reason, once until the popup is set again.

- A press dismisses, nested popups first, every popup that neither holds the pressed node nor has its anchor holding it, keeping the popups those nest under, as HTML's popover stack does.
- An Escape no handler takes dismisses the popup set last.
- Focus moved by code or navigation outside a popup and its anchor dismisses it.

## Exits

`muiNode_BeginExit` gives a node the exiting state, which only exits set and clear, so its classes' exiting variants and transitions play ([exit.c](../../src/exit.c)). The node and its subtree leave hit testing (what lies behind is hit), focus and navigation at once, and a modal layer inside no longer blocks. After transitions advance in a layout, an exit with no transition running in its subtree is reported once by `mui_notificationExitFinished`. The host then destroys the node or cancels the exit. The library never destroys a node by itself.

The interaction property `exitLayout`, read when the exit begins, keeps the node in its place (the default) or pops it out of its parent's flow at its last rectangle, its children laid out at that size, so its siblings close up while it plays out, as Motion's `popLayout` does. Cancelling puts it back.
