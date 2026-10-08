#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# tools/text-headers.txt lists the text component's public headers
# (record mui-0006), which the size report counts as text and an install
# without the component leaves out. It must name exactly the public
# headers declaring a function that a source of cmake/Text.cmake defines.
#
# usage: check_text_headers.py

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECLARATION = re.compile(r"\bMUI_API\b[^;{]*?\b(mui[A-Za-z0-9_]+)\s*\(", re.S)
# A definition's first line: not indented, not static, not a prototype.
DEFINITION = re.compile(r"^(?!static\b)[A-Za-z][^;]*?\b(mui[A-Za-z0-9_]+)\s*\([^;]*$")


def listed():
    path = os.path.join(ROOT, "tools", "text-headers.txt")
    if not os.path.exists(path):
        sys.exit("tools/text-headers.txt is missing")
    lines = (line.split("#", 1)[0].strip() for line in open(path, encoding="utf-8"))
    return {line for line in lines if line}


def text_sources():
    """The src/ files cmake/Text.cmake builds."""
    text = open(os.path.join(ROOT, "cmake", "Text.cmake"), encoding="utf-8").read()
    return set(re.findall(r"\bsrc/([a-z0-9_]+\.c)\b", text))


def text_headers():
    """The public headers declaring a function the text sources define."""
    defined = set()
    for name in text_sources():
        for line in open(os.path.join(ROOT, "src", name), encoding="utf-8", errors="replace"):
            match = DEFINITION.match(line)
            if match:
                defined.add(match.group(1))
    headers = os.path.join(ROOT, "include", "maul-ui")
    found = set()
    for name in sorted(os.listdir(headers)):
        text = open(os.path.join(headers, name), encoding="utf-8").read()
        if defined.intersection(DECLARATION.findall(text)):
            found.add(name)
    return found


def main():
    found = text_headers()
    names = listed()
    errors = [f"include/maul-ui/{name}: declares text functions; list it in "
              "tools/text-headers.txt" for name in sorted(found - names)]
    errors += [f"tools/text-headers.txt: {name} declares no text function"
               for name in sorted(names - found)]
    for error in errors:
        print(error)
    if errors:
        return 1
    print(f"text headers: {len(found)}, each listed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
