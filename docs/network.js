// MP NetWire page (NetWire 1.0): reads wires.json from the netwire-status branch (netwire-247.yml, every 5 min).
(function () {
  "use strict";
  var SRC = "https://raw.githubusercontent.com/JebetrexxMC/wagu/netwire-status/wires.json";
  var ORDER = ["all", "default", "featured", "crossplay", "region", "vps", "lobby", "emqx", "oracle", "xdp", "relay", "custom", "omp", "web"];
  var GAME = { mqtt: 1, mp: 1, samp: 1, udp: 1 };
  var $ = function (id) { return document.getElementById(id); };
  var data = null, group = "all";

  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function ms(v) { return v == null ? "-" : Math.round(v) + " ms"; }
  function ago(t) {
    var s = Math.max(0, Math.round(Date.now() / 1000 - t));
    if (s < 90) return "just now"; if (s < 5400) return Math.round(s / 60) + " min ago";
    return Math.round(s / 3600) + " h ago";
  }
  function setLive(cls, t) { var l = $("live"); l.className = "live " + cls; $("live-t").textContent = t; }

  function tabs() {
    var box = $("tabs"); box.textContent = "";
    ORDER.forEach(function (g) {
      var ws = g === "all" ? data.wires : data.wires.filter(function (w) { return w.group === g; });
      if (!ws.length) return;
      var up = ws.filter(function (w) { return w.grade !== "down"; }).length;
      var b = el("button", "tab"); b.type = "button"; b.setAttribute("role", "tab");
      b.setAttribute("aria-selected", g === group ? "true" : "false");
      b.appendChild(el("span", null, g)); b.appendChild(el("span", "c", up + "/" + ws.length));
      b.addEventListener("click", function () { group = g; tabs(); rows(); });
      box.appendChild(b);
    });
  }

  function rows() {
    var tb = $("wb"); tb.textContent = "";
    var ws = data.wires.filter(function (w) { return group === "all" || w.group === group; });
    ws.sort(function (a, b) {
      var r = { green: 0, yellow: 1, red: 2, skip: 3, down: 4 };
      return (r[a.grade] - r[b.grade]) || ((a.rtt == null ? 1e9 : a.rtt) - (b.rtt == null ? 1e9 : b.rtt));
    });
    if (!ws.length) { var tr0 = el("tr"); var td0 = el("td", "m", "No wires in this group."); td0.colSpan = 7; tr0.appendChild(td0); tb.appendChild(tr0); return; }
    ws.forEach(function (w) {
      var tr = el("tr"), td = el("td"), nm = el("div", "nm");
      nm.appendChild(el("span", "gd " + w.grade));
      var t = el("div");
      t.appendChild(el("span", null, w.name));
      var sub = w.kind === "lobby" ? "in the MP lobby · port " + w.port : w.host + ":" + w.port;
      if (w.players != null) sub += " · " + w.players + (w.max_players != null ? "/" + w.max_players : "") + " players";
      if (w.server && w.server !== w.name) sub += " · " + w.server;
      if (w.grade === "down" && w.error) sub += " · " + w.error;
      t.appendChild(el("small", null, sub));
      nm.appendChild(t); td.appendChild(nm); tr.appendChild(td);
      tr.appendChild(el("td", "m", w.kind));
      tr.appendChild(el("td", "n g-" + w.grade, w.kind === "lobby" ? (w.grade === "down" ? "down" : "listed") : ms(w.rtt)));
      tr.appendChild(el("td", "n", w.jitter == null ? "-" : Math.round(w.jitter) + " ms"));
      tr.appendChild(el("td", "n", w.kind === "lobby" ? "-" : w.loss + "%"));
      tr.appendChild(el("td", "n", w.uptime == null ? "-" : w.uptime + "%"));
      var sp = el("td"), s = el("div", "spark"); s.setAttribute("aria-label", "last checks");
      (w.spark || "").slice(-48).split("").forEach(function (c) { s.appendChild(el("i", c)); });
      sp.appendChild(s); tr.appendChild(sp);
      tb.appendChild(tr);
    });
  }

  function summary() {
    $("s-up").textContent = data.up + " / " + data.probed;
    $("s-green").textContent = data.green;
    var best = null;
    data.wires.forEach(function (w) {
      if (w.grade === "down" || w.rtt == null || !GAME[w.kind] || w.group === "omp") return;
      if (!best || w.rtt < best.rtt) best = w;
    });
    $("s-best").textContent = best ? best.name + " · " + ms(best.rtt) : "none up";
    $("s-at").textContent = ago(data.updated);
    var stale = Date.now() / 1000 - data.updated > 20 * 60;
    var ratio = data.probed ? data.up / data.probed : 0;
    setLive(stale ? "warn" : (ratio > 0.8 ? "ok" : ratio > 0 ? "warn" : "bad"),
      (stale ? "old data · " : "") + data.up + "/" + data.probed + " wires up");
  }

  function load() {
    fetch(SRC + (SRC.indexOf("?") < 0 ? "?t=" : "&t=") + Date.now(), { cache: "no-store" })
      .then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status); return r.json(); })
      .then(function (d) { data = d; summary(); tabs(); rows(); })
      .catch(function (e) {
        setLive("bad", "status not available yet");
        if (!data) { var tb = $("wb"); tb.textContent = ""; var tr = el("tr"); var td = el("td", "m", "No status yet (" + e.message + "). The first round runs right after the workflow starts."); td.colSpan = 7; tr.appendChild(td); tb.appendChild(tr); }
      });
  }
  load();
  setInterval(load, 60000);
})();
