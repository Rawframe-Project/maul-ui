# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulColor.ttf, the font the colour glyph tests render (record
# mui-0006): 1000 units per em, an A of COLR version 0 layers (one with no
# outline, a box of palette entry 0, the box's right half of entry 1, a
# small box of the text's colour, entry 0xFFFF, and a small box of entry
# 2, past the palettes) over two CPAL palettes of two entries, and a B
# with no colour, its outline a plain box. Needs fontTools (pip install
# fonttools); its timestamps are fixed, so it writes the same bytes each
# time.
#
#     python3 make_color_font.py

import io
import pathlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

# 2026-01-01 in seconds from 1904, the epoch of the head table.
TIMESTAMP = 3850070400


def box(left, bottom, right, top):
    pen = TTGlyphPen(None)
    pen.moveTo((left, bottom))
    pen.lineTo((left, top))
    pen.lineTo((right, top))
    pen.lineTo((right, bottom))
    pen.closePath()
    return pen.glyph()


def main():
    names = [".notdef", "A", "B", "whole", "half", "dot", "empty", "stray"]
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder(names)
    builder.setupCharacterMap({ord("A"): "A", ord("B"): "B"})
    glyphs = {
        ".notdef": box(100, 0, 900, 800),
        # The colour glyph's own outline, drawn where colour is not.
        "A": box(100, 0, 900, 800),
        "B": box(100, 0, 900, 800),
        "whole": box(100, 0, 900, 800),
        "half": box(500, 0, 900, 800),
        "dot": box(300, 200, 500, 400),
        "empty": TTGlyphPen(None).glyph(),
        "stray": box(600, 200, 800, 400),
    }
    builder.setupGlyf(glyphs)
    # A left side bearing of each outline's own left, as TrueType places
    # outlines by it.
    bearings = {"half": 500, "dot": 300, "empty": 0, "stray": 600}
    builder.setupHorizontalMetrics({name: (1000, bearings.get(name, 100)) for name in names})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupCPAL(
        [
            [(1.0, 0.0, 0.0, 1.0), (0.0, 0.0, 1.0, 0.5)],
            [(0.0, 1.0, 0.0, 1.0), (1.0, 1.0, 0.0, 1.0)],
        ]
    )
    # A layer with no outline, and one of an entry past the palettes.
    layers = [("empty", 0), ("whole", 0), ("half", 1), ("dot", 0xFFFF), ("stray", 2)]
    builder.setupCOLR({"A": layers}, version=0)
    builder.setupNameTable({"familyName": "Maul Color", "styleName": "Regular"})
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.updateHead(created=TIMESTAMP, modified=TIMESTAMP)
    out = io.BytesIO()
    builder.save(out)
    path = pathlib.Path(__file__).with_name("MaulColor.ttf")
    path.write_bytes(out.getvalue())


if __name__ == "__main__":
    main()
