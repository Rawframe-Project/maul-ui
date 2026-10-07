# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes MaulBitmap.ttf, the font the colour bitmap tests render (record
# mui-0006): 1000 units per em, no outlines (as Noto Color Emoji has
# none), and CBLC and CBDT tables written here byte by byte so that every
# index format and image format is used. Two strikes, of 10 and 20 ppem.
# In the 10 ppem strike:
#
#   glyph 1 "one"    index format 1, image format 17: 8 by 8, red on its
#                    left half and blue on its right, its top left at 1
#                    across and 8 up;
#   glyph 2 "two"    index format 1, image format 17: 2 by 2 green at 3,
#                    5;
#   glyph 3 "three"  index format 3, image format 18: 2 by 2 blue at 2, 4;
#   glyphs 4, 5      index format 2, image format 19, the index's metrics
#                    (top left at 1, 3): 2 by 2 red and green;
#   glyphs 6, 8      index format 4 over 6 to 8, image format 17: 2 by 2
#                    blue and red at 0, 2; glyph 7 has no image;
#   glyphs 9, 11     index format 5 over 9 to 11, image format 19, the
#                    index's metrics (at 2, 2): 2 by 2 green and blue;
#                    glyph 10 has no image;
#   glyph 14         its PNG's length three bytes past its image, to be
#                    refused;
#   glyph 15         2 by 2 red at half alpha, at 0, 2.
#
# A 15 ppem strike of 8 bits a pixel, not colour, has glyph 1 all blue,
# to be passed over.
#
# In the 20 ppem strike: glyph 1 at twice the size (16 by 16 at 2, 16),
# green on its right half where the 10 ppem strike's is blue, and glyph
# 12, only here: 4 by 4, its top row red, the rest clear, at 0, 4. Glyph 13 has no bitmap. Needs fontTools and Pillow;
# fixed timestamps, so it writes the same bytes each time.
#
#     python3 make_bitmap_font.py

import io
import pathlib
import struct

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables.DefaultTable import DefaultTable
from PIL import Image

TIMESTAMP = 3850070400
RED = (255, 0, 0, 255)
GREEN = (0, 255, 0, 255)
BLUE = (0, 0, 255, 255)
CLEAR = (0, 0, 0, 0)


def png(width, height, pixel):
    """A PNG whose pixel at x, y is pixel(x, y)."""
    image = Image.new("RGBA", (width, height))
    image.putdata([pixel(x, y) for y in range(height) for x in range(width)])
    out = io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


def solid(color, size=2):
    return png(size, size, lambda x, y: color)


def halves(size, right=BLUE):
    return png(size, size, lambda x, y: RED if x < size // 2 else right)


def small(png_bytes, width, height, left, top):
    return struct.pack(">BBbbB", height, width, left, top, width) + struct.pack(">I", len(png_bytes)) + png_bytes


def big(png_bytes, width, height, left, top):
    metrics = struct.pack(">BBbbBbbB", height, width, left, top, width, 0, 0, height)
    return metrics + struct.pack(">I", len(png_bytes)) + png_bytes


def bare(png_bytes, length):
    """Image format 19 padded to length, for the fixed-size index formats."""
    data = struct.pack(">I", len(png_bytes)) + png_bytes
    return data + bytes(length - len(data))


def big_metrics(width, height, left, top):
    return struct.pack(">BBbbBbbB", height, width, left, top, width, 0, 0, height)


class Writer:
    """CBDT's images, appended; offsets from the table's start."""

    def __init__(self):
        self.data = bytearray(struct.pack(">HH", 3, 0))

    def add(self, data):
        at = len(self.data)
        self.data += data
        return at


def subtable(index_format, image_format, image_offset, body):
    return struct.pack(">HHI", index_format, image_format, image_offset) + body


def strike_10(cbdt):
    """The 10 ppem strike's subtables: (first, last, bytes)."""
    out = []
    # Format 1, image 17: glyphs 1 and 2.
    base = len(cbdt.data)
    one = small(halves(8), 8, 8, 1, 8)
    two = small(solid(GREEN), 2, 2, 3, 5)
    cbdt.add(one)
    cbdt.add(two)
    offsets = struct.pack(">III", 0, len(one), len(one) + len(two))
    out.append((1, 2, subtable(1, 17, base, offsets)))
    # Format 3, image 18: glyph 3.
    base = cbdt.add(big(solid(BLUE), 2, 2, 2, 4))
    three_length = len(cbdt.data) - base
    out.append((3, 3, subtable(3, 18, base, struct.pack(">HH", 0, three_length))))
    # Format 2, image 19: glyphs 4 and 5, one size, metrics in the index.
    four, five = solid(RED), solid(GREEN)
    size = 4 + max(len(four), len(five)) + 3
    base = cbdt.add(bare(four, size))
    cbdt.add(bare(five, size))
    out.append((4, 5, subtable(2, 19, base, struct.pack(">I", size) + big_metrics(2, 2, 1, 3))))
    # Format 4, image 17: glyphs 6 and 8 of 6 to 8.
    six = small(solid(BLUE), 2, 2, 0, 2)
    eight = small(solid(RED), 2, 2, 0, 2)
    base = len(cbdt.data)
    cbdt.add(six)
    cbdt.add(eight)
    pairs = struct.pack(">I", 2) + struct.pack(">HHHHHH", 6, 0, 8, len(six), 0, len(six) + len(eight))
    out.append((6, 8, subtable(4, 17, base, pairs)))
    # Format 5, image 19: glyphs 9 and 11 of 9 to 11.
    nine, eleven = solid(GREEN), solid(BLUE)
    size = 4 + max(len(nine), len(eleven)) + 5
    base = cbdt.add(bare(nine, size))
    cbdt.add(bare(eleven, size))
    body = struct.pack(">I", size) + big_metrics(2, 2, 2, 2) + struct.pack(">IHH", 2, 9, 11)
    out.append((9, 11, subtable(5, 19, base, body)))
    # Format 1, image 17: glyph 14, its PNG's length three bytes past its
    # image, and glyph 15, half clear red.
    fourteen = solid(GREEN)
    fourteen = struct.pack(">BBbbB", 2, 2, 0, 2, 2) + struct.pack(">I", len(fourteen) + 3) + fourteen
    fifteen = small(solid((255, 0, 0, 128)), 2, 2, 0, 2)
    base = len(cbdt.data)
    cbdt.add(fourteen)
    cbdt.add(fifteen)
    offsets = struct.pack(">III", 0, len(fourteen), len(fourteen) + len(fifteen))
    out.append((14, 15, subtable(1, 17, base, offsets)))
    return out


def strike_15(cbdt):
    """A strike of 8 bits a pixel, not colour, to be passed over."""
    base = len(cbdt.data)
    one = small(png(12, 12, lambda x, y: BLUE), 12, 12, 1, 12)
    cbdt.add(one)
    return [(1, 1, subtable(1, 17, base, struct.pack(">II", 0, len(one))))]


def strike_20(cbdt):
    out = []
    base = len(cbdt.data)
    one = small(halves(16, GREEN), 16, 16, 2, 16)
    cbdt.add(one)
    out.append((1, 1, subtable(1, 17, base, struct.pack(">II", 0, len(one)))))
    base = len(cbdt.data)
    twelve = small(png(4, 4, lambda x, y: RED if y < 1 else CLEAR), 4, 4, 0, 4)
    cbdt.add(twelve)
    out.append((12, 12, subtable(1, 17, base, struct.pack(">II", 0, len(twelve)))))
    return out


def cblc(strikes):
    """strikes: [(ppem, depth, [(first, last, subtable bytes)])]."""
    header = struct.pack(">HHI", 3, 0, len(strikes))
    records = b""
    arrays = b""
    at = len(header) + 48 * len(strikes)
    for ppem, depth, subtables in strikes:
        entries = b""
        bodies = b""
        offset = 8 * len(subtables)
        for first, last, body in subtables:
            entries += struct.pack(">HHI", first, last, offset)
            bodies += body
            offset += len(body)
        array = entries + bodies
        first = min(s[0] for s in subtables)
        last = max(s[1] for s in subtables)
        lines = bytes(12)
        records += struct.pack(">IIII", at, len(array), len(subtables), 0) + lines + lines
        records += struct.pack(">HHBBBb", first, last, ppem, ppem, depth, 1)
        arrays += array
        at += len(array)
    return header + records + arrays


def main():
    names = [".notdef", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen"]
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder(names)
    builder.setupCharacterMap({0x41 + i: name for i, name in enumerate(names[1:])})
    builder.setupGlyf({name: TTGlyphPen(None).glyph() for name in names})
    builder.setupHorizontalMetrics({name: (1000, 0) for name in names})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    cbdt = Writer()
    strikes = [(10, 32, strike_10(cbdt)), (15, 8, strike_15(cbdt)), (20, 32, strike_20(cbdt))]
    builder.setupNameTable({"familyName": "Maul Bitmap", "styleName": "Regular"})
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.updateHead(created=TIMESTAMP, modified=TIMESTAMP)
    font = builder.font
    # No outlines, as a bitmap-only emoji font has none.
    del font["glyf"]
    del font["loca"]
    for tag, data in (("CBLC", cblc(strikes)), ("CBDT", bytes(cbdt.data))):
        table = DefaultTable(tag)
        table.data = data
        font[tag] = table
    out = io.BytesIO()
    font.save(out)
    pathlib.Path(__file__).with_name("MaulBitmap.ttf").write_bytes(out.getvalue())


if __name__ == "__main__":
    main()
