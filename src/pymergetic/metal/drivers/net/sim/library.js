/* pymergetic.metal.drivers.net.sim — browser L2 JS import.
 * Wired by metal.mk (`--js-library`) when CC is emcc. Do not edit
 * ports/webassembly/library.js (vanilla µPy). WAN HTTP stays js.fetch.
 */
mergeInto(LibraryManager.library, {
    pm_metal_drivers_net_sim_js_tx__postset:
        "Module.metalL2RxQueue = Module.metalL2RxQueue || [];",

    pm_metal_drivers_net_sim_js_tx: (ptr, len) => {
        const bytes = HEAPU8.slice(ptr, ptr + len);
        if (typeof Module.metalL2Tx === "function") {
            Module.metalL2Tx(bytes);
        } else if (typeof postMessage === "function") {
            postMessage({ type: "metal-l2-tx", frame: bytes });
        }
        return 0;
    },

    pm_metal_drivers_net_sim_js_rx: (ptr, max) => {
        const q = Module.metalL2RxQueue;
        if (!q || q.length === 0) {
            return 0;
        }
        const frame = q.shift();
        const n = Math.min(frame.length, max);
        HEAPU8.set(frame.subarray(0, n), ptr);
        return n;
    },
});
