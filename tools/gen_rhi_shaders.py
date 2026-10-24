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
# It generates only with the pinned tools (PINNED, compared exactly, as
# Maul RHI's generators do) and a clean Maul RHI checkout at the tag
# cmake/Rhi.cmake pins, and refuses anything else by name: other tools,
# or an edited tree, write other bytes. The header carries a stamp: the
# SHA-256 of each input (the sources and this script), the Maul RHI
# release, the pinned tools, and the SHA-256 of the container. --check
# needs no tool: it recomputes the hashes, so CI fails when a source or
# this script changed without a regeneration, a pin moved, or the
# container was edited by hand. Inputs are hashed as Git stores them,
# with LF line ends, so a checkout with CRLF ends passes. --self-test
# shows on a copy that every such change fails the check.
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
# The tools the committed container was made by, as tool_identities()
# reads them (docs/operations/development.md#the-shader-container says
# where each comes from), and the stamp's lines naming them: DXC's name
# the family's release asset of microsoft/DirectXShaderCompiler and its
# SHA-256, as Maul RHI's stamps do.
PINNED = {
    "glslang": "glslang 15.1.0",
    "SPIRV-Tools": "SPIRV-Tools v2025.1",
    "spirv-cross": "spirv-cross 2021.01.15+1.3.239.0",
    "dxc": "libdxcompiler 1.9.0.1, 0d3ee6b5",
}
TOOL_LINES = (
    PINNED["glslang"],
    PINNED["SPIRV-Tools"],
    PINNED["spirv-cross"],
    "dxc linux_dxc_2026_07_29 (v1.9.2607, libdxcompiler 1.9.0.1, 0d3ee6b5)",
    "dxc sha256:55665c87824051ed4774ff3280a79ccbbb7d39243b9736ca5e98222134112d54",
)


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


def output(*command, env=None):
    """What a command prints, or nothing when it is missing or fails."""
    try:
        result = subprocess.run(command, capture_output=True, text=True, env=env)
    except OSError:
        return ""
    return result.stdout + result.stderr if result.returncode == 0 else ""


def tool_identities():
    """The installed tools, as PINNED names them. spirv-cross built by a
    distribution reports no version of its own; its package's is read."""
    glslang = output("glslangValidator", "--version").split("\n")[0].rsplit(":", 1)[-1]
    tools = " ".join(output("spirv-link", "--version").split()[:2])
    cross = output("spirv-cross", "--revision").strip()
    if cross in ("", "(Debian package)"):
        cross = output("dpkg-query", "-W", "-f=${Version}", "spirv-cross").rsplit("-", 1)[0]
    dxc = re.search(r"\((\d+)-(\w+)\)\(([\d.]+)\)", output("dxc", "--version"))
    return {"glslang": f"glslang {glslang.strip()}", "SPIRV-Tools": tools,
            "spirv-cross": f"spirv-cross {cross}",
            "dxc": f"libdxcompiler {dxc.group(3)}, {dxc.group(2)}" if dxc else "dxc: not found"}


def unpinned(found):
    """Each installed tool whose identity is not exactly its pin."""
    return [f"{name}: installed {found.get(name)}, pinned {pin}"
            for name, pin in PINNED.items() if found.get(name) != pin]


def git_environment():
    """The environment git runs in here: no global or system configuration
    and none of the caller's Git variables, so a developer's hooks, ignore
    rules or signing settings change neither what the checks see nor what
    the self-test does."""
    environment = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    environment.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    return environment


def rhi_release(tree):
    """The tag a clean Maul RHI checkout is at, and why it cannot be used
    when it is not: an edit or an untracked file there changes what its
    tools write while the tag stays the same."""
    # The clone is trusted by its path: it may belong to another user, as a
    # tree mounted into a container does, and no system or global
    # configuration is read to say safe.directory for it.
    git = ["git", "-c", f"safe.directory={os.path.realpath(tree)}", "-C", tree]
    try:
        status = subprocess.run(git + ["status", "--porcelain", "--untracked-files=all"],
                                capture_output=True, text=True, env=git_environment())
    except OSError:
        return None, "git is not installed"
    if status.returncode != 0:
        if "dubious ownership" in status.stderr:
            return None, (f"{tree} belongs to another user, and git trusts such a checkout "
                          "only at its top directory: name the clone's top directory")
        return None, f"{tree} is not a Git checkout"
    if status.stdout.strip():
        return None, f"{tree} has uncommitted changes or untracked files"
    tag = output(*git, "describe", "--tags", "--exact-match", env=git_environment()).strip()
    return (tag, None) if tag else (None, f"{tree} is not at a tag")


def stamp(root, rhi, data):
    lines = ["// The stamp tools/gen_rhi_shaders.py --check reads: the SHA-256 of",
             "// each input with LF line ends, the Maul RHI release, the SHA-256",
             "// of the container, and the pinned tools that built it."]
    lines += [f"//   input {path} {input_hash(root, path)}" for path in INPUTS]
    lines.append(f"//   maul-rhi {rhi}")
    lines.append(f"//   container {hashlib.sha256(data).hexdigest()}")
    lines += [f"//   tool {tool}" for tool in TOOL_LINES]
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
    if tuple(re.findall(r"^//   tool (.*)$", text, re.M)) != TOOL_LINES:
        found.append(f"{CONTAINER}: not built by the pinned tools")
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
        copy()
        edit(CONTAINER, lambda data: data.replace(b"spirv-cross 2021", b"spirv-cross 2022", 1))
        if not any("pinned tools" in problem for problem in problems(work)):
            failures.append("a container stamped by other tools passes")
        failures += self_test_generation(work)
    for failure in failures:
        print(f"gen_rhi_shaders --self-test: {failure}")
    if failures:
        sys.exit(1)
    print("gen_rhi_shaders --self-test: every change fails the check")


def self_test_generation(work):
    """What generating refuses: tools other than the pins, and a Maul RHI
    checkout that is edited, has untracked files, or is not at a tag."""
    failures = []
    if unpinned(PINNED):
        failures.append("the pinned tools are refused")
    if not unpinned({**PINNED, "spirv-cross": "spirv-cross 2022.01.01"}):
        failures.append("another spirv-cross is taken")
    tree = os.path.join(work, "maul-rhi")
    os.makedirs(os.path.join(tree, "tools"))
    git = ["git", "-C", tree, "-c", "user.name=self-test", "-c", "user.email=self-test@localhost"]
    environment = git_environment()

    def write(path, text):
        with open(os.path.join(tree, path), "w", encoding="utf-8") as file:
            file.write(text)

    write("tools/mrhi_container.py", "# a tool\n")
    for command in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "a release"],
                    ["tag", "v0.0.1"]):
        subprocess.run(git + command, capture_output=True, check=True, env=environment)
    if rhi_release(tree) != ("v0.0.1", None):
        failures.append("a clean checkout at its tag is refused")
    write("tools/mrhi_container.py", "# a tool, edited\n")
    if rhi_release(tree)[0] is not None:
        failures.append("an edited Maul RHI checkout is taken")
    subprocess.run(git + ["checkout", "-q", "--", "."], capture_output=True, check=True,
                   env=environment)
    write("tools/extra.py", "# untracked\n")
    if rhi_release(tree)[0] is not None:
        failures.append("a Maul RHI checkout with an untracked file is taken")
    os.remove(os.path.join(tree, "tools", "extra.py"))
    write("tools/mrhi_container.py", "# a later tool\n")
    subprocess.run(git + ["commit", "-q", "-am", "after the release"], capture_output=True,
                   check=True, env=environment)
    if rhi_release(tree)[0] is not None:
        failures.append("a Maul RHI checkout past its tag is taken")
    return failures


def main():
    if sys.argv[1:] == ["--check"]:
        return check()
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        sys.exit("usage: gen_rhi_shaders.py <a Maul RHI source tree> | --check | --self-test")
    tree = os.path.abspath(sys.argv[1])
    tools = os.path.join(tree, "tools")
    # The release the build fetches, unedited, so the container matches
    # the library it is loaded by.
    rhi, why = rhi_release(tree)
    if rhi is None:
        sys.exit(f"gen_rhi_shaders: {why}")
    if rhi != pinned_rhi(ROOT):
        sys.exit(f"gen_rhi_shaders: {tree} is at {rhi}, cmake/Rhi.cmake pins {pinned_rhi(ROOT)}")
    wrong = unpinned(tool_identities())
    if wrong:
        sys.exit("gen_rhi_shaders: the installed tools are not the pinned ones:\n  " +
                 "\n  ".join(wrong))
    # A Maul RHI release that moved its tools fails here, before anything
    # is compiled.
    for script in ("mrhi_dxil.py", "mrhi_msl.py", "mrhi_container.py"):
        if not os.path.isfile(os.path.join(tools, script)):
            sys.exit(f"gen_rhi_shaders: no {script} in {tools}")
    with tempfile.TemporaryDirectory() as work:
        data = build(work, tools)
    path = os.path.join(ROOT, CONTAINER)
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write(header(data, stamp(ROOT, rhi, data)))
    print(f"{os.path.relpath(path, ROOT)}: {len(data)} bytes")


if __name__ == "__main__":
    main()
