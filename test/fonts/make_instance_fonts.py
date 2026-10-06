# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes the fonts the font instance tests read (record mui-0006): 1000
# units per em and an A, a box 400 wide and 700 high with an advance of
# 500 at the default instance, whose axes move it by amounts a test can
# tell apart:
#
# - wght, 100 to 900 from 400: the right side and the advance grow by
#   400 at 900 and shrink by 150 at 100 (in MaulVariableSlant.ttf, 100
#   to 500, growing by 100 at 500, so bold is out of its reach);
# - ital, 0 to 1 from 0: the top moves right by 100 at 1;
# - slnt, -15 to 0 from 0: the top moves right by 188 at -15;
# - opsz, 8 to 72 from 16: the right side and the advance shrink by 100
#   at 72 and grow by 50 at 8.
#
# MaulVariable.ttf has all four axes; MaulVariableSlant.ttf all but ital;
# MaulVariableItalic.ttf ital alone; MaulItalic.ttf none, its face
# marked italic.
#
# Needs fontTools (pip install fonttools); its timestamps are fixed, so
# it writes the same bytes each time.
#
#     python3 make_instance_fonts.py

import io
import pathlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables.TupleVariation import TupleVariation

# 2026-01-01 in seconds from 1904, the epoch of the head table.
TIMESTAMP = 3850070400


def box(left, bottom, right, top):
    pen = TTGlyphPen(None)
    if right > left:
        pen.moveTo((left, bottom))
        pen.lineTo((left, top))
        pen.lineTo((right, top))
        pen.lineTo((right, bottom))
        pen.closePath()
    return pen.glyph()


def deltas(right=0, top=0, advance=0):
    # The box's points from its bottom left, clockwise, then the four
    # phantom points: the origin, the advance, and the vertical two.
    return [(0, 0), (top, 0), (right + top, 0), (right, 0),
            (0, 0), (advance, 0), (0, 0), (0, 0)]


def build(name, family, axes, italic_face=False):
    # axes: a tuple of the axis tags to give, and the heaviest weight.
    tags, heaviest = axes
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder([".notdef", "A"])
    builder.setupCharacterMap({0x41: "A"})
    builder.setupGlyf({".notdef": box(0, 0, 0, 0), "A": box(0, 0, 400, 700)})
    builder.setupHorizontalMetrics({".notdef": (500, 0), "A": (500, 0)})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({
        "familyName": family,
        "styleName": "Italic" if italic_face else "Regular",
        "copyright": "Copyright (c) 2026 Sirac Ozmen. MIT License.",
    })
    gain = {900: 400, 500: 100}.get(heaviest, 0)
    ranges = {
        "wght": (100, 400, heaviest, "Weight"),
        "ital": (0, 0, 1, "Italic"),
        "slnt": (-15, 0, 0, "Slant"),
        "opsz": (8, 16, 72, "Optical size"),
    }
    variations = {
        "wght": [
            TupleVariation({"wght": (0, 1, 1)}, deltas(right=gain, advance=gain)),
            TupleVariation({"wght": (-1, -1, 0)}, deltas(right=-150, advance=-150)),
        ],
        "ital": [TupleVariation({"ital": (0, 1, 1)}, deltas(top=100))],
        "slnt": [TupleVariation({"slnt": (-1, -1, 0)}, deltas(top=188))],
        "opsz": [
            TupleVariation({"opsz": (0, 1, 1)}, deltas(right=-100, advance=-100)),
            TupleVariation({"opsz": (-1, -1, 0)}, deltas(right=50, advance=50)),
        ],
    }
    if tags:
        builder.setupFvar([(tag,) + ranges[tag] for tag in tags], [])
        builder.setupGvar({".notdef": [],
                           "A": [v for tag in tags for v in variations[tag]]})
    # fsSelection: italic, else regular.
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=0,
                     usWinAscent=800, usWinDescent=200, usWeightClass=400,
                     fsSelection=0x01 if italic_face else 0x40)
    builder.setupPost(italicAngle=-14 if italic_face else 0)
    builder.setupHead(unitsPerEm=1000, created=TIMESTAMP, modified=TIMESTAMP,
                      macStyle=0x02 if italic_face else 0)
    builder.font.recalcTimestamp = False
    out = io.BytesIO()
    builder.font.save(out)
    pathlib.Path(__file__).with_name(name).write_bytes(out.getvalue())


def main():
    build("MaulVariable.ttf", "Maul Variable", (("wght", "ital", "slnt", "opsz"), 900))
    build("MaulVariableSlant.ttf", "Maul Variable Slant", (("wght", "slnt", "opsz"), 500))
    build("MaulVariableItalic.ttf", "Maul Variable Italic", (("ital",), 900))
    build("MaulItalic.ttf", "Maul Italic", ((), 900), italic_face=True)


if __name__ == "__main__":
    main()
