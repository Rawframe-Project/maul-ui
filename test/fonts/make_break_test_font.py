# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulBreakTest.ttf, the font the text tests break lines in
# (record mui-0006): shaping that differs when a line breaks inside it.
# A hyphen kerns against V, and a space and x form a ligature, both
# across line break opportunities. Every glyph is a box; 1000 units per
# em. Needs fontTools (pip install fonttools); its timestamps are fixed,
# so it writes the same bytes each time.
#
#     python3 make_break_test_font.py

import io
import pathlib

from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

GLYPHS = {
    # name: (code point, advance)
    ".notdef": (None, 500),
    "space": (0x20, 250),
    "hyphen": (0x2D, 300),
    "A": (0x41, 600),
    "V": (0x56, 600),
    "x": (0x78, 600),
    "space_x": (None, 850),
}

FEATURES = """
languagesystem DFLT dflt;
languagesystem latn dflt;
feature kern { pos hyphen V -200; } kern;
feature liga { sub space x by space_x; } liga;
"""

# 2026-01-01 in seconds from 1904, the epoch of the head table.
TIMESTAMP = 3850070400


def box(advance):
    pen = TTGlyphPen(None)
    if advance > 250:
        pen.moveTo((50, 0))
        pen.lineTo((50, 700))
        pen.lineTo((advance - 50, 700))
        pen.lineTo((advance - 50, 0))
        pen.closePath()
    return pen.glyph()


def main():
    names = list(GLYPHS)
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder(names)
    builder.setupCharacterMap({cp: name for name, (cp, _) in GLYPHS.items() if cp is not None})
    builder.setupGlyf({name: box(advance) for name, (_, advance) in GLYPHS.items()})
    builder.setupHorizontalMetrics({name: (advance, 50) for name, (_, advance) in GLYPHS.items()})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({
        "familyName": "Maul Break Test",
        "styleName": "Regular",
        "copyright": "Copyright (c) 2026 Sirac Ozmen. MIT License.",
    })
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=0,
                     usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.setupHead(unitsPerEm=1000, created=TIMESTAMP, modified=TIMESTAMP)
    addOpenTypeFeaturesFromString(builder.font, FEATURES)
    builder.font.recalcTimestamp = False
    out = io.BytesIO()
    builder.font.save(out)
    path = pathlib.Path(__file__).with_name("MaulBreakTest.ttf")
    path.write_bytes(out.getvalue())


if __name__ == "__main__":
    main()
