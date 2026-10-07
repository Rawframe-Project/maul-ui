// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs a web test in headless Chrome, as Maul Window's runner does
// (copied by record 0004, with what this library's tests need): serves
// the test's directory, opens a page that loads the test, and carries
// out the commands the test prints ("mui-test: <command>"), answering
// each by appending whether it held to globalThis.muiTestAnswers, which
// the test waits on. The test ends by printing "mui-test: exit
// <status>". Puppeteer comes from MUI_NODE_MODULES; without it the test
// is skipped (status 77).
//
// Commands:
// - ax <selector> <expected>: the browser's accessibility tree under an
//   element, one node a line joined with " | ", each its depth in two
//   spaces, its role, its name in quotes and its properties; holds when
//   it is the expected text.
// - rect <selector> <x> <y> <width> <height>: an element's box, relative
//   to the box of the element #host, in CSS pixels.
// - press <selector>: clicks an element, as a screen reader does.
//
// Chrome runs with WebGPU on SwiftShader, for programs that draw; the
// arguments after the test's are its own (Module.arguments), and
// MUI_RHI_REQUIRED, when set, is set in its environment too.
//
// usage: node web_runner.cjs <test.js> [argument...]

const http = require('http');
const fs = require('fs');
const path = require('path');

const modules = process.env.MUI_NODE_MODULES;
let puppeteer = null;
try {
    puppeteer = require(path.join(modules || '', 'puppeteer'));
} catch (error) {
    console.log('puppeteer not found through MUI_NODE_MODULES: skipped');
    process.exit(77);
}

const script = path.resolve(process.argv[2]);
const root = path.dirname(script);
const required = process.env.MUI_RHI_REQUIRED || '';
const moduleSetup = `var Module = {arguments: ${JSON.stringify(process.argv.slice(3))},
    preRun: [() => { if (Module.ENV && ${JSON.stringify(required)}) {
        Module.ENV.MUI_RHI_REQUIRED = ${JSON.stringify(required)}; } }]};`;
const page = `<!doctype html><html><head><meta charset="utf-8"></head><body style="margin:0">
<canvas id="page-canvas" style="width:400px;height:300px"></canvas>
<script>addEventListener('error', e => console.log(
    'page error at ' + e.filename + ':' + e.lineno + ':' + e.colno + ': ' + e.message));
${moduleSetup}</script>
<script src="${path.basename(script)}"></script></body></html>`;
const types = {'.js': 'text/javascript', '.wasm': 'application/wasm'};

const server = http.createServer((request, response) => {
    const name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
    if (name === '/') {
        response.writeHead(200, {'Content-Type': 'text/html'});
        response.end(page);
        return;
    }
    const file = path.join(root, path.normalize(name));
    if (!file.startsWith(root) || !fs.existsSync(file)) {
        response.writeHead(404);
        response.end();
        return;
    }
    response.writeHead(200, {'Content-Type': types[path.extname(file)] || 'application/octet-stream'});
    fs.createReadStream(file).pipe(response);
});

// The properties written, in this order, with their values.
const shown = ['checked', 'pressed', 'expanded', 'selected', 'disabled', 'required', 'readonly',
               'busy', 'modal', 'hasPopup', 'invalid', 'level', 'valuemin', 'valuemax',
               'valuetext', 'focusable'];

// The accessibility tree under an element, as text.
async function treeUnder(tab, selector) {
    const session = await tab.createCDPSession();
    const {root: document} = await session.send('DOM.getDocument', {depth: 0});
    const {nodeId} = await session.send('DOM.querySelector', {nodeId: document.nodeId, selector});
    if (!nodeId) {
        return '(no element ' + selector + ')';
    }
    const {node} = await session.send('DOM.describeNode', {nodeId});
    const {nodes} = await session.send('Accessibility.getFullAXTree');
    const byId = new Map(nodes.map(n => [n.nodeId, n]));
    const top = nodes.find(n => n.backendDOMNodeId === node.backendNodeId);
    const lines = [];
    const visit = (ax, depth, parentName) => {
        const role = ax.role ? ax.role.value : '';
        const name = ax.name ? ax.name.value : '';
        // Ignored nodes, unnamed generic ones and text repeating its
        // parent's name are passed through.
        const through = ax.ignored || (role === 'generic' && name === '') ||
                        role === 'InlineTextBox' || (role === 'StaticText' && name === parentName);
        let next = depth;
        if (!through && ax !== top) {
            const properties = [];
            for (const key of shown) {
                const property = (ax.properties || []).find(p => p.name === key);
                // "invalid=false" is on every native control: none.
                const value = property ? property.value.value : '';
                if (value !== '' && value !== false && !(key === 'invalid' && value === 'false')) {
                    properties.push(key + '=' + value);
                }
            }
            if (ax.value && ax.value.value !== '') {
                properties.push('value=' + ax.value.value);
            }
            lines.push('  '.repeat(depth) + role + ' "' + name + '"' +
                       (properties.length ? ' ' + properties.join(' ') : ''));
            next = depth + 1;
        }
        for (const id of ax.childIds || []) {
            const child = byId.get(id);
            if (child) {
                visit(child, next, through ? parentName : name);
            }
        }
    };
    if (top) {
        visit(top, 0, '');
    }
    await session.detach();
    return lines.join(' | ');
}

// The commands a test may give: whether each held.
async function carryOut(tab, command, rest) {
    if (command === 'ax') {
        const space = rest.indexOf(' ');
        const selector = space < 0 ? rest : rest.slice(0, space);
        const expected = space < 0 ? '' : rest.slice(space + 1);
        const tree = await treeUnder(tab, selector);
        if (tree !== expected) {
            console.log('ax: expected ' + expected + '\nax: got      ' + tree);
        }
        return tree === expected;
    }
    if (command === 'rect') {
        const [selector, ...numbers] = rest.split(' ');
        const box = await tab.evaluate(sel => {
            const element = document.querySelector(sel);
            const host = document.querySelector('#host');
            if (!element || !host) {
                return null;
            }
            const a = element.getBoundingClientRect();
            const b = host.getBoundingClientRect();
            return [a.left - b.left, a.top - b.top, a.width, a.height];
        }, selector);
        const held = box !== null && numbers.every((n, i) => Math.abs(Number(n) - box[i]) < 0.01);
        if (!held) {
            console.log('rect: expected ' + numbers.join(' ') + ', got ' + JSON.stringify(box));
        }
        return held;
    }
    if (command === 'press') {
        return await tab.evaluate(sel => {
            const element = document.querySelector(sel);
            if (element) {
                element.click();
            }
            return element !== null;
        }, rest);
    }
    throw new Error('unknown command ' + command);
}

// Commands run one after another, in the order the test gave them.
let queue = Promise.resolve();

async function main() {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const browser = await puppeteer.launch({
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
               '--use-webgpu-adapter=swiftshader'],
    });
    let status = 1;
    try {
        const tab = await browser.newPage();
        await tab.setViewport({width: 1024, height: 768, deviceScaleFactor: 1});
        const done = new Promise((resolve, reject) => {
            tab.on('pageerror', error => {
                console.log('page error: ' + (error.stack || error.message));
                reject(error);
            });
            tab.on('console', message => {
                const text = message.text();
                console.log(text);
                const match = /^mui-test: (\w+) ?(.*)$/.exec(text);
                if (!match) {
                    return;
                }
                if (match[1] === 'exit') {
                    resolve(Number(match[2]));
                    return;
                }
                queue = queue.then(() => carryOut(tab, match[1], match[2]))
                             .then(held => tab.evaluate(h => {
                                 (globalThis.muiTestAnswers = globalThis.muiTestAnswers || [])
                                     .push(h);
                             }, held))
                             .catch(reject);
            });
        });
        done.catch(() => {});
        await tab.goto(`http://127.0.0.1:${server.address().port}/`);
        const timeout = new Promise((_, reject) =>
            setTimeout(() => reject(new Error('timed out')), 60000));
        status = await Promise.race([done, timeout]);
    } catch (error) {
        console.log('runner: ' + error.message);
    } finally {
        await browser.close();
        server.close();
    }
    process.exit(status);
}

main();
