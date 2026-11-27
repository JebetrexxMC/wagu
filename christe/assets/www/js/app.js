/* Christe Multiplayer 1.0 - launcher UI (landscape). Talks to Android through window.Christe (MainActivity.Bridge).
   Server lists: MP lobby (live-status/status.json), MP Crossplay 24/7 (mp247-data/omp247.json), open.mp (api.open.mp/servers).
   Login: the MP account bot (js/mpauth.js, the same accounts as the game and Moquil). */
(function (G, D) {
  'use strict';
  var $ = function (s, r) { return (r || D).querySelector(s); }, $$ = function (s, r) { return Array.prototype.slice.call((r || D).querySelectorAll(s)); };
  var VERSION = '1.0.0';
  var URLS = {
    lobby: 'https://raw.githubusercontent.com/JebetrexxMC/wagu/live-status/status.json',
    f247: 'https://raw.githubusercontent.com/JebetrexxMC/wagu/mp247-data/omp247.json',
    omp: 'https://api.open.mp/servers',
    rel: 'https://api.github.com/repos/JebetrexxMC/wagu/releases/latest'
  };
  var DISCORD = (G.MPAuth && MPAuth.DISCORD) || 'https://discord.gg/QSdtut3SWV';
  var REGIONS = ['Auto', 'Asia', 'Europe', 'NA', 'Oceania'];
  G.MPAUTH_APP = 'christe';

  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function ls(k, v) { try { if (v === undefined) { var x = G.localStorage.getItem('cx_' + k); return x == null ? null : JSON.parse(x); } if (v === null) G.localStorage.removeItem('cx_' + k); else G.localStorage.setItem('cx_' + k, JSON.stringify(v)); } catch (e) { return null; } }
  function ic(n, cls) { return '<svg' + (cls ? ' class="' + cls + '"' : '') + '><use href="#i-' + n + '"/></svg>'; }
  function hue(s) { var h = 0; s = String(s || ''); for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0; return Math.abs(h) % 360; }
  function initials(s) { var w = String(s || '?').replace(/[^A-Za-z0-9 ]/g, ' ').trim().split(/\s+/); return ((w[0] || '?')[0] + (w[1] ? w[1][0] : (w[0] || '')[1] || '')).toUpperCase(); }
  function art(seed, a) { var h = hue(seed), h2 = (h + 50) % 360; return 'background:radial-gradient(ellipse at 85% 0%,hsla(' + h + ',80%,55%,' + (a || .55) + '),transparent 60%),radial-gradient(ellipse at 0% 100%,hsla(' + h2 + ',75%,45%,.35),transparent 60%),linear-gradient(160deg,#151a2b,#0b0e18)'; }
  function logo(seed) { var h = hue(seed); return 'background:linear-gradient(135deg,hsl(' + h + ',70%,52%),hsl(' + ((h + 40) % 360) + ',70%,38%))'; }

  // ---------- the Android bridge (and a browser fallback for testing) ----------
  var NATIVE = !!G.Christe;
  var CB = {}, PCB = {}, cbn = 0;
  var X = NATIVE ? G.Christe : {
    info: function () { return JSON.stringify({ deviceAbi: 'arm64-v8a', abi: 'arm64-v8a', abiMode: 'auto', abis: ['arm64-v8a', 'armeabi-v7a'], sdk: 34, android: '14', model: 'Browser preview', saPkg: 'com.rockstargames.gtasa',
      sa: { installed: true, version: '2.10', abi: 'arm64-v8a' }, vc: { installed: true, version: '1.12' }, vl: { installed: false }, moquil: { installed: false },
      saObb: [{ name: 'main.8.com.rockstargames.gtasa.obb', found: true, visible: true, size: 1970000000 }, { name: 'patch.8.com.rockstargames.gtasa.obb', found: true, visible: true, size: 625000000 }],
      vcObb: [{ name: 'main.0.com.rockstargames.gtavc.obb', found: false, visible: false }], ownObb: '', ownObbDir: '/storage/emulated/0/Android/obb/com.christe.mp',
      date2: '/storage/emulated/0/Android/data/com.christe.mp/date2', online: navigator.onLine, version: VERSION, hasLib32: true, hasLib64: true }); },
    online: function () { return navigator.onLine; }, setup: function () { return '{"ok":true,"lib":"ok"}'; }, set: function () {}, startupVideo: function () { return ''; },
    get: function (id, url) { fetch(url).then(function (r) { return r.text().then(function (t) { G.CX_got(id, r.status, t); }); }).catch(function (e) { G.CX_got(id, 0, String(e)); }); },
    ping: function (id) { setTimeout(function () { G.CX_ping(id, 20 + Math.round(Math.random() * 180)); }, 300); },
    play: function () { return 'ok'; }, openApp: function () { return false; }, open: function (u) { G.open(u, '_blank'); }, toast: function (t) { toast(t); }, exit: function () {}, log: function () {}
  };
  G.CX_got = function (id, code, body) { var f = CB[id]; delete CB[id]; if (f) f(code, body); };
  G.CX_ping = function (id, ms) { var f = PCB[id]; delete PCB[id]; if (f) f(ms); };
  function get(url, cb) { var id = 'g' + (++cbn); CB[id] = cb; X.get(id, url); }
  function getJson(url) { return new Promise(function (ok, no) { get(url, function (c, b) { if (c >= 200 && c < 300) { try { ok(JSON.parse(b)); } catch (e) { no(new Error('bad JSON')); } } else no(new Error('HTTP ' + c)); }); }); }
  function ping(host, port, cb) { var id = 'p' + (++cbn); PCB[id] = cb; X.ping(id, host, port | 0); }
  var INFO = {};
  function readInfo() { try { INFO = JSON.parse(X.info()) || {}; } catch (e) { INFO = {}; } return INFO; }

  // ---------- state ----------
  var S = {
    page: 'home', tab: 'all', comm: null, q: '', sort: ls('sort') || 'players', sortDir: -1,
    region: ls('region') || 0, rows: [], loaded: false, loading: false, err: '', online: true, sel: null,
    favs: ls('favs') || {}, notes: ls('notes') || [], user: ls('session'), nick: ls('nick') || '', setTab: 'account',
    exploreSort: 'rec', autoRefresh: ls('autoRefresh') !== false, relTag: ''
  };
  function nick() { return (S.user && S.user.user) || S.nick || ''; }

  // ---------- communities ----------
  var COMMS = [
    { id: 'mp', name: 'MP Official', tag: 'MP', desc: 'The MP lobby: MP LAN Server and every player\'s own server, GTA SA on Android.', match: function (r) { return r.kind === 'mp' && !SPECIAL.test(r.name); }, badge: 'Official' },
    { id: 'f247', name: 'MP Crossplay 24/7', tag: '24', desc: 'open.mp 24/7 server for PC (open.mp launcher, SA-MP 0.3.7 / 0.3.DL) and MP for Android together.', match: function (r) { return r.kind === 'f247'; }, badge: 'Crossplay' },
    { id: 'bcw', name: 'Brief Case War', tag: 'BC', desc: 'Grab the briefcase and hold it. Team mode on the MP lobby.', match: function (r) { return /brief ?case/i.test(r.name); } },
    { id: 'mw3', name: 'Call Of Duty: MW3', tag: 'CO', desc: 'Modern Warfare 3 style deathmatch with loadouts and killstreaks.', match: function (r) { return /modern warfare|mw3/i.test(r.name); } },
    { id: 'mvf', name: 'Mega Vil Futuristic', tag: 'MV', desc: 'Roleplay in a futuristic city with jobs, houses and gangs.', match: function (r) { return /mega vil/i.test(r.name); } },
    { id: 'pdz', name: 'Project Day Z Legacy', tag: 'DZ', desc: 'Survival: loot, bases, zombies and the Day Z map.', match: function (r) { return /day ?z/i.test(r.name); } },
    { id: 'lg', name: 'MP League', tag: 'LG', desc: 'Ranked matches between teams.', match: function (r) { return /league/i.test(r.name); } },
    { id: 'vc', name: 'Vice City', tag: 'VC', desc: 'GTA Vice City with ViceLoader: the VC LAN server and crossplay with San Andreas players.', match: function (r) { return r.game === 'vc'; }, badge: 'VC' },
    { id: 'omp', name: 'open.mp Community', tag: 'OM', desc: 'Every open.mp / SA-MP PC server. MP joins them through crossplay.', match: function (r) { return r.kind === 'omp'; } }
  ];
  var SPECIAL = /brief ?case|modern warfare|mw3|mega vil|day ?z|league/i;
  function commRows(c) { return S.rows.filter(c.match); }
  function commPlayers(c) { return commRows(c).reduce(function (n, r) { return n + (r.players | 0); }, 0); }

  // ---------- server rows ----------
  function rowKey(r) { return r.kind + '|' + (r.host || r.name).toLowerCase() + '|' + r.port; }
  function isVc(name, mode) { return /vice ?city|\bvc\b|vclan/i.test((name || '') + ' ' + (mode || '')); }
  function joinSpec(r) { return r.kind === 'mp' ? 'mp|' + r.name + '|' + r.port + '|' + (r.host || '') : 'omp|' + r.host + '|' + r.port + '|' + r.name; }
  function platforms(r) {
    var p = [];
    if (r.game === 'vc') p.push('<span class="chip vc">VC</span>'); else p.push('<span class="chip sa">SA</span>');
    if (r.kind === 'mp') p.push('<span class="chip mob">Mobile</span>');
    else { p.push('<span class="chip pc">PC</span>'); p.push('<span class="chip mob">Mobile</span>'); }
    if (r.kind === 'f247' || r.cross) p.push('<span class="chip neon">Crossplay</span>');
    return p.join('');
  }
  function loadServers(force) {
    if (S.loading) return;
    S.online = X.online(); if (!S.online) { S.err = 'offline'; render(); return; }
    S.loading = true; S.err = ''; $('#refreshBtn').classList.add('spin'); if (!S.loaded) render();
    var rows = [];
    var a = getJson(URLS.lobby).then(function (j) {
      var seen = {};
      (j.servers || []).forEach(function (s) {
        var k = (s.name || '').toLowerCase() + '|' + s.port;
        if (seen[k]) { seen[k].players = Math.max(seen[k].players, s.players | 0); return; }
        var r = { kind: 'mp', name: s.name || 'MP server', host: '', port: s.port | 0, players: s.players | 0, max: s.max | 0, region: (s.region | 0) + 1, mode: 'Freeroam', lang: 'Any', locked: !!s.locked, ping: -2, game: isVc(s.name) ? 'vc' : 'sa', desc: '' };
        if (/brief ?case/i.test(r.name)) r.mode = 'Brief Case War'; else if (/mw3|modern warfare/i.test(r.name)) r.mode = 'Deathmatch'; else if (/mega vil/i.test(r.name)) r.mode = 'Roleplay'; else if (/day ?z/i.test(r.name)) r.mode = 'Survival';
        seen[k] = r; rows.push(r);
      });
      S.lobbyMeta = { updated: j.updated, players: j.players };
    });
    var b = getJson(URLS.f247).then(function (j) {
      if (!j || !j.addr) return;
      var hp = String(j.addr).split(':');
      rows.push({ kind: 'f247', name: j.name || 'MP Crossplay 24/7', host: hp[0], port: (hp[1] | 0) || j.port, players: j.players | 0, max: j.max | 0, region: REGIONS.indexOf(j.region) > 0 ? REGIONS.indexOf(j.region) : 1,
        mode: j.gamemode || 'TDM', lang: 'English', locked: false, ping: -1, game: 'sa', cross: true, live: !!j.online, desc: (j.server || '') + ' - ' + (j.clients || []).join(', ') });
    });
    var c = getJson(URLS.omp).then(function (j) {
      (j || []).slice(0, 600).forEach(function (s) {
        var hp = String(s.ip || '').split(':'); if (!hp[0]) return;
        rows.push({ kind: 'omp', name: s.hn || s.ip, host: hp[0], port: (hp[1] | 0) || 7777, players: s.pc | 0, max: s.pm | 0, region: 0, mode: s.gm || '', lang: s.la || '', locked: !!s.pa, ping: -1, game: isVc(s.hn, s.gm) ? 'vc' : 'sa', ver: s.vn || '', desc: s.vn || '' });
      });
    });
    Promise.all([a.catch(function (e) { return e; }), b.catch(function (e) { return e; }), c.catch(function (e) { return e; })]).then(function (res) {
      S.loading = false; $('#refreshBtn').classList.remove('spin');
      var fails = res.filter(function (x) { return x instanceof Error; }).length;
      if (!rows.length) { S.online = X.online(); S.err = S.online ? 'down' : 'offline'; render(); return; }
      var old = {}; S.rows.forEach(function (r) { old[rowKey(r)] = r.ping; });
      rows.forEach(function (r) { r.key = rowKey(r); if (old[r.key] >= 0) r.ping = old[r.key]; });
      S.rows = rows; S.loaded = true; S.err = fails === 3 ? 'down' : '';
      notesFromServers(); renderSide(); render(); pingVisible();
    });
  }
  var pinging = 0;
  function pingVisible() {
    var list = filtered().filter(function (r) { return r.kind !== 'mp' && r.ping === -1; }).slice(0, 30);
    list.forEach(function (r) {
      r.ping = -3; pinging++;
      ping(r.host, r.port, function (ms) { pinging--; r.ping = ms >= 0 ? ms : -4; var el = D.querySelector('[data-key="' + cssq(r.key) + '"] .pg'); if (el) { el.className = 'pg ' + pcls(r.ping); el.textContent = ptxt(r); } });
    });
  }
  function cssq(s) { return String(s).replace(/["\\]/g, '\\$&'); }
  function pcls(p) { return p < 0 ? 'p-none' : p < 80 ? 'p-good' : p < 160 ? 'p-mid' : 'p-bad'; }
  function ptxt(r) { if (r.kind === 'mp') return REGIONS[r.region] || '-'; return r.ping >= 0 ? String(r.ping) : r.ping === -3 ? '...' : '-'; }

  function filtered() {
    var q = S.q.trim().toLowerCase(), rows = S.rows;
    if (S.page === 'favorites') rows = rows.filter(function (r) { return S.favs[r.key]; });
    else if (S.page === 'community' && S.comm) rows = rows.filter(S.comm.match);
    else if (S.page === 'internet') {
      if (S.tab === 'mp') rows = rows.filter(function (r) { return r.kind !== 'omp'; });
      else if (S.tab === 'pc') rows = rows.filter(function (r) { return r.kind !== 'mp'; });
      else if (S.tab === 'vc') rows = rows.filter(function (r) { return r.game === 'vc'; });
    }
    if (S.region) rows = rows.filter(function (r) { return r.kind === 'omp' || r.kind === 'f247' || r.region === S.region; });
    if (q) rows = rows.filter(function (r) { return (r.name + ' ' + r.mode + ' ' + r.lang + ' ' + (r.host || '')).toLowerCase().indexOf(q) >= 0; });
    var k = S.sort, d = S.sortDir;
    rows = rows.slice().sort(function (a, b) {
      var fa = a.kind === 'f247' ? 1 : 0, fb = b.kind === 'f247' ? 1 : 0; if (fa !== fb) return fb - fa;
      if (k === 'name') return d * (a.name.localeCompare(b.name) * -1);
      if (k === 'ping') { var pa = a.ping >= 0 ? a.ping : 9999, pb = b.ping >= 0 ? b.ping : 9999; return d * (pb - pa); }
      if (k === 'mode') return d * (a.mode.localeCompare(b.mode) * -1);
      return d * ((a.players - b.players) || (a.kind === 'mp' ? 1 : 0) - (b.kind === 'mp' ? 1 : 0));
    });
    return rows;
  }

  // ---------- pages ----------
  function render() {
    $$('#nav a, .sideFoot a').forEach(function (a) { a.classList.toggle('on', a.dataset.page === S.page); });
    $$('.comm').forEach(function (c) { c.classList.toggle('on', S.page === 'community' && S.comm && c.dataset.id === S.comm.id); });
    var P = $('#page'), f = PAGES[S.page] || PAGES.home; P.innerHTML = f(); P.scrollTop = 0; bindPage();
  }
  function needNet() {
    if (S.err === 'offline') return oops('No internet connection', 'Something went wrong. Try turning on Wi-Fi or mobile data, then tap Retry.', 'wifi-off');
    if (S.err === 'down' && !S.rows.length) return oops('Something went wrong', 'The server list could not be loaded. Check your connection or try again in a moment.', 'globe');
    return '';
  }
  function oops(t, p, icon) { return '<div class="oops"><div class="oi">' + ic(icon) + '</div><h2>' + esc(t) + '</h2><p>' + esc(p) + '</p><div style="display:flex;gap:8px;margin-top:6px"><button class="btn go" data-act="retry">' + ic('refresh') + 'Retry</button><button class="btn" data-act="offline-play">Play offline</button></div></div>'; }
  function skelRows(n) { var o = ''; for (var i = 0; i < n; i++) o += '<div class="skRow"><span class="sk" style="width:14px"></span><span class="sk" style="width:' + (30 + (i * 37) % 45) + '%"></span><span class="grow" style="flex:1"></span><span class="sk" style="width:46px"></span><span class="sk" style="width:30px"></span></div>'; return o; }
  function skelCards(n) { var o = ''; for (var i = 0; i < n; i++) o += '<div class="scard"><span class="sk" style="height:12px;width:80%"></span><span class="sk" style="height:9px;width:55%;margin-top:7px"></span><div class="sf"><span class="sk" style="width:30px;height:30px;border-radius:8px"></span><span style="flex:1"></span><span class="sk" style="height:10px;width:70px"></span></div></div>'; return o; }

  function obbLine(list) {
    if (!list || !list.length) return '';
    return list.map(function (o) { return o.found ? '<span class="st-ok">' + esc(o.name.split('.')[0]) + ' OBB ' + (o.size ? (o.size / 1073741824).toFixed(2) + ' GB' : 'ok') + '</span>' : o.visible ? '<span class="st-bad">' + esc(o.name.split('.')[0]) + ' OBB missing</span>' : '<span class="st-mid">' + esc(o.name.split('.')[0]) + ' OBB: read by the game</span>'; }).join(' &middot; ');
  }
  function gameCard(g) {
    var sa = g === 'sa', inf = sa ? INFO.sa || {} : INFO.vc || {}, vl = INFO.vl || {};
    var abi = sa ? (INFO.abi || 'armeabi-v7a') : '';
    var ver = inf.installed ? 'v' + esc(inf.version || '?') : 'not installed';
    var abiChip = sa ? '<span class="chip ' + (abi.indexOf('64') > 0 ? 'mob' : 'pc') + '">' + ic('chip') + esc(abi) + ' &middot; GTA SA ' + (abi.indexOf('64') > 0 ? '2.10' : '2.00') + '</span>' : '';
    return '<div class="gcard ' + g + '"><div class="art">' + (sa ? 'SA' : 'VC') + '</div><div class="gt">' + (sa ? 'GRAND THEFT AUTO' : 'GRAND THEFT AUTO') + '</div>' +
      '<h2>' + (sa ? 'San Andreas' : 'Vice City') + '</h2>' +
      '<div class="gi"><span>' + (sa ? 'AML' : (vl.installed ? 'ViceLoader' : 'VC')) + ' ' + ver + '</span>' + abiChip + '</div>' +
      '<div class="gi">' + obbLine(sa ? INFO.saObb : INFO.vcObb) + '</div>' +
      '<div class="ga"><button class="btn ' + (sa ? 'go' : 'vc') + '" data-play="' + g + '"' + (inf.installed ? '' : ' disabled') + '>' + ic('play') + 'Play ' + (sa ? 'GTA SA' : 'Vice City') + '</button>' +
      '<button class="btn sm" data-act="quick-' + g + '">Quick connect</button></div></div>';
  }
  function serverCard(r) {
    return '<div class="scard" data-open-row="' + esc(r.key) + '"><div class="bgart" style="' + art(r.name) + '"></div><div class="st">' + esc(r.name) + '</div><div class="sd">' + esc(r.mode || '') + (r.desc ? ' &middot; ' + esc(r.desc) : '') + '</div>' +
      '<div class="sf"><div class="sl" style="' + logo(r.name) + '">' + esc(initials(r.name)) + '</div><div class="sn"><div class="on"><i></i>' + r.players + ' / ' + r.max + '</div><b>' + platforms(r) + '</b></div></div></div>';
  }
  var PAGES = {
    home: function () {
      var feat = S.rows.filter(function (r) { return r.kind !== 'omp'; }).sort(function (a, b) { return (b.kind === 'f247') - (a.kind === 'f247') || b.players - a.players; }).slice(0, 6);
      var top = S.rows.filter(function (r) { return r.kind === 'omp'; }).sort(function (a, b) { return b.players - a.players; }).slice(0, 6);
      var tot = S.rows.reduce(function (n, r) { return n + (r.kind !== 'omp' ? r.players : 0); }, 0);
      return '<div class="ph"><h1>Welcome' + (nick() ? ', <b>' + esc(nick()) + '</b>' : ' to <b>Christe</b>') + '</h1><span class="grow"></span><span class="count">' + (S.loaded ? tot + ' MP PLAYERS ONLINE &middot; ' + S.rows.length + ' SERVERS' : '') + '</span></div>' +
        '<div class="hero">' + gameCard('sa') + gameCard('vc') + '</div>' +
        (needNet() ? '<div style="height:230px">' + needNet() + '</div>' :
        '<div class="sec">Featured MP servers<span class="grow"></span><a data-go="internet">See all</a></div><div class="cards">' + (S.loaded ? feat.map(serverCard).join('') : skelCards(6)) + '</div>' +
        '<div class="sec">Top crossplay (PC + Mobile)<span class="grow"></span><a data-go="internet" data-tab="pc">See all</a></div><div class="cards">' + (S.loaded ? top.map(serverCard).join('') : skelCards(3)) + '</div>');
    },
    internet: function () { return browser('Internet', [['all', 'All'], ['mp', 'MP'], ['pc', 'PC cross'], ['vc', 'Vice City']]); },
    favorites: function () { return browser('Favorites', null); },
    community: function () {
      var c = S.comm || COMMS[0], rs = commRows(c);
      return '<div class="comPage"><div class="banner"><div class="bgart" style="' + art(c.name, .8) + '"></div><div class="big" style="' + logo(c.name) + '">' + esc(c.tag) + '</div><div style="flex:1;min-width:0"><h2>' + esc(c.name) + '</h2><p>' + esc(c.desc) + '</p></div>' +
        '<div style="text-align:right"><div class="chip neon">' + ic('users') + commPlayers(c) + ' online</div><div style="margin-top:6px"><button class="btn sm blue" data-open-url="' + esc(DISCORD) + '">' + ic('discord') + 'Discord</button></div></div></div>' +
        (needNet() || '<div style="height:calc(100vh - 230px);min-height:170px">' + table(rs, true) + '</div>') + '</div>';
    },
    explore: function () {
      var q = S.q.trim().toLowerCase(), list = COMMS.filter(function (c) { return !q || (c.name + ' ' + c.desc).toLowerCase().indexOf(q) >= 0; });
      if (S.exploreSort === 'players') list.sort(function (a, b) { return commPlayers(b) - commPlayers(a); });
      else if (S.exploreSort === 'az') list.sort(function (a, b) { return a.name.localeCompare(b.name); });
      return '<div class="ph"><h1>Explore <b>Community</b></h1><span class="sub">Partnered MP communities and servers</span></div>' +
        '<div class="toolbar"><span style="flex:1"></span><select class="dd" id="exSort"><option value="rec">Recommended</option><option value="players">Most players</option><option value="az">A - Z</option></select></div>' +
        (needNet() || '<div class="cards">' + (S.loaded ? list.map(function (c) {
          var n = commRows(c).length;
          return '<div class="scard ccard" data-comm="' + c.id + '" style="min-height:128px"><div class="bgart" style="' + art(c.name, .7) + '"></div><div class="st">' + esc(c.name) + (c.badge ? ' <span class="badge">' + esc(c.badge) + '</span>' : '') + '</div><div class="sd">' + esc(c.desc) + '</div>' +
            '<div class="sf"><div class="sl" style="' + logo(c.name) + '">' + esc(c.tag) + '</div><div class="sn"><div class="on"><i></i>' + commPlayers(c) + ' online</div><b>' + n + ' server' + (n === 1 ? '' : 's') + '</b></div></div></div>';
        }).join('') : skelCards(6)) + '</div>');
    },
    about: function () {
      return '<div class="ph"><h1>About <b>Christe Multiplayer</b></h1><span class="sub">v' + esc(INFO.version || VERSION) + '</span></div>' +
        '<div class="card"><h3>The launcher</h3><p style="margin:0 0 6px;color:#c3c7d2">Christe Multiplayer is the MP launcher for GTA San Andreas (AML) and GTA Vice City on Android, with SA + VC + PC crossplay. It starts the games you already have installed, with their own OBB files, and hands them the server you picked. MP (libMP) joins it from the title screen.</p>' +
        '<div class="kv"><span>MP mod</span><span>libMP 5.02.76+ (joins the server picked here)</span><span>GTA SA</span><span>com.rockstargames.gtasa - OBB main.8 + patch.8</span><span>Vice City</span><span>com.rockstargames.gtavc + ViceLoader (127.0.0.1:47790)</span><span>CPU</span><span>' + esc(INFO.deviceAbi || '') + ' device, MP ' + esc(INFO.abi || '') + ' (arm64-v8a = 2.10, armeabi-v7a = 2.00)</span><span>Own files</span><span>' + esc(INFO.date2 || '') + '</span><span>Own OBB</span><span>' + esc(INFO.ownObbDir || '') + '</span></div></div>' +
        '<div class="card"><h3>From the MP mod</h3><div class="kv"><span>Mod</span><span>MP (Multiplayer for GTA SA Android) by JebetrexxMC / ThirtyFoxMC</span><span>Source</span><span><a data-open-url="https://github.com/JebetrexxMC/wagu">github.com/JebetrexxMC/wagu</a></span><span>Website</span><span><a data-open-url="https://mpsamp-open.github.io">mpsamp-open.github.io</a></span><span>Discord</span><span><a data-open-url="' + esc(DISCORD) + '">MP Discord server</a></span><span>Latest MP release</span><span>' + esc(S.relTag || '-') + '</span></div></div>' +
        '<div class="card"><h3>Open source used</h3><div class="kv"><span>Skeleton loaders</span><span>idea from SkeletonView (Juanpe)</span><span>Loading bars</span><span>LoadingAnimations (Nick Babich)</span><span>Boot hand-off</span><span>idea from dragon-codex-boot, react-native-lottie-splash-screen</span><span>Server list</span><span>open.mp API, MP lobby</span></div></div>';
    },
    settings: function () {
      var tabs = [['account', 'Account'], ['game', 'Game'], ['vc', 'Vice City'], ['storage', 'Storage & OBB'], ['network', 'Network'], ['ui', 'Interface']];
      return '<div class="setGrid"><nav class="setNav">' + tabs.map(function (t) { return '<a data-set="' + t[0] + '" class="' + (S.setTab === t[0] ? 'on' : '') + '">' + t[1] + '</a>'; }).join('') + '</nav><div class="setBody">' + (SET[S.setTab] || SET.account)() + '</div></div>';
    }
  };
  function browser(title, tabs) {
    var rows = filtered();
    var side = tabs ? '<div class="tabsV">' + tabs.map(function (t) { return '<a data-tab="' + t[0] + '" class="' + (S.tab === t[0] ? 'on' : '') + '">' + t[1] + '</a>'; }).join('') + '</div>' : '';
    return '<div class="browser" style="height:100%">' + side + (needNet() ? '<div style="flex:1">' + needNet() + '</div>' : table(rows, false)) + '</div>';
  }
  function table(rows, inComm) {
    if (!rows) rows = filtered();
    if (inComm) { var q = S.q.trim().toLowerCase(); if (q) rows = rows.filter(function (r) { return r.name.toLowerCase().indexOf(q) >= 0; }); }
    var sel = rows.filter(function (r) { return r.key === S.sel; })[0] || rows[0];
    if (sel) S.sel = sel.key;
    function th(k, t, cls) { return '<span class="' + (cls || '') + (S.sort === k ? ' sorted' : '') + '" data-sort="' + k + '">' + t + (S.sort === k ? (S.sortDir < 0 ? ' &#9662;' : ' &#9652;') : '') + '</span>'; }
    var body = !S.loaded ? skelRows(9) : rows.length ? rows.slice(0, 400).map(function (r) {
      return '<div class="trow' + (sel && r.key === sel.key ? ' sel' : '') + '" data-key="' + esc(r.key) + '"><span class="fav' + (S.favs[r.key] ? ' y' : '') + '" data-fav="' + esc(r.key) + '">' + ic('star') + '</span><span class="lk' + (r.locked ? ' y' : '') + '">' + (r.locked ? ic('lock') : '') + '</span>' +
        '<span class="nm">' + esc(r.name) + (r.kind === 'f247' ? '<em>24/7</em>' : r.game === 'vc' ? '<em>VC</em>' : '') + '</span><span class="pl"><b>' + r.players + '</b><span> / ' + r.max + '</span></span><span class="pg ' + pcls(r.kind === 'mp' ? 0 : r.ping) + '">' + ptxt(r) + '</span><span class="md">' + esc(r.mode) + '</span><span class="lg">' + esc(r.lang) + '</span></div>';
    }).join('') : '<div class="empty">' + (S.page === 'favorites' ? 'No favourite servers yet. Tap the star of a server to add it.' : 'No servers match.') + '</div>';
    var det = sel ? '<div class="detail"><div class="dn"><b>' + esc(sel.name) + '</b><span>' + platforms(sel) + '<span>' + esc(sel.kind === 'mp' ? 'MP lobby &middot; port ' + sel.port + ' &middot; ' + (REGIONS[sel.region] || '') : sel.host + ':' + sel.port) + '</span>' + (sel.desc ? '<span>' + esc(sel.desc) + '</span>' : '') + '</span></div>' +
      '<button class="btn sm" data-fav="' + esc(sel.key) + '">' + ic('star') + (S.favs[sel.key] ? 'Saved' : 'Favorite') + '</button>' +
      (sel.game === 'vc' ? '<button class="btn vc" data-join="' + esc(sel.key) + '" data-game="vc">' + ic('play') + 'Join (VC)</button>' : '') +
      '<button class="btn go" data-join="' + esc(sel.key) + '" data-game="sa">' + ic('users') + 'JOIN</button></div>' : '';
    return '<div class="tbl"><div class="thead"><span>' + ic('star') + '</span><span>' + ic('lock') + '</span>' + th('name', 'Server') + th('players', 'Players') + th('ping', 'Ping', 'pgh') + th('mode', 'Mode', 'md') + '<span class="lg">Lang</span></div><div class="tbody">' + body + '</div>' + det + '</div>';
  }

  // ---------- settings pages ----------
  function opt(t, s, ctl) { return '<div class="opt"><div class="ol"><b>' + t + '</b>' + (s ? '<span>' + s + '</span>' : '') + '</div>' + (ctl || '') + '</div>'; }
  function sw(id, on) { return '<button class="sw' + (on ? ' on' : '') + '" data-sw="' + id + '"></button>'; }
  var SET = {
    account: function () {
      return '<div class="card"><h3>MP account</h3>' + (S.user ? opt('Signed in as ' + esc(S.user.user), 'The same account as in the game, Moquil and the MP apps', '<button class="btn sm red" data-act="logout">Sign out</button>')
        : opt('Guest', 'Sign in or create an MP account to keep your name, stats and friends', '<button class="btn sm go" data-act="auth">Sign in</button>')) +
        opt('Name in game', 'Sent to MP when the game starts', '<input class="inp" id="nickInp" style="width:170px" maxlength="23" value="' + esc(nick()) + '"' + (S.user ? ' disabled' : '') + '>') + '</div>';
    },
    game: function () {
      var sa = INFO.sa || {};
      return '<div class="card"><h3>GTA San Andreas (AML)</h3>' + opt('Game', esc(INFO.saPkg || 'com.rockstargames.gtasa') + ' &middot; ' + (sa.installed ? 'v' + esc(sa.version) + ' (' + esc(sa.abi || '?') + ')' : '<span class="st-bad">not installed</span>'), '<button class="btn sm" data-act="open-sa">Open</button>') +
        opt('Game package', 'Change only for a renamed AML GTA SA', '<input class="inp" id="saPkg" style="width:210px" value="' + esc(INFO.saPkg || 'com.rockstargames.gtasa') + '">') +
        opt('CPU / MP build', 'Auto: from the installed game, else the phone (' + esc(INFO.deviceAbi || '') + '). arm64-v8a = GTA SA 2.10, armeabi-v7a = GTA SA 2.00',
          '<select class="dd" id="abiSel"><option value="auto">Auto (' + esc(INFO.abi || '') + ')</option><option value="arm64-v8a">arm64-v8a (2.10)</option><option value="armeabi-v7a">armeabi-v7a (2.00)</option></select>') +
        opt('Join from the title screen', 'MP 5.02.76+ joins the server you picked as soon as the title menu is up', sw('autoJoin', ls('autoJoin') !== false)) + '</div>';
    },
    vc: function () {
      var vc = INFO.vc || {}, vl = INFO.vl || {};
      return '<div class="card"><h3>Vice City</h3>' + opt('GTA Vice City', vc.installed ? 'v' + esc(vc.version) : '<span class="st-bad">not installed</span>', '<button class="btn sm" data-act="open-vc">Open</button>') +
        opt('ViceLoader', vl.installed ? 'installed v' + esc(vl.version) + ' - the MP overlay for Vice City (joins over 127.0.0.1:47790)' : 'not installed (ViceLoader.apk in the wagu release)', '<button class="btn sm" data-open-url="https://github.com/JebetrexxMC/wagu/releases">Get</button>') +
        opt('OBB', obbLine(INFO.vcObb), '') + '</div>';
    },
    storage: function () {
      var lib = INFO.abi && (INFO.abi.indexOf('64') > 0 ? INFO.hasLib64 : INFO.hasLib32);
      return '<div class="card"><h3>Christe folder (date2)</h3>' + opt('date2', esc(INFO.date2 || ''), '<button class="btn sm" data-act="setup">Repair</button>') +
        opt('Folders', 'resources, libs, files, configs, mods, scripts, logs', '') +
        opt('libMP for this phone', 'date2/libs/' + esc(INFO.abi || '') + '/libMP.so ' + (lib ? '<span class="st-ok">bundled</span>' : '<span class="st-mid">not in this APK</span>') + ' - copy it into GTA SA\'s AML mods folder to update MP', '') +
        opt('Boot video', 'Put your own startup.mp4 in date2/resources to play it at start-up', '') + '</div>' +
        '<div class="card"><h3>OBB</h3>' + opt('GTA SA', obbLine(INFO.saObb) || '-', '') + opt('Shared OBB', 'Christe and AML GTA SA use the same OBB in Android/obb/com.rockstargames.gtasa (or Android_unprotected): nothing is downloaded or copied twice.', '') +
        opt('Christe OBB', esc(INFO.ownObb || (INFO.ownObbDir || '') + '/main.&lt;n&gt;.com.christe.mp.obb (optional)') + ' - launcher resources, unpacked into date2/resources', '') + '</div>';
    },
    network: function () {
      return '<div class="card"><h3>Network</h3>' + opt('Status', S.online ? '<span class="st-ok">Online</span>' : '<span class="st-bad">Offline - turn on Wi-Fi or mobile data</span>', '<button class="btn sm" data-act="retry">Retry</button>') +
        opt('Region', 'MP lobby servers of this region (Auto = all)', '<select class="dd" id="regSel">' + REGIONS.map(function (r, i) { return '<option value="' + i + '"' + (S.region === i ? ' selected' : '') + '>' + r + '</option>'; }).join('') + '</select>') +
        opt('Refresh the list every 60 s', '', sw('autoRefresh', S.autoRefresh)) + '</div>';
    },
    ui: function () {
      return '<div class="card"><h3>Interface</h3>' + opt('Boot animation', 'The animated start-up screen', sw('bootAnim', ls('bootAnim') !== false)) +
        opt('Notifications', 'MP updates and server news in the bell', sw('notify', ls('notify') !== false)) + '</div>';
    }
  };

  // ---------- binding ----------
  function bindPage() {
    var P = $('#page');
    $$('[data-tab]', P).forEach(function (a) { a.onclick = function () { S.tab = a.dataset.tab; if (a.dataset.go) S.page = a.dataset.go; render(); pingVisible(); }; });
    $$('[data-go]', P).forEach(function (a) { a.onclick = function () { S.page = a.dataset.go; if (a.dataset.tab) S.tab = a.dataset.tab; render(); pingVisible(); }; });
    $$('[data-sort]', P).forEach(function (a) { a.onclick = function () { if (S.sort === a.dataset.sort) S.sortDir = -S.sortDir; else { S.sort = a.dataset.sort; S.sortDir = -1; } ls('sort', S.sort); render(); }; });
    $$('.trow', P).forEach(function (r) {
      r.onclick = function (e) {
        if (e.target.closest('[data-fav]')) return;
        var was = S.sel === r.dataset.key; S.sel = r.dataset.key;
        if (was && Date.now() - (r._t || 0) < 450) { join(rowBy(S.sel), 'sa'); return; }
        $$('.trow', P).forEach(function (x) { x.classList.toggle('sel', x === r); }); r._t = Date.now();
        var d = $('.detail', P), t = d && d.parentNode; if (t) { var tmp = D.createElement('div'); tmp.innerHTML = table(currentRows(), S.page === 'community'); var nd = $('.detail', tmp); if (nd) { d.replaceWith(nd); bindDetail(nd); } }
      };
    });
    $$('[data-fav]', P).forEach(function (f) { f.onclick = function (e) { e.stopPropagation(); toggleFav(f.dataset.fav); }; });
    var d = $('.detail', P); if (d) bindDetail(d);
    $$('[data-play]', P).forEach(function (b) { b.onclick = function () { play(b.dataset.play, null); }; });
    $$('[data-open-row]', P).forEach(function (c) { c.onclick = function () { var r = rowBy(c.dataset.openRow); if (r) { S.sel = r.key; S.page = r.kind === 'omp' ? 'internet' : 'internet'; S.tab = r.kind === 'omp' ? 'pc' : 'mp'; render(); pingVisible(); } }; });
    $$('[data-comm]', P).forEach(function (c) { c.onclick = function () { openComm(c.dataset.comm); }; });
    $$('[data-open-url]', P).forEach(function (a) { a.onclick = function (e) { e.preventDefault(); X.open(a.dataset.openUrl); }; });
    $$('[data-set]', P).forEach(function (a) { a.onclick = function () { S.setTab = a.dataset.set; render(); }; });
    $$('[data-sw]', P).forEach(function (b) { b.onclick = function () { var k = b.dataset.sw, on = !b.classList.contains('on'); b.classList.toggle('on', on); if (k === 'autoRefresh') { S.autoRefresh = on; } ls(k, on); }; });
    $$('[data-act]', P).forEach(function (b) { b.onclick = function () { act(b.dataset.act); }; });
    var ex = $('#exSort', P); if (ex) { ex.value = S.exploreSort; ex.onchange = function () { S.exploreSort = ex.value; render(); }; }
    var ni = $('#nickInp', P); if (ni) ni.onchange = function () { S.nick = ni.value.trim().slice(0, 23); ls('nick', S.nick); renderAvatar(); toast('Name saved'); };
    var sp = $('#saPkg', P); if (sp) sp.onchange = function () { X.set('sa_pkg', sp.value.trim() || 'com.rockstargames.gtasa'); readInfo(); toast('Game package saved'); };
    var ab = $('#abiSel', P); if (ab) { ab.value = INFO.abiMode || 'auto'; ab.onchange = function () { X.set('abi', ab.value); setTimeout(function () { readInfo(); render(); }, 400); }; }
    var rg = $('#regSel', P); if (rg) rg.onchange = function () { setRegion(rg.value | 0); };
  }
  function currentRows() { return S.page === 'community' && S.comm ? commRows(S.comm) : filtered(); }
  function bindDetail(d) {
    $$('[data-join]', d).forEach(function (b) { b.onclick = function () { join(rowBy(b.dataset.join), b.dataset.game); }; });
    $$('[data-fav]', d).forEach(function (f) { f.onclick = function (e) { e.stopPropagation(); toggleFav(f.dataset.fav); }; });
  }
  function rowBy(k) { return S.rows.filter(function (r) { return r.key === k; })[0]; }
  function toggleFav(k) { if (S.favs[k]) delete S.favs[k]; else S.favs[k] = 1; ls('favs', S.favs); render(); }
  function openComm(id) { S.comm = COMMS.filter(function (c) { return c.id === id; })[0]; S.page = 'community'; S.sel = null; render(); pingVisible(); }
  function act(a) {
    if (a === 'retry') { S.err = ''; loadServers(true); render(); }
    else if (a === 'offline-play') play('sa', null);
    else if (a === 'auth') showAuth('login');
    else if (a === 'logout') confirmDlg('Sign out', 'Sign out of your MP account on this phone?', 'Sign out', function () { S.user = null; ls('session', null); renderAvatar(); render(); });
    else if (a === 'open-sa') { if (!X.openApp(INFO.saPkg || 'com.rockstargames.gtasa')) toast('GTA SA is not installed'); }
    else if (a === 'open-vc') { if (!X.openApp('com.rockstargames.gtavc')) toast('Vice City is not installed'); }
    else if (a === 'setup') { var r = {}; try { r = JSON.parse(X.setup()); } catch (e) {} readInfo(); toast(r.ok ? 'date2 is ready' : 'date2: ' + (r.err || 'failed')); render(); }
    else if (a === 'quick-sa' || a === 'quick-vc') {
      var g = a.slice(6), best = S.rows.filter(function (r) { return !r.locked && (g === 'vc' ? r.game === 'vc' || r.kind === 'f247' : r.kind !== 'omp') && (!r.max || r.players < r.max); }).sort(function (x, y) { return (y.kind === 'f247') - (x.kind === 'f247') || y.players - x.players; })[0];
      if (best) join(best, g); else play(g, null);
    }
  }

  // ---------- play / join ----------
  function play(game, row, pass) {
    var inf = game === 'vc' ? INFO.vc : INFO.sa;
    if (!inf || !inf.installed) {
      if (game === 'vc' && INFO.vl && INFO.vl.installed) {} else {
        dialog(game === 'vc' ? 'Vice City is not installed' : 'GTA SA is not installed', game === 'vc' ? 'Install GTA Vice City (and ViceLoader for MP) first.' : 'Install GTA San Andreas with AML (Android Mod Loader) and MP first. Christe Multiplayer uses its OBB (Android/obb/com.rockstargames.gtasa) - you do not need to download it again.', [['OK', '']]);
        return;
      }
    }
    if (game === 'sa' && INFO.saObb && INFO.saObb.some(function (o) { return o.visible && !o.found; })) toast('Warning: a GTA SA OBB file looks missing');
    var spec = row ? joinSpec(row) : '';
    var L = D.createElement('div'); L.className = 'launching';
    L.innerHTML = '<div class="bars"><i></i><i></i><i></i><i></i><i></i></div><h2>Starting ' + (game === 'vc' ? 'Vice City' : 'GTA SA') + '</h2><p>' + (row ? 'MP joins ' + esc(row.name) + ' from the title screen' : 'Single player / MP menu') + '</p>';
    D.body.appendChild(L);
    X.log(new Date().toISOString() + ' play ' + game + ' ' + spec);
    setTimeout(function () {
      var r = X.play(game, ls('autoJoin') === false ? '' : spec, nick(), pass || '');
      setTimeout(function () { L.remove(); }, 1200);
      if (r !== 'ok') toast(r === 'missing' ? 'The game is not installed' : 'Could not start the game: ' + r);
    }, 900);
  }
  function join(row, game) {
    if (!row) return;
    if (!nick()) { askName(function () { join(row, game); }); return; }
    if (row.locked && row.kind !== 'mp') {
      dialog('Password', '<input class="inp" id="pwInp" type="password" placeholder="Server password">', [['Cancel', ''], ['Join', 'go']], function (i) { if (i === 1) play(game || 'sa', row, $('#pwInp').value); });
      return;
    }
    play(game || (row.game === 'vc' ? 'vc' : 'sa'), row);
  }
  function askName(done) {
    dialog('Choose a name', '<p>Play as a guest with a name, or sign in to your MP account.</p><input class="inp" id="nmInp" maxlength="23" placeholder="Name_Surname">', [['Sign in', ''], ['Play as guest', 'go']], function (i) {
      if (i === 0) { showAuth('login'); return; }
      var v = ($('#nmInp').value || '').trim().replace(/[^A-Za-z0-9_\[\].$=@()]/g, '').slice(0, 23); if (v.length < 3) { toast('3-23 letters, digits or _'); return false; }
      S.nick = v; ls('nick', v); renderAvatar(); done();
    });
  }

  // ---------- dialogs, popovers, toast ----------
  function dialog(title, body, btns, cb) {
    var W = $('#dlg'); W.innerHTML = '<div class="dlg"><h2>' + esc(title) + '</h2>' + (body.charAt(0) === '<' ? body : '<p>' + esc(body) + '</p>') + '<div class="row">' + btns.map(function (b, i) { return '<button class="btn ' + b[1] + '" data-i="' + i + '">' + esc(b[0]) + '</button>'; }).join('') + '</div></div>';
    W.classList.remove('hide');
    $$('[data-i]', W).forEach(function (b) { b.onclick = function () { var r = cb ? cb(b.dataset.i | 0) : null; if (r !== false) W.classList.add('hide'); }; });
    W.onclick = function (e) { if (e.target === W) W.classList.add('hide'); };
    var i = $('input', W); if (i) setTimeout(function () { i.focus(); }, 60);
  }
  function confirmDlg(t, b, ok, f) { dialog(t, b, [['Cancel', ''], [ok, 'red']], function (i) { if (i === 1) f(); }); }
  var toastT;
  function toast(t) { var e = $('#toast'); e.textContent = t; e.classList.remove('hide'); clearTimeout(toastT); toastT = setTimeout(function () { e.classList.add('hide'); }, 2600); }
  function pop(anchor, html, bind) {
    var P = $('#pop'); P.innerHTML = html; P.classList.remove('hide');
    var r = anchor.getBoundingClientRect(), w = P.offsetWidth;
    P.style.top = (r.bottom + 6) + 'px'; P.style.left = Math.max(6, Math.min(G.innerWidth - w - 6, r.right - w)) + 'px';
    if (bind) bind(P);
    setTimeout(function () { D.addEventListener('click', closePop, { once: true, capture: true }); }, 0);
  }
  function closePop(e) { var P = $('#pop'); if (e && P.contains(e.target)) { setTimeout(function () { D.addEventListener('click', closePop, { once: true, capture: true }); }, 0); return; } P.classList.add('hide'); }
  function setRegion(i) { S.region = i; ls('region', i); X.set('region', REGIONS[i].toLowerCase()); $('#regionLbl').textContent = REGIONS[i]; render(); }

  // ---------- notifications ----------
  function note(id, title, text) {
    if (ls('notify') === false) return;
    if (S.notes.some(function (n) { return n.id === id; })) return;
    S.notes.unshift({ id: id, t: title, s: text, at: Date.now(), seen: false }); S.notes = S.notes.slice(0, 30); ls('notes', S.notes); bellDot();
  }
  function bellDot() { $('#bellDot').classList.toggle('hide', !S.notes.some(function (n) { return !n.seen; })); }
  function notesFromServers() {
    var f = S.rows.filter(function (r) { return r.kind === 'f247'; })[0];
    if (f && f.live) note('f247-' + new Date().toISOString().slice(0, 10), 'MP Crossplay 24/7 is online', f.players + ' / ' + f.max + ' players - PC and mobile together');
  }
  function checkRelease() {
    getJson(URLS.rel).then(function (j) { if (j && j.tag_name) { S.relTag = j.tag_name; note('rel-' + j.tag_name, 'MP ' + j.tag_name + ' is out', (j.name || 'New release') + ' - update libMP in GTA SA\'s mods folder'); } }).catch(function () {});
  }

  // ---------- auth (fullscreen, Moquil style, with X back to the launcher) ----------
  var AC = null;
  function acct(h) {
    if (AC) AC.stop();
    AC = new MPAuth.Client(function (a) {
      if (a.op === 'link') st('Connected to the MP account server.', 'ok'); else if (a.op === 'down') st('Connection lost, trying another server...', 'bad'); else if (a.op === 'slow') st('Still connecting...', '');
      var f = h[a.op]; if (f) f(a);
    });
    st('Connecting to the MP account server...'); AC.start(); return AC;
  }
  function st(t, c) { var s = $('#status'); if (s) { s.textContent = t; s.className = 'status ' + (c || ''); } }
  function busy(on) { $$('#auth button[type=submit]').forEach(function (b) { b.disabled = !!on; }); }
  function closeAuth() { if (AC) { AC.stop(); AC = null; } $('#auth').classList.add('hide'); $('#auth').innerHTML = ''; }
  function shell(title, sub, inner, foot) {
    return '<div class="aSky"><i></i></div><button class="aClose" id="aX" title="Back">' + ic('x') + '</button>' +
      '<div class="aWrap"><div class="aForm"><div class="aBrand"><img src="img/logo.png" alt="">CHRISTE MULTIPLAYER</div><h1>' + title + '</h1><div class="sub">' + sub + '</div>' + inner + '<div id="status" class="status"></div><div class="aFoot">' + foot + '</div></div>' +
      '<div class="aSide"><h2>One MP account</h2><p>The same account in the game, Moquil, MP Voice and Christe Multiplayer. Create it with the MP Discord bot: a code, /gen in #verification, then your name and password.</p><button class="btn sm blue" data-ourl="' + esc(DISCORD) + '">' + ic('discord') + 'MP Discord</button><button class="btn sm" id="aGuest">Continue as guest</button></div></div>';
  }
  function bindAuth() {
    var A = $('#auth'); $('#aX').onclick = closeAuth;
    $$('[data-auth]', A).forEach(function (a) { a.onclick = function (e) { e.preventDefault(); showAuth(a.dataset.auth); }; });
    $$('[data-ourl]', A).forEach(function (a) { a.onclick = function (e) { e.preventDefault(); X.open(a.dataset.ourl); }; });
    var g = $('#aGuest'); if (g) g.onclick = function () { closeAuth(); if (!nick()) askName(function () { toast('Playing as ' + nick()); }); };
  }
  function codeFlow(c, onV) {
    var rid = '', code = '';
    function gen() { code = MPAuth.Client.code(); $('#code').textContent = '/gen ' + code; rid = c.send({ op: 'gen', code: code }); }
    gen(); $('#again').onclick = gen;
    return { gen: function (a) { if (a.rid !== rid) return; if (a.ok === '1') { st('Waiting for /gen in Discord... (the code works 10 minutes)', 'ok'); return; } if (a.err === 'dup') { gen(); return; } st(a.err || 'The code was not accepted.', 'bad'); }, verified: function (a) { if (a.vtoken) onV(a); } };
  }
  function discordBtns() { return '<div class="aRow" style="margin-top:8px"><button type="button" class="btn sm" id="again">New code</button><button type="button" class="btn sm blue" data-ourl="' + esc(DISCORD) + '">Open Discord</button></div>'; }
  function showAuth(view) {
    var A = $('#auth'); A.classList.remove('hide');
    if (view === 'register') {
      A.innerHTML = shell('Create an account', 'One MP account for the game, Moquil and Christe',
        '<div id="step1"><p class="sub" style="margin:0 0 8px">1. Join the MP Discord server.<br>2. In the #verification channel type:</p><div id="code" class="code">/gen ........</div>' + discordBtns() + '</div>' +
        '<form id="step2" class="hide" autocomplete="off"><div class="chip mob" id="dname"></div><label class="lbl">Account name</label><input id="user" class="inp" required minlength="3" maxlength="24" pattern="[A-Za-z0-9_\\[\\].$=@()]+">' +
        '<div class="aRow"><div><label class="lbl">Password</label><input id="pass" type="password" class="inp" required minlength="6"></div><div><label class="lbl">Repeat</label><input id="pass2" type="password" class="inp" required></div></div>' +
        '<div class="aRow"><div><label class="lbl">Gender</label><select id="gender" class="inp"><option>Male</option><option>Female</option><option>Other</option></select></div><div><label class="lbl">Age</label><input id="age" type="number" class="inp" min="13" max="99" value="18" required></div></div>' +
        '<button type="submit" class="btn go wide" style="margin-top:12px">Continue</button></form>', '<a href="#" data-auth="login">Already have an account? Log in</a>');
      bindAuth();
      var vt = '', flow, c = acct({ link: function () { if (!flow) flow = codeFlow(c, function (a) {
          if (a.vtoken === vt) return; vt = a.vtoken;
          if (a.has === '1') { st('Discord verified. You already have the account ' + a.user + ': log in.', 'ok'); setTimeout(function () { showAuth('login'); $('#user').value = a.user; }, 2000); return; }
          $('#step1').classList.add('hide'); $('#step2').classList.remove('hide'); $('#dname').textContent = 'Discord verified: ' + (a.dname || 'ok'); st('');
        }); },
        gen: function (a) { flow && flow.gen(a); }, verified: function (a) { flow && flow.verified(a); },
        signup: function (a) { busy(false); if (a.ok === '1') loggedIn(a.user || $('#user').value.trim()); else st(a.err || 'Sign up failed.', 'bad'); } });
      $('#step2').onsubmit = function (e) {
        e.preventDefault(); if ($('#pass').value !== $('#pass2').value) { st('The passwords are not the same.', 'bad'); return; }
        if (!this.checkValidity()) { st('Account name: 3-24 letters, digits or _ . Password: 6 or more.', 'bad'); return; }
        busy(true); st('Creating the account...');
        c.send({ op: 'signup', vtoken: vt, user: $('#user').value.trim(), pass: $('#pass').value, gender: $('#gender').value, age: String($('#age').value | 0), chal: MPAuth.Client.chal(), srv: 'christe' });
      };
      return;
    }
    if (view === 'forgot') {
      A.innerHTML = shell('Forgot password', 'Prove the account is yours with its Discord account',
        '<p class="sub" style="margin:0 0 8px">In the #verification channel of the MP Discord server type:</p><div id="code" class="code">/gen ........</div>' + discordBtns() +
        '<form id="rs" class="hide" autocomplete="off"><div class="chip mob" id="who"></div><label class="lbl">New password</label><input id="pass" type="password" class="inp" required minlength="6"><label class="lbl">Confirm password</label><input id="pass2" type="password" class="inp" required>' +
        '<button type="submit" class="btn go wide" style="margin-top:12px">Reset password</button></form>', '<a href="#" data-auth="login">Back to log in</a>');
      bindAuth();
      var R = null, f2, c2 = acct({ link: function () { if (!f2) f2 = codeFlow(c2, function (a) {
          if (a.has !== '1') { st('This Discord account has no MP account yet. Create one.', 'bad'); setTimeout(function () { showAuth('register'); }, 2000); return; }
          R = a; $('#rs').classList.remove('hide'); $('#who').textContent = 'Account ' + a.user; st('');
        }); },
        gen: function (a) { f2 && f2.gen(a); }, verified: function (a) { f2 && f2.verified(a); },
        reset: function (a) { busy(false); if (a.ok === '1') { st('Password changed. Log in with the new one.', 'ok'); setTimeout(function () { showAuth('login'); $('#user').value = R.user; }, 1500); } else st(a.err || 'The password was not changed.', 'bad'); } });
      $('#rs').onsubmit = function (e) {
        e.preventDefault(); if ($('#pass').value.length < 6 || $('#pass').value !== $('#pass2').value) { st('6 or more characters, the same in both boxes.', 'bad'); return; }
        busy(true); st('Changing the password...'); c2.send({ op: 'reset', vtoken: R.vtoken, user: R.user, pass: $('#pass').value, logout: '0' });
      };
      return;
    }
    var last = ls('lastUser') || '';
    A.innerHTML = shell('Welcome back', 'Log in with your MP account',
      '<form id="lf" autocomplete="on"><label class="lbl">MP account name <em>*</em></label><input id="user" class="inp" required maxlength="24" autocomplete="username" value="' + esc(last) + '">' +
      '<label class="lbl">Password <em>*</em></label><input id="pass" type="password" class="inp" required autocomplete="current-password">' +
      '<div style="margin-top:6px"><a href="#" data-auth="forgot" style="font-size:12px">Forgot your password?</a></div>' +
      '<button type="submit" class="btn go wide" style="margin-top:10px">Log in</button></form>', 'Need an account? <a href="#" data-auth="register">Register</a>');
    bindAuth();
    var t = 0, c3 = acct({ login: function (a) { clearTimeout(t); busy(false); if (a.ok === '1') loggedIn(a.user || $('#user').value.trim()); else st(a.err || 'Login failed.', 'bad'); } });
    $('#lf').onsubmit = function (e) {
      e.preventDefault(); var u = $('#user').value.trim(), p = $('#pass').value; if (!u || !p) { st('Type your account name and password.', 'bad'); return; }
      busy(true); st('Logging in...'); c3.send({ op: 'login', user: u, pass: p, chal: MPAuth.Client.chal(), srv: 'christe' });
      t = setTimeout(function () { busy(false); st('No answer from the account server. Try again.', 'bad'); }, 20000);
    };
  }
  function loggedIn(user) {
    S.user = { user: user, at: Date.now() }; ls('session', S.user); ls('lastUser', user); S.nick = user; ls('nick', user);
    closeAuth(); renderAvatar(); render(); toast('Signed in as ' + user);
  }
  function renderAvatar() {
    var n = nick(), im = $('#avatarImg');
    $('#avatarName').textContent = S.user ? S.user.user : (n ? n : 'Guest');
    if (S.user || n) { im.className = 'av'; im.style.cssText = logo(n); im.textContent = initials(n); } else { im.className = 'av guest'; im.style.cssText = ''; im.innerHTML = ic('user'); }
  }

  // ---------- sidebar + top bar ----------
  function renderSide() {
    $('#commList').innerHTML = COMMS.map(function (c) {
      var p = S.loaded ? commPlayers(c) : 0;
      return '<div class="comm" data-id="' + c.id + '"><div class="ci" style="' + logo(c.name) + '">' + esc(c.tag) + '</div><div class="cn"><b>' + esc(c.name) + '</b><span>' + (S.loaded ? p + ' online' : '...') + '</span></div>' + (c.badge && p ? '<span class="badge live">' + p + '</span>' : '') + '</div>';
    }).join('');
    $$('.comm').forEach(function (c) { c.onclick = function () { openComm(c.dataset.id); }; });
  }
  function bindShell() {
    $$('#nav a, .sideFoot a').forEach(function (a) { a.onclick = function () { S.page = a.dataset.page; S.comm = null; render(); if (S.page === 'internet' || S.page === 'favorites') pingVisible(); }; });
    var qt; $('#q').oninput = function () {
      clearTimeout(qt); var v = this.value; qt = setTimeout(function () {
        S.q = v; if (/^[\w.-]+:\d{2,5}$/.test(v.trim())) { addressRow(v.trim()); return; }
        if (S.q && (S.page === 'home' || S.page === 'about' || S.page === 'settings')) S.page = 'internet';
        render(); pingVisible(); $('#q').focus();
      }, 220);
    };
    $('#refreshBtn').onclick = function () { readInfo(); loadServers(true); };
    $('#regionBtn').onclick = function () {
      pop(this, '<div class="ph2">REGION</div>' + REGIONS.map(function (r, i) { return '<div class="pi' + (S.region === i ? ' on' : '') + '" data-r="' + i + '">' + ic('pin') + r + (i === 0 ? ' (all regions)' : '') + '</div>'; }).join(''), function (P) {
        $$('[data-r]', P).forEach(function (x) { x.onclick = function () { setRegion(x.dataset.r | 0); P.classList.add('hide'); }; });
      });
    };
    $('#bellBtn').onclick = function () {
      var html = '<div class="ph2">NOTIFICATIONS</div>' + (S.notes.length ? S.notes.map(function (n) { return '<div class="nt' + (n.seen ? '' : ' new') + '"><b>' + esc(n.t) + '</b><span>' + esc(n.s) + '</span></div>'; }).join('') : '<div class="nt"><span>No notifications yet.</span></div>');
      pop(this, html); S.notes.forEach(function (n) { n.seen = true; }); ls('notes', S.notes); bellDot();
    };
    $('#gearBtn').onclick = function () {
      pop(this, '<div class="ph2">QUICK SETTINGS</div>' +
        '<div class="pi" data-q="name">' + ic('user') + 'Name: ' + esc(nick() || 'not set') + '</div>' +
        '<div class="pi" data-q="region">' + ic('pin') + 'Region: ' + REGIONS[S.region] + '</div>' +
        '<div class="pi" data-q="abi">' + ic('chip') + 'MP build: ' + esc(INFO.abi || '') + '</div>' +
        '<div class="pi' + (S.autoRefresh ? ' on' : '') + '" data-q="auto">' + ic('refresh') + 'Auto refresh</div>' +
        '<div class="pi' + (ls('autoJoin') !== false ? ' on' : '') + '" data-q="join">' + ic('play') + 'Join from the title</div>' +
        '<div class="pi" data-q="all">' + ic('gear') + 'All settings</div>', function (P) {
        $$('[data-q]', P).forEach(function (x) { x.onclick = function () {
          var k = x.dataset.q; P.classList.add('hide');
          if (k === 'name') { if (S.user) { S.page = 'settings'; S.setTab = 'account'; render(); } else askName(function () { toast('Name saved'); }); }
          else if (k === 'region') { S.page = 'settings'; S.setTab = 'network'; render(); }
          else if (k === 'abi') { S.page = 'settings'; S.setTab = 'game'; render(); }
          else if (k === 'auto') { S.autoRefresh = !S.autoRefresh; ls('autoRefresh', S.autoRefresh); toast('Auto refresh ' + (S.autoRefresh ? 'on' : 'off')); }
          else if (k === 'join') { var on = ls('autoJoin') === false; ls('autoJoin', on); toast('Join from the title ' + (on ? 'on' : 'off')); }
          else { S.page = 'settings'; render(); }
        }; });
      });
    };
    $('#avatarBtn').onclick = function () {
      if (!S.user) { showAuth('login'); return; }
      pop(this, '<div class="ph2">' + esc(S.user.user) + '</div><div class="pi" data-p="set">' + ic('gear') + 'Account settings</div><div class="pi" data-p="moq">' + ic('users') + 'Open Moquil</div><div class="pi" data-p="out">' + ic('x') + 'Sign out</div>', function (P) {
        $$('[data-p]', P).forEach(function (x) { x.onclick = function () { P.classList.add('hide'); if (x.dataset.p === 'set') { S.page = 'settings'; S.setTab = 'account'; render(); } else if (x.dataset.p === 'moq') { if (!X.openApp('com.thirtyfox.moquil')) X.open('https://mpsamp-open.github.io/moquils-web'); } else act('logout'); }; });
      });
    };
  }
  function addressRow(a) {
    var hp = a.split(':'), r = { kind: 'omp', name: a, host: hp[0], port: hp[1] | 0, players: 0, max: 0, region: 0, mode: 'Direct', lang: '', locked: false, ping: -1, game: 'sa', desc: 'typed address' };
    r.key = rowKey(r); if (!rowBy(r.key)) S.rows.unshift(r); S.sel = r.key; S.page = 'internet'; S.tab = 'all'; render(); pingVisible();
  }

  // ---------- network state ----------
  G.CX_net = function (on) {
    S.online = on; var b = $('#netBar');
    b.className = 'netBar ' + (on ? 'on' : 'off'); b.innerHTML = on ? ic('globe') + 'Back online' : ic('wifi-off') + 'Lost connection. Try turning on Wi-Fi or mobile data.';
    if (on) { setTimeout(function () { b.classList.add('hide'); }, 2500); if (S.err) { S.err = ''; loadServers(true); } }
    else if (!S.rows.length) { S.err = 'offline'; render(); }
  };
  if (!NATIVE) { G.addEventListener('online', function () { G.CX_net(true); }); G.addEventListener('offline', function () { G.CX_net(false); }); }
  G.CX_resume = function () { readInfo(); if (S.page === 'home' || S.page === 'settings') render(); };
  G.CX_back = function () {
    if (!$('#auth').classList.contains('hide')) { closeAuth(); return true; }
    if (!$('#dlg').classList.contains('hide')) { $('#dlg').classList.add('hide'); return true; }
    if (!$('#pop').classList.contains('hide')) { $('#pop').classList.add('hide'); return true; }
    if (S.page !== 'home') { S.page = 'home'; S.comm = null; render(); return true; }
    X.exit(); return false;
  };

  // ---------- boot (animation or date2/resources/startup.mp4, then the hand-off) ----------
  function boot() {
    readInfo(); try { X.setup(); } catch (e) {}
    var hp = /[#&]p=(\w+)/.exec(location.hash); if (hp && PAGES[hp[1]]) { S.page = hp[1]; if (hp[1] === 'community') S.comm = COMMS[0]; }
    if (/auth/.test(location.hash)) setTimeout(function () { showAuth('login'); }, 10);
    $('#regionLbl').textContent = REGIONS[S.region] || 'Auto';
    renderAvatar(); renderSide(); bindShell(); render();
    loadServers(); checkRelease();
    var B = $('#boot'), steps = ['Detecting the device (' + (INFO.deviceAbi || '') + ')', 'Checking GTA SA ' + ((INFO.sa && INFO.sa.version) || '') + ' and the OBB', 'Preparing date2', 'Loading servers'], si = 0;
    var stepT = setInterval(function () { $('#bootStep').textContent = steps[si++ % steps.length]; }, 620);
    var done = false;
    function handoff() { if (done) return; done = true; clearInterval(stepT); $('#app').classList.remove('hide'); B.classList.add('out'); setTimeout(function () { B.remove(); }, 650); }
    $('#bootSkip').onclick = handoff;
    var vid = X.startupVideo && X.startupVideo();
    if (vid) {
      var V = $('#bootVid'); V.classList.remove('hide'); $('#bootArt').classList.add('hide'); V.src = vid;
      V.onended = handoff; V.onerror = handoff; V.play().catch(handoff); setTimeout(handoff, 20000);
      return;
    }
    if (ls('bootAnim') === false) { handoff(); return; }
    var t0 = Date.now();
    (function wait() { if (Date.now() - t0 > 2600 && (S.loaded || S.err || Date.now() - t0 > 4500)) handoff(); else setTimeout(wait, 150); })();
  }
  setInterval(function () { if (S.autoRefresh && S.online && !D.hidden && (S.page === 'internet' || S.page === 'home' || S.page === 'favorites' || S.page === 'community')) loadServers(); }, 60000);
  if (D.readyState === 'loading') D.addEventListener('DOMContentLoaded', boot); else boot();
})(window, document);
