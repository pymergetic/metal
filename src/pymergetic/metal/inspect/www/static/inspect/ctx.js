/**
 * Shared inspect runtime handle + late-bound action hooks (breaks cycles).
 * @typedef {{ dialog: HTMLDialogElement, els: Record<string, HTMLElement>, state: object }} InspectUi
 */

/** @type {InspectUi | null} */
export let ui = null;

export function setUi(next) {
  ui = next;
  return ui;
}

export function requireUi() {
  if (!ui) throw new Error("inspect UI not ready");
  return ui;
}

/** Populated by main.js after modules load. */
export const hooks = {
  openInspect: /** @type {null | Function} */ (null),
  navigateCatalog: /** @type {null | Function} */ (null),
  selectBinary: /** @type {null | Function} */ (null),
  selectSection: /** @type {null | Function} */ (null),
  selectSymbol: /** @type {null | Function} */ (null),
};
