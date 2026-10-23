# The web

The library runs in browsers built two ways: with Emscripten, and with a plain WebAssembly toolchain, Clang's `wasm32-wasi` with wasi-libc, as Rawframe's engine is built. The core is plain C and needs nothing of either. The ARIA adapter is the one part that talks to the page, since a browser's accessibility tree is reachable only through the DOM ([Accessibility](accessibility.md#the-adapters)).

## One set of JavaScript

The page's side of the ARIA adapter is written once, as `EM_JS` functions in [aria_page.c](../../src/aria_page.c), which Emscripten compiles into the program. A `wasm32-wasi` build has nothing that compiles JavaScript into a module, so the same source serves it through a generator, as Maul Window's web backend does:

- [web_js.h](../../src/web_js.h), the one file of the library that includes Emscripten's headers, defines `EM_JS` for a build without Emscripten as the declaration of a function imported under its own name from the module `env`.
- [gen_web_glue.py](../../tools/gen_web_glue.py) reads the same files and writes `maul-ui.mjs`, whose one export, `maulUiImports(exports, module)`, returns those functions for the page to put among its `env` imports. Their names start with `muiAriaJs`, so they meet no other library's there. The build writes the file beside the library, and the library's `MAUL_UI_WEB_GLUE` property names it ([Web.cmake](../../cmake/Web.cmake)).
- The functions the page calls back are exported by `MUI_WEB_EXPORT`: Emscripten's `EMSCRIPTEN_KEEPALIVE`, or `export_name` under the function's own name. The generated file gives each JavaScript body the export it calls under Emscripten's name, and makes its views of memory again after the call, since a call that allocates may grow memory, which Emscripten's views follow by themselves.

The JavaScript therefore cannot drift between the two builds: the generator makes the second form of it, and a construct it does not understand fails the build of the glue, not the page. A page loads `maul-ui.mjs` beside Maul Window's and exports the table, lines a plain WebAssembly program's loader has anyway.

## Building and testing

[wasm32-wasi.cmake](../../cmake/wasm32-wasi.cmake) builds the library and its tests for `wasm32-wasi`, and the tests run under Node's WASI ([wasi_run.mjs](../../test/wasi_run.mjs)). The text component is left out there: FreeType needs `setjmp`, which wasi-libc as packaged lacks, so a host that builds text for `wasm32-wasi` supplies its own.

The ARIA test builds both ways and runs in headless Chrome through [web_runner.cjs](../../test/web_runner.cjs), comparing the browser's accessibility tree. Without Emscripten it is a reactor whose `main` the runner calls, with a minimal WASI and the generated imports, its waits suspending through JavaScript promise integration where Emscripten's build waits through Asyncify. That needs Chrome 137 or later.

A default context holds several megabytes, more than the 16 MiB Emscripten starts with when a test holds several, so Emscripten test programs grow their memory as a page does.
