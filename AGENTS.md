# Maul UI

Maul UI is a retained UI core in C: the host keeps its interface as a tree of nodes in a context, writes sizes, styles and content, and reads back rectangles, a draw-command list and accessibility updates. Flexbox layout, style with transitions, a text component on FreeType and HarfBuzz, input, focus and scrolling, virtual lists, and accessibility adapters for six platforms; an optional reference renderer on Maul RHI and a glue to Maul Window sit beside it. It is part of the Maul family; the family rules are in [docs/internals/maul-conventions.md](docs/internals/maul-conventions.md), and [docs/internals/overview.md](docs/internals/overview.md) explains how the parts fit.

It never compromises on:

1. **Same bits everywhere.** The same tree and inputs give the same layout, style, draw-list bytes, accessibility tree, shaped text and glyph images on every platform and compiler.
2. **A static frame costs nothing.** No stage visits a node no edit reached, and a frame allocates nothing: memory is reserved by the limits of a def.
3. **The host owns its program.** The library opens no window, starts no thread, owns no GPU, and calls host code only inside the call that needs it. No public identifier names an engine's concepts.
4. **Hostile input.** Every argument, font and byte from another program is validated and refused with a status in every build.
5. **Behave as the platforms do.** Layout agrees with Chrome; input, focus, editing and accessibility follow the platform or browser behaviour named in the internals pages, not an invention of the library.

## Commands

```sh
tools/check.sh changed          # before every push: rules, format, docs, the tests the changes reach (< 2 min)
tools/check.sh pr               # what the pull request check runs on Linux (< 10 min)
tools/check.sh full             # adds TSan, shared, the core alone, the parts without text, Release builds, longer fuzzing (< 20 min)
cmake --preset linux-clang-20-asan && cmake --build --preset linux-clang-20-asan   # one CI cell
ctest --preset linux-clang-20-asan -L text_editor     # one suite, by label
ctest --test-dir out/linux-clang-20-asan -LE integration   # what needs no device, bus or browser
python3 tools/gen_layout_fixtures.py                  # after editing test/layout/*.txt
python3 /home/rawframe-project/.github/maul/family_sync.py --check maul-ui   # shared files
```

Run the `pr` and `full` tiers through the workspace's heavy-job wrapper on the shared machine, with gcc-14 or clang-20; the machine's default gcc is too old for C23. Every CI cell is a preset in `CMakePresets.json` (configure, build and test of one name, built in `out/<preset>`); add a cell there, never as commands in a workflow. `linux-parts` builds the renderer, the window glue and the samples. The macOS, iOS, Android, Windows and web cells cannot run on Linux: [docs/operations/test-environments.md](docs/operations/test-environments.md) says how they run.

## Ways to hurt yourself

- **Killing by pattern.** Never `pkill -f`, `pgrep | kill` or `kill` a PID found by matching a name or path: other sessions build, test and run Xvfb, dbus-daemon, emulators, browsers and fuzzers on the same machine. Kill only a PID you started.
- **Touching a real session.** Tests start their own `dbus-daemon`, and the Orca walk its own Xvfb and session bus. Never point a test or a walk at a developer's `DISPLAY` or session bus, and never run the Windows tests on a desktop someone uses.
- **Leaving emulators and simulators running.** Stop the Android emulator or iOS simulator you booted when the run that needed it ends.
- **Endless fuzzing.** A libFuzzer target runs until it finds something. Always pass `-max_total_time`.
- **Editing generated files.** `test/generated/layout_fixtures.h` comes from `test/layout/*.txt`, `src/generated/inflate_tables.h` from `tools/gen_inflate_tables.py`, `rhi/shaders/quad_container.h` from `rhi/shaders/quad.*`, and the test fonts and `test/images/pngs.bin` from the scripts beside them. Change the source and run the generator ([development.md](docs/operations/development.md#generated-files)).
- **Hand-writing a layout expectation.** The rectangles in `test/layout/*.txt` are Chrome's, written by `gen_layout_fixtures.py --oracle`. Never edit one to make a test pass; a fixture the solver gets wrong is a solver bug.
- **Editing shared family files.** These are synced from the canonical copies in the organization's `.github` repository (`maul/`) and fail CI when they differ: `docs/internals/maul-conventions.md`; `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`; `cmake/CompilerFlags.cmake`, `cmake/Install.cmake`, `cmake/Library.cmake`, `cmake/ReadVersion.cmake`, `cmake/Testing.cmake`, `cmake/Config.cmake.in`, `cmake/pkg-config.pc.in`, `cmake/HeaderCheck/CMakeLists.txt`; `bench/CMakeLists.txt`; `test/test_harness.h`; `src/inflate.c`, `src/inflate.h`, `test/test_inflate.c`, `src/generated/inflate_tables.h`, `tools/gen_inflate_tables.py`; and `tools/build_docs.py`, `check_docs.py`, `check_guide.py`, `check_lengths.py`, `check_modules.py`, `check_source.py`, `gen_api.py`. Change the canonical copy first, through the manager.
- **A Java member reached by name without a keep rule.** The Android adapter finds `java/maul/ui` classes and members through JNI. A new one needs a rule in `java/proguard-rules.pro`; `test_android_access` is shrunk by R8 and fails without it.
- **Including a UI Automation header.** The Windows SDK's headers define const variables that a C program including them as well could not link with. The adapter declares what it uses itself, and `test_uia` checks those declarations against the SDK.

## Invariants

- **Parts and direction.** No core module includes a text module, and `tools/text-headers.txt` names the text component's public headers. `rhi/` and `window/` are libraries of their own that include only public headers; the core and the text component never include them. `tools/modules.txt` lists which module may include which, and `tools/check_modules.py` enforces it.
- **Determinism.** Geometry is binary32 with no contraction. Only the `<math.h>` functions in `tools/libm-allowed.txt` are called; anything else (exponentials, powers, roots, trigonometry) comes from `src/motion_math.c`. Time enters only as timestamps the host passes. No result depends on pointer values, hash iteration or allocation order.
- **Memory.** Running a frame (style, layout, emission, input) never allocates; a new bound is a named limit in a def, with a default. The text service may grow its scratch to the largest text it has seen. HarfBuzz allocates from the C library, the one exception.
- **Host code.** A measure, baseline, paint, access text or allocator function the context is running may read it but not change it: such a call answers `mui_errorState` and counts misuse (`src/host_call.h`). The event function may edit the tree, focus and capture, but not feed input.
- **Draw list bytes.** Commands are fixed-size records zeroed before they are written; equal trees give equal bytes. A change to what a command means changes the renderer in `rhi/` in the same pull request.
- **Assertions:** `MUI_ASSERT` (`src/invariant.h`) checks internal invariants only. It traps in builds without `NDEBUG` (Debug, sanitizers, fuzzing) and compiles out of Release, whose per-node loops must not pay for it. Never assert on caller input, a font or another program's bytes: validate and return a status. Library code never calls `assert` or `abort`.
- **Public headers** are the common subset of C17 and C++17 and declare only the API; every public function has a `///` block with `@param`, `@return` and a thread safety paragraph from the list in `tools/check_docs.py`. No identifier names an engine concept (`tools/check_litmus.py`).
- **Library version:** `MUI_VERSION_*` between the release-please markers in `include/maul-ui/base.h`; CMake reads it there and `MUI_ABI_VERSION` follows from it. Only release-please changes it. The context def and the text service def are the root defs and the only ones that carry the ABI version.
- **Web JavaScript** exists once, as `EM_JS` functions in `src/aria_page.c`; `tools/gen_web_glue.py` makes the wasm32-wasi imports from them. Every JavaScript function name starts with `muiAriaJs`.
- **Dependencies:** FreeType and HarfBuzz are pinned by version and SHA-256, and Maul Unicode, Maul Window and Maul RHI by release tag, in `cmake/`. A new pin is a pull request of its own that names what changed.
- **Workflows:** every action is pinned by commit SHA with its version in a comment, every job declares the least `permissions` it needs, and no job installs packages the CI image could carry.

## Testing

Follow the [repository standard's Tests section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#tests); in this repository:

- Test through the public headers. `WHITEBOX` suites are for a unit with a contract of its own (a decoder, a packer, a table), never for a module's private state.
- A layout change is proven by fixtures in `test/layout/` rendered by Chrome, not by hand-written rectangles. Where Chrome and Firefox part, say which one the fixture follows.
- A test that drives a real device, bus, platform API, browser, emulator or simulator carries the `integration` label. A test that cannot run exits 77; CI fails on any skipped test, so a skip is only for an environment genuinely missing locally.
- The renderer's pixel tests and the samples run on lavapipe under the Vulkan validation layer; any "Validation Error" fails them.
- Golden draw lists are few and pin the list's format; check other drawing by the properties that matter (a rectangle, a clip, a color).
- Every C snippet of `docs/user/` appears unchanged in `test/test_guide*.c`; `tools/check_guide.py` enforces it.
- A fuzz finding becomes a test in its module's suite with the minimized input.
- No sleeps, no wall clock and no retries in tests: input and transitions take the times the test passes.

## Documentation

Follow the [repository standard's Documentation section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#documentation). A change in what a caller must do or can rely on updates the page of `docs/user/` for that part and, if it is in a snippet, the matching code in `test/test_guide*.c`; a change in how a part works or why updates its `docs/internals/` page. Algorithms from the literature are cited where the internals page explains them. The API reference is generated from the headers into the documentation site; never commit it.

## Pull requests

Titles are conventional commits. Scopes: `tree`, `layout`, `style`, `draw`, `text`, `edit`, `glyph`, `input`, `focus`, `scroll`, `virtual`, `access`, `uia`, `atspi`, `aria`, `macos`, `ios`, `android`, `web`, `rhi`, `window`, `samples`, `build`, `bench`, `tools`, `family`. Example: `fix(layout): break a wrapping column at its pixel maximum`.

A change to generated files names the generator or input change that caused it. A change of results (a layout, a draw list's bytes, shaped text, a glyph image) says who sees it.
