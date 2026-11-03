# Reference renderer

`maul-ui-rhi` (the option `MAUL_UI_RHI`, off by default) draws draw lists with Maul RHI. It is a static library of its own beside the core, in `rhi/`, with Maul RHI found installed or fetched at its release tag ([Rhi.cmake](../../cmake/Rhi.cmake)). The core and the text component never see it. It exists so that a host has a renderer that draws every command exactly as the list means it, and so that the list's promises are tested on real GPUs.

## Contents

- [One pipeline of quads](#one-pipeline-of-quads)
- [Frames](#frames)
- [Shapes, gradients and shadows](#shapes-gradients-and-shadows)
- [Images](#images)
- [Glyphs](#glyphs)
- [World space](#world-space)
- [Culling and uploads](#culling-and-uploads)
- [Shaders](#shaders)
- [Determinism](#determinism)

## One pipeline of quads

Every command is an instance of one pipeline of quads. Its record goes in a storage buffer uploaded each frame, and the vertex shader makes its six vertices ([pack.c](../../rhi/src/pack.c)). The fragment shader evaluates a rounded rectangle's signed distance in pixels for coverage, splitting the fill (inside the borders' inner edge) from the borders, each side's color where that side is nearest. Colors blend premultiplied into an sRGB target.

Transforms and clips come from their tables in two more buffers. Each quad's corners go through its transform, and distances are measured in its own pixels and made screen pixels by the transform's scale, so edges stay a pixel wide at any scale. Each fragment is brought back through each of its clips' transforms for coverage. The tables always have entry 0, and every index into them is checked as the list is packed, so no list makes the shaders read past one.

The plan starts a new draw only where a texture changes ([plan.c](../../rhi/src/plan.c)); boxes and shadows join any draw. `test_rhi_pack` holds this with a list that changes clip, transform and gradient at every command.

## Frames

Each frame, `muiRhiRenderer_AddPasses` adds the renderer's upload and draw passes into the host's target while the host builds its frame, and `muiRhiRenderer_Record` records them after the frame is compiled ([renderer.c](../../rhi/src/renderer.c)). The pipeline's creation is answered on the device's queue, which the host reads and hands on; until then frames draw nothing. A frame whose uploads would not fit the device's frame uploads is refused before any pass is added. A buffer or texture the renderer makes, a stream's buffer grown, a glyph page or the placeholder, reads as zeros in the first frame that uses it: Maul RHI clears it in a pass of its own before the renderer's, which costs that frame a clear and nothing after.

## Shapes, gradients and shadows

Gradients come from the list's table in a second storage buffer, mixed in premultiplied Oklab as the core's transitions mix colors; a conic gradient finds its turn from the point's angle about the centre. Shadows are Gaussian blurs of their rounded shape in Evan Wallace's closed form, exact along one axis and four samples along the other. Their shape is spread with CSS's radius adjustment and drawn outside their box, or inside it when inset.

## Images

Images are textures the host's function names by key, asked for once per key a frame ([images.c](../../rhi/src/images.c)). An image is one instance whose fragment maps its place to a uv piecewise, so a nine-slice's corners and edges keep their insets' size (shrunk by one factor where facing ones would not fit) and its middle fills as the list says, with no seams between parts. The texture and a linear sampler are bound in a second table.

## Glyphs

Glyph runs come from three glyph atlases of the renderer's own over the host's text service ([Glyphs](glyphs.md#atlases)): coverage, multi-channel fields and colour glyphs ([glyphs.c](../../rhi/src/glyphs.c)). Their pages are numbered together as they are first used, made as R8, RGBA8 and sRGB RGBA8 textures, and only the rectangles the atlases changed are uploaded, gutters included, so every texel a glyph samples has been written.

- A run whose transform only moves it is drawn as coverage rendered at its device pixels.
- A run a transform scales, or a glyph too large for the atlas as coverage, is drawn from multi-channel distance fields of an em of 32, 64 or 128 pixels (the least at least the em drawn) and a spread of 4 field pixels. The median of each sample's channels is made a distance in screen pixels, so corners stay sharp however far a field is magnified.
- A glyph with colour layers is drawn from its colour image instead, its texel faded by the run's alpha.

A host built without the text component's atlas gives no text service, and the renderer refuses one given.

## World space

World-space UI is the same list through a projection the renderer applies, as NoesisGUI and Unity's world-space canvas draw UI in 3D, rather than into a texture first as Unreal's widget component does. The renderer's target takes an optional column-major 4x4 matrix from the list's logical units into clip space. Under it, edges and clips are antialiased by the pixels a unit covers at each fragment, clips are evaluated at the fragment's point of the list, every glyph run is drawn from its distance field, and only clips cull. The host turns its ray into a point of the list for hit testing.

A renderer made with a depth format tests a projected panel against a depth texture the target names without writing it, as Unity's UI shader tests the scene's depth: UI is drawn after the scene's opaque geometry, and what stands in front of a panel hides it.

## Culling and uploads

As the list is packed, an instance whose quad, through its transform, misses its clip chain's bounds or the target is not drawn, nor is its image asked for ([cull.c](../../rhi/src/cull.c)). No scissor is set, since culling already drops what one would. Each stream keeps the records its buffer holds, and a frame writes only the runs that differ, nearby runs merged, so a frame with one change uploads little ([streams.c](../../rhi/src/streams.c)).

## Shaders

The shaders are GLSL and WGSL in `rhi/shaders/`, made into a Maul RHI shader container offline by [gen_rhi_shaders.py](../../tools/gen_rhi_shaders.py), which needs a Maul RHI source tree for its cross-compilers. The container is committed as `rhi/shaders/quad_container.h`, because CI has none of those tools. A stamp of its inputs' hashes lets CI check that it is current without them ([Development](../operations/development.md#the-shader-container)).

Its tests run on Maul RHI's test driver everywhere, and on lavapipe and headless Chrome's WebGPU where those exist, comparing probed pixels with what the list says, under the Vulkan validation layer ([Test environments](../operations/test-environments.md)).

## Determinism

The renderer is outside the core's promise of the same bits everywhere ([Overview](overview.md)). Its pixels come from the GPU, whose rasterization and arithmetic differ between devices, so its tests probe pixels against what the list says rather than compare bits. Everything it computes on the CPU runs in the host's floating-point environment and is not bit-exact across platforms, compilers or the host's rounding and flush-to-zero settings. That covers all that packing a list computes ([pack.c](../../rhi/src/pack.c)), such as a glyph run's pens and rectangles, its field's distance scale, atlas coordinates and colours, a shadow's spread corners and blur reach, and an image's nine-slice fit; the culling bounds of each quad through its transform ([cull.c](../../rhi/src/cull.c)); and the frame's setup, the target's 2 / width and 2 / height among it ([renderer.c](../../rhi/src/renderer.c)). One of these results is discrete: the em of the distance field a scaled glyph run is drawn from, 32, 64 or 128 pixels, is picked from the em drawn, which comes from the square root of the transform's determinant. A run drawn at an em near 32 or 64 can take a different field on another platform, and so look slightly different. The draw list it reads stays the core's, the same bits everywhere. `tools/check_float_env.py` checks the public functions marked `MUI_API` only, on purpose, so the renderer's `MUI_RHI_API` functions are not wrappers.
