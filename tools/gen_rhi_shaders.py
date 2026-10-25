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
import importlib.util
import os
import py_compile
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

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
# The commit each Maul RHI release tag names: a local tag of that name on
# another commit is not the release.
RHI_COMMITS = {"v0.3.0": "b9edaf7e3a71ff5e78771f1e4e10d657b92e7fc7"}

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


def run(*command, cwd=None):
    result = subprocess.run(command, capture_output=True, text=True, cwd=cwd)
    if result.returncode != 0:
        sys.exit(f"{command[0]} failed:\n{result.stdout}{result.stderr}")


def tool_command(work, script):
    """How a Maul RHI tool runs: from the exported release, isolated from
    the environment and the user's site packages, its own directory not on
    the path, with bytecode neither read from nor written beside any
    source, only under the work directory."""
    return [sys.executable, "-I", "-B", "-X", f"pycache_prefix={os.path.join(work, 'pycache')}",
            script]


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
    # The tools run in the work directory, so a path they take relative to
    # where they run never names the caller's.
    run(*tool_command(work, os.path.join(tools, "mrhi_dxil.py")), linked, reflection, dxil,
        cwd=work)
    run(*tool_command(work, os.path.join(tools, "mrhi_msl.py")), linked, reflection, msl,
        cwd=work)
    container = os.path.join(work, f"{NAME}.mrsc")
    run(*tool_command(work, os.path.join(tools, "mrhi_container.py")), "--dxil", dxil, "--msl",
        msl, linked, os.path.join(SHADERS, f"{NAME}.wgsl"), reflection, container, cwd=work)
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
    the self-test does. Over the clone's own configuration, which may not
    be the caller's: no file system monitor runs and no untracked cache is
    read; nothing is fetched, though the clone be a partial one whose
    missing objects git would fetch through its remote's transport (no
    lazy fetch, and no protocol allowed whatever protocol.* the clone
    sets); no replace ref stands an edited commit in for the release; and
    git speaks English, so its messages can be matched."""
    environment = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    environment.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1", LC_ALL="C",
                       GIT_NO_LAZY_FETCH="1", GIT_ALLOW_PROTOCOL="none",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_CONFIG_COUNT="2",
                       GIT_CONFIG_KEY_0="core.fsmonitor", GIT_CONFIG_VALUE_0="false",
                       GIT_CONFIG_KEY_1="core.untrackedCache", GIT_CONFIG_VALUE_1="false")
    return environment


def blob_id(data, algorithm):
    """The object id Git gives a blob of these bytes."""
    digest = hashlib.new(algorithm)
    digest.update(b"blob %d\0" % len(data))
    digest.update(data)
    return digest.hexdigest()


def release_files(git, commit, algorithm, environment):
    """The files of a commit by path, their mode and blob id, read from its
    objects through cat-file, each object hashed against its id: git
    neither checks a loose or alternate object it reads against its id nor
    lets ls-tree do so, so a clone could hold a forged commit or tree under
    the release's ids. None when an object is missing, of another kind,
    not what its id names, or not a tree of plain files."""
    files = {}
    command = git + ["cat-file", "--batch"]
    with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, env=environment) as batch:

        def read(object_id, kind):
            batch.stdin.write(object_id.encode() + b"\n")
            batch.stdin.flush()
            header = batch.stdout.readline().split()
            if len(header) != 3:
                return None
            length = int(header[2])
            data = batch.stdout.read(length)
            batch.stdout.read(1)
            digest = hashlib.new(algorithm, b"%s %d\0" % (header[1], length) + data)
            return data if header[1] == kind and digest.hexdigest() == object_id else None

        try:
            data = read(commit, b"commit")
            if data is None or not data.startswith(b"tree "):
                return None
            trees = [(b"", data[5:data.index(b"\n")].decode())]
            size = hashlib.new(algorithm).digest_size
            while trees:
                prefix, tree_id = trees.pop()
                data = read(tree_id, b"tree")
                if data is None:
                    return None
                at = 0
                while at < len(data):
                    space = data.index(b" ", at)
                    end = data.index(b"\0", space)
                    mode, name = data[at:space], data[space + 1:end]
                    object_id = data[end + 1:end + 1 + size].hex()
                    at = end + 1 + size
                    if name in (b"", b".", b"..", b".git") or b"/" in name or len(object_id) != 2 * size:
                        return None
                    if mode == b"40000":
                        trees.append((prefix + name + b"/", object_id))
                    elif mode in (b"100644", b"100755", b"120000"):
                        files[prefix + name] = (mode, object_id)
                    else:
                        return None
        except (OSError, ValueError):
            return None
        finally:
            batch.stdin.close()
    return files


def verified_checkout(git, tree, commit, algorithm, environment):
    """A checkout's files by path, their mode and the bytes read and hashed,
    when they are a commit's: None when a file's bytes, kind or executable
    bit differ, the index lists other paths, or there are untracked files
    that its committed .gitignore files do not ignore. Read without git
    status, which runs the clean filters and the file system monitor a
    clone's own configuration names, and without the clone's
    .git/info/exclude, which is not part of the release."""
    files = release_files(git, commit, algorithm, environment)
    if files is None:
        return None
    checked = {}
    root = os.fsencode(tree)
    for path, (mode, object_id) in files.items():
        full = os.path.join(root, path)
        if mode in (b"100644", b"100755"):
            if os.path.islink(full) or not os.path.isfile(full):
                return None
            if bool(os.stat(full).st_mode & 0o100) != (mode == b"100755"):
                return None
            with open(full, "rb") as file:
                data = file.read()
        else:
            if not os.path.islink(full):
                return None
            data = os.readlink(full)
        if blob_id(data, algorithm) != object_id:
            return None
        checked[path] = (mode, data)
    # The index decides what ls-files counts as untracked: it must list
    # the release's paths and no others.
    cached = subprocess.run(git + ["ls-files", "-z", "--cached"], capture_output=True,
                            env=environment)
    if cached.returncode != 0 or set(filter(None, cached.stdout.split(b"\0"))) != set(files):
        return None
    untracked = git + ["ls-files", "-z", "--others", "--exclude-per-directory=.gitignore"]
    others = subprocess.run(untracked, capture_output=True, env=environment)
    # An untracked .gitignore could hide itself with the rest.
    ignored = subprocess.run(untracked + ["--ignored"], capture_output=True, env=environment)
    clean = (others.returncode == 0 and ignored.returncode == 0 and others.stdout == b"" and
             not any(os.path.basename(path) == b".gitignore"
                     for path in ignored.stdout.split(b"\0")))
    return checked if clean else None


def export_release(files, directory):
    """Writes a release's files as verified_checkout read and hashed them
    into a directory it creates, from which its tools run: nothing else in
    the clone, an ignored file or bytecode, is ever read or run. Each file
    is created anew, never through a link, and the links come last, each
    of them resolving inside the directory. None, or why the release
    cannot be exported: a link that is absolute or leads out of it."""
    root = os.fsencode(directory)
    os.makedirs(root)
    links = []
    for path, (mode, data) in sorted(files.items()):
        full = os.path.join(root, path)
        if mode == b"120000":
            if os.path.isabs(data):
                return f"{path.decode(errors='replace')} is an absolute link"
            links.append((path, full, data))
            continue
        os.makedirs(os.path.dirname(full), exist_ok=True)
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(full, flags, 0o600)
        with os.fdopen(descriptor, "wb") as file:
            os.fchmod(descriptor, 0o755 if mode == b"100755" else 0o644)
            file.write(data)
    for _, full, target in links:
        os.makedirs(os.path.dirname(full), exist_ok=True)
        os.symlink(target, full)
    real_root = os.path.realpath(root)
    for path, full, _ in links:
        if os.path.commonpath([os.path.realpath(full), real_root]) != real_root:
            return f"{path.decode(errors='replace')} is a link out of the release"
    return None


def rhi_release(tree, commits=RHI_COMMITS, export=None):
    """The tag a clean Maul RHI checkout is at, and why it cannot be used
    when it is not: a tag of the release's name on a commit other than the
    one commits pins for it is not the release, and an edit or an
    untracked file in the checkout changes what its tools write while the
    tag stays the same. With export, a fresh directory, the release's
    files are written there from the bytes that were checked."""
    # The clone is trusted by its path: it may belong to another user, as a
    # tree mounted into a container does, and no system or global
    # configuration is read to say safe.directory for it.
    git = ["git", "-c", f"safe.directory={os.path.realpath(tree)}", "-C", tree]
    environment = git_environment()
    try:
        top = subprocess.run(git + ["rev-parse", "--show-toplevel", "--show-object-format"],
                             capture_output=True, text=True, env=environment)
    except OSError:
        return None, "git is not installed"
    if top.returncode != 0:
        if "dubious ownership" in top.stderr:
            return None, (f"{tree} belongs to another user, and git trusts such a checkout "
                          "only at its top directory: name the clone's top directory")
        return None, f"{tree} is not a Git checkout"
    toplevel, algorithm = top.stdout.split("\n")[:2]
    if os.path.realpath(toplevel) != os.path.realpath(tree):
        return None, f"{tree} is inside a checkout: name the clone's top directory"
    tag = output(*git, "describe", "--tags", "--exact-match", env=environment).strip()
    if not tag:
        return None, f"{tree} is not at a tag"
    head = output(*git, "rev-parse", "--verify", "HEAD^{commit}", env=environment).strip()
    if head != commits.get(tag):
        return None, f"{tree} is at a {tag} tag on {head}, not the release's commit"
    # The release's objects as its pinned commit id names them, not as the
    # clone holds them.
    files = verified_checkout(git, tree, head, algorithm, environment)
    if files is None:
        return None, f"{tree} has uncommitted changes or untracked files"
    problem = export_release(files, export) if export is not None else None
    if problem is not None:
        return None, f"{tree}: {problem}"
    return tag, None


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
        failures += self_test_isolation(os.path.join(work, "isolation"))
        failures += self_test_links(os.path.join(work, "links"))
    for failure in failures:
        print(f"gen_rhi_shaders --self-test: {failure}")
    if failures:
        sys.exit(1)
    print("gen_rhi_shaders --self-test: every change fails the check")


def self_test_generation(work):
    """What generating refuses: tools other than the pins, and a Maul RHI
    checkout that is edited, has untracked files, is not at a tag or at a
    tag on another commit than the pinned one, or would fetch."""
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
    commit = subprocess.run(git + ["rev-parse", "HEAD"], capture_output=True, text=True,
                            check=True, env=environment).stdout.strip()

    def released():
        return rhi_release(tree, {"v0.0.1": commit})

    if released() != ("v0.0.1", None):
        failures.append("a clean checkout at its tag is refused")
    if rhi_release(tree, {"v0.0.1": "0" * 40})[0] is not None:
        failures.append("a tag of the release's name on another commit is taken")
    # The release's commit replaced by an edited one, whose files the
    # checkout holds.
    write("tools/mrhi_container.py", "# a tool, replaced\n")
    subprocess.run(git + ["add", "-A"], capture_output=True, check=True, env=environment)
    edited_tree = subprocess.run(git + ["write-tree"], capture_output=True, text=True, check=True,
                                 env=environment).stdout.strip()
    edited = subprocess.run(git + ["commit-tree", edited_tree, "-p", commit, "-m", "edited"],
                            capture_output=True, text=True, check=True,
                            env=environment).stdout.strip()
    subprocess.run(git + ["replace", commit, edited], capture_output=True, check=True,
                   env=environment)
    if released()[0] is not None:
        failures.append("a release replaced by an edited commit is taken")
    subprocess.run(git + ["replace", "-d", commit], capture_output=True, check=True,
                   env=environment)
    write("tools/mrhi_container.py", "# a tool\n")
    subprocess.run(git + ["add", "-A"], capture_output=True, check=True, env=environment)
    failures += self_test_forgeries(tree, git, environment, commit, released)
    # A clone's own configuration names a command git would run on status.
    marker = os.path.join(work, "fsmonitor-ran")
    subprocess.run(git + ["config", "core.fsmonitor", f"touch '{marker}' #"], capture_output=True,
                   check=True, env=environment)
    released()
    if os.path.exists(marker):
        failures.append("a command the clone's configuration names is run")
    subprocess.run(git + ["config", "--unset", "core.fsmonitor"], capture_output=True, check=True,
                   env=environment)
    # A clean filter the clone's attributes and configuration name, on a
    # file whose stat data changed.
    marker = os.path.join(work, "filter-ran")
    os.makedirs(os.path.join(tree, ".git", "info"), exist_ok=True)
    with open(os.path.join(tree, ".git", "info", "attributes"), "w", encoding="utf-8") as file:
        file.write("* filter=probe\n")
    subprocess.run(git + ["config", "filter.probe.clean", f"touch '{marker}'; cat"],
                   capture_output=True, check=True, env=environment)
    write("tools/mrhi_container.py", "# a tool\n")
    if released() != ("v0.0.1", None):
        failures.append("a clean checkout with a configured filter is refused")
    if os.path.exists(marker):
        failures.append("a filter the clone's configuration names is run")
    os.remove(os.path.join(tree, ".git", "info", "attributes"))
    subprocess.run(git + ["config", "--remove-section", "filter.probe"], capture_output=True,
                   check=True, env=environment)
    # An untracked file the clone's own exclude file hides, and one an
    # untracked .gitignore hides with itself.
    with open(os.path.join(tree, ".git", "info", "exclude"), "w", encoding="utf-8") as file:
        file.write("extra.py\n")
    write("tools/extra.py", "# untracked\n")
    if released()[0] is not None:
        failures.append("an untracked file .git/info/exclude hides is taken")
    os.remove(os.path.join(tree, ".git", "info", "exclude"))
    write("tools/.gitignore", ".gitignore\nextra.py\n")
    if released()[0] is not None:
        failures.append("an untracked file an untracked .gitignore hides is taken")
    os.remove(os.path.join(tree, "tools", ".gitignore"))
    os.remove(os.path.join(tree, "tools", "extra.py"))
    write("tools/mrhi_container.py", "# a tool, edited\n")
    if released()[0] is not None:
        failures.append("an edited Maul RHI checkout is taken")
    subprocess.run(git + ["checkout", "-q", "--", "."], capture_output=True, check=True,
                   env=environment)
    write("tools/extra.py", "# untracked\n")
    if released()[0] is not None:
        failures.append("a Maul RHI checkout with an untracked file is taken")
    os.remove(os.path.join(tree, "tools", "extra.py"))
    write("tools/mrhi_container.py", "# a later tool\n")
    subprocess.run(git + ["commit", "-q", "-am", "after the release"], capture_output=True,
                   check=True, env=environment)
    if released()[0] is not None:
        failures.append("a Maul RHI checkout past its tag is taken")
    # A partial clone missing HEAD's tree, which git would fetch through
    # the clone's remote with the ssh command its configuration names.
    marker = os.path.join(work, "fetch-ran")
    tree_id = subprocess.run(git + ["rev-parse", "HEAD^{tree}"], capture_output=True, text=True,
                             check=True, env=environment).stdout.strip()
    os.remove(os.path.join(tree, ".git", "objects", tree_id[:2], tree_id[2:]))
    for key, value in (("core.repositoryformatversion", "1"),
                       ("extensions.partialClone", "origin"),
                       ("remote.origin.url", "ssh://promisor.invalid/maul-rhi"),
                       ("remote.origin.promisor", "true"),
                       ("remote.origin.partialclonefilter", "tree:0"),
                       ("protocol.ssh.allow", "always"),
                       ("core.sshCommand", f"touch '{marker}' #")):
        subprocess.run(git + ["config", key, value], capture_output=True, check=True,
                       env=environment)
    if released()[0] is not None:
        failures.append("a partial clone missing objects is taken")
    # With lazy fetching left on, as a Git without GIT_NO_LAZY_FETCH
    # leaves it, the protocol allow-list alone stops the fetch.
    lazy = {key: value for key, value in environment.items() if key != "GIT_NO_LAZY_FETCH"}
    subprocess.run(git + ["ls-tree", "-r", "HEAD"], capture_output=True, env=lazy)
    if os.path.exists(marker):
        failures.append("a fetch the clone's configuration names is run")
    return failures


def self_test_isolation(work):
    """A release's tools run from the copy written as checked: bytecode
    and an ignored module planted in the clone, which a tool would load
    from its own directory, run no code."""
    failures = []
    tree = os.path.join(work, "isolated")
    os.makedirs(os.path.join(tree, "tools"))
    git = ["git", "-C", tree, "-c", "user.name=self-test", "-c", "user.email=self-test@localhost"]
    environment = git_environment()
    files = {".gitignore": "__pycache__/\njson.py\n",
             "tools/helper.py": "VALUE = 1\n",
             "tools/script.py": ("import importlib.util, json, os\n"
                                 "path = os.path.join(os.path.dirname(os.path.abspath(__file__)),"
                                 " 'helper.py')\n"
                                 "spec = importlib.util.spec_from_file_location('helper', path)\n"
                                 "helper = importlib.util.module_from_spec(spec)\n"
                                 "spec.loader.exec_module(helper)\n"
                                 "print(json.dumps(helper.VALUE))\n")}
    for path, text in files.items():
        with open(os.path.join(tree, path), "w", encoding="utf-8") as file:
            file.write(text)
    for command in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "a release"],
                    ["tag", "v0.0.1"]):
        subprocess.run(git + command, capture_output=True, check=True, env=environment)
    commit = subprocess.run(git + ["rev-parse", "HEAD"], capture_output=True, text=True,
                            check=True, env=environment).stdout.strip()
    # Bytecode for helper that Python takes without checking its source,
    # and a json module beside the tool; both ignored, so the checkout
    # is still the release.
    markers = [os.path.join(work, "bytecode-ran"), os.path.join(work, "module-ran")]
    planted = os.path.join(work, "planted.py")
    with open(planted, "w", encoding="utf-8") as file:
        file.write(f"open({markers[0]!r}, 'w').close()\nVALUE = 1\n")
    py_compile.compile(planted, importlib.util.cache_from_source(
        os.path.join(tree, "tools", "helper.py")), doraise=True,
        invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)
    with open(os.path.join(tree, "tools", "json.py"), "w", encoding="utf-8") as file:
        file.write(f"open({markers[1]!r}, 'w').close()\ndumps = str\n")
    release = os.path.join(work, "isolated-release")
    if rhi_release(tree, {"v0.0.1": commit}, export=release) != ("v0.0.1", None):
        failures.append("a release with ignored files is refused")
    ran = subprocess.run(tool_command(work, os.path.join(release, "tools", "script.py")),
                         capture_output=True, text=True, cwd=work)
    if ran.stdout.strip() != "1":
        failures.append("a tool of the exported release does not run")
    if any(os.path.exists(marker) for marker in markers):
        failures.append("code the clone holds outside the release is run")
    return failures


def self_test_links(work):
    """A release's links exported only where they stay inside it: one to
    a file or a directory of the release is kept; one that is absolute,
    climbs out, or climbs out through another link refuses the release."""
    failures = []

    def release(name, links):
        tree = os.path.join(work, name)
        os.makedirs(os.path.join(tree, "tools"))
        with open(os.path.join(tree, "tools", "helper.py"), "w", encoding="utf-8") as file:
            file.write("VALUE = 1\n")
        for path, target in links.items():
            os.makedirs(os.path.dirname(os.path.join(tree, path)), exist_ok=True)
            os.symlink(target, os.path.join(tree, path))
        git = ["git", "-C", tree, "-c", "user.name=self-test",
               "-c", "user.email=self-test@localhost"]
        environment = git_environment()
        for command in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "a release"],
                        ["tag", "v0.0.1"]):
            subprocess.run(git + command, capture_output=True, check=True, env=environment)
        commit = subprocess.run(git + ["rev-parse", "HEAD"], capture_output=True, text=True,
                                check=True, env=environment).stdout.strip()
        return rhi_release(tree, {"v0.0.1": commit}, export=os.path.join(work, name + "-export"))

    if release("contained", {"tools/ok": "helper.py", "docs": "tools"}) != ("v0.0.1", None):
        failures.append("a release with links inside it is refused")
    elif not os.path.isfile(os.path.join(work, "contained-export", "docs", "ok")):
        failures.append("a release's links do not resolve in its export")
    for name, links in (("climbing", {"tools/escape": "../../outside"}),
                        ("absolute", {"tools/escape": "/etc/passwd"}),
                        ("chained", {"a/up": "..", "escape": "a/up/../.."})):
        if release(name, links)[0] is not None:
            failures.append(f"a release with a link out of it ({name}) is taken")
    return failures


def self_test_forgeries(tree, git, environment, commit, released):
    """Objects a clone forges under the release's ids, which git reads
    without checking, and a file only its index lists: each refused."""
    failures = []

    def run(*arguments):
        return subprocess.run(git + list(arguments), capture_output=True, text=True, check=True,
                              env=environment).stdout.strip()

    def object_path(objects, object_id):
        return os.path.join(objects, object_id[:2], object_id[2:])

    def forge(path, kind, content):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if os.path.exists(path):
            os.chmod(path, 0o644)
        with open(path, "wb") as file:
            file.write(zlib.compress(b"%s %d\0" % (kind, len(content)) + content))

    def edit():
        with open(os.path.join(tree, "tools", "mrhi_container.py"), "w", encoding="utf-8") as file:
            file.write("# a tool, forged\n")
        return run("hash-object", "-w", "tools/mrhi_container.py")

    def restore(path, data):
        if os.path.exists(path):
            os.chmod(path, 0o644)
        with open(path, "wb") as file:
            file.write(data)
        with open(os.path.join(tree, "tools", "mrhi_container.py"), "w", encoding="utf-8") as file:
            file.write("# a tool\n")

    objects = os.path.join(tree, ".git", "objects")
    tools = run("rev-parse", "HEAD:tools")
    with open(object_path(objects, tools), "rb") as file:
        genuine = file.read()
    # A tree forged under the tools tree's id, naming an edited blob the
    # checkout holds.
    forged = b"100644 mrhi_container.py\0" + bytes.fromhex(edit())
    forge(object_path(objects, tools), b"tree", forged)
    if released()[0] is not None:
        failures.append("a tree forged under the release's id is taken")
    restore(object_path(objects, tools), genuine)
    # The same, from an alternate object directory.
    edit()
    os.remove(object_path(objects, tools))
    alternate = os.path.join(os.path.dirname(tree), "alternate")
    forge(object_path(alternate, tools), b"tree", forged)
    with open(os.path.join(objects, "info", "alternates"), "w", encoding="utf-8") as file:
        file.write(alternate + "\n")
    if released()[0] is not None:
        failures.append("a tree forged in an alternate object directory is taken")
    os.remove(os.path.join(objects, "info", "alternates"))
    restore(object_path(objects, tools), genuine)
    # A commit forged under the release's id, naming a tree of edited files.
    with open(object_path(objects, commit), "rb") as file:
        genuine = file.read()
    edit()
    run("add", "-A")
    edited = run("write-tree")
    forge(object_path(objects, commit), b"commit",
          f"tree {edited}\nauthor a <a> 0 +0000\ncommitter a <a> 0 +0000\n\nforged\n".encode())
    if released()[0] is not None:
        failures.append("a commit forged under the release's id is taken")
    restore(object_path(objects, commit), genuine)
    run("add", "-A")
    # A file the index lists that the release does not, which ls-files
    # then counts as tracked.
    with open(os.path.join(tree, "tools", "extra.py"), "w", encoding="utf-8") as file:
        file.write("# only in the index\n")
    run("add", "tools/extra.py")
    if released()[0] is not None:
        failures.append("a file only the index lists is taken")
    run("rm", "-q", "--cached", "tools/extra.py")
    os.remove(os.path.join(tree, "tools", "extra.py"))
    if released() != ("v0.0.1", None):
        failures.append("the release is refused after the forgeries are undone")
    return failures


def main():
    if sys.argv[1:] == ["--check"]:
        return check()
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        sys.exit("usage: gen_rhi_shaders.py <a Maul RHI source tree> | --check | --self-test")
    tree = os.path.abspath(sys.argv[1])
    wrong = unpinned(tool_identities())
    if wrong:
        sys.exit("gen_rhi_shaders: the installed tools are not the pinned ones:\n  " +
                 "\n  ".join(wrong))
    with tempfile.TemporaryDirectory() as work:
        # The release the build fetches, unedited, so the container matches
        # the library it is loaded by; its tools run from the copy of it
        # written as checked, never from the clone.
        release = os.path.join(work, "maul-rhi")
        rhi, why = rhi_release(tree, export=release)
        if rhi is None:
            sys.exit(f"gen_rhi_shaders: {why}")
        if rhi != pinned_rhi(ROOT):
            sys.exit(f"gen_rhi_shaders: {tree} is at {rhi}, cmake/Rhi.cmake pins "
                     f"{pinned_rhi(ROOT)}")
        # A Maul RHI release that moved its tools fails here, before
        # anything is compiled.
        tools = os.path.join(release, "tools")
        for script in ("mrhi_dxil.py", "mrhi_msl.py", "mrhi_container.py"):
            if not os.path.isfile(os.path.join(tools, script)):
                sys.exit(f"gen_rhi_shaders: no {script} in {tree}/tools")
        data = build(work, tools)
    path = os.path.join(ROOT, CONTAINER)
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write(header(data, stamp(ROOT, rhi, data)))
    print(f"{os.path.relpath(path, ROOT)}: {len(data)} bytes")


if __name__ == "__main__":
    main()
