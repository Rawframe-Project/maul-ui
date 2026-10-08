#!/usr/bin/env python3
# Writes the fuzz targets' seeds (test/fuzz_*.c), each into
# corpus-fuzz_<area> under a directory:
# - inflate: zlib streams of stored, fixed and dynamic blocks, each after
#   the two bytes of its inflated size, as fuzz_inflate reads them;
# - png: PNG images of every colour type and bit depth, a palette with
#   tRNS, Adam7 interlacing and the five filters;
# - font: the test fonts, TrueType, CFF, variable, COLR, CBDT and sbix;
# - text: texts in several scripts and directions, with marks, emoji,
#   line and paragraph breaks and ill-formed UTF-8, each after the four
#   bytes fuzz_text reads first and before edits of every kind;
# - atspi: D-Bus method calls, marshalled as on the wire, of every method
#   the adapter answers, on the root and each node of fuzz_atspi's tree.
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


TEXTS = [
    "Hello, world",
    "fi\ufb01 e\u0301 Ha\u0308ngen \u0645\u0631\u062d\u0628\u0627 123",
    "\u0928\u092e\u0938\u094d\u0924\u0947 \u6f22\u5b57\u304b\u306a \U0001f44d\U0001f3fd \U0001f468\u200d\U0001f469\u200d\U0001f467",
    "one\ntwo\r\nthree\u2028four\u2029five\u0085six",
    "\u05e9\u05dc\u05d5\u05dd abc \u202edef\u202c ghi",
    "a\tb  c   " + "word " * 60,
]


def edits():
    """One edit of each kind fuzz_text makes, as its three bytes and text."""
    out = b""
    out += bytes([0, 5, 0]) + b"xy\xc3\xa9z"    # type
    out += bytes([1, 4, 0]) + b"\xe2\x82\xac!!"  # paste
    out += bytes([2, 0, 0])                        # erase backward
    out += bytes([3, 2, 9])                        # select
    out += bytes([4, 6, 1])                        # move, extending
    out += bytes([5, 30, 10])                      # press
    out += bytes([6, 90, 12])                      # drag
    out += bytes([7, 4, 2]) + b"\xe3\x81\x8b\x61"  # compose
    out += bytes([8, 0, 0])                        # end composition
    out += bytes([9, 1, 0])                        # undo
    out += bytes([10, 1, 3]) + b"ab"               # replace
    out += bytes([11, 1, 16])                      # editing rules
    return out


def text_seeds(directory):
    samples = [t.encode("utf-8") for t in TEXTS] + [b"ok\xff\xc0\x80 \xed\xa0\x80 \xf4\x90\x80\x80"]
    for i, text in enumerate(samples):
        for flags in (0, 0x09, 0x36):
            head = bytes([30 + 20 * (i % 3), flags]) + struct.pack("<H", len(text))
            write(directory, f"text{i}-{flags:02x}", head + text + edits())


class Wire:
    """A D-Bus message being marshalled, little endian."""

    def __init__(self):
        self.data = bytearray()

    def align(self, n):
        self.data += bytes(-len(self.data) % n)

    def byte(self, v):
        self.data.append(v)

    def u32(self, v):
        self.align(4)
        self.data += struct.pack("<I", v)

    def i32(self, v):
        self.align(4)
        self.data += struct.pack("<i", v)

    def double(self, v):
        self.align(8)
        self.data += struct.pack("<d", v)

    def string(self, v):
        b = v.encode()
        self.u32(len(b))
        self.data += b + b"\0"

    def signature(self, v):
        b = v.encode()
        self.byte(len(b))
        self.data += b + b"\0"

    def value(self, kind, v):
        if kind in "so":
            self.string(v)
        elif kind == "g":
            self.signature(v)
        elif kind == "i":
            self.i32(v)
        elif kind == "u":
            self.u32(v)
        elif kind == "d":
            self.double(v)
        elif kind == "v":
            inner, value = v
            self.signature(inner)
            self.value(inner, value)


def method_call(path, interface, member, signature="", args=()):
    body = Wire()
    for kind, value in zip(signature, args):
        body.value(kind, value)
    fields = Wire()
    # Header fields start at offset 16 of the message: alignment holds.
    fields.data = bytearray(16)
    for code, kind, value in ((1, "o", path), (2, "s", interface), (3, "s", member),
                              (6, "s", ":1.7"), (8, "g", signature)):
        if kind == "g" and not value:
            continue
        fields.align(8)
        fields.byte(code)
        fields.signature(kind)
        fields.value(kind, value)
    array = bytes(fields.data[16:])
    head = Wire()
    head.data += b"l" + bytes([1, 0, 1])
    head.u32(len(body.data))
    head.u32(1)
    head.u32(len(array))
    head.data += array
    head.align(8)
    return bytes(head.data) + bytes(body.data)


ACCESSIBLE = "org.a11y.atspi.Accessible"
CALLS = [
    (ACCESSIBLE, m, "", ()) for m in ("GetRole", "GetRoleName", "GetLocalizedRoleName", "GetState",
                                      "GetChildren", "GetIndexInParent", "GetRelationSet",
                                      "GetAttributes", "GetInterfaces", "GetApplication")
] + [
    (ACCESSIBLE, "GetChildAtIndex", "i", (1,)),
    ("org.freedesktop.DBus.Properties", "Get", "ss", (ACCESSIBLE, "Name")),
    ("org.freedesktop.DBus.Properties", "Get", "ss", (ACCESSIBLE, "ChildCount")),
    ("org.freedesktop.DBus.Properties", "Get", "ss", ("org.a11y.atspi.Value", "CurrentValue")),
    ("org.freedesktop.DBus.Properties", "GetAll", "s", ("org.a11y.atspi.Value",)),
    ("org.freedesktop.DBus.Properties", "Set", "ssv",
     ("org.a11y.atspi.Value", "CurrentValue", ("d", 50.0))),
    ("org.a11y.atspi.Component", "Contains", "iiu", (20, 40, 0)),
    ("org.a11y.atspi.Component", "GetAccessibleAtPoint", "iiu", (20, 40, 1)),
    ("org.a11y.atspi.Component", "GetExtents", "u", (0,)),
    ("org.a11y.atspi.Component", "GetPosition", "u", (1,)),
    ("org.a11y.atspi.Component", "ScrollTo", "u", (0,)),
] + [("org.a11y.atspi.Component", m, "", ()) for m in ("GetSize", "GetLayer", "GetMDIZOrder",
                                                        "GrabFocus", "GetAlpha")] + [
    ("org.a11y.atspi.Action", "GetActions", "", ()),
] + [("org.a11y.atspi.Action", m, "i", (0,)) for m in ("DoAction", "GetName", "GetDescription",
                                                       "GetKeyBinding", "GetLocalizedName")] + [
    ("org.freedesktop.DBus.Introspectable", "Introspect", "", ()),
]


def atspi_seeds(directory):
    paths = ["root"] + [f"w1n{i}" for i in range(1, 8)]
    for p in paths:
        for i, (interface, member, signature, args) in enumerate(CALLS):
            data = method_call("/org/a11y/atspi/accessible/" + p, interface, member, signature, args)
            write(directory, f"{p}-{i:02d}-{member}", data)


def main():
    base = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path.cwd()
    inflate_seeds(base / "corpus-fuzz_inflate")
    png_seeds(base / "corpus-fuzz_png")
    font_seeds(base / "corpus-fuzz_font")
    text_seeds(base / "corpus-fuzz_text")
    atspi_seeds(base / "corpus-fuzz_atspi")


if __name__ == "__main__":
    main()
