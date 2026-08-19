/** Inspect shared constants + pure helpers. */
export const HEX_PREVIEW = 65536;
export const DISASM_LIMIT = 64;
export const MPY_DISASM_LIMIT = 96;
export const MODES = ["hex", "asm", "source"];

export function esc(s) {
  return String(s).replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]
  );
}

export function fmtSize(n) {
  const bytes = Number(n);
  if (!Number.isFinite(bytes) || bytes < 0) return "?";
  const exact = Math.trunc(bytes) + " B";
  if (bytes < 1024) return exact;
  const units = ["KiB", "MiB", "GiB", "TiB"];
  let value = bytes;
  let unit = units[0];
  for (let i = 0; i < units.length; i++) {
    value /= 1024;
    unit = units[i];
    if (value < 1024) break;
  }
  const pretty =
    value >= 100 ? value.toFixed(0) : value >= 10 ? value.toFixed(1) : value.toFixed(2);
  return pretty + " " + unit + " (" + exact + ")";
}

export function fmtOff(n) {
  const v = Number(n);
  if (!Number.isFinite(v)) return "?";
  const i = Math.trunc(v);
  if (i < 0) return String(i);
  return "0x" + i.toString(16);
}

/** Prefer dwarf, then def/decl, then twin, over role=sym stubs. */
export function pickBestLocIndex(locations) {
  if (!locations || !locations.length) return 0;
  const rank = (role) =>
    role === "dwarf"
      ? 0
      : role === "def" || role === "decl"
        ? 1
        : role === "twin"
          ? 2
          : role && role !== "sym"
            ? 3
            : 4;
  let best = 0;
  let bestRank = 4;
  locations.forEach((l, i) => {
    const r = rank(l && l.role);
    if (r < bestRank) {
      bestRank = r;
      best = i;
    }
  });
  return best;
}

export function guessLang(path) {
  const lower = String(path || "")
    .toLowerCase()
    .replace(/\\/g, "/");
  const base = lower.split("/").pop() || lower;
  if (lower.endsWith(".py") || lower.endsWith(".pyi")) return "python";
  if (lower.endsWith(".c") || lower.endsWith(".h")) return "c";
  if (
    lower.endsWith(".cc") ||
    lower.endsWith(".cpp") ||
    lower.endsWith(".cxx") ||
    lower.endsWith(".hpp") ||
    lower.endsWith(".hh")
  ) {
    return "cpp";
  }
  if (lower.endsWith(".rs")) return "rust";
  if (lower.endsWith(".js") || lower.endsWith(".mjs") || lower.endsWith(".cjs")) {
    return "javascript";
  }
  if (lower.endsWith(".ts") || lower.endsWith(".tsx")) return "typescript";
  if (lower.endsWith(".toml") || lower.endsWith(".ini") || base === "pack.toml") {
    return "ini";
  }
  if (lower.endsWith(".md")) return "markdown";
  if (lower.endsWith(".json")) return "json";
  return "";
}

export function cdnPrefix() {
  const repl = document.getElementById("mpy-repl");
  if (repl && repl.dataset.cdnBase) {
    try {
      return new URL(repl.dataset.cdnBase).pathname.replace(/\/$/, "") || "";
    } catch (_) {
      /* fall through */
    }
  }
  const base = (document.body && document.body.dataset.basePath) || "";
  if (base) return base.replace(/\/$/, "");
  const m = window.location.pathname.match(/^(\/cdn)(?=\/|$)/);
  return (m && m[1]) || "";
}

export function artifactRoot(opts) {
  const pref = cdnPrefix();
  const file = encodeURIComponent(opts.filename);
  if (opts.version) {
    return `${pref}/artifacts/pin/${encodeURIComponent(opts.version)}/${file}`;
  }
  return `${pref}/artifacts/lead/${file}`;
}

export function inferChannel(opts) {
  if (opts.channel) return opts.channel;
  if (opts.version) return "@" + String(opts.version).replace(/^@/, "");
  return "lead";
}

export function pinVersionFromChannel(channel) {
  if (!channel || channel === "lead") return null;
  return String(channel).replace(/^@/, "");
}

export function pickDefaultArtifact(siblings, prefer) {
  if (prefer && siblings.some((s) => s.filename === prefer)) return prefer;
  const rank = (name) => {
    const n = String(name).toLowerCase();
    if (n.endsWith(".wasm") && !n.endsWith(".zlib")) return 0;
    if (n.endsWith(".elf")) return 1;
    if (n.includes(".elf")) return 2;
    if (n.endsWith(".aot")) return 3;
    return 9;
  };
  const sorted = siblings.slice().sort((a, b) => rank(a.filename) - rank(b.filename));
  return sorted.length ? sorted[0].filename : null;
}
