/* Factory floor — the build card's live pane.
 *
 *   GET  /build                     the unit index (matrix rows) + walk state
 *   GET  /build/events?since=N      the ring tail (this pane's heartbeat)
 *   POST /build?all=1[&target=N]    the BUILD ALL walk
 *
 * No framework, no state beyond `since` — the ring is the state. Unit names are
 * always the full FQN (pymergetic.metal.build, never a stripped label): the
 * registry face is the authority and the pane must not rename it. */
let since = 0;
let building = false;
let units = [];
let walk = null; /* the seat's walk state from GET /build */

/* Last event seen per unit, kept across ticks. A poll returns only the events
 * newer than `since`, and the ring holds far fewer entries than a whole walk
 * emits — so a row rebuilt from one tick's tail loses every unit that finished
 * earlier and flickers back to idle. The durable truth is the unit's `built`
 * flag on the index; the ring only sharpens it while a walk is in flight. */
const lastByFqn = Object.create(null);

const $ = (id) => document.getElementById(id);

function setStatus(text, busy) {
  const el = $("fx-status");
  if (!el) return;
  el.textContent = text;
  el.className = busy ? "busy" : "";
}

function fmtBytes(n) {
  if (!n) return "0 B";
  if (n < 1024) return n + " B";
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB";
  return (n / 1024 / 1024).toFixed(1) + " MiB";
}

function esc(s) {
  return String(s == null ? "" : s).replace(/[&<>"]/g, (c) => (
    { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]
  ));
}

/* The matrix: one row per unit. State comes from the index's durable `built`
 * flag, refined by the newest event this client has seen for that unit. */
function renderMatrix() {
  const tbody = $("fx-rows");
  if (!tbody) return;
  let html = "";
  for (const u of units) {
    const st = lastByFqn[u.fqn] || null;
    let cls = u.built ? "ok" : "idle";
    let label = u.built ? "built" : "—";
    let bytes = "";
    if (st) {
      if (st.kind === "unit_fail") {
        cls = "fail";
        label = "fail";
      } else if (st.kind === "unit_end") {
        cls = "ok";
        label = "ok";
      } else {
        cls = "busy";
        label = st.kind.replace(/_/g, " ");
      }
      bytes = st.bytes ? fmtBytes(st.bytes) : "";
    }
    /* A download only where the bytes are really retained: the index's
     * `objects` count is the seat's own answer, and it goes to 0 when the
     * cache resets. Offering a link otherwise would download nothing. */
    const dl = u.objects
      ? '<button type="button" class="fx-dl" data-fqn="' + esc(u.fqn) + '">' +
        "get " + u.objects + (u.objects === 1 ? " object" : " objects") +
        "</button>"
      : "";
    html += "<tr>" +
      "<td>" + esc(u.fqn) + "</td>" +
      "<td>" + esc(u.impl || "?") + "</td>" +
      "<td>" + (u.n_sources || 0) + "</td>" +
      '<td class="' + cls + '">' + esc(label) + "</td>" +
      '<td class="bytes">' + bytes + "</td>" +
      "<td>" + dl + "</td>" +
      "</tr>";
  }
  tbody.innerHTML = html;
  const cnt = $("fx-count");
  if (cnt) {
    /* `built` is a retained record, not "this seat can build it", and
     * `buildable` is a per-unit object-count cap, not an arch or impl
     * limit. Naming them for what the seat measures stops the footer
     * contradicting a walk that just reported every unit ok. */
    const held = units.filter((u) => u.built).length;
    const capped = units.filter((u) => !u.buildable).length;
    cnt.textContent = units.length + " units discovered · " + held +
      " with objects held here" +
      (capped ? " · " + capped + " too many sources to retain" : "");
  }
}

/* Pull one blob the seat serves in windows and hand it to the browser as a
 * file. Both byte faces answer up to whatever their body holds and stop, so
 * the loop asks again at the offset it reached — the declared length is the
 * only thing that says when it is done. An empty window before that length
 * means the seat lost the bytes mid-download (the cache reset), which is a
 * refusal to report, not a short file to save. */
async function download(url, total, filename, onProgress) {
  const parts = [];
  let got = 0;
  while (got < total) {
    const r = await fetch(url + (url.includes("?") ? "&" : "?") + "off=" + got);
    if (!r.ok) throw new Error(url + " -> HTTP " + r.status);
    const buf = await r.arrayBuffer();
    if (!buf.byteLength) {
      throw new Error("seat stopped at " + got + " of " + total + " bytes");
    }
    parts.push(buf);
    got += buf.byteLength;
    if (onProgress) onProgress(got, total);
  }
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob(parts, {
    type: "application/octet-stream",
  }));
  a.download = filename;
  a.click();
  URL.revokeObjectURL(a.href);
}

/* Every object a unit retained, one file each. */
async function downloadObjects(fqn) {
  setStatus("reading " + fqn + " objects…", true);
  try {
    const meta = await (await fetch("/build/objects/" + fqn)).json();
    const objs = meta.objects || [];
    if (!objs.length) {
      setStatus(fqn + ": nothing retained (rebuild it first)", false);
      return;
    }
    for (let i = 0; i < objs.length; i++) {
      const o = objs[i];
      await download("/build/object/" + fqn + "/" + i, o.len,
        fqn + "." + o.src + ".o",
        (n, t) => setStatus("downloading " + fqn + " " + o.src + " — " +
          fmtBytes(n) + " of " + fmtBytes(t), true));
    }
    setStatus("saved " + objs.length +
      (objs.length === 1 ? " object" : " objects") + " for " + fqn, false);
  } catch (e) {
    setStatus("download failed: " + e.message, false);
  }
}

/* The seat's own built images, listed once at load (the host build writes
 * them; they do not change while the seat runs). */
async function loadImages() {
  const box = $("fx-images");
  if (!box) return;
  try {
    const d = await (await fetch("/images")).json();
    const imgs = d.images || [];
    if (!imgs.length) {
      box.textContent = d.note || "no built images on this seat";
      return;
    }
    /* Group by board. The seat lists them in directory order, which
       interleaves variants of the same board and reads as a jumble. */
    const byBoard = new Map();
    for (const im of imgs) {
      if (!byBoard.has(im.board)) byBoard.set(im.board, []);
      byBoard.get(im.board).push(im);
    }
    box.innerHTML = "";
    for (const board of [...byBoard.keys()].sort()) {
      const group = document.createElement("div");
      group.className = "fx-board";
      const name = document.createElement("span");
      name.className = "fx-board-name";
      name.textContent = board;
      group.appendChild(name);
      for (const im of byBoard.get(board).sort((a, b) =>
        a.file.localeCompare(b.file))) {
        const b = document.createElement("button");
        b.type = "button";
        b.className = "fx-dl";
        b.textContent = im.file + " — " + fmtBytes(im.len);
        b.addEventListener("click", async () => {
          const as = board + "-" + im.file;
          setStatus("downloading " + as + "…", true);
          try {
            await download("/images/" + im.board + "/" + im.file, im.len, as,
              (n, t) => setStatus("downloading " + as + " — " +
                fmtBytes(n) + " of " + fmtBytes(t), true));
            setStatus("saved " + as, false);
          } catch (e) {
            setStatus("download failed: " + e.message, false);
          }
        });
        group.appendChild(b);
      }
      box.appendChild(group);
    }
  } catch (e) {
    box.textContent = "images unavailable: " + e.message;
  }
}

/* The stream: append the ring's new tail, cap the DOM at ~200 rows. */
function renderEvents(events) {
  const box = $("fx-events");
  if (!box || !events.length) return;
  let html = "";
  for (const e of events) {
    html += '<div class="ev' + (e.kind === "unit_fail" ? " fail" : "") + '">' +
      '<span class="k">' + esc(e.kind) + "</span>" +
      '<span class="s">' + esc(e.fqn) + (e.src ? " · " + esc(e.src) : "") + "</span>" +
      '<span class="b">' +
      (e.dur_us ? (e.dur_us / 1000).toFixed(1) + " ms" : "") +
      (e.bytes ? " · " + fmtBytes(e.bytes) : "") +
      "</span>" +
      "</div>";
  }
  box.insertAdjacentHTML("beforeend", html);
  while (box.children.length > 200) box.removeChild(box.firstChild);
  box.scrollTop = box.scrollHeight;
}

/* The walk is background: this pane only starts it and watches the walk object
 * the /build index carries — the compile itself runs one unit per runner
 * quantum on the seat, this poll is a pure read. */
function renderWalk() {
  const btn = $("fx-build-all");
  const prod = $("fx-produce");
  const sel = $("fx-target");
  const lane = $("fx-lane-fill");
  const w = walk;
  if (!w) return;
  building = w.state === "running";
  if (btn) btn.disabled = building;
  /* Both buttons drive the one walk, so a running walk locks both — and the
   * produce side stays locked while no lane is emittable. */
  if (prod) {
    prod.disabled = building || !sel || sel.value === "" ||
      (sel.selectedOptions[0] || {}).disabled === true;
  }
  if (lane && w.total > 0) {
    const done = (w.done || 0) + (w.failed || 0) + (w.skipped || 0);
    lane.style.width = Math.round((done / w.total) * 100) + "%";
  }
  if (building) {
    setStatus("walk #" + w.id + " " + (w.mode || "local") +
      " on lane " + w.target + ": " +
      (w.done || 0) + " ok, " + (w.failed || 0) + " failed, " +
      (w.skipped || 0) + " skipped of " + (w.total || 0) +
      " — " + (w.running || 0) + " lane(s) in flight", true);
  } else if (w.id && w.state === "done") {
    setStatus("walk #" + w.id + " " + (w.mode || "local") + " done: " +
      (w.done || 0) + " ok, " +
      (w.failed || 0) + " failed, " + (w.skipped || 0) + " skipped" +
      (w.fail_error ? " — " + w.fail_fqn + ": " + w.fail_error : ""), false);
  }
}

/* The lane picker, built from what the seat says it can emit.
 *
 * `produce` is the only thing that decides whether a lane is offered here: a
 * lane is usable when the seat carries a backend for that arch, which for a
 * foreign arch means a linked TCC cross instance and for the seat's own arch
 * means the native one. A seat naming its own arch on a cross lane is not a
 * missing lane — the cross instances are defined only for foreign arches, so
 * routing it there refused an arch the seat plainly emits. `local` marks the
 * lane the rebuild button uses; it is shown for orientation, not as a limit
 * on producing. */
let lanesKey = "";
function renderLanes(lanes, seatArch) {
  const sel = $("fx-target");
  if (!sel || !lanes) return;
  const key = JSON.stringify(lanes);
  if (key === lanesKey) return;   /* rebuilt only when the seat's answer changes */
  lanesKey = key;
  const keep = sel.value;
  sel.innerHTML = "";
  for (const l of lanes) {
    const o = document.createElement("option");
    o.value = String(l.target);
    let label = l.name === "seat" ? "seat-native (" + (seatArch || "?") + ")"
                                  : l.arch;
    if (l.produce && l.local) {
      label += " — this seat's arch";
    } else if (l.produce) {
      label += " — cross";
    } else {
      label += " — no backend on this seat";
    }
    o.textContent = label;
    o.disabled = !l.produce;
    sel.appendChild(o);
  }
  const still = [...sel.options].find((o) => o.value === keep && !o.disabled);
  sel.value = still ? keep : "0";
  const prod = $("fx-produce");
  if (prod) prod.disabled = building || sel.value === "";
}

/* The poll: index + tail in one tick; 1 Hz keeps the ring the truth without
 * hammering the seat. */
async function tick() {
  try {
    const [index, ev] = await Promise.all([
      fetch("/build").then((r) => r.json()),
      fetch("/build/events?since=" + since).then((r) => r.json()),
    ]);
    units = index.units || [];
    walk = index.walk || null;
    renderLanes(index.lanes, index.seat_arch);
    if (ev.latest) since = ev.latest;
    const events = ev.events || [];
    renderEvents(events);
    for (const e of events) lastByFqn[e.fqn] = e;
    renderMatrix();
    renderWalk();
    if (!building && (!walk || walk.state !== "done")) {
      setStatus("idle — latest seq " + since, false);
    }
    /* A full window means the seat had more than it could fit in one body and
     * `since` stopped at what it sent. Come back for the rest now rather than
     * at the next second, so a walk's log is not minutes behind the walk. */
    if (events.length >= 32) setTimeout(tick, 50);
  } catch (e) {
    setStatus("poll failed: " + e, false);
  }
}

/* `produce` picks the lane and stops at the objects; the default rebuilds this
 * seat, which only its own arch can do — so the two go through the same walk
 * but never share a button, and neither can be started as the other. */
async function startWalk(produce) {
  if (building) return;
  const local = $("fx-build-all");
  const prod = $("fx-produce");
  const target = produce ? $("fx-target").value : "0";
  const what = produce ? "producing objects for lane " + target
                       : "rebuilding this seat";
  setStatus(what + "…", true);
  local.disabled = true;
  if (prod) prod.disabled = true;
  try {
    const q = "/build?all=1&target=" + target + (produce ? "&mode=produce" : "");
    const r = await fetch(q, { method: "POST" });
    const j = await r.json();
    if (j.error) {
      setStatus("walk refused: " + j.error, false);
      local.disabled = false;
      if (prod) prod.disabled = false;
      return;
    }
    /* The reply is the walk id; the poll loop renders progress from the walk
     * object on /build — no client-side wait, the POST was the whole blocking
     * portion on this side. */
    setStatus("walk #" + j.walk + " " + (j.mode || "local") + " on lane " +
      (j.target || 0) + " (" + (j.units || 0) + " units)", true);
  } catch (e) {
    setStatus("walk failed: " + e, false);
    local.disabled = false;
    if (prod) prod.disabled = false;
  }
}

const buildBtn = $("fx-build-all");
const produceBtn = $("fx-produce");
if (produceBtn) {
  produceBtn.addEventListener("click", () => startWalk(true));
}
if (buildBtn) {
  buildBtn.addEventListener("click", () => startWalk(false));
  /* Delegated, because renderMatrix replaces every row each tick — a listener
   * bound to a button would be thrown away a second later. */
  const rows = $("fx-rows");
  if (rows) {
    rows.addEventListener("click", (ev) => {
      const btn = ev.target.closest(".fx-dl");
      if (btn) downloadObjects(btn.dataset.fqn);
    });
  }
  await Promise.all([tick(), loadImages()]);
  setInterval(tick, 1000);
}
