# Node tree

The library owns the tree and the host edits it through ids. The other choice, reaching the host's tree through callbacks, would make a static screen cost a walk of the host's structures every frame, and would let the host's order of iteration leak into the results. The host keeps one map from its objects to node ids. Each node carries a 64-bit host key that the library never reads.

## One block

A context is one allocation, taken at creation from the caller's allocator and sized by its limits ([context.c](../../src/context.c)). The tree's links and each node's layout, style, text-style and paint records are parallel arrays indexed by slot.

Each array starts on a 64-byte cache line placed from the block's actual address. A record whose size is a multiple of 64 then keeps each field at the same place within its lines whatever the parts before it hold, and a field read every frame, such as a node's rectangle, never spans two lines. Without this, a change in the size of an earlier part moved later records across line boundaries and slowed drawing by up to 30%.

## Ids

An id is a 1-based slot and a generation ([pool.c](../../src/pool.c)). A destroyed node's slot is reused last in, first out, with its generation raised, so the same edits give the same ids and an old id never names its successor. Every call checks the generation and refuses a stale id with `mui_errorStale`, which is not counted as misuse. Styles, node types, transitions, tokens and themes are context objects with ids of the same shape.

## Links

Each node links to its parent, its first and last child and its siblings ([tree.c](../../src/tree.c)). Every edit is constant time and none allocates. A child is inserted before a named sibling, or last. A node with a parent must be detached before it moves. An insertion that would make a cycle, or that names a sibling under another parent, is refused as misuse.

Destroying a node destroys its subtree, children first and in order. Access to a child by index is a walk. The pipeline never needs one, and the host has its own order.

## Dirty tracking

For each stage (style, layout, paint and accessibility) a node keeps two flags: a request to redo this node, and a subtree flag saying that this node or a descendant has one. A request sets subtree flags up the parent chain and stops at the first ancestor that already has them, so many edits to one branch cost one walk to the root.

- Inserting marks the child's style and its new parent's layout and paint, and carries what the child's subtree still owes up to the new parent.
- Detaching marks the old parent's layout and paint.
- Each stage's work marks the next one's. A changed style value marks layout or only paint, depending on its group ([Style](style.md)). A changed rectangle marks paint.

A pass descends only into subtrees whose flag is set. It clears both flags on every node it reaches, whether it worked there or not, so no flag outlives a frame and a static frame reaches no node. Every edit that marks a stage also marks accessibility, so an update visits the same subtrees.

## Re-entry

The host's measure, baseline, paint and accessibility text functions run inside the context's own calls. The tree is half-updated at those points, so the context refuses edits made from them with `mui_errorBusy` ([host_call.h](../../src/host_call.h)). The event function runs between routing steps and may edit the tree; routing writes its route down before the first call, so an edit changes neither who hears the event nor in what order ([Interaction](interaction.md#routing)).
