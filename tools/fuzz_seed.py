#!/usr/bin/env python3
# Writes the fuzz targets' seeds (test/fuzz_*.c), each into
# corpus-fuzz_<area> under a directory:
# - inflate: zlib streams of stored, fixed and dynamic blocks, each after
#   the two bytes of its inflated size, as fuzz_inflate reads them;
# - png: PNG images of every colour type and bit depth, a palette with
#   tRNS, Adam7 interlacing and the five filters;
# - font: the test fonts, TrueType, CFF, variable, COLR, CBDT and sbix,
#   and a collection of four of them, faces of one family;
# - text: texts in several scripts and directions, with marks, emoji,
#   line and paragraph breaks and ill-formed UTF-8, each after the six
#   bytes fuzz_text reads first (an allocation to fail among them) and
#   before edits of every kind;
# - atspi: D-Bus method calls, marshalled as on the wire, of every method
#   the adapter answers, on the root and each node of fuzz_atspi's tree,
#   and the Text and EditableText ones on its two nodes with text;
# - api: sequences of public calls in fuzz_api's encoding, trees made,
#   styled and moved, then laid out and compared with a solve from
#   nothing, and one for each further family of calls (destroying,
#   content changed, scrolling, hit tests, classes, states and
#   gradients).
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


def collection(fonts):
    """A TrueType collection of whole fonts, each table offset moved to
    count from the collection's start."""
    header = 12 + 4 * len(fonts)
    bases = []
    body = bytearray()
    for font in fonts:
        body.extend(b"\0" * (-(header + len(body)) % 4))
        base = header + len(body)
        bases.append(base)
        moved = bytearray(font)
        for i in range(struct.unpack(">H", font[4:6])[0]):
            at = 12 + 16 * i + 8
            offset = struct.unpack(">I", font[at:at + 4])[0]
            moved[at:at + 4] = struct.pack(">I", base + offset)
        body.extend(moved)
    return (b"ttcf" + struct.pack(">II", 0x00010000, len(fonts)) +
            b"".join(struct.pack(">I", base) for base in bases) + bytes(body))


def font_seeds(directory):
    for font in sorted((ROOT / "test" / "fonts").iterdir()):
        if font.suffix in (".ttf", ".otf"):
            directory.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(font, directory / font.name)
    # Faces of one family: an italic, and variable faces with weight,
    # slant and italic axes, for fuzz_font to match among.
    faces = ["MaulVariable.ttf", "MaulItalic.ttf", "MaulVariableSlant.ttf",
             "MaulVariableItalic.ttf"]
    write(directory, "family.ttc",
          collection([(ROOT / "test" / "fonts" / face).read_bytes() for face in faces]))


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
            # Every third seed fails an allocation, early or late.
            fail = (0, 40, 400)[(i + flags) % 3]
            head = bytes([30 + 20 * (i % 3), flags]) + struct.pack("<HH", len(text), fail)
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
        elif kind in "ub":
            # A boolean is a 32-bit 0 or 1 on the wire.
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


TEXT = "org.a11y.atspi.Text"
EDITABLE = "org.a11y.atspi.EditableText"
# The Text and EditableText calls, for the nodes that have text: offsets
# inside it, and boundaries of each kind.
TEXT_CALLS = [
    (TEXT, "GetText", "ii", (1, 9)),
    (TEXT, "GetCharacterAtOffset", "i", (2,)),
] + [(TEXT, m, "iu", (8, boundary)) for m in ("GetStringAtOffset", "GetTextAtOffset",
                                              "GetTextBeforeOffset", "GetTextAfterOffset")
     for boundary in (0, 2, 4)] + [
    (TEXT, "SetCaretOffset", "i", (3,)),
    (TEXT, "GetNSelections", "", ()),
    (TEXT, "GetSelection", "i", (0,)),
    (TEXT, "AddSelection", "ii", (2, 5)),
    (TEXT, "RemoveSelection", "i", (0,)),
    (TEXT, "SetSelection", "iii", (0, 1, 12)),
    (TEXT, "GetCharacterExtents", "iu", (4, 0)),
    (TEXT, "GetRangeExtents", "iiu", (2, 13, 1)),
    (TEXT, "GetOffsetAtPoint", "iiu", (30, 45, 1)),
    (TEXT, "GetAttributeRun", "ib", (3, 1)),
    (TEXT, "GetDefaultAttributeSet", "", ()),
    (TEXT, "GetAttributeValue", "is", (3, "weight")),
    (TEXT, "ScrollSubstringTo", "iiu", (0, 4, 0)),
    ("org.freedesktop.DBus.Properties", "Get", "ss", (TEXT, "CharacterCount")),
    ("org.freedesktop.DBus.Properties", "Get", "ss", (TEXT, "CaretOffset")),
    (EDITABLE, "SetTextContents", "s", ("na\u00efve",)),
    (EDITABLE, "InsertText", "isi", (2, "x\u20ac", 4)),
    (EDITABLE, "DeleteText", "ii", (1, 4)),
    (EDITABLE, "CutText", "ii", (0, 3)),
    (EDITABLE, "PasteText", "i", (2,)),
]


def atspi_seeds(directory):
    paths = ["root"] + [f"w1n{i}" for i in range(1, 8)]
    for p in paths:
        for i, (interface, member, signature, args) in enumerate(CALLS):
            data = method_call("/org/a11y/atspi/accessible/" + p, interface, member, signature, args)
            write(directory, f"{p}-{i:02d}-{member}", data)
    # The label's value and the text input's.
    for p in ("w1n3", "w1n6"):
        for i, (interface, member, signature, args) in enumerate(TEXT_CALLS):
            data = method_call("/org/a11y/atspi/accessible/" + p, interface, member, signature, args)
            write(directory, f"{p}-text{i:02d}-{member}", data)


def pick(index, count):
    """A byte fuzz_api reads as the id at index among count it holds."""
    return index + count * ((16 + count - 1) // count)


# fuzz_api's call kinds, the first byte of each call.
API_CREATE, API_DESTROY, API_INSERT, API_DETACH, API_VALUES = 0, 1, 2, 3, 4
API_CONTENT, API_LAYOUT, API_SCROLL, API_HIT, API_CLASS, API_STATES = 6, 7, 10, 13, 14, 15
API_VISUAL = 16
# Byte offsets in muiLayoutStyle and muiVisualStyle on the 64-bit targets
# fuzzing runs on, and property bits of their masks: a seed that drifts
# from them still runs, it only writes another value.
WIDTH, HEIGHT, MIN_WIDTH, PADDING_START, CONTENT, SCROLL_AXES = 4, 16, 28, 152, 229, 230
BIT_WIDTH, BIT_HEIGHT, BIT_MIN_WIDTH, BIT_PADDING_START = 0, 1, 2, 27
BIT_CONTENT, BIT_SCROLL_AXES = 39, 40
BACKGROUND_BLUE, CLIP, BIT_BACKGROUND, BIT_CLIP = 8, 328, 0, 16
GRADIENT, BIT_GRADIENT = 16, 1
STATE_HOVERED, VARIANT_HOVERED = 16, 5


def api_number(value):
    """A byte fuzz_api reads as the small length value."""
    assert value % 4 == 0 and -16 <= value <= 236
    return (value + 16) // 4


def api_values(node, at, data, bit, mask=None):
    """A raw write of data at byte at of a node's layout style, under a
    mask of one property or the mask given."""
    mask = 1 << bit if mask is None else mask
    return bytes([API_VALUES, node, at, len(data) - 1]) + data + struct.pack(">Q", mask)


def api_visual(node, at, data, bit):
    """A raw write of data at byte at of a node's visual style, one property in the mask."""
    return bytes([API_VISUAL, node, at >> 8, at & 0xFF, len(data) - 1]) + data + \
        struct.pack(">Q", 1 << bit)


def api_length(node, at, value, bit):
    """A dimension given value, written from its offset field at byte at."""
    return api_values(node, at, struct.pack("<fB3x", value, 1), bit)


def api_compare(root):
    """A layout of root at 200 by 160, held to a solve from nothing."""
    return bytes([API_LAYOUT, root, 1, api_number(200), api_number(160), 255, 0])


def api_tree(count, parents, root_width=200, root_height=160):
    """count nodes, node i under parents[i - 1], each painted blue, the
    first sized, the others measured by the host and given a height."""
    out = bytearray()
    for i in range(count):
        out += bytes([API_CREATE, 10 + 7 * i, 255])
    for i, parent in enumerate(parents, 1):
        out += bytes([API_INSERT, pick(parent, count), pick(i, count), 0])
    out += api_length(pick(0, count), WIDTH, root_width, BIT_WIDTH)
    out += api_length(pick(0, count), HEIGHT, root_height, BIT_HEIGHT)
    for i in range(1, count):
        out += api_values(pick(i, count), CONTENT, b"\x01", BIT_CONTENT)
        out += api_length(pick(i, count), HEIGHT, 10 + i, BIT_HEIGHT)
    for i in range(count):
        out += api_visual(pick(i, count), BACKGROUND_BLUE, struct.pack("<ff", 1, 1),
                          BIT_BACKGROUND)
    return out


def api_class(count, variant, node):
    """A class with a padding in variant, listed on node."""
    return bytes([API_CLASS, 0, API_CLASS, 1, 16, variant, PADDING_START + 3, 0x42]) + \
        struct.pack(">Q", 1 << BIT_PADDING_START) + bytes([API_CLASS, 2, pick(node, count), 1, 16])


def api_seeds(directory):
    for shape in range(4):
        out = bytearray()
        count = 0
        for i in range(10):
            out += bytes([API_CREATE, i % 3, 255])
            count += 1
            if i > 0:
                parent = 0 if shape == 0 else (i - 1 if shape == 1 else (i - 1) // 2)
                out += bytes([API_INSERT, pick(parent, count), pick(i, count), 0])
            # A raw write of a few bytes of the layout style, any mask.
            out += api_values(pick(i, count), (i * 13) % 200, bytes([i, 2, 0, 1, 0]), 0,
                              0xFFFFFFFF)
        out += bytes([API_LAYOUT, pick(0, count), 1, 120, 80, 255, 0])
        out += bytes([9, pick(0, count), 1, 4])
        out += bytes([11, pick(0, count), 1, 0, 0, 1, 0, 1, 40, 40, 0])
        out += bytes([12, pick(0, count), 0, 0])
        out += bytes([API_DETACH, pick(count // 2, count)]) + api_compare(pick(0, count))
        write(directory, f"tree-{shape}", bytes(out))
    # One seed for each family of calls the trees leave out, each between
    # two solves held to a solve from nothing.
    count = 10
    binary = [(i - 1) // 2 for i in range(1, count)]
    root = pick(0, count)
    out = api_tree(count, binary) + api_compare(root)
    out += bytes([API_DESTROY, pick(2, count)]) + api_compare(root)
    write(directory, "destroy", bytes(out))
    out = api_tree(count, binary) + api_compare(root) + bytes([API_CONTENT, pick(4, count)]) + api_compare(root)
    write(directory, "content", bytes(out))
    # A row scrolled sideways: a clipping container 100 wide under the
    # root, its children 60 wide at least.
    out = api_tree(count, [0] + [1] * (count - 2))
    out += api_length(pick(1, count), WIDTH, 100, BIT_WIDTH)
    out += api_visual(pick(1, count), CLIP, b"\x01", BIT_CLIP)
    out += api_values(pick(1, count), SCROLL_AXES, b"\x01", BIT_SCROLL_AXES)
    for i in range(2, count):
        out += api_length(pick(i, count), MIN_WIDTH, 60, BIT_MIN_WIDTH)
    out += api_compare(root)
    out += bytes([API_SCROLL, pick(1, count), api_number(48), api_number(0)]) + api_compare(root)
    write(directory, "scroll", bytes(out))
    out = api_tree(count, binary) + api_compare(root)
    out += bytes([API_HIT, root, api_number(40), api_number(12)]) + api_compare(root)
    write(directory, "hit", bytes(out))
    out = api_tree(count, binary) + api_class(count, 0, 0) + api_compare(root)
    write(directory, "class", bytes(out))
    # Linear gradients of two stops at three angles; one node resized
    # after, so the bounded build copies the others' entries.
    out = api_tree(count, binary)
    for i in range(1, 4):
        out += api_visual(pick(i, count), GRADIENT, struct.pack("<BBxxf", 1, 2, 30 * i),
                          BIT_GRADIENT)
    out += api_compare(root) + api_length(pick(1, count), HEIGHT, 40, BIT_HEIGHT)
    out += api_compare(root)
    write(directory, "gradient", bytes(out))
    out = api_tree(count, binary) + api_class(count, VARIANT_HOVERED, 0) + api_compare(root)
    out += bytes([API_STATES, root, STATE_HOVERED]) + api_compare(root)
    write(directory, "states", bytes(out))


def main():
    base = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path.cwd()
    inflate_seeds(base / "corpus-fuzz_inflate")
    png_seeds(base / "corpus-fuzz_png")
    font_seeds(base / "corpus-fuzz_font")
    text_seeds(base / "corpus-fuzz_text")
    atspi_seeds(base / "corpus-fuzz_atspi")
    api_seeds(base / "corpus-fuzz_api")


if __name__ == "__main__":
    main()
