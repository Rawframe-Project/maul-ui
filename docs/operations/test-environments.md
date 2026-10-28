# Test environments

Most tests need nothing but the library. The rest drive something real: a GPU, a bus, an operating system's accessibility API, a browser, an emulator, a simulator or a screen reader. Those carry the `integration` label. This page says what each environment is and how to run it by hand.

## Contents

- [Vulkan on lavapipe](#vulkan-on-lavapipe)
- [The window glue's test backend](#the-window-glues-test-backend)
- [AT-SPI on a private bus](#at-spi-on-a-private-bus)
- [Windows, macOS and iOS](#windows-macos-and-ios)
- [Android](#android)
- [The web](#the-web)
- [Screen readers](#screen-readers)

## Vulkan on lavapipe

The renderer's pixel test and the samples draw on a real device. On Linux that is lavapipe, Mesa's software Vulkan driver. The `linux-parts` test preset sets `MUI_RHI_REQUIRED=1`, so a missing device fails instead of skipping, and `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, and CTest fails a test whose output holds a validation error. Without a device and without `MUI_RHI_REQUIRED`, the pixel test exits 77 and counts as skipped; every CI cell fails on a skipped test. `test_rhi_renderer` and `test_rhi_pack` run on Maul RHI's test driver and need no device.

Mesa's Vulkan driver leaks a few allocations of its own when it is unloaded, which LeakSanitizer reports from a module it can no longer name. The sanitized renderer and sample tests therefore run without leak detection, which the core's own sanitized suite keeps on.

## The window glue's test backend

The glue's tests and the samples run on Maul Window's headless test backend, which the build fetches with Maul Window: no window system is needed, and a sample's frames are checked in the sample itself (`--headless`). On the backend there is no accessibility adapter, so the glue keeps the tree alone and the tests read it.

## AT-SPI on a private bus

`test_atspi` starts a private `dbus-daemon` and plays both the AT-SPI registry and a client against the adapter, so nothing on the machine's own buses is touched. It needs `dbus-daemon` and libdbus-1, which the adapter opens at run time.

## Windows, macOS and iOS

- **Windows:** `test_uia` drives the adapter through `IUIAutomation` from a second thread, as a screen reader does, on the runner's desktop. Run it with the `windows-clang-cl` presets in a desktop session.
- **macOS:** `test_nsaccessibility` asks the adapter's objects as the accessibility server does, since the client API needs a trusted process, and records the notifications in place of AppKit's. `macos-clang` presets.
- **iOS:** `cmake/ios-simulator.cmake` builds for the simulator. Headless tests run through `simctl spawn`, and `test_uiaccessibility` is an application run by `tools/run_ios_app.sh`, which installs and launches it, waits for its closing line, and shows where a stuck application is. Boot a simulator, set `MUI_IOS_DEVICE` to its UDID, and use the `ios-simulator` presets. A new device's first launch takes minutes; CI pays it once before the tests.

## Android

`cmake/android-emulator.cmake` wraps the NDK's toolchain file. Executables run through `tools/run_android.sh` (`adb push`, `adb shell`). `test_android_access` is an application built without Gradle by `tools/build_android_app.sh` (javac, R8 with the library's keep rules, aapt2, zipalign, apksigner) and run by `tools/run_android_app.sh`, which installs and starts it and waits for its result line. With an emulator booted, `ANDROID_HOME` and `ANDROID_NDK_HOME` set and `ANDROID_SERIAL` naming it, use the `android-emulator` presets. Stop the emulator when the run ends.

## The web

`web-wasi` tests run under Node's WASI ([wasi_run.mjs](../../test/wasi_run.mjs)), and `web-emscripten` tests under Node. The ARIA test, the renderer's pixels on WebGPU and the samples run in headless Chrome through puppeteer ([web_runner.cjs](../../test/web_runner.cjs)), found through `MUI_NODE_MODULES`; without it they exit 77. The runner serves the build, gives each test a page and carries out what it asks: read the accessibility tree under an element, read an element's box, press an element. `test/browser/` pins puppeteer and its dependencies in a lockfile:

```sh
mkdir -p out/browser && cp test/browser/package*.json out/browser/ && npm ci --prefix out/browser
MUI_NODE_MODULES=$PWD/out/browser/node_modules ctest --preset web-wasi
```

## Screen readers

The samples are walked by screen readers from the steps files in `tools/screen_reader/`, each walk checking that the reader said the phrases its file expects, in the order the file lists them, and, where a file says a phrase is heard, that the reader said it in reply to the step before:

- **Orca**, locally: `tools/screen_reader_walk.sh <sample> <steps> <out-dir>` runs a sample built with `MAUL_UI_WINDOW` and `MAUL_UI_ATSPI` under Xvfb on a private session bus, with Orca writing what it would say to its log and xdotool pressing the keys. It needs Xvfb, `dbus-run-session`, `at-spi-bus-launcher`, Orca and xdotool.
- **VoiceOver and NVDA** on the macOS and Windows runners, and **NVDA over the ARIA adapter** in Chrome, through Guidepup and `tools/screen_reader/walk.mjs`, in the nightly run (`.github/workflows/screen-readers.yml`). `tools/screen_reader/` pins Guidepup and puppeteer in a lockfile: `npm ci --prefix tools/screen_reader`.

A screen reader's speech depends on timing that a busy machine changes, so a walk is not a pull request check and is never retried: a walk that misses a phrase is investigated and recorded in the walks' tracking issue.
