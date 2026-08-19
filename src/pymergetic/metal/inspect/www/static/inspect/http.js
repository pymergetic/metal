/** Artifact fetch helpers (hex / asm / source / mpy). */
import {
  DISASM_LIMIT,
  HEX_PREVIEW,
  MPY_DISASM_LIMIT,
  artifactRoot,
  esc,
} from "./util.js";
import { formatAsm, formatSourceSnippet, hexdumpHtml } from "./format.js";
import { requireUi } from "./ctx.js";

export async function fetchJson(url) {
  const res = await fetch(url, { headers: { Accept: "application/json" } });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.detail || res.status);
  return data;
}

export async function loadHex(sectionIndex, offset, limit) {
  const { state } = requireUi();
  const root = artifactRoot(state.opts);
  const off = Number(offset) || 0;
  const lim =
    limit != null && Number.isFinite(Number(limit)) && Number(limit) > 0
      ? Math.min(Number(limit), HEX_PREVIEW)
      : HEX_PREVIEW;
  const url =
    `${root}/sections/raw?index=${encodeURIComponent(String(sectionIndex))}` +
    `&offset=${encodeURIComponent(String(off))}` +
    `&limit=${encodeURIComponent(String(lim))}`;
  const res = await fetch(url, { headers: { Accept: "application/octet-stream" } });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    state.hexHtml = esc(err.detail || "hex load failed (" + res.status + ")");
    return;
  }
  const buf = await res.arrayBuffer();
  state.hexHtml = hexdumpHtml(buf, lim, off);
}

export async function loadAsm(sectionIndex, offset, limit) {
  const { state } = requireUi();
  const root = artifactRoot(state.opts);
  const lim =
    limit != null && Number.isFinite(Number(limit)) && Number(limit) > 0
      ? Math.min(Number(limit), DISASM_LIMIT)
      : DISASM_LIMIT;
  const url =
    `${root}/disasm?index=${encodeURIComponent(String(sectionIndex))}` +
    `&offset=${encodeURIComponent(String(offset || 0))}` +
    `&limit=${encodeURIComponent(String(lim))}`;
  try {
    const lines = await fetchJson(url);
    state.asmHtml = formatAsm(lines);
  } catch (err) {
    state.asmHtml = esc(err.message || err);
  }
}

export async function loadMpyDisasm(path) {
  const { state } = requireUi();
  const root = artifactRoot(state.opts);
  const url =
    `${root}/files/mpy-disasm?path=${encodeURIComponent(path)}` +
    `&limit=${MPY_DISASM_LIMIT}`;
  try {
    const lines = await fetchJson(url);
    state.asmHtml =
      `<div class="muted">mpy · ${esc(path)}</div>\n` + formatAsm(lines);
  } catch (err) {
    state.asmHtml = esc(err.message || err);
  }
}

export async function loadSourceForLoc() {
  const { state } = requireUi();
  const loc = state.locations[state.locIndex];
  if (!loc) {
    state.sourceHtml = '<span class="muted">No location.</span>';
    return;
  }
  const role = loc.role || "";
  if (role === "sym") {
    state.sourceHtml =
      `<span class="muted">symbol</span> <code>${esc(loc.path)}</code>` +
      (loc.line != null ? ` <span class="muted">line ${esc(loc.line)}</span>` : "");
    return;
  }
  const root = artifactRoot(state.opts);
  const path = loc.path;
  try {
    const meta = await fetchJson(`${root}/files?path=${encodeURIComponent(path)}`);
    if (meta.binary || meta.text == null) {
      state.sourceHtml = `<span class="muted">binary ${esc(path)}</span>`;
      return;
    }
    state.sourceHtml = formatSourceSnippet(path, meta.text, loc, role);
  } catch (err) {
    state.sourceHtml =
      `<code>${esc(loc.path)}</code>` +
      (loc.line != null ? `:${esc(loc.line)}` : "") +
      ` <span class="muted">(${esc(role)}) — ${esc(err.message || err)}</span>`;
  }
}

export async function resolveCodeSectionIndex() {
  const { state } = requireUi();
  if (state.codeSectionIndex != null && Number.isFinite(state.codeSectionIndex)) {
    return state.codeSectionIndex;
  }
  const fromState = (state.sections || []).find(
    (s) =>
      s.role === "code" ||
      s.name === "code" ||
      s.name === ".text" ||
      Number(s.type_id) === 10
  );
  if (fromState && fromState.index != null && Number.isFinite(Number(fromState.index))) {
    state.codeSectionIndex = Number(fromState.index);
    return state.codeSectionIndex;
  }
  const root = artifactRoot(state.opts);
  try {
    const secs = await fetchJson(`${root}/sections`);
    state.sections = secs || state.sections;
    const code = (secs || []).find(
      (s) =>
        s.role === "code" ||
        s.name === "code" ||
        s.name === ".text" ||
        Number(s.type_id) === 10
    );
    if (code && code.index != null && Number.isFinite(Number(code.index))) {
      state.codeSectionIndex = Number(code.index);
      return state.codeSectionIndex;
    }
  } catch (_) {
    /* fall through */
  }
  return null;
}

export async function loadHexArtifact() {
  const { state } = requireUi();
  const root = artifactRoot(state.opts);
  const res = await fetch(root, { headers: { Accept: "application/octet-stream" } });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    state.hexHtml = esc(err.detail || "artifact load failed (" + res.status + ")");
    return;
  }
  const buf = await res.arrayBuffer();
  state.hexHtml = hexdumpHtml(buf, HEX_PREVIEW, 0);
}
