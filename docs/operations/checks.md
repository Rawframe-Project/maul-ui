# Checks

What verifies a change, locally and in CI, and the time each may take.

## Contents

- [Locally](#locally)
- [Tests and labels](#tests-and-labels)
- [In CI](#in-ci)
- [Fuzzing](#fuzzing)
- [Budgets](#budgets)

## Locally

`tools/check.sh` runs the Linux checks in three tiers; each includes the one before it. Its builds are the CI cells' presets from `CMakePresets.json`, in `out/<preset>`, and the other trees go to `out/check-*`, each with its log beside it.

| Tier | Runs | Budget |
|---|---|---|
| `changed` | The source, module, length, API documentation, snippet, text header, litmus, float environment and test hygiene rules, the shader container's stamp, the organization's documentation check, `clang-format` on the changed C and Objective-C files, clang-tidy in a Release and a Debug build on the changed C sources of the core, the renderer and the glue that the `pr` tier checks, and the tests whose labels the changed files reach, integration tests left out: the core's in `linux-clang-20-asan`, the renderer's and the glue's in `linux-parts`. | under 2 minutes |
| `pr` | Every rule on every file, the layout fixture tables against the corpus, that the documentation site builds, clang-tidy over the core's, the renderer's and the glue's sources in a Release and a Debug build, the whole `linux-clang-20-asan` and `linux-parts` suites, and 10 seconds of fuzzing per target. | under 10 minutes |
| `full` | Also the other Linux presets (ThreadSanitizer, shared, the core alone, the parts without text and with GCC, Clang and GCC Release) and 60 seconds per fuzz target. | under 20 minutes |

Run `changed` before every push. On the shared development machine, run `pr` and `full` through the workspace's heavy-job wrapper. The macOS, iOS, Android, Windows and web cells run in CI; [Test environments](test-environments.md) says how to run each by hand.

The sanitizer presets build with assertions on at `-O1`, as the sanitizers advise: the seeded simulations and measurements below run several times faster than at `-O0`, and nothing is compiled out.

## Tests and labels

Each suite is `test/test_<name>.c` (or `.m`), one executable registered with CTest as `<name>` and labeled `<name>`, so `ctest --preset <preset> -L <name>` runs one. A suite checks behavior through the public headers; the few that include `src/` (marked `WHITEBOX` in `CMakeLists.txt`) test an internal unit against its own specification, such as the inflater against RFC 1951 or the number formatter against the C library, or reach a state a public call cannot, such as a counter at its last value. A shared build leaves those out.

- `integration`: tests that drive a real device or system: the renderer's pixels and the samples on lavapipe or WebGPU, the AT-SPI adapter on a private bus, the ARIA adapter in headless Chrome, and the Windows, macOS, iOS and Android adapters. They may read the clock, take about a second each, and stay out of the `changed` tier.
- `quarantine`: a test that fails now and then without a fault in the change under test ([Quarantine](#quarantine)). Every test preset and every tier of `tools/check.sh` leave it out.

Some suites are seeded simulations rather than examples: `layout_bound` edits a tree at random and compares the bounded layout with one solved from nothing, `text_fuzz` and `color_glyph` read seeded damaged fonts and random text, and `field_quality` measures distance fields against exact coverage. A seed reproduces a run, so a failure prints its seed and step.

The family's test hygiene check (`tools/check_test_hygiene.py`) rejects a sleep, a wall clock, a retry loop or a skip in the test sources, and a test without a label in a configured build; `check.sh` runs it over the tree and after each preset's configure. A use that must stay, such as an integration test's wait for a browser or a bus, carries `// hygiene: <reason>` (`# hygiene:` in CMake) on its line or the line before.

A test that cannot run exits 77, which CTest reports as skipped, and every CI cell fails on a skipped or unrun test, so a platform that did not start never passes with nothing tested. The renderer's test presets set `MUI_RHI_REQUIRED=1`, so a missing device fails rather than skips, and CTest fails a test whose output holds a Vulkan validation error. A failed test job uploads its CTest logs, kept for a week.

### Quarantine

A failing test is never rerun to pass. One that fails without a fault in the change under test is quarantined, as [maul-conventions.md](../internals/maul-conventions.md) says:

1. An issue records the failure and its run.
2. The test, or the one check that failed split into a test of its own, takes the `quarantine` label: through `maul_add_test(<name> QUARANTINE)`, or `maul_quarantine(<name>)` for a test registered another way.
3. The test presets leave the label out, so no required job runs it, and the `quarantine` job runs it at night through the family's `tools/run_quarantine.sh`. There a failure is a warning and a line in the job summary, while a test that is skipped or not run, a sanitizer report or a crash fails the night.
4. The test leaves quarantine when its issue's fix lands and 14 nights in a row have passed it.

No test is quarantined now.

## In CI

`.github/workflows/ci.yml` runs on every pull request, every push to `main` and every night. The Linux jobs run in the organization's CI image, pinned by digest, and install nothing. The ruleset requires only its `required` job, which passes when every other job passed or was skipped, and the title check.

| Job | What it guards |
|---|---|
| `changes` | Whether a pull request changes anything beyond documentation; when it does not, the build jobs are skipped. |
| `docs` | The organization's documentation check, run from the pinned commit of `.github`, over the whole repository. |
| `zizmor` | The workflows: injection, persisted credentials, permissions, unpinned actions. |
| `checks` | `clang-format`, the family's rules (the float environment rule among them), the text header list, the litmus check, test hygiene and the labels of `linux-clang-20-asan`'s and `linux-parts`' tests, the layout fixture tables, the shader container's stamp, that the documentation site builds, and clang-tidy in a Release build, where the assertions compile out, and a Debug build, with them live. It runs on documentation changes too. |
| `build` | One call of the organization's `cpp.yml` per preset: GCC 14 on x86-64 and on arm64, Clang 20 in Release, Clang with AddressSanitizer and UBSan (each test repeated up to three times on a pull request, so a flaky one fails before it merges), Clang with ThreadSanitizer, the shared library, the core without the text component, the renderer and the glue on lavapipe with Clang and the sanitizers, with GCC 14, and without text, macOS on its runner and `clang-cl` on Windows. All build with warnings as errors. |
| `msvc-headers` | Every public header compiled by MSVC's `cl` as C17 and C++17 with `/W4 /WX`. |
| `fuzz` | Every fuzz target: 20 seconds each on a pull request, 120 on `main` and at night. |
| `samples-x11` | Each sample presented on an X11 window under Xvfb through Maul Window's X11 backend, on lavapipe under the validation layer, its last frame read back and checked. |
| `ios-simulator` | The iOS adapter and its tests built on every pull request; on `main` and at night, run in a booted simulator of the newest runtime. A simulator's boot and first launch take longer than the pull request budget leaves. |
| `android-emulator` | Every test in an Android 11 emulator on the Linux runner with KVM, the accessibility adapter's as an application. |
| `web-wasi` | The core built by Clang for wasm32-wasi in the CI image, the tests under Node's WASI and the ARIA test in headless Chrome. |
| `web-emscripten` | The library, the renderer and the glue built by Emscripten: the tests under Node, the ARIA adapter, the renderer's pixels and the samples in headless Chrome's WebGPU, and the renderer's WGSL. It runs on the runner, which has Emscripten, and hands the built samples to `web-canvas`. |
| `web-canvas` | Each Emscripten sample presenting into a canvas from the browser's frames, its last frame checked, in the CI image, whose lavapipe gives Chrome's compositor the Vulkan it needs. |
| `size` | The wasm each part adds to a program at `-Oz` with link-time optimization; a part over its ceiling fails ([Development](development.md#benchmarks-and-size)). |
| `quarantine` | At night and on a manual run only: the quarantined tests of `linux-clang-20-asan` ([Quarantine](#quarantine)). |

`screen-readers.yml` walks the samples with VoiceOver, NVDA, and NVDA over the ARIA adapter in Chrome every night, outside the required check and never retried; its failures are tracked in one issue ([Test environments](test-environments.md#screen-readers)). `pr-title.yml` checks that the pull request title is a conventional commit with a scope from `AGENTS.md`, and `pr-gate.yml` closes a pull request from outside the organization unless it closes an issue labeled `accepted`. `codeql.yml` runs CodeQL in the CI image, built with GCC 14, on every pull request and on `main`. `scorecard.yml` grades the repository's security practices weekly, and Dependabot proposes updates of the pinned actions and of the browser tests' lockfile once a week.

## Fuzzing

The fuzz targets (`test/fuzz_<area>.c`, built with `-DMAUL_UI_FUZZ=ON` and Clang, the library instrumented too) cover the bytes the library reads from outside: zlib streams, PNG images, font files, text with edits of every kind, and D-Bus calls to the AT-SPI adapter. `fuzz_api` covers the API's own untrusted input: sequences of public calls with ids that may be stale or made up and layout and visual values written raw, given to two contexts alike. Both must answer every call with the same status, and when the second's nodes are all marked changed, the first's bounded layout must give the same rectangles and the same draw list bytes. Each target runs with assertions on and checks properties that hold for any input: no crash, no read or write past a buffer, no undefined behavior, no broken invariant. The first breach of any of them stops the run with the input that caused it. `tools/fuzz_seed.py` writes their seeds; `fuzz_api`'s include one for each family of calls, each ending in that comparison on a painted tree. A crash a fuzzer finds becomes a test in its module's suite ([Development](development.md#a-fuzz-finding)).

## Budgets

Over a budget, tests are pruned or merged before anything else is added. The pull request check's longest jobs are the Android emulator and the Emscripten build; the whole Linux suite runs in under a minute once built. The iOS simulator run, most of it booting, is on `main` and at night.
