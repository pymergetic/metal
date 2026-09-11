/* The wasm seat, running here in the page.
 *
 * The console panel's other tab is a viewport on the box that served the page.
 * This one is not a viewport on anything: it is a second seat — the browser
 * build of this same tree, booted from the image the host build left behind and
 * running on the machine that is reading the page. Its console is its own.
 *
 *   GET /images                          what images this seat can serve
 *   GET /images/BROWSER/<file>?off=N     one window of the browser image
 *
 * The pair comes down through the same windowed face the factory pane's
 * download buttons use — the .wasm is tens of megabytes against a 1 MiB body,
 * so the loop asks again at the offset it reached. Blobs rather than direct
 * URLs because the seat serves windows, not files: the loader is handed to
 * `import()` as text/javascript and the runtime to Emscripten's locateFile as
 * application/wasm, which is what lets it instantiate by streaming.
 *
 * A typed line is not interpreted here. It goes to `metal_repl.run` inside the
 * wasm seat — the same module the host seat runs a panel line through — so the
 * echo, the value, and a traceback come out identically on both tabs, from one
 * implementation.
 *
 * A seat with no browser image (firmware, or the browser cell itself) lists
 * none, and this refuses with the command that builds one: a missing fill,
 * reported, not a dark feature.
 */
const BOARD = "BROWSER";
const LOADER = "micropython.mjs";
const RUNTIME = "micropython.wasm";
const HEAP = 16 * 1024 * 1024;
/* The name a line is parked under while the seat's own REPL module runs it, so
 * no source has to be pasted together over here. */
const LINE_VAR = "_mc_line";
/* The prompt is the seat's, not this tab's: what it looks like and when it is
 * skipped live in metal_repl, the same module the echo comes out of. */
const PROMPT = "metal_repl.prompt()";
const PROXY_KIND_MP_EXCEPTION = -1;

function mb(n) {
  return (n / (1024 * 1024)).toFixed(1) + " MB";
}

/* One file the seat serves in ?off= windows, as a Blob of the given type. The
 * declared length is the only thing that says when it is done; an empty window
 * before that means the seat lost the bytes, which is a refusal to report
 * rather than a short file to run. */
async function fetchBlob(file, total, type, onProgress) {
  const parts = [];
  let got = 0;
  while (got < total) {
    const r = await fetch(`/images/${BOARD}/${file}?off=${got}`, { cache: "no-store" });
    if (!r.ok) throw new Error(`${file} -> HTTP ${r.status}`);
    const buf = await r.arrayBuffer();
    if (!buf.byteLength) {
      throw new Error(`seat stopped at ${got} of ${total} bytes of ${file}`);
    }
    parts.push(buf);
    got += buf.byteLength;
    if (onProgress) onProgress(got, total);
  }
  return new Blob(parts, { type });
}

async function imageFiles() {
  const r = await fetch("/images", { cache: "no-store" });
  if (!r.ok) throw new Error("/images -> HTTP " + r.status);
  const d = await r.json();
  const mine = (d.images || []).filter((im) => im.board === BOARD);
  const loader = mine.find((im) => im.file === LOADER);
  const runtime = mine.find((im) => im.file === RUNTIME);
  if (!loader || !runtime) {
    throw new Error("no browser image on this seat — build one with "
      + "`make -C extmod/metal browser`");
  }
  return { loader, runtime };
}

/* Run one line of Python in the wasm seat, through Asyncify.
 *
 * api.js's own runPythonAsync does not await the suspension, so a line that
 * reaches js.fetch (the CDN card) would return before it finished. This awaits
 * the ccall and reports the seat's traceback the way its terminal would. */
function execFn(mp, onOut) {
  const Module = mp._module;
  return (src) => {
    const len = Module.lengthBytesUTF8(src);
    const buf = Module._malloc(len + 1);
    Module.stringToUTF8(src, buf, len + 1);
    const value = Module._malloc(3 * 4);
    return Module.ccall(
      "mp_js_do_exec_async",
      "number",
      ["pointer", "number", "pointer"],
      [buf, len, value],
      { async: true },
    ).then(() => {
      try {
        if (Module.getValue(value, "i32") === PROXY_KIND_MP_EXCEPTION) {
          const strLen = Module.getValue(value + 4, "i32");
          const strPtr = Module.getValue(value + 8, "i32");
          const raw = Module.UTF8ToString(strPtr, strLen);
          Module._free(strPtr);
          /* µPy hands back "<value>\x04<traceback>"; the traceback is the half
           * worth showing. */
          const parts = String(raw).split("\x04");
          onOut((parts[1] || parts[0] || raw).replace(/\s+$/, "") + "\n");
        }
      } finally {
        Module._free(buf);
        Module._free(value);
      }
    });
  };
}

/* Boot the pair already sitting at these two URLs, whatever kind they are: the
 * tab hands blob: URLs it built out of windows, the headless prove hands file:
 * ones. Everything a line does afterwards lives here, so the two callers run
 * the same seat rather than two lookalikes.
 *
 * `onOut` takes whatever the seat prints, `onStatus` the progress of getting
 * there. Returns the one thing a console needs of it: run a line. */
export async function bootFrom({ loaderUrl, runtimeUrl, onOut, onStatus, heapsize }) {
  const say = onStatus || (() => {});
  say("instantiating…");
  const mod = await import(loaderUrl);
  const loadMicroPython = mod.loadMicroPython || mod.default?.loadMicroPython;
  if (!loadMicroPython) throw new Error(LOADER + " has no loadMicroPython");
  /* Unbuffered, so a prompt the seat has not followed with a newline shows up
   * in front of the caret the way the host tab's pending line does. The price
   * is that output arrives a byte at a time as a Uint8Array — the boot tree is
   * UTF-8 box drawing, so the decoder has to be a streaming one or every stem
   * comes out as replacement characters. */
  const dec = new TextDecoder("utf-8");
  const emit = (chunk) => onOut(typeof chunk === "string"
    ? chunk
    : dec.decode(chunk, { stream: true }));
  const mp = await loadMicroPython({
    heapsize: heapsize || HEAP,
    url: runtimeUrl,
    stdout: emit,
    stderr: emit,
    linebuffer: false,
  });

  const exec = execFn(mp, onOut);
  await exec("import metal_repl");
  say("up");
  /* Nothing here runs a REPL loop, so the prompt a seat sits at between lines
   * is asked for. Without it the caret waits on a bare line and the `>>> `
   * only appears behind a line once it has been sent. */
  await exec(PROMPT);
  return {
    async line(text) {
      mp.globals.set(LINE_VAR, text);
      await exec(`metal_repl.run(${LINE_VAR})`);
      await exec(PROMPT);
    },
  };
}

/* Fetch this seat's browser image out of its windowed face and boot that. */
export async function boot({ onOut, onStatus, heapsize }) {
  const say = onStatus || (() => {});
  say("reading the image list…");
  const { loader, runtime } = await imageFiles();

  say(`fetching ${RUNTIME} — 0 of ${mb(runtime.len)}`);
  const wasmBlob = await fetchBlob(RUNTIME, runtime.len, "application/wasm",
    (n, t) => say(`fetching ${RUNTIME} — ${mb(n)} of ${mb(t)}`));
  say(`fetching ${LOADER}…`);
  const mjsBlob = await fetchBlob(LOADER, loader.len, "text/javascript");

  const loaderUrl = URL.createObjectURL(mjsBlob);
  const runtimeUrl = URL.createObjectURL(wasmBlob);
  try {
    return await bootFrom({ loaderUrl, runtimeUrl, onOut, onStatus, heapsize });
  } finally {
    /* Instantiation is done with both by now, and a 26 MB blob is not
     * something to leave pinned in the tab for the rest of the session. */
    URL.revokeObjectURL(loaderUrl);
    URL.revokeObjectURL(runtimeUrl);
  }
}
