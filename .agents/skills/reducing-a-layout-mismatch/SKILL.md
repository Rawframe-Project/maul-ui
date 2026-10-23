---
name: reducing-a-layout-mismatch
description: Reduces a layout where Maul UI's flex solver and Chrome disagree to a smallest fixture, checks Firefox, turns it into a corpus fixture and drives the fix. Use when test_layout_fixtures fails, when random layouts part from Chrome, or when a user reports a layout that differs from a browser.
---

# Reducing a layout mismatch

The layout corpus in `test/layout/*.txt` holds fixtures with Chrome's rectangles, and `test_layout_fixtures` compares the solver with them within 1/32 unit. A mismatch is a solver bug unless the browsers themselves disagree. This procedure finds the smallest case, decides which, and leaves a fixture that guards the fix.

## Setup

Rendering needs Node and puppeteer, from the lockfile:

```sh
mkdir -p out/browser && cp test/browser/package*.json out/browser/ && npm ci --prefix out/browser
export MUI_NODE_MODULES=$PWD/out/browser/node_modules
npx --prefix out/browser puppeteer browsers install firefox   # prints the Firefox executable's path
```

The reducer also builds the fixture test itself, so it needs CMake and a C23 compiler (`CC=clang-20` or `CC=gcc-14` on the shared machine). Run long reductions through the workspace's heavy-job wrapper.

## Steps

1. **Get the case into the corpus format.** A failing fixture already is. Random layouts come from `python3 tools/random_layouts.py generate SEED COUNT [--wide | --words | --deep | --reach | --scroll | --placed | --ratio | --limits]`; put them in a scratch file under `test/layout/`. HTML from a user becomes a fixture by hand: one `node` line per element with the corpus keys (the header of `tools/gen_layout_fixtures.py` lists them), and `=> 0 0 0 0` for the rectangles.
2. **Render Chrome's rectangles:** `python3 tools/gen_layout_fixtures.py --oracle`. It rewrites the expectations of every fixture in the corpus and regenerates `test/generated/layout_fixtures.h`. Check that `git diff test/layout` touches only the fixtures you meant to render; any other change means Chrome moved, which is a separate pull request.
3. **See it fail:** build and run `test_layout_fixtures` (`ctest --preset linux-clang-20-asan -L layout_fixtures`). The output names each fixture and node that parts.
4. **Reduce:** `python3 tools/random_layouts.py reduce test/layout/<file>.txt <name> --firefox <path>`. It drops subtrees and keys while the same node and number still part from Chrome, wherever Firefox agrees with Chrome, and prints the reduced fixture with Chrome's rectangles and the solver's answers. Without `--firefox` a reduction can slide into a case where the engines part, which proves nothing.
5. **Decide.**
   - If Chrome and Firefox agree and the solver differs, it is a solver bug. Keep the reduced fixture.
   - If the reducer says the case agrees "wherever Firefox does", the engines part on it. The solver may follow either; record which one in the fixture's comment, and keep the fixture only if it pins a choice worth keeping.
6. **Add the fixture** to the corpus file of its feature (`aspect.txt`, `wrap.txt`, `intrinsic.txt` and so on), under a comment that says in plain words what the case shows and which engines agree. Do not cite issue or research numbers. Never edit the rectangles by hand.
7. **Fix the solver**, in the module the reduced case points at ([docs/internals/layout.md](../../../docs/internals/layout.md) maps the modules). Write the rule as CSS states it, not as a patch for the case.
8. **Regress against the whole corpus:** `python3 tools/gen_layout_fixtures.py` (tables only), then the full `test_layout_fixtures` and `layout` suites. A fix that breaks saved random fixtures (`random*.txt`) is wrong for those cases, even if it fixes the new one. Look for one rule that answers all of them before changing two.
9. **Commit** the fixture and the fix together. The pull request title is `fix(layout): ...` and its body names the reduced case and what each engine gives.

## Edge cases

- **A reduction that slides.** The reducer keeps a candidate only while one of the original parting nodes still parts with the same number, but a candidate can still reach a different rule. Compare the final case with the original before trusting it, and reduce again from a hand-trimmed start if needed.
- **Text.** Fixtures with `content=WxH*N` are words that wrap greedily, not real text. A mismatch that needs real shaping is not a layout fixture; test it in the text suites.
- **Scroll extents.** For a scroll container the oracle reads `scrollWidth` and `scrollHeight`; a parting extent is a fixture like any other.
- **Chrome updates.** The `# chrome <version>` line of each file records the browser. A new Chrome that moves rectangles across the corpus is its own pull request, with every moved fixture explained.
