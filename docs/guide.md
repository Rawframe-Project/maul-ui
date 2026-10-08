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

## 5. Text

The core lays out and draws no text: it measures and paints a node's
host content through the functions the frame gives it. The library's
text component, built by default (`MAUL_UI_TEXT`), is a text service
that does both: fonts read by FreeType, text shaped by HarfBuzz, lines
broken and runs ordered by Unicode's rules. A program may bring
another text stack instead and hand its own functions to the frame.

```c
#include "maul-ui/font.h"
#include "maul-ui/glyph_atlas.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"
#include "maul-ui/text_style.h"
```

The service holds fonts, families and text blocks. A font is the bytes
of a TrueType, OpenType or collection file, copied or borrowed:

```c
// A text service whose default font is one the program loaded.
static muiTextService* MakeTextService(const void* fontData, size_t fontSize)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    if (muiCreateTextService(&def, &service) != mui_success)
    {
        return NULL;
    }
    muiFontDef font = muiDefaultFontDef();
    font.data = fontData;
    font.size = fontSize;
    muiFontId fontId = {0, 0};
    if (muiCreateFont(service, &font, &fontId) != mui_success ||
        muiSetDefaultFont(service, fontId) != mui_success)
    {
        muiDestroyTextService(service);
        return NULL;
    }
    return service;
}
```

A block is UTF-8 text the service lays out. A node shows it when its
host key is the block's key and its content is the host's; its text
style, the font, size, color, weight, alignment and wrapping, comes
through the same layers as every property and is inherited as CSS
inherits it:

```c
// A label: a node showing a block of text at 16 units, dark grey.
static muiNodeId AddLabel(muiContext* context, muiTextService* service, muiNodeId parent,
                          const char* text, muiTextBlockId* blockOut)
{
    muiNodeId node = {0, 0};
    if (muiCreateTextBlock(service, text, strlen(text), blockOut) != mui_success)
    {
        return node;
    }
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(*blockOut);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, 16.0f, mui_dimensionValue};
    style.color = (muiColor){0.2f, 0.2f, 0.2f, 1.0f};
    if (muiCreateNode(context, &def, &node) != mui_success ||
        muiNode_SetLayoutValues(context, node, &layout, MUI_PROPERTY_BIT(mui_propertyContent)) !=
            mui_success ||
        muiNode_SetTextValues(context, node, &style,
                              MUI_PROPERTY_BIT(mui_propertyFontSize) |
                                  MUI_PROPERTY_BIT(mui_propertyTextColor)) != mui_success ||
        muiNode_InsertChild(context, parent, node, (muiNodeId){0, 0}) != mui_success)
    {
        return (muiNodeId){0, 0};
    }
    return node;
}
```

The frame hands the service's functions to layout and drawing, with a
`muiTextHost` naming the service and the context:

```c
// Lays a root out and draws it, its text measured and painted by the
// service.
static muiResult FrameWithText(muiContext* context, muiTextService* service, muiNodeId root,
                               float width, float height)
{
    muiTextHost host = {service, context};
    const muiLayoutInput layout = {width, height,          muiMeasureText, &host,
                                   0,     muiTextBaseline, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
    muiResult result = muiComputeLayout(context, root, &layout);
    return result == mui_success ? muiBuildDrawList(context, root, &draw) : result;
}
```

A label in a column 120 wide takes one line for "Hello" and wraps a
longer text onto as many as it needs, at the break opportunities
Unicode gives, its height growing. Each line's glyphs reach the draw
list as glyph runs, in their font and size; section 7 draws them.

A block's text is the program's to change. The context does not watch
it: marking the node's content changed has it measured and painted
again, its siblings moving if its size changes.

```c
// Replaces a label's text: the block takes it, and the node is measured
// and painted again at the next frame.
static muiResult SetLabel(muiContext* context, muiTextService* service, muiNodeId label,
                          muiTextBlockId block, const char* text)
{
    muiResult result = muiTextBlock_SetText(service, block, text, strlen(text));
    return result == mui_success ? muiNode_MarkContentChanged(context, label) : result;
}
```

Spans style parts of a block's text over the node's style: color,
decoration, font, size, weight, slant and baseline shift, each by
bytes. A span's text is drawn as glyph runs of its own:

```c
// Makes bytes from start up to end of a label's text bold.
static muiResult Embolden(muiContext* context, muiTextService* service, muiNodeId label,
                          muiTextBlockId block, uint32_t start, uint32_t end)
{
    muiTextSpan bold = {start, end - start, MUI_PROPERTY_BIT(mui_propertyFontWeight),
                        muiDefaultTextStyle()};
    bold.style.weight = 700.0f;
    muiResult result = muiTextBlock_SetSpans(service, block, &bold, 1);
    return result == mui_success ? muiNode_MarkContentChanged(context, label) : result;
}
```

`muiSetFallbackFonts` names fonts tried for characters the style's
font lacks, an emoji font or one for another script; a font family
(`muiCreateFontFamily`) picks a face by weight, width and slant as CSS
does, a variable font's axes set from them.

## 6. Editing text

A block opted into editing keeps a selection, an undo history and its
field's rules: one line or many, read only, a password shown as
bullets, a filter taking integers or decimals only, a most
characters, an input purpose for on-screen keyboards. It takes typing, pastes, deletions,
undo and redo, and its text scrolls in the node to keep the caret in
view.

```c
// Makes a label's block an editable field: one line, at most 64
// characters, undo keeping the last 100 edits.
static muiResult MakeField(muiTextService* service, muiTextBlockId block)
{
    muiTextEditDef def = muiDefaultTextEditDef();
    def.maxLength = 64;
    def.undoLimit = 100;
    return muiTextBlock_SetEditing(service, block, &def);
}
```

`muiTextEditEvent` maps a platform's events onto the field: keys under
the PC's or the Mac's shortcuts, typed text, and the pointer's presses
and drags placing the selection. The clipboard and focus stay the
program's: copying and cutting write through the input's function, and
a paste is asked for, the program answering it once its clipboard is
read.

```c
// What the program's clipboard holds, as the editor copies and cuts.
static char s_clipboard[256];

static void WriteClipboard(void* user, const char* text, size_t length)
{
    (void)user;
    size_t kept = length < sizeof s_clipboard - 1 ? length : sizeof s_clipboard - 1;
    memcpy(s_clipboard, text, kept);
    s_clipboard[kept] = '\0';
}

// Hands an event to the focused field: keys, typed text and the
// pointer; a paste it asks for is answered from the clipboard. Whether
// the field took the event.
static bool FieldEvent(muiContext* context, muiTextService* service, muiNodeId field,
                       muiTextBlockId block, const muiEvent* event)
{
    muiTextHost host = {service, context};
    const muiTextEditInput input = {mui_keymapPc, WriteClipboard, NULL};
    muiTextEditOutcome outcome = {false, false, false};
    if (muiTextEditEvent(&host, field, event, &input, &outcome) != mui_success)
    {
        return false;
    }
    if (outcome.paste)
    {
        bool pasted = false;
        if (muiTextBlock_Paste(service, block, s_clipboard, strlen(s_clipboard), &pasted) ==
            mui_success)
        {
            outcome.changed = outcome.changed || pasted;
        }
    }
    if (outcome.changed)
    {
        (void)muiNode_MarkContentChanged(context, field);
    }
    return outcome.handled;
}
```

A field fed "hello", then Backspace, holds "hell"; Control and A, then
C, copies it; two pastes give "hellhell", and Control and Z takes the
second back. Shift with a key extends the selection; Control moves and
erases by word.

An input method's composition is shown in the field with
`muiTextBlock_Compose` as the platform sends it, and committed as
typing; undo waits until it ends. Maul Window's glue does this for a
program (section 8).

The library paints the text, not the caret or the selection, whose
look is the program's. The editing primitives give their places in
the node's content box as drawn: `muiTextGetCaret` the caret, and
`muiTextGetRangeRects` a range's rectangles, one for each stretch of
it side by side on a line, as mixed directions split it.

```c
// Where to draw the caret, in the field's content box as laid out.
static bool CaretOf(muiContext* context, muiTextService* service, muiNodeId field,
                    muiTextBlockId block, muiTextCaret* caretOut)
{
    muiTextHost host = {service, context};
    muiTextSelection selection;
    float width = muiNode_GetContentRect(context, field).width;
    return muiTextBlock_GetSelection(service, block, &selection) == mui_success &&
           muiTextGetCaret(&host, field, width, selection.caret, caretOut) == mui_success;
}
```

`muiTextHitTest` finds the position under a point, and `muiTextMove`
moves one by cluster, word or line, as the keys do.

## 7. Painting

A draw list is what a renderer draws for a root: fixed-size commands in
paint order, boxes with their rounded corners, borders and gradients,
shadows, images and glyph runs. Coordinates are logical units, colors
linear light with premultiplied alpha, and the same tree gives the same
bytes. Each command names a clip and a transform by index; index 0 of
the clip table is none, and entry 0 of the transform table the
identity, so a renderer can batch across them:

```c
// Where a box command lands on the surface, in device pixels: its rect
// through its transform, at the list's scale. A renderer's walk does
// this for each command, in order, in the command's clip.
static muiRect DeviceRect(const muiDrawList* list, uint32_t index)
{
    const muiDrawCommand* command = &list->commands[index];
    const muiDrawTransform* to = &list->transforms[command->transform];
    const muiRect r = command->box.rect;
    float scale = list->header.scale;
    // Scale and translation only, as layout makes them; a renderer takes
    // the whole affine transform.
    return (muiRect){(to->a * r.x + to->e) * scale, (to->d * r.y + to->f) * scale,
                     to->a * r.width * scale, to->d * r.height * scale};
}
```

A node with host content is painted by the frame's paint function,
which adds glyph runs and rectangles through a sink, relative to the
node's content box, in the node's clip and opacity. This is how a
program draws what the library does not, a chart, a gauge:

```c
// A gauge the program draws itself: a node whose host key is a fill
// level in thousandths, painted as a bar of that much of its width.
static void PaintGauge(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                       muiDrawSink* sink)
{
    (void)user;
    (void)nodeId;
    const muiColor green = {0.2f, 0.7f, 0.3f, 1.0f};
    float filled = width * (float)hostKey / 1000.0f;
    (void)muiDrawSink_AddRect(sink, (muiRect){0.0f, 0.0f, filled, height}, green);
}
```

A node 200 units wide and a quarter full is painted as one box, 50
units wide, which lands 100 device pixels wide at a scale of 2. An
image is a visual property, a host key naming the image (with nine-slice
insets, tint and repeat), which the renderer looks up.

Glyph runs name a font key, a size and glyph ids; a renderer draws
their images from an atlas, which renders them once (coverage, distance
fields or color, as `muiRenderGlyph` and its siblings do) and packs
them into pages it keeps:

```c
// Makes sure every glyph of a list is in the atlas, then takes the
// rectangles of its pages that changed, which a renderer uploads. How
// many glyphs are packed.
static uint32_t PackGlyphs(muiGlyphAtlas* atlas, const muiDrawList* list, muiAtlasUpdate* updates,
                           uint32_t capacity, uint32_t* updateCount)
{
    uint32_t packed = 0;
    float scale = list->header.scale;
    muiGlyphAtlas_NextFrame(atlas);
    for (uint32_t i = 0; i < list->commandCount; i++)
    {
        const muiDrawGlyphRun* run = &list->commands[i].glyphRun;
        if (list->commands[i].kind != mui_drawGlyphRun)
        {
            continue;
        }
        for (uint32_t g = 0; g < run->glyphCount; g++)
        {
            const muiGlyph* glyph = &list->glyphs[run->firstGlyph + g];
            muiAtlasGlyph image;
            if (muiGlyphAtlas_Get(atlas, run->font, glyph->id, run->size * scale,
                                  (run->originX + glyph->x) * scale,
                                  (run->originY + glyph->y) * scale, &image) == mui_success)
            {
                packed++;
            }
        }
    }
    *updateCount = 0;
    (void)muiGlyphAtlas_TakeUpdates(atlas, updates, capacity, updateCount);
    return packed;
}
```

The first frame packs every glyph and gives the rectangles to upload;
the next, its glyphs packed already, gives none. A plot of glyphs no
frame has used lately is emptied when room runs out.

The reference renderer, the optional `maul-ui-rhi` (`MAUL_UI_RHI`),
does all this with Maul RHI: each command an instance of one pipeline,
its glyphs from atlases it keeps, images from a function the program
gives it. Each frame, `muiRhiRenderer_AddPasses` adds its upload and
draw passes into the program's frame, and `muiRhiRenderer_Record`
records them once the frame is compiled. The samples (`samples/`) run
it in a window with Maul Window.
