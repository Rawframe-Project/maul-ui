// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The colour a sample's window surface is configured in, apart from the
// harness so that a test can check it without a window or a device.

#include "harness.h"

// The sRGB twin of an 8-bit unorm format, or none.
static mrhiFormat TwinOf(mrhiFormat format)
{
    switch (format)
    {
    case mrhi_formatRgba8Unorm:
        return mrhi_formatRgba8UnormSrgb;
    case mrhi_formatBgra8Unorm:
        return mrhi_formatBgra8UnormSrgb;
    default:
        return mrhi_formatNone;
    }
}

bool SampleSurfaceColor(const mrhiSurfaceCaps* caps, mrhiSurfaceConfig* config,
                        mrhiFormat* drawFormatOut)
{
    // 8-bit sRGB in Rec. 709, BGRA asked first. Where a surface has no
    // BGRA of it, Maul RHI may suggest half floats before an RGBA one, so
    // RGBA is asked for in its turn.
    static const mrhiSurfaceColor asked[] = {
        {mrhi_formatBgra8Unorm, mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard},
        {mrhi_formatRgba8Unorm, mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard},
    };
    for (size_t i = 0; i < sizeof asked / sizeof asked[0]; i++)
    {
        mrhiSurfaceColor color;
        if (mrhiSuggestSurfaceColor(caps, &asked[i], &color) != mrhi_success)
        {
            return false;
        }
        mrhiFormat twin = TwinOf(color.format);
        if (twin == mrhi_formatNone || color.primaries != mrhi_primariesBt709 ||
            color.transfer != mrhi_transferSrgb || color.range != mrhi_rangeStandard)
        {
            continue;
        }
        config->color = color;
        if (caps->twinImages)
        {
            config->color.format = twin;
        }
        else
        {
            config->viewFormats[0] = twin;
        }
        *drawFormatOut = twin;
        return true;
    }
    return false;
}
