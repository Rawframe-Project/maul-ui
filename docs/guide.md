# The Maul UI guide

This guide walks through Maul UI part by part: how a program keeps its
interface as a tree of nodes, styles and lays it out, shows text, takes
input and tells assistive technology what it shows. The
[API reference](api.md) lists every public function; the design
records in [adr/](adr/mui.md) give the reasons.

## 1. The model

A program keeps its interface in a context, as a tree of nodes. Each
node has a style: where it goes and how large it is (layout), how it
looks (visual), its text's font and color, and how it answers input.
What the library does not draw itself, a text or an image, a node names
by a host key, and the program measures and paints it when asked.

Each frame the program lays a root out in the space it has, then builds
a draw list: boxes, shadows, images and glyph runs, back to front,
which a renderer draws. Maul UI draws nothing itself and opens no
window; Maul Window and Maul UI's reference renderer on Maul RHI do,
or a program's own.

A screen is built once. Each node here takes its layout from a whole
style, every property set on the node itself, and its background:

```c
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

// Adds a node under parent, or a root for the null id, laid out and
// filled as asked; the null id when the context refuses.
static muiNodeId Add(muiContext* context, muiNodeId parent, const muiLayoutStyle* layout,
                     muiColor background)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = background;
    if (muiCreateNode(context, &def, &node) != mui_success ||
        muiNode_SetLayoutValues(context, node, layout, MUI_LAYOUT_PROPERTIES) != mui_success ||
        muiNode_SetVisualValues(context, node, &visual, MUI_PROPERTY_BIT(mui_propertyBackground)) !=
            mui_success)
    {
        return (muiNodeId){0, 0};
    }
    if (parent.index1 != 0 &&
        muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}) != mui_success)
    {
        return (muiNodeId){0, 0};
    }
    return node;
}
```

```c
// The whole window, a column 16 units in from its edges: a bar 48 high,
// and under it two panels side by side sharing the rest.
static muiNodeId BuildScreen(muiContext* context)
{
    const muiColor white = {1.0f, 1.0f, 1.0f, 1.0f};
    const muiColor blue = {0.2f, 0.4f, 0.8f, 1.0f};
    const muiColor grey = {0.9f, 0.9f, 0.9f, 1.0f};
    const muiColor none = {0.0f, 0.0f, 0.0f, 0.0f};

    muiLayoutStyle column = muiDefaultLayoutStyle();
    column.sizing.width = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    column.sizing.height = (muiDimension){1.0f, 0.0f, mui_dimensionValue};
    column.container.direction = mui_flexColumn;
    column.container.rowGap = 8.0f;
    column.padding = (muiEdges){16.0f, 16.0f, 16.0f, 16.0f};
    muiNodeId root = Add(context, (muiNodeId){0, 0}, &column, white);

    muiLayoutStyle bar = muiDefaultLayoutStyle();
    bar.sizing.height = (muiDimension){0.0f, 48.0f, mui_dimensionValue};
    Add(context, root, &bar, blue);

    muiLayoutStyle row = muiDefaultLayoutStyle();
    row.container.columnGap = 8.0f;
    row.item.grow = 1.0f;
    muiNodeId panels = Add(context, root, &row, none);

    muiLayoutStyle panel = muiDefaultLayoutStyle();
    panel.item.grow = 1.0f;
    Add(context, panels, &panel, grey);
    Add(context, panels, &panel, grey);
    return root;
}
```

A dimension is a scale of the parent's content box plus an offset:
`{1, 0}` is all of it, `{0, 48}` is 48 units. A root's parent is the
space the frame gives it. Flex places the children: the column puts the
bar on top and gives the row what is left (`grow`), and the row shares
its width between the two panels.

A frame lays the screen out and builds its list:

```c
// Lays the screen out in a window's size and shows what to draw.
static muiResult Frame(muiContext* context, muiNodeId root, float width, float height,
                       muiDrawList* listOut)
{
    const muiLayoutInput layout = {width, height, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    muiResult result = muiComputeLayout(context, root, &layout);
    if (result == mui_success)
    {
        result = muiBuildDrawList(context, root, &draw);
    }
    return result == mui_success ? muiGetDrawList(context, listOut) : result;
}
```

```c
int main(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    if (muiCreateContext(&def, &context) != mui_success)
    {
        return 1;
    }
    muiNodeId root = BuildScreen(context);
    muiDrawList list;
    bool drawn = root.index1 != 0 && Frame(context, root, 800.0f, 600.0f, &list) == mui_success;
    // The window, the bar and the two panels: four boxes, back to front.
    uint32_t boxes = 0;
    for (uint32_t i = 0; drawn && i < list.commandCount; i++)
    {
        if (list.commands[i].kind == mui_drawBox)
        {
            boxes++;
        }
    }
    muiRect last = list.commands[list.commandCount - 1].box.rect;
    muiDestroyContext(context);
    // The second panel: 380 wide, from 16 + 380 + 8 across, under the bar.
    return drawn && boxes == 4 && last.x == 404.0f && last.y == 72.0f && last.width == 380.0f &&
                   last.height == 512.0f
               ? 0
               : 1;
}
```

Layout and drawing are kept between frames. Laying out again revisits
only what changed since: an edit, a new size, content marked changed,
a running transition (`muiIsUpdatePending` says whether there is
any). A build reuses what it drew for subtrees that did not change. A
transparent node draws no box, so the row adds none.

## 2. Results, ids and defs

Every call that can fail returns a `muiResult`: `mui_success`, or an
error that names why. `mui_empty` says a queue had nothing to take.
`mui_errorInvalid` is a program's bug, an argument outside the
contract; `mui_errorStale` an id whose object is gone;
`mui_errorCapacity` a limit or a buffer too small; `mui_errorFormat`
damaged data, such as a font; `mui_errorPlatform` a refusal of the
platform's. `muiResultName` names any of them.

Ids name what a context owns: a slot and a generation. A destroyed
node's slot is used again, with a new generation, so an old id never
names its successor. Refusals of invalid input are counted, a number a
release build can watch for a program's bugs; stale ids are not:

```c
// What a context says of calls that went wrong.
static void Refusals(muiContext* context)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = {0, 0};
    if (muiCreateNode(context, &def, &node) != mui_success)
    {
        return;
    }
    (void)muiDestroyNode(context, node);
    // A later node may take the slot; the old id never names it.
    muiResult stale = muiNode_InsertChild(context, node, node, (muiNodeId){0, 0});
    // A null id where a node is needed is the program's bug: refused,
    // and counted.
    muiResult invalid = muiNode_InsertChild(context, (muiNodeId){0, 0}, node, (muiNodeId){0, 0});
    printf("%s, %s, %llu refused\n", muiResultName(stale), muiResultName(invalid),
           (unsigned long long)muiGetContextMisuse(context));
}
```

This prints `mui_errorStale, mui_errorInvalid, 1 refused`.

Objects are made from defs, which start from their defaults
(`muiDefaultContextDef`, `muiDefaultNodeDef` and so on); a def's
cookie tells a def made so from one left uninitialized. A context
takes its allocator and its limits there, and reserves the memory its
limits name when it is made: a frame allocates nothing, and a limit
reached refuses with `mui_errorCapacity` rather than growing.

```c
// The bytes the library holds, counted.
static size_t s_held;

static void* Alloc(size_t size, size_t alignment, void* user)
{
    (void)user;
    // The library asks for no more alignment than malloc gives.
    void* memory = alignment <= alignof(max_align_t) ? malloc(size) : NULL;
    s_held += memory != NULL ? size : 0;
    return memory;
}

static void Free(void* memory, size_t size, size_t alignment, void* user)
{
    (void)alignment;
    (void)user;
    s_held -= size;
    free(memory);
}
```

```c
// A context for a small panel: room for 64 nodes and 256 draw commands,
// taken when it is made, from the counted allocator.
static muiContext* SmallContext(void)
{
    muiContextDef def = muiDefaultContextDef();
    def.allocator = (muiAllocator){Alloc, Free, NULL};
    def.limits.nodes = 64;
    def.limits.drawCommands = 256;
    muiContext* context = NULL;
    return muiCreateContext(&def, &context) == mui_success ? context : NULL;
}
```

Its 65th node is refused; destroying it gives every byte back. The
library asks an allocator for no alignment above `max_align_t`. A
context is used from one thread at a time.

## 3. Nodes and the tree

`muiCreateNode` makes a root. `muiNode_InsertChild` puts a root under a
parent, before one of its children or last; `muiNode_Detach` makes a
node a root again, its subtree with it; `muiDestroyNode` destroys a
node and its whole subtree. Every root is laid out and drawn on its
own, a window's screen or anything a program keeps apart from it. The
tree is read through parents and siblings:

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

A node's def carries its host key, which names the content the program
draws in it: a text block (section 5), an image, anything else.
The library passes it back when it measures and paints the node, and
never reads it.

## 4. Style

A node's values resolve in layers, each later one winning: the
defaults; the base values of each of its classes, in order; the
variants of the states it is in (checked, selected, focused, focus
visible, hovered, pressed, disabled, exiting, the later stronger); the
variants of conditions that hold; and the values set on the node
itself, as section 1 did. A node's classes are its node type's, then
its own (`muiNode_SetClasses`). A mask says which fields of a style
struct a call sets; the rest keep coming from the layers below.

A class names the look of a kind of node once:

```c
// A button class: padded and blue, lighter while hovered, darker while
// pressed; and a node type of buttons, which lists it.
static muiResult MakeButtonType(muiContext* context, muiStyleId* classOut, muiNodeTypeId* typeOut)
{
    muiResult result = muiCreateStyle(context, classOut);
    muiStyleId button = *classOut;
    if (result != mui_success)
    {
        return result;
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding = (muiEdges){12.0f, 12.0f, 6.0f, 6.0f};
    const muiPropertyMask padding =
        MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
        MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom);
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    muiVisualStyle normal = muiDefaultVisualStyle();
    normal.background = (muiColor){0.2f, 0.4f, 0.8f, 1.0f};
    muiVisualStyle hovered = normal;
    hovered.background = (muiColor){0.3f, 0.5f, 0.9f, 1.0f};
    muiVisualStyle pressed = normal;
    pressed.background = (muiColor){0.1f, 0.3f, 0.6f, 1.0f};
    if ((result = muiStyle_SetLayoutValues(context, button, mui_variantBase, &layout, padding)) !=
            mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantBase, &normal,
                                           background)) != mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantHovered, &hovered,
                                           background)) != mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantPressed, &pressed,
                                           background)) != mui_success)
    {
        return result;
    }
    return muiCreateNodeType(context, &button, 1, typeOut);
}
```

A node of that type is blue; `muiNode_SetStates(context, node,
mui_stateHovered)` makes it lighter, and pressed as well, darker, as
pressed is the stronger state. Pointer input sets hover and press
itself, and focus sets the focus states (section 8); a program sets
the others. Nodes are styled again at the next `muiComputeLayout`.

A condition holds for a node in some environment or at some size: a
viewport class, an input modality, a text scale, reduced motion, the
node's own size or direction from its last layout. Its values are a
variant of the class, set as a state's are:

```c
// A class that stacks a row's children on small viewports, a phone's.
static muiResult MakeStacking(muiContext* context, muiStyleId* classOut)
{
    muiResult result = muiCreateStyle(context, classOut);
    muiCondition small = muiDefaultCondition();
    small.viewports = mui_viewportSmall;
    muiVariant variant = mui_variantBase;
    if (result == mui_success)
    {
        result = muiStyle_AddCondition(context, *classOut, &small, &variant);
    }
    muiLayoutStyle stacked = muiDefaultLayoutStyle();
    stacked.container.direction = mui_flexColumn;
    return result == mui_success
               ? muiStyle_SetLayoutValues(context, *classOut, variant, &stacked,
                                          MUI_PROPERTY_BIT(mui_propertyFlexDirection))
               : result;
}
```

`muiSetContextEnvironment` tells the context where its interface is
shown: on a small viewport the row stacks. A condition on the node's
own size reads its last layout, so a frame that changes the size lays
out once more; `muiIsUpdatePending` says when.

Tokens name values a theme can change: a class variant names a token
for a property instead of a value, and every node using it follows it.
A theme overrides tokens for the subtree it is set on, the nearest
theme above a node winning:

```c
// An accent color token the button class paints with, and a theme that
// makes it orange in the subtree it is set on.
static muiResult UseAccent(muiContext* context, muiStyleId button, muiNodeId warning)
{
    muiTokenValue value = {.type = mui_tokenColor, .color = {0.2f, 0.4f, 0.8f, 1.0f}};
    muiTokenId accent = {0, 0};
    muiThemeId alert = {0, 0};
    muiResult result = muiCreateToken(context, &value, &accent);
    if (result == mui_success)
    {
        result =
            muiStyle_SetToken(context, button, mui_variantBase, mui_propertyBackground, accent);
    }
    if (result == mui_success)
    {
        result = muiCreateTheme(context, &alert);
    }
    value.color = (muiColor){0.9f, 0.5f, 0.1f, 1.0f};
    if (result == mui_success)
    {
        result = muiTheme_SetTokenValue(context, alert, accent, &value);
    }
    return result == mui_success ? muiNode_SetTheme(context, warning, alert) : result;
}
```

The buttons are blue; under the node with the theme, orange. Changing a
token restyles every node, and transitions (section 10) move the
change.
