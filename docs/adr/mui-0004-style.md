# mui-0004. Style

Status: Accepted

## Context

A UI's values come from many places at once: a widget kind's look, the
classes a program gives one element, the states a pointer or a toggle
puts it in, and values the program sets on the element itself. CSS
decides between them by a cascade and selector specificity, which
callers must reason about to predict a result; Roblox orders rules by
an integer priority and leaves equal priorities undefined. Maul UI's
requirements ask for no cascade, no specificity and no selectors, and
for a fixed order in which a later layer always wins.

## Decision

- **A style class is a context object** with an id (family record
  0016). It holds a base property set and one set per state variant,
  each created when its first value is set and given back when its
  last value is reset. The context reserves classes, node types and
  property sets at creation, each up to a named limit.
- **A property set is a mask over the property ids and a
  `muiLayoutStyle` of values.** Values are set from the typed fields of
  a `muiLayoutStyle` with a mask, checked per property as
  `muiNode_SetLayoutStyle` checks them, and a refused write changes
  nothing. One table gives each property's place, kind and allowed
  values, so checking, applying and comparing are one loop over the set
  bits.
- **Assignment is by node type and by the node's own classes.** A node
  type is a context object with an ordered list of classes. A node has
  one type and its own ordered list; its classes are the type's, then
  its own. A class destroyed while listed is skipped.
- **Layers are fixed and global.** A node's values start from the
  defaults; then every class's base values in order; then the state
  variants, the states from weakest to strongest (checked, selected,
  focused, hovered, pressed, disabled, exiting) and the classes in
  order within each; then the node's direct writes. A later layer wins
  whatever class it comes from, so a hovered value of the first class
  beats the base value of the last.
- **Conditions are a layer of their own,** between the state variants
  and the direct writes. A class holds up to eight, each a conjunction
  of typed clauses with its own values: half-open ranges over the
  node's border-box width, height and aspect and the text scale, sets
  of viewport classes and input modalities, and choices for reduced
  motion and text direction. A clause at its default reads nothing.
  Conditions apply in class order, then in each class's order.
- **Conditions read the state before the pass.** Size and direction
  come from the node's last layout; the viewport class, input modality,
  text scale and reduced motion are the environment the host sets on
  the context, and a change to it restyles every node. When a layout
  changes a size or direction a node's conditions read, the node's
  style is requested for the next run, and `muiIsUpdatePending` tells
  the host a run is owed.
- **A condition's values may not set what it reads:** the sizes and
  limits of an axis it reads, the aspect ratio when it reads either
  axis, and the text direction when it reads direction. A write or a
  replacement condition that would is refused.
- **Direct writes win until reset.** The node keeps which properties
  it writes directly, and their values are its resolved ones: a direct
  write takes effect at once, and resolution leaves those properties
  alone. `muiNode_SetLayoutStyle` writes every layout property this
  way.
- **The style pass runs first in `muiComputeLayout`,** over the nodes
  whose style was requested: a change of type, classes or states, a
  reset, or insertion. Any change to a class or a node type requests
  every node. A node whose resolved values did not change marks
  nothing; one whose values changed marks its layout and its parent's.

## Consequences

Precedence can be read off the layer a value comes from, with no
selector to evaluate and no tie to break. A state that changes only
values a node does not use, or a class no node lists, costs a style
pass and no layout. Restyling every node on a class edit costs a pass
over the tree, which a theme switch costs anyway, and keeps no index
from classes to nodes. Nodes that write every property directly skip
resolution. A size or direction condition shows its effect one run
after the layout that changes what it reads, and a node never seen
laid out reads a size of 0; in exchange no stage feeds an earlier one
within a run. A rule that feeds its own condition through layout (a
narrow node made wider) can only be seen across runs.
