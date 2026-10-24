#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Builds the reference renderer's shader container
# offline, as Maul RHI's own script does: glslangValidator compiles each stage of
# rhi/shaders/quad.* under its entry name, spirv-link joins them, spirv-val
# checks the module for Vulkan 1.3, and Maul RHI's tools cross it to
# DXIL (spirv-cross and dxc) and Metal (spirv-cross) and write the
# container with the WGSL beside it, which lands in
# rhi/shaders/quad_container.h as bytes. CI runs none of these tools; the
# header is committed, and this script is run again whenever a source
# changes.
#
# The header carries a stamp: the SHA-256 of each input (the sources, this
# script, and the Maul RHI release, which must be the one cmake/Rhi.cmake
# pins), the SHA-256 of the container, and what each tool reported. --check
# needs no tool: it recomputes the hashes, so CI fails when a source or this
# script changed without a regeneration, or the container was edited by
# hand. Inputs are hashed as Git stores them, with LF line ends, so a
# checkout with CRLF ends passes. --self-test shows on a copy that every
# such change fails the check.
#
# usage: gen_rhi_shaders.py <a Maul RHI source tree, for its tools/>
#        gen_rhi_shaders.py --check | --self-test

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADERS = os.path.join(ROOT, "rhi", "shaders")
NAME = "quad"
STAGES = (("vert", "vs"), ("frag", "fs"))
# The inputs the stamp names, relative to the repository's root.
INPUTS = tuple(f"rhi/shaders/{NAME}.{suffix}" for suffix in ("frag", "json", "vert", "wgsl")) + (
    "tools/gen_rhi_shaders.py",)
CONTAINER = f"rhi/shaders/{NAME}_container.h"


def run(*command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"{command[0]} failed:\n{result.stdout}{result.stderr}")


def build(work, tools):
    modules = []
    for suffix, entry in STAGES:
        module = os.path.join(work, f"{NAME}_{entry}.spv")
        run("glslangValidator", "-V", "--target-env", "vulkan1.3", "-e", entry,
            "--source-entrypoint", "main", "-o", module,
            os.path.join(SHADERS, f"{NAME}.{suffix}"))
        modules.append(module)
    linked = os.path.join(work, f"{NAME}.spv")
    run("spirv-link", "--target-env", "vulkan1.3", *modules, "-o", linked)
    run("spirv-val", "--target-env", "vulkan1.3", linked)
    reflection = os.path.join(SHADERS, f"{NAME}.json")
    dxil = os.path.join(work, "dxil")
    msl = os.path.join(work, "msl")
    run(sys.executable, os.path.join(tools, "mrhi_dxil.py"), linked, reflection, dxil)
    run(sys.executable, os.path.join(tools, "mrhi_msl.py"), linked, reflection, msl)
    container = os.path.join(work, f"{NAME}.mrsc")
    run(sys.executable, os.path.join(tools, "mrhi_container.py"), "--dxil", dxil, "--msl", msl,
        linked, os.path.join(SHADERS, f"{NAME}.wgsl"), reflection, container)
    with open(container, "rb") as file:
        return file.read()


def input_hash(root, path):
    """The SHA-256 of a file's bytes as Git stores them: LF line ends."""
    with open(os.path.join(root, path), "rb") as file:
        return hashlib.sha256(file.read().replace(b"\r\n", b"\n")).hexdigest()


def pinned_rhi(root):
    """The Maul RHI release cmake/Rhi.cmake pins, as its tag."""
    with open(os.path.join(root, "cmake", "Rhi.cmake"), encoding="utf-8") as file:
        found = re.search(r"set\(MAUL_UI_RHI_VERSION ([0-9.]+)\)", file.read())
    if found is None:
        sys.exit("gen_rhi_shaders: no MAUL_UI_RHI_VERSION in cmake/Rhi.cmake")
    return "v" + found.group(1)


def first_line(*command):
    result = subprocess.run(command, capture_output=True, text=True)
    lines = (result.stdout + result.stderr).strip().splitlines()
    return lines[0] if lines else "unknown"


def tool_versions():
    return [
        first_line("glslangValidator", "--version"),
        first_line("spirv-val", "--version"),
        "spirv-cross " + first_line("spirv-cross", "--revision"),
        first_line("dxc", "--version"),
    ]


def stamp(root, rhi, data, tools):
    lines = ["// The stamp tools/gen_rhi_shaders.py --check reads: the SHA-256 of",
             "// each input with LF line ends, the Maul RHI release, the SHA-256",
             "// of the container, and the tools that built it."]
    lines += [f"//   input {path} {input_hash(root, path)}" for path in INPUTS]
    lines.append(f"//   maul-rhi {rhi}")
    lines.append(f"//   container {hashlib.sha256(data).hexdigest()}")
    lines += [f"//   tool {tool}" for tool in tools]
    return lines


def header(data, stamped):
    guard = f"MAUL_UI_{NAME.upper()}_CONTAINER_H"
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        "// The reference renderer's shader container, generated by",
        f"// tools/gen_rhi_shaders.py from rhi/shaders/{NAME}.*: edit the sources",
        "// and run the script, not this file.",
        "//",
        *stamped,
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdalign.h>",
        "#include <stdint.h>",
        "",
        f"alignas(8) static const uint8_t s_{NAME}Container[{len(data)}] = {{",
    ]
    for at in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in data[at:at + 16]))
    lines += ["};", "", f"#endif // {guard}", ""]
    return "\n".join(lines)


def problems(root):
    """What in root's container no longer matches its inputs; empty when
    it is current."""
    with open(os.path.join(root, CONTAINER), encoding="utf-8") as file:
        text = file.read()
    found = []
    stamped = dict(re.findall(r"^//   input (\S+) ([0-9a-f]{64})$", text, re.M))
    for path in INPUTS:
        if path not in stamped:
            found.append(f"{path}: not in the stamp")
        elif stamped[path] != input_hash(root, path):
            found.append(f"{path}: changed since the container was generated")
    rhi = re.search(r"^//   maul-rhi (\S+)$", text, re.M)
    if rhi is None or rhi.group(1) != pinned_rhi(root):
        found.append(f"cmake/Rhi.cmake pins {pinned_rhi(root)}, the container was built with "
                     f"{rhi.group(1) if rhi else 'no release'}")
    digest = re.search(r"^//   container ([0-9a-f]{64})$", text, re.M)
    body = text.split("= {", 1)[1] if "= {" in text else ""
    data = bytes(int(byte, 16) for byte in re.findall(r"0x([0-9A-F]{2}),", body))
    if digest is None or digest.group(1) != hashlib.sha256(data).hexdigest():
        found.append(f"{CONTAINER}: its bytes are not the ones the stamp names")
    return found


def check():
    found = problems(ROOT)
    for problem in found:
        print(f"gen_rhi_shaders --check: {problem}")
    if found:
        sys.exit("regenerate the container: python3 tools/gen_rhi_shaders.py <Maul RHI "
                 f"{pinned_rhi(ROOT)}> (docs/operations/development.md#generated-files)")
    print(f"{CONTAINER}: current")


def self_test():
    """On a copy of the inputs: the check passes, and fails on each kind
    of change it guards against; CRLF line ends still pass."""
    files = INPUTS + (CONTAINER, "cmake/Rhi.cmake")
    with tempfile.TemporaryDirectory() as work:
        def copy():
            for path in files:
                os.makedirs(os.path.dirname(os.path.join(work, path)), exist_ok=True)
                shutil.copyfile(os.path.join(ROOT, path), os.path.join(work, path))

        def edit(path, change):
            with open(os.path.join(work, path), "rb") as file:
                data = file.read()
            with open(os.path.join(work, path), "wb") as file:
                file.write(change(data))

        failures = []
        copy()
        if problems(work):
            failures.append(f"a current copy fails: {problems(work)}")
        for path in INPUTS:
            edit(path, lambda data: data.replace(b"\n", b"\r\n"))
        if problems(work):
            failures.append(f"CRLF line ends fail: {problems(work)}")
        for path in INPUTS:
            copy()
            # One byte of the source's first line, which every input has.
            edit(path, lambda data: data[:3] + bytes([data[3] ^ 1]) + data[4:])
            if not any(problem.startswith(path) for problem in problems(work)):
                failures.append(f"a changed byte of {path} passes")
        copy()
        edit("cmake/Rhi.cmake",
             lambda data: re.sub(rb"(MAUL_UI_RHI_VERSION [0-9.]+)", rb"\g<1>9", data))
        if not any("pins" in problem for problem in problems(work)):
            failures.append("another Maul RHI pin passes")
        copy()
        edit(CONTAINER, lambda data: data.replace(b"0x4D, 0x52", b"0x4D, 0x53", 1))
        if not any("bytes" in problem for problem in problems(work)):
            failures.append("a changed container byte passes")
    for failure in failures:
        print(f"gen_rhi_shaders --self-test: {failure}")
    if failures:
        sys.exit(1)
    print("gen_rhi_shaders --self-test: every change fails the check")


def main():
    if sys.argv[1:] == ["--check"]:
        return check()
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        sys.exit("usage: gen_rhi_shaders.py <a Maul RHI source tree> | --check | --self-test")
    tree = os.path.abspath(sys.argv[1])
    tools = os.path.join(tree, "tools")
    # The release the build fetches, so the container matches the
    # library it is loaded by.
    rhi = first_line("git", "-C", tree, "describe", "--tags", "--exact-match")
    if rhi != pinned_rhi(ROOT):
        sys.exit(f"gen_rhi_shaders: {tree} is at {rhi}, cmake/Rhi.cmake pins {pinned_rhi(ROOT)}")
    # A Maul RHI release that moved its tools fails here, before anything
    # is compiled.
    for script in ("mrhi_dxil.py", "mrhi_msl.py", "mrhi_container.py"):
        if not os.path.isfile(os.path.join(tools, script)):
            sys.exit(f"gen_rhi_shaders: no {script} in {tools}")
    with tempfile.TemporaryDirectory() as work:
        data = build(work, tools)
    path = os.path.join(ROOT, CONTAINER)
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write(header(data, stamp(ROOT, rhi, data, tool_versions())))
    print(f"{os.path.relpath(path, ROOT)}: {len(data)} bytes")


if __name__ == "__main__":
    main()
