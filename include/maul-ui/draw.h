// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The draw-command list (record mui-0005): what a renderer draws for a
// subtree, as fixed-size records in paint order, each with an index into
// a clip table and a transform table, so that a renderer evaluates clips
// per command and batches across them. Coordinates are logical units;
// colors are linear light with premultiplied alpha. Identical trees give
// byte-identical lists.

#ifndef MAUL_UI_DRAW_H
#define MAUL_UI_DRAW_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A color in linear light, its red, green and blue premultiplied by
    // its alpha.
    typedef struct muiLinearColor
    {
        float r;
        float g;
        float b;
        float a;
    } muiLinearColor;

    // A value per corner, physical: top left first, then clockwise.
    typedef struct muiCorners
    {
        float topLeft;
        float topRight;
        float bottomRight;
        float bottomLeft;
    } muiCorners;

    // A value per side, physical: top first, then clockwise.
    typedef struct muiSides
    {
        float top;
        float right;
        float bottom;
        float left;
    } muiSides;

    // What a command draws.
    typedef uint32_t muiDrawKind;

    enum
    {
        // A box with rounded corners, its fill and its borders.
        mui_drawBox = 1,
        // A blurred shadow of a rounded box, outside or inside it.
        mui_drawShadow = 2,
        // An image the host names, stretched or in nine slices.
        mui_drawImage = 3,
    };

    // The color space a gradient's colors move through between stops.
    typedef uint32_t muiDrawInterpolation;

    enum
    {
        // Premultiplied Oklab, as transitions move colors.
        mui_interpolateOklab = 1,
    };

    enum
    {
        // The stops of a gradient in the list.
        MUI_MAX_DRAW_STOPS = 4
    };

    // A gradient of the gradient table. kind is mui_gradientLinear or
    // mui_gradientRadial (maul-ui/visual.h); angle is in degrees clockwise
    // from toward the top, for a linear one.
    typedef struct muiDrawGradient
    {
        uint32_t kind;
        uint32_t stopCount;
        muiDrawInterpolation interpolation;
        float angle;
        muiLinearColor colors[MUI_MAX_DRAW_STOPS];
        float positions[MUI_MAX_DRAW_STOPS];
    } muiDrawGradient;

    // A rounded box: rect is its border box. The gradient, an index into
    // the gradient table, 0 for none, is painted over fill; the borders lie
    // inside rect.
    typedef struct muiDrawBox
    {
        muiRect rect;
        muiCorners radii;
        muiLinearColor fill;
        uint32_t gradient;
        uint32_t reserved;
        muiSides borderWidths;
        // Top, right, bottom and left.
        muiLinearColor borderColors[4];
    } muiDrawBox;

    // A shadow of the rounded box rect and radii: outside it, or inside it
    // when inset is 1, offset, grown by spread and blurred over blur, as
    // CSS's box-shadow.
    typedef struct muiDrawShadow
    {
        muiRect rect;
        muiCorners radii;
        muiLinearColor color;
        float offsetX;
        float offsetY;
        float blur;
        float spread;
        uint32_t inset;
    } muiDrawShadow;

    // An image the host's key names, its uv rectangle (0 to 1 for all of
    // it) drawn into rect and multiplied by tint. Slice insets, in image
    // pixels, cut it into nine parts whose corners keep their size at one
    // logical unit per pixel; all 0 stretches it whole.
    typedef struct muiDrawImage
    {
        muiRect rect;
        uint64_t image;
        muiRect uv;
        muiSides slice;
        muiLinearColor tint;
    } muiDrawImage;

    // A command: what it draws, the clip it is drawn in (an index of the
    // clip table, 0 for none) and the transform its coordinates go through
    // (an index of the transform table).
    typedef struct muiDrawCommand
    {
        muiDrawKind kind;
        uint32_t clip;
        uint32_t transform;
        uint32_t reserved;
        union
        {
            muiDrawBox box;
            muiDrawShadow shadow;
            muiDrawImage image;
        };
    } muiDrawCommand;

    // A clip: drawing is kept inside the rounded rect, or outside it when
    // invert is 1, and inside its parent, an index of the clip table (0
    // for none), too.
    typedef struct muiDrawClip
    {
        muiRect rect;
        muiCorners radii;
        uint32_t parent;
        uint32_t transform;
        uint32_t invert;
    } muiDrawClip;

    // A 2D affine transform: x' = a x + c y + e, y' = b x + d y + f.
    typedef struct muiDrawTransform
    {
        float a;
        float b;
        float c;
        float d;
        float e;
        float f;
    } muiDrawTransform;

    // What a list is for.
    typedef struct muiDrawHeader
    {
        // The host's key for the surface, as muiDrawInput gave it.
        uint64_t surface;
        // Counts the context's builds, from 1.
        uint64_t generation;
        // The root's size, in logical units.
        float width;
        float height;
        // Device pixels per logical unit.
        float scale;
        uint32_t reserved;
    } muiDrawHeader;

    // A list, valid until the context's next build. Index 0 of the clip
    // and gradient tables is a placeholder for none; entry 0 of the
    // transform table is the identity.
    typedef struct muiDrawList
    {
        muiDrawHeader header;
        const muiDrawCommand* commands;
        uint32_t commandCount;
        uint32_t clipCount;
        const muiDrawClip* clips;
        const muiDrawTransform* transforms;
        uint32_t transformCount;
        uint32_t gradientCount;
        const muiDrawGradient* gradients;
    } muiDrawList;

    // What a build draws for.
    typedef struct muiDrawInput
    {
        uint64_t surface;
        // Device pixels per logical unit, above 0: what snapping rounds to.
        float scale;
    } muiDrawInput;

    /// Paints a root's subtree, as its last muiComputeLayout left it, into
    /// the context's list, and clears the subtree's paint requests. When
    /// nothing below the root asked for paint since the last build of the
    /// same root, surface and scale, the list stays as it is, generation
    /// and all; otherwise subtrees nothing asked to repaint, at the origin
    /// and opacity they were painted at, copy their commands from the last
    /// list, which gives the bytes a build from nothing would. Per node, in paint order: its
    /// outer shadow, its box, its inner shadow and its image, then its children, depth first; a
    /// node that clips draws its children inside its rounded border box. Opacity multiplies down
    /// the subtree into every command's colors. At the identity transform, box and image edges and
    /// clips snap to device pixels, and border widths to whole device pixels, at least one.
    ///
    /// @param context  The context.
    /// @param rootId   The root.
    /// @param input    The surface and scale.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a scale that is not a finite number above 0, or a
    ///         call from a measure function; `mui_errorStale` for a root that
    ///         is gone; `mui_errorCapacity` when the list needs more commands,
    ///         clips or gradients than the context's limits, which leaves the
    ///         list empty.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiBuildDrawList(muiContext* context, muiNodeId rootId,
                                                     const muiDrawInput* input);

    /// Shows the context's last list.
    ///
    /// @param context  The context.
    /// @param listOut  Receives the list; empty before any build.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGetDrawList(const muiContext* context, muiDrawList* listOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_DRAW_H
