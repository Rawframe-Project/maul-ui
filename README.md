# Maul UI

An engine-agnostic retained UI core in C: layout, style, text, input, accessibility and a draw-command list.

[![CI](https://github.com/Rawframe-Project/maul-ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Rawframe-Project/maul-ui/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/github/license/Rawframe-Project/maul-ui)](LICENSE)
[![Release](https://img.shields.io/github/v/release/Rawframe-Project/maul-ui)](https://github.com/Rawframe-Project/maul-ui/releases)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/Rawframe-Project/maul-ui/badge)](https://scorecard.dev/viewer/?uri=github.com/Rawframe-Project/maul-ui)

A game, an engine or an application needs an interface that lays out like the web, looks the same on every machine, answers a mouse, a touch screen, a keyboard and a gamepad, and reads to a screen reader, without taking over its window, its renderer or its main loop. Maul UI is that interface's core. The program keeps its interface as a tree of nodes in a context, writes sizes, styles and content, and reads back rectangles and a list of draw commands for its own renderer.

- **Layout:** CSS flexbox with wrapping, gaps, aspect ratios, absolute placement, baselines, right-to-left mirroring and safe areas, checked against Chrome on a corpus of over a thousand layouts.
- **Style:** classes and node types with state variants, conditions on size and environment, design tokens and scoped themes, and transitions, timed or spring, in a fixed order of layers with no cascade to reason about.
- **Text:** shaping, bidirectional text, font fallback, families and variable fonts, rich-text spans, and fields with selection, undo, input methods and passwords; glyph images as coverage, multi-channel distance fields and colour.
- **Input:** hit testing through rounded clips and scrolling, hover and press, focus for up to eight players with keys and gamepads, drags and drops, sliders, scrolling with flings, popups, and exits that play out before a node goes.
- **Lists:** virtual lists of a hundred thousand items that keep only what is near the viewport.
- **Accessibility:** an accessibility tree with adapters for UI Automation, AT-SPI, NSAccessibility, UIAccessibility, Android and the web's ARIA.
- **Drawing:** a renderer-agnostic draw list with clip chains and transforms that batch well, the same bytes on every platform, and an optional reference renderer on Maul RHI.

A screen that does not change costs no work per frame. The library starts no thread, allocates nothing during a frame, and calls the program back only inside the call that needs it.

## Quick start

It builds with CMake 3.25 or newer and GCC 14 or newer, Clang 19 or newer, or Apple Clang. On Linux and macOS:

```sh
git clone https://github.com/Rawframe-Project/maul-ui.git
cd maul-ui
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Ubuntu 24.04's default compiler is GCC 13, which the build refuses ("GCC 14 or newer is required for C23"). Install GCC 14 and configure again in a fresh build directory, since `build/` keeps the compiler it first found; then go on from `cmake --build build`:

```sh
sudo apt install gcc-14
rm -rf build && CC=gcc-14 cmake -B build -DCMAKE_BUILD_TYPE=Release
```

On Windows the library builds with `clang-cl`, and on the web with Emscripten; [Getting started](docs/user/getting-started.md#requirements) lists what each needs.

Install it with `cmake --install build --prefix <prefix>` and use it from CMake:

```cmake
find_package(maul-ui REQUIRED CONFIG)
target_link_libraries(app PRIVATE maul-ui::maul-ui)
```

or through pkg-config as `maul-ui`. The core fetches Maul Tween at configure time. The text component, on by default, fetches FreeType, HarfBuzz and Maul Unicode and needs a C++ compiler for HarfBuzz; `-DMAUL_UI_TEXT=OFF` builds the core alone. To see it in a window, build the samples with `-DMAUL_UI_WINDOW=ON -DMAUL_UI_RHI=ON`. [Getting started](docs/user/getting-started.md) has the options and a first screen.

## Platforms

| Platform | Built with | Accessibility |
|---|---|---|
| Windows | `clang-cl` | UI Automation |
| macOS | Xcode 16 | NSAccessibility |
| iOS | Xcode 16 | UIAccessibility |
| Android | NDK r28 | Android accessibility |
| Linux | GCC 14 or Clang 19 | AT-SPI |
| The web | Emscripten, or Clang's wasm32-wasi | ARIA |

CI builds and tests every platform: the desktops on their runners, iOS in the simulator, Android in the emulator, the web under Node and in headless Chrome. The public headers are C17 and C++17, so any C or C++ program can include them; the library itself is C23.

## Documentation

- [The documentation site](https://rawframe-project.github.io/maul-ui/), with the API reference generated from the headers.
- [User documentation](docs/README.md): one page per part of the library.
- [How it works](docs/internals/overview.md) and [how to develop it](docs/operations/development.md).
- [Changelog](CHANGELOG.md).

Maul UI belongs to the Maul family of standalone C libraries, which share one set of conventions. It depends on [Maul Tween](https://github.com/Rawframe-Project/maul-tween) for its curves and springs and on [Maul Unicode](https://github.com/Rawframe-Project/maul-unicode) for text, and its optional parts on [Maul Window](https://github.com/Rawframe-Project/maul-window) and [Maul RHI](https://github.com/Rawframe-Project/maul-rhi).

## Contributing

Issues are welcome. Pull requests are accepted for issues a maintainer has approved; see [CONTRIBUTING.md](https://github.com/Rawframe-Project/.github/blob/main/CONTRIBUTING.md). The project is developed with AI coding agents, directed and reviewed by its maintainers; outside contributions follow the [AI policy](https://github.com/Rawframe-Project/.github/blob/main/AI_POLICY.md).

## License

MIT; see [LICENSE](LICENSE). The text component builds FreeType and HarfBuzz into the library, under their own licenses; [Licenses](docs/user/licenses.md) says what a program that ships it carries.
