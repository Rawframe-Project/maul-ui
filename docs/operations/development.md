# Development

## Contents

- [Toolchain](#toolchain)
- [Building and running tests](#building-and-running-tests)
- [Generated files](#generated-files)
- [Layout fixtures](#layout-fixtures)
- [A fuzz finding](#a-fuzz-finding)
- [Benchmarks and size](#benchmarks-and-size)

## Toolchain

GCC 14 or Clang 20, CMake 3.25, Ninja, Python 3, the pinned `clang-format` and `clang-tidy-20`, and a C++ compiler for HarfBuzz. The renderer's tests need lavapipe and the Vulkan validation layer, and the AT-SPI test needs `dbus-daemon`. The organization's CI image (`ghcr.io/rawframe-project/ci-cpp`) has all of them, so running the checks in it is the surest way to match CI:

```sh
docker run --rm -v "$PWD:/w" -w /w ghcr.io/rawframe-project/ci-cpp:<version> tools/check.sh pr
```

GCC 13 and older are refused: the library is C23. The core fetches Maul Tween and the text component FreeType, HarfBuzz and Maul Unicode at configure time; [Getting started](../user/getting-started.md#build-options) says how to build offline.

## Building and running tests

Every CI cell is a preset in `CMakePresets.json` with a configure, build and test step of the same name, built in `out/<preset>`:

```sh
cmake --preset linux-clang-20-asan
cmake --build --preset linux-clang-20-asan
ctest --preset linux-clang-20-asan                    # the whole suite
ctest --preset linux-clang-20-asan -L text_editor     # one suite, by its label
ctest --test-dir out/linux-clang-20-asan -LE integration   # what needs no device, bus or browser
```

Add a CI cell as a preset, never as commands in a workflow. `linux-parts` builds the reference renderer and the window glue with the samples, on Maul Window's headless test backend, and runs the renderer's pixels and the samples on lavapipe. [Test environments](test-environments.md) covers the presets of the other platforms. `tools/check.sh` runs the Linux checks in tiers ([Checks](checks.md)): `changed` before every push, `pr` and `full` through the workspace's heavy-job wrapper on the shared development machine.

## Generated files

These are committed, because a build must not need their generators' tools:

| File | Generator | From |
|---|---|---|
| `test/generated/layout_fixtures.h` | `python3 tools/gen_layout_fixtures.py` | `test/layout/*.txt`; CI checks that it is current |
| `src/generated/inflate_tables.h` | `python3 tools/gen_inflate_tables.py` | RFC 1951's fixed codes; a family file |
| `rhi/shaders/quad_container.h` | `python3 tools/gen_rhi_shaders.py <maul-rhi source tree>` | `rhi/shaders/quad.*`, with glslang, SPIRV-Tools, SPIRV-Cross and dxc ([the shader container](#the-shader-container)); CI checks its stamp |
| `test/fonts/Maul*.ttf`, `MaulCff.otf` | `test/fonts/make_*.py` | fontTools (and Pillow for the bitmap fonts); [Test fonts](test-fonts.md) describes each |
| `test/images/pngs.bin` | `test/images/make_pngs.py` | Pillow |
| `test/browser/package-lock.json`, `tools/screen_reader/package-lock.json` | `npm install --package-lock-only` in the folder | the `package.json` beside it; Dependabot updates both weekly |

Each generator writes the same bytes on every run. A change to a generated file names the generator or input change that caused it.

### The shader container

CI has none of the shader tools, so it cannot rebuild the container. Instead the header carries a stamp: the SHA-256 of each input as Git stores it (the four `rhi/shaders/quad.*` sources and the generator, with LF line ends), the Maul RHI release, the pinned tools, and the SHA-256 of the container's bytes. `python3 tools/gen_rhi_shaders.py --check` needs no tool. It fails when a source or the generator changed without a regeneration, when `cmake/Rhi.cmake` pins another Maul RHI release, when the stamp names other tools than the pins, or when the container was edited by hand. `--self-test` shows on a copy that each of those fails the check, and that generating refuses what it must. The `checks` job and every tier of `tools/check.sh` run both.

After any such change, regenerate on a machine with the tools. Other versions write other bytes, so the generator compares each tool's version with its pin exactly and refuses any other by name. These are the pins, the same as Maul RHI's:

| Tool | Pinned version | Where it comes from |
|---|---|---|
| `glslangValidator` | 15.1.0 | Ubuntu 24.04's `glslang-tools` (15.1.0-2~ubuntu0.24.04.2) |
| `spirv-link`, `spirv-val` | SPIRV-Tools v2025.1 | Ubuntu 24.04's `spirv-tools` (2025.1~rc1-1~ubuntu0.24.04.2) |
| `spirv-cross` | 2021.01.15+1.3.239.0 | Ubuntu 24.04's `spirv-cross`; it reports no version itself, so the generator reads its package's |
| `dxc` | v1.9.2607: libdxcompiler 1.9.0.1, commit 0d3ee6b5 | the release asset `linux_dxc_2026_07_29.x86_x64.tar.gz` of [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2607), SHA-256 `55665c87824051ed4774ff3280a79ccbbb7d39243b9736ca5e98222134112d54`; check the tarball against it, unpack it anywhere and put its `bin` on `PATH` |
| Maul RHI | the release `cmake/Rhi.cmake` pins | a clean checkout at its tag |

```sh
git clone --depth 1 --branch v0.3.0 https://github.com/Rawframe-Project/maul-rhi /tmp/maul-rhi
PATH=<dxc>/bin:$PATH python3 tools/gen_rhi_shaders.py /tmp/maul-rhi
python3 tools/gen_rhi_shaders.py --check
```

The generator refuses a Maul RHI tree at another tag than the pinned one, at a tag of that name on another commit than the one `RHI_COMMITS` in the generator pins for it, or with uncommitted changes or untracked files, since an edit to its tools changes the bytes while the tag stays. Moving the Maul RHI pin moves that commit too. Its tools then run, in the work directory, from a fresh copy of the release written from the bytes that were checked, each file created anew and every link refused unless it resolves inside the copy, with Python isolated and its bytecode kept out of every source directory, so nothing else in the clone, an ignored module or a `__pycache__`, is ever loaded. It never runs `git status` there: it reads the pinned commit and every tree under it, hashing each object against its id as git does not for trees, compares every file's bytes with them, requires the index to list exactly the release's paths, and lists untracked files by the `.gitignore` files the release commits, so nothing the clone configures (a clean filter, a file system monitor, `.git/info/exclude`, a partial clone's fetch, a replace ref) is run or hides a change. Its git runs without the global or system configuration, so a developer's hooks and ignore rules cannot change what it sees, and trusts the clone by its path, so a clone that belongs to another user, such as one mounted into the CI image, works when named by its top directory. Moving a pin means regenerating with the new tool in a pull request of its own that says why the bytes changed.

The build generates the rest and never commits it: `maul-ui.mjs` for wasm32-wasi builds (`tools/gen_web_glue.py`, [The web](../internals/web.md)), the embedded test fonts, and the API reference for the documentation site (`tools/gen_api.py`, run by `tools/build_docs.py`).

## Layout fixtures

The layout corpus in `test/layout/` holds fixtures with the rectangles Chrome gives them ([Layout](../internals/layout.md#conformance)). Rendering needs Node and puppeteer, which `MUI_NODE_MODULES` names; building and testing need neither.

- **A new fixture:** write it in the corpus format (the header of `tools/gen_layout_fixtures.py`), run `python3 tools/gen_layout_fixtures.py --oracle` to render the corpus in headless Chrome and write the rectangles back, then run `test_layout_fixtures`. Without `--oracle` the script only writes the C tables.
- **Random layouts:** `python3 tools/random_layouts.py generate SEED COUNT [--wide | --words | --deep | --reach | --scroll | --placed | --ratio | --limits]` draws layouts in the corpus format. Put them in a file under `test/layout/`, render it with `--oracle`, and run the fixture test to see which part from Chrome. The ones that agree may stay.
- **A mismatch:** `python3 tools/random_layouts.py reduce FILE NAME --firefox <firefox>` shrinks a fixture that parts from Chrome to a smallest case, checking Firefox too, so the reduction does not slide into a case where the browsers themselves disagree. The `reducing-a-layout-mismatch` skill walks through the whole procedure.

## A fuzz finding

Build the fuzz targets with Clang:

```sh
cmake -B out/fuzz -G Ninja -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DMAUL_UI_FUZZ=ON -DMAUL_UI_BUILD_TESTS=OFF
cmake --build out/fuzz
python3 tools/fuzz_seed.py out/corpus
./out/fuzz/fuzz_text -max_total_time=60 out/corpus/corpus-fuzz_text
./out/fuzz/fuzz_text crash-<hash>      # reproduce one input
```

Always pass `-max_total_time`. A crash becomes a test in its module's suite with the minimized input (`-minimize_crash=1`), then the fix.

## Benchmarks and size

`bench/` holds plain C benchmarks with fixed inputs: layout, styling and draw lists over a list of 10,000 rows and a deep tree (`bench_main.c`), a game HUD's frames, 100,000 virtual rows, and text. They print their timings and are never a CI gate. A change meant to affect performance gives before and after numbers from the same machine in its pull request.

`python3 tools/size_report.py <build>` measures the wasm a program links of the core and of the text component against their ceilings ([Overview](../internals/overview.md#profile)). It needs an Emscripten build at `-Oz` with link-time optimization; its header gives the configure line. CI runs it with `--check`, which fails when a part is over its ceiling, on the Emscripten version the `size` job of `ci.yml` pins, so the sizes move only with the code or with that pin. A pull request that moves the pin gives both parts' sizes before and after. A ceiling is set at the measured size plus about 10% when it is set or raised, so a pin move that shifts the sizes a little passes, and it moves only when a change pushes against it: raised ahead of need, it would hide growth. A change that takes a part past its ceiling first drops what it pulls in that no public function reaches, then raises the ceiling to the new size plus about 10%, saying why in [Overview](../internals/overview.md#profile). The text component's 640,000 was set over 565,136 bytes and stays until a change crosses it.
