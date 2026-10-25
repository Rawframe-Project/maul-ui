# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulTricky.ttf, the font the glyph tests see a tricky font's
# bytecode run in: FreeType takes a face whose family name holds "DLC" for
# one of the old CJK fonts that assemble their glyphs in TrueType bytecode,
# and runs that bytecode even when a glyph is loaded unhinted. 1000 units
# per em and an A, a box from 100 to 500 units across, whose glyph program
# moves its four points 2 pixels right. Needs fontTools (pip install
# fonttools); its timestamps are fixed, so it writes the same bytes each
# time.
#
#     python3 make_tricky_font.py

import io
import pathlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables import ttProgram

# 2026-01-01 in seconds from 1904, the epoch of the head table.
TIMESTAMP = 3850070400


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
    glyph = boxes((100, 0, 500, 700))
    # Points 0 to 3 shifted by 128/64, 2 pixels, along the x axis.
    glyph.program = ttProgram.Program()
    glyph.program.fromAssembly(["PUSHB[ ] 4", "SLOOP[ ]", "PUSHB[ ] 0 1 2 3 128", "SHPIX[ ]"])
    builder.setupGlyf({".notdef": boxes(), "A": glyph})
    builder.setupHorizontalMetrics({".notdef": (500, 0), "A": (600, 100)})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({
        "familyName": "Maul Tricky DLC",
        "styleName": "Regular",
        "copyright": "Copyright (c) 2026 Sirac Ozmen. MIT License.",
    })
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=0,
                     usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.setupMaxp()
    builder.setupHead(unitsPerEm=1000, created=TIMESTAMP, modified=TIMESTAMP)
    maxp = builder.font["maxp"]
    maxp.maxZones = 1
    maxp.maxStackElements = 8
    maxp.maxSizeOfInstructions = len(glyph.program.getBytecode())
    builder.font.recalcTimestamp = False
    out = io.BytesIO()
    builder.font.save(out)
    path = pathlib.Path(__file__).with_name("MaulTricky.ttf")
    path.write_bytes(out.getvalue())


if __name__ == "__main__":
    main()
