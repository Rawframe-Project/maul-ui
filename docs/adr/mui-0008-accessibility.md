# mui-0008. Accessibility

Status: Accepted

## Context

Screen readers and other assistive technology read a UI through the
platform's accessibility API: UI Automation on Windows, AT-SPI on
Linux, NSAccessibility on macOS, UIAccessibility on iOS, Android's
accessibility nodes, and the browser's accessibility tree on the web.
Each wants the same things of every element: what it is, its name and
value, its states, where it is, its children, which one has the focus,
and what it can be asked to do. AccessKit gives a toolkit one schema
for all of them: nodes sent whole in updates, the focus with every
update, and requests coming back. Its platforms call in on threads of
their own (UI Automation does), while a Maul UI context is used by one
thread at a time (family record 0017). Maul UI already holds most of
what the schema asks for: rectangles, scrolling, focus, states, value
ranges and virtual lists.

## Decision

- **The core produces, adapters keep** (`maul-ui/access.h`). The
  library builds updates in AccessKit's shape: the nodes that changed,
  each whole, the focus with every update and the root with the first.
  It keeps no platform state. Adapters keep their own copies under
  their own locks and answer their platforms from them; the host hands
  them each update, and applies on its thread the requests they queue.
- **Every node is a node.** Its id is its handle packed in 64 bits,
  generation high and index low, so a reused slot is new to the
  platform. Its role is `mui_roleGeneric` until the host gives one; an
  adapter leaves generic nodes out and shows their children in their
  place, so layout-only nodes cost the reader nothing.
- **Roles** are a closed, append-only enum: AccessKit's roles less the
  browser's document internals (text runs, layout tables, frames, web
  areas, PDF and graphics roles, DPUB, inline formatting, HTML-only
  parts). Every role left is one every platform expresses.
- **The host's data per node:** the role; seven texts (label,
  description, value, placeholder, keyboard shortcut, role description,
  state description), well-formed UTF-8 without NUL, copied; and flags
  (hidden, read only, required, multiselectable, busy, checkable,
  mixed, selectable, expandable, expanded, clickable). Nodes with data
  are bounded by `limits.accessNodes`; texts are allocated when set and
  freed when replaced, cleared or their node is destroyed.
- **What the library derives**, read when an update is built: the
  children (a virtual list's in item order); bounds, the border box in
  the node's own space, and a transform to its parent's, the node's
  place moved by the parent's scroll as painting moves it; disabled
  from the state, and hidden for a node exiting; checked and selected
  from the states on nodes that can have them; focusable, and the
  focus and blur actions, from the focus rules and player 0's focus;
  modal for a modal layer's root; scrolling values, clipping and the
  scroll actions for scroll containers; the value, limits and step,
  with increment, decrement and set value unless read only, for
  ranges; position in set and set size for items bound in a virtual
  list; click for the roles that take one or a node flagged
  clickable; expand or collapse for expandable nodes.
- **Updates from marks.** Every mark of another stage also marks the
  node for accessibility, as do the host's accessibility edits, a
  scroll container's offset (with its children) and a range's value. A
  build visits marked subtrees only, derives each marked node and
  compares it with the copy last sent: an equal node is not sent, so a
  color animating is nothing to assistive technology. A child its
  parent lists anew, after a list that left it out, is sent with its
  subtree, since adapters let a node go with the last list holding it.
- **Lazily, per root** (`muiAccess_Enable`, `muiAccess_Disable`): up to
  `limits.accessRoots` roots, one per window. Enabling the first
  allocates, for every node slot, the copy last sent and the update's
  buffers; the first update for a root sends every node; disabling the
  last frees them. Enabling an enabled root makes its next update whole
  again, for an adapter starting over.
- **Requests** (`muiPerformAccessAction`) the library performs where it
  owns what they change: focus and blur for player 0, scrolling a node
  into view, a container a page each way or to an offset, and a
  range's value a step or to a value, reported as input's are. A click
  is routed to the node as a navigation event, `mui_navigateActivate`,
  so a widget answers assistive technology as it answers Enter. Expand
  and collapse are posted to the host as
  `mui_notificationAccessAction`. A request a node does not take now
  is refused as empty; one from an event function is misuse, as other
  input is.

## Consequences

Hosts write accessibility once, for every platform, and most of it is
not written at all: layout, scrolling, focus, states, ranges and lists
are read where they already are. A context with no root enabled pays
its table of host data, reserved at creation (112 bytes an entry and
4 a node slot), and a bit per mark. With one enabled, a frame's cost is
deriving the nodes marked since the last update, and memory is a copy
of a node record and its place for each node slot, 248 bytes each. Relations, typed values such as levels and table positions, live
regions and text blocks naming their nodes come next; then the
consumer that adapters share (the tree, names, filtering and the
changes platforms announce), and the adapters themselves.
