#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The layout fixture corpus (record mui-0003): reads test/layout/*.txt and
# writes test/generated/layout_fixtures.h, the C tables test_layout_fixtures
# runs. With --check it only reports whether that file is current, as CI
# does. With --oracle it renders every fixture in Chrome through
# tools/layout_oracle.mjs (Node and puppeteer, a development tool) and
# writes Chrome's rectangles back into the corpus.
#
# A corpus file holds fixtures:
#
#   fixture <name> available=<width>x<height>
#   node <key>=<value> ... => <x> <y> <width> <height>
#     node ...                 (two spaces of indentation per depth)
#
# Lines starting with # are comments; "# chrome <version>" records the
# browser that produced the expectations. Keys: width height min-width
# min-height max-width max-height basis (auto, <offset>, <scale>%,
# <scale>%+<offset>, <scale>%-<offset>), grow shrink row-gap column-gap
# (numbers), direction (row row-reverse column column-reverse), justify
# (start end center space-between space-around space-evenly), wrap
# (nowrap wrap wrap-reverse), align-content (stretch start end center
# space-between space-around space-evenly),
# align-items and align-self (auto stretch start end center baseline), margin
# border padding (one value, or start,end,top,bottom; margins may be
# auto), position (flow absolute), start end top bottom (insets, as
# dimensions), anchor (<x>,<y> from 0 to 1), dir (inherit ltr rtl),
# content (<width>x<height>:
# host content of that size).
#
# usage: gen_layout_fixtures.py [--check | --oracle]

import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS = os.path.join(ROOT, "test", "layout")
OUTPUT = os.path.join(ROOT, "test", "generated", "layout_fixtures.h")
ORACLE = os.path.join(ROOT, "tools", "layout_oracle.mjs")

DIMENSIONS = ("width", "height", "min-width", "min-height", "max-width", "max-height", "basis")
INSETS = ("start", "end", "top", "bottom")
NUMBERS = ("grow", "shrink", "row-gap", "column-gap", "aspect")
EDGES = ("margin", "border", "padding")
ENUMS = {
    "direction": ("row", "row-reverse", "column", "column-reverse"),
    "wrap": ("nowrap", "wrap", "wrap-reverse"),
    "dir": ("inherit", "ltr", "rtl"),
    "align-content": ("stretch", "start", "end", "center", "space-between", "space-around",
                      "space-evenly"),
    "justify": ("start", "end", "center", "space-between", "space-around", "space-evenly"),
    "align-items": ("auto", "stretch", "start", "end", "center", "baseline"),
    "align-self": ("auto", "stretch", "start", "end", "center", "baseline"),
}
DEFAULTS = {"direction": "row", "wrap": "nowrap", "dir": "inherit", "align-content": "stretch", "justify": "start", "align-items": "stretch",
            "align-self": "auto", "grow": "0", "shrink": "1", "row-gap": "0",
            "column-gap": "0", "aspect": "0"}
DIMENSION = re.compile(r"^(?:(-?[0-9.]+)%)?([+-]?[0-9.]+)?$")


class CorpusError(Exception):
    pass


def parse_dimension(text):
    """(kind, scale, offset) of a dimension."""
    if text == "auto":
        return (0, 0.0, 0.0)
    match = DIMENSION.match(text)
    if not match or (match.group(1) is None and match.group(2) is None):
        raise CorpusError(f"bad dimension {text!r}")
    scale = float(match.group(1)) / 100.0 if match.group(1) else 0.0
    offset = float(match.group(2)) if match.group(2) else 0.0
    return (1, scale, offset)


def parse_edges(text, auto_allowed=False):
    """The four values, start, end, top, bottom, with "auto" as None."""
    values = text.split(",")
    if len(values) == 1:
        values = values * 4
    if len(values) != 4:
        raise CorpusError(f"edges take 1 or 4 values: {text!r}")
    if not auto_allowed and "auto" in values:
        raise CorpusError(f"only margins can be auto: {text!r}")
    return [None if v == "auto" else float(v) for v in values]


def parse_anchor(text):
    parts = [float(v) for v in text.split(",")]
    if len(parts) != 2 or not all(0.0 <= v <= 1.0 for v in parts):
        raise CorpusError(f"anchor is x,y between 0 and 1: {text!r}")
    return parts


def parse_node(text, number):
    props = {}
    for item in text.split():
        if "=" not in item:
            raise CorpusError(f"line {number}: expected key=value, got {item!r}")
        key, value = item.split("=", 1)
        if key in DIMENSIONS or key in INSETS:
            parse_dimension(value)
        elif key == "position":
            if value not in ("flow", "absolute"):
                raise CorpusError(f"line {number}: position is flow or absolute")
        elif key == "anchor":
            parse_anchor(value)
        elif key in NUMBERS:
            float(value)
        elif key in EDGES:
            parse_edges(value, key == "margin")
        elif key in ENUMS:
            if value not in ENUMS[key]:
                raise CorpusError(f"line {number}: {key} cannot be {value!r}")
        elif key == "content":
            if not re.match(r"^[0-9.]+x[0-9.]+$", value):
                raise CorpusError(f"line {number}: content is <width>x<height>")
        else:
            raise CorpusError(f"line {number}: unknown key {key!r}")
        props[key] = value
    return props


def parse_file(path):
    """The file's fixtures: dicts with name, available, nodes, and the
    line numbers of their node lines."""
    fixtures = []
    lines = open(path, encoding="utf-8").read().split("\n")
    for number, line in enumerate(lines, 1):
        if not line.strip() or line.startswith("#"):
            continue
        if line.startswith("fixture "):
            parts = line.split()
            match = re.match(r"^available=([0-9.]+)x([0-9.]+)$", parts[2] if len(parts) > 2 else "")
            if len(parts) != 3 or not match:
                raise CorpusError(f"{path}:{number}: fixture <name> available=<w>x<h>")
            fixtures.append({"name": parts[1], "available": (float(match.group(1)),
                             float(match.group(2))), "nodes": []})
            continue
        stripped = line.lstrip(" ")
        indent = len(line) - len(stripped)
        if not fixtures or not stripped.startswith("node") or indent % 2:
            raise CorpusError(f"{path}:{number}: expected a node line")
        body, _, expect = stripped[4:].partition("=>")
        nodes = fixtures[-1]["nodes"]
        depth = indent // 2
        if (not nodes and depth != 0) or (nodes and (depth == 0 or depth > nodes[-1]["depth"] + 1)):
            raise CorpusError(f"{path}:{number}: one root per fixture, children one level deeper")
        expected = [float(v) for v in expect.split()] if expect.strip() else None
        if expected is not None and len(expected) != 4:
            raise CorpusError(f"{path}:{number}: => x y width height")
        nodes.append({"depth": depth, "props": parse_node(body, number), "expected": expected,
                      "line": number - 1})
    return lines, fixtures


def corpus():
    for path in sorted(glob.glob(os.path.join(CORPUS, "*.txt"))):
        yield path, parse_file(path)


def c_float(value):
    text = repr(float(value))
    return text + "f" if "e" in text or "." in text else text + ".0f"


def c_dimension(text):
    kind, scale, offset = parse_dimension(text)
    return f"{{{c_float(scale)}, {c_float(offset)}, {kind}}}"


def c_edges(text):
    return "{" + ", ".join(c_float(v or 0.0) for v in parse_edges(text, True)) + "}"


def c_auto_mask(text):
    bits = (1, 2, 4, 8)
    return sum(bit for bit, v in zip(bits, parse_edges(text, True)) if v is None)


def c_style(props):
    def get(key, default="auto"):
        return props.get(key, DEFAULTS.get(key, default))
    sizing = ", ".join(c_dimension(get(k)) for k in DIMENSIONS[:6]) + f", {c_float(get('aspect'))}"
    container = (f"{{{ENUMS['direction'].index(get('direction'))}, "
                 f"{ENUMS['wrap'].index(get('wrap'))}, "
                 f"{ENUMS['justify'].index(get('justify'))}, "
                 f"{ENUMS['align-items'].index(get('align-items'))}, "
                 f"{ENUMS['align-content'].index(get('align-content'))}, "
                 f"{c_float(get('row-gap'))}, {c_float(get('column-gap'))}}}")
    item = (f"{{{c_float(get('grow'))}, {c_float(get('shrink'))}, {c_dimension(get('basis'))}, "
            f"{ENUMS['align-self'].index(get('align-self'))}}}")
    margin = get("margin", "0")
    edges = (f"{c_edges(margin)}, {c_auto_mask(margin)}, {c_edges(get('border', '0'))}, "
             f"{c_edges(get('padding', '0'))}")
    position = 1 if get("position", "flow") == "absolute" else 0
    insets = ", ".join(c_dimension(get(k)) for k in INSETS)
    anchor = parse_anchor(get("anchor", "0,0"))
    placement = f"{{{position}, {{{insets}}}, {c_float(anchor[0])}, {c_float(anchor[1])}}}"
    direction = ENUMS["dir"].index(get("dir"))
    content = 1 if "content" in props else 0
    return (f"{{{{{sizing}}}, {container}, {item}, {edges}, {placement}, {direction}, "
            f"{content}}}")


def generate():
    out = ["// SPDX-License-Identifier: MIT",
           "// Copyright (c) 2026 Sirac Ozmen",
           "//",
           "// The layout fixture corpus as C tables. Generated by",
           "// tools/gen_layout_fixtures.py from test/layout/*.txt; do not edit.",
           "// clang-format off",
           "",
           "#ifndef MAUL_UI_TEST_GENERATED_LAYOUT_FIXTURES_H",
           "#define MAUL_UI_TEST_GENERATED_LAYOUT_FIXTURES_H",
           "",
           '#include "../layout_fixture.h"',
           ""]
    table = []
    for path, (_, fixtures) in corpus():
        for fixture in fixtures:
            name = fixture["name"]
            if any(node["expected"] is None for node in fixture["nodes"]):
                raise CorpusError(f"{name}: run --oracle to fill in the expectations")
            out.append(f"static const LayoutFixtureNode s_{name}[] = {{")
            for node in fixture["nodes"]:
                content = node["props"].get("content", "0x0").split("x")
                expected = ", ".join(c_float(v) for v in node["expected"])
                out.append(f"    {{{node['depth']}, {c_style(node['props'])},")
                out.append(f"     {{{c_float(content[0])}, {c_float(content[1])}}}, {{{expected}}}}},")
            out.append("};")
            out.append("")
            width, height = fixture["available"]
            table.append(f"    {{\"{name}\", {c_float(width)}, {c_float(height)}, s_{name}, "
                         f"{len(fixture['nodes'])}}},")
    out.append("static const LayoutFixture s_layoutFixtures[] = {")
    out.extend(table)
    out.append("};")
    out.append("")
    out.append("#endif // MAUL_UI_TEST_GENERATED_LAYOUT_FIXTURES_H")
    return "\n".join(out) + "\n"


def css_dimension(text, auto="auto"):
    kind, scale, offset = parse_dimension(text)
    if kind == 0:
        return auto
    # A plain length when the scale is 0: CSS treats any percentage, even
    # 0% inside calc(), as automatic against an indefinite extent.
    if scale == 0.0:
        return f"{offset!r}px"
    return f"calc({scale * 100.0!r}% + {offset!r}px)"


def css_edges(prefix, text, suffix=""):
    # Start and end are logical, as Maul UI's are.
    sides = ("inline-start", "inline-end", "top", "bottom")
    values = parse_edges(text, True)
    return "".join(f"{prefix}-{side}{suffix}:{'auto' if v is None else repr(v) + 'px'};"
                   for side, v in zip(sides, values))


ALIGN_CSS = {"auto": "auto", "stretch": "stretch", "start": "flex-start", "end": "flex-end",
             "center": "center", "baseline": "baseline"}
CONTENT_CSS = {"stretch": "stretch", "start": "flex-start", "end": "flex-end", "center": "center",
               "space-between": "space-between", "space-around": "space-around",
               "space-evenly": "space-evenly"}
JUSTIFY_CSS = {"start": "flex-start", "end": "flex-end", "center": "center",
               "space-between": "space-between", "space-around": "space-around",
               "space-evenly": "space-evenly"}


def css_style(props, rtl):
    get = lambda key, default="auto": props.get(key, DEFAULTS.get(key, default))
    css = [f"width:{css_dimension(get('width'))};", f"height:{css_dimension(get('height'))};",
           f"min-width:{css_dimension(get('min-width'))};",
           f"min-height:{css_dimension(get('min-height'))};",
           f"max-width:{css_dimension(get('max-width'), 'none')};",
           f"max-height:{css_dimension(get('max-height'), 'none')};",
           f"flex-basis:{css_dimension(get('basis'))};",
           f"flex-grow:{get('grow')};flex-shrink:{get('shrink')};",
           f"aspect-ratio:{get('aspect') if float(get('aspect')) > 0 else 'auto'};",
           f"flex-direction:{get('direction')};flex-wrap:{get('wrap')};",
           f"align-content:{CONTENT_CSS[get('align-content')]};",
           f"justify-content:{JUSTIFY_CSS[get('justify')]};",
           f"align-items:{ALIGN_CSS[get('align-items')]};",
           f"align-self:{ALIGN_CSS[get('align-self')]};",
           f"row-gap:{get('row-gap')}px;column-gap:{get('column-gap')}px;",
           css_edges("margin", get("margin", "0")), css_edges("padding", get("padding", "0")),
           css_edges("border", get("border", "0"), "-width")]
    if get("position", "flow") == "absolute":
        css.append("position:absolute;")
        for key, side in zip(INSETS, ("inset-inline-start", "inset-inline-end", "top", "bottom")):
            css.append(f"{side}:{css_dimension(get(key))};")
        x, y = parse_anchor(get("anchor", "0,0"))
        # The x fraction is measured from the inline start.
        x = x if rtl else -x
        css.append(f"transform:translate({x * 100.0!r}%,{-y * 100.0!r}%);")
    if get("dir") != "inherit":
        css.append(f"direction:{get('dir')};")
    return "".join(css)


def fixture_html(fixture):
    """The fixture's root as HTML; every node's element carries data-i."""
    parts = []
    stack = []
    # The resolved direction at each depth, right to left when true.
    rtl = [False]
    for index, node in enumerate(fixture["nodes"]):
        while stack and stack[-1] >= node["depth"]:
            parts.append("</div>")
            stack.pop()
        inherited = rtl[node["depth"]]
        own = node["props"].get("dir", "inherit")
        resolved = inherited if own == "inherit" else own == "rtl"
        rtl[node["depth"] + 1:] = [resolved]
        content = ""
        if "content" in node["props"]:
            w, h = node["props"]["content"].split("x")
            content = f'<div data-content style="width:{w}px;height:{h}px;flex:none"></div>'
        parts.append(f'<div data-i="{index}" style="{css_style(node["props"], inherited)}">'
                     f'{content}')
        stack.append(node["depth"])
    parts.extend("</div>" for _ in stack)
    return "".join(parts)


def run_oracle():
    pages = []
    files = list(corpus())
    for path, (_, fixtures) in files:
        for fixture in fixtures:
            pages.append({"name": fixture["name"], "width": fixture["available"][0],
                          "height": fixture["available"][1], "html": fixture_html(fixture)})
    result = subprocess.run(["node", ORACLE], input=json.dumps(pages), capture_output=True,
                            text=True, cwd=os.path.dirname(ORACLE))
    if result.returncode != 0:
        raise CorpusError("the oracle failed:\n" + result.stderr)
    measured = json.loads(result.stdout)
    for path, (lines, fixtures) in files:
        chrome = [i for i, line in enumerate(lines) if line.startswith("# chrome ")]
        if len(chrome) != 1:
            raise CorpusError(f"{path}: needs exactly one '# chrome' line")
        lines[chrome[0]] = f"# chrome {measured['version']}"
        for fixture in fixtures:
            rects = measured["fixtures"][fixture["name"]]
            for node, rect in zip(fixture["nodes"], rects):
                body = lines[node["line"]].split("=>")[0].rstrip()
                lines[node["line"]] = body + " => " + " ".join(format_number(v) for v in rect)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))


def format_number(value):
    text = repr(float(value))
    return text[:-2] if text.endswith(".0") else text


def main():
    args = sys.argv[1:]
    try:
        if args == ["--oracle"]:
            run_oracle()
        text = generate()
    except CorpusError as error:
        sys.exit(f"layout fixtures: {error}")
    if args == ["--check"]:
        current = open(OUTPUT, encoding="utf-8").read() if os.path.exists(OUTPUT) else None
        if current != text:
            sys.exit("layout fixtures: test/generated/layout_fixtures.h is out of date")
        print("layout fixtures: current")
        return
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8") as f:
        f.write(text)


if __name__ == "__main__":
    main()
