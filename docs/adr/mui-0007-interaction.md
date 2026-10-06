# mui-0007. Interaction

Status: Accepted

## Context

A UI over a game or an application answers input from a mouse, touch,
keys and gamepads: it finds what a point is over, tracks hover, press
and focus, routes input through layers such as dialogs and menus, and
lets input it leaves unused reach what lies behind it. The web, WPF,
Flutter, Godot, Unity and Roblox agree on most of it: hit testing in
reverse paint order, a per-node choice of whether the node, its
children or neither are hit, clips that cut hits as they cut drawing,
and layers above the base content in the order they were opened.
Maul UI owns the tree and its layout, so it can answer these questions
itself instead of each host repeating them.

## Decision

- **Interaction properties** (`maul-ui/interaction.h`) are a fourth
  property group, ids from 192, styled like any other through classes,
  state variants, conditions and direct writes, and not inherited. The
  hit mode says what a point can hit: the node in its rounded border
  box and its children (`mui_hitAuto`, the default), its children alone
  (`mui_hitChildren`), or nothing of its subtree (`mui_hitNone`). The
  pass-through flag says whether input the node is hit by but leaves
  unused goes on to what lies behind the UI.
- **Layers:** a third interaction property, the layer kind, makes a
  node root an activation layer (a dialog or a menu), a modal one, or a
  part of the overlay band (popups and tooltips). The context holds the
  nodes that root a layer, up to its `layers` limit, in paint order:
  activation layers in the order they became layers or were raised
  (`muiNode_RaiseLayer`), the overlay band after them all, each band in
  the same order. A draw list paints a root's subtree without the layers
  below it, then each layer below it from the bottom: at its laid-out
  place, but outside its ancestors' clips and opacity, as the web's top
  layer is. A node that becomes a layer past the limit stays in its
  parent's layer until its kind changes again; a destroyed one's room is
  taken back when needed.
- **Hit testing** (`muiHitTest`) finds the topmost node of a root's
  subtree at a point: the last in paint order whose rounded border box
  holds it, inside the rounded clip of every clipping ancestor, under
  no node whose mode leaves it out. Boxes are half open. Opacity does
  not matter, as in CSS. The result is the node, the point in its
  border box and its pass-through flag; a point that hits nothing
  passes through. The walk is preorder over the tree's links with no
  stack; origins are summed in doubles so that climbing back up takes
  off exactly what climbing down added. Layers are tried from the top
  down before the content they are not in; a point a modal layer's
  subtree misses hits the modal layer's root, blocked, with no
  pass-through, and nothing below it.
- **Pointer input** (`maul-ui/pointer.h`): the host passes its pointer
  events as `muiPointerEvent`s (time, pointer id, kind, action, button,
  buttons, point) to `muiPointerInput`, which hit tests and updates the
  pointer. A node is hovered while a pointer's topmost node is it or a
  descendant, and pressed while a pointer that pressed it or a
  descendant holds a button, as CSS's `:hover` and `:active`; a touch
  hovers only in contact. Per node the library counts the pointers
  whose hover chain, and press chain, holds it, apart from the host's
  states; styling and `muiNode_GetStates` see their union, and a count
  leaving or reaching 0 restyles the node. Inserting, detaching or
  destroying a subtree a pointer's chain runs through takes that
  pointer's counts off first and puts them back after.
- **Capture:** a touch or a pen captures to what it pressed;
  `muiPointer_SetCapture` captures any pointer holding a button. A
  captured pointer hovers its target and its records go there, until
  its last release, a cancel, `muiPointer_ReleaseCapture` or the
  target's destruction, each with a capture-lost record.
- **Records** wait in a ring of `limits.pointerRecords`, taken with
  `muiNextPointerRecord`; a full ring counts what it drops into one
  record, as notifications do. They report presses, releases, clicks,
  cancels, lost captures and captured moves, each with its node, the
  point in its border box, the buttons, the click count and
  pass-through. A release clicks the capture target, or else the
  nearest common ancestor of the press's node and the release's, as
  UI Events does; every button clicks and names itself. The click
  count grows while presses of one button follow within an interval
  and distance (500 ms and 2 units unless `muiSetClickRule` says
  otherwise).
- **Focus** (`maul-ui/focus.h`) is kept per player slot, up to
  `MUI_MAX_PLAYERS` (8), for local multiplayer. Two interaction
  properties decide which nodes take it: the focus mode (none; by
  pointer and code only; also by sequential navigation) and the tab
  order (0 for tree order; 1 to 255 first, ascending). Disabled and
  exiting nodes, and nodes a modal layer covers, take none.
  `muiFocus_Set` takes a cause: navigation shows the focus, a pointer
  hides it, code follows the player's last cause, as CSS's
  `:focus-visible` heuristics do. The node gets `mui_stateFocused`, and
  `mui_stateFocusVisible` while shown, a new state between focused and
  hovered. A pointer press focuses the nearest node from its target up
  that takes focus, for the event's player, or takes the focus away.
- **Sequential navigation** (`muiFocus_Move`) runs over the layer that
  holds the focus, without the layers in it, and wraps; with no focus
  under the root, or a covered one, it starts in the top modal layer
  under the root, or else the root's content. One walk finds the
  focus's place and one the nearest nodes around it.
- **Directional navigation** (`muiFocus_MoveToward`) moves toward up,
  down, left or right on the screen, which right to left text does not
  flip. A link the focused node has for the direction
  (`muiNode_SetNeighbor`, kept in a context table of `limits.neighbors`)
  to a node that takes focus wins; a link to the node itself stops the
  move. Otherwise it follows Android's focus search exactly, over the
  laid-out border boxes of the nodes Tab reaches in the same scope: a
  node must lie past the focus in the direction; one overlapping the
  focus across the direction beats one that does not (moving up or
  down, only when it is nearer than the other's far edge); else the
  least `13 * major^2 + minor^2` wins, the gap along the direction and
  the distance between centers across it; ties go to tree order. There
  is no wrap.
- **Routed input** (`maul-ui/event.h`): `muiKeyInput`, `muiTextInput`
  and `muiNavigationInput` go to a player's focus under the root (the
  top modal layer when one covers the focus, the root when there is
  none), and `muiDispatchPointerRecord` to a pointer record's node. One
  host function (`muiSetEventFunction`) hears each event at every node
  on the route, from the top of the target's tree down to the target,
  then back up, and returns whether it handled it, which ends the
  route. The route is written down first, in a buffer of one id per
  node reserved with the context, so a function that edits the tree
  changes neither who hears the event nor in what order; nodes gone
  since are passed over. The function may edit the tree, focus and
  capture, not feed input. Keys carry the USB HID code, the layout's
  key (a code point, or `MUI_KEY_NAMED` with the code) and modifiers,
  as Maul Window's do. Unhandled, Tab and Shift+Tab, navigation next
  and previous move focus sequentially, the arrows without modifiers
  and the four navigation directions directionally, and a move counts
  as handled; each call reports whether the UI handled the input, so
  the host hands the rest to the game. Key downs and navigation make
  the player's next focus by code shown.
- **Scrolling** (`maul-ui/scroll.h`): a node whose layout property
  `scrollAxes` names an axis is a scroll container. It clips its
  children at its rounded padding box and its flex automatic minimum
  is 0, as CSS's. Layout measures its extent, logically from the start:
  the furthest end of its children's margin boxes (border boxes for
  absolute ones) plus its end padding, at least its padding box.
  Offsets are logical, clamped to 0 through the extent less the padding
  box, and kept there by later layouts. Painting draws the children
  unscrolled through a transform per scroll container, its offset
  composed with its ancestors' and rounded to device pixels; a build
  whose only change is offsets rewrites the transform table and keeps
  the commands; copies renumber transforms as they do clips. Hit
  testing, pointer records, directional navigation and layers add the
  offsets of scrolling ancestors. `muiNode_ScrollIntoView` scrolls each
  scrolling ancestor, nearest first, as CSSOM View's "nearest", and
  focus moved by navigation does so. A node that stops scrolling drops
  its offset.
- **Wheel input** (`muiWheelInput`) is routed from the node under its
  point. Unhandled, it scrolls the nearest scroll container from there
  up that can move that way, not past the given root or a layer's root,
  by the scroll rule's step a detent (`muiSetScrollRule`, 100 by
  default); the container keeps later turns while they come within the
  rule's latch time (500 ms) and over it, as Firefox's wheel
  transaction, so a list at its end does not hand a turn to the page
  behind. Shift turns a vertical-only turn horizontal.
  `muiNode_GetScrollThumb` places a scrollbar thumb on a host's track.
- **Focus notifications** report each player's focus gained and lost,
  whatever moved it; a focused node that is destroyed, or stops taking
  focus at its styling or a direct write, loses it. Detaching keeps it,
  since moving a node is a detach and an insert.

## Consequences

A host routes a point by one call, and the same layout, clips and
corners decide what is drawn and what is hit; a randomized test checks
that the node hit is the one whose box was painted last at the point.
Positions are summed as painting sums them, but in doubles, so they can
differ from painted ones by a float's rounding, which no pointer
resolves. A walk visits every node whose subtree the point can reach,
which is linear in their number; a spatial index may come with
virtualization if large trees need it. A layer's place is found by
adding its ancestors' places, linear in its depth, at each build and
hit test. A hover change touches only the nodes whose counts pass 0;
a tree edit under a pointer walks that pointer's chain twice. Records
keep input free of callbacks into the host. Focus, routing, scrolling
and drags build on this record as they are added.
