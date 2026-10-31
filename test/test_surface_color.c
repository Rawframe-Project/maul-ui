// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The colour the samples configure a window surface in
// (SampleSurfaceColor), from caps written by hand: 8-bit sRGB in Rec. 709
// however many other colours a surface lists before it, as Vulkan's
// VK_EXT_swapchain_colorspace lets a driver do.

#include "harness.h"
#include "test_harness.h"

static const mrhiSurfaceColor s_bgraSrgb = {mrhi_formatBgra8Unorm, mrhi_primariesBt709,
                                            mrhi_transferSrgb, mrhi_rangeStandard};
static const mrhiSurfaceColor s_rgbaSrgb = {mrhi_formatRgba8Unorm, mrhi_primariesBt709,
                                            mrhi_transferSrgb, mrhi_rangeStandard};
static const mrhiSurfaceColor s_bgraLinear = {mrhi_formatBgra8Unorm, mrhi_primariesBt709,
                                              mrhi_transferLinear, mrhi_rangeStandard};
static const mrhiSurfaceColor s_bgraExtended = {mrhi_formatBgra8Unorm, mrhi_primariesBt709,
                                                mrhi_transferSrgb, mrhi_rangeExtended};
static const mrhiSurfaceColor s_halfLinear = {mrhi_formatRgba16Float, mrhi_primariesBt709,
                                              mrhi_transferLinear, mrhi_rangeExtended};

static mrhiSurfaceCaps Caps(const mrhiSurfaceColor* colors, uint32_t count, bool twinImages)
{
    mrhiSurfaceCaps caps = {0};
    caps.presentable = true;
    for (uint32_t i = 0; i < count; i++)
    {
        caps.colors[i] = colors[i];
    }
    caps.colorCount = count;
    caps.twinImages = twinImages;
    caps.twinViews = !twinImages;
    return caps;
}

static bool IsColor(const mrhiSurfaceColor* color, mrhiFormat format, mrhiTransfer transfer,
                    mrhiColorRange range)
{
    return color->format == format && color->primaries == mrhi_primariesBt709 &&
           color->transfer == transfer && color->range == range;
}

static void TestBeforeSrgb(void)
{
    // A linear and an extended BGRA in Rec. 709 listed before the sRGB one.
    const mrhiSurfaceColor colors[] = {s_bgraLinear, s_bgraExtended, s_bgraSrgb};
    mrhiSurfaceCaps caps = Caps(colors, 3, true);
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    mrhiFormat draw = mrhi_formatNone;
    CHECK(SampleSurfaceColor(&caps, &config, &draw) &&
              IsColor(&config.color, mrhi_formatBgra8UnormSrgb, mrhi_transferSrgb,
                      mrhi_rangeStandard) &&
              draw == mrhi_formatBgra8UnormSrgb,
          "sRGB past a linear and an extended BGRA");
}

static void TestRgba(void)
{
    // No BGRA in sRGB: half floats first, which Maul RHI would suggest
    // before RGBA, are passed over.
    const mrhiSurfaceColor colors[] = {s_halfLinear, s_bgraLinear, s_rgbaSrgb};
    mrhiSurfaceCaps caps = Caps(colors, 3, true);
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    mrhiFormat draw = mrhi_formatNone;
    CHECK(SampleSurfaceColor(&caps, &config, &draw) &&
              IsColor(&config.color, mrhi_formatRgba8UnormSrgb, mrhi_transferSrgb,
                      mrhi_rangeStandard) &&
              draw == mrhi_formatRgba8UnormSrgb,
          "RGBA in sRGB past half floats");
    // BGRA asked first, wherever it is listed.
    const mrhiSurfaceColor both[] = {s_rgbaSrgb, s_bgraSrgb};
    caps = Caps(both, 2, true);
    CHECK(SampleSurfaceColor(&caps, &config, &draw) && draw == mrhi_formatBgra8UnormSrgb,
          "BGRA first");
}

static void TestTwinViews(void)
{
    // Without sRGB images, the images keep the unorm format and frames draw
    // in a view of its twin.
    const mrhiSurfaceColor colors[] = {s_bgraExtended, s_rgbaSrgb};
    mrhiSurfaceCaps caps = Caps(colors, 2, false);
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    mrhiFormat draw = mrhi_formatNone;
    CHECK(
        SampleSurfaceColor(&caps, &config, &draw) &&
            IsColor(&config.color, mrhi_formatRgba8Unorm, mrhi_transferSrgb, mrhi_rangeStandard) &&
            config.viewFormats[0] == mrhi_formatRgba8UnormSrgb && draw == mrhi_formatRgba8UnormSrgb,
        "a view of the twin");
}

static void TestNone(void)
{
    // No 8-bit sRGB in Rec. 709 at all: refused, the config left alone.
    const mrhiSurfaceColor colors[] = {s_halfLinear, s_bgraLinear, s_bgraExtended};
    mrhiSurfaceCaps caps = Caps(colors, 3, true);
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    const mrhiSurfaceConfig before = config;
    mrhiFormat draw = mrhi_formatNone;
    CHECK(!SampleSurfaceColor(&caps, &config, &draw) && draw == mrhi_formatNone &&
              config.color.format == before.color.format &&
              config.color.primaries == before.color.primaries &&
              config.color.transfer == before.color.transfer &&
              config.color.range == before.color.range &&
              config.viewFormats[0] == before.viewFormats[0],
          "no sRGB colour");
}

int main(void)
{
    TestBeforeSrgb();
    TestRgba();
    TestTwinViews();
    TestNone();
    return s_failures == 0 ? 0 : 1;
}
