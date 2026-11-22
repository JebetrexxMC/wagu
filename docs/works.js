/* Works with MP (5.02.57): docs/works.json from tools/omp_sweep/works_json.py */
(function () {
  "use strict";
  var data = null, shown = 150;
  function el(t, c, x) { var e = document.createElement(t); if (c) e.className = c; if (x != null) e.textContent = x; return e; }
  function n(x) { return (x || 0).toLocaleString("en-US"); }
  function toast(t) { var d = el("div", "toast", t); document.body.appendChild(d); setTimeout(function () { d.remove(); }, 1600); }
  function draw() {
    var q = document.getElementById("q").value.trim().toLowerCase();
    var list = data.servers.filter(function (s) { return !q || (s.n + " " + s.a + " " + s.v).toLowerCase().indexOf(q) >= 0; });
    var tb = document.getElementById("rows"); tb.textContent = "";
    list.slice(0, shown).forEach(function (s) {
      var tr = el("tr"), a = el("td"), p = el("td", "r", n(s.p)), v = el("td", "w-vc"), j = el("td");
      a.appendChild(el("span", "w-n", s.n)); a.appendChild(el("span", "w-a", s.a));
      v.appendChild(el("span", "w-v", s.v || "-"));
      var w = el("div", "w-j"), go = el("a", "btn main", "Join"), cp = el("button", "btn cp", "Copy");
      go.href = "samp://" + s.a; go.title = "Join " + s.a + " (SA-MP / open.mp on PC)";
      cp.type = "button"; cp.addEventListener("click", function () {
        (navigator.clipboard ? navigator.clipboard.writeText(s.a) : Promise.reject()).then(function () { toast("Copied " + s.a); }, function () { toast(s.a); });
      });
      w.appendChild(go); w.appendChild(cp); j.appendChild(w);
      [a, p, v, j].forEach(function (c) { tr.appendChild(c); }); tb.appendChild(tr);
    });
    var more = document.getElementById("w-more"); more.textContent = "";
    if (list.length > shown) {
      var b = el("button", "btn", "Show more (" + n(list.length - shown) + " left)"); b.type = "button";
      b.addEventListener("click", function () { shown += 300; draw(); }); more.appendChild(b);
    } else more.textContent = n(list.length) + " server" + (list.length === 1 ? "" : "s") + (q ? " match" : "") + ".";
  }
  fetch("works.json?v=" + Date.now()).then(function (r) { return r.json(); }).then(function (d) {
    data = d; var c = d.counts || {};
    document.getElementById("c-ok").textContent = n(d.servers.length);
    document.getElementById("c-all").textContent = n(d.checked);
    document.getElementById("c-no").textContent = n(c.no_initgame);
    document.getElementById("c-k").textContent = n((c.kicked || 0) + (c.loop || 0));
    document.getElementById("w-sum").textContent = n(d.servers.length) + " of " + n(d.checked) + " open servers let MP for Android join and spawn (checked " + d.date + "). Join opens the server in the SA-MP client or open.mp launcher on PC; on a phone, add the address in MP.";
    document.getElementById("q").addEventListener("input", function () { shown = 150; draw(); });
    draw();
  }, function () { document.getElementById("w-sum").textContent = "The list could not be loaded. Try again in a moment."; });
})();
