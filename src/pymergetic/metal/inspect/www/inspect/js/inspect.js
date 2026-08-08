(async function () {
  const params = new URLSearchParams(location.search);
  const theme = params.get("theme") || "metal";
  const link = document.getElementById("theme-css");
  if (link) {
    link.href = "css/themes/" + theme + ".css";
  }
  async function load(id, path) {
    const el = document.getElementById(id);
    try {
      const r = await fetch(path);
      const t = await r.text();
      el.textContent = t;
      if (id === "caps") {
        try {
          const j = JSON.parse(t);
          document.getElementById("role").textContent =
            "role=" + (j.role || "?") + " theme=" + theme;
        } catch (_) {}
      }
    } catch (e) {
      el.textContent = String(e);
    }
  }
  await load("health", "/health");
  await load("caps", "/capabilities");
})();
