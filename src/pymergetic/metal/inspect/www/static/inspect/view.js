/** Inspect dialog DOM, nav chrome, dual-pane paint, keybindings. */
import { MODES, esc, fmtSize } from "./util.js";
import { hooks, setUi, ui } from "./ctx.js";

function paneTabsHtml(id) {
  return MODES.map(
    (m) =>
      `<button type="button" class="inspect-tab" data-pane="${id}" data-tab="${m}" role="tab" aria-selected="false">${m}</button>`
  ).join("");
}

export function ensureUi() {
  if (ui) {
    if (
      !ui.els.packageSel ||
      !document.getElementById("inspect-package-sel") ||
      !document.getElementById("inspect-body-a")
    ) {
      try {
        ui.dialog.remove();
      } catch (_) {
        /* ignore */
      }
      setUi(null);
    } else {
      return ui;
    }
  }
  const dialog = document.createElement("dialog");
  dialog.id = "inspect-dialog";
  dialog.className = "source-dialog inspect-dialog";
  dialog.innerHTML = `
      <form method="dialog" class="source-dialog-head">
        <strong id="inspect-dialog-title">Inspect</strong>
        <div class="source-dialog-actions">
          <button type="submit" class="icon-btn" value="close" aria-label="Close">✕</button>
        </div>
      </form>
      <div class="inspect-commander">
        <div id="inspect-nav" class="inspect-nav">
          <label class="inspect-nav-field">
            <span class="inspect-nav-label">package</span>
            <select id="inspect-package-sel" class="inspect-nav-select" aria-label="Package"></select>
          </label>
          <label class="inspect-nav-field">
            <span class="inspect-nav-label">version</span>
            <select id="inspect-version-sel" class="inspect-nav-select" aria-label="Channel / version"></select>
          </label>
          <label class="inspect-nav-field">
            <span class="inspect-nav-label">artifact</span>
            <select id="inspect-artifact-sel" class="inspect-nav-select" aria-label="Sibling artifact"></select>
          </label>
          <label class="inspect-nav-field">
            <span class="inspect-nav-label">section</span>
            <select id="inspect-section-sel" class="inspect-nav-select" aria-label="Container section"></select>
          </label>
          <label class="inspect-nav-field">
            <span class="inspect-nav-label">symbol</span>
            <select id="inspect-symbol-sel" class="inspect-nav-select" aria-label="Symbol"></select>
          </label>
          <div id="inspect-meta" class="inspect-nav-meta muted"></div>
        </div>
        <div id="inspect-loc-bar" class="inspect-loc-bar" hidden></div>
        <div class="inspect-panes">
          <div class="inspect-pane is-focus" data-pane="a" id="inspect-pane-a">
            <div class="inspect-tabs" role="tablist" aria-label="View A">${paneTabsHtml("a")}</div>
            <pre id="inspect-body-a" class="source-body hex-body"></pre>
          </div>
          <div class="inspect-pane" data-pane="b" id="inspect-pane-b">
            <div class="inspect-tabs" role="tablist" aria-label="View B">${paneTabsHtml("b")}</div>
            <pre id="inspect-body-b" class="source-body hex-body"></pre>
          </div>
        </div>
      </div>`;
  document.body.appendChild(dialog);

  const els = {
    title: dialog.querySelector("#inspect-dialog-title"),
    nav: dialog.querySelector("#inspect-nav"),
    packageSel: dialog.querySelector("#inspect-package-sel"),
    versionSel: dialog.querySelector("#inspect-version-sel"),
    artifactSel: dialog.querySelector("#inspect-artifact-sel"),
    sectionSel: dialog.querySelector("#inspect-section-sel"),
    symbolSel: dialog.querySelector("#inspect-symbol-sel"),
    locBar: dialog.querySelector("#inspect-loc-bar"),
    meta: dialog.querySelector("#inspect-meta"),
    bodyA: dialog.querySelector("#inspect-body-a"),
    bodyB: dialog.querySelector("#inspect-body-b"),
    paneA: dialog.querySelector("#inspect-pane-a"),
    paneB: dialog.querySelector("#inspect-pane-b"),
    tabs: dialog.querySelectorAll(".inspect-tab"),
  };

  const state = {
    opts: null,
    packages: [],
    versions: [],
    symbols: [],
    sections: [],
    siblings: [],
    info: null,
    selected: null,
    locations: [],
    locIndex: 0,
    hasDwarf: false,
    navQuiet: false,
    paneA: "hex",
    paneB: "source",
    focusPane: "a",
    hexHtml: "",
    asmHtml: "",
    sourceHtml: "",
  };

  els.packageSel.addEventListener("change", () => {
    if (state.navQuiet || !state.opts || !hooks.navigateCatalog) return;
    const name = els.packageSel.value;
    if (!name || name === state.opts.package) return;
    hooks.navigateCatalog({ package: name, channel: "lead" });
  });
  els.versionSel.addEventListener("change", () => {
    if (state.navQuiet || !state.opts || !hooks.navigateCatalog) return;
    const channel = els.versionSel.value;
    if (!channel || channel === state.opts.channel) return;
    hooks.navigateCatalog({
      package: state.opts.package,
      channel,
      filename: state.opts.filename,
    });
  });
  els.artifactSel.addEventListener("change", () => {
    if (state.navQuiet || !state.opts || !hooks.openInspect) return;
    const filename = els.artifactSel.value;
    if (!filename || filename === state.opts.filename) return;
    const sib = (state.siblings || []).find((s) => s.filename === filename) || {};
    hooks.openInspect({
      package: sib.package || state.opts.package,
      filename,
      version: sib.version != null ? sib.version : state.opts.version,
      channel: state.opts.channel,
      siblings: state.siblings,
      tab: state.paneA,
    });
  });
  els.sectionSel.addEventListener("change", () => {
    if (state.navQuiet || !state.opts) return;
    const v = els.sectionSel.value;
    if (v === "" || v === "binary") {
      state.navQuiet = true;
      try {
        els.symbolSel.value = "";
      } finally {
        state.navQuiet = false;
      }
      if (hooks.selectBinary) hooks.selectBinary();
      return;
    }
    const idx = Number(v);
    const sec = state.sections.find((s) => Number(s.index) === idx);
    if (!sec) return;
    state.navQuiet = true;
    try {
      els.symbolSel.value = "";
    } finally {
      state.navQuiet = false;
    }
    if (hooks.selectSection) hooks.selectSection(sec);
  });
  els.symbolSel.addEventListener("change", () => {
    if (state.navQuiet || !state.opts) return;
    const name = els.symbolSel.value;
    if (!name) {
      const secVal = els.sectionSel.value;
      if (secVal && secVal !== "binary") {
        const sec = state.sections.find((s) => Number(s.index) === Number(secVal));
        if (sec && hooks.selectSection) {
          hooks.selectSection(sec);
          return;
        }
      }
      if (hooks.selectBinary) hooks.selectBinary();
      return;
    }
    const sym = state.symbols.find((s) => s.name === name);
    if (sym && hooks.selectSymbol) hooks.selectSymbol(sym);
  });
  els.tabs.forEach((tab) => {
    tab.addEventListener("click", () => {
      const pane = tab.dataset.pane === "b" ? "b" : "a";
      setPaneMode(pane, tab.dataset.tab);
      setFocusPane(pane);
    });
  });
  els.paneA.addEventListener("mousedown", () => setFocusPane("a"));
  els.paneB.addEventListener("mousedown", () => setFocusPane("b"));
  els.locBar.addEventListener("click", (ev) => {
    const btn = ev.target.closest("[data-loc]");
    if (!btn) return;
    state.locIndex = Number(btn.dataset.loc);
    renderLocBar();
    import("./http.js").then(({ loadSourceForLoc }) =>
      loadSourceForLoc().then(() => paintActive())
    );
  });
  dialog.addEventListener("keydown", (ev) => {
    const tag = (document.activeElement && document.activeElement.tagName) || "";
    const inSelect = tag === "SELECT";
    if (ev.key === "/" && !inSelect) {
      if (tag !== "INPUT" && tag !== "TEXTAREA") {
        ev.preventDefault();
        els.symbolSel.focus();
      }
      return;
    }
    if (inSelect) return;
    if (ev.key === "Tab" && !ev.altKey && !ev.metaKey && !ev.ctrlKey) {
      if (tag !== "INPUT" && tag !== "TEXTAREA" && tag !== "SELECT" && tag !== "BUTTON") {
        ev.preventDefault();
        setFocusPane(state.focusPane === "a" ? "b" : "a");
      }
      return;
    }
    if (ev.key === "ArrowDown" || ev.key === "ArrowUp") {
      ev.preventDefault();
      moveSymbol(ev.key === "ArrowDown" ? 1 : -1);
      return;
    }
    if (ev.key === "[" || ev.key === "]") {
      ev.preventDefault();
      moveLoc(ev.key === "]" ? 1 : -1);
      return;
    }
    if (ev.key === "?") {
      ev.preventDefault();
      flashMeta("keys: / ↑↓ [ ] c Tab 1/2/3 Esc");
      return;
    }
    if (ev.key === "c" || ev.key === "C") {
      ev.preventDefault();
      copyActiveLocation();
      return;
    }
    if (ev.key === "1") {
      ev.preventDefault();
      setPaneMode(state.focusPane, "hex");
    } else if (ev.key === "2") {
      ev.preventDefault();
      setPaneMode(state.focusPane, "asm");
    } else if (ev.key === "3") {
      ev.preventDefault();
      setPaneMode(state.focusPane, "source");
    }
  });

  return setUi({ dialog, els, state });
}

export function flashMeta(msg) {
  const { els } = ensureUi();
  const prev = els.meta.textContent;
  els.meta.textContent = msg;
  setTimeout(() => {
    if (els.meta.textContent === msg) els.meta.textContent = prev;
  }, 1600);
}

export function refreshChrome() {
  const { els, state } = ensureUi();
  if (!state.opts) return;
  const bits = [];
  if (state.opts.package) bits.push(state.opts.package);
  if (state.hasDwarf) bits.push("dwarf");
  els.title.textContent = bits.length ? bits.join(" · ") : "Inspect";
}

export function setMetaStatus(text) {
  ensureUi().els.meta.textContent = text || "";
}

export function setFocusPane(pane) {
  const { els, state } = ensureUi();
  state.focusPane = pane === "b" ? "b" : "a";
  els.paneA.classList.toggle("is-focus", state.focusPane === "a");
  els.paneB.classList.toggle("is-focus", state.focusPane === "b");
}

export function setPaneMode(pane, mode) {
  const { state } = ensureUi();
  if (!MODES.includes(mode)) return;
  if (pane === "b") state.paneB = mode;
  else state.paneA = mode;
  syncPaneTabs();
  paintActive();
}

export function syncPaneTabs() {
  const { els, state } = ensureUi();
  els.tabs.forEach((t) => {
    const pane = t.dataset.pane === "b" ? "b" : "a";
    const mode = pane === "b" ? state.paneB : state.paneA;
    const on = t.dataset.tab === mode;
    t.classList.toggle("is-active", on);
    t.setAttribute("aria-selected", on ? "true" : "false");
  });
}

function htmlForMode(mode) {
  const { state } = ensureUi();
  if (mode === "hex") {
    return {
      cls: "source-body hex-body",
      html: state.hexHtml || '<span class="muted">No hex loaded.</span>',
    };
  }
  if (mode === "asm") {
    return {
      cls: "source-body hex-body",
      html: state.asmHtml || '<span class="muted">No disassembly.</span>',
    };
  }
  return {
    cls: "source-body",
    html: state.sourceHtml || '<span class="muted">No source location.</span>',
  };
}

export function paintPane(pane) {
  const { els, state } = ensureUi();
  const mode = pane === "b" ? state.paneB : state.paneA;
  const body = pane === "b" ? els.bodyB : els.bodyA;
  const { cls, html } = htmlForMode(mode);
  delete body.dataset.highlighted;
  body.className = cls;
  body.innerHTML = html;
  if (mode === "source") {
    const hit = body.querySelector(".inspect-src-hit");
    if (hit && typeof hit.scrollIntoView === "function") {
      hit.scrollIntoView({ block: "nearest" });
    }
  }
}

export function paintActive() {
  syncPaneTabs();
  paintPane("a");
  paintPane("b");
}

function moveSymbol(delta) {
  const { els, state } = ensureUi();
  const names = (state.symbols || []).map((s) => s.name);
  if (!names.length) return;
  const cur = els.symbolSel.value;
  let idx = names.indexOf(cur);
  if (idx < 0) idx = delta > 0 ? -1 : 0;
  idx = Math.max(0, Math.min(names.length - 1, idx + delta));
  const sym = state.symbols.find((s) => s.name === names[idx]);
  if (sym && hooks.selectSymbol) {
    els.symbolSel.value = sym.name;
    hooks.selectSymbol(sym);
  }
}

function moveLoc(delta) {
  const { state } = ensureUi();
  if (!state.locations || state.locations.length < 2) return;
  const n = state.locations.length;
  state.locIndex = (state.locIndex + delta + n) % n;
  renderLocBar();
  import("./http.js").then(({ loadSourceForLoc }) =>
    loadSourceForLoc().then(() => paintActive())
  );
}

function copyActiveLocation() {
  const { state } = ensureUi();
  const loc = state.locations && state.locations[state.locIndex];
  let text = "";
  if (loc && loc.path) {
    text = loc.path + (loc.line != null ? ":" + loc.line : "");
  } else if (state.selected && state.selected.name) {
    text = state.selected.name;
  }
  if (!text) return;
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).then(() => flashMeta("copied " + text)).catch(() => {});
  }
}

export function renderLocBar() {
  const { els, state } = ensureUi();
  if (!state.locations || state.locations.length < 1) {
    els.locBar.hidden = true;
    els.locBar.innerHTML = "";
    return;
  }
  els.locBar.hidden = false;
  let html = `<span class="muted">locations</span>`;
  state.locations.forEach((loc, i) => {
    const label =
      loc.path + (loc.line != null ? ":" + loc.line : "") + " (" + (loc.role || "?") + ")";
    html += `<button type="button" class="inspect-loc-btn${
      i === state.locIndex ? " is-active" : ""
    }" data-loc="${i}">${esc(label)}</button>`;
  });
  els.locBar.innerHTML = html;
}

export function syncNavSelection() {
  const { els, state } = ensureUi();
  if (!els.sectionSel || !els.symbolSel) return;
  state.navQuiet = true;
  try {
    if (els.packageSel && state.opts && state.opts.package) {
      els.packageSel.value = state.opts.package;
    }
    if (els.versionSel && state.opts) {
      els.versionSel.value = state.opts.channel || "lead";
    }
    const sel = state.selected;
    if (!sel || sel.type === "binary") {
      els.sectionSel.value = "binary";
      els.symbolSel.value = "";
    } else if (sel.type === "section") {
      els.sectionSel.value = String(sel.index);
      els.symbolSel.value = "";
    } else if (sel.type === "symbol") {
      els.symbolSel.value = sel.name || "";
      if (sel.symbol && sel.symbol.section_index != null) {
        els.sectionSel.value = String(sel.symbol.section_index);
      }
    }
    if (els.artifactSel && state.opts) {
      els.artifactSel.value = state.opts.filename;
    }
  } finally {
    state.navQuiet = false;
  }
}

export function renderNav() {
  const { els, state } = ensureUi();
  if (!els.artifactSel || !els.sectionSel || !els.symbolSel) return;
  state.navQuiet = true;
  try {
    const pkg = (state.opts && state.opts.package) || "";
    const pkgs = state.packages && state.packages.length
      ? state.packages.slice()
      : pkg
        ? [pkg]
        : [];
    if (pkg && !pkgs.includes(pkg)) pkgs.unshift(pkg);
    const meta = state.packageMeta || {};
    els.packageSel.innerHTML = pkgs
      .map((n) => {
        const role = meta[n] && meta[n].role;
        const label = role ? `${n} · ${role}` : n;
        return `<option value="${esc(n)}"${n === pkg ? " selected" : ""}>${esc(label)}</option>`;
      })
      .join("");
    els.packageSel.disabled = pkgs.length < 1;

    const channel = (state.opts && state.opts.channel) || "lead";
    const vers = state.versions && state.versions.length
      ? state.versions
      : [{ channel, version: "", label: channel }];
    els.versionSel.innerHTML = vers
      .map((v) => {
        const label = v.label || v.channel;
        return `<option value="${esc(v.channel)}"${
          v.channel === channel ? " selected" : ""
        }>${esc(label)}</option>`;
      })
      .join("");
    els.versionSel.disabled = vers.length < 1;

    const sibs = state.siblings && state.siblings.length
      ? state.siblings
      : [{ filename: state.opts.filename, package: state.opts.package, version: state.opts.version }];
    els.artifactSel.innerHTML = sibs
      .map(
        (s) =>
          `<option value="${esc(s.filename)}"${
            s.filename === state.opts.filename ? " selected" : ""
          }>${esc(s.filename)}</option>`
      )
      .join("");
    els.artifactSel.disabled = sibs.length < 2;

    let secHtml = `<option value="binary">Whole binary</option>`;
    for (const s of state.sections || []) {
      const label =
        "#" +
        s.index +
        " · " +
        (s.name || "section_" + s.index) +
        (s.role ? " · " + s.role : "") +
        (s.size != null ? " · " + fmtSize(s.size) : "");
      secHtml += `<option value="${esc(s.index)}">${esc(label)}</option>`;
    }
    els.sectionSel.innerHTML = secHtml;
    els.sectionSel.disabled = !(state.sections && state.sections.length);

    let symHtml = `<option value="">—</option>`;
    for (const s of state.symbols || []) {
      const meta = [s.kind, s.binding].filter(Boolean).join(" · ");
      const label = meta ? s.name + " · " + meta : s.name;
      symHtml += `<option value="${esc(s.name)}">${esc(label)}</option>`;
    }
    els.symbolSel.innerHTML = symHtml;
    els.symbolSel.disabled = !(state.symbols && state.symbols.length);

    syncNavSelection();
  } finally {
    state.navQuiet = false;
  }
}
