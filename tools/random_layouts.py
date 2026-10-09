#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Random layouts against Chrome (record mui-0003), a development tool
# beside gen_layout_fixtures.py.
#
# "generate SEED COUNT" prints COUNT random fixtures drawn from SEED in the
# corpus format: trees up to three levels deep, at most three children a
# node, most keys the corpus knows; --wide adds scaled dimensions with an
# offset, automatic margins, baseline alignment and anchors, under names
# random_<seed>w_<n>, leaving the default draws as they were; --words
# draws the --wide layouts again, some host content made words that wrap
# (from a stream of its own, so the layouts are otherwise the same),
# under names random_<seed>t_<n>. Put them in
# a file under test/layout/, render it with gen_layout_fixtures.py
# --oracle and run test_layout_fixtures to see which part from Chrome; the
# ones that agree may stay in the corpus.
#
# "reduce FILE NAME" shrinks fixture NAME of corpus file FILE, which parts
# from Chrome, to a smallest case that still does. It copies the tree's
# tracked files to a temporary directory, builds the fixture test there,
# and drops subtrees and keys while Chrome and the library still part,
# rendering each round's candidates in one Chrome call. It prints the
# reduced fixture with Chrome's rectangles and the library's answers.
#
# Both need what --oracle needs: Node and puppeteer (MUI_NODE_MODULES);
# reduce also needs CMake and a C23 compiler (CC names one if the default
# is older).
#
# usage: random_layouts.py generate SEED COUNT [--wide | --words]
#        random_layouts.py reduce FILE NAME

import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def call(args, **options):
    """Runs a command; on failure, exits with its output."""
    result = subprocess.run(args, capture_output=True, text=True, **options)
    if result.returncode != 0:
        sys.exit(f"random layouts: {' '.join(args)} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


class Generator:
    """Draws fixtures; a seed always draws the same ones."""

    def __init__(self, seed, wide=False, words=False):
        self.rng = random.Random(seed)
        # Wide draws add keys the default leaves out; the default's draws
        # stay as they were, so its seeds keep their layouts.
        self.wide = wide or words
        # Words come from a stream of their own: a seed's word layouts are
        # its wide ones, some content wrapping.
        self.words = random.Random(seed ^ 0x5A5A5A5A) if words else None

    def dimension(self):
        r = self.rng.random()
        if r < 0.45:
            return None
        if r < 0.8:
            return str(self.rng.choice([0, 10, 20, 30, 40, 60, 80, 120, 150]))
        scale = f"{self.rng.choice([10, 25, 50, 75, 100])}%"
        if self.wide and self.rng.random() < 0.4:
            return scale + self.rng.choice(["+", "-"]) + str(self.rng.choice([5, 10, 20]))
        return scale

    def edges(self):
        if self.rng.random() < 0.6:
            return None
        if self.rng.random() < 0.5:
            return str(self.rng.choice([0, 2, 5, 10]))
        return ",".join(str(self.rng.choice([0, 3, 7])) for _ in range(4))

    def pick(self, chance, key, values):
        return [f"{key}={self.rng.choice(values)}"] if self.rng.random() < chance else []

    def item_keys(self):
        keys = self.pick(0.25, "grow", [0, 1, 2]) + self.pick(0.2, "shrink", [0, 1, 3])
        if self.rng.random() < 0.15:
            basis = self.dimension()
            keys += [f"basis={basis}"] if basis else []
        aligns = ["auto", "stretch", "start", "end", "center"] + (["baseline"] if self.wide else [])
        keys += self.pick(0.2, "align-self", aligns)
        if self.rng.random() < 0.08:
            keys.append("position=absolute")
            for inset in ("start", "end", "top", "bottom"):
                keys += self.pick(0.4, inset, [0, 5, 10, 20])
            if self.wide and self.rng.random() < 0.3:
                keys.append(f"anchor={self.rng.choice([0, 0.5, 1])},{self.rng.choice([0, 0.5, 1])}")
        return keys

    def node(self, depth, root=False):
        """A node and its subtree as (keys, children), depth from the root."""
        keys = []
        for key in ("width", "height"):
            value = self.dimension()
            if root and value is None and self.rng.random() < 0.5:
                value = str(self.rng.choice([200, 300, 400]))
            keys += [f"{key}={value}"] if value else []
        for key in ("min-width", "min-height", "max-width", "max-height"):
            if self.rng.random() < 0.12:
                value = self.dimension()
                keys += [f"{key}={value}"] if value else []
        if not root:
            keys += self.item_keys()
        keys += self.pick(0.12, "aspect", [0.5, 1, 1.5, 2])
        for key in ("margin", "border", "padding"):
            value = self.edges()
            if key == "margin" and self.wide and self.rng.random() < 0.15:
                value = ",".join(self.rng.choice(["auto", "0", "5"]) for _ in range(4))
            if value and not (key == "margin" and root):
                keys.append(f"{key}={value}")
        if depth >= 3 or self.rng.random() < (0.35 if depth > 0 else 0.0):
            if self.rng.random() < 0.6:
                width = self.rng.choice([10, 20, 30, 50, 80])
                content = f"content={width}x{self.rng.choice([10, 16, 20, 40])}"
                if self.words and self.words.random() < 0.5:
                    content += f"*{self.words.randint(2, 8)}"
                keys.append(content)
            return keys, []
        return self.container(keys, depth)

    def container(self, keys, depth):
        r = self.rng
        keys = keys + self.pick(0.4, "direction", ["row", "row-reverse", "column", "column-reverse"])
        keys += self.pick(0.3, "wrap", ["nowrap", "wrap", "wrap-reverse"])
        aligns = ["start", "end", "center", "space-between", "space-around", "space-evenly"]
        keys += self.pick(0.3, "justify", aligns)
        items = ["stretch", "start", "end", "center"] + (["baseline"] if self.wide else [])
        keys += self.pick(0.3, "align-items", items)
        keys += self.pick(0.2, "align-content", ["stretch"] + aligns)
        keys += self.pick(0.15, "row-gap", [0, 4, 10]) + self.pick(0.15, "column-gap", [0, 4, 10])
        keys += self.pick(0.1, "dir", ["ltr", "rtl"])
        return keys, [self.node(depth + 1) for _ in range(r.randint(1, 3))]


def render(name, available, nodes):
    """A fixture's text from (depth, keys) pairs, rectangles left zero."""
    lines = [f"fixture {name} available={available}"]
    for depth, keys in nodes:
        lines.append("  " * depth + " ".join(["node"] + keys) + " => 0 0 0 0")
    return "\n".join(lines)


def flatten(tree, depth=0):
    keys, kids = tree
    nodes = [(depth, keys)]
    for kid in kids:
        nodes += flatten(kid, depth + 1)
    return nodes


def generate(seed, count, draws=""):
    generator = Generator(seed, draws == "--wide", draws == "--words")
    tag = f"{seed}{ {'--wide': 'w', '--words': 't'}.get(draws, '')}"
    out = [f"# Random layouts, seed {tag} (tools/random_layouts.py).", "# chrome 0", ""]
    for i in range(count):
        tree = generator.node(0, root=True)
        out += [render(f"random_{tag}_{i}", "500x400", flatten(tree)), ""]
    return "\n".join(out)


class Reducer:
    """A copy of the tree with the fixture test built, run on candidates."""

    def __init__(self, work):
        self.source = os.path.join(work, "src")
        self.build = os.path.join(work, "build")
        files = call(["git", "-C", ROOT, "ls-files", "-z"]).split("\0")
        for name in filter(None, files):
            target = os.path.join(self.source, name)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if os.path.isfile(os.path.join(ROOT, name)):
                shutil.copy2(os.path.join(ROOT, name), target)
        self.corpus = os.path.join(self.source, "test", "layout")
        for name in os.listdir(self.corpus):
            os.remove(os.path.join(self.corpus, name))
        self.write([])
        options = ["TEXT", "ATSPI", "ACCESS_TREE", "BUILD_BENCH", "BUILD_SAMPLES"]
        call(["cmake", "-S", self.source, "-B", self.build, "-DCMAKE_BUILD_TYPE=Debug"] +
             [f"-DMAUL_UI_{option}=OFF" for option in options])

    def write(self, candidates, available="500x400"):
        text = ["# chrome 0", ""]
        for i, nodes in enumerate(candidates):
            text += [render(f"cand_{i}", available, nodes), ""]
        with open(os.path.join(self.corpus, "cand.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(text))

    def run(self, candidates, available):
        """The indices of the candidates that part from Chrome, and the output."""
        self.write(candidates, available)
        tool = os.path.join(self.source, "tools", "gen_layout_fixtures.py")
        call([sys.executable, tool, "--oracle"], cwd=self.source)
        call(["cmake", "--build", self.build, "--target", "test_layout_fixtures"])
        test = os.path.join(self.build, "test_layout_fixtures")
        result = subprocess.run([test], capture_output=True, text=True)
        output = result.stdout + result.stderr
        failing = {int(m.group(1)) for m in re.finditer(r"^cand_(\d+) ", output, re.M)}
        return failing, output


def candidates(nodes):
    """Each node's subtree dropped, then each key of each node dropped."""
    for i in range(1, len(nodes)):
        end = i + 1
        while end < len(nodes) and nodes[end][0] > nodes[i][0]:
            end += 1
        yield nodes[:i] + nodes[end:]
    for i, (depth, keys) in enumerate(nodes):
        for k in range(len(keys)):
            yield nodes[:i] + [(depth, keys[:k] + keys[k + 1:])] + nodes[i + 1:]


def reduce(path, name):
    text = open(path, encoding="utf-8").read()
    match = re.search(r"^fixture " + re.escape(name) + r" available=(\S+)\n(.*?)(?:\n\n|\Z)",
                      text, re.S | re.M)
    if match is None:
        sys.exit(f"random layouts: no fixture {name} in {path}")
    available = match.group(1)
    nodes = []
    for line in match.group(2).splitlines():
        if line.strip().startswith("node"):
            depth = (len(line) - len(line.lstrip(" "))) // 2
            nodes.append((depth, line.split("=>")[0].split()[1:]))
    with tempfile.TemporaryDirectory(prefix="mui-reduce-") as work:
        reducer = Reducer(work)
        failing, output = reducer.run([nodes], available)
        if failing != {0}:
            sys.exit(f"random layouts: {name} agrees with Chrome")
        while True:
            options = list(candidates(nodes))
            failing, _ = reducer.run(options, available) if options else (set(), "")
            if not failing:
                break
            nodes = options[min(failing)]
        _, output = reducer.run([nodes], available)
        print(open(os.path.join(reducer.corpus, "cand.txt"), encoding="utf-8").read())
        print(output)


def main():
    args = sys.argv[1:]
    if len(args) in (3, 4) and args[0] == "generate" and args[3:] in ([], ["--wide"], ["--words"]):
        print(generate(int(args[1]), int(args[2]), "".join(args[3:])))
    elif len(args) == 3 and args[0] == "reduce":
        reduce(args[1], args[2])
    else:
        sys.exit("usage: random_layouts.py generate SEED COUNT [--wide | --words] | reduce FILE NAME")


if __name__ == "__main__":
    main()
