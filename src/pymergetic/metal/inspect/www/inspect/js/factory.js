/* factory floor — the live view over the build card's telemetry.
 *
 * Three faces, one page:
 *   GET  /build            the unit index (matrix rows)
 *   GET  /build/events?since=N  the ring tail (this page's heartbeat)
 *   POST /build?all=1[&target=N] the BUILD ALL walk
 *
 * No framework, no state beyond `since` — the ring is the state. */
(function () {
  "use strict";

  var since = 0;
  var building = false;
  var units = [];

  var $ = function (id) { return document.getElementById(id); };

  function setStatus(text, busy) {
    var el = $("fx-status");
    el.textContent = text;
    el.className = busy ? "busy" : "";
  }

  function fmtBytes(n) {
    if (!n) return "0 B";
    if (n < 1024) return n + " B";
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB";
    return (n / 1024 / 1024).toFixed(1) + " MiB";
  }

  function shortFqn(fqn) {
    return fqn.replace(/^pymergetic\.(metal|wasmmod|util)\./, "");
  }

  /* the matrix: one row per unit, its last per-lane state from the ring.
   * Rows rebuild from the event tail each tick — the ring IS the lane
   * state, so there is nothing to sync. */
  function renderMatrix(latestByFqn) {
    var tbody = $("fx-rows");
    if (!tbody) return;
    var html = "";
    for (var i = 0; i < units.length; i++) {
      var u = units[i];
      var st = latestByFqn[u.fqn] || null;
      var cls = "idle";
      var label = "—";
      var bytes = "";
      if (st) {
        if (st.kind === "unit_fail") { cls = "ref"; label = "fail"; }
        else if (st.kind === "unit_end") { cls = "ok"; label = "ok"; }
        else { cls = "busy"; label = st.kind.replace(/_/g, " "); }
        bytes = st.bytes ? fmtBytes(st.bytes) : "";
      }
      html += "<tr>" +
        "<td>" + shortFqn(u.fqn) + "</td>" +
        "<td>" + (u.impl || "?") + "</td>" +
        "<td>" + (u.n_sources || 0) + "</td>" +
        '<td class="' + cls + '">' + label + "</td>" +
        '<td class="bytes">' + bytes + "</td>" +
        "<td></td>" +
        "</tr>";
    }
    tbody.innerHTML = html;
    $("fx-count").textContent = "(" + units.length + " units)";
  }

  /* the stream: append the ring's new tail, cap the DOM at ~200 rows */
  function renderEvents(events) {
    var box = $("fx-events");
    if (!box || !events.length) return;
    var html = "";
    for (var i = 0; i < events.length; i++) {
      var e = events[i];
      var parts = e.fqn.split(".");
      var unit = parts[parts.length - 1];
      html += '<div class="ev' +
        (e.kind === "unit_fail" ? " fail" : "") + '">' +
        '<span class="k">' + e.kind + "</span>" +
        '<span class="s">' + shortFqn(e.fqn) +
        (e.src ? " · " + e.src.split("/").pop() : "") + "</span>" +
        '<span class="b">' +
        (e.dur_us ? (e.dur_us / 1000).toFixed(1) + " ms" : "") +
        (e.bytes ? " · " + fmtBytes(e.bytes) : "") +
        "</span>" +
        "</div>";
    }
    box.insertAdjacentHTML("beforeend", html);
    while (box.children.length > 200) {
      box.removeChild(box.firstChild);
    }
    box.scrollTop = box.scrollHeight;
  }

  /* the poll: index + tail in one tick; 1 Hz keeps the ring the truth
   * without hammering the seat. */
  function tick() {
    return Promise.all([
      fetch("/build").then(function (r) { return r.json(); }),
      fetch("/build/events?since=" + since).then(function (r) { return r.json(); }),
    ]).then(function (rs) {
      units = rs[0].units || [];
      var ev = rs[1];
      if (ev.latest) {
        since = ev.latest;
      }
      renderEvents(ev.events || []);
      var latestByFqn = {};
      var events = ev.events || [];
      for (var i = 0; i < events.length; i++) {
        latestByFqn[events[i].fqn] = events[i];
      }
      renderMatrix(latestByFqn);
      if (!building) {
        setStatus("idle — latest seq " + since, false);
      }
    }).catch(function (e) {
      setStatus("poll failed: " + e, false);
    });
  }

  function buildAll() {
    if (building) return;
    building = true;
    var target = $("fx-target").value;
    setStatus("building all on lane " + target + "…", true);
    $("fx-build-all").disabled = true;
    fetch("/build?all=1&target=" + target, { method: "POST" })
      .then(function (r) { return r.json(); })
      .then(function (r) {
        setStatus("walk done: " + (r.ok || 0) + " ok, " +
          (r.refused || 0) + " refused", false);
      })
      .catch(function (e) {
        setStatus("walk failed: " + e, false);
      })
      .then(function () {
        building = false;
        $("fx-build-all").disabled = false;
      });
  }

  document.addEventListener("DOMContentLoaded", function () {
    $("fx-build-all").addEventListener("click", buildAll);
    tick();
    setInterval(tick, 1000);
  });
})();
