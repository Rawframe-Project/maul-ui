# mui-0009. The web without Emscripten

Status: Accepted

## Context

The ARIA adapter (mui-0008) is built with Emscripten: its JavaScript is
in `src/aria_page.c` as `EM_JS` functions, which Emscripten compiles
into the program, and the page calls the program back through
functions Emscripten exports by its own names (`_muiAriaEventFromPage`).

Programs built for the web with a plain WebAssembly toolchain (Clang's
`wasm32-wasi` and wasi-libc, as Rawframe's engine is) have neither:
nothing compiles JavaScript into such a module, and its exports carry
the names its link gives them. Maul Window (mwin-0022) and Maul RHI
(mrhi-0016) already serve those programs; without the same here, such
a program's UI reached no screen reader in a browser.

## Decision

- **One set of JavaScript, as Maul Window's.** The `EM_JS` functions
  stay the only copy of the page's side. `src/web_js.h`, the one file
  of the library that includes Emscripten's headers, defines `EM_JS`
  for a build without Emscripten as the declaration of a function
  imported under its own name from the module `env`.
  `tools/gen_web_glue.py`, Maul Window's generator with this library's
  names, reads the same files and writes `maul-ui.mjs`, whose one
  export, `maulUiImports(exports, module)`, returns those functions for
  the page to put among its `env` imports. Their names start with
  `muiAriaJs`, so they meet no other library's there. The build writes
  the file beside the library; the library's `MAUL_UI_WEB_GLUE`
  property names it.
- **The calls back.** The functions the page calls are exported by
  `MUI_WEB_EXPORT`: Emscripten's `EMSCRIPTEN_KEEPALIVE`, or
  `export_name` under the function's own name. The generated file
  gives the bodies each one they call under Emscripten's name
  (`_muiAriaTextFromPage` calls the export `muiAriaTextFromPage`), and
  makes its views of memory again after the call, since a call that
  allocates may grow memory, which Emscripten's views follow by
  themselves.
- **The component** is on by default for both toolchains.
- **Tests.** `cmake/wasm32-wasi.cmake` builds the library and its
  tests for `wasm32-wasi`; the tests run under Node's WASI
  (`test/wasi_run.mjs`). The ARIA test builds both ways: without
  Emscripten it is a reactor whose `main` the runner calls in headless
  Chrome, with a minimal WASI and the generated imports of the library
  and the test, its sleeps suspending through JavaScript promise
  integration where Emscripten's build sleeps through Asyncify. The
  CI cell `web-wasi` runs them, the text component left out: FreeType
  needs `setjmp`, which wasi-libc as packaged lacks, so a host that
  builds text for `wasm32-wasi` supplies its own.

## Consequences

- The JavaScript cannot drift between the two builds: the generator is
  what makes the second form of it, and a construct it does not
  understand fails the build of the glue, not the page.
- A page loads `maul-ui.mjs` beside Maul Window's and exports the
  table: lines a plain WebAssembly program's loader has anyway.
- A test that sleeps without Emscripten needs a browser with
  JavaScript promise integration (Chrome 137 and later).
