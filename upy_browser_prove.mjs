#!/usr/bin/env node
/* Metal browser prove. Vanilla CLI uses sync mp_js_do_exec — js.fetch is
 * Asyncify, so CDN GET runs via mp_js_do_exec_async with {async:true}.
 * Do not edit ports/webassembly/api.js. */
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { pathToFileURL } from "node:url";

const wasmMjs = path.resolve(process.argv[2]);
const pyFile = path.resolve(process.argv[3]);
const src = fs.readFileSync(pyFile, "utf8");
const helloPack = fs.readFileSync(path.resolve(
    path.dirname(pyFile),
    "../wasmmod/examples/packs/pymergetic.wasmmod_examples.hello.wasm",
));

const { loadMicroPython } = await import(pathToFileURL(wasmMjs).href);
const mp = await loadMicroPython({
    heapsize: 16 * 1024 * 1024,
    stdout: (data) => process.stdout.write(data),
    linebuffer: false,
});

async function execAsync(code) {
    const Module = mp._module;
    const len = Module.lengthBytesUTF8(code);
    const buf = Module._malloc(len + 1);
    Module.stringToUTF8(code, buf, len + 1);
    const value = Module._malloc(3 * 4);
    try {
        await Module.ccall(
            "mp_js_do_exec_async",
            "number",
            ["pointer", "number", "pointer"],
            [buf, len, value],
            { async: true },
        );
    } finally {
        Module._free(buf);
        Module._free(value);
    }
}

const wasmLead = Buffer.from([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]);
const server = http.createServer((req, res) => {
    if (req.headers.authorization !== "Bearer tok-cdn"
        || req.headers["x-shell-session-id"] !== "sess-1") {
        res.writeHead(401);
        res.end();
        return;
    }
    if (req.url === "/artifacts/lead/hello.wasm") {
        res.writeHead(200, {
            "Content-Type": "application/wasm",
            "Content-Length": String(wasmLead.length),
        });
        res.end(wasmLead);
        return;
    }
    if (req.url === "/artifacts/lead/pymergetic.wasmmod_examples.hello.wasm") {
        res.writeHead(200, {
            "Content-Type": "application/wasm",
            "Content-Length": String(helloPack.length),
        });
        res.end(helloPack);
        return;
    }
    res.writeHead(404);
    res.end();
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const { port } = server.address();
try {
    await execAsync(`${src}\n_cdn("http://127.0.0.1:${port}")\n`);
} catch (error) {
    if (error.name === "PythonError") {
        console.error(error.message);
        process.exit(1);
    }
    throw error;
} finally {
    await new Promise((resolve) => server.close(resolve));
}
