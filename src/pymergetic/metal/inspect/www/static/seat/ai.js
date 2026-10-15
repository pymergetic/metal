/* The AI pane's page script — reads /ai/state, renders the hub's live faces,
 * drills into one object (Browse), and posts the factory forms (Factory). */
(function () {
  var POLL_MS = 3000;
  var state = null;
  function el(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s == null ? "" : s)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }
  function get(url, cb) {
    var x = new XMLHttpRequest();
    x.onload = function () { try { cb(JSON.parse(x.responseText || "{}")); } catch (_) { cb(null); } };
    x.onerror = function () { cb(null); };
    x.open("GET", url); x.send();
  }
  /* POST to an /ai/… write face. Params ride in the query string (the seat's
   * fill() reads them there); the body is empty. */
  function post(url, cb) {
    var x = new XMLHttpRequest();
    x.onload = function () { try { cb(JSON.parse(x.responseText || "{}")); } catch (_) { cb(null); } };
    x.onerror = function () { cb(null); };
    x.open("POST", url); x.send();
  }
  function q(obj) {
    var parts = [];
    for (var k in obj) {
      if (obj.hasOwnProperty(k) && obj[k] !== undefined && obj[k] !== null) {
        parts.push(encodeURIComponent(k) + "=" + encodeURIComponent(obj[k]));
      }
    }
    return parts.length ? "?" + parts.join("&") : "";
  }
  function list(nodeId, rows, renderRow, empty) {
    var host = el(nodeId);
    if (!host) return;
    if (!rows || rows.length === 0) { host.innerHTML = '<p class="muted">' + empty + "</p>"; return; }
    host.innerHTML = rows.map(renderRow).join("");
  }

  function render() {
    if (!state) return;
    var c = state.counts || {};
    el("ai-status").textContent =
      (c.providers || 0) + " provider(s) · " + (c.sessions || 0) + " session(s) · " +
      (c.tools || 0) + " tool(s) · " + (c.config || 0) + " param(s) · " + (c.mcp || 0) + " mcp server(s)";

    list("ai-tools", state.tools, function (t) {
      return '<article class="service-card"><h3>' + esc(t.name) + "</h3>" +
        '<dl><dt>Description</dt><dd>' + esc(t.description || "—") + "</dd>" +
        "<dt>Params schema</dt><dd>" + (t.schema_len ? t.schema_len + " bytes" : "—") + "</dd></dl></article>";
    }, "No tools registered on this seat.");

    list("ai-providers", state.providers, function (p) {
      return '<article class="service-card browse" data-browse-provider="' + esc(p.name) + '"><h3>' + esc(p.name) + "</h3>" +
        '<dl><dt>Model</dt><dd>' + esc(p.model || "—") + "</dd>" +
        "<dt>URL</dt><dd>" + esc(p.url || "—") + "</dd>" +
        "<dt>API key</dt><dd>" + (p.api_key_set ? "set" : "not set") + "</dd></dl></article>";
    }, "No model providers registered.");

    list("ai-sessions", state.sessions, function (s) {
      return '<article class="service-card browse" data-browse-session="' + s.id + '"><h3>' + esc(s.name || "unnamed") + "</h3>" +
        '<dl><dt>Id</dt><dd>' + s.id + "</dd>" +
        "<dt>Messages</dt><dd>" + s.msg_count + "</dd>" +
        "<dt>Persistent</dt><dd>" + (s.persistent ? "yes" : "no") + "</dd></dl></article>";
    }, "No chat sessions.");

    // wire Browse on the provider/session cards
    var cards = document.querySelectorAll("[data-browse-provider]");
    for (var i = 0; i < cards.length; i++) {
      cards[i].onclick = (function (n) {
        return function () { browseProvider(n.getAttribute("data-browse-provider")); };
      })(cards[i]);
    }
    var scards = document.querySelectorAll("[data-browse-session]");
    for (var j = 0; j < scards.length; j++) {
      scards[j].onclick = (function (n) {
        return function () { browseSession(n.getAttribute("data-browse-session")); };
      })(scards[j]);
    }

    var cfg = el("ai-config");
    if (cfg) {
      cfg.textContent = (state.config || []).length
        ? state.config.map(function (p) { return p.key + " = " + p.value; }).join("\n")
        : "no parameters set";
    }
    var mcp = el("ai-mcp");
    if (mcp) {
      mcp.textContent = (state.mcp || []).length
        ? state.mcp.map(function (s) {
            return s.name + "  [" + (s.transport ? "sse" : "stdio") + "]" +
              (s.command ? "\n  cmd: " + s.command : "") +
              (s.sse_url ? "\n  sse: " + s.sse_url : "") +
              "\n  timeout: " + s.timeout_ms + " ms";
          }).join("\n\n")
        : "no mcp servers discovered";
    }
    var raw = el("ai-raw");
    if (raw) raw.textContent = JSON.stringify(state, null, 2);
  }

  function browseProvider(name) {
    var d = el("ai-detail");
    if (!d) return;
    d.textContent = "loading " + name + "…";
    get("/ai/provider" + q({ name: name }), function (r) {
      if (!r || r.error) { d.textContent = "provider not found: " + name; return; }
      d.innerHTML = "<strong>Provider " + esc(r.name) + "</strong><dl>" +
        "<dt>Model</dt><dd>" + esc(r.model || "—") + "</dd>" +
        "<dt>URL</dt><dd>" + esc(r.url || "—") + "</dd>" +
        "<dt>API key</dt><dd>" + (r.api_key_set ? "set" : "not set") + "</dd></dl>" +
        '<button type="button" data-remove-provider="' + esc(r.name) + '">Remove provider</button>';
      var rm = d.querySelector("[data-remove-provider]");
      if (rm) rm.onclick = function () {
        post("/ai/provider/remove" + q({ name: name }), function () { d.textContent = "removed " + name; poll(); });
      };
    });
  }
  function browseSession(id) {
    var d = el("ai-detail");
    if (!d) return;
    d.textContent = "loading session " + id + "…";
    get("/ai/session" + q({ id: id }), function (r) {
      if (!r || r.error) { d.textContent = "session not found: " + id; return; }
      var msgs = (r.messages || []).map(function (m) {
        var role = ["user", "assistant", "system"][m.role] || m.role;
        return "<dt>" + esc(role) + "</dt><dd>" + esc(m.content) + "</dd>";
      }).join("");
      d.innerHTML = "<strong>Session " + esc(r.name) + " (id " + r.id + ")</strong><dl>" +
        (msgs || "<dt>messages</dt><dd>none</dd>") + "</dl>";
    });
  }

  function factoryStatus(msg, ok) {
    var s = el("ai-factory-status");
    if (s) { s.textContent = msg; s.className = ok ? "hint" : "hint muted"; }
  }
  function wireForm(id, url, fields, after) {
    var f = el(id);
    if (!f) return;
    f.onsubmit = function (e) {
      e.preventDefault();
      var params = {};
      for (var i = 0; i < fields.length; i++) {
        var input = f.elements[fields[i]];
        if (input) params[fields[i]] = input.value;
      }
      post(url + q(params), function (r) {
        var ok = r && r.ok;
        factoryStatus((ok ? "✓ " : "✗ ") + url + " → " + JSON.stringify(r || {}), ok);
        if (after) after(r);
        poll();
      });
    };
  }

  function poll() {
    get("/ai/state", function (data) { if (!data) return; state = data; render(); });
  }

  var reload = el("ai-reload");
  if (reload) reload.addEventListener("click", poll);

  wireForm("ai-provider-form", "/ai/provider/set", ["name", "url", "model", "api_key"]);
  wireForm("ai-session-form", "/ai/session/create", ["name"]);
  wireForm("ai-append-form", "/ai/session/append", ["id", "role", "content"]);
  wireForm("ai-config-form", "/ai/config/set", ["key", "value"]);
  wireForm("ai-mcp-form", "/ai/mcp/start", ["name", "transport", "command", "sse_url", "timeout_ms"]);

  poll();
  setInterval(poll, POLL_MS);
})();
