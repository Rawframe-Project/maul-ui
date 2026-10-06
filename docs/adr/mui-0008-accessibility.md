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
  mixed, selectable, expandable, expanded, clickable); relations to
  other nodes (labelled by, described by, controls, details, flows to,
  and the single active descendant, error message and popup it is for),
  sent as links in order of kind; and typed values (level, position
  and set size, table rows and columns with indices and spans, live,
  has popup, orientation, sort, invalid, current). Nodes with data are
  bounded by `limits.accessNodes`; texts and links are allocated when
  set and freed when replaced, cleared or their node is destroyed. A
  node named that is destroyed later stays named, as adapters pass over
  ids they do not hold.
- **Host content reads as its text** through a function the host sets
  (`muiSetAccessTextFunction`), as layout measures through one: a node
  whose content is the host's and whose value the host did not set
  takes the text the function gives as its value, and the label role
  when the host gave it none. The text component gives one for text
  blocks (`muiAccessTextOf`), so text in the UI is named without the
  host repeating it, and a button's name comes from the label inside
  it by the consumer's rule for names. The function may not edit the
  context; its text must be well-formed UTF-8, and is left out
  otherwise. Its text is compared by a 64-bit fingerprint, kept per
  node slot, since it is not the library's to keep.
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
  ranges, and their axis as orientation unless the host gives one;
  position in set and set size for items bound in a virtual list,
  unless the host gives them; click for the roles that take one or a node flagged
  clickable; expand or collapse for expandable nodes.
- **Updates from marks.** Every mark of another stage also marks the
  node for accessibility, as do the host's accessibility edits, a
  scroll container's offset (with its children) and a range's value. A
  build visits marked subtrees only, derives each marked node and
  compares it with the copy last sent: an equal node is not sent, so a
  color animating is nothing to assistive technology. A child its
  parent lists anew, after a list that left it out, is sent with its
  subtree, since adapters let a node go with the last list holding it. A change to the layers under the root (one opening, closing,
  raised, changing kind or exiting) makes the build compare every node,
  since it changes which nodes a modal layer covers and so which can
  take focus; a fingerprint of the layers in order finds it.
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

- **The consumer** (`maul-ui/access_tree.h`, the component
  `MAUL_UI_ACCESS_TREE`, on by default) keeps the copy adapters read:
  a `muiAccessTree` applies updates whole or not at all (checked, then
  copied into memory it owns, then put in place), lets a node no node
  lists go with its subtree, refuses lists that would not leave a tree
  (a child listed twice, the root listed, a node under itself), tells
  the adapter what was added, updated (with the old record), removed
  and where the focus moved, and writes itself as text for tests. It
  answers what platforms see as AccessKit's consumer does: hidden
  subtrees and children clipped wholly out of view left out (but the
  first past each edge, to scroll to), generic nodes flattened unless
  labelled (the core makes every node generic until the host gives a
  role, so a labelled one must stay to be heard), the focus never left
  out for itself; names from the label, the labelling nodes, or for
  button-like roles the labels and images inside; bounds through every
  transform, the root's included. It is used on one thread, the one
  the platform calls on: family record 0017 allows no lock or wait on
  a platform's thread, and with UI Automation's COM threading on an STA
  thread every platform calls on the window's thread.

## Consequences

Hosts write accessibility once, for every platform, and most of it is
not written at all: layout, scrolling, focus, states, ranges and lists
are read where they already are. A context with no root enabled pays
its table of host data, reserved at creation (168 bytes an entry and
4 a node slot), and a bit per mark. With one enabled, a frame's cost is
deriving the nodes marked since the last update, and memory is a copy
of a node record and its place for each node slot, 308 bytes each.
The adapters come next.
