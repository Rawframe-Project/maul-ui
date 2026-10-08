# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulLargeGlyph.ttf, the font the glyph image tests render too
# large in (record mui-0006): 16 units per em, the fewest a font may
# have, and an A that is a box 16 ems a side, so at the largest size an
# image would be far wider than FreeType's rasterizer allows; a B 16 ems
# wide and 1 tall and a C 1 em wide and 16 tall, too large one way
# alone. Needs
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


def box(width, height=None):
    height = width if height is None else height
    pen = TTGlyphPen(None)
    if width:
        pen.moveTo((0, 0))
        pen.lineTo((0, height))
        pen.lineTo((width, height))
        pen.lineTo((width, 0))
        pen.closePath()
    return pen.glyph()


def main():
    builder = FontBuilder(16, isTTF=True)
    builder.setupGlyphOrder([".notdef", "A", "B", "C"])
    builder.setupCharacterMap({0x41: "A", 0x42: "B", 0x43: "C"})
    builder.setupGlyf({".notdef": box(0), "A": box(256), "B": box(256, 16), "C": box(16, 256)})
    builder.setupHorizontalMetrics({".notdef": (8, 0), "A": (256, 0), "B": (256, 0), "C": (16, 0)})
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
