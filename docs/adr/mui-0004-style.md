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
- **Oscillation is reported and held.** A node's last stylings since
  the host last restyled it are kept. When four in a row read two sizes
  in turn, exactly, and its conditions' outcome flipped each time, the
  context posts a `mui_notificationOscillation` record for the node in
  its notification queue (family record 0018), drained with
  `muiNextNotification`, and holds its conditions at their outcome: a
  layout that gives either of the two sizes does not style it again.
  A host edit of the node, of a class, a node type or the environment,
  or any other size releases it. The queue is a ring reserved at
  creation; records past its limit are counted into one
  `mui_notificationDropped` record in the place of the first lost one.
- **A condition's values may not set what it reads:** the sizes and
  limits of an axis it reads, the aspect ratio when it reads either
  axis, and the text direction when it reads direction. A write or a
  replacement condition that would is refused.
- **Transitions are shared specs.** A spec is a context object, timed
  (duration, delay, a CSS easing keyword or cubic Bezier) or a spring
  (frequency, damping ratio). A class's variant names a spec for some
  properties, and the spec a change takes is resolved through the same
  layers as values, from the state after the change, as CSS reads
  `transition` from the after-change style.
- **Transitions run against host time.** `muiLayoutInput.timeNs` is
  nanoseconds on a monotonic clock; a time before the last counts as
  none. Each run moves running transitions to now, styles (starting and
  retargeting transitions from where values are), and moves them once
  more so that one of no length ends in the same run. Numbers, and
  dimensions that are Scale+Offset on both sides, move; enumerators and
  changes to or from automatic apply at once, as do all changes under
  reduced motion, which also ends running ones. A timed transition
  follows its easing from the current value; a reversal is shortened by
  the share the old one covered, as CSS shortens it. A spring starts
  from the current value and speed, so a new target keeps its momentum,
  and rests on its target exactly. Values a property does not allow (a
  spring's overshoot below 0 for a length) are held at its bounds.
- **Running transitions are pooled records.** The context reserves them
  up to a limit; a change that finds none free applies at once. A
  direct write stops the transition of its property. A record whose
  node is gone is freed when transitions next move. While one runs
  below a root, `muiIsUpdatePending` holds.
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
narrow node made wider) can only be seen across runs: it flickers for
four runs before it is held. A host that writes a parent's size back
and forth between two values on every run makes the same pattern and is
reported the same way.
