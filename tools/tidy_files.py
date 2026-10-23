#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Lists the sources clang-tidy checks: the core's, the renderer's and the
glue's C files in a build's compile_commands.json, generated files and
fetched dependencies left out. Fails when there are none, so a check over
an empty list never passes.

Usage: tidy_files.py BUILD_DIR/compile_commands.json
"""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    mine = tuple(str(ROOT / part) + "/" for part in ("src", "rhi/src", "window/src"))
    entries = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    files = sorted({str(pathlib.Path(e["directory"], e["file"]).resolve()) for e in entries})
    files = [f for f in files if f.startswith(mine) and "/generated/" not in f]
    if not files:
        sys.exit("tidy_files: no source of the library in the compile commands")
    print("\n".join(files))


if __name__ == "__main__":
    main()
