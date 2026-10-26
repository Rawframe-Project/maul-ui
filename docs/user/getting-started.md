# Getting started

## Contents

- [Requirements](#requirements)
- [Building and installing](#building-and-installing)
- [Build options](#build-options)
- [The model](#the-model)
- [A first screen](#a-first-screen)
- [Results, ids and defs](#results-ids-and-defs)

## Requirements

- CMake 3.25 or newer.
- GCC 14 or newer, Clang 19 or newer, or Apple Clang. On Windows, `clang-cl`, the Visual Studio component "C++ Clang tools for Windows". The library is C23. Its public headers are C17 and C++17, so any C or C++ program can include them.
- For the web, Emscripten, or Clang's `wasm32-wasi` with wasi-libc; [The web](../internals/web.md) says what each takes.
- A C++ compiler as well for the text component, which builds HarfBuzz.
- A network connection at configure time, for Maul Tween and the text component's libraries, unless they are given as local copies ([Build options](#build-options)).

## Building and installing

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix <prefix>
```

The install holds the headers, the library, a CMake package and a pkg-config file. A program takes the library as a package or as a subdirectory; both give the target `maul-ui::maul-ui`:

```cmake
find_package(maul-ui REQUIRED CONFIG)   # or: add_subdirectory(maul-ui)
target_link_libraries(app PRIVATE maul-ui::maul-ui)
```

or through pkg-config as `maul-ui`. Before 1.0 the package accepts only its exact version, since any release may change the interface. [samples/minimal](../../samples/minimal) is a program built against an install alone.

Maul Tween and the text component's dependencies are built into the library, so an installed library links with nothing more than the C library and libm. A program that links the static library through CMake links as C, though the library holds HarfBuzz's C++ objects; this build sets `LINKER_LANGUAGE C` for its own programs, and a program in a Visual Studio project should do the same to keep its C standard.

## Build options

| Option | Default | What it builds |
|---|---|---|
| `MAUL_UI_TEXT` | on | The text component: fonts, shaping, paragraphs, editing and glyph images. Off, the library is the core alone, with Maul Tween built in. |
| `MAUL_UI_ACCESS_TREE` | on | The accessibility tree's consumer, which the platform adapters read. |
| `MAUL_UI_UIA`, `MAUL_UI_ATSPI`, `MAUL_UI_NSACCESSIBILITY`, `MAUL_UI_UIACCESSIBILITY`, `MAUL_UI_ANDROID_ACCESSIBILITY`, `MAUL_UI_ARIA` | on, on their platform | The accessibility adapter for Windows, Linux, macOS, iOS, Android and the web ([Accessibility](accessibility.md)). |
| `MAUL_UI_RHI` | off | `maul-ui-rhi`, the reference renderer on Maul RHI ([Drawing](drawing.md#the-reference-renderer)). |
| `MAUL_UI_WINDOW` | off | `maul-ui-window`, the glue to Maul Window ([Maul Window](maul-window.md)). |
| `MAUL_UI_BUILD_SHARED` | off | A shared library instead of a static one. |
| `MAUL_UI_TEXT_SYSTEM_LIBRARIES` | off | Links the installed FreeType and HarfBuzz instead of building the pinned releases. Results then depend on their versions. |

The core fetches Maul Tween, and the text component FreeType, HarfBuzz and Maul Unicode, at configure time, each checked by its hash or tag. To build offline, point `FETCHCONTENT_SOURCE_DIR_MAUL-TWEEN`, `FETCHCONTENT_SOURCE_DIR_MAUL_UI_FREETYPE`, `FETCHCONTENT_SOURCE_DIR_MAUL_UI_HARFBUZZ` and `FETCHCONTENT_SOURCE_DIR_MAUL-UNICODE` at local copies of the same releases; the core alone needs only Maul Tween's:

```sh
git clone --depth 1 --branch v0.5.0 https://github.com/Rawframe-Project/maul-tween.git ../maul-tween
cmake -B build -DMAUL_UI_TEXT=OFF -DFETCHCONTENT_SOURCE_DIR_MAUL-TWEEN=$PWD/../maul-tween
```

`MAUL_UI_RHI` and `MAUL_UI_WINDOW` find Maul RHI and Maul Window installed, or fetch them at their release tags.

## The model

A program keeps its interface in a context, as a tree of nodes. Each node has a style: where it goes and how large it is (layout), how it looks (visual), its text's font and color, and how it answers input. What the library does not draw itself, a text or an image, a node names by a host key, and the program measures and paints it when asked.

Each frame the program lays a root out in the space it has, then builds a draw list: boxes, shadows, images and glyph runs, back to front, which a renderer draws. Maul UI draws nothing itself and opens no window. Maul Window and the reference renderer on Maul RHI can do both, or a program's own window and renderer do.

## A first screen

A screen is built once. Each node here takes its whole layout and its background from values set on the node itself:

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

A dimension is a scale of the parent's content box plus an offset: `{1, 0}` is all of it and `{0, 48}` is 48 units. A root's parent is the space the frame gives it. Flex places the children: the column puts the bar on top and gives the row what is left (`grow`), and the row shares its width between the two panels.

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

Layout and drawing are kept between frames. Laying out again revisits only what changed: an edit, a new size, content marked changed, a running transition. `muiIsUpdatePending` says whether anything is owed. A build reuses what it drew for subtrees that did not change, and a transparent node draws no box.

## Results, ids and defs

Every call that can fail returns a `muiResult`: `mui_success`, or a value that names what happened.

| Result | Meaning |
|---|---|
| `mui_empty` | A queue had nothing to take. |
| `mui_errorInvalid` | An argument outside the contract: a bug in the program. |
| `mui_errorStale` | An id whose object is gone. |
| `mui_errorCapacity` | A limit or a buffer is too small. |
| `mui_errorFormat` | Damaged data, such as a font. |
| `mui_errorPlatform` | The platform refused. |
| `mui_errorVersion` | A root def built against another release's headers. |
| `mui_errorState` | A call back into an object from host code it is running, such as a measure function editing the tree. |

A call on a context, a text service, an access tree, a glyph atlas or a window glue clears what it writes on any failure, before it looks at its arguments: zeros, the null id, `NULL`. The count or length a too-small buffer needed is the one exception. A `mui_empty` answer, which reads nothing, leaves zeros too, unless the call's header says what it gives, and each header says what a call leaves. `muiResultName` names any of them. Ids name what a context owns: a slot and a generation. A destroyed node's slot is used again with a new generation, so an old id never names its successor. Refusals of invalid input are counted, a number a release build can watch for a program's bugs; stale ids are not:

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

Objects are made from defs, which start from their defaults (`muiDefaultContextDef`, `muiDefaultNodeDef` and so on). A def's cookie tells a def made so from one left uninitialized. A context takes its allocator and its limits there, and reserves the memory its limits name when it is made: a frame allocates nothing, and a limit reached refuses with `mui_errorCapacity` rather than growing.

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

Its 65th node is refused, and destroying the context gives every byte back. The library asks an allocator for no alignment above `max_align_t`. A context is used from one thread at a time; separate contexts may run on separate threads.

The library computes in a floating-point environment of its own: round to nearest, subnormal numbers kept, no traps. A thread may run with flush-to-zero, denormals-are-zero or another rounding mode. Each call that computes switches to the library's environment and gives the thread's back before it returns, and the program's own functions the library calls (measure, baseline, paint, access text, events, the allocator, an adapter's action function) run in the thread's. Layout, drawing and text come out the same whatever the thread has set. WebAssembly has only the library's environment, so nothing switches there.

Next: [Nodes and layout](layout.md).
