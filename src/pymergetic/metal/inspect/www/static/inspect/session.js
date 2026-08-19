/** Select binary/section/symbol + openInspect entry. */
import {
  MODES,
  artifactRoot,
  esc,
  fmtOff,
  fmtSize,
  inferChannel,
  pickBestLocIndex,
} from "./util.js";
import {
  fetchJson,
  loadAsm,
  loadHex,
  loadHexArtifact,
  loadMpyDisasm,
  loadSourceForLoc,
  resolveCodeSectionIndex,
} from "./http.js";
import { ensurePackageList, discoverSiblings, loadVersionOptions } from "./catalog.js";
import {
  ensureUi,
  paintActive,
  refreshChrome,
  renderLocBar,
  renderNav,
  setFocusPane,
  setMetaStatus,
  syncNavSelection,
  syncPaneTabs,
} from "./view.js";

export async function selectBinary() {
  const { state } = ensureUi();
  state.selected = { type: "binary" };
  state.locations = [];
  state.locIndex = 0;
  renderLocBar();
  syncNavSelection();
  const info = state.info || {};
  setMetaStatus(
    (info.kind || "binary") +
      (info.encoding ? " · " + info.encoding : "") +
      (info.signed ? " · signed" : "") +
      " · " +
      fmtSize(info.naked_size != null ? info.naked_size : info.size || 0)
  );
  state.asmHtml =
    '<span class="muted">Whole artifact — pick a code section or symbol for disassembly.</span>';
  state.sourceHtml =
    '<span class="muted">Whole artifact — pick a symbol for source / DWARF.</span>';
  await loadHexArtifact();
  paintActive();
}

export async function selectSection(sec) {
  const { state } = ensureUi();
  state.selected = { type: "section", index: Number(sec.index), name: sec.name };
  state.locations = [];
  state.locIndex = 0;
  renderLocBar();
  syncNavSelection();
  const role = sec.role || "other";
  setMetaStatus(
    role +
      (sec.type_id != null ? " · type " + sec.type_id : "") +
      " · " +
      fmtSize(sec.size || 0) +
      (sec.offset != null ? " · file off " + fmtOff(sec.offset) : "")
  );
  state.sourceHtml =
    '<span class="muted">Container section — pick a symbol for source / DWARF.</span>';
  const jobs = [loadHex(sec.index, 0, sec.size > 0 ? sec.size : null)];
  if (role === "code") {
    jobs.push(loadAsm(sec.index, 0, sec.size > 0 ? sec.size : null));
  } else {
    state.asmHtml =
      '<span class="muted">Non-code section — hex only (or pick a symbol).</span>';
  }
  await Promise.all(jobs);
  paintActive();
}

export async function selectSymbol(sym) {
  const { state } = ensureUi();
  state.selected = { type: "symbol", name: sym.name, symbol: sym };
  syncNavSelection();
  setMetaStatus(
    (sym.kind || "sym") +
      (sym.binding ? " · " + sym.binding : "") +
      " · off=" +
      (sym.offset != null ? fmtOff(sym.offset) : "?") +
      " · " +
      fmtSize(sym.size || 0) +
      (sym.section_index != null ? " · section " + sym.section_index : "")
  );

  const root = artifactRoot(state.opts);
  try {
    state.locations = await fetchJson(
      `${root}/locations?name=${encodeURIComponent(sym.name)}`
    );
  } catch (_) {
    state.locations = [];
  }
  state.locIndex = pickBestLocIndex(state.locations);
  renderLocBar();

  let sec =
    sym.section_index != null && Number.isFinite(Number(sym.section_index))
      ? Number(sym.section_index)
      : null;
  const kind = String(sym.kind || "");
  const wantsCode = kind === "export" || kind === "func" || kind === "data";
  if ((sec == null || !Number.isFinite(sec)) && wantsCode) {
    sec = await resolveCodeSectionIndex();
  }
  const off = Number(sym.offset) || 0;
  const size = Number(sym.size) || 0;
  const win = size > 0 ? size : null;
  const jobs = [loadSourceForLoc()];
  if (sec != null && Number.isFinite(sec) && sec < 65500) {
    jobs.push(loadHex(sec, off, win), loadAsm(sec, off, win));
  } else {
    const msg = wantsCode
      ? '<span class="muted">No section index for hex/asm.</span>'
      : `<span class="muted">No code section for ${esc(kind || "this")} symbol.</span>`;
    state.hexHtml = msg;
    state.asmHtml = msg;
  }
  await Promise.all(jobs);
  paintActive();
}

export async function openInspect(opts) {
  if (!opts || !opts.filename) {
    console.warn("openInspect requires { filename }");
    return;
  }
  const { dialog, els, state } = ensureUi();
  if (opts.tab && MODES.includes(opts.tab)) {
    state.paneA = opts.tab;
  } else if (opts.mpyPath) {
    state.paneA = "asm";
    state.paneB = "source";
  } else if (opts.sourcePath) {
    state.paneA = "source";
    state.paneB = "hex";
  } else if (opts.symbol || opts.addr != null) {
    if (!state._seededForSymbol) {
      state.paneA = "asm";
      state.paneB = "source";
      state._seededForSymbol = true;
    }
  }
  state.opts = {
    filename: opts.filename,
    version: opts.version || null,
    package: opts.package || null,
    channel: inferChannel(opts),
    sectionIndex: opts.sectionIndex != null ? Number(opts.sectionIndex) : null,
    mpyPath: opts.mpyPath || null,
    sourcePath: opts.sourcePath || null,
  };
  state.symbols = [];
  state.sections = [];
  state.siblings = discoverSiblings(opts);
  state.info = null;
  state.selected = null;
  state.locations = [];
  state.locIndex = 0;
  state.hasDwarf = false;
  state.codeSectionIndex = null;
  state.hexHtml = "";
  state.asmHtml = "";
  state.sourceHtml = "";
  renderLocBar();
  refreshChrome();
  setMetaStatus("Loading…");
  els.bodyA.textContent = "Loading…";
  els.bodyB.textContent = "Loading…";
  syncPaneTabs();
  setFocusPane(state.focusPane || "a");
  dialog.showModal();
  try {
    els.symbolSel.focus({ preventScroll: true });
  } catch (_) {
    els.symbolSel.focus();
  }

  const catalogP = (async () => {
    await ensurePackageList();
    if (state.opts.package) await loadVersionOptions(state.opts.package);
  })().catch(() => {});

  const root = artifactRoot(state.opts);
  const inspectP = fetchJson(`${root}/inspect`)
    .then((info) => {
      state.info = info || null;
      state.hasDwarf = !!(info && info.has_dwarf);
      if (info && Array.isArray(info.sections) && info.sections.length) {
        state.sections = info.sections;
      }
    })
    .catch(() => {
      state.info = null;
      state.hasDwarf = false;
    });
  const sectionsP = fetchJson(`${root}/sections`)
    .then((secs) => {
      if (Array.isArray(secs) && secs.length) state.sections = secs;
    })
    .catch(() => {});
  try {
    state.symbols = await fetchJson(`${root}/symbols`);
  } catch (err) {
    setMetaStatus("symbols: " + (err.message || err));
    state.symbols = [];
  }
  await Promise.all([inspectP, sectionsP, catalogP]);
  refreshChrome();
  renderNav();

  if (opts.mpyPath) {
    setMetaStatus("mpy " + opts.mpyPath);
    state.selected = { type: "symbol", name: opts.mpyPath };
    syncNavSelection();
    state.hexHtml =
      '<span class="muted">Embedded .mpy — use asm view for mpy-dis.</span>';
    state.sourceHtml =
      `<span class="muted">path</span> <code>${esc(opts.mpyPath)}</code>` +
      ` <span class="muted">(prefer twin .py via source tree)</span>`;
    await loadMpyDisasm(opts.mpyPath);
    const twin = String(opts.mpyPath)
      .replace(/\.upy\.mpy\d+\.sib\d+\.mpy$/i, ".py")
      .replace(/\.mpy$/i, ".py");
    if (twin !== opts.mpyPath) {
      try {
        const meta = await fetchJson(
          `${root}/files?path=${encodeURIComponent(twin)}`
        );
        if (meta && meta.text != null) {
          state.locations = [{ path: twin, line: null, role: "twin" }];
          state.locIndex = 0;
          renderLocBar();
          await loadSourceForLoc();
        }
      } catch (_) {
        /* twin optional */
      }
    }
    paintActive();
    return;
  }

  if (opts.sourcePath) {
    setMetaStatus("source " + opts.sourcePath);
    state.selected = { type: "symbol", name: opts.sourcePath };
    syncNavSelection();
    state.locations = [{ path: opts.sourcePath, line: null, role: "embed" }];
    state.locIndex = 0;
    renderLocBar();
    state.hexHtml =
      '<span class="muted">Embedded pack/source file — use the source pane.</span>';
    state.asmHtml =
      '<span class="muted">No asm for embedded source files.</span>';
    await loadSourceForLoc();
    paintActive();
    return;
  }

  if (opts.symbol) {
    const hit =
      state.symbols.find((s) => s.name === opts.symbol) || {
        name: opts.symbol,
        section_index: opts.sectionIndex,
        offset: opts.addr != null ? opts.addr : 0,
        size: 0,
        kind: "other",
      };
    await selectSymbol(hit);
    return;
  }

  if (opts.addr != null) {
    const addr = Number(opts.addr);
    setMetaStatus("addr=" + fmtOff(addr));
    try {
      state.locations = await fetchJson(
        `${root}/addr2line?addr=${encodeURIComponent(String(addr))}`
      );
    } catch (err) {
      state.locations = [];
      state.sourceHtml = esc(err.message || err);
    }
    state.locIndex = pickBestLocIndex(state.locations);
    renderLocBar();
    let sec = state.opts.sectionIndex;
    if (sec == null || !Number.isFinite(Number(sec))) {
      sec = await resolveCodeSectionIndex();
    }
    if (sec != null && Number.isFinite(sec)) {
      const hit = state.sections.find((s) => Number(s.index) === Number(sec));
      state.selected = hit
        ? { type: "section", index: Number(hit.index), name: hit.name }
        : { type: "section", index: Number(sec) };
      syncNavSelection();
    }
    const jobs = [loadSourceForLoc()];
    if (sec != null && Number.isFinite(sec)) {
      jobs.push(loadHex(sec, addr), loadAsm(sec, addr));
    } else {
      jobs.push(loadAsm(0, addr));
      state.hexHtml =
        '<span class="muted">No section index (asm may still work for Wasm).</span>';
    }
    await Promise.all(jobs);
    paintActive();
    return;
  }

  if (state.opts.sectionIndex != null && Number.isFinite(state.opts.sectionIndex)) {
    const hit =
      state.sections.find(
        (s) => Number(s.index) === Number(state.opts.sectionIndex)
      ) || {
        index: state.opts.sectionIndex,
        name: "section_" + state.opts.sectionIndex,
        role: "other",
        size: 0,
      };
    await selectSection(hit);
    return;
  }

  await selectBinary();
}
