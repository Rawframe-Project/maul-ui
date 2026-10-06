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
hit test. Pointer state, focus, routing, scrolling and drags build on
this record as they are added.
