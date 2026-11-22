// 5.02.49: live player count of MP Crossplay 24/7 (the open.mp server) next to "PC" in the top bar.
// Same data as pc.html: omp247.json on the mp247-data branch, written every 10 min by server/omp_crossplay.sh.
(function () {
  "use strict";
  var URL = "https://raw.githubusercontent.com/JebetrexxMC/wagu/mp247-data/omp247.json";
  var STALE_S = 25 * 60;
  var b = document.getElementById("xpb"), n = document.getElementById("xpb-n");
  if (!b || !n) return;
  function show(st) {
    var now = Date.now() / 1000;
    var state = !st || !st.online ? "off" : (now - st.updated > STALE_S ? "stale" : "on");
    b.classList.remove("on", "off", "stale"); b.classList.add(state);
    if (state === "off") { n.textContent = "offline"; b.title = "MP Crossplay 24/7: offline"; }
    else {
      n.textContent = st.players + "/" + st.max;
      b.title = "MP Crossplay 24/7: " + st.players + " of " + st.max + " players" + (state === "stale" ? " (last check over 25 min ago)" : "");
    }
    b.setAttribute("aria-label", b.title);
    b.hidden = false;
  }
  function load() {
    fetch(URL + "?t=" + Math.floor(Date.now() / 60000), { cache: "no-store" })
      .then(function (r) { if (!r.ok) throw 0; return r.json(); })
      .then(show, function () { show(null); });
  }
  load();
  setInterval(load, 60000);
})();
