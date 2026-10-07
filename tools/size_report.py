#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Reports Maul UI's wasm size as record mui-0001's size budget measures it.

Each part is measured as the bytes of wasm it adds to a program at -Oz
with link-time optimization: the core above an empty program, and the
text component (Maul UI's text code with FreeType and HarfBuzz) above the
core. A program that calls a few functions keeps only what those reach,
so the measured programs take the address of every public function of
their parts that the build defines, the most a program can link. Maul
Unicode, built into the library, counts against its own budget: the
public functions of it that Maul UI's code calls are measured above an
empty program and taken off the text component's bytes.

Usage: size_report.py BUILD_DIR, BUILD_DIR configured by emcmake with
CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON, CMAKE_BUILD_TYPE=MinSizeRel and
-Oz for C and C++, and built; emcc, em++ and emnm on the PATH
(emsdk_env). Prints the sizes against the ceilings; the
ceilings are checked when a release is made, so it fails only when a
program does not build.
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADERS = ROOT / "include" / "maul-ui"
# The text component's public headers, from the list check_modules.py
# keeps whole.
TEXT_HEADERS = {line.split("#", 1)[0].strip()
                for line in (ROOT / "tools" / "text-headers.txt").read_text(encoding="utf-8")
                .splitlines()} - {""}
CEILINGS = {"core": 160_000, "text": 640_000}
FLAGS = ["-Oz", "-flto"]
DECLARATION = re.compile(r"\bMUI_API\b[^;{]*?\b(mui[A-Za-z0-9_]+)\s*\(", re.S)
UNICODE_DECLARATION = re.compile(r"\bMUNI_API\b[^;{]*?\b(muni[A-Za-z0-9_]+)\s*\(", re.S)
OWN = re.compile(r"^mui[A-Z]")


def public_functions():
    """The public functions by header, split into the core's and the text's."""
    core, text = [], []
    for header in sorted(HEADERS.glob("*.h")):
        names = DECLARATION.findall(header.read_text(encoding="utf-8"))
        (text if header.name in TEXT_HEADERS else core).append((header.name, names))
    return core, text


def symbols(archives):
    """The functions the archives define, and those Maul UI's own members
    (those defining a mui name) call without defining."""
    nm = shutil.which("emnm")
    if nm is None:
        sys.exit("size_report: emnm is not on the PATH (run emsdk_env)")
    output = subprocess.run([nm, "-A", *archives], check=True, capture_output=True,
                            text=True).stdout
    members = {}
    for line in output.splitlines():
        place, _, rest = line.rpartition(": ")
        fields = rest.split()
        if len(fields) < 2:
            continue
        defined, undefined = members.setdefault(place, (set(), set()))
        (undefined if fields[-2] == "U" else defined).add(fields[-1])
    every = set().union(*(defined for defined, _ in members.values()))
    called = set()
    for defined, undefined in members.values():
        if any(OWN.match(name) for name in defined):
            called |= undefined
    return every, called


def unicode_functions(build, called):
    """Maul Unicode's public functions Maul UI calls, by header, and the
    directory holding its headers; none when the build has no Maul
    Unicode."""
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    found = re.search(r"^maul-unicode_SOURCE_DIR:STATIC=(.*)$", cache, re.M)
    if found is None:
        return [], None
    headers = pathlib.Path(found.group(1)) / "include" / "maul-unicode"
    part = []
    for header in sorted(headers.glob("*.h")):
        names = [name for name in UNICODE_DECLARATION.findall(header.read_text(encoding="utf-8"))
                 if name in called]
        if names:
            part.append((header.name, names))
    return part, headers.parent


def program(parts, defined):
    """A program taking every defined function of the parts' headers."""
    headers, names = [], []
    for part in parts:
        for header, found in part:
            kept = [name for name in found if name in defined]
            if kept:
                headers.append(header if "/" in header else f"maul-ui/{header}")
                names.extend(kept)
    lines = [f'#include "{header}"' for header in headers]
    lines += ["", "#include <stddef.h>", "#include <stdint.h>", "",
              "typedef void (*Function)(void);", "static Function const s_functions[] = {"]
    lines += [f"    (Function){name}," for name in names]
    lines += ["};", "",
              "int main(int count, char** arguments)", "{", "    (void)arguments;",
              "    size_t at = (size_t)count % (sizeof s_functions / sizeof s_functions[0]);",
              "    return (int)((uintptr_t)s_functions[at] & 1u);", "}", ""]
    return "\n".join(lines), len(names)


def wasm_bytes(directory, name, source, archives, includes):
    """Builds a program and returns its wasm's bytes."""
    path = directory / f"{name}.c"
    path.write_text(source, encoding="utf-8")
    objects = directory / f"{name}.o"
    output = directory / f"{name}.js"
    subprocess.run(["emcc", *FLAGS, "-std=c23", *[f"-I{include}" for include in includes], "-c",
                    str(path), "-o", str(objects)], check=True)
    subprocess.run(["em++", *FLAGS, "-sALLOW_MEMORY_GROWTH", str(objects), *archives, "-o",
                    str(output)], check=True)
    return (directory / f"{name}.wasm").stat().st_size


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build = pathlib.Path(sys.argv[1]).resolve()
    archives = [str(path) for path in sorted(build.rglob("*.a"))]
    if not archives:
        sys.exit(f"size_report: no archives in {build}")
    defined, called = symbols(archives)
    core, text = public_functions()
    unicode, unicode_include = unicode_functions(build, called)
    unicode = [(f"maul-unicode/{header}", names) for header, names in unicode]
    includes = [ROOT / "include"] + ([unicode_include] if unicode_include else [])
    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(temporary)

        def measure(name, parts):
            source, count = program(parts, defined)
            return wasm_bytes(directory, name, source, archives, includes), count

        empty = wasm_bytes(directory, "empty", "int main(void)\n{\n    return 0;\n}\n", archives,
                           includes)
        with_core, core_count = measure("core", [core])
        with_text, all_count = measure("text", [core, text])
        with_unicode, unicode_count = measure("unicode", [unicode])
    linked = with_text - with_core
    unicode_bytes = with_unicode - empty
    sizes = {"core": with_core - empty, "text": linked - unicode_bytes}
    counts = {"core": core_count, "text": all_count - core_count}
    print(f"wasm at -Oz with LTO; an empty program is {empty} bytes")
    for part in ("core", "text"):
        ceiling = CEILINGS[part]
        verdict = "within" if sizes[part] <= ceiling else "OVER"
        print(f"{part}: {sizes[part]} bytes for {counts[part]} functions, "
              f"{verdict} the ceiling of {ceiling} ({100 * sizes[part] / ceiling:.0f}%)")
    print(f"maul-unicode as the text component calls it: {unicode_bytes} bytes for "
          f"{unicode_count} functions, against its own budget (the text component links "
          f"{linked} with it)")


if __name__ == "__main__":
    main()
