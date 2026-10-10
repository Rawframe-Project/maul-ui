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
    const muiStyleDef styleDef = muiDefaultStyleDef();
    muiResult result = muiCreateStyle(context, &styleDef, classOut);
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
    muiNodeTypeDef typeDef = muiDefaultNodeTypeDef();
    typeDef.classes = &button;
    typeDef.classCount = 1;
    return muiCreateNodeType(context, &typeDef, typeOut);
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
    const muiStyleDef styleDef = muiDefaultStyleDef();
    muiResult result = muiCreateStyle(context, &styleDef, classOut);
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
    muiTokenDef tokenDef = muiDefaultTokenDef();
    tokenDef.value = value;
    muiResult result = muiCreateToken(context, &tokenDef, &accent);
    if (result == mui_success)
    {
        result =
            muiStyle_SetToken(context, button, mui_variantBase, mui_propertyBackground, accent);
    }
    if (result == mui_success)
    {
        const muiThemeDef themeDef = muiDefaultThemeDef();
        result = muiCreateTheme(context, &themeDef, &alert);
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
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = text;
    blockDef.length = strlen(text);
    if (muiCreateTextBlock(service, &blockDef, blockOut) != mui_success)
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
    muiTextEditOutcome outcome = {false, false, false, false};
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
    // A moved caret changes only what assistive technology reads.
    if (outcome.changed)
    {
        (void)muiNode_MarkContentChanged(context, field);
    }
    else if (outcome.selected)
    {
        (void)muiNode_MarkAccessChanged(context, field);
    }
    return outcome.handled;
}
```

A moved caret is told to the accessibility tree, which reads the
selection through the text function (section 11). A field fed
"hello", then Backspace, holds "hell"; Control and A, then
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

## 8. Input

The program hands the context its platform's input; the context finds
the nodes it concerns and routes it to one function of the program's,
at each node from the root down to the target (tunnel), then back up
(bubble). Whatever no node handles is the program's again: a game
behind its interface takes the clicks and keys the interface leaves.

```c
// What the program keeps: the button it watches and its clicks.
typedef struct App
{
    muiNodeId ok;
    int clicks;
} App;

// The function routed events reach, at each node from the root down to
// the target, then back up; true stops them. The OK button clicked, or
// activated by a gamepad's confirm button or by assistive technology, is
// the program's.
static bool OnEvent(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    App* app = user;
    bool clicked =
        event->kind == mui_eventPointer && event->pointer->kind == mui_pointerRecordClick;
    bool activated =
        event->kind == mui_eventNavigation && event->navigation == mui_navigateActivate;
    if (phase == mui_phaseBubble && (clicked || activated) && nodeId.index1 == app->ok.index1 &&
        nodeId.generation == app->ok.generation)
    {
        app->clicks++;
        return true;
    }
    return false;
}
```

`muiSetEventFunction(context, OnEvent, &app)` sets it. A button takes
its activation too: a gamepad's confirm button (`muiNavigationInput`)
and assistive technology's press (section 11) arrive as it, not as a
click, so a button that heeds only clicks is one they cannot press.
Pointer events
go in after the frame's layout, which hit testing reads; each makes
records, which the program takes and routes:

```c
// Takes a platform's pointer event after the frame's layout, then
// routes the records it made: presses, releases, clicks, drags. Whether
// the interface used it, a record handled or a node under the pointer
// that does not let input through; the rest is the game's, behind it.
static bool Pointer(muiContext* context, muiNodeId root, const muiPointerEvent* event)
{
    if (muiPointerInput(context, root, event) != mui_success)
    {
        return false;
    }
    bool handled = false;
    muiPointerRecord record;
    while (muiNextPointerRecord(context, &record) == mui_success)
    {
        bool taken = false;
        (void)muiDispatchPointerRecord(context, &record, &taken);
        handled = handled || taken;
    }
    muiHit hit;
    return handled ||
           (muiHitTest(context, root, event->x, event->y, &hit) == mui_success && !hit.passThrough);
}
```

Moving over a node puts it and its ancestors in the hovered state, and
a press in the pressed state while the button is held, as CSS's
`:hover` and `:active`; a press and release on the same node make a
click, two close in time a double click (`muiSetClickRule`). A node
can capture a pointer (`muiPointer_SetCapture`) to keep its moves, and
a press that moves past the drag threshold starts a drag, whose records
offer data to the nodes it passes over. A node whose interaction style
lets input pass through, a HUD over a game world, is hit and hovered
but leaves the press to what lies behind.

The focus is the node a player's keys and gamepad go to, one per
player for local multiplayer. A press on a node that takes the focus
focuses it; code moves it with `muiFocus_Set`, and keys do:

```c
// Lets a node take the focus from keys and pointers alike.
static muiResult MakeFocusable(muiContext* context, muiNodeId node)
{
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    return muiNode_SetInteractionValues(context, node, &interaction,
                                        MUI_PROPERTY_BIT(mui_propertyFocusMode));
}
```

```c
// Hands a key to the focus under the root. Unhandled, Tab moves the
// focus, arrows move it toward their side or scroll, and the rest is
// the game's.
static bool Key(muiContext* context, muiNodeId root, muiKeyCode code, muiModifiers modifiers,
                bool down)
{
    const muiKeyEvent event = {
        .key = MUI_KEY_NAMED | code, .code = code, .modifiers = modifiers, .down = down};
    bool handled = false;
    return muiKeyInput(context, root, &event, &handled) == mui_success && handled;
}
```

Unhandled, Tab and Shift with Tab move the focus in tree order, or in
the tab order the interaction style gives; arrows move it toward the
nearest node on their side, as a gamepad's stick does
(`muiNavigationInput`), or scroll the container holding it. The
focused node is in the focused state, and in focus visible when keys
moved the focus there, so that a style can ring it then and not after
a click. Typed text goes to the focus with `muiTextInput`, and a text
field there takes it (section 6).

Maul Window's glue (`maul-ui-window`, `MAUL_UI_WINDOW`) does this
translation for a program: its events into pointer, key, text and
wheel input, input method compositions into the focused field, and the
clipboard.

## 9. Scrolling, lists and popups

A node whose layout scrolls on an axis (`scrollAxes`) is a scroll
container: its children may reach past its box, and an offset moves
them. `muiNode_SetScroll` sets the offset, `muiNode_ScrollIntoView`
brings a node into its containers' view, and unhandled wheel, keys and
navigation scroll the container holding the focus or the pointer,
eased as the scroll rule says. `muiNode_GetScrollThumb` gives a
scrollbar's thumb along its track. Painting moves the children through
a transform, so moving them redraws nothing.

A list of many items keeps only those near its viewport as nodes. The
library keeps every item's extent, fixed or estimated until measured,
and after each layout works out the window of items that should exist;
the program makes them:

```c
// A list of 10,000 rows 24 units high that scrolls, of which only the
// rows near its viewport exist as nodes.
static muiResult MakeList(muiContext* context, muiNodeId list)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.scrollAxes = mui_scrollVertical;
    muiVirtualList items = muiDefaultVirtualList();
    items.count = 10000;
    items.extent = 24.0f;
    items.fixed = true;
    muiResult result =
        muiNode_SetLayoutValues(context, list, &layout, MUI_PROPERTY_BIT(mui_propertyScrollAxes));
    return result == mui_success ? muiNode_SetVirtualList(context, list, &items) : result;
}
```

```c
// Makes the rows the list's window asks for: a node bound to each index
// from first up to end. The old rows go and the window's are made anew;
// a program reuses them instead, binding a row that left to an index
// that came and resetting what it showed.
static muiResult Realize(muiContext* context, muiNodeId list)
{
    uint32_t first = 0;
    uint32_t end = 0;
    muiResult result = muiNode_GetVirtualWindow(context, list, &first, &end);
    for (muiNodeId row = muiNode_GetFirstChild(context, list);
         result == mui_success && row.index1 != 0; row = muiNode_GetFirstChild(context, list))
    {
        result = muiDestroyNode(context, row);
    }
    muiNodeDef def = muiDefaultNodeDef();
    for (uint32_t index = first; result == mui_success && index < end; index++)
    {
        muiNodeId row = {0, 0};
        result = muiCreateNode(context, &def, &row);
        if (result == mui_success)
        {
            result = muiNode_InsertChild(context, list, row, (muiNodeId){0, 0});
        }
        if (result == mui_success)
        {
            result = muiNode_SetItem(context, row, index);
        }
    }
    return result;
}
```

```c
// A frame with a list: layout says when the list's window changed, the
// rows are made, and layout places them.
static muiResult ListFrame(muiContext* context, muiNodeId root, muiNodeId list,
                           const muiLayoutInput* input)
{
    muiResult result = muiComputeLayout(context, root, input);
    bool changed = false;
    muiNotification notification;
    while (result == mui_success && muiNextNotification(context, &notification) == mui_success)
    {
        changed = changed || (notification.kind == mui_notificationWindowChanged &&
                              notification.node.index1 == list.index1);
    }
    if (result == mui_success && changed)
    {
        result = Realize(context, list);
        result = result == mui_success ? muiComputeLayout(context, root, input) : result;
    }
    return result;
}
```

A list 300 units high holds a few dozen rows of its 10,000, those in
its viewport and the overscan on each side; scrolled to row 5,000, the
rows around it. Items inserted, removed and moved
(`muiNode_InsertVirtualItems` and its siblings) keep the rows bound to
the items they show, and the scroll position steady.

A popup is a node placed beside an anchor after each layout: below,
above or to a side, flipped when it does not fit and kept inside the
root. Its layer says how it paints; an overlay paints above everything
and outside its ancestors' clips:

```c
// Opens a menu below a button: absolutely placed, so it takes no room
// in the tree, painted in the overlay layer above everything, and put
// beside its anchor after each layout. A press outside it or Escape
// dismisses it.
static muiResult OpenMenu(muiContext* context, muiNodeId menu, muiNodeId button)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.placement.position = mui_positionAbsolute;
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.layer = mui_layerOverlay;
    muiPopup popup = muiDefaultPopup();
    popup.anchor = button;
    popup.side = mui_popupBelow;
    popup.gap = 4.0f;
    muiResult result =
        muiNode_SetLayoutValues(context, menu, &layout, MUI_PROPERTY_BIT(mui_propertyPosition));
    if (result == mui_success)
    {
        result = muiNode_SetInteractionValues(context, menu, &interaction,
                                              MUI_PROPERTY_BIT(mui_propertyLayer));
    }
    return result == mui_success ? muiNode_SetPopup(context, menu, &popup) : result;
}
```

A menu opened from a button 40 units high sits 4 below it. A press
outside it, Escape, or the focus moved elsewhere dismisses it, which
the context reports with `mui_notificationPopupDismissed`; the program
closes it, with whatever exit it likes (section 10). A popup anchored
inside another nests under it, and a press inside the inner one keeps
both open.

## 10. Transitions and exits

A transition says how a property moves to a new value: over a
duration along an easing curve, as CSS names them, or as a spring that
keeps its speed when the target changes. A class's variants name
transitions for properties as they name values, and the spec for a
change resolves through the same layers, from the state after the
change. Numbers, dimensions, radii, colors (in Oklab) and shadows
move; what cannot, an enumerator, an image key, a direct write, and
everything under reduced motion, changes at once. Transitions run
against the time each frame hands to layout (`timeNs`), so a program
that renders when something changes keeps drawing while
`muiIsUpdatePending` says one runs.

```c
// A class whose nodes' opacity moves over 150 ms, easing out, and which
// fades them to nothing while they leave.
static muiResult MakeFading(muiContext* context, muiStyleId* classOut)
{
    muiTransitionDef def = muiDefaultTransitionDef();
    def.kind = mui_transitionTimed;
    def.durationNs = 150000000;
    def.easing = mui_easingEaseOut;
    muiTransitionId fade = {0, 0};
    muiVisualStyle gone = muiDefaultVisualStyle();
    gone.opacity = 0.0f;
    const muiPropertyMask opacity = MUI_PROPERTY_BIT(mui_propertyOpacity);
    const muiStyleDef styleDef = muiDefaultStyleDef();
    muiResult result = muiCreateStyle(context, &styleDef, classOut);
    if (result == mui_success)
    {
        result = muiCreateTransition(context, &def, &fade);
    }
    if (result == mui_success)
    {
        result = muiStyle_SetTransition(context, *classOut, mui_variantBase, fade, mui_groupVisual,
                                        opacity);
    }
    return result == mui_success
               ? muiStyle_SetVisualValues(context, *classOut, mui_variantExiting, &gone, opacity)
               : result;
}
```

A node that leaves plays its way out before it goes. `muiNode_BeginExit`
puts it and its subtree in the exiting state, so its classes' exiting
variants and their transitions apply; it leaves hit testing, focus and
navigation at once. When nothing moves in it any more, the context
reports it, once:

```c
// Destroys the nodes whose exits finished, as a frame's notifications
// say. A node leaves with muiNode_BeginExit; the library never destroys
// it.
static void DestroyExited(muiContext* context)
{
    muiNotification notification;
    while (muiNextNotification(context, &notification) == mui_success)
    {
        if (notification.kind == mui_notificationExitFinished)
        {
            (void)muiDestroyNode(context, notification.node);
        }
    }
}
```

A toast of the fading class begins its exit fully opaque, is half way
out 75 ms later and still in the tree, and at 150 ms is transparent,
its exit reported and the node destroyed. `muiNode_CancelExit` brings a
leaving node back: the exiting state goes, it takes input again, and
its transitions move it back.

## 11. Accessibility

Every node of a root is a node of its accessibility tree, with a role,
texts, flags and actions. Most come from what the library holds:
rectangles, scrolling, focus, states, value ranges, virtual lists,
host content's text through the service (`muiAccessTextOf`, set with
`muiSetAccessTextFunction`), with an editing field's selection, its
lines and words, and where its characters are near what is shown; a
screen reader's requests to move that selection or change the text go
to the text component (`muiTextPerformAccessAction`), which applies
them as a paste. The rest the program says:

```c
// Tells assistive technology what a node is: a button, and its name.
static muiResult Describe(muiContext* context, muiNodeId button, const char* name)
{
    muiResult result = muiNode_SetAccessRole(context, button, mui_roleButton);
    return result == mui_success
               ? muiNode_SetAccessText(context, button, mui_accessLabel, name, strlen(name))
               : result;
}
```

Nothing is built until the program enables a root, as when Maul Window
reports that an assistive technology asked for one
(`muiAccess_Enable`). Then each frame gives an update in the shape
AccessKit uses: the nodes that changed, each whole, the focus with
every update, and the root with the first. The program hands it to
the platform's adapter, which keeps its own copy and answers the
screen reader from it; the adapter queues what assistive technology
asks, a press, a focus, a value, a scroll, and the program applies
it on its own thread:

```c
// A frame's accessibility, once the root is enabled: the update of what
// changed since the last, for the platform's adapter, then the requests
// it queued from assistive technology, applied on the program's thread.
static muiResult AccessFrame(muiContext* context, muiNodeId root, muiAccessUpdate* updateOut,
                             const muiAccessRequest* requests, uint32_t requestCount)
{
    muiResult result = muiBuildAccessUpdate(context, root, updateOut);
    for (uint32_t i = 0; result == mui_success && i < requestCount; i++)
    {
        // A node that does not take the action, or is gone, is skipped.
        muiResult applied = muiPerformAccessAction(context, &requests[i], NULL);
        result = applied == mui_errorInvalid ? applied : mui_success;
    }
    return result;
}
```

The first update after enabling holds the whole tree, the button in it
named "OK"; a press requested by assistive technology reaches the
program as the button's activation (section 8). A request to move a
text's selection or change its text is not the context's, so this loop
skips it; a program with an editing field gives those two to
`muiTextPerformAccessAction` instead, as `samples/app.c` does.

The adapters, each its own component, are AT-SPI on Linux, UI
Automation on Windows, NSAccessibility on macOS, UIAccessibility on
iOS, Android's accessibility, and ARIA elements on the web
(`maul-ui/access_*.h`). Maul Window's glue connects the one for the
window's platform. Android's is partly Java, `java/maul/ui`, which the
application compiles with its own; its native code reaches that Java by
name, so an application that shrinks its code (as a Gradle release
build does, with ProGuard's rules) adds `java/proguard-rules.pro` to
its rules.

A window should open with a control focused, as platform toolkits'
windows do: a keyboard user then has a place to start, and Orca 46
announces nothing for the first focus of a window that opened with
none, taking that focus silently while it handles the key that moved
it.

## 12. Building and testing

Maul UI builds with CMake 3.25 and a C23 compiler, GCC 14 or Clang 19
or newer, `clang-cl` on Windows; the text component needs a C++
compiler for HarfBuzz. A program takes it as a subdirectory or, once
installed, as a package; both give the target `maul-ui::maul-ui`, and
the install a pkg-config file as well:

```cmake
add_subdirectory(maul-ui)
# or: find_package(maul-ui REQUIRED)
target_link_libraries(app PRIVATE maul-ui::maul-ui)
```

Its parts are options:

- `MAUL_UI_TEXT`, on: the text service. It fetches FreeType, HarfBuzz
  and Maul Unicode at configure time; the `FETCHCONTENT_SOURCE_DIR_*`
  variables the README names point at local copies to build offline,
  and `MAUL_UI_TEXT_SYSTEM_LIBRARIES` links the installed FreeType and
  HarfBuzz instead. Off, the core
  alone needs none of them.
- `MAUL_UI_ACCESS_TREE`, on, and the adapter for the platform built,
  each its own option (`MAUL_UI_ATSPI`, `MAUL_UI_UIA` and so on).
- `MAUL_UI_RHI`, off: the reference renderer, `maul-ui-rhi::maul-ui-rhi`,
  with Maul RHI.
- `MAUL_UI_WINDOW`, off: the glue to Maul Window, `maul-ui-window`.
- `MAUL_UI_BUILD_SHARED`, off: a shared library instead of a static
  one.

Nothing in the core needs a window, a GPU or a thread: a program's
interface is tested by building its screen in a context and reading
what layout and drawing give, as this guide's own tests do
(`test/test_guide*.c`). The context counts its work, so a test can
hold a frame to what it should cost:

```c
// A test a program can run with no window and no GPU: its screen laid
// out and drawn twice with nothing changed, the second frame doing no
// work at all.
static bool StillFrameIsFree(muiContext* context, muiNodeId root)
{
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    if (muiComputeLayout(context, root, &layout) != mui_success ||
        muiBuildDrawList(context, root, &draw) != mui_success)
    {
        return false;
    }
    muiWorkCounts before = muiGetWorkCounts(context);
    if (muiComputeLayout(context, root, &layout) != mui_success ||
        muiBuildDrawList(context, root, &draw) != mui_success)
    {
        return false;
    }
    muiWorkCounts after = muiGetWorkCounts(context);
    return after.styled == before.styled && after.sized == before.sized &&
           after.measured == before.measured && after.painted == before.painted;
}
```

The library's own tests run under AddressSanitizer, UndefinedBehavior
Sanitizer and ThreadSanitizer (`MAUL_UI_SANITIZE`, `MAUL_UI_TSAN`); its
layout is held to Chrome's on a corpus of fixtures; and the bytes fonts
bring have libFuzzer targets (`MAUL_UI_FUZZ`, with Clang), which CI
runs on every push. Maul Window's headless test backend runs the
glue's tests without a window system.
