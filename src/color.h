// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Colors as transitions move them: premultiplied Oklab, as CSS Color 4
// interpolates colors where no legacy result is owed (record mui-0004).

#ifndef MAUL_UI_SRC_COLOR_H
#define MAUL_UI_SRC_COLOR_H

#include "maul-ui/visual.h"

// A color's Oklab lightness, a and b, each times its alpha, and its alpha.
void muiColorToChannels(muiColor color, float channelsOut[4]);

// The color of four channels, held to the sRGB gamut and alpha from 0 to
// 1; a color with no alpha is clear black.
muiColor muiColorFromChannels(const float channels[4]);

#endif // MAUL_UI_SRC_COLOR_H
