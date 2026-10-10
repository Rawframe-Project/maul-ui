// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Walks a sample with VoiceOver on macOS or NVDA on Windows through
// Guidepup, as tools/screen_reader_walk.sh walks one with Orca (record
// mui-0008):
//
//   node tools/screen_reader/walk.mjs <sample> <steps> <out-dir>
//
// A sample's .js (an Emscripten build) is a web sample: served with a
// page holding its canvas and opened in Chrome through Puppeteer, which
// must be installed beside this script.
//
// The machine is set up first by Guidepup's setup (`npx @guidepup/setup
// setup --ci`, then `install voiceover` or `install nvda`), and
// @guidepup/guidepup is installed where Node finds it. The
// sample, built with a window, starts and is given the keyboard; the
// screen reader starts; the steps go to it as keys, in the steps files'
// form (key, type, hold, wait, focus, expect). <out-dir> receives
// speech.txt, a line per utterance, steps.txt, each step with what was
// said after it (and, on the web, what the page had focused), on macOS
// stack.txt, the sample's threads as the walk ends, and on the web
// tree.json, the browser's accessibility tree at the end. The exit
// status is 1 when a phrase expected was not said, or nothing was.

import { nvda, voiceOver } from "@guidepup/guidepup";
import { spawn, spawnSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { createRequire } from "node:module";
import { basename, dirname, extname, join } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";

const [sample, steps, out] = process.argv.slice(2);
const mac = process.platform === "darwin";
const reader = mac ? voiceOver : nvda;

// xdotool's key names as Guidepup names them.
const keys = {
  space: "Space",
  Return: "Enter",
  BackSpace: "Backspace",
  Delete: "Delete",
  Up: "ArrowUp",
  Down: "ArrowDown",
  Left: "ArrowLeft",
  Right: "ArrowRight",
};
// A word's move is Control and an arrow on Windows, Option and one on
// macOS.
const modifiers = { ctrl: mac ? "Option" : "Control", shift: "Shift", alt: mac ? "Option" : "Alt" };

const keyOf = (name) => keys[name] ?? name;
// What a step waits for the sample and the reader to settle: longer on
// macOS, where the runner's virtual GPU keeps the sample's main thread
// waiting for a drawable a third of the time (research 148).
const settle = mac || process.argv[2].endsWith(".js") ? 4000 : 2500;

// The sample's window brought to the front, by its process: a program
// started from a shell is not activated on macOS, and Windows gives the
// foreground only to whom the user last worked with.
function front(pid) {
  const [command, args] = mac
    ? ["osascript", ["-e", `tell application "System Events" to set frontmost of ` +
                           `(first process whose unix id is ${pid}) to true`]]
    : ["powershell", ["-NoProfile", "-Command",
                      `(New-Object -ComObject WScript.Shell).AppActivate(${pid})`]];
  const result = spawnSync(command, args, { encoding: "utf8" });
  console.log(`front: ${result.status} ${result.stdout.trim()} ${result.stderr.trim()}`);
}

// A web sample (its .js): served with a page holding the canvas Maul
// Window draws into, as test/web_runner.cjs serves it, and opened in a
// Chrome with a window, through the Puppeteer installed beside this
// script; the browser is the process brought to the front. Any other
// sample is run.
async function startWeb(script) {
  const types = { ".js": "text/javascript", ".wasm": "application/wasm" };
  const page = `<!doctype html><html><head><meta charset="utf-8"><title>${basename(script)}</title>
</head><body style="margin:0"><canvas id="page-canvas" style="width:400px;height:300px"></canvas>
<script>var Module = {arguments: []};</script><script src="${basename(script)}"></script></body></html>`;
  const server = createServer((request, response) => {
    const name = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
    if (name === "/") {
      response.writeHead(200, { "Content-Type": "text/html" });
      response.end(page);
      return;
    }
    try {
      const body = readFileSync(join(dirname(script), basename(name)));
      response.writeHead(200, { "Content-Type": types[extname(name)] ?? "application/octet-stream" });
      response.end(body);
    } catch {
      response.writeHead(404);
      response.end();
    }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const puppeteer = createRequire(import.meta.url)("puppeteer");
  const browser = await puppeteer.launch({
    headless: false,
    defaultViewport: null,
    args: ["--force-renderer-accessibility", "--enable-unsafe-webgpu", "--enable-unsafe-swiftshader",
           "--ignore-gpu-blocklist", "--enable-features=Vulkan", "--use-vulkan=swiftshader",
           "--use-webgpu-adapter=swiftshader", "--window-size=1000,800", "--no-first-run"],
  });
  const tab = (await browser.pages())[0] ?? (await browser.newPage());
  tab.on("console", (message) => console.log(`page: ${message.text()}`));
  await tab.goto(`http://127.0.0.1:${server.address().port}/`);
  const adapter = await tab.evaluate(async () => {
    const found = navigator.gpu ? await navigator.gpu.requestAdapter() : null;
    return found ? `${found.info?.vendor} ${found.info?.architecture} ${found.info?.description}` : "none";
  });
  console.log(`webgpu adapter: ${adapter}`);
  // The keys go to the page, not the browser's toolbar: the tab brought
  // forward and its body focused, before the hidden button that enables
  // the page's accessibility tree.
  await tab.bringToFront();
  await tab.evaluate(() => {
    document.body.tabIndex = -1;
    document.body.focus();
    // Every click, by its target, for steps.txt: whether a reader's press
    // reached the page, and where.
    globalThis.muiClicks = [];
    document.addEventListener("click", (e) => globalThis.muiClicks.push(e.target.tagName), true);
  });
  const title = await tab.title();
  console.log(`page title: ${title}`);
  return {
    pid: browser.process().pid,
    // What the page has focused, for steps.txt, with whether the page
    // has the system's focus, how many elements the ARIA adapter shows
    // (none before its tree is enabled), a field's selection, whether the
    // enabling button is there and the clicks since the last step.
    focused: () =>
      tab.evaluate(() => {
        const element = document.activeElement;
        const where = element ? `${element.tagName} ${element.getAttribute("role") ?? ""} ` +
          `"${element.getAttribute("aria-label") ?? element.textContent.slice(0, 30)}"` : "none";
        const shown = document.querySelectorAll("[id^=mui][id*='-']:not([aria-live])").length;
        const field = element && "selectionStart" in element && element.selectionStart !== null
          ? ` selection=${element.selectionStart}-${element.selectionEnd}` : "";
        const button = document.querySelector("button") ? " button" : "";
        const clicks = globalThis.muiClicks.splice(0).join(",");
        return `${where} focus=${document.hasFocus()} elements=${shown}${field}${button}` +
          (clicks ? ` clicks=${clicks}` : "");
      }),
    // Whether the page has the system's focus: its window is the one the
    // reader's keys go to.
    hasFocus: () => tab.evaluate(() => document.hasFocus()),
    tree: async () => JSON.stringify(await tab.accessibility.snapshot()).slice(0, 4000),
    stop: async () => {
      await browser.close();
      server.close();
    },
  };
}

mkdirSync(out, { recursive: true });
// A browser takes long to start and keeps the machine busy meanwhile,
// so the reader starts first for a web sample, after the sample
// otherwise.
const web = sample.endsWith(".js");
if (web) {
  await reader.start({ capture: true });
}
const app = web ? await startWeb(sample) : spawn(sample, [], { stdio: "ignore" });
await sleep(web ? 10000 : 5000);
if (!web) {
  await reader.start({ capture: true });
}
await sleep(2000);
// The browser's window by its process, as the sample's: by its title
// Windows never found it, and a walk whose keys went to another window
// heard nothing. A page is asked whether it has the focus, again until it
// does.
front(app.pid);
await sleep(3000);
for (let tries = 0; web && tries < 5 && !(await app.hasFocus()); tries++) {
  front(app.pid);
  await sleep(2000);
}
if (web) {
  console.log(`page focused: ${await app.hasFocus()}`);
}
const told = [];
const expected = [];
let said = 0;
for (const line of readFileSync(steps, "utf8").split("\n")) {
  const [verb, ...rest] = line.trim().split(/\s+/);
  if (verb === "" || verb.startsWith("#")) {
    continue;
  }
  if (verb === "expect") {
    expected.push(rest.join(" "));
    continue;
  }
  if (verb === "key") {
    await reader.press(keyOf(rest[0]));
  } else if (verb === "type") {
    await reader.type(rest.join(" "));
  } else if (verb === "hold") {
    await reader.press(`${modifiers[rest[0]] ?? rest[0]}+${keyOf(rest[1])}`);
  } else if (verb === "focus") {
    // The reader asked to say what has the focus (NVDA's Insert+Tab), as
    // a user does; where the reader has no such command, nothing.
    const command = reader.keyboardCommands?.reportCurrentFocus;
    if (command) {
      await reader.perform(command);
    }
  } else if (verb === "wait") {
    await sleep(Number(rest[0]) * 1000);
  } else {
    console.error(`unknown step: ${verb}`);
  }
  await sleep(settle);
  const log = await reader.spokenPhraseLog();
  const where = web ? ` [${await app.focused()}]` : "";
  told.push(`${line.trim()}\t${log.slice(said).join(" | ")}${where}`);
  said = log.length;
}
const log = await reader.spokenPhraseLog();
// A browser closes before its reader, the reverse of their start: a
// reader stopped under a running Chrome heard nothing on the next try.
if (web) {
  writeFileSync(`${out}/tree.json`, await app.tree());
  await app.stop();
}
await reader.stop();
// Where the sample's threads are, should it have stopped answering.
if (mac && !web) {
  spawnSync("sample", [String(app.pid), "2", "-file", `${out}/stack.txt`]);
}
if (!web) {
  app.kill();
}
writeFileSync(`${out}/speech.txt`, log.join("\n") + "\n");
writeFileSync(`${out}/steps.txt`, told.join("\n") + "\n");
console.log(told.join("\n"));
const speech = log.join("\n").toLowerCase();
const missing = expected.filter((phrase) => !speech.includes(phrase.toLowerCase()));
for (const phrase of missing) {
  console.error(`not said: ${phrase}`);
}
process.exit(log.length === 0 || missing.length !== 0 ? 1 : 0);
