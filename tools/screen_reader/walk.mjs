// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Walks a sample with VoiceOver on macOS or NVDA on Windows through
// Guidepup, as tools/screen_reader_walk.sh walks one with Orca (record
// mui-0008):
//
//   node tools/screen_reader/walk.mjs <sample> <steps> <out-dir>
//
// The machine is set up first by Guidepup's setup (`npx @guidepup/setup
// setup --ci`, then `install voiceover` or `install nvda`), and
// @guidepup/guidepup is installed where Node finds it. The
// sample, built with a window, starts and is given the keyboard; the
// screen reader starts; the steps go to it as keys, in the steps files'
// form (key, type, hold, wait, expect). <out-dir> receives speech.txt, a
// line per utterance, steps.txt, each step with what was said after it,
// and on macOS stack.txt, the sample's threads as the walk ends. The exit
// status is 1 when a phrase expected was not said, or nothing was.

import { nvda, voiceOver } from "@guidepup/guidepup";
import { spawn, spawnSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
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
const settle = mac ? 4000 : 2500;

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

mkdirSync(out, { recursive: true });
const app = spawn(sample, [], { stdio: "ignore" });
await sleep(5000);
await reader.start({ capture: true });
await sleep(2000);
front(app.pid);
await sleep(3000);
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
  } else if (verb === "wait") {
    await sleep(Number(rest[0]) * 1000);
  } else {
    console.error(`unknown step: ${verb}`);
  }
  await sleep(settle);
  const log = await reader.spokenPhraseLog();
  told.push(`${line.trim()}\t${log.slice(said).join(" | ")}`);
  said = log.length;
}
const log = await reader.spokenPhraseLog();
await reader.stop();
// Where the sample's threads are, should it have stopped answering.
if (mac) {
  spawnSync("sample", [String(app.pid), "2", "-file", `${out}/stack.txt`]);
}
app.kill();
writeFileSync(`${out}/speech.txt`, log.join("\n") + "\n");
writeFileSync(`${out}/steps.txt`, told.join("\n") + "\n");
console.log(told.join("\n"));
const speech = log.join("\n").toLowerCase();
const missing = expected.filter((phrase) => !speech.includes(phrase.toLowerCase()));
for (const phrase of missing) {
  console.error(`not said: ${phrase}`);
}
process.exit(log.length === 0 || missing.length !== 0 ? 1 : 0);
