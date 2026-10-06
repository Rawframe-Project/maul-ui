// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Compiles WGSL files with headless Chrome's WebGPU, on SwiftShader, and
// fails on any error or warning its compilation reports, so that the
// reference renderer's WGSL (record mui-0005) is checked where nothing
// else compiles it: the shader tools pass WGSL through untouched.
//
//   MUI_NODE_MODULES=<node_modules with puppeteer> node tools/check_wgsl.cjs <file.wgsl>...

'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const puppeteer = require(path.join(process.env.MUI_NODE_MODULES || 'node_modules', 'puppeteer'));

async function main(files) {
    const browser = await puppeteer.launch({
        headless: true,
        args: ['--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
               '--use-webgpu-adapter=swiftshader', '--no-sandbox'],
    });
    // WebGPU needs a secure context: a page from localhost is one.
    const server = http.createServer((request, response) => response.end('<html></html>'));
    await new Promise((resolve) => server.listen(0, resolve));
    let failed = false;
    try {
        const page = await browser.newPage();
        await page.goto(`http://localhost:${server.address().port}/`);
        for (const file of files) {
            const code = fs.readFileSync(file, 'utf8');
            const result = await page.evaluate(async (source) => {
                if (!navigator.gpu) {
                    return ['error: no WebGPU'];
                }
                const adapter = await navigator.gpu.requestAdapter();
                if (!adapter) {
                    return ['error: no WebGPU adapter'];
                }
                const device = await adapter.requestDevice();
                const module = device.createShaderModule({code: source});
                const info = await module.getCompilationInfo();
                return info.messages.map((m) => `${m.type} ${m.lineNum}:${m.linePos} ${m.message}`);
            }, code);
            for (const message of result) {
                console.log(`${file}: ${message}`);
            }
            failed = failed || result.length > 0;
            if (result.length === 0) {
                console.log(`${file}: ok`);
            }
        }
    } finally {
        await browser.close();
        server.close();
    }
    return failed ? 1 : 0;
}

main(process.argv.slice(2)).then((status) => process.exit(status), (error) => {
    console.error(error);
    process.exit(1);
});
