/** Hex / asm / source HTML formatting. */
import { HEX_PREVIEW, esc, guessLang } from "./util.js";

export function highlightCode(text, lang) {
  if (window.hljs && lang && window.hljs.getLanguage(lang)) {
    try {
      return window.hljs.highlight(text, {
        language: lang,
        ignoreIllegals: true,
      }).value;
    } catch (_) {
      /* fall through */
    }
  }
  return esc(text);
}

function paintHexByte(b) {
  const hx = b.toString(16).padStart(2, "0");
  if (b === 0) return `<span class="hx-null">${hx}</span>`;
  if (b >= 32 && b < 127) return `<span class="hx-print">${hx}</span>`;
  return `<span class="hx-hi">${hx}</span>`;
}

export function hexdumpHtml(buf, limit, baseOffset) {
  const cap = limit == null ? HEX_PREVIEW : limit;
  const base = Number(baseOffset) || 0;
  const view = buf.byteLength > cap ? buf.slice(0, cap) : buf;
  const u8 = new Uint8Array(view);
  const width = 16;
  const lines = [];
  for (let i = 0; i < u8.length; i += width) {
    const chunk = u8.subarray(i, i + width);
    const hex = Array.from(chunk, paintHexByte).join(" ");
    const pad = "   ".repeat(width - chunk.length);
    let ascii = "";
    for (const b of chunk) ascii += b >= 32 && b < 127 ? String.fromCharCode(b) : ".";
    const addr = (base + i).toString(16).padStart(8, "0");
    lines.push(
      `<span class="hx-off">${addr}</span>  ${hex}${pad}  |${esc(ascii)}|`
    );
  }
  let html = lines.join("\n");
  if (buf.byteLength > cap) {
    html += `\n… showing ${cap} of ${buf.byteLength} bytes`;
  }
  return html;
}

export function formatAsm(lines) {
  if (!lines || !lines.length) return '<span class="muted">empty</span>';
  return lines
    .map((ln) => {
      const addr = Number(ln.addr).toString(16).padStart(8, "0");
      const raw = ln.raw_hex ? `<span class="hx-null">${esc(ln.raw_hex)}</span>  ` : "";
      return `<span class="hx-off">${addr}</span>  ${raw}${esc(ln.text)}`;
    })
    .join("\n");
}

export function formatSourceSnippet(path, text, loc, role) {
  const lang = guessLang(path);
  if (loc && loc.line != null && loc.line > 0) {
    const lines = String(text).split("\n");
    const i = loc.line - 1;
    const start = Math.max(0, i - 8);
    const end = Math.min(lines.length, i + 12);
    const chunk = [];
    for (let n = start; n < end; n++) {
      const mark = n === i ? "›" : " ";
      const num = String(n + 1).padStart(4, " ");
      const cls = n === i ? "inspect-src-hit" : "";
      chunk.push(
        `<span class="${cls}"><span class="hx-off">${mark}${num}</span>  ${highlightCode(lines[n], lang)}</span>`
      );
    }
    return (
      `<div class="muted">${esc(path)}:${esc(loc.line)} · ${esc(role)}</div>\n` +
      chunk.join("\n")
    );
  }
  return (
    `<div class="muted">${esc(path)} · ${esc(role)}</div>\n` +
    highlightCode(String(text).slice(0, 12000), lang)
  );
}
