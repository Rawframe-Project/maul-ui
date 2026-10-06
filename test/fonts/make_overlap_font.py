# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulOverlap.ttf, the font the distance field tests render
# overlapping contours in (record mui-0006): 1000 units per em and an A
# made of two boxes that overlap, as variable fonts' glyphs often are,
# marked with the TrueType overlap flag. Needs fontTools (pip install
# fonttools); its timestamps are fixed, so it writes the same bytes each
# time.
#
#     python3 make_overlap_font.py

import io
import pathlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

# 2026-01-01 in seconds from 1904, the epoch of the head table.
TIMESTAMP = 3850070400

# The glyf flag that marks a simple glyph's contours as overlapping.
OVERLAP_SIMPLE = 0x40


def boxes(*rectangles):
    pen = TTGlyphPen(None)
    for left, bottom, right, top in rectangles:
        pen.moveTo((left, bottom))
        pen.lineTo((left, top))
        pen.lineTo((right, top))
        pen.lineTo((right, bottom))
        pen.closePath()
    return pen.glyph()


def main():
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder([".notdef", "A"])
    builder.setupCharacterMap({0x41: "A"})
    glyph = boxes((0, 0, 600, 700), (300, 0, 900, 700))
    glyph.flags[0] |= OVERLAP_SIMPLE
    builder.setupGlyf({".notdef": boxes(), "A": glyph})
    builder.setupHorizontalMetrics({".notdef": (500, 0), "A": (900, 0)})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({
        "familyName": "Maul Overlap",
        "styleName": "Regular",
        "copyright": "Copyright (c) 2026 Sirac Ozmen. MIT License.",
    })
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=0,
                     usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.setupHead(unitsPerEm=1000, created=TIMESTAMP, modified=TIMESTAMP)
    builder.font.recalcTimestamp = False
    out = io.BytesIO()
    builder.font.save(out)
    path = pathlib.Path(__file__).with_name("MaulOverlap.ttf")
    path.write_bytes(out.getvalue())


if __name__ == "__main__":
    main()
