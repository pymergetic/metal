/* Registry console — the seat's live registry, read over its own JSON API.
 *
 *   GET /health                        seat liveness
 *   GET /capabilities                  role + which faces this fill advertises
 *   GET /inspect/self                  self description
 *   GET /inspect/reg                   the module ledger
 *   GET /inspect/reg/completeness      cold-path face/honesty gaps
 *   GET /inspect/reg/<fqn>             one module's exports
 *   GET /inspect/reg/<fqn>/<func>      one export's detail
 *   GET /inspect/call/<fqn>/<func>     invoke a container export (RPC)
 *
 * The page is static bytes on every seat; everything below is fetched. There is
 * no theme switch here on purpose: the shell owns the one token palette. */
const params = new URLSearchParams(location.search);

const $ = (id) => document.getElementById(id);

function qbool(id) {
  const el = $(id);
  return !!(el && el.checked);
}

function pretty(el, text) {
  if (!el) return;
  try {
    el.textContent = JSON.stringify(JSON.parse(text), null, 2);
  } catch (_) {
    el.textContent = text;
  }
}

async function load(id, path) {
  const el = $(id);
  if (!el) return null;
  try {
    const r = await fetch(path);
    const t = await r.text();
    el.textContent = t;
    return t;
  } catch (e) {
    el.textContent = String(e);
    return null;
  }
}

async function loadCaps() {
  const text = await load("caps", "/capabilities");
  if (text === null) return null;
  let caps = null;
  try {
    caps = JSON.parse(text);
  } catch (_) {
    return null;
  }
  const role = $("role");
  if (role) {
    role.textContent = "role=" + (caps.role || "?") +
      " · static=" + (caps.static_backend || "?") +
      (caps.rpc ? " · rpc" : " · no rpc");
  }
  const rpcUi = $("rpc-ui");
  if (rpcUi) {
    rpcUi.style.display = caps.rpc ? "" : "none";
  }
  return caps;
}

async function loadCompleteness() {
  const summary = $("reg-summary");
  const treeEl = $("reg-tree");
  const modsEl = $("reg-modules");
  const qs = new URLSearchParams();
  if (qbool("gaps-only")) qs.set("gaps_only", "1");
  if (qbool("detail")) qs.set("detail", "1");
  qs.set("fmt", "tree");
  const treeUrl = "/inspect/reg/completeness?" + qs.toString();
  qs.set("fmt", "json");
  const jsonUrl = "/inspect/reg/completeness?" + qs.toString();
  try {
    const [tr, jr] = await Promise.all([fetch(treeUrl), fetch(jsonUrl)]);
    if (treeEl) treeEl.textContent = await tr.text();
    const j = JSON.parse(await jr.text());
    if (summary) {
      summary.textContent = "methods=" + (j.method_count || 0) +
        " gaps=" + (j.gap_count || 0) +
        " modules=" + ((j.modules && j.modules.length) || 0);
    }
    if (modsEl) {
      modsEl.textContent = "";
      (j.gaps || []).forEach((g) => modsEl.appendChild(gapButton(g)));
    }
  } catch (e) {
    if (treeEl) treeEl.textContent = String(e);
  }
}

/* One gap row: clicking it drills that method's detail into #reg-drill. */
function gapButton(g) {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = "gap-btn";
  btn.textContent = (g.module || "") + "." + (g.func || "") +
    "  miss=" + (g.miss || []).join(",") +
    (g.bad && g.bad.length ? "  bad=" + g.bad.join(",") : "");
  btn.addEventListener("click", async () => {
    const drill = $("reg-drill");
    try {
      /* Path form, not ?module=&func=: only the microdot adapter ever had the
       * query route, so the query shape reads back as a module named "method"
       * on a C-served seat. This one answers on every seat. */
      const r = await fetch("/inspect/reg/" + encodeURIComponent(g.module || "") +
        "/" + encodeURIComponent(g.func || ""));
      pretty(drill, await r.text());
    } catch (e) {
      if (drill) drill.textContent = String(e);
    }
  });
  return btn;
}

async function openModule(fqn) {
  const mod = $("rpc-module");
  if (mod) mod.value = fqn;
  try {
    const r = await fetch("/inspect/reg/" + encodeURIComponent(fqn));
    pretty($("rpc-out"), await r.text());
  } catch (e) {
    const out = $("rpc-out");
    if (out) out.textContent = String(e);
  }
}

/* Every module in the ledger becomes a clickable export listing. */
async function loadModules() {
  const navEl = $("mod-list");
  if (!navEl) return;
  try {
    const r = await fetch("/inspect/reg");
    const text = await r.text();
    pretty($("reg"), text);
    const mods = JSON.parse(text).modules || [];
    navEl.textContent = "";
    mods.forEach((m) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "mod-btn";
      btn.textContent = m;
      btn.addEventListener("click", () => openModule(m));
      navEl.appendChild(btn);
    });
  } catch (e) {
    navEl.textContent = String(e);
  }
}

function wireRpc() {
  const run = $("rpc-run");
  const mod = $("rpc-module");
  const func = $("rpc-func");
  const arg = $("rpc-arg");
  const out = $("rpc-out");
  if (!run || !mod || !func) return;
  run.addEventListener("click", async () => {
    const m = mod.value.trim();
    const f = func.value.trim();
    if (!m || !f) {
      if (out) out.textContent = "error: module and func required";
      return;
    }
    const a0 = arg ? arg.value.trim() : "";
    try {
      const r = await fetch("/inspect/call/" + encodeURIComponent(m) +
        "/" + encodeURIComponent(f) +
        (a0 ? "?a0=" + encodeURIComponent(a0) : ""));
      pretty(out, await r.text());
    } catch (e) {
      if (out) out.textContent = String(e);
    }
  });
}

/* One batch, not a chain: these five reads are independent, and the seat speaks
 * HTTP/1.0 without keep-alive — every request pays a fresh connection plus one
 * serve-loop tick, so a serial chain costs its length in round trips. That is
 * invisible on loopback and very visible from another machine. */
await Promise.all([
  load("health", "/health"),
  loadCaps(),
  load("self", "/inspect/self"),
  loadModules(),
  loadCompleteness(),
]);

const reload = $("reg-reload");
if (reload) reload.addEventListener("click", loadCompleteness);
["gaps-only", "detail"].forEach((id) => {
  const el = $(id);
  if (el) el.addEventListener("change", loadCompleteness);
});
wireRpc();

/* Deep link from a /packs/<fqn> page: open that card's live exports. */
const want = params.get("module");
if (want) await openModule(want);
