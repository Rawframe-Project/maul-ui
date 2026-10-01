# mui-0002. The node tree

Status: Accepted

## Context

Maul UI computes over a retained tree its host supplies (an engine's
entities, a program's widgets). Either the library owns a tree the host
edits, or it reaches the host's tree through callbacks. A static screen
must cost nothing per frame, the same tree must give the same output
everywhere, and the host must never write what the library computes.

## Decision

- **The library owns the tree.** A context reserves a slot for every
  node its limit allows when it is created. The host creates, inserts,
  detaches and destroys nodes through generation-checked ids (family
  record 0016) and keeps its own map to them; each node carries a
  64-bit host key the host chooses, which the library never reads. The
  host writes authored values through setters and reads computed values
  through getters.
- **A context is one block of parallel arrays.** The tree's links, the
  layout, style, text and paint records of each node are arrays indexed
  by slot, each starting on a 64-byte cache line placed from the
  block's actual address, as data-oriented engines lay out component
  arrays. A record whose size is a multiple of 64 then keeps each field
  in the same place within its lines whatever the parts before it
  hold; a field read every frame, such as a node's rectangle, never
  spans two lines, which halves load throughput on current x86 cores
  and was measured to slow drawing by up to 30% when the block's
  earlier parts changed size.
- **Children are sibling links.** Each node links to its parent, its
  first and last child and its siblings, so every edit is constant time
  and none allocates. A child is inserted before a named sibling, or
  last. A node with a parent must be detached before it moves; an
  insertion that would make a cycle, or that names a sibling of another
  parent, is refused as misuse.
- **Destroying a node destroys its subtree,** children first, in order.
  Freed slots are reused last in, first out, so the same edits give the
  same ids.
- **Dirty tracking is two flags per stage.** For style, layout and paint
  each node keeps a request (redo this node) and a subtree flag (this
  node or a descendant). A request sets subtree flags up the parent
  chain and stops at the first ancestor that has them. Inserting marks
  the child's style and the parent's layout and paint, and carries what
  the child's subtree still owes up to the new parent; detaching marks
  the old parent's layout and paint.
- **A pass clears what it reaches.** Each pass descends only into
  subtrees whose flag is set and clears both flags on every node it
  reaches, whether it worked there or not, so no flag outlives a frame
  and a static frame reaches no node.

## Consequences

The host keeps one map from its objects to node ids, and pays for
creation only within the limit it chose. Edits are cheap and many edits
to one branch cost one walk to the root. Access to a child by index is
a walk; the pipeline never needs it, and the host has its own order.
