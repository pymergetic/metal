/**
 * wasmmod-cdn Inspect commander (ES module entry).
 *
 * Nav: package · version · artifact · section · symbol
 * Dual panes: hex | asm | source (modes stick while navigating)
 *
 * window.openInspect({ filename, version?, package?, symbol?, addr?, sectionIndex?, mpyPath?, sourcePath?, tab? })
 */
import { hooks } from "./ctx.js";
import { navigateCatalog } from "./catalog.js";
import { openInspect, selectBinary, selectSection, selectSymbol } from "./session.js";
import {
  artifactRoot,
  cdnPrefix,
  esc,
  fmtOff,
  fmtSize,
  guessLang,
  pickBestLocIndex,
} from "./util.js";
import { hexdumpHtml } from "./format.js";

hooks.openInspect = openInspect;
hooks.navigateCatalog = navigateCatalog;
hooks.selectBinary = selectBinary;
hooks.selectSection = selectSection;
hooks.selectSymbol = selectSymbol;

window.openInspect = openInspect;
window.MetalInspect = {
  openInspect,
  hexdumpHtml,
  esc,
  fmtSize,
  fmtOff,
  cdnPrefix,
  artifactRoot,
  pickBestLocIndex,
  guessLang,
};
