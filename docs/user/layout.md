# Nodes and layout

## The tree

`muiCreateNode` makes a root. `muiNode_InsertChild` puts a root under a parent, before one of its children or last. `muiNode_Detach` makes a node a root again, its subtree with it, and `muiDestroyNode` destroys a node and its whole subtree. Every root is laid out and drawn on its own: a window's screen, or anything a program keeps apart from it. The tree is read through parents and siblings:

```c
// Counts a node's children.
static uint32_t CountChildren(const muiContext* context, muiNodeId parent)
{
    uint32_t count = 0;
    for (muiNodeId child = muiNode_GetFirstChild(context, parent); child.index1 != 0;
         child = muiNode_GetNextSibling(context, child))
    {
        count++;
    }
    return count;
}
```

A node is moved by detaching it and inserting it again:

```c
// Moves a node to the front of its parent's children, as a list sorted
// again does.
static muiResult MoveFirst(muiContext* context, muiNodeId node)
{
    muiNodeId parent = muiNode_GetParent(context, node);
    muiNodeId first = muiNode_GetFirstChild(context, parent);
    if (first.index1 == node.index1)
    {
        return mui_success;
    }
    muiResult result = muiNode_Detach(context, node);
    return result == mui_success ? muiNode_InsertChild(context, parent, node, first) : result;
}
```

A node's def carries its host key, which names the content the program draws in it: a text block, an image, anything else. The library passes it back when it measures and paints the node, and never reads it.

## Layout is flexbox

Every node lays its children out as a CSS flex container, with CSS's defaults: a row, items that do not grow and do shrink, automatic sizes, and children stretched across the line. Every box is a border box. A layout that is wrong in Maul UI and right in a browser is a bug worth reporting with the HTML that shows it.

The values are in `muiLayoutStyle` (`maul-ui/layout.h`), set on a node or through a class ([Styling](styling.md)):

- **Sizes.** Width, height, their minimums and maximums and the flex basis are automatic, or a `muiDimension`: a scale of the parent's content extent plus an offset, CSS's `calc(scale * 100% + offset)`. A scaled size against a parent whose extent is not yet known is automatic.
- **The container:** direction, wrapping, `justify-content`, `align-items`, `align-content` and the row and column gaps.
- **The item:** grow, shrink, basis, `align-self` and an aspect ratio, width over height.
- **Edges:** padding, border widths and margins, each by start, end, top and bottom. Margins may be automatic, which centers or pushes an item as in CSS.
- **Placement:** in the flow, or absolute with insets, which take the node out of its parent's flex layout. An absolute node's anchor point moves it back by a fraction of its own size, so `{0.5, 0.5}` centers it on its insets.
- **Direction:** inherit, left to right or right to left. Start and end follow it, so a right-to-left subtree mirrors without other changes.
- **The safe area:** the edges whose padding should be at least the surface's safe-area insets, which the frame passes in `muiLayoutInput`: a bar whose background reaches a phone's rounded corner while its content stays clear of it.

## Host content

A node whose `content` is `mui_contentHost` is sized by the program. The layout input names a measure function, called inside `muiNode_ComputeLayout` with the node, its host key and the constraint on each axis, and a baseline function for nodes aligned by baseline. The text component provides both for text ([Text](text.md)).

The library caches what a function answered and asks again only when the node or its content changed. When the content changes outside the library's view, such as a new string or a reloaded image, `muiNode_MarkContentChanged` has the node measured and painted again at the next frame, and its siblings move if its size changes.

A measure function that cannot measure now, as when its memory ran out, answers NaN for the width or the height; a baseline function answers +infinity. The node is laid out as empty for this frame, the answer is not kept, `muiNode_ComputeLayout` returns `mui_errorCapacity`, and the next layout asks again. The text component answers so when it runs out of memory.

## Reading the results

`muiNode_GetRect` gives a node's border box relative to its parent's, and `muiNode_GetContentRect` its content box, in logical units and unrounded. `muiNode_MapToRoot` carries a point of a node into its root's space, through scrolling and scales. The draw list snaps to device pixels by itself ([Drawing](drawing.md)), so a program reads exact values and draws sharp edges.

A frame that changes nothing costs nothing: a layout with no edit since the last visits no node.
