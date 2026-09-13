/* P2P Orchestration panel — polls live state from all six cards. */
(function () {
  var POLL_MS = 3000;

  function el(id) { return document.getElementById(id); }

  function json_get(url, cb) {
    var x = new XMLHttpRequest();
    x.onload = function () {
      try { cb(JSON.parse(x.responseText || "{}"), null); }
      catch (e) { cb(null, e); }
    };
    x.onerror = function () { cb(null, "fetch failed"); };
    x.open("GET", url);
    x.send();
  }

  /* ---- Neighbors ---- */
  function fill_neighbors(data) {
    var count = el("p2p-nb-count");
    var table = el("p2p-nb-table");
    var empty = el("p2p-nb-empty");
    var tbody = table && table.tBodies[0];
    var nb = (data && data.neighbors) || [];
    if (count) count.textContent = nb.length + " neighbour(s)";
    if (!nb.length) {
      if (table) table.hidden = true;
      if (empty) empty.hidden = false;
      return;
    }
    if (table) table.hidden = false;
    if (empty) empty.hidden = true;
    if (tbody) {
      tbody.innerHTML = "";
      var caps_labels = {1:"build",2:"jit",4:"zenoh",8:"services"};
      nb.forEach(function (n) {
        var caps = [];
        Object.keys(caps_labels).forEach(function (k) {
          if (n.caps & parseInt(k)) caps.push(caps_labels[k]);
        });
        var tr = tbody.insertRow();
        tr.insertCell().textContent = n.peer_id;
        tr.insertCell().textContent = n.zenoh_id || "";
        tr.insertCell().textContent = n.host || "";
        tr.insertCell().textContent = caps.join(", ") || "—";
        tr.insertCell().textContent = n.alive ? "yes" : "no";
      });
    }
  }

  /* ---- RPC ---- */
  function fill_rpc_handlers(data) {
    var pre = el("p2p-rpc-handlers");
    var h = (data && data.handlers) || [];
    pre.textContent = h.length ? h.join("\n") : "(no handlers registered)";
  }

  function fill_rpc_calls(data) {
    var pre = el("p2p-rpc-calls");
    var p = (data && data.pending) || [];
    var lines = [];
    p.forEach(function (c) {
      var s = "call " + c.call_id + " status=" + c.status;
      if (c.result) s += " result=" + c.result;
      lines.push(s);
    });
    pre.textContent = lines.length ? lines.join("\n") : "(no pending calls)";
  }

  /* ---- Distributed State ---- */
  function fill_dstate(data) {
    var count = el("p2p-ds-count");
    var table = el("p2p-ds-table");
    var tbody = table && table.tBodies[0];
    var entries = (data && data.entries) || [];
    if (count) count.textContent = entries.length + " entry(s)";
    if (!entries.length) { if (table) table.hidden = true; return; }
    if (table) table.hidden = false;
    if (tbody) {
      tbody.innerHTML = "";
      entries.forEach(function (e) {
        var tr = tbody.insertRow();
        tr.insertCell().textContent = e.key || "";
        tr.insertCell().textContent = (e.value_hex || "").substring(0, 64)
          + (e.value_len > 32 ? "…" : "");
        tr.insertCell().textContent = e.version !== undefined ? String(e.version) : "—";
        tr.insertCell().textContent = e.peer_id !== undefined ? String(e.peer_id) : "—";
      });
    }
  }

  /* ---- Cloud Compile ---- */
  function fill_cloud(data) {
    var count = el("p2p-cc-count");
    var table = el("p2p-cc-table");
    var tbody = table && table.tBodies[0];
    var jobs = (data && data.jobs) || [];
    count.textContent = (data ? data.total || 0 : 0) + " job(s) — "
      + (data ? data.pending || 0 : 0) + " pending, "
      + (data ? data.running || 0 : 0) + " running";
    if (!jobs.length) { if (table) table.hidden = true; return; }
    if (table) table.hidden = false;
    if (tbody) {
      tbody.innerHTML = "";
      jobs.forEach(function (j) {
        var tr = tbody.insertRow();
        tr.insertCell().textContent = j.job_id;
        tr.insertCell().textContent = j.target || "";
        tr.insertCell().textContent = j.peer_id || "—";
        tr.insertCell().textContent = j.state || "—";
        var art = "";
        if (j.state === "done" && j.artifact_len)
          art = j.artifact_len + " B";
        if (j.state === "failed" && j.error)
          art = j.error.substring(0, 60);
        tr.insertCell().textContent = art || "—";
      });
    }
  }

  /* ---- Workspace ---- */
  function fill_workspace(data) {
    var count = el("p2p-ws-count");
    var table = el("p2p-ws-table");
    var tbody = table && table.tBodies[0];
    var files = (data && data.files) || [];
    if (count) count.textContent = files.length + " file(s)";
    if (!files.length) { if (table) table.hidden = true; return; }
    if (table) table.hidden = false;
    if (tbody) {
      tbody.innerHTML = "";
      files.forEach(function (f) {
        var tr = tbody.insertRow();
        tr.insertCell().textContent = f.path || "";
        tr.insertCell().textContent = (f.content_len !== undefined)
          ? f.content_len + " B" : "—";
        tr.insertCell().textContent = f.author_peer !== undefined
          ? String(f.author_peer) : "—";
        tr.insertCell().textContent = f.content_hash !== undefined
          ? "0x" + f.content_hash.toString(16) : "—";
      });
    }
  }

  function poll() {
    json_get("/p2p/neighbors", fill_neighbors);
    json_get("/p2p/rpc/handlers", fill_rpc_handlers);
    json_get("/p2p/rpc/calls", fill_rpc_calls);
    json_get("/p2p/dstate", fill_dstate);
    json_get("/p2p/cloud", fill_cloud);
    json_get("/p2p/workspace", fill_workspace);
  }

  poll();
  setInterval(poll, POLL_MS);
})();