/* Network service browser — local and peer-advertised application services. */
(function () {
  var POLL_MS = 3000;
  var state = { hosts: [], services: [] };
  function el(id) { return document.getElementById(id); }
  function get(url, cb) {
    var x = new XMLHttpRequest();
    x.onload = function () { try { cb(JSON.parse(x.responseText || "{}")); } catch (_) { cb(null); } };
    x.onerror = function () { cb(null); };
    x.open("GET", url); x.send();
  }
  function hostName(peer) {
    var found = state.hosts.filter(function (h) { return Number(h.peer_id) === Number(peer); })[0];
    return found ? (found.host || ("peer " + peer)) : (peer === 0 ? "localhost" : "peer " + peer);
  }
  function renderHosts() {
    var select = el("svc-host");
    var selected = select.value || "0";
    select.innerHTML = "";
    state.hosts.forEach(function (h) {
      var o = document.createElement("option");
      o.value = String(h.peer_id);
      o.textContent = h.local ? "localhost (this seat)" : ((h.host || "peer") + " · " + h.peer_id + (h.alive ? "" : " · offline"));
      select.appendChild(o);
    });
    if ([].some.call(select.options, function (o) { return o.value === selected; })) select.value = selected;
  }
  function renderServices() {
    var peer = Number(el("svc-host").value || 0);
    var rows = state.services.filter(function (s) { return Number(s.peer_id) === peer; });
    var host = hostName(peer);
    el("svc-title").textContent = "Services on " + host;
    el("svc-summary").textContent = rows.length + " advertised application service(s)";
    el("svc-host-status").textContent = peer === 0 ? "local loopback host" : "remote discovery record";
    var grid = el("svc-grid"); grid.innerHTML = "";
    el("svc-empty").hidden = rows.length !== 0;
    rows.forEach(function (s) {
      var card = document.createElement("article"); card.className = "service-card";
      var h = document.createElement("h3"); h.textContent = s.name || "unnamed service"; card.appendChild(h);
      var dl = document.createElement("dl");
      [["Module", s.fqn || "—"], ["Endpoint", (peer === 0 ? "localhost" : host) + ":" + s.port], ["Ownership", s.local ? "local" : "remote"], ["Instances", s.status_queryable ? String(s.instances) : "remote status unavailable"]].forEach(function (row) {
        var dt = document.createElement("dt"); dt.textContent = row[0]; var dd = document.createElement("dd"); dd.textContent = row[1]; dl.appendChild(dt); dl.appendChild(dd);
      });
      card.appendChild(dl); grid.appendChild(card);
    });
  }
  function poll() { get("/p2p/services", function (data) { if (!data) return; state = data; renderHosts(); renderServices(); }); }
  el("svc-host").addEventListener("change", renderServices);
  poll(); setInterval(poll, POLL_MS);
})();
