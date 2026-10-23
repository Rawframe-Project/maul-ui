# Scrolling and lists

Scrolling is the library's because it touches every stage: layout measures what scrolls, painting moves it, hit testing and navigation look through it, and input moves it. Virtual lists build on scroll containers so that a list of a hundred thousand items costs what its viewport shows.

## Contents

- [Scroll containers](#scroll-containers)
- [Keys and the wheel](#keys-and-the-wheel)
- [Motion](#motion)
- [Touch, flings and overscroll](#touch-flings-and-overscroll)
- [Virtual lists](#virtual-lists)

## Scroll containers

A node whose layout property `scrollAxes` names an axis is a scroll container ([scroll.c](../../src/scroll.c)). It clips its children at its rounded padding box, and its flex automatic minimum is 0, as CSS's.

Layout measures its extent logically from the start: the furthest end of its children's margin boxes (border boxes for absolute children) plus its end padding, at least its padding box. Offsets are logical, clamped between 0 and the extent less the padding box, and kept there by later layouts. A node that stops scrolling drops its offset.

Painting draws the children unscrolled through a transform per scroll container, its offset composed with its ancestors' and rounded to device pixels ([Draw list](draw-list.md#clips-and-transforms)). A build whose only change is offsets rewrites the transform table and keeps the commands, so scrolling alone costs one entry per container. Hit testing, pointer records, directional navigation and layers add the offsets of scrolling ancestors.

`muiNode_ScrollIntoView` scrolls each scrolling ancestor, nearest first, as CSSOM View's `nearest` does, and focus moved by navigation scrolls into view the same way. `muiNode_GetScrollThumb` places a scrollbar thumb on a host's track.

## Keys and the wheel

- **Wheel.** `muiWheelInput` is routed from the node under its point. Unhandled, it scrolls the nearest scroll container from there up that can move that way, not past the given root or a layer's root, by the scroll rule's step a detent (`muiSetScrollRule`, 100 by default). The container keeps later turns while they come within the rule's latch time (500 ms), as Firefox's wheel transaction, so a list at its end does not hand a turn to the page behind it. Shift turns a vertical-only turn horizontal.
- **Arrows and directions.** With the focus in a scroll container along its axis, an arrow or navigation direction follows Android's `ScrollView`: the focus moves to the candidate inside when it lies within half a scrollport of the visible part, else the container steps a line (40 units). At its end the focus moves as anywhere else.
- **Pages.** Page keys and Space step the vertical scroll container holding the focus by 0.875 of a page, and Home and End go to its ends.

## Motion

Whole detents and key steps move toward a target over 150 ms with a cubic ease out, and a further step retargets from where it is ([scroll.c](../../src/scroll.c)). Fractional deltas, from smooth wheels and touchpads, apply at once. Reduced motion and a time of 0 jump. Steps advance in `muiComputeLayout` with its time and keep `muiIsUpdatePending` true. Setting an offset or scrolling into view stops them. Up to eight containers ease at once; a ninth jumps.

## Touch, flings and overscroll

For touch and pens, a scroll container is a drag node too where no nearer node takes drags. Its drag records, when dispatched, pan it opposite the pointer's offset from the drag's start.

A drag that is not cancelled ends in a fling at the velocity of a least squares line through the moves of its last 100 ms, from 50 to 8,000 units a second. The fling decays by the rule's `decelerationRate` a millisecond (0.998, iOS's) in closed form, until it is under 10 units a second or meets a limit. A press, a step or setting the offset stops it, and reduced motion skips it.

Overscroll is off by default, as on desktop browsers. With the rule's `overscroll` on:

- a pan past a limit keeps the offset at the limit and moves the children on by iOS's rubber band, `d (1 - 1 / (0.55 x / d + 1))` for `x` past the limit in a scrollport of `d`, drawn and hit but not counted as offset;
- released, it springs back on a critically damped spring of 14.14 radians a second, Flutter's iOS spring (stiffness 100 over mass 0.5), and a pan catching it continues from where it is;
- a fling meeting a limit springs on past it at the speed it met it, on the same spring, all in closed form;
- a step or setting the offset takes it back at once, as reduced motion does.

Without overscroll a fling stops at the limit.

## Virtual lists

A scroll container may be a list of many items, each fixed in extent or estimated until measured, with a gap and an overscan ([virtual.c](../../src/virtual.c)). Estimated extents live in a Fenwick tree per list, out of `limits.virtualItems`, so the offset of an item and the item at an offset each cost O(log n).

- After each layout the window of items that should exist is found from the scroll offset and reported by `mui_notificationWindowChanged` when it changes. The host realizes it, binding child nodes to indices (`muiNode_SetItem`).
- Bound items leave the flow, stretch across the list's content box, are measured into their extents and placed at their offsets. The list's extent covers every item, realized or not, so scrolling is not clamped to what exists.
- Items are inserted, removed and moved by index ([virtual_edit.c](../../src/virtual_edit.c)). Their extents go with them and bound nodes follow their items. A removed item's node stays out of the flow, placed nowhere, until the host destroys or rebinds it.
- The offset moves by whatever changed above the viewport's start, as does an item above it measured anew, so what is shown stays put, as CSS's scroll anchoring does.
- A list set anew keeps the item it showed first where it was once the host binds that item's node again, wherever the item now is, as ItemsRepeater's reset with keys does.

Accessibility reads a virtual list's children in item order and gives each bound item its position in the set ([Accessibility](accessibility.md)).
