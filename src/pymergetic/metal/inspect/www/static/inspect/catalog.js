/** Package / version catalog navigation for inspect. */
import {
  cdnPrefix,
  inferChannel,
  pickDefaultArtifact,
  pinVersionFromChannel,
} from "./util.js";
import { fetchJson } from "./http.js";
import { hooks, requireUi } from "./ctx.js";
import { flashMeta, setMetaStatus } from "./view.js";

function roleRank(role) {
  if (role === "engine") return 0;
  if (role === "host") return 1;
  if (role === "kernel") return 2;
  if (role === "arch") return 3;
  return 4;
}

export async function ensurePackageList() {
  const { state } = requireUi();
  if (state.packages && state.packages.length) return;
  const root = cdnPrefix();
  try {
    const rows = await fetchJson(`${root}/packages?channel=lead`);
    const rich = (rows || [])
      .filter((r) => r && r.name)
      .map((r) => ({ name: r.name, role: r.role || null }));
    rich.sort((a, b) => {
      const rr = roleRank(a.role) - roleRank(b.role);
      return rr !== 0 ? rr : a.name.localeCompare(b.name);
    });
    state.packageMeta = Object.fromEntries(rich.map((r) => [r.name, r]));
    state.packages = rich.map((r) => r.name);
  } catch (_) {
    state.packageMeta = {};
    state.packages = state.opts && state.opts.package ? [state.opts.package] : [];
  }
}

export async function loadVersionOptions(packageName) {
  const { state } = requireUi();
  if (!packageName) {
    state.versions = [];
    return;
  }
  const root = cdnPrefix();
  try {
    state.versions = await fetchJson(
      `${root}/packages/${encodeURIComponent(packageName)}/versions`
    );
  } catch (_) {
    state.versions = [
      {
        channel: inferChannel(state.opts || {}),
        version: (state.opts && state.opts.version) || "?",
        label: inferChannel(state.opts || {}),
      },
    ];
  }
}

export async function navigateCatalog({ package: pkg, channel, filename }) {
  const { state } = requireUi();
  if (!pkg || !hooks.openInspect) return;
  setMetaStatus("Loading " + pkg + "…");
  const root = cdnPrefix();
  const ch = channel || "lead";
  try {
    await loadVersionOptions(pkg);
    const entry = await fetchJson(
      `${root}/packages/${encodeURIComponent(pkg)}?channel=${encodeURIComponent(ch)}`
    );
    const pinVer = pinVersionFromChannel(ch);
    const siblings = (entry.artifacts || []).map((a) => ({
      filename: a.path,
      package: pkg,
      version: pinVer,
    }));
    const file = pickDefaultArtifact(siblings, filename);
    if (!file) {
      flashMeta("no artifacts in " + pkg);
      return;
    }
    await hooks.openInspect({
      package: pkg,
      filename: file,
      version: pinVer,
      channel: ch,
      siblings,
      tab: state.paneA,
    });
  } catch (err) {
    flashMeta(String(err.message || err));
  }
}

export function discoverSiblings(opts) {
  if (opts.siblings && opts.siblings.length) {
    return opts.siblings.slice();
  }
  const out = [];
  const seen = new Set();
  document.querySelectorAll("#artifact-list .artifact-row").forEach((row) => {
    const filename = row.dataset.filename;
    if (!filename || seen.has(filename)) return;
    if (
      opts.package &&
      row.dataset.package &&
      row.dataset.package !== opts.package
    ) {
      return;
    }
    seen.add(filename);
    out.push({
      filename,
      package: row.dataset.package || opts.package || null,
      version: row.dataset.version || opts.version || null,
    });
  });
  if (!seen.has(opts.filename)) {
    out.unshift({
      filename: opts.filename,
      package: opts.package || null,
      version: opts.version || null,
    });
  }
  return out;
}
