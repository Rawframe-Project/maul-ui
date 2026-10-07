# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulColor.ttf, the font the colour glyph tests render (record
# mui-0006): 1000 units per em, an A of COLR version 0 layers (one with no
# outline, a box of palette entry 0, the box's right half of entry 1, a
# small box of the text's colour, entry 0xFFFF, and a small box of entry
# 2, past the palettes) over two CPAL palettes of two entries, a B with no
# colour, its outline a plain box, and glyphs of version 1 paint graphs,
# one for each kind of paint the tests draw (see GRAPHS). Needs fontTools
# (pip install fonttools); its timestamps are fixed, so it writes the same
# bytes each time.
#
#     python3 make_color_font.py

import io
import pathlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables.otTables import PaintFormat

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


def solid(entry, alpha=1.0):
    return {"Format": PaintFormat.PaintSolid, "PaletteIndex": entry, "Alpha": alpha}


def shape(glyph, paint):
    return {"Format": PaintFormat.PaintGlyph, "Glyph": glyph, "Paint": paint}


# Version 1 graphs, glyph ids 9 on in this order. In pixels at an em of
# 10, "dot" is a box from 3 to 5 across and 2 to 4 up, "bar" from 3 to 7
# and 2 to 3.
GRAPHS = {
    # The box of entry 0, the dot over it of the text's colour at half alpha.
    "layered": {
        "Format": PaintFormat.PaintColrLayers,
        "Layers": [shape("whole", solid(0)), shape("dot", solid(0xFFFF, 0.5))],
    },
    # The dot moved 3 right and 1 up.
    "moved": {
        "Format": PaintFormat.PaintTranslate,
        "Paint": shape("dot", solid(0)),
        "dx": 300,
        "dy": 100,
    },
    # The bar turned a quarter counter-clockwise about its lower left
    # corner: 2 to 3 across and 2 to 6 up.
    "turned": {
        "Format": PaintFormat.PaintRotateAroundCenter,
        "Paint": shape("bar", solid(0)),
        "angle": 90,
        "centerX": 300,
        "centerY": 200,
    },
    # The dot scaled by 1.5 across and a half up about its lower left
    # corner (scales are 2.14 numbers, under 2): 3 to 6 and 2 to 3.
    "scaled": {
        "Format": PaintFormat.PaintScaleAroundCenter,
        "Paint": shape("dot", solid(0)),
        "scaleX": 1.5,
        "scaleY": 0.5,
        "centerX": 300,
        "centerY": 200,
    },
    # The dot skewed by 45 degrees across about its lower left corner, its
    # top leaning left: from 3 to 5 at 2 up, 1 to 3 at 4 up.
    "skewed": {
        "Format": PaintFormat.PaintSkewAroundCenter,
        "Paint": shape("dot", solid(0)),
        "xSkewAngle": 45,
        "ySkewAngle": 0,
        "centerX": 300,
        "centerY": 200,
    },
    # The dot by x' = x + y - 2, y' = y: from 3 to 5 at 2 up, 5 to 7 at 4.
    "transformed": {
        "Format": PaintFormat.PaintTransform,
        "Paint": shape("dot", solid(0)),
        "Transform": {"xx": 1.0, "yx": 0.0, "xy": 1.0, "yy": 1.0, "dx": -200, "dy": 0},
    },
    # "moved" moved back 3 left: the dot raised by 1.
    "reused": {
        "Format": PaintFormat.PaintTranslate,
        "Paint": {"Format": PaintFormat.PaintColrGlyph, "Glyph": "moved"},
        "dx": -300,
        "dy": 0,
    },
    # The whole box, its clip box from 3 to 5 across and 2.5 to 4 up.
    "clipped": shape("whole", solid(0)),
    # A graph that paints itself.
    "looped": {"Format": PaintFormat.PaintColrGlyph, "Glyph": "looped"},
    # The dot skewed by a quarter turn, flattened to a line of no end.
    "flattened": {
        "Format": PaintFormat.PaintSkew,
        "Paint": shape("dot", solid(0)),
        "xSkewAngle": 90,
        "ySkewAngle": 0,
    },
    # "scaled" moved 1 right: the outer transform applied last, 4 to 7
    # and 2 to 3.
    "nested": {
        "Format": PaintFormat.PaintTranslate,
        "Paint": {
            "Format": PaintFormat.PaintScaleAroundCenter,
            "Paint": shape("dot", solid(0)),
            "scaleX": 1.5,
            "scaleY": 0.5,
            "centerX": 300,
            "centerY": 200,
        },
        "dx": 100,
        "dy": 0,
    },
    # A graph that paints itself, its box its clip box's.
    "boxedLoop": {"Format": PaintFormat.PaintColrGlyph, "Glyph": "boxedLoop"},
}


def main():
    names = [".notdef", "A", "B", "whole", "half", "dot", "empty", "stray", "bar"]
    names += list(GRAPHS)
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
        "bar": box(300, 200, 700, 300),
    }
    for name in GRAPHS:
        glyphs[name] = box(100, 0, 900, 800)
    builder.setupGlyf(glyphs)
    # A left side bearing of each outline's own left, as TrueType places
    # outlines by it.
    bearings = {"half": 500, "dot": 300, "empty": 0, "stray": 600, "bar": 300}
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
    builder.setupCOLR({"A": layers, **GRAPHS}, clipBoxes={"clipped": (300, 250, 500, 400), "boxedLoop": (300, 200, 500, 400)})
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
