// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer (record mui-0005), the optional
// target maul-ui-rhi: a draw list drawn with Maul RHI, every command an
// instance of one pipeline whose fragment shader evaluates it. Each
// frame, while the host builds it, muiRhiRenderer_AddPasses adds the
// renderer's upload and draw passes into the host's target; after
// mrhiCompileFrame, muiRhiRenderer_Record records them. The core and the
// text component never depend on it; it is a static library, whose
// functions MUI_RHI_API marks.

#ifndef MAUL_UI_RHI_RENDERER_H
#define MAUL_UI_RHI_RENDERER_H

#include "maul-rhi/device.h"
#include "maul-rhi/frame.h"
#include "maul-ui/base.h"
#include "maul-ui/draw.h"

#include <stdbool.h>
#include <stdint.h>

#define MUI_RHI_API extern

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiRhiRenderer muiRhiRenderer;

    // Maul UI's text service (maul-ui/text.h).
    typedef struct muiTextService muiTextService;

    // An image a host's key names: a 2D texture of the renderer's device
    // that shaders sample (mrhi_textureSampled), its texels with
    // premultiplied alpha and sampled as floats (an sRGB format for
    // sRGB-encoded texels), and its size in texels.
    typedef struct muiRhiImage
    {
        mrhiTextureId texture;
        uint32_t width;
        uint32_t height;
    } muiRhiImage;

    // Finds the image a key names, while muiRhiRenderer_AddPasses runs, at
    // most once a key a frame: true and the image, or false for a key the
    // host has no image for, which is then not drawn.
    typedef bool (*muiRhiImageFunction)(void* context, uint64_t key, muiRhiImage* imageOut);

    // How a renderer is made. Build it with muiDefaultRhiRendererDef.
    typedef struct muiRhiRendererDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        // The device it draws with; the renderer does not own it.
        mrhiDevice* device;
        // The format of the targets it draws into: an sRGB one encodes
        // the list's linear colors.
        mrhiFormat targetFormat;
        // The instances it holds room for at first; it grows as lists
        // need, at least 1.
        uint32_t instances;
        // The device's frameUploadBytes (its mrhiDeviceLimits), 1 MiB by
        // default as Maul RHI's: a frame whose uploads would not fit is
        // refused. A frame uploads only the records that changed since
        // the last, but a list's first frame uploads 144 bytes a drawn
        // command and its glyphs' new images.
        uint64_t uploadBytes;
        // The host's images, and the context handed to the function; with
        // no function, images are not drawn.
        muiRhiImageFunction image;
        void* imageContext;
        // The text service whose fonts glyph runs name, or NULL to draw
        // none: the renderer packs its glyphs into an atlas of its own,
        // which needs Maul UI built with its text component
        // (MAUL_UI_TEXT). The service outlives the renderer.
        muiTextService* text;
        // The format of the depth textures projected targets may carry, so
        // that a panel in a 3D scene is hidden by what stands in front of
        // it (record mui-0005), and the test against them; mrhi_formatNone,
        // the default, for none. Depth is tested, never written, as UI is
        // drawn after a scene's opaque geometry.
        mrhiFormat depthFormat;
        mrhiCompareFunction depthCompare;
    } muiRhiRendererDef;

    // Where a frame's list is drawn: a texture of the frame, its size in
    // device pixels, and whether it is cleared to a color first or drawn
    // over.
    typedef struct muiRhiTarget
    {
        mrhiResourceId resource;
        uint32_t width;
        uint32_t height;
        bool clear;
        // Linear and premultiplied, when clear is set.
        muiLinearColor clearColor;
        // Whether the list is drawn through projection, as a panel in a
        // 3D scene (record mui-0005): a column-major 4x4 matrix taking a
        // point of the list, in logical units with z 0, into the target's
        // clip space. Its edges and glyphs stay sharp at any angle: every
        // glyph run is drawn from its distance field, and edges and clips
        // are antialiased by the pixels a unit covers where it is drawn.
        // Unset, the list is drawn at the target's pixels by its scale.
        bool projected;
        float projection[16];
        // A depth texture of the frame in the def's depthFormat, its
        // depth tested against the projection's and never written; a null
        // id for none. Only with a projection.
        mrhiResourceId depth;
    } muiRhiTarget;

    /// The default def: the C library's allocation, no device, an
    /// sRGB RGBA8 target, room for 1024 instances, no depth and a
    /// less-or-equal depth test.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_RHI_API muiRhiRendererDef muiDefaultRhiRendererDef(void);

    /// Makes a renderer: its shader, its pipeline, whose creation
    /// finishes later (muiRhiRenderer_GetPipelineRequest), and its
    /// instance buffer.
    ///
    /// @param def          The def, from muiDefaultRhiRendererDef.
    /// @param rendererOut  Receives the renderer; NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         def not from muiDefaultRhiRendererDef, a half-set
    ///         allocator, no device, instances or upload bytes, or a text service
    ///         where Maul UI was built without text; `mui_errorCapacity`
    ///         when memory runs out; `mui_errorPlatform` when the device
    ///         refuses the shader, the pipeline, a buffer or a texture.
    /// @par Thread safety
    /// Safe from any thread; the renderer and its device are used by one
    /// thread at a time.
    MUI_NODISCARD MUI_RHI_API muiResult muiCreateRhiRenderer(const muiRhiRendererDef* def,
                                                             muiRhiRenderer** rendererOut);

    /// Destroys a renderer and its device objects, which the device
    /// retires after the frames that used them; NULL is ignored.
    ///
    /// @param renderer  The renderer.
    /// @par Thread safety
    /// Safe from any thread; the renderer and its device are used by one
    /// thread at a time.
    MUI_RHI_API void muiDestroyRhiRenderer(muiRhiRenderer* renderer);

    /// The request its pipeline's creation answers on the device's
    /// notification queue (mrhi_devicePipelineReady).
    ///
    /// @param renderer  The renderer.
    /// @return The request; a null one for a NULL renderer.
    /// @par Thread safety
    /// Safe from any thread; the renderer and its device are used by one
    /// thread at a time.
    MUI_RHI_API mrhiRequestId muiRhiRenderer_GetPipelineRequest(const muiRhiRenderer* renderer);

    /// Hands the renderer a notification the host took from the
    /// device's queue (mrhiNextDeviceNotification): the one answering its
    /// pipeline makes it ready, or failed.
    ///
    /// @param renderer      The renderer.
    /// @param notification  The notification.
    /// @return Whether the notification was the renderer's.
    /// @par Thread safety
    /// Safe from any thread; the renderer and its device are used by one
    /// thread at a time.
    MUI_RHI_API bool muiRhiRenderer_Notify(muiRhiRenderer* renderer,
                                           const mrhiDeviceNotification* notification);

    /// Whether its pipelines are ready, so that frames draw: one, and a
    /// second testing depth when the def names a depth format.
    ///
    /// @param renderer  The renderer.
    /// @return Whether it is ready; false for a NULL renderer.
    /// @par Thread safety
    /// Safe from any thread; the renderer and its device are used by one
    /// thread at a time.
    MUI_RHI_API bool muiRhiRenderer_IsReady(const muiRhiRenderer* renderer);

    /// Adds the renderer's passes to the frame the device is building:
    /// one uploading what changed of the list's instances and the glyph
    /// images the atlas changed, one drawing them into the target. The
    /// list is read now; it may change after the call.
    ///
    /// @param renderer  The renderer.
    /// @param list      The list.
    /// @param target    Where it is drawn.
    /// @return `mui_success`; `mui_empty` when its pipelines are not ready
    ///         yet, nothing added; `mui_errorInvalid` for a NULL
    ///         argument, a target of no size, or a depth texture without
    ///         a projection or a def's depth format; `mui_errorCapacity` when
    ///         memory runs out, or the frame's uploads would not fit the
    ///         def's uploadBytes, nothing added; `mui_errorPlatform` when
    ///         the device refuses a pass, a buffer or a texture.
    /// @par Thread safety
    /// Safe from any thread; the renderer and its device are used by one
    /// thread at a time.
    MUI_NODISCARD MUI_RHI_API muiResult muiRhiRenderer_AddPasses(muiRhiRenderer* renderer,
                                                                 const muiDrawList* list,
                                                                 const muiRhiTarget* target);

    /// Records the passes muiRhiRenderer_AddPasses added, after the frame
    /// is compiled; nothing when it added none.
    ///
    /// @param renderer  The renderer.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL renderer;
    ///         `mui_errorPlatform` when the device refuses a command.
    /// @par Thread safety
    /// Safe from any thread; the renderer and its device are used by one
    /// thread at a time.
    MUI_NODISCARD MUI_RHI_API muiResult muiRhiRenderer_Record(muiRhiRenderer* renderer);

    /// Forgets what the last recorded frame uploaded, after the host
    /// dropped that frame instead of submitting it: the next frame
    /// uploads every record and the glyph images that frame wrote.
    ///
    /// @param renderer  The renderer, or NULL for nothing.
    /// @par Thread safety
    /// Safe from any thread; the renderer is used by one thread at a
    /// time.
    MUI_RHI_API void muiRhiRenderer_Forget(muiRhiRenderer* renderer);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_RHI_RENDERER_H
