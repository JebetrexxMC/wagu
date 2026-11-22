// MP for PC (5.02.48): live status of the 24/7 open.mp crossplay server + one-tap join links.
// Status: server/omp_crossplay.sh writes omp247.json every 10 min on the mp247-data branch (mp-247-servers.yml).
// One-tap: samp://host:port (registered by the SA-MP client and the open.mp launcher), omp://host:port (open.mp launcher).
// join.html = the same, but it opens the game right away (a link you can share: mpsamp-open.github.io/join.html).
// ?addr=host:port overrides the address (testing, or a VPS copy of the server).
(function () {
  "use strict";
  var STATUS = "https://raw.githubusercontent.com/JebetrexxMC/wagu/mp247-data/omp247.json";
  var STALE_S = 25 * 60;
  var isJoin = document.body.classList.contains("jp-page");
  var $ = function (id) { return document.getElementById(id); };

  function validAddr(a) {
    return typeof a === "string" && /^[A-Za-z0-9.-]{1,253}:\d{1,5}$/.test(a) && +a.split(":")[1] > 0 && +a.split(":")[1] < 65536;
  }
  function override() {
    var q = new URLSearchParams(location.search).get("addr");
    return validAddr(q) ? q : "";
  }
  function toast(t) {
    var d = document.createElement("div"); d.className = "toast"; d.setAttribute("role", "status"); d.textContent = t;
    document.body.appendChild(d); setTimeout(function () { d.remove(); }, 2200);
  }
  function copy(t, msg) {
    (navigator.clipboard ? navigator.clipboard.writeText(t) : Promise.reject()).then(function () { toast(msg); }, function () {
      window.prompt("Copy:", t);
    });
  }
  function ago(s) {
    if (s < 90) return "just now";
    if (s < 5400) return Math.round(s / 60) + " min ago";
    return Math.round(s / 3600) + " h ago";
  }
  function setText(id, t) { var e = $(id); if (e) e.textContent = t; }

  function enable(addr) {
    var samp = "samp://" + addr, omp = "omp://" + addr;
    var share = location.origin + location.pathname.replace(/[^/]*$/, "") + "join.html" + (override() ? "?addr=" + encodeURIComponent(addr) : "");
    [["j-samp", samp], ["j-omp", omp]].forEach(function (p) {
      var a = $(p[0]); if (!a) return; a.href = p[1]; a.removeAttribute("aria-disabled");
    });
    var c = $("j-copy"); if (c) { c.disabled = false; c.onclick = function () { copy(addr, "Address copied: " + addr); }; }
    var s = $("j-share"); if (s) { s.disabled = false; s.onclick = function () { copy(share, "One-tap link copied"); }; }
    return samp;
  }

  function show(st) {
    var addr = override() || (st && validAddr(st.addr) ? st.addr : "");
    var srv = $("join"), now = Date.now() / 1000;
    var state = !st ? "off" : !st.online ? "off" : (now - st.updated > STALE_S ? "stale" : "on");
    if (srv) { srv.classList.remove("on", "off", "stale"); srv.classList.add(state); }
    var live = $("live"); if (live) { live.classList.remove("ok", "bad", "warn"); live.classList.add(state === "on" ? "ok" : state === "stale" ? "warn" : "bad"); }
    setText("srv-badge", state === "on" ? "ONLINE" : state === "stale" ? "NO RECENT CHECK" : "OFFLINE");
    setText("live-t", state === "on" ? "crossplay server online" : state === "stale" ? "last check " + ago(now - st.updated) : "crossplay server offline");
    if (st) {
      setText("srv-h", st.name || "MP Crossplay 24/7");
      setText("k-pl", st.online ? st.players + " / " + st.max : "-");
      setText("k-reg", st.region || "-");
      setText("k-up", st.updated ? ago(now - st.updated) : "-");
    }
    setText("k-addr", addr || "not public yet");
    if (addr) {
      var samp = enable(addr);
      if (isJoin && state !== "off") { setText("jp-t", "Opening the game…"); location.href = samp; }
      else if (isJoin) setText("jp-t", "The server looks offline right now. You can still try:");
    } else {
      var n = $("j-note");
      if (n) n.textContent = "The PC address is not public yet: the server owner has to set up the PC tunnel (PLAYIT_SECRET + OMP_PUBLIC_ADDR). Android players can already join from the MP Internet list.";
      if (isJoin) setText("jp-t", "The PC address is not public yet.");
    }
  }

  function load() {
    fetch(STATUS + "?t=" + Math.floor(Date.now() / 60000), { cache: "no-store" })
      .then(function (r) { if (!r.ok) throw 0; return r.json(); })
      .then(show, function () { show(null); });
  }
  load();
  if (!isJoin) setInterval(load, 60000);
})();
