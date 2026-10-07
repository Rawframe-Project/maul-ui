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
- **Property ids come in groups of 64,** one per values struct: layout
  from 0, visual from 64, text from 128, interaction from 192. A
  public mask is one group's 64 bits (`MUI_PROPERTY_BIT` takes the id
  modulo 64), so each values call keeps a C caller's `|`-ed masks; the
  calls that span groups (resets, the direct properties, transitions)
  name a group. Inside, a set of properties is a word per group. A
  single 64-bit mask would have run out at the text properties, and
  CSS-like UIs keep adding properties (RmlUi allows 255). Cold styling
  costs about 5% more for the wider sets in the benchmark.
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
  more so that one of no length ends in the same run. Numbers, insets,
  dimensions and radii that are Scale+Offset on both sides, colors and
  shadows move, through up to eight channels; enumerators, flags, image
  keys, gradients and changes to or from automatic apply at once, as do all changes under
  reduced motion, which also ends running ones, and a node's first
  styling, which has nothing to move from (CSS starts no transition for
  an element without a before-change style). A timed transition
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
- **Visual values are style properties too** (`maul-ui/visual.h`): a
  background color and a gradient of up to four stops (linear at an
  angle, radial to the farthest corner, or conic around the centre from
  an angle, as CSS's three), corner radii as
  Scale+Offset of the border box's shorter side (held to half of it, so
  a large one makes a pill, as Roblox's `UICorner`), border colors per
  side (the widths stay layout's), an outer and an inner shadow, an
  image by host key with 9-slice insets, a tint and whether it mirrors
  under right to left, opacity,
  clipping, and a local scale about an origin (record mui-0005). Their
  ids are the visual group's, so classes,
  variants, conditions, transitions and direct writes reach them
  unchanged; `muiVisualStyle` holds them apart from `muiLayoutStyle`,
  which the solver reads. A changed visual value marks the node's
  paint, never its layout. Colors are sRGB-encoded with straight alpha
  and move in premultiplied Oklab, as CSS Color 4 recommends where no
  legacy result is owed: a fade from clear keeps its hue, and a mix
  keeps the lightness the eye expects. The conversion uses the library's
  own power and cube root, so it gives the same bits everywhere. A
  record keeps its target as stored and writes it on arrival, so a
  value moved through other channels ends exactly on it.
- **Text style is inherited** (`maul-ui/text_style.h`): color, a font
  key the text service gives out, size, weight from 1 to 1000, slant,
  line height, letter spacing, alignment and wrapping, in the text
  group. A property no layer or direct write gives a node takes its
  parent's computed value, and a root the defaults (black, font 0, 16
  units, the font's own line height, weight 400), as CSS inherits these
  properties. The size is Scale+Offset of the parent's computed size,
  as `em`; the line height and letter spacing are Scale+Offset of the
  node's own size and are inherited as written, as a unitless CSS
  `line-height` is, so one ratio serves every size below.
  `muiNode_GetTextStyle` gives the computed values. The style pass
  computes them after a node's own values, parent before child, and
  carries a change down only while children's values change; an
  animated text value does the same each frame, so a transition on a
  parent reaches its children. The core measures and draws no text: a
  change marks a node with host content to be laid out again when it
  sizes text, and painted again for color or alignment.
- **Themes are tokens** (`maul-ui/token.h`): typed context objects
  (color, number, dimension, shadow, gradient) holding a literal or an
  alias to a token of their type, as the W3C design tokens format and
  Roblox's token sheets have them; an alias that would close a cycle is
  refused when it is set. A class variant names a token for a property
  in place of a value, in the same layer; the token's type must be the
  property's. Resolution reads the token through its aliases; a value
  the property does not allow there, or a token that is gone, leaves
  that layer silent for the property, as CSS treats a variable invalid
  at computed-value time. Changing a token restyles every node, so a
  theme switch (semantic tokens re-pointed at other primitives) moves
  through the transitions classes name.
- **Themes are scoped token sets** (`maul-ui/theme.h`): a theme
  overrides tokens with literals or aliases and is set on nodes; a node
  reads each token from the nearest theme above it, itself included,
  that overrides it, then the next out, up to eight, then the context,
  as CSS custom properties nest and Roblox's `StyleLink` scopes a sheet.
  An alias read in a subtree is read there, so an outer theme's
  semantic alias picks up an inner theme's primitive. Each theme keeps a
  table from token slots to overrides, so a lookup is one read; each
  node keeps the nearest themed node found when it was last styled, and
  a change of it restyles its children, so setting a theme or moving a
  subtree restyles only where themes change. A cycle through one theme
  and the context is refused when set; one that only nested themes
  close gives no value, as an invalid token does.
- **Resolution passes by what no class names.** The context keeps the
  properties any class has given a value or any node has reset; every
  other property can hold only its default or a direct write, so
  resolution does not visit it.
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
from classes to nodes. Nodes that write directly every property a class
names skip resolution, as do visual values in a context whose classes
set none, and inheritance in a context where nothing sets a text
property. A text change at a root walks the subtree below it once, as
CSS inheritance does. A size or direction condition shows its effect one run
after the layout that changes what it reads, and a node never seen
laid out reads a size of 0; in exchange no stage feeds an earlier one
within a run. A rule that feeds its own condition through layout (a
narrow node made wider) can only be seen across runs: it flickers for
four runs before it is held. A host that writes a parent's size back
and forth between two values on every run makes the same pattern and is
reported the same way.
