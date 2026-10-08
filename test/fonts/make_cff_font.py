# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulCff.otf, an OpenType font of CFF outlines (record mui-0006):
# 1000 units per em and an A of two boxes that overlap, the outline of
# MaulOverlap.ttf's A, so a test holds the CFF driver's field of it to
# the TrueType one. Needs fontTools (pip install fonttools); its
# timestamps are fixed, so it writes the same bytes each time.
#
#     python3 make_cff_font.py

import io
import pathlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen

# 2026-01-01 in seconds from 1904, the epoch of the head table.
TIMESTAMP = 3850070400


def boxes(width, *rectangles):
    # Counter-clockwise, as CFF's outer contours go.
    pen = T2CharStringPen(width, None)
    for left, bottom, right, top in rectangles:
        pen.moveTo((left, bottom))
        pen.lineTo((right, bottom))
        pen.lineTo((right, top))
        pen.lineTo((left, top))
        pen.closePath()
    return pen.getCharString()


def main():
    builder = FontBuilder(1000, isTTF=False)
    builder.setupGlyphOrder([".notdef", "A"])
    builder.setupCharacterMap({0x41: "A"})
    names = {"FullName": "Maul Cff", "FamilyName": "Maul Cff", "Weight": "Regular"}
    builder.setupCFF("MaulCff-Regular", names,
                     {".notdef": boxes(500), "A": boxes(900, (0, 0, 600, 700), (300, 0, 900, 700))},
                     {})
    builder.setupHorizontalMetrics({".notdef": (500, 0), "A": (900, 0)})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({
        "familyName": "Maul Cff",
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
    path = pathlib.Path(__file__).with_name("MaulCff.otf")
    path.write_bytes(out.getvalue())


if __name__ == "__main__":
    main()
