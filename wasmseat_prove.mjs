#!/usr/bin/env node
/* The console panel's wasm tab, without a browser.
 *
 * The tab (www/static/seat/wasmseat.js) fetches the browser image from the seat
 * in ?off= windows, hands the pair to `import()` and Emscripten's locateFile,
 * and then runs every typed line through `metal_repl.run` inside that seat —
 * the same module the host seat runs a panel line through, so one tab is not a
 * different REPL from the other.
 *
 * The fetching half is pinned on the seat side (upy_repl_prove.py walks the
 * windows). This is the other half, and it is the tab's own code that runs it:
 * this imports `bootFrom` out of wasmseat.js and hands it file: URLs where the
 * page hands blob: ones. A copy of the boot here would have proven the copy —
 * and did, right past a shadowed name the tab threw on.
 *
 * The image is copied out to a bare directory first: nothing of the build tree
 * beside it, the way it arrives in a browser.
 *
 * Usage: node wasmseat_prove.mjs <path to micropython.mjs>
 */
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const LOADER = "micropython.mjs";
const RUNTIME = "micropython.wasm";
const TAB = "src/pymergetic/metal/inspect/www/static/seat/wasmseat.js";

const src = path.dirname(path.resolve(process.argv[2]));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "metal-wasmseat-"));
for (const f of [LOADER, RUNTIME]) {
    fs.copyFileSync(path.join(src, f), path.join(tmp, f));
}

let out = "";
const onOut = (s) => {
    out += s;
    process.stdout.write(s);
};
const status = [];
const onStatus = (s) => status.push(s);

const fails = [];
function check(name, ok, detail) {
    console.log("  %s %s", name.padEnd(52), ok ? "ok" : "FAIL");
    if (!ok) fails.push(name + (detail ? " — " + String(detail).slice(0, 200) : ""));
}

/* The tab's own file, byte for byte — under a name node agrees to load as a
 * module, since this tree has no package.json saying .js means that. */
const here = path.dirname(fileURLToPath(import.meta.url));
const tabCopy = path.join(tmp, "wasmseat.mjs");
fs.copyFileSync(path.join(here, TAB), tabCopy);
const tab = await import(pathToFileURL(tabCopy).href);
const seat = await tab.bootFrom({
    loaderUrl: pathToFileURL(path.join(tmp, LOADER)).href,
    runtimeUrl: path.join(tmp, RUNTIME),
    onOut,
    onStatus,
});
check("the image boots with nothing but its own pair beside it",
    !/Traceback|ImportError/.test(out), out.slice(-200));
check("and the tab is told it came up", status[status.length - 1] === "up", status.join(" / "));
/* A seat sits at a prompt while it waits, so the caret has one in front of it
 * before anything is typed — and the line then continues that prompt instead
 * of bringing a second one. */
check("it waits at a prompt, before a line is typed", out.endsWith(">>> "), out.slice(-40));

out = "";
await seat.line("6 * 7");
check("a panel line is echoed by the seat that ran it", out.startsWith("6 * 7"), out);
check("the waiting prompt is the one it is echoed behind", !out.includes(">>> 6 * 7"), out);
check("and its value comes back", out.includes("42"), out);
check("and it waits at a prompt again", out.endsWith(">>> "), out.slice(-40));

out = "";
await seat.line("nope_at_all");
check("a failing line lands its traceback in the same place",
    out.includes("NameError"), out);

out = "";
await seat.line("keep = 3");
await seat.line("keep + 1");
check("names stay between lines, so it is a session", out.includes("4"), out);

fs.rmSync(tmp, { recursive: true, force: true });
console.log("");
if (fails.length) {
    for (const f of fails) console.log("FAIL " + f);
    process.exit(1);
}
console.log("wasm seat panel prove");
