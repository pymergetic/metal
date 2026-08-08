(async function () {
  const params = new URLSearchParams(location.search);
  let theme = params.get("theme");
  const link = document.getElementById("theme-css");
  function applyTheme(name) {
    theme = name || "metal";
    if (link) {
      link.href = "css/themes/" + theme + ".css";
    }
  }
  if (theme) {
    applyTheme(theme);
  }
  async function load(id, path) {
    const el = document.getElementById(id);
    if (!el) {
      return;
    }
    try {
      const r = await fetch(path);
      const t = await r.text();
      el.textContent = t;
      if (id === "caps") {
        try {
          const j = JSON.parse(t);
          if (!params.get("theme") && j.theme) {
            applyTheme(j.theme);
          }
          document.getElementById("role").textContent =
            "role=" + (j.role || "?") + " theme=" + theme;
        } catch (_) {}
      }
    } catch (e) {
      el.textContent = String(e);
    }
  }
  /* Relative to /inspect/ or /cdn/inspect/ → sibling host routes. */
  await load("health", "../health");
  await load("caps", "../capabilities");
  await load("self", "self");
})();
