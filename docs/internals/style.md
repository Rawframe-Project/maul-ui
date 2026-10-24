# Style

A UI's values come from many places at once: a widget kind's look, the classes a program gives one element, the states a pointer or a toggle puts it in, and values set on the element itself. CSS decides between them with a cascade and selector specificity, which a caller must reason about to predict a result. Roblox orders rules by an integer priority and leaves equal priorities undefined. Maul UI has no cascade, no specificity and no selectors. It uses a fixed order of layers, in which a later layer always wins.

## Contents

- [Properties](#properties)
- [Layers](#layers)
- [Conditions](#conditions)
- [Transitions](#transitions)
- [Visual values and colors](#visual-values-and-colors)
- [Inherited text style](#inherited-text-style)
- [Tokens and themes](#tokens-and-themes)
- [The style pass](#the-style-pass)

## Properties

Property ids come in groups of 64, one per values struct: layout from 0, visual from 64, text from 128 and interaction from 192. A public mask is one group's 64 bits (`MUI_PROPERTY_BIT` takes the id modulo 64), so each values call keeps a C caller's `|`-ed masks. The calls that span groups (resets, transitions) name a group. Inside the library a set of properties is a word per group. A single 64-bit mask would have run out at the text properties, and UIs built like CSS keep adding properties.

One table gives each property's place, kind and allowed values ([property.c](../../src/property.c)), so checking, applying and comparing a set are one loop over its bits. A refused write changes nothing.

## Layers

A node's values start from the defaults. Then come, each later layer winning:

1. every class's base values, in order;
2. the state variants: the states from weakest to strongest (checked, selected, focused, focus visible, hovered, pressed, disabled, exiting), and the classes in order within each state;
3. the conditions that hold, in class order and then in each class's order;
4. the node's direct writes.

A node's classes are its node type's, then its own. A class destroyed while listed is skipped. Because the order is global, a hovered value of the first class beats the base value of the last, and precedence can be read off the layer a value came from.

A class holds a base property set and one per variant. Each set is created when its first value is set and given back when its last is reset. The context reserves classes, node types and property sets at creation, each up to a named limit.

Direct writes win until reset. The node keeps which properties it writes directly, and resolution leaves those alone. `muiNode_SetLayoutValues` with every layout property writes the whole layout this way.

## Conditions

A condition is a conjunction of typed clauses with its own values, up to eight a class: half-open ranges over the node's border-box width, height and aspect and the text scale; sets of viewport classes and input modalities; and choices for reduced motion and text direction. A clause at its default reads nothing.

Conditions read the state before the pass. Size and direction come from the node's last layout. The viewport class, input modality, text scale and reduced motion are the environment the host sets on the context, and a change to it restyles every node. When a layout changes a size or direction that a node's conditions read, the node's style is requested for the next run and `muiIsUpdatePending` tells the host a run is owed. No stage feeds an earlier one within a run. In exchange a size condition shows its effect one run late, and a node never laid out reads a size of 0.

A condition's values may not set what it reads: the sizes and limits of an axis it reads, the aspect ratio when it reads either axis, and the direction when it reads direction. Such a write is refused.

A rule can still feed itself through layout: a narrow node made wider stops being narrow. Such a rule flickers between two sizes. The context keeps a node's last stylings since the host last restyled it ([restyle.c](../../src/restyle.c)). When four in a row read two sizes in turn, exactly, and the conditions' outcome flipped each time, it posts `mui_notificationOscillation` for the node and holds its conditions at their outcome. A host edit of the node, of a class, a node type or the environment, or any third size releases it. A host that writes a parent's size back and forth between two values every run makes the same pattern and is reported the same way.

## Transitions

A transition spec is a context object. It is timed (duration, delay, a CSS easing keyword or a cubic Bezier) or a spring (frequency and damping ratio). A class variant names a spec for some properties. The spec a change takes is resolved through the same layers as values, from the state after the change, as CSS reads `transition` from the after-change style.

Transitions run against host time: `muiLayoutInput.timeNs` is nanoseconds on a monotonic clock, and a time before the last counts as none. Each run moves running transitions to now, styles (starting and retargeting transitions from where values are), and moves them once more, so a transition of no length ends in the same run.

- Numbers, insets, dimensions and radii that are Scale+Offset on both sides, colors and shadows move, through up to eight channels.
- Enumerators, flags, image keys, gradients and changes to or from automatic apply at once. So does every change under reduced motion, which also ends running transitions, and a node's first styling, which has nothing to move from.
- A timed transition follows its easing from the current value. A reversal is shortened by the share the old one covered, as CSS shortens it.
- A spring starts from the current value and speed, so a new target keeps its momentum, and rests exactly on its target ([animation.c](../../src/animation.c)).
- Values a property does not allow, such as a spring's overshoot below 0 for a length, are held at its bounds.

Running transitions are pooled records up to a limit ([animation.c](../../src/animation.c)). A change that finds none free applies at once. A direct write stops the transition of its property. A record whose node is gone is freed when transitions next move. While one runs below a root, `muiIsUpdatePending` holds.

Easing curves and springs are [Maul Tween](https://github.com/Rawframe-Project/maul-tween)'s, which gives the same bits on every platform. Easing keywords and cubic Beziers are CSS Easing Functions Level 1's, solved by Newton's method inside a bracket to 1e-14 (`mtwnMakeCubicBezier`) and sampled by `mtwnEaseCubicBezier`, which links none of Maul Tween's other curves. A spring follows the closed-form solutions of the damped harmonic oscillator, made from its frequency and damping ratio (`mtwnMakeSpringFromFrequency`); one motion per frame (`mtwnSpringMotionAt`) moves every channel from its own start. Scrolling's exponentials and colour fonts' trigonometry stay the library's own ([motion_math.c](../../src/motion_math.c)), with arguments reduced as Cody and Waite's *Software Manual for the Elementary Functions* describes.

## Visual values and colors

Visual values are style properties of their own group (`maul-ui/visual.h`): background color and gradient, corner radii, border colors, shadows, an image with its slices, tint, mirroring and repeat, opacity, clipping and a local scale. Classes, variants, conditions, transitions and direct writes reach them unchanged. A changed visual value marks the node's paint, never its layout.

Corner radii are Scale+Offset of the border box's shorter side, held to half of it, so a large one makes a pill, as Roblox's `UICorner` does.

Colors are sRGB-encoded with straight alpha in the API (IEC 61966-2-1's transfer function). They move in premultiplied Oklab, Ottosson's perceptual space, as CSS Color 4 recommends where no legacy result is owed: a fade from clear keeps its hue, and a mix keeps the lightness the eye expects ([color.c](../../src/color.c)). The conversion uses the library's own power and cube root. A record keeps its target as stored and writes it on arrival, so a value moved through other channels ends exactly on it.

## Inherited text style

Text properties (color, font, size, weight, slant, line height, letter spacing, alignment, wrapping, decoration) are inherited, as CSS inherits them ([inherit.c](../../src/inherit.c)). A property no layer gives a node takes its parent's computed value, and a root takes the defaults.

- The size is Scale+Offset of the parent's computed size, as `em`.
- Line height and letter spacing are Scale+Offset of the node's own size and are inherited as written, as a unitless CSS `line-height` is, so one ratio serves every size below.

The style pass computes text values after a node's own, parent before child, and carries a change down only while children's values change. An animated text value does the same each run, so a transition on a parent reaches its children. The core measures and draws no text: a change marks a node with host content to be laid out again when it sizes text, and only painted again for color or alignment.

## Tokens and themes

A token is a typed context object (color, number, dimension, shadow, gradient) holding a literal or an alias to a token of its type, as the W3C design tokens format and Roblox's token sheets have them ([token.c](../../src/token.c)). An alias that would close a cycle is refused when set. A class variant names a token for a property in place of a value, in the same layer, and the token's type must be the property's. A value the property does not allow there, or a token that is gone, leaves that layer silent for the property, as CSS treats a variable invalid at computed-value time. Changing a token restyles every node, so a theme switch moves through the transitions classes name.

A theme overrides tokens with literals or aliases and is set on nodes ([theme.c](../../src/theme.c)). A node reads each token from the nearest theme above it, itself included, that overrides it, then the next out, up to eight, then the context, as CSS custom properties nest. An alias read in a subtree is read there, so an outer theme's semantic alias picks up an inner theme's primitive. Each theme keeps a table from token slots to overrides, so a lookup is one read. Each node keeps the nearest themed ancestor found when it was last styled, and a change of it restyles its children, so setting a theme or moving a subtree restyles only where themes change.

## The style pass

The style pass runs first in `muiComputeLayout` over the nodes whose style was requested: a change of type, classes or states, a reset, or insertion ([restyle.c](../../src/restyle.c)). Any change to a class or a node type requests every node. Keeping no index from classes to nodes costs a pass over the tree on a class edit, which a theme switch costs anyway.

The context keeps the properties any class has given a value or any node has reset. Every other property can hold only its default or a direct write, so resolution does not visit it. A node whose resolved values did not change marks nothing. One whose layout values changed marks its layout and its parent's, and one whose visual values changed marks its paint.
