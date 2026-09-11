/* The seat's console, in the corner of every seat page.
 *
 *   GET  /console/<id>?since=<seq>   the ring, by cursor
 *   POST /console/exec?cmd=<line>    run one line on the seat
 *
 * This is not a Python runtime in the browser (that is the CDN's µPy shell,
 * static/repl.js, which evaluates locally). This panel is a second viewport
 * on the seat's console 0: the same ring the serial console, the framebuffer
 * and an ssh session read. A line typed here runs on the seat and its output
 * appears on the seat's own terminal too — and a line typed at that terminal
 * appears here. That is the point of mirroring rather than proxying.
 *
 * The seat's http card has no WebSocket and never reads a request body, so
 * the transport is the cursor poll the factory pane already uses for build
 * events (`?since=`), and the command rides in the query string. Polling is
 * 4 Hz, and a poll that falls behind the ring is told so rather than silently
 * skipping lines.
 *
 * There is no input row: the caret sits in the terminal, directly after the
 * line the seat has not finished writing, because a console with a separate
 * box to type in is two things pretending to be one.
 *
 * The second tab is a different animal: not a viewport but a second seat, the
 * browser build of this tree booted in this page from the image the host build
 * left behind (static/seat/wasmseat.js). It is fetched only when that tab is
 * opened — it is tens of megabytes — and its console is its own.
 *
 * The panel builds its own DOM: it is attached from the shell's page head on
 * every seat page, including the ones rendered at runtime, so there is no
 * template for it to live in.
 */
/* Where this script came from, so the wasm tab can load its module from beside
 * it whatever base path the seat serves pages under. Read at top level, which
 * is the only time a deferred script is `currentScript`. */
const SELF = (document.currentScript && document.currentScript.src) || "";
const CONSOLE_ID = 0;
/* This panel's name for itself, sent with every poll. The seat counts its
 * cursor readers as viewports of the console — two tabs are two views, one tab
 * polling for an hour is one — and it can only tell them apart if they say who
 * they are. Per page load, because that is what a view is here. */
const WHO = 1 + Math.floor(Math.random() * 0xfffffffe);
const POLL_MS = 250;
const MAX_LINES = 600;
const HISTORY_MAX = 100;
const STATE_KEY = "pymergetic.metal.console.size";

/* --- ANSI ---------------------------------------------------------------
 * The ring holds what the seat printed, escape codes and all: the boot tree
 * is dim stems with green `ok`, and the banner is a per-character rainbow.
 * Rendering those is the difference between a mirror and a transcript, so
 * the SGR subset the seat actually emits is translated to spans. Anything
 * else is dropped rather than shown as garbage. */
const SGR_FG = {
  30: "#484f58", 31: "#f85149", 32: "#3fb950", 33: "#d29922",
  34: "#58a6ff", 35: "#bc8cff", 36: "#39c5cf", 37: "#b1bac4",
  90: "#6e7681", 91: "#ff7b72", 92: "#56d364", 93: "#e3b341",
  94: "#79c0ff", 95: "#d2a8ff", 96: "#56d4dd", 97: "#f0f6fc",
};

function applySgr(style, codes) {
  for (let i = 0; i < codes.length; i++) {
    const c = codes[i];
    if (c === 0) {
      style.color = "";
      style.bold = false;
      style.dim = false;
    } else if (c === 1) {
      style.bold = true;
    } else if (c === 2) {
      style.dim = true;
    } else if (c === 22) {
      style.bold = false;
      style.dim = false;
    } else if (c === 39) {
      style.color = "";
    } else if (SGR_FG[c]) {
      style.color = SGR_FG[c];
    } else if (c === 38 && codes[i + 1] === 2) {
      /* 38;2;r;g;b — the rainbow banner's true-color run. */
      const r = codes[i + 2] | 0, g = codes[i + 3] | 0, b = codes[i + 4] | 0;
      style.color = `rgb(${r},${g},${b})`;
      i += 4;
    } else if (c === 38 && codes[i + 1] === 5) {
      i += 2;
    }
  }
}

/* One console line -> a DocumentFragment of spans. Never innerHTML: the ring
 * carries whatever the seat printed, and a build log is not trusted markup. */
function ansiFragment(text) {
  const frag = document.createDocumentFragment();
  const style = { color: "", bold: false, dim: false };
  let buf = "";
  const flush = () => {
    if (!buf) return;
    if (!style.color && !style.bold && !style.dim) {
      frag.appendChild(document.createTextNode(buf));
    } else {
      const span = document.createElement("span");
      span.textContent = buf;
      if (style.color) span.style.color = style.color;
      if (style.bold) span.style.fontWeight = "600";
      if (style.dim) span.style.opacity = "0.62";
      frag.appendChild(span);
    }
    buf = "";
  };
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (ch !== "\x1b") {
      buf += ch;
      continue;
    }
    if (text[i + 1] !== "[") continue;     /* not CSI: drop the escape */
    const end = text.indexOf("m", i + 2);
    if (end < 0) break;                    /* truncated sequence: stop */
    const body = text.slice(i + 2, end);
    i = end;
    if (/[^0-9;]/.test(body)) continue;    /* a CSI that is not SGR */
    flush();
    applySgr(style, body.split(";").map((s) => (s === "" ? 0 : parseInt(s, 10))));
  }
  flush();
  return frag;
}

/* --- the panel ---------------------------------------------------------- */
function build() {
  const el = document.createElement("div");
  el.id = "metal-console-panel";
  el.className = "metal-console";
  el.innerHTML = `
    <div class="metal-console-bar">
      <button type="button" class="metal-console-tab" id="mc-tab-seat" aria-pressed="true"
              title="Console ${CONSOLE_ID} of the seat that served this page">seat</button>
      <button type="button" class="metal-console-tab" id="mc-tab-wasm" aria-pressed="false"
              title="The browser build of this seat, booted and running in this page">wasm</button>
      <span class="metal-console-status" id="mc-status">…</span>
      <span class="metal-console-spacer"></span>
      <button type="button" id="mc-size" title="Panel size: corner / tall">size</button>
      <button type="button" id="mc-follow" aria-pressed="true" title="Stay at the newest line">follow</button>
    </div>
    <div class="metal-console-body">
      <div class="metal-console-term" id="mc-term" tabindex="-1">
        <div class="metal-console-out" id="mc-out"></div>
        <div class="metal-console-echo">
          <span class="metal-console-pending" id="mc-pending"></span>
          <label class="visually-hidden" for="mc-in">Line to run on the seat</label>
          <input id="mc-in" type="text" autocomplete="off" spellcheck="false"
                 placeholder="m.services()" />
        </div>
      </div>
      <div class="metal-console-term" id="mc-wterm" tabindex="-1" hidden>
        <div class="metal-console-out" id="mc-wout"></div>
        <div class="metal-console-echo">
          <span class="metal-console-pending" id="mc-wpending"></span>
          <label class="visually-hidden" for="mc-win">Line to run on the wasm seat</label>
          <input id="mc-win" type="text" autocomplete="off" spellcheck="false"
                 placeholder="import pymergetic.metal as m" />
        </div>
      </div>
    </div>`;
  document.body.appendChild(el);
  return el;
}

/* Two: the corner tail and the tall one. */
const SIZES = ["", "is-open"];

function start() {
  if (document.getElementById("metal-console-panel")) return;
  const el = build();
  const $ = (id) => document.getElementById(id);
  const out = $("mc-out");
  const pendingEl = $("mc-pending");
  const term = $("mc-term");
  const input = $("mc-in");
  const status = $("mc-status");
  const wout = $("mc-wout");
  const wpendingEl = $("mc-wpending");
  const wterm = $("mc-wterm");
  const winput = $("mc-win");

  let size = Math.max(0, SIZES.indexOf(sessionStorage.getItem(STATE_KEY) || ""));
  let follow = true;
  let since = 0;
  let seen = 0;
  let inFlight = false;
  const history = [];
  let histAt = 0;
  /* Which tab is showing. Never restored from the session: opening the wasm
   * one downloads a seat image, and that is a thing to ask for, not to inherit
   * from a page you visited earlier. */
  let onWasm = false;

  const view = () => (onWasm ? wterm : term);

  function apply() {
    for (const c of SIZES) if (c) el.classList.remove(c);
    if (SIZES[size]) el.classList.add(SIZES[size]);
    sessionStorage.setItem(STATE_KEY, SIZES[size] || "");
    if (follow) view().scrollTop = view().scrollHeight;
  }

  function setStatus(text, cls) {
    status.textContent = text;
    status.className = "metal-console-status" + (cls ? " " + cls : "");
  }

  function append(node) {
    out.appendChild(node);
    seen++;
    while (seen > MAX_LINES && out.firstChild) {
      out.removeChild(out.firstChild);
      seen--;
    }
  }

  function appendLine(text) {
    const div = document.createElement("div");
    div.appendChild(ansiFragment(text));
    if (!text) div.appendChild(document.createTextNode("\u00a0"));
    append(div);
  }

  function appendNote(text) {
    const div = document.createElement("div");
    div.className = "metal-console-gap";
    div.textContent = text;
    append(div);
  }

  function appendGap(n) {
    appendNote(`… ${n} line${n === 1 ? "" : "s"} scrolled out of the seat's ring`);
  }

  /* Everything on screen belongs to a seat that is gone. Keeping it and
   * appending the new one's lines is how a viewport stops being a mirror: the
   * two transcripts read as one console that repeated itself. */
  function restarted() {
    while (out.firstChild) out.removeChild(out.firstChild);
    seen = 0;
    appendNote("… the seat restarted; this is its new ring");
    since = 0;
    setStatus("restarted", "is-busy");
  }

  async function poll() {
    if (inFlight) return;
    inFlight = true;
    const asked = since;
    try {
      const r = await fetch(`/console/${CONSOLE_ID}?since=${asked}&who=${WHO}`,
        { cache: "no-store" });
      if (!r.ok) throw new Error("http " + r.status);
      const d = await r.json();
      /* A ring cannot go backwards, so an answer behind the cursor it was
       * asked for is a seat that began again at zero under us. The next poll
       * reads its ring from the oldest line it kept. */
      if (d.seq < asked) {
        restarted();
        return;
      }
      if (d.dropped) appendGap(d.dropped);
      for (const line of d.lines || []) appendLine(line);
      since = d.seq;
      /* The line the seat has not finished writing sits in front of the caret,
       * so typing continues the seat's own prompt instead of a second one
       * below it. A seat with nothing pending has no prompt to continue, so
       * the panel supplies one. */
      pendingEl.textContent = "";
      if (d.pending) {
        pendingEl.appendChild(ansiFragment(d.pending));
      } else {
        pendingEl.appendChild(document.createTextNode(">>> "));
      }
      if (follow && !onWasm) term.scrollTop = term.scrollHeight;
      /* The poll keeps running while the wasm tab is up — the ring would
       * otherwise scroll away underneath — but the bar belongs to whichever
       * seat is on screen. The viewport count includes this panel: it is one of
       * the ways this console is being watched, and the seat says so too. */
      if (!onWasm) {
        const vp = typeof d.viewports === "number" ? ` · ${d.viewports} viewports` : "";
        setStatus(`live · ${since}${vp}`, "is-live");
      }
    } catch (e) {
      if (!onWasm) setStatus("seat unreachable", "is-lost");
    } finally {
      inFlight = false;
    }
  }

  /* --- the wasm tab -----------------------------------------------------
   * A seat of its own, so none of the poll above applies: there is no ring to
   * read, only what the thing prints as it prints it. Output arrives in
   * whatever chunks it wrote, so the tail is held until its newline the same
   * way the ring holds an unfinished line. */
  let wasm = null;
  let wasmBooting = false;
  let wtail = "";
  let wseen = 0;

  function wappend(node) {
    wout.appendChild(node);
    wseen++;
    while (wseen > MAX_LINES && wout.firstChild) {
      wout.removeChild(wout.firstChild);
      wseen--;
    }
  }

  function wasmOut(text) {
    wtail += text;
    const parts = wtail.split("\n");
    wtail = parts.pop();
    for (const p of parts) {
      const div = document.createElement("div");
      div.appendChild(ansiFragment(p));
      if (!p) div.appendChild(document.createTextNode("\u00a0"));
      wappend(div);
    }
    wpendingEl.textContent = "";
    if (wtail) wpendingEl.appendChild(ansiFragment(wtail));
    if (follow && onWasm) wterm.scrollTop = wterm.scrollHeight;
  }

  function wasmNote(text) {
    const div = document.createElement("div");
    div.className = "metal-console-gap";
    div.textContent = text;
    wappend(div);
    if (follow && onWasm) wterm.scrollTop = wterm.scrollHeight;
  }

  async function bootWasm() {
    if (wasm || wasmBooting) return;
    wasmBooting = true;
    wasmNote("… fetching this seat's browser image and booting it here");
    try {
      const mod = await import(new URL("wasmseat.js", SELF).href);
      wasm = await mod.boot({
        onOut: wasmOut,
        onStatus: (s) => {
          if (onWasm) setStatus(s, s === "up" ? "is-live" : "is-busy");
        },
      });
      wasmNote("… the browser build of this seat is up; it shares nothing with "
        + "the box that served the page");
      if (onWasm) setStatus("wasm seat · local", "is-live");
    } catch (e) {
      wasmNote("… " + ((e && e.message) || e));
      if (onWasm) setStatus("no wasm seat", "is-lost");
    } finally {
      wasmBooting = false;
    }
  }

  async function sendWasm(line) {
    if (!line.trim()) return;
    history.push(line);
    while (history.length > HISTORY_MAX) history.shift();
    histAt = history.length;
    winput.value = "";
    if (!wasm) {
      wasmNote("… no wasm seat to run that on");
      return;
    }
    setStatus("running…", "is-busy");
    try {
      await wasm.line(line);
      setStatus("wasm seat · local", "is-live");
    } catch (e) {
      wasmNote("… " + ((e && e.message) || e));
      setStatus("wasm seat faulted", "is-lost");
    }
  }

  function showTab(wantWasm) {
    onWasm = wantWasm;
    term.hidden = wantWasm;
    wterm.hidden = !wantWasm;
    $("mc-tab-seat").setAttribute("aria-pressed", wantWasm ? "false" : "true");
    $("mc-tab-wasm").setAttribute("aria-pressed", wantWasm ? "true" : "false");
    (wantWasm ? winput : input).focus();
    if (follow) view().scrollTop = view().scrollHeight;
    if (wantWasm) {
      setStatus(wasm ? "wasm seat · local" : "wasm seat…", wasm ? "is-live" : "is-busy");
      bootWasm();
    } else {
      setStatus(`live · ${since}`, "is-live");
    }
  }

  async function send(line) {
    if (!line.trim()) return;
    history.push(line);
    while (history.length > HISTORY_MAX) history.shift();
    histAt = history.length;
    input.value = "";
    setStatus("running…", "is-busy");
    try {
      /* The line rides in the query because the seat's http card never reads
       * a body; POST because running it twice is not the same as once. */
      const r = await fetch(`/console/exec?cmd=${encodeURIComponent(line)}`, {
        method: "POST",
        cache: "no-store",
      });
      const d = await r.json();
      /* Rewind to just before the line ran so its echo and its output arrive
       * through the same poll everything else does — one path, one order. */
      if (typeof d.since === "number" && d.since < since) since = d.since;
    } catch (e) {
      setStatus("send failed", "is-lost");
      return;
    }
    await poll();
  }

  $("mc-size").addEventListener("click", () => {
    size = (size + 1) % SIZES.length;
    apply();
  });
  $("mc-tab-seat").addEventListener("click", () => showTab(false));
  $("mc-tab-wasm").addEventListener("click", () => showTab(true));
  const followBtn = $("mc-follow");
  followBtn.addEventListener("click", () => {
    follow = !follow;
    followBtn.setAttribute("aria-pressed", follow ? "true" : "false");
    if (follow) view().scrollTop = view().scrollHeight;
  });
  for (const t of [term, wterm]) {
    t.addEventListener("scroll", () => {
      /* Scrolling up to read something is a request to stop being yanked
       * back. */
      const atEnd = t.scrollHeight - t.scrollTop - t.clientHeight < 24;
      if (atEnd !== follow) {
        follow = atEnd;
        followBtn.setAttribute("aria-pressed", follow ? "true" : "false");
      }
    });
    t.addEventListener("mouseup", () => {
      if (!window.getSelection().toString()) {
        (t === wterm ? winput : input).focus();
      }
    });
  }
  /* Reaching for the frame, the tabs or a bar button is not leaving the line
   * you were typing. mousedown is where the browser moves focus, so refusing
   * its default there keeps the caret in the field — the click still lands.
   * The terminals are left alone: selecting text in them needs the default. */
  el.addEventListener("mousedown", (e) => {
    if (!term.contains(e.target) && !wterm.contains(e.target)) e.preventDefault();
  });
  for (const box of [input, winput]) {
    box.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        (box === winput ? sendWasm : send)(box.value);
      } else if (e.key === "ArrowUp") {
        if (histAt > 0) {
          histAt--;
          box.value = history[histAt];
          e.preventDefault();
        }
      } else if (e.key === "ArrowDown") {
        if (histAt < history.length - 1) {
          histAt++;
          box.value = history[histAt];
        } else {
          histAt = history.length;
          box.value = "";
        }
        e.preventDefault();
      }
    });
  }

  apply();
  setStatus("connecting…");
  poll();
  setInterval(poll, POLL_MS);
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", start);
} else {
  start();
}
