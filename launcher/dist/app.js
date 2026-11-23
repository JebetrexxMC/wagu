// MP Launcher frontend (no build step). Talks to the Rust side through window.__TAURI__.
const T = window.__TAURI__;
const call = (cmd, args) => T ? T.core.invoke(cmd, args) : Promise.reject("Run inside MP Launcher");
const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const store = {
  get: (k, d) => { try { return JSON.parse(localStorage.getItem("mp." + k)) ?? d; } catch { return d; } },
  set: (k, v) => localStorage.setItem("mp." + k, JSON.stringify(v)),
};
const status = (t) => ($("#status").textContent = t);
const pingClass = (p) => (p == null ? "muted" : p < 90 ? "p-good" : p < 180 ? "p-ok" : "p-bad");
const pingText = (p) => (p == null ? "-" : p + " ms");
const split = (a) => { const i = a.lastIndexOf(":"); return i > 0 ? [a.slice(0, i), +a.slice(i + 1) || 7777] : [a, 7777]; };

// ---------- tabs ----------
document.querySelectorAll("nav button").forEach((b) =>
  b.addEventListener("click", () => {
    document.querySelectorAll("nav button,.tab").forEach((e) => e.classList.remove("on"));
    b.classList.add("on");
    $("#" + b.dataset.tab).classList.add("on");
    if (b.dataset.tab === "internet" && !all.length) loadInternet();
    if (b.dataset.tab === "favorites") renderFavs();
  })
);

// ---------- nickname + settings ----------
$("#nick").value = store.get("nick", "");
$("#nick").addEventListener("input", (e) => store.set("nick", e.target.value.trim()));
$("#gta").value = store.get("gta", "");
const checkGame = async () => {
  const d = $("#gta").value.trim();
  store.set("gta", d);
  if (!d) return ($("#gameCheck").textContent = "Pick your GTA San Andreas folder to play.");
  try {
    const c = await call("check_game", { gtaDir: d });
    $("#gameCheck").textContent = `gta_sa.exe ${c.gta_sa ? "found" : "missing"} - samp.dll ${c.samp ? "found" : "missing"} - open.mp client ${c.omp ? "found" : "not found (optional)"}`;
  } catch (e) { $("#gameCheck").textContent = String(e); }
};
$("#gta").addEventListener("change", checkGame);
$("#fromSamp").addEventListener("click", async () => {
  const s = await call("samp_settings").catch(() => ({}));
  if (s.gta_dir) $("#gta").value = s.gta_dir;
  if (s.name && !$("#nick").value) { $("#nick").value = s.name; store.set("nick", s.name); }
  checkGame();
  status(s.gta_dir ? "Loaded SA-MP settings" : "No SA-MP settings found on this PC");
});
(async () => {
  if (!store.get("gta", "") || !store.get("nick", "")) {
    const s = await call("samp_settings").catch(() => ({}));
    if (s.gta_dir && !$("#gta").value) $("#gta").value = s.gta_dir;
    if (s.name && !$("#nick").value) { $("#nick").value = s.name; store.set("nick", s.name); }
  }
  checkGame();
})();

// ---------- ping queue (small, so nothing floods the network) ----------
const pings = new Map();
const queue = [];
let running = 0;
function ping(host, port) {
  const key = host + ":" + port;
  if (pings.has(key)) return pings.get(key);
  const p = new Promise((res) => queue.push({ host, port, res }));
  pings.set(key, p);
  pump();
  return p;
}
function pump() {
  while (running < 16 && queue.length) {
    const j = queue.shift();
    running++;
    call("query", { host: j.host, port: j.port })
      .catch(() => ({ ok: false }))
      .then((r) => { running--; j.res(r); pump(); });
  }
}
function paintPing(td, r) { td.textContent = pingText(r.ping); td.className = "ping " + pingClass(r.ping); }

// ---------- featured / crossplay ----------
async function loadFeatured() {
  const list = await call("featured").catch(() => [{ name: "MP Crossplay 24/7 [PC + Mobile]", host: "wap-fresh.tun.ply.gg", port: 56616, note: "PC + Mobile" }]);
  $("#feat").innerHTML = "";
  for (const f of list) {
    const c = document.createElement("div");
    c.className = "card";
    c.innerHTML = `<h3>${esc(f.name)}</h3><div class="muted">${esc(f.host)}:${f.port}</div><div class="muted">${esc(f.note)}</div>
      <div class="info muted">Checking...</div><div class="row"><button class="go">Play</button><button class="det">Details</button></div>`;
    c.querySelector(".go").onclick = () => join(f.host, f.port, false);
    c.querySelector(".det").onclick = () => openSide({ host: f.host, port: f.port, hn: f.name });
    $("#feat").append(c);
    ping(f.host, f.port).then((r) => {
      c.querySelector(".info").innerHTML = r.ok
        ? `<span class="${pingClass(r.ping)}">${pingText(r.ping)}</span> - ${r.players}/${r.max} players${r.gamemode ? " - " + esc(r.gamemode) : ""}`
        : `<span class="p-bad">Offline right now</span>`;
    });
  }
}

// ---------- internet list ----------
let all = [];
let sortKey = "pc", sortDir = -1;
async function loadInternet() {
  status("Loading server list...");
  try {
    const v = await call("servers");
    all = (Array.isArray(v) ? v : []).map((s) => {
      const [host, port] = split(s.ip || "");
      return { host, port, hn: s.hn || s.ip, pc: s.pc | 0, pm: s.pm | 0, gm: s.gm || "", la: s.la || "", pa: !!s.pa, omp: !!s.omp, vn: s.vn || "", ping: null };
    }).filter((s) => s.host);
    status(`${all.length} servers`);
    renderInternet();
  } catch (e) { status("Server list failed: " + e); }
}
function filtered() {
  const q = $("#q").value.trim().toLowerCase();
  return all.filter((s) =>
    (!$("#fOmp").checked || s.omp) && (!$("#fFull").checked || s.pc < s.pm) && (!$("#fPass").checked || !s.pa) &&
    (!q || `${s.hn} ${s.gm} ${s.la} ${s.host}:${s.port}`.toLowerCase().includes(q))
  ).sort((a, b) => {
    const x = a[sortKey] ?? 1e9, y = b[sortKey] ?? 1e9;
    return (typeof x === "string" ? x.localeCompare(y) : x - y) * sortDir;
  });
}
function row(s, tb, extra) {
  const tr = document.createElement("tr");
  tr.innerHTML = `<td>${s.pa ? "&#128274;" : ""}</td><td>${esc(s.hn)}${s.omp ? '<span class="tag omp">open.mp</span>' : ""}</td>
    <td>${s.pc}/${s.pm}</td><td class="ping muted">-</td><td>${esc(s.gm)}</td><td>${extra ?? esc(s.la)}</td>`;
  tr.onclick = (e) => { if (!e.target.closest("button")) openSide(s); };
  tr.ondblclick = () => join(s.host, s.port, s.pa);
  tb.append(tr);
  const td = tr.children[3];
  if (s.ping != null) paintPing(td, s);
  else ping(s.host, s.port).then((r) => {
    s.ping = r.ping ?? null;
    if (r.ok && r.hostname) { s.pc = r.players; s.pm = r.max; tr.children[2].textContent = `${s.pc}/${s.pm}`; }
    paintPing(td, r);
  });
  return tr;
}
function renderInternet() {
  const list = filtered();
  const tb = $("#rows");
  tb.innerHTML = "";
  list.slice(0, 250).forEach((s) => row(s, tb));
  $("#count").textContent = list.length > 250 ? `showing 250 of ${list.length}` : `${list.length} servers`;
}
let t;
["#q", "#fOmp", "#fFull", "#fPass"].forEach((s) => $(s).addEventListener("input", () => { clearTimeout(t); t = setTimeout(renderInternet, 150); }));
$("#reload").onclick = () => { pings.clear(); loadInternet(); };
document.querySelectorAll("th[data-k]").forEach((th) =>
  th.addEventListener("click", () => {
    const k = th.dataset.k;
    sortDir = sortKey === k ? -sortDir : k === "hn" || k === "ping" || k === "gm" || k === "la" ? 1 : -1;
    sortKey = k;
    renderInternet();
  })
);

// ---------- favorites ----------
const favs = () => store.get("favs", [{ host: "wap-fresh.tun.ply.gg", port: 56616, hn: "MP Crossplay 24/7 [PC + Mobile]" }]);
function addFav(s) {
  const f = favs();
  if (!f.some((x) => x.host === s.host && x.port === s.port)) f.push({ host: s.host, port: s.port, hn: s.hn || `${s.host}:${s.port}` });
  store.set("favs", f);
  renderFavs();
}
function renderFavs() {
  const tb = $("#favRows");
  tb.innerHTML = "";
  favs().forEach((f, i) => {
    const s = { ...f, pc: 0, pm: 0, gm: "", la: "", pa: false, ping: null };
    const tr = row(s, tb, `<button>Remove</button>`);
    tr.querySelector("button").onclick = () => { const l = favs(); l.splice(i, 1); store.set("favs", l); renderFavs(); };
    pings.delete(f.host + ":" + f.port);
    ping(f.host, f.port).then((r) => { if (r.ok) { tr.children[1].textContent = r.hostname || f.hn; tr.children[4].textContent = r.gamemode; s.pa = r.password; } });
  });
}
$("#addFav").onclick = () => {
  const a = $("#addr").value.trim();
  if (!a) return;
  const [host, port] = split(a);
  addFav({ host, port });
  $("#addr").value = "";
};

// ---------- details panel + join ----------
let cur = null;
async function openSide(s) {
  cur = s;
  $("#side").classList.remove("hidden");
  $("#sName").textContent = s.hn || `${s.host}:${s.port}`;
  $("#sAddr").textContent = `${s.host}:${s.port}`;
  $("#sInfo").innerHTML = "<dt>Status</dt><dd>Checking...</dd>";
  $("#sMsg").textContent = "";
  $("#sPass").value = "";
  $("#sPass").classList.toggle("hidden", !s.pa);
  const r = await call("query", { host: s.host, port: s.port }).catch((e) => ({ ok: false, error: String(e) }));
  if (cur !== s) return;
  if (!r.ok) return ($("#sInfo").innerHTML = `<dt>Status</dt><dd class="p-bad">Offline (${esc(r.error)})</dd>`);
  if (r.hostname) $("#sName").textContent = r.hostname;
  s.pa = r.password;
  $("#sPass").classList.toggle("hidden", !r.password);
  $("#sInfo").innerHTML = `<dt>Ping</dt><dd class="${pingClass(r.ping)}">${pingText(r.ping)}</dd>
    <dt>Players</dt><dd>${r.players}/${r.max}</dd><dt>Mode</dt><dd>${esc(r.gamemode)}</dd>
    <dt>Language</dt><dd>${esc(r.language)}</dd><dt>Password</dt><dd>${r.password ? "Yes" : "No"}</dd>`;
}
$("#closeSide").onclick = () => { $("#side").classList.add("hidden"); cur = null; };
$("#sFav").onclick = () => cur && (addFav(cur), ($("#sMsg").textContent = "Added to favorites"));
$("#sPlay").onclick = () => cur && join(cur.host, cur.port, cur.pa);

async function join(host, port, locked) {
  let pass = "";
  if (locked) {
    if (!cur || cur.host !== host || cur.port !== port) return openSide({ host, port, pa: true });
    pass = $("#sPass").value;
  } else if (cur && cur.host === host && cur.port === port) pass = $("#sPass").value;
  const name = $("#nick").value.trim();
  const gtaDir = $("#gta").value.trim();
  if (!name) { status("Set a nickname first"); $("#nick").focus(); return; }
  if (!gtaDir) { status("Set your GTA San Andreas folder in Settings"); return; }
  status(`Starting game for ${host}:${port}...`);
  try {
    const m = await call("play", { host, port, name, password: pass, gtaDir });
    status(m);
    $("#sMsg").textContent = m;
  } catch (e) { status(String(e)); $("#sMsg").textContent = String(e); }
}

loadFeatured();
