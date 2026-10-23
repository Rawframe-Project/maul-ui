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

GCC 13 and older are refused: the library is C23. The text component fetches FreeType, HarfBuzz and Maul Unicode at configure time; [Getting started](../user/getting-started.md#build-options) says how to build offline.

## Building and running tests

Every CI cell is a preset in `CMakePresets.json` with a configure, build and test step of the same name, built in `out/<preset>`:

```sh
cmake --preset linux-clang-20-asan
cmake --build --preset linux-clang-20-asan
ctest --preset linux-clang-20-asan                    # the whole suite
ctest --preset linux-clang-20-asan -L text_editor     # one suite, by its label
ctest --test-dir out/linux-clang-20-asan -LE integration   # what needs no device, bus or browser
```

Add a CI cell as a preset, never as commands in a workflow. `linux-parts` builds the reference renderer and the window glue with the samples, on Maul Window's headless test backend, and runs the renderer's pixels and the samples on lavapipe. [Test environments](test-environments.md) covers the presets of the other platforms. `tools/check.sh` runs the Linux checks in tiers: `changed` before every push, `pr` and `full` through the workspace's heavy-job wrapper on the shared development machine.

## Generated files

These are committed, because a build must not need their generators' tools:

| File | Generator | From |
|---|---|---|
| `test/generated/layout_fixtures.h` | `python3 tools/gen_layout_fixtures.py` | `test/layout/*.txt`; CI checks that it is current |
| `src/generated/inflate_tables.h` | `python3 tools/gen_inflate_tables.py` | RFC 1951's fixed codes; a family file |
| `rhi/shaders/quad_container.h` | `python3 tools/gen_rhi_shaders.py <maul-rhi source tree>` | `rhi/shaders/quad.*`, with glslang, SPIRV-Tools, SPIRV-Cross and dxc |
| `test/fonts/Maul*.ttf`, `MaulCff.otf` | `test/fonts/make_*.py` | fontTools (and Pillow for the bitmap fonts) |
| `test/images/pngs.bin` | `test/images/make_pngs.py` | Pillow |

Each generator writes the same bytes on every run. A change to a generated file names the generator or input change that caused it.

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

`python3 tools/size_report.py <build>` measures the wasm a program links of the core and of the text component against their ceilings ([Overview](../internals/overview.md#profile)). It needs an Emscripten build at `-Oz` with link-time optimization; its header gives the configure line.
