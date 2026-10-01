// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Renders layout fixtures in headless Chrome and prints every node's
// border box relative to its parent's, for tools/gen_layout_fixtures.py
// --oracle. Reads a JSON array of {name, width, height, html} on stdin;
// prints {version, fixtures: {name: [[x, y, width, height], ...]}}.
// A development tool: it needs Node and puppeteer, which the library
// and its tests do not. MUI_NODE_MODULES names the node_modules directory
// that holds puppeteer when it is not installed beside this file, and
// PUPPETEER_CACHE_DIR where its browser is.

import { createRequire } from 'node:module';
import path from 'node:path';

const base = process.env.MUI_NODE_MODULES
    ? path.join(process.env.MUI_NODE_MODULES, 'resolve.js')
    : import.meta.url;
const puppeteer = createRequire(base)('puppeteer');

const BASE = `<!DOCTYPE html><html><head><style>
body { margin: 0; }
div { display: flex; box-sizing: border-box; position: relative; margin: 0; padding: 0;
      border: 0 solid; }
#space { position: relative; }
#space > div { position: absolute; left: 0; top: 0; }
</style></head><body>`;

function readStdin() {
    return new Promise((resolve) => {
        let text = '';
        process.stdin.setEncoding('utf8');
        process.stdin.on('data', (chunk) => { text += chunk; });
        process.stdin.on('end', () => resolve(text));
    });
}

const fixtures = JSON.parse(await readStdin());
const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
const page = await browser.newPage();
const result = { version: (await browser.version()).replace('Chrome/', ''), fixtures: {} };
for (const fixture of fixtures) {
    const space = `<div id="space" style="width:${fixture.width}px;height:${fixture.height}px">`;
    await page.setContent(BASE + space + fixture.html + '</div></body></html>');
    result.fixtures[fixture.name] = await page.evaluate(() => {
        const nodes = [...document.querySelectorAll('[data-i]')];
        return nodes.map((node) => {
            const box = node.getBoundingClientRect();
            const parent = node.parentElement.getBoundingClientRect();
            return [box.x - parent.x, box.y - parent.y, box.width, box.height];
        });
    });
}
await browser.close();
process.stdout.write(JSON.stringify(result));
