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
          window.__caps = j;
          renderRpcUi(j);
        } catch (_) {}
      }
    } catch (e) {
      el.textContent = String(e);
    }
  }

  function qbool(id) {
    const el = document.getElementById(id);
    return !!(el && el.checked);
  }

  async function loadCompleteness() {
    const summary = document.getElementById("reg-summary");
    const treeEl = document.getElementById("reg-tree");
    const modsEl = document.getElementById("reg-modules");
    const drillEl = document.getElementById("reg-drill");
    const qs = new URLSearchParams();
    if (qbool("gaps-only")) {
      qs.set("gaps_only", "1");
    }
    if (qbool("detail")) {
      qs.set("detail", "1");
    }
    qs.set("fmt", "tree");
    const treeUrl = "reg/completeness?" + qs.toString();
    qs.set("fmt", "json");
    const jsonUrl = "reg/completeness?" + qs.toString();
    try {
      const [tr, jr] = await Promise.all([fetch(treeUrl), fetch(jsonUrl)]);
      const treeText = await tr.text();
      if (treeEl) {
        treeEl.textContent = treeText;
      }
      const j = JSON.parse(await jr.text());
      if (summary) {
        summary.textContent =
          "methods=" +
          (j.method_count || 0) +
          " gaps=" +
          (j.gap_count || 0) +
          " modules=" +
          ((j.modules && j.modules.length) || 0);
      }
      if (modsEl) {
        modsEl.textContent = "";
        const gaps = j.gaps || [];
        for (let i = 0; i < gaps.length; i++) {
          const g = gaps[i];
          const btn = document.createElement("button");
          btn.type = "button";
          btn.className = "gap-btn";
          btn.textContent =
            (g.module || "") +
            "." +
            (g.func || "") +
            "  miss=" +
            (g.miss || []).join(",") +
            (g.bad && g.bad.length ? "  bad=" + g.bad.join(",") : "");
          btn.addEventListener("click", async function () {
            try {
              const r = await fetch(
                "reg/method?module=" +
                  encodeURIComponent(g.module || "") +
                  "&func=" +
                  encodeURIComponent(g.func || "")
              );
              const t = await r.text();
              if (drillEl) {
                try {
                  drillEl.textContent = JSON.stringify(JSON.parse(t), null, 2);
                } catch (_) {
                  drillEl.textContent = t;
                }
              }
            } catch (e) {
              if (drillEl) {
                drillEl.textContent = String(e);
              }
            }
          });
          modsEl.appendChild(btn);
        }
      }
    } catch (e) {
      if (treeEl) {
        treeEl.textContent = String(e);
      }
    }
  }

  /* Relative to /inspect/ or /cdn/inspect/ → sibling host routes. */
  await load("health", "../health");
  await load("caps", "../capabilities");
  await load("self", "self");
  try {
    const r = await fetch("reg");
    const t = await r.text();
    const el = document.getElementById("reg");
    if (el) {
      try {
        el.textContent = JSON.stringify(JSON.parse(t), null, 2);
      } catch (_) {
        el.textContent = t;
      }
    }
  } catch (e) {
    const el = document.getElementById("reg");
    if (el) {
      el.textContent = String(e);
    }
  }
  await loadCompleteness();
  const reload = document.getElementById("reg-reload");
  if (reload) {
    reload.addEventListener("click", loadCompleteness);
  }
  ["gaps-only", "detail"].forEach(function (id) {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener("change", loadCompleteness);
    }
  });

  /* ---- Module / func / RPC drive (gate on capabilities) ---- */
  const rpcUi = document.getElementById("rpc-ui");
  const rpcModule = document.getElementById("rpc-module");
  const rpcFunc = document.getElementById("rpc-func");
  const rpcArg = document.getElementById("rpc-arg");
  const rpcRun = document.getElementById("rpc-run");
  const rpcOut = document.getElementById("rpc-out");
  const navEl = document.getElementById("mod-list");

  function renderRpcUi(caps) {
    if (!rpcUi) {
      return;
    }
    const canRpc = !!(caps && caps.rpc);
    rpcUi.style.display = canRpc ? "" : "none";
  }

  function esc(s) {
    return String(s == null ? "" : s).replace(/[&<>"]/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c];
    });
  }

  function showOut(text, prettyJson) {
    if (!rpcOut) {
      return;
    }
    try {
      if (prettyJson) {
        rpcOut.textContent = JSON.stringify(JSON.parse(text), null, 2);
        return;
      }
    } catch (_) {}
    rpcOut.textContent = text;
  }

  async function openModule(fqn) {
    rpcModule.value = fqn;
    try {
      const r = await fetch("reg/" + encodeURIComponent(fqn));
      showOut(await r.text(), true);
    } catch (e) {
      showOut(String(e));
    }
  }

  /* Make each module in the completeness JSON a clickable nav link. */
  async function wireModuleNav() {
    if (!navEl) {
      return;
    }
    try {
      const r = await fetch("reg");
      const j = JSON.parse(await r.text());
      const mods = j.modules || [];
      navEl.textContent = "";
      mods.forEach(function (m) {
        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = "mod-btn";
        btn.textContent = m;
        btn.addEventListener("click", function () {
          openModule(m);
        });
        navEl.appendChild(btn);
      });
    } catch (e) {
      if (navEl) {
        navEl.textContent = String(e);
      }
    }
  }

  if (rpcRun && rpcModule && rpcFunc) {
    rpcRun.addEventListener("click", async function () {
      const m = rpcModule.value.trim();
      const f = rpcFunc.value.trim();
      if (!m || !f) {
        showOut("error: module and func required");
        return;
      }
      const a0 = rpcArg ? rpcArg.value.trim() : "";
      try {
        const r = await fetch(
          "call/" +
            encodeURIComponent(m) +
            "/" +
            encodeURIComponent(f) +
            (a0 ? "?a0=" + encodeURIComponent(a0) : "")
        );
        showOut(await r.text(), true);
      } catch (e) {
        showOut(String(e));
      }
    });
  }
  await wireModuleNav();
})();
