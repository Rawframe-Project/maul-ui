// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs a test built for wasm32-wasi under Node's WASI
// (cmake/wasm32-wasi.cmake), as Maul Window's does (mui-0009). The ARIA
// test needs a page and runs in a browser instead (web_runner.cjs); here
// the page's imports fail, since nothing reaches them without one.
//
// usage: node wasi_run.mjs <test.wasm> [arguments]

import {readFile} from 'node:fs/promises';
import {WASI} from 'node:wasi';
import {argv, env, exit} from 'node:process';

const wasi = new WASI({version: 'preview1', args: argv.slice(2), env, returnOnExit: true});
const module = await WebAssembly.compile(await readFile(argv[2]));
const page = new Proxy({}, {
    get: (target, name) => () => {
        throw new Error(`${String(name)} needs a page`);
    },
});
const instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport, env: page});
exit(wasi.start(instance));
