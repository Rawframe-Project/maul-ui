# Drawing

## Contents

- [The draw list](#the-draw-list)
- [Content the program draws](#content-the-program-draws)
- [Glyphs](#glyphs)
- [The reference renderer](#the-reference-renderer)
- [Writing a renderer](#writing-a-renderer)

## The draw list

A draw list is what a renderer draws for a root: fixed-size commands in paint order, boxes with their rounded corners, borders and gradients, shadows, images and glyph runs. Coordinates are logical units, colors are linear light with premultiplied alpha, and the same tree gives the same bytes. Each command names a clip and a transform by index. Index 0 of the clip table is no clip, and entry 0 of the transform table is the identity, so a renderer can batch across both:

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

`muiNode_BuildDrawList` builds the list for a root as its last layout left it, at the scale and surface the draw input names, and `muiContext_GetDrawList` shows it until the next build. The header's generation changes only when the list does, so a program that sees the same generation can skip presenting the frame.

## Content the program draws

A node with host content is painted by the frame's paint function, which adds glyph runs and rectangles through a sink, relative to the node's content box, in the node's clip and opacity. This is how a program draws what the library does not, such as a chart or a gauge:

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

A node 200 units wide and a quarter full is painted as one box 50 units wide, which lands 100 device pixels wide at a scale of 2. For long content, `muiDrawSink_GetVisibleRect` tells the part of the content box that can be seen, so only that is painted. A paint function that cannot paint all it has to, as when its memory ran out, calls `muiDrawSink_Fail`: `muiNode_BuildDrawList` keeps what was added and returns `mui_errorCapacity`, and the next build paints the node again rather than copying it from this list.

An image is a visual value: a host key naming the image, with nine-slice insets, a tint and a repeat mode. The library never loads images; the renderer looks the key up.

## Glyphs

Glyph runs name a font key, a size and glyph ids. A renderer draws their images from an atlas, which renders them once (coverage, distance fields or colour, as `muiTextService_RenderGlyph` and its siblings do) and packs them into pages it keeps:

```c
// Makes sure every glyph of a list is in the atlas, then takes the
// rectangles of its pages that changed, which a renderer uploads. How
// many glyphs are packed.
static uint32_t PackGlyphs(muiGlyphAtlas* atlas, const muiDrawList* list, muiAtlasUpdate* updates,
                           uint32_t capacity, uint32_t* updateCount)
{
    uint32_t packed = 0;
    float scale = list->header.scale;
    muiGlyphAtlas_BeginFrame(atlas);
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
            if (muiGlyphAtlas_GetGlyph(atlas, run->font, glyph->id, run->size * scale,
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

The first frame packs every glyph and gives the rectangles to upload; the next, its glyphs packed already, gives none. A plot of glyphs no frame has used lately is emptied when room runs out. Text drawn scaled or in 3D looks sharp drawn from multi-channel distance fields (`muiGlyphAtlas_GetGlyphMultiField`), and colour emoji come from a colour atlas (`muiGlyphAtlas_GetColorGlyph`).

## The reference renderer

The reference renderer, `maul-ui-rhi` (`MAUL_UI_RHI`), does all of this with Maul RHI on Vulkan, Direct3D 12, Metal and WebGPU: each command is an instance of one pipeline, its glyphs come from atlases it keeps, and images from a function the program gives it. Each frame, `muiRhiRenderer_AddPasses` adds its upload and draw passes into the program's frame, and `muiRhiRenderer_Record` records them once the frame is compiled. Its target takes an optional projection, which draws a panel in 3D, tested against the scene's depth. The samples (`samples/`) run it in a window with Maul Window.

## Writing a renderer

A renderer of a program's own walks the list in order:

- bind or look up the command's clip chain and transform, and evaluate the chain per fragment or per command; a clip change need not end a batch;
- draw a box as a rounded rectangle with its four border widths and colors, and its gradient from the gradient table when it has one;
- draw a shadow as a Gaussian blur of its rounded shape, outside its box or, inset, inside it;
- draw an image's slices as its uv runs, which is reversed for a mirrored image;
- draw glyph runs from an atlas, positioned at the run's origin plus each glyph's offset.

Edges are already snapped to device pixels per kind, and colors are already premultiplied, so a renderer blends premultiplied into an sRGB target.
