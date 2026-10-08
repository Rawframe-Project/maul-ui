#!/usr/bin/env python3
# Writes the fuzz targets' seeds (test/fuzz_*.c), each into
# corpus-fuzz_<area> under a directory:
# - inflate: zlib streams of stored, fixed and dynamic blocks, each after
#   the two bytes of its inflated size, as fuzz_inflate reads them;
# - png: PNG images of every colour type and bit depth, a palette with
#   tRNS, Adam7 interlacing and the five filters;
# - font: the test fonts, TrueType, CFF, variable, COLR, CBDT and sbix.
# The standard library only.
#
# Usage: python3 tools/fuzz_seed.py [DIRECTORY]  (from the repository root;
#        DIRECTORY defaults to the current one)

import pathlib
import shutil
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent


def write(directory, name, data):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / name).write_bytes(data)


def inflate_seeds(directory):
    texts = [b"", b"a", b"abracadabra " * 40, bytes(range(256)) * 4]
    for i, text in enumerate(texts):
        # Level 0 stores, 1 mostly fixed codes, 9 dynamic ones.
        for level in (0, 1, 9):
            stream = zlib.compress(text, level)
            write(directory, f"z{i}-{level}", struct.pack(">H", len(text)) + stream)


def chunk(kind, body):
    return (struct.pack(">I", len(body)) + kind + body +
            struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))


def png(width, height, depth, colour, rows, interlace=0, extra=b""):
    header = struct.pack(">IIBBBBB", width, height, depth, colour, 0, 0, interlace)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + extra +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def rows_of(width, height, bits_per_pixel, filters):
    stride = (width * bits_per_pixel + 7) // 8
    out = bytearray()
    for y in range(height):
        out.append(filters[y % len(filters)])
        out.extend((x * 37 + y * 11) & 0xFF for x in range(stride))
    return bytes(out)


def png_seeds(directory):
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
    depths = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}
    for colour, allowed in depths.items():
        for depth in allowed:
            bits = channels[colour] * depth
            extra = b""
            if colour == 3:
                entries = 1 << depth
                extra = (chunk(b"PLTE", bytes(i * 7 & 0xFF for i in range(3 * entries))) +
                         chunk(b"tRNS", bytes(i * 3 & 0xFF for i in range(entries))))
            rows = rows_of(5, 4, bits, (0, 1, 2, 3, 4))
            write(directory, f"c{colour}-d{depth}.png", png(5, 4, depth, colour, rows, 0, extra))
    # Adam7: the seven passes of an 8 by 8 RGBA image, each row filtered
    # with Paeth.
    passes = [(1, 1), (1, 1), (2, 1), (2, 2), (4, 2), (4, 4), (8, 4)]
    rows = b"".join(rows_of(w, h, 32, (4,)) for w, h in passes)
    write(directory, "adam7.png", png(8, 8, 8, 6, rows, 1))


def font_seeds(directory):
    for font in sorted((ROOT / "test" / "fonts").iterdir()):
        if font.suffix in (".ttf", ".otf"):
            directory.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(font, directory / font.name)


def main():
    base = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path.cwd()
    inflate_seeds(base / "corpus-fuzz_inflate")
    png_seeds(base / "corpus-fuzz_png")
    font_seeds(base / "corpus-fuzz_font")


if __name__ == "__main__":
    main()
