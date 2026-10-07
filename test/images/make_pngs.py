# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes pngs.bin, the PNG decoder's cases (record mui-0006): images of
# every colour type and bit depth, plain and interlaced, their rows
# filtered by each of the five filters in turn, their data stored, fixed
# or dynamic and split over several chunks; images Pillow encodes; and
# damaged images to be refused. Each image's expected RGBA comes from the
# samples it was made of, by the specification's rules, and Pillow's own
# decoding of it must agree. Needs Pillow (pip install pillow); seeded,
# so it writes the same bytes each time.
#
#     python3 make_pngs.py
#
# The file: a little-endian uint32 count, then per case its PNG's length
# and bytes, its width and height, and its width * height * 4 bytes of
# RGBA; a width and height of 0 for an image to be refused.

import io
import pathlib
import random
import struct
import zlib

from PIL import Image

SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
PASSES = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def pack_row(samples, depth):
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    if depth == 8:
        return bytes(samples)
    out = bytearray()
    per = 8 // depth
    for i in range(0, len(samples), per):
        byte = 0
        group = samples[i : i + per]
        for j, s in enumerate(group):
            byte |= s << (8 - depth * (j + 1))
        out.append(byte)
    return bytes(out)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filter_rows(rows, bpp, first_filter, only_filter=None):
    out = bytearray()
    prior = bytes(len(rows[0])) if rows else b""
    for y, row in enumerate(rows):
        kind = (first_filter + y) % 5 if only_filter is None else only_filter
        out.append(kind)
        for i, x in enumerate(row):
            a = row[i - bpp] if i >= bpp else 0
            b = prior[i]
            c = prior[i - bpp] if i >= bpp else 0
            predicted = [0, a, b, (a + b) // 2, paeth(a, b, c)][kind]
            out.append((x - predicted) & 255)
        prior = row
    return bytes(out)


def encode(width, height, kind, depth, samples, interlaced, palette=None, trns=None, level=6, strategy=zlib.Z_DEFAULT_STRATEGY, split=0, first_filter=0, only_filter=None):
    """samples[y][x] is a tuple of the pixel's samples."""
    channels = CHANNELS[kind]
    bpp = max(1, channels * depth // 8)
    raw = b""
    passes = PASSES if interlaced else [(0, 0, 1, 1)]
    for x0, y0, dx, dy in passes:
        xs = list(range(x0, width, dx))
        ys = list(range(y0, height, dy))
        if not xs or not ys:
            continue
        rows = [pack_row([s for x in xs for s in samples[y][x]], depth) for y in ys]
        raw += filter_rows(rows, bpp, first_filter, only_filter)
    compressor = zlib.compressobj(level, zlib.DEFLATED, 15, 9, strategy)
    data = compressor.compress(raw) + compressor.flush()
    png = SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, kind, 0, 0, 1 if interlaced else 0))
    png += chunk(b"tEXt", b"Comment\x00skipped")
    if palette is not None:
        png += chunk(b"PLTE", b"".join(bytes(c) for c in palette))
    if trns is not None:
        png += chunk(b"tRNS", trns)
    pieces = [data[i : i + split] for i in range(0, len(data), split)] if split else [data]
    for piece in pieces:
        png += chunk(b"IDAT", piece)
    return png + chunk(b"IEND", b"")


def scale(sample, depth):
    return {1: sample * 255, 2: sample * 85, 4: sample * 17, 8: sample, 16: sample >> 8}[depth]


def expected(width, height, kind, depth, samples, palette=None, alphas=None, key=None):
    out = bytearray()
    for y in range(height):
        for x in range(width):
            s = samples[y][x]
            if kind == 3:
                r, g, b = palette[s[0]]
                a = alphas[s[0]] if alphas and s[0] < len(alphas) else 255
            elif kind in (0, 4):
                r = g = b = scale(s[0], depth)
                a = scale(s[1], depth) if kind == 4 else (0 if key is not None and s[0] == key[0] else 255)
            else:
                r, g, b = (scale(v, depth) for v in s[:3])
                a = scale(s[3], depth) if kind == 6 else (0 if key is not None and tuple(s[:3]) == key else 255)
            out += bytes((r, g, b, a))
    return bytes(out)


def pillow_rgba(png):
    image = Image.open(io.BytesIO(png))
    image.load()
    return image.convert("RGBA").tobytes()


def made(rng, width, height, kind, depth, interlaced, tile=None, **options):
    channels = CHANNELS[kind]
    top = (1 << depth) - 1
    palette = alphas = key = trns = None
    if kind == 3:
        count = min(top + 1, rng.randint(2, 40))
        palette = [tuple(rng.randrange(256) for _ in range(3)) for _ in range(count)]
        alphas = [rng.randrange(256) for _ in range(rng.randint(0, count))]
        trns = bytes(alphas) if alphas else None
        top = count - 1
    samples = [[tuple(rng.randint(0, top) for _ in range(channels)) for _ in range(width)] for _ in range(height)]
    if tile is not None:
        samples = [[samples[y % tile[1]][x % tile[0]] for x in range(width)] for y in range(height)]
    if kind in (0, 2) and rng.random() < 0.6:
        key = samples[rng.randrange(height)][rng.randrange(width)][:3 if kind == 2 else 1]
        trns = b"".join(struct.pack(">H", v) for v in key)
    png = encode(width, height, kind, depth, samples, interlaced, palette, trns, **options)
    rgba = expected(width, height, kind, depth, samples, palette, alphas, key)
    # Pillow takes 16-bit grey as 32-bit integers, not as eight bits, and
    # compares a tRNS key at any depth but 8 with the sample made eight
    # bits, not with the sample as the specification asks.
    pillow_differs = (kind in (0, 4) and depth == 16) or (key is not None and depth != 8)
    if not pillow_differs:
        assert pillow_rgba(png) == rgba, (width, height, kind, depth, interlaced)
    return png, width, height, rgba


def cases():
    rng = random.Random(20261007)
    out = []
    depths = {0: [1, 2, 4, 8, 16], 2: [8, 16], 3: [1, 2, 4, 8], 4: [8, 16], 6: [8, 16]}
    shapes = [(1, 1), (3, 2), (7, 5), (9, 9), (17, 3), (2, 13)]
    strategies = [(0, zlib.Z_DEFAULT_STRATEGY), (9, zlib.Z_FIXED), (9, zlib.Z_DEFAULT_STRATEGY), (6, zlib.Z_RLE)]
    n = 0
    for kind, kinds_depths in depths.items():
        for depth in kinds_depths:
            for interlaced in (False, True):
                for width, height in shapes:
                    level, strategy = strategies[n % len(strategies)]
                    out.append(made(rng, width, height, kind, depth, interlaced, level=level, strategy=strategy, split=[0, 5, 64][n % 3], first_filter=n % 5))
                    n += 1
    # A larger image, its matches reaching back across many rows.
    out.append(made(rng, 64, 48, 6, 8, False, tile=(13, 7), level=9, first_filter=4))
    # Sixteen grey values as often as the Fibonacci numbers, unfiltered and
    # compressed as Huffman codes alone: a code fifteen bits deep, past
    # the decoder's table of short codes.
    values = []
    a, b = 1, 1
    for v in range(16):
        values += [v * 16] * a
        a, b = b, a + b
    values += [0] * (64 * 41 - len(values))
    rng.shuffle(values)
    fib = [[(values[y * 64 + x],) for x in range(64)] for y in range(41)]
    png = encode(64, 41, 0, 8, fib, False, level=9, strategy=zlib.Z_HUFFMAN_ONLY, only_filter=0)
    assert pillow_rgba(png) == expected(64, 41, 0, 8, fib)
    out.append((png, 64, 41, expected(64, 41, 0, 8, fib)))
    # Pillow's encoder, its own filters and compression.
    for mode in ("RGBA", "RGB", "L", "LA", "P"):
        image = Image.new(mode, (23, 11))
        values = [rng.randrange(256) for _ in range(23 * 11 * len(mode if mode != "P" else "L"))]
        image.frombytes(bytes(values))
        buffer = io.BytesIO()
        options = {"optimize": True}
        if mode == "P":
            image.putpalette([rng.randrange(256) for _ in range(768)])
            options["transparency"] = bytes(rng.randrange(256) for _ in range(256))
        image.save(buffer, format="PNG", **options)
        png = buffer.getvalue()
        out.append((png, 23, 11, pillow_rgba(png)))
    return out


def damaged(good):
    """Images to be refused, each made from a good one."""
    png = good[0]
    ihdr_end = 8 + 12 + 13
    out = []

    def rechunk(kind, data):
        return chunk(kind, data)

    # A CRC off by one.
    bad = bytearray(png)
    bad[ihdr_end - 1] ^= 1
    out.append(bytes(bad))
    # Truncated, and without its signature.
    out.append(png[: len(png) // 2])
    out.append(b"\x00" + png[1:])
    # No IEND.
    out.append(png[:-12])
    # A width of 0, and a depth of 3.
    head = struct.unpack(">IIBBBBB", png[16:29])
    for fields in ((0,) + head[1:], head[:2] + (3,) + head[3:]):
        out.append(png[:8] + rechunk(b"IHDR", struct.pack(">IIBBBBB", *fields)) + png[ihdr_end:])
    # An unknown critical chunk, and a compression or filter method not 0.
    out.append(png[:ihdr_end] + rechunk(b"ABCD", b"") + png[ihdr_end:])
    for at in (5, 6):
        fields = list(head)
        fields[at] = 1
        out.append(png[:8] + rechunk(b"IHDR", struct.pack(">IIBBBBB", *fields)) + png[ihdr_end:])
    return out


def grey_with_data(data):
    """A 4 by 2 image of eight-bit grey with this zlib stream."""
    header = struct.pack(">IIBBBBB", 4, 2, 8, 0, 0, 0, 0)
    return SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", data) + chunk(b"IEND", b"")


def main():
    good = cases()
    rng = random.Random(7)
    samples = [[(rng.randint(0, 3),) for _ in range(4)] for _ in range(4)]
    palette = [(255, 0, 0), (0, 255, 0)]
    bad = damaged(good[40])
    # A palette index past the palette, a palette image without its
    # palette, a row of filter 5, and data whose checksum is wrong.
    samples[2][1] = (3,)
    bad.append(encode(4, 4, 3, 2, samples, False, palette=palette))
    bad.append(encode(4, 4, 3, 2, [[(0,)] * 4] * 4, False, palette=None))
    bad.append(grey_with_data(zlib.compress(b"\x00\x01\x02\x03\x04" + b"\x05\x01\x02\x03\x04")))
    stream = bytearray(zlib.compress(b"\x00\x01\x02\x03\x04" * 2))
    stream[-1] ^= 1
    bad.append(grey_with_data(bytes(stream)))
    # Image data split by another chunk, and a palette's tRNS longer than
    # the palette.
    whole = zlib.compress(b"\x00\x01\x02\x03\x04" * 2)
    header = chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 2, 8, 0, 0, 0, 0))
    bad.append(SIGNATURE + header + chunk(b"IDAT", whole[:5]) + chunk(b"tEXt", b"a\x00b") + chunk(b"IDAT", whole[5:]) + chunk(b"IEND", b""))
    two = [[(0,), (1,), (0,), (1,)]] * 4
    bad.append(encode(4, 4, 3, 2, two, False, palette=palette, trns=b"\x10\x20\x30"))
    out = struct.pack("<I", len(good) + len(bad))
    for png, width, height, rgba in good:
        out += struct.pack("<I", len(png)) + png + struct.pack("<II", width, height) + rgba
    for png in bad:
        out += struct.pack("<I", len(png)) + png + struct.pack("<II", 0, 0)
    pathlib.Path(__file__).with_name("pngs.bin").write_bytes(out)
    print(len(good), "good,", len(bad), "bad,", len(out), "bytes")


if __name__ == "__main__":
    main()
