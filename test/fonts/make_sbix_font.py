# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulSbix.ttf, the font the sbix tests render (record mui-0006):
# 1000 units per em, outlines in glyf, and an sbix table written here
# byte by byte, its 20 ppem strike first and its 10 ppem strike second.
# At 10 ppem a unit is a tenth of a pixel:
#
#   glyph 1 "boxed"  an outline box from 100 to 500 across and -100 to
#                    300 up; 4 by 4 red, its origin offset 1, 0, so its
#                    left at 1 + 1 and its bottom at 0 - 1 (the box's
#                    lower left corner). At 20 ppem: 8 by 8 green, its
#                    origin offset 2, 0.
#   glyph 2 "bare"   no outline; 2 by 2 blue, its origin offset 1, 2.
#   glyph 3 "copy"   no outline; 'dupe' of glyph 2.
#   glyph 4 "jpeg"   a 'jpg ' graphic, not drawn.
#   glyph 5 "loop"   'dupe' of itself.
#   glyph 6 "half"   an outline box from 150 to 350 and 0 to 200; 2 by 2
#                    red at origin offset 0, 0: its left at 1.5 pixels.
#   glyph 7 "mislabel"  a PNG called 'jpg ', not drawn.
#
# Needs fontTools and Pillow; fixed timestamps, so it writes the same
# bytes each time.
#
#     python3 make_sbix_font.py

import io
import pathlib
import struct

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables.DefaultTable import DefaultTable
from PIL import Image

TIMESTAMP = 3850070400
NAMES = [".notdef", "boxed", "bare", "copy", "jpeg", "loop", "half", "mislabel"]


def box(left, bottom, right, top):
    pen = TTGlyphPen(None)
    pen.moveTo((left, bottom))
    pen.lineTo((left, top))
    pen.lineTo((right, top))
    pen.lineTo((right, bottom))
    pen.closePath()
    return pen.glyph()


def solid(color, size):
    image = Image.new("RGBA", (size, size), color)
    out = io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


def graphic(x, y, kind, data):
    return struct.pack(">hh4s", x, y, kind) + data


def strike(ppem, glyphs):
    """glyphs: {glyph id: data}; the rest have none."""
    count = len(NAMES)
    header = 4 + 4 * (count + 1)
    offsets = []
    body = b""
    for glyph in range(count):
        offsets.append(header + len(body))
        body += glyphs.get(glyph, b"")
    offsets.append(header + len(body))
    return struct.pack(">HH", ppem, 72) + b"".join(struct.pack(">I", o) for o in offsets) + body


def sbix():
    red = (255, 0, 0, 255)
    ten = strike(
        10,
        {
            1: graphic(1, 0, b"png ", solid(red, 4)),
            2: graphic(1, 2, b"png ", solid((0, 0, 255, 255), 2)),
            3: graphic(0, 0, b"dupe", struct.pack(">H", 2)),
            4: graphic(0, 0, b"jpg ", b"\xff\xd8\xff\xd9"),
            5: graphic(0, 0, b"dupe", struct.pack(">H", 5)),
            6: graphic(0, 0, b"png ", solid(red, 2)),
            7: graphic(0, 0, b"jpg ", solid(red, 2)),
        },
    )
    twenty = strike(20, {1: graphic(2, 0, b"png ", solid((0, 255, 0, 255), 8))})
    header_size = 8 + 4 * 2
    header = struct.pack(">HHI", 1, 1, 2)
    header += struct.pack(">II", header_size, header_size + len(twenty))
    return header + twenty + ten


def main():
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder(NAMES)
    builder.setupCharacterMap({0x41 + i: name for i, name in enumerate(NAMES[1:])})
    empty = TTGlyphPen(None).glyph()
    outlines = {name: empty for name in NAMES}
    outlines["boxed"] = box(100, -100, 500, 300)
    outlines["half"] = box(150, 0, 350, 200)
    builder.setupGlyf(outlines)
    bearings = {"boxed": 100, "half": 150}
    builder.setupHorizontalMetrics({name: (1000, bearings.get(name, 0)) for name in NAMES})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({"familyName": "Maul Sbix", "styleName": "Regular"})
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.updateHead(created=TIMESTAMP, modified=TIMESTAMP)
    table = DefaultTable("sbix")
    table.data = sbix()
    builder.font["sbix"] = table
    out = io.BytesIO()
    builder.save(out)
    pathlib.Path(__file__).with_name("MaulSbix.ttf").write_bytes(out.getvalue())


if __name__ == "__main__":
    main()
