# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulLargeGlyph.ttf, the font the glyph image tests render too
# large in (record mui-0006): 16 units per em, the fewest a font may
# have, and an A that is a box 16 ems a side, so at the largest size an
# image would be far wider than FreeType's rasterizer allows. Needs
# fontTools (pip install fonttools); its timestamps are fixed, so it
# writes the same bytes each time.
#
#     python3 make_large_glyph_font.py

import io
import pathlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

# 2026-01-01 in seconds from 1904, the epoch of the head table.
TIMESTAMP = 3850070400


def box(side):
    pen = TTGlyphPen(None)
    if side:
        pen.moveTo((0, 0))
        pen.lineTo((0, side))
        pen.lineTo((side, side))
        pen.lineTo((side, 0))
        pen.closePath()
    return pen.glyph()


def main():
    builder = FontBuilder(16, isTTF=True)
    builder.setupGlyphOrder([".notdef", "A"])
    builder.setupCharacterMap({0x41: "A"})
    builder.setupGlyf({".notdef": box(0), "A": box(256)})
    builder.setupHorizontalMetrics({".notdef": (8, 0), "A": (256, 0)})
    builder.setupHorizontalHeader(ascent=13, descent=-3)
    builder.setupNameTable({
        "familyName": "Maul Large Glyph",
        "styleName": "Regular",
        "copyright": "Copyright (c) 2026 Sirac Ozmen. MIT License.",
    })
    builder.setupOS2(sTypoAscender=13, sTypoDescender=-3, sTypoLineGap=0,
                     usWinAscent=13, usWinDescent=3)
    builder.setupPost()
    builder.setupHead(unitsPerEm=16, created=TIMESTAMP, modified=TIMESTAMP)
    builder.font.recalcTimestamp = False
    out = io.BytesIO()
    builder.font.save(out)
    path = pathlib.Path(__file__).with_name("MaulLargeGlyph.ttf")
    path.write_bytes(out.getvalue())


if __name__ == "__main__":
    main()
