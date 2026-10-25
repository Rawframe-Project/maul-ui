// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Maul UI's FreeType options: FreeType's defaults, less what the text
// component refuses or does not need. Without zlib, LZW, bzip2 or Brotli,
// compressed fonts (WOFF, WOFF2, gzip) are refused, and without
// environment properties, nothing outside the build changes how glyphs
// render.
#ifndef MAUL_UI_FTOPTION_H
#define MAUL_UI_FTOPTION_H

#include <freetype/config/ftoption.h>

#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES
#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_HARFBUZZ
#undef FT_CONFIG_OPTION_USE_BROTLI
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK
#undef FT_CONFIG_OPTION_INCREMENTAL
#undef FT_CONFIG_OPTION_SVG
#undef TT_CONFIG_OPTION_BDF
// Glyphs are always loaded unhinted (FT_LOAD_NO_HINTING): distance fields
// and outlines are drawn at any scale, so the TrueType bytecode
// interpreter that hints them is never run.
#undef TT_CONFIG_OPTION_BYTECODE_INTERPRETER
#undef TT_CONFIG_OPTION_SUBPIXEL_HINTING
// ftoption.h derives these two from the options above at its end, before
// the lines above can take the options back, so they are taken back too.
#undef TT_USE_BYTECODE_INTERPRETER
#undef TT_SUPPORT_SUBPIXEL_HINTING_MINIMAL
// The Adobe Glyph List only lets FreeType make a Unicode charmap from
// glyph names. The text component maps characters through HarfBuzz and
// reads no FreeType charmap or glyph name.
#undef FT_CONFIG_OPTION_ADOBE_GLYPH_LIST

#endif // MAUL_UI_FTOPTION_H
