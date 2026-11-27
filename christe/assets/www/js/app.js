/* AML Multiplayer 1.3.1 (was Christe Multiplayer) - launcher UI (landscape). Talks to Android through window.Christe (MainActivity.Bridge).
   The layout is the SA-MP 0.3.7 browser (File/View/Servers/Tools/Help, tool bar + Name:, HostName/Players/Ping/Mode/
   Language, Player/Score + Rule/Value, Favorites/Internet/Hosted tabs, "Servers: N players" line) in the MP launcher skin.
   Lists: Internet = MP lobby (live-status/status.json) + open.mp (api.open.mp/servers), Featured = MP's own servers,
   Favorites, Lan = MP's own LAN query (PKT_QUERY 14 -> PKT_INFO 15, MainActivity.lanScan), Hosted = open.mp PC servers.
   Login: the MP account bot (js/mpauth.js, the same accounts as the game and Moquil). */
(function (G, D) {
  'use strict';
  var $ = function (s, r) { return (r || D).querySelector(s); }, $$ = function (s, r) { return Array.prototype.slice.call((r || D).querySelectorAll(s)); };
  var VERSION = '1.4.0', MPVER = '5.02.79';
  var URLS = {
    lobby: 'https://raw.githubusercontent.com/JebetrexxMC/wagu/live-status/status.json',
    f247: 'https://raw.githubusercontent.com/JebetrexxMC/wagu/mp247-data/omp247.json',
    omp: 'https://api.open.mp/servers',
    ompOne: 'https://api.open.mp/servers/',
    rel: 'https://api.github.com/repos/JebetrexxMC/wagu/releases/latest',
    coop: 'https://raw.githubusercontent.com/JebetrexxMC/wagu/main/coopfusion/servers.json'
  };
  var DISCORD = (G.MPAuth && MPAuth.DISCORD) || 'https://discord.gg/QSdtut3SWV', YOUTUBE = 'https://youtube.com/@thirtyfoxmc';
  var REGIONS = ['Auto', 'Asia', 'Europe', 'NA', 'Oceania'];
  var TIPS = ['Tap a server twice to join it', 'Featured servers are online 24/7', 'Hold the chat bubble to talk (voice chat)', 'Settings > Servers keeps your favourite servers', '/help lists the commands of the server you are on'];
  var CC = 'af,ax,al,dz,as,ad,ao,ai,aq,ag,ar,am,aw,au,at,az,bs,bh,bd,bb,by,be,bz,bj,bm,bt,bo,bq,ba,bw,bv,br,io,bn,bg,bf,bi,cv,kh,cm,ca,ky,cf,td,cl,cn,cx,cc,co,km,ck,cr,hr,cu,cw,cy,cz,ci,cd,dk,dj,dm,do,ec,eg,sv,gq,er,ee,sz,et,fk,fo,fm,fj,fi,fr,gf,pf,tf,ga,gm,ge,de,gh,gi,gr,gl,gd,gp,gu,gt,gg,gn,gw,gy,ht,hm,va,hn,hk,hu,is,in,id,ir,iq,ie,im,il,it,jm,jp,je,jo,kz,ke,ki,kw,kg,la,lv,lb,ls,lr,ly,li,lt,lu,mo,mg,mw,my,mv,ml,mt,mh,mq,mr,mu,yt,mx,md,mc,mn,me,ms,ma,mz,mm,na,nr,np,nl,nc,nz,ni,ne,ng,nu,nf,kp,mk,mp,no,om,pk,pw,pa,pg,py,pe,ph,pn,pl,pt,pr,qa,cg,ro,ru,rw,re,bl,sh,kn,lc,mf,pm,vc,ws,sm,st,sa,sn,rs,sc,sl,sg,sx,sk,si,sb,so,za,gs,kr,ss,es,lk,ps,sd,sr,sj,se,ch,sy,tw,tj,tz,th,tl,tg,tk,to,tt,tn,tm,tc,tv,tr,ug,ua,ae,gb,um,us,uy,uz,vu,ve,vn,vg,vi,wf,eh,ye,zm,zw,xk,gb-eng,gb-sct,gb-wls,eu,un'.split(',');
  G.MPAUTH_APP = 'christe';
  // 1.1: the MP Crossplay 24/7 [PC + Mobile] server is not listed in Internet (and not in the MP lobby rows)
  var HIDE_INTERNET = /crossplay ?24 ?\/ ?7|pc ?\+ ?mobile/i;

  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function ls(k, v) { try { if (v === undefined) { var x = G.localStorage.getItem('cx_' + k); return x == null ? null : JSON.parse(x); } if (v === null) G.localStorage.removeItem('cx_' + k); else G.localStorage.setItem('cx_' + k, JSON.stringify(v)); } catch (e) { return null; } }
  function ic(n, cls) { return '<svg' + (cls ? ' class="' + cls + '"' : '') + '><use href="#i-' + n + '"/></svg>'; }
  function hue(s) { var h = 0; s = String(s || ''); for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0; return Math.abs(h) % 360; }
  function initials(s) { var w = String(s || '?').replace(/[^A-Za-z0-9 ]/g, ' ').trim().split(/\s+/); return ((w[0] || '?')[0] + (w[1] ? w[1][0] : (w[0] || '')[1] || '')).toUpperCase(); }
  function logo(seed) { var h = hue(seed); return 'background:linear-gradient(135deg,hsl(' + h + ',45%,42%),hsl(' + ((h + 40) % 360) + ',45%,30%))'; }
  function gb(b) { return (b / 1073741824).toFixed(2) + ' GB'; }

  // ---------- the Android bridge (and a browser fallback for the preview) ----------
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
    play: function () { return 'ok'; }, openApp: function () { return false; }, open: function (u) { G.open(u, '_blank'); }, toast: function (t) { toast(t); }, exit: function () {}, log: function () {},
    obb: function () { return JSON.stringify({ main: { own: 1970000000, game: 1970000000, ownPath: '/storage/emulated/0/Android/obb/com.christe.mp/main.8.com.christe.mp.obb' }, patch: { own: 0, game: 625000000, ownPath: '/storage/emulated/0/Android/obb/com.christe.mp/patch.8.com.christe.mp.obb' }, ready: true, canAsk: true, gameAdded: true, gameImported: true, gameSize: 1.6e9, gameNeed: 'GTA SA 2.10 AML (64-bit, arm64-v8a) - a 32-bit GTA SA 2.00 works with AML Multiplayer 32-bit', edition: '64', has32: true, gameAbi: '32-bit only (armeabi-v7a)', editionNeed: G.location.hash.indexOf('ed32') >= 0 ? '32' : '', editionText: 'This GTA SA is 32-bit only (no arm64-v8a, GTA SA 2.00). Switch to AML Multiplayer 32-bit to play it. Your settings, game files and the added GTA SA stay.' }); },
    obbCopy: function (id) { var d = 0; var t = setInterval(function () { d += 0.2; G.CX_prog(id, d >= 1 ? 'done' : 'copy', d * 2.6e9, 2.6e9, d >= 1 ? 'Game files ready' : 'main'); if (d >= 1) clearInterval(t); }, 400); },
    obbImport: function (id) { G.CX_prog(id, 'cancel', 0, 0, ''); }, obbAccess: function () {}, gameAdd: function (id) { G.CX_prog(id, 'cancel', 0, 0, ''); }, gameRemove: function () {},
    coopInfo: function (id, h, p) { setTimeout(function () { G.CX_coop(id, JSON.stringify({ ok: true, name: 'CoopFusion LAN', players: 2, max: 8, ver: '1.1', ms: 40 })); }, 300); },
    coopLan: function (id) { setTimeout(function () { G.CX_coopLan(id, JSON.stringify([{ host: '192.168.1.7', port: 6767, name: "Juan's co-op", players: 1, max: 64, ver: '1.1' }])); }, 700); }, coopReady: function () { return ''; },
    mpFiles: function (id) { G.CX_prog(id, 'done', 1, 1, 'MP files ready'); },
    engine: function () { return JSON.stringify({ mode: 'christe', problem: '', proc64: true, mpFiles: true }); }, crashSeen: function () {},
    lan: function (id) { setTimeout(function () { G.CX_lan(id, JSON.stringify([{ host: '192.168.1.5', port: 1922, ping: 4, players: 2, max: 50, region: 1, name: 'MP LAN Server', locked: false, icon: 0, lang: 'Filipino / English', cc: 'ph', mode: 'Freeroam' }])); }, 900); }
  };
  G.CX_got = function (id, code, body) { var f = CB[id]; delete CB[id]; if (f) f(code, body); };
  G.CX_ping = function (id, ms) { var f = PCB[id]; delete PCB[id]; if (f) f(ms); };
  var PROG = {};
  G.CX_prog = function (id, state, done, total, msg) { var f = PROG[id]; if (f) f(state, done, total, msg); if (state === 'done' || state === 'error' || state === 'cancel') delete PROG[id]; };
  var LANCB = {}, COOPCB = {};
  G.CX_coop = function (id, o) { var f = COOPCB[id]; delete COOPCB[id]; if (typeof o === 'string') { try { o = JSON.parse(o); } catch (e) { o = {}; } } if (f) f(o || {}); };
  G.CX_coopLan = function (id, a) { var f = COOPCB[id]; delete COOPCB[id]; if (typeof a === 'string') { try { a = JSON.parse(a); } catch (e) { a = []; } } if (f) f(a || []); };
  G.CX_lan = function (id, json) { var f = LANCB[id]; delete LANCB[id]; var a = []; try { a = JSON.parse(json); } catch (e) {} if (f) f(a); };
  function get(url, cb) { var id = 'g' + (++cbn); CB[id] = cb; X.get(id, url); }
  function getJson(url) { return new Promise(function (ok, no) { get(url, function (c, b) { if (c >= 200 && c < 300) { try { ok(JSON.parse(b)); } catch (e) { no(new Error('bad JSON')); } } else no(new Error('HTTP ' + c)); }); }); }
  function ping(host, port, cb) { var id = 'p' + (++cbn); PCB[id] = cb; X.ping(id, host, port | 0); }
  function call(fn, id, a2) { try { if (X[fn]) { if (a2 === undefined) X[fn](id); else X[fn](id, a2); return true; } } catch (e) {} return false; }
  function json(fn) { try { return X[fn] ? JSON.parse(X[fn]()) || {} : {}; } catch (e) { return {}; } }
  var INFO = {}, ENG = {}, OBB = {};
  function readInfo() { try { INFO = JSON.parse(X.info()) || {}; } catch (e) { INFO = {}; } ENG = json('engine'); OBB = json('obb'); return INFO; }

  // ---------- state ----------
  var S = {
    page: 'internet', comm: null, q: '', sort: ls('sort') || 'players', sortDir: -1,
    region: ls('region') || 0, rows: [], lan: [], lanBusy: false, lanAt: 0, loaded: false, loading: false, err: '', online: true, sel: null,
    favs: ls('favs') || {}, notes: ls('notes') || [], user: ls('session'), nick: ls('nick') || '', setTab: 'account',
    exploreSort: 'rec', autoRefresh: ls('autoRefresh') !== false, relTag: '', rules: {}, job: null,
    coop: [], coopLan: [], coopBusy: false, coopAt: 0, coopManual: ls('coopManual') || [], soc: null, socCh: 'general', socOn: false
  };
  function nick() { return (S.user && S.user.user) || S.nick || ''; }
  var LISTS = { internet: 1, featured: 1, favorites: 1, lan: 1, hosted: 1, community: 1, coop: 1 };

  // ---------- communities ----------
  var COMMS = [
    { id: 'mp', name: 'MP Official', icon: 'main_df', bg: 'default', desc: 'The MP lobby: MP LAN Server and every player\'s own server, GTA SA on Android.', match: function (r) { return r.kind === 'mp' && !SPECIAL.test(r.name); } },
    { id: 'bcw', name: 'Brief Case War', icon: 'bcw', bg: 'bcw', desc: 'Grab the briefcase and hold it. Team mode on the MP lobby.', match: function (r) { return /brief ?case/i.test(r.name); } },
    { id: 'mw3', name: 'Call Of Duty: MW3', icon: 'mw3', bg: 'mw3', desc: 'Modern Warfare 3 style deathmatch with loadouts and killstreaks.', match: function (r) { return /modern warfare|mw3/i.test(r.name); } },
    { id: 'mvf', name: 'Mega Vil Futuristic', icon: 'rp', bg: 'rp', desc: 'Roleplay in a futuristic city with jobs, houses and gangs.', match: function (r) { return /mega vil/i.test(r.name); } },
    { id: 'pdz', name: 'Project Day Z Legacy', icon: 'dz', bg: 'dz', desc: 'Survival: loot, bases, zombies and the Day Z map.', match: function (r) { return /day ?z/i.test(r.name); } },
    { id: 'lg', name: 'Rocket League', icon: 'main_df', bg: 'lg', desc: 'Car football matches between teams.', match: function (r) { return /league/i.test(r.name); } },
    { id: 'vc', name: 'Vice City', icon: 'main_df', bg: '7', desc: 'GTA Vice City with ViceLoader: the VC LAN server and crossplay with San Andreas players.', match: function (r) { return r.game === 'vc'; }, badge: 'VC' },
    { id: 'omp', name: 'open.mp Community', icon: 'main_df', bg: 'default', desc: 'Every open.mp / SA-MP PC server. MP joins them through crossplay.', match: function (r) { return r.kind === 'omp'; } }
  ];
  var SPECIAL = /brief ?case|modern warfare|mw3|mega vil|day ?z|league/i;
  function commRows(c) { return S.rows.filter(c.match); }
  function commPlayers(c) { return commRows(c).reduce(function (n, r) { return n + (r.players | 0); }, 0); }

  // ---------- server rows ----------
  function rowKey(r) { return r.kind + '|' + (r.host || r.name).toLowerCase() + '|' + r.port; }
  function isVc(name, mode) { return /vice ?city|\bvc\b|vclan/i.test((name || '') + ' ' + (mode || '')); }
  function joinSpec(r) { if (r.kind === 'coop') return 'coop|' + r.host + '|' + r.port + '|' + r.name; return r.kind === 'mp' || r.kind === 'lan' ? 'mp|' + r.name + '|' + r.port + '|' + (r.host || '') : 'omp|' + r.host + '|' + r.port + '|' + r.name; }
  // 1.2: the row / pane background is the MP mod's server picture (bg_server.jar), not a logo icon
  var FEAT = [[/brief ?case/i, 'bcw', 'Brief Case War'], [/modern warfare|mw3/i, 'mw3', 'MW3'], [/mega vil/i, 'rp', 'Roleplay'], [/day ?z/i, 'dz', 'Day Z'], [/league/i, 'lg', 'Rocket Soccer']];
  function featOf(name) { for (var i = 0; i < FEAT.length; i++) if (FEAT[i][0].test(name || '')) return FEAT[i]; return null; }
  function bgOf(r) {
    var f = featOf(r.name); if (f) return 'img/bg/bg_' + f[1] + '.jpg';
    if (r.kind === 'omp') { var h = 5381, k = (r.host || '') + ':' + r.port; for (var i = 0; i < k.length; i++) h = (h * 33 + k.charCodeAt(i)) >>> 0; return 'img/bg/bg_' + (1 + h % 40) + '.jpg'; }
    if (r.game === 'vc') return 'img/bg/bg_7.jpg';
    if (r.kind === 'coop') return 'img/bg/bg_default.jpg';
    return 'img/bg/bg_default.jpg';
  }
  var LANGCC = { ru: 'ru', rus: 'ru', russian: 'ru', br: 'br', pt: 'br', brasil: 'br', portuguese: 'br', ro: 'ro', romana: 'ro', romanian: 'ro', en: 'gb', eng: 'gb', english: 'gb', es: 'es', spanish: 'es', espanol: 'es', ph: 'ph', filipino: 'ph', tagalog: 'ph',
    id: 'id', indonesia: 'id', indonesian: 'id', tr: 'tr', turkish: 'tr', pl: 'pl', polish: 'pl', polski: 'pl', vn: 'vn', vietnamese: 'vn', de: 'de', german: 'de', fr: 'fr', french: 'fr', ua: 'ua', ukrainian: 'ua', it: 'it', italian: 'it',
    hu: 'hu', hungarian: 'hu', bg: 'bg', bulgarian: 'bg', cz: 'cz', czech: 'cz', sk: 'sk', rs: 'rs', serbian: 'rs', hr: 'hr', ar: 'sa', arabic: 'sa', ge: 'ge', lt: 'lt', lv: 'lv', az: 'az', kz: 'kz', th: 'th', thai: 'th', my: 'my', malay: 'my', mx: 'mx', us: 'us', uk: 'gb', il: 'il', gr: 'gr', md: 'md', mk: 'mk', ba: 'ba', al: 'al', by: 'by', mn: 'mn', cn: 'cn', jp: 'jp', kr: 'kr', in: 'in', hindi: 'in', pk: 'pk', ir: 'ir', persian: 'ir', nl: 'nl', dutch: 'nl', se: 'se', no: 'no', dk: 'dk', fi: 'fi', ee: 'ee' };
  function ccFromLang(l) { var w = String(l || '').toLowerCase().split(/[^a-z]+/); for (var i = 0; i < w.length; i++) if (LANGCC[w[i]]) return LANGCC[w[i]]; return ''; }
  function flag(cc) {
    if (cc === 'global') return '<span class="flag glob" title="Global">' + ic('globe') + '</span>';
    var i = cc ? CC.indexOf(cc.toLowerCase()) : -1; if (i < 0) return '<span class="flag none" title="Unknown">-</span>'; var slot = i + 1;
    return '<span class="flag" title="' + esc(cc || '') + '" style="background-position:' + (-(slot % 16) * 20) + 'px ' + (-Math.floor(slot / 16) * 15) + 'px"></span>';
  }
  function loadServers() {
    if (S.loading) return;
    S.online = X.online(); if (!S.online) { S.err = 'offline'; render(); return; }
    S.loading = true; S.err = ''; $('#refreshBtn').classList.add('spin'); if (!S.loaded) render();
    var rows = [];
    var a = getJson(URLS.lobby).then(function (j) {
      var seen = {};
      (j.servers || []).forEach(function (s) {
        if (HIDE_INTERNET.test(s.name || '')) return;
        var k = (s.name || '').toLowerCase() + '|' + s.port;
        if (seen[k]) { seen[k].players = Math.max(seen[k].players, s.players | 0); return; }
        var r = { kind: 'mp', name: s.name || 'MP server', host: '', port: s.port | 0, players: s.players | 0, max: s.max | 0, region: (s.region | 0) + 1, mode: 'Freeroam', lang: 'Any', cc: s.cc || '', locked: !!s.locked, ping: -2, game: isVc(s.name) ? 'vc' : 'sa', desc: '' };
        var ft = featOf(r.name); r.feat = !!ft; r.mode = ft ? ft[2] : r.game === 'vc' ? 'Vice City' : /mp lan server/i.test(r.name) ? 'Bounty Hunter' : 'Freeroam'; if (!r.cc) r.cc = 'global';
        seen[k] = r; rows.push(r);
      });
      S.lobbyMeta = { updated: j.updated, players: j.players };
    });
    var c = getJson(URLS.omp).then(function (j) {
      (j || []).slice(0, 700).forEach(function (s) {
        var hp = String(s.ip || '').split(':'); if (!hp[0]) return;
        if (HIDE_INTERNET.test(s.hn || '')) return;
        rows.push({ kind: 'omp', name: s.hn || s.ip, host: hp[0], port: (hp[1] | 0) || 7777, players: s.pc | 0, max: s.pm | 0, region: 0, mode: s.gm || '', lang: s.la || '', cc: ccFromLang(s.la), locked: !!s.pa, ping: -1, game: isVc(s.hn, s.gm) ? 'vc' : 'sa', ver: s.vn || '', omp: !!s.omp });
      });
    });
    Promise.all([a.catch(function (e) { return e; }), c.catch(function (e) { return e; })]).then(function (res) {
      S.loading = false; $('#refreshBtn').classList.remove('spin');
      var fails = res.filter(function (x) { return x instanceof Error; }).length;
      if (!rows.length) { S.online = X.online(); S.err = S.online ? 'down' : 'offline'; render(); return; }
      var old = {}; S.rows.forEach(function (r) { old[rowKey(r)] = r.ping; });
      rows.forEach(function (r) { r.key = rowKey(r); if (old[r.key] >= 0) r.ping = old[r.key]; });
      S.rows = rows; S.loaded = true; S.err = fails === 2 ? 'down' : '';
      render(); pingVisible();
    });
  }
  function scanLan() {
    if (S.lanBusy) return; S.lanBusy = true; render();
    var id = 'l' + (++cbn);
    LANCB[id] = function (list) {
      S.lanBusy = false; S.lanAt = Date.now();
      S.lan = list.filter(function (s) { return !HIDE_INTERNET.test(s.name || ''); }).map(function (s) {
        var r = { kind: 'lan', name: s.name || 'MP LAN Server', host: s.host, port: s.port | 0, players: s.players | 0, max: s.max | 0, region: s.region | 0, mode: s.mode || 'Freeroam', lang: s.lang || '', cc: s.cc || 'global', locked: !!s.locked, ping: s.ping | 0, game: isVc(s.name, s.mode) ? 'vc' : 'sa', desc: s.desc || '', tags: s.tags || '' };
        r.key = rowKey(r); return r;
      });
      render();
    };
    if (!call('lan', id)) { S.lanBusy = false; render(); }
  }
  function allRows() { return S.rows.concat(S.lan, S.coop); }

  // ---------- Co-op: CoopFusion32 servers. libMP joins them itself (crossplay, 1.3.1), 32-bit and 64-bit ----------
  function coopRow(o, src) {
    var r = { kind: 'coop', src: src, name: o.name || (o.host + ':' + o.port), host: o.host, port: (o.port | 0) || 6767, players: o.players | 0, max: o.max | 0, mode: 'Co-op (story)', lang: o.lang || '-', cc: o.cc || 'global', game: 'sa', ping: -1, ver: o.ver || '', locked: false };
    r.key = rowKey(r); return r;
  }
  function coopMerge() {
    var seen = {}, out = [];
    S.coopLan.concat(S.coopList || [], S.coopManual.map(function (m) { return coopRow(m, 'manual'); })).forEach(function (r) { if (!seen[r.key]) { seen[r.key] = 1; out.push(r); } });
    var old = {}; S.coop.forEach(function (r) { old[r.key] = r; });
    out.forEach(function (r) { var o = old[r.key]; if (o && o.ping >= 0 && r.ping < 0) { r.ping = o.ping; r.players = o.players; r.max = o.max; r.ver = o.ver; if (r.src !== 'lan') r.name = o.name; } });
    S.coop = out;
  }
  function coopInfo(r) {
    if (r._q) return; r._q = 1; var id = 'c' + (++cbn);
    COOPCB[id] = function (o) {
      r._q = 0; r.ping = o.ok ? (o.ms | 0) : -4; if (o.ok) { if (o.name && r.src !== 'manual') r.name = o.name; r.players = o.players | 0; r.max = o.max | 0; r.ver = o.ver || r.ver; }
      if (S.page === 'coop') render();
    };
    try { X.coopInfo(id, r.host, r.port); } catch (e) { r._q = 0; }
  }
  function loadCoop() {
    S.coopBusy = true; S.coopAt = Date.now(); if (S.page === 'coop') render();
    var n = 2, fin = function () { if (--n) return; S.coopBusy = false; coopMerge(); S.coop.forEach(coopInfo); if (S.page === 'coop') render(); };
    getJson(URLS.coop + '?t=' + Date.now()).then(function (j) { S.coopList = ((j && j.servers) || []).filter(function (o) { return o.host; }).map(function (o) { return coopRow(o, 'list'); }); fin(); }).catch(function () { S.coopList = S.coopList || []; fin(); });
    var id = 'cl' + (++cbn); COOPCB[id] = function (a) { S.coopLan = a.map(function (o) { var r = coopRow(o, 'lan'); r.ping = 1; return r; }); fin(); };
    try { X.coopLan(id); } catch (e) { S.coopLan = []; fin(); }
  }
  function coopAdd() {
    dialog('Add a co-op server', '<p>The address of a CoopFusion32 server (the Node.js server or a friend\'s LAN host).</p><input class="inp" id="coHost" placeholder="IP or host name"> <input class="inp" id="coPort" style="width:90px" value="6767">', [['Cancel', ''], ['Add', 'go']], function (i) {
      if (i !== 1) return; var h = ($('#coHost').value || '').trim(), p = ($('#coPort').value | 0) || 6767; if (!/^[A-Za-z0-9.\-]{3,}$/.test(h)) { toast('Type the server address'); return false; }
      S.coopManual = S.coopManual.filter(function (m) { return !(m.host === h && m.port === p); }).concat([{ host: h, port: p }]); ls('coopManual', S.coopManual); coopMerge(); S.coop.forEach(coopInfo); render();
    });
  }
  function pingVisible() {
    filtered().filter(function (r) { return r.kind === 'omp' && r.ping === -1; }).slice(0, 30).forEach(function (r) {
      r.ping = -3;
      ping(r.host, r.port, function (ms) { r.ping = ms >= 0 ? ms : -4; var el = D.querySelector('[data-key="' + cssq(r.key) + '"] .pg'); if (el) { el.className = 'pg ' + pcls(r.ping); el.textContent = ptxt(r); } });
    });
  }
  function cssq(s) { return String(s).replace(/["\\]/g, '\\$&'); }
  function pcls(p) { return p < 0 ? 'p-none' : p < 80 ? 'p-good' : p < 160 ? 'p-mid' : 'p-bad'; }
  function ptxt(r) { if (r.kind === 'coop' && r.src === 'lan' && r.ping < 0) return 'LAN'; if (r.kind === 'mp') return r.region ? REGIONS[r.region] || '-' : '-'; return r.ping >= 0 ? String(r.ping) : r.ping === -3 ? '...' : '-'; }

  function listRows(page) {
    var rows = S.rows;
    if (page === 'internet') rows = rows.filter(function (r) { return r.kind === 'mp' && !r.feat; });   // 1.2: MP servers only (MP LAN Server, VC, players' own); open.mp is Hosted
    else if (page === 'featured') rows = rows.filter(function (r) { return r.kind === 'mp' && r.feat; });   // the MP 24/7 featured servers
    else if (page === 'hosted') rows = rows.filter(function (r) { return r.kind === 'omp'; });
    else if (page === 'lan') rows = S.lan;
    else if (page === 'coop') rows = S.coop;
    else if (page === 'favorites') rows = allRows().filter(function (r) { return S.favs[r.key]; });
    else if (page === 'community' && S.comm) rows = rows.filter(S.comm.match);
    return rows;
  }
  function filtered() {
    var q = S.q.trim().toLowerCase(), rows = listRows(S.page);
    if (S.region && S.page !== 'lan' && S.page !== 'coop') rows = rows.filter(function (r) { return r.kind !== 'mp' || r.region === S.region || !r.region; });
    if (q) rows = rows.filter(function (r) { return (r.name + ' ' + r.mode + ' ' + r.lang + ' ' + (r.host || '')).toLowerCase().indexOf(q) >= 0; });
    var k = S.sort, d = S.sortDir;
    return rows.slice().sort(function (a, b) {
      var fa = a.kind === 'mp' ? 1 : 0, fb = b.kind === 'mp' ? 1 : 0; if (S.page === 'internet' && fa !== fb && k === 'players') return fb - fa;
      if (k === 'name') return d * (a.name.localeCompare(b.name) * -1);
      if (k === 'ping') { var pa = a.ping >= 0 ? a.ping : 9999, pb = b.ping >= 0 ? b.ping : 9999; return d * (pb - pa); }
      if (k === 'mode') return d * (a.mode.localeCompare(b.mode) * -1);
      if (k === 'lang') return d * (String(a.lang).localeCompare(String(b.lang)) * -1);
      return d * (a.players - b.players);
    });
  }

  // ---------- pages ----------
  function render() {
    var setOn = S.page === 'settings' ? S.setTab : '';
    $$('#nav a').forEach(function (a) { a.classList.toggle('on', a.dataset.page === S.page && (!a.dataset.set || a.dataset.set === setOn)); });
    $$('#btabs a').forEach(function (a) { a.classList.toggle('on', a.dataset.page === S.page); });
    var P = $('#page'), f = PAGES[S.page] || PAGES.internet; P.className = LISTS[S.page] ? '' : 'pad'; P.innerHTML = f(); P.scrollTop = 0; bindPage();
    statusLine();
  }
  function statusLine() {
    var rows = LISTS[S.page] ? listRows(S.page) : S.rows, pl = 0, slots = 0;
    rows.forEach(function (r) { pl += r.players | 0; slots += Math.max(0, (r.max | 0) - (r.players | 0)); });
    var t = S.loading && !S.loaded ? 'Loading servers...' : 'Servers: ' + pl + ' players, playing on ' + rows.length + ' servers. (' + slots + ' player slots available)';
    if (S.page === 'lan' && S.lanBusy) t = 'Searching this Wi-Fi / hotspot for MP servers...';
    if (S.page === 'coop') t = S.coopBusy ? 'Looking for CoopFusion32 co-op servers...' : 'Co-op: ' + pl + ' players on ' + rows.length + ' CoopFusion32 server' + (rows.length === 1 ? '' : 's') + '. Crossplay through MP, on every phone.';
    $('#statusLine').textContent = t;
    var sel = rowBy(S.sel); $('#connectBtn').disabled = !(LISTS[S.page] && sel);
  }
  function needNet() {
    if (S.err === 'offline') return oops('No internet connection', 'Try turning on Wi-Fi or mobile data, then tap Retry. Lan still works on Wi-Fi or a hotspot.', 'wifi-off');
    if (S.err === 'down' && !S.rows.length) return oops('Something went wrong', 'The server list could not be loaded. Check your connection or try again in a moment.', 'globe');
    return '';
  }
  function oops(t, p, icon) { return '<div class="oops"><div class="oi">' + ic(icon) + '</div><h2>' + esc(t) + '</h2><p>' + esc(p) + '</p><div style="display:flex;gap:8px;margin-top:6px"><button class="btn go" data-act="retry">' + ic('refresh') + 'Retry</button><button class="btn" data-go="lan">Lan</button><button class="btn" data-act="offline-play">Play offline</button></div></div>'; }
  function skelRows(n) { var o = ''; for (var i = 0; i < n; i++) o += '<div class="skRow"><span class="sk" style="width:14px"></span><span class="sk" style="width:' + (30 + (i * 37) % 40) + '%"></span><span style="flex:1"></span><span class="sk" style="width:46px"></span><span class="sk" style="width:30px"></span><span class="sk" style="width:60px"></span></div>'; return o; }

  var PAGES = {
    internet: function () { return browser(); }, featured: function () { return browser(); }, favorites: function () { return browser(); },
    hosted: function () { return browser(); },
    lan: function () { if (!S.lanAt && !S.lanBusy) setTimeout(scanLan, 30); return browser(); },
    community: function () { return browser(); },
    coop: function () { if (!S.coopAt) setTimeout(loadCoop, 30); return browser(); },
    explore: function () { socStart(); return S.soc ? socServer(S.soc) : socList(); },
    game: function () { return '<div class="ph"><h1>Play</h1><span class="sub">GTA San Andreas runs inside AML Multiplayer</span></div><div class="hero">' + gameCard('sa') + gameCard('vc') + '</div>' + addGameCard() + editionCard() + obbCard() + engineCard(); },
    about: function () {
      return '<div class="ph"><h1>AML Multiplayer</h1><span class="sub">v' + esc(INFO.version || VERSION) + '</span></div>' +
        '<div class="card"><h3>The launcher</h3><p style="margin:0 0 6px;color:#c3c7d2">AML Multiplayer is the MP launcher for GTA San Andreas (AML) and GTA Vice City on Android, with SA + VC + PC crossplay and crossplay with CoopFusion32 co-op servers. GTA SA runs inside AML Multiplayer (its own game screen, not the GTA SA app), and MP joins the server you picked from the title screen.</p>' +
        '<div class="kv"><span>MP mod</span><span>libMP ' + MPVER + ' (bundled, copied to Android/data/com.christe.mp/mods)</span><span>GTA SA code</span><span>' + esc(INFO.saPkg || 'com.rockstargames.gtasa') + ' (AML) - 2.10 on 64-bit, 2.00 on 32-bit</span><span>Game files</span><span>' + (hasObb() ? 'ready' : 'not added yet (Settings > Storage)') + '</span><span>Co-op</span><span>CoopFusion32 servers, joined through libMP (32-bit and 64-bit)</span><span>Vice City</span><span>com.rockstargames.gtavc + ViceLoader (127.0.0.1:47790)</span><span>CPU</span><span>' + esc(INFO.deviceAbi || '') + '</span><span>Own files</span><span>' + esc(INFO.date2 || '') + '</span></div></div>' +
        '<div class="card"><h3>From the MP mod</h3><div class="kv"><span>Mod</span><span>MP (Multiplayer for GTA SA Android) by ThirtyFoxMC</span><span>Discord</span><span><a data-open-url="' + esc(DISCORD) + '">MP Discord server</a></span><span>Latest MP release</span><span>' + esc(S.relTag || '-') + '</span></div></div>' +
        '<div class="card"><h3>Open source used</h3><div class="kv"><span>Layout</span><span>SA-MP 0.3.7 server browser</span><span>Skin</span><span>MP launcher (libMP): ui_bg.jpg, title.png, icons, flags (lipis/flag-icons, MIT)</span><span>Skeleton loaders</span><span>idea from SkeletonView (Juanpe)</span><span>Server list</span><span>open.mp API, MP lobby (HiveMQ)</span><span>Co-op</span><span>CoopFusion32 by our collaborator (MP crossplays with its servers; the mod itself is unchanged)</span><span>Communities</span><span>MP Social (the same as the game and Moquil)</span></div></div>';
    },
    settings: function () { return '<div class="ph"><h1>Settings</h1></div>' + (SET[S.setTab] || SET.account)(); }
  };

  // ---------- 1.3 Communities = MP Social servers (js/soc.js + js/srv.js from Moquil: wagu/soc1/ over MQTT) ----------
  var socT = 0;
  function socStart() {
    if (S.socOn || !G.Soc) return; S.socOn = true;
    try {
      Soc.start(nick() || 'Player').catch(function (e) { X.log('social: ' + e); });
      Soc.on('change', socRefresh); Soc.on('post', socRefresh); Soc.on('link', socRefresh);
    } catch (e) { X.log('social: ' + e); }
  }
  function socRefresh() {
    if (S.page !== 'explore') return; clearTimeout(socT);
    socT = setTimeout(function () {
      var box = $('#socPosts'), inp = $('#socMsg');
      if (S.soc && box) { box.innerHTML = socPostsHtml(S.soc); var j = $('#socJoin'); if (j) { var c = Soc.community(S.soc) || {}; j.textContent = c.joined ? 'Leave' : 'Join'; } if (inp && D.activeElement === inp) return; }
      if (D.activeElement && D.activeElement.id === 'socMsg') return;
      render();
    }, 350);
  }
  function socIcon(c, big) {
    if (c.icon) return '<i class="cic" style="background-image:url(' + esc(c.icon) + ')"></i>';
    return '<i class="cic" style="' + logo(c.cid) + '">' + esc(initials(c.name)) + '</i>';
  }
  function socLinks() {
    return '<div class="socLinks"><a data-open-url="' + esc(DISCORD) + '"><img src="img/discord.png" alt=""><b>MP Discord</b><span>community server</span></a>' +
      '<a data-open-url="' + esc(YOUTUBE) + '">' + ic('play') + '<b>ThirtyFoxMC</b><span>YouTube</span></a>' +
      '<a data-act="soc-moquil">' + ic('users') + '<b>Moquil</b><span>friends, chats, voice</span></a></div>';
  }
  function socList() {
    var head = '<div class="ph"><h1>Communities</h1><span class="sub">MP Social servers - the same as in the game and Moquil</span><span class="grow"></span><select class="dd" id="exSort"><option value="rec">Newest</option><option value="players">Most members</option><option value="az">A - Z</option></select></div>';
    if (!G.Soc) return head + socLinks() + '<div class="empty">MP Social is not in this build.</div>';
    var q = S.q.trim().toLowerCase(), list = Soc.communities().filter(function (c) { return !q || (c.name + ' ' + (c.desc || '')).toLowerCase().indexOf(q) >= 0; });
    if (S.exploreSort === 'players') list.sort(function (a, b) { return (b.members | 0) - (a.members | 0); });
    else if (S.exploreSort === 'az') list.sort(function (a, b) { return a.name.localeCompare(b.name); });
    var body = list.length ? '<div class="cards">' + list.slice(0, 120).map(function (c) {
      var bg = c.banner ? 'url(' + esc(c.banner) + ')' : 'url(img/bg/bg_default.jpg)';
      return '<div class="scard ccard" data-soc="' + esc(c.cid) + '" style="min-height:112px"><div class="bgart" style="background:' + bg + ' center / cover"></div><div class="chd">' + socIcon(c) + '<div class="st">' + esc(c.name) + (c.joined ? ' <span class="badge">Joined</span>' : '') + '</div></div><div class="sd">' + esc(c.desc || 'An MP Social server.') + '</div>' +
        '<div class="sf"><div class="sn">' + (c.members ? '<div class="on"><i></i>' + c.members + ' member' + (c.members === 1 ? '' : 's') + '</div>' : '<div>MP Social</div>') + '<b>by ' + esc(c.ownerName || c.ownerId || '?') + '</b></div></div></div>';
    }).join('') + '</div>' : !S.online ? oops('No internet connection', 'Communities come from MP Social over the internet.', 'wifi-off') : skelCards() + '<div class="empty">Connecting to MP Social' + (Soc.up && Soc.up() ? ' - looking for servers...' : '...') + '</div>';
    return head + socLinks() + body;
  }
  function skelCards() { var o = '<div class="cards">'; for (var i = 0; i < 6; i++) o += '<div class="scard" style="min-height:112px"><span class="sk" style="width:60%;height:14px;display:block;margin-bottom:8px"></span><span class="sk" style="width:90%;height:10px;display:block"></span></div>'; return o + '</div>'; }
  function socChans(cid) { var l = []; try { if (G.MoqSrv) l = MoqSrv.textChans(cid) || []; } catch (e) {} return l.length ? l : [{ id: 'general', name: 'general' }]; }
  function socPostsHtml(cid) {
    var ps = []; try { ps = G.MoqSrv ? MoqSrv.posts(cid, S.socCh) : ((Soc.community(cid) || {}).posts || []).filter(function (p) { return (p.ch || 'general') === S.socCh; }); } catch (e) {}
    if (!ps.length) return '<div class="none">No messages in #' + esc(S.socCh) + ' yet. Say hi.</div>';
    return ps.slice(0, 150).map(function (p) {
      var h = hue(p.fromId), d = new Date((p.ts || 0) * 1000);
      return '<div class="po"><span class="av" style="background:hsl(' + h + ',40%,35%)">' + esc(initials(p.fromName)) + '</span><div class="pb"><b style="color:hsl(' + h + ',60%,72%)">' + esc(p.fromName || p.fromId) + '</b><i>' + esc(d.toLocaleDateString() + ' ' + d.toLocaleTimeString().replace(/:\d\d( |$)/, '$1')) + '</i><p>' + esc(p.text || (p.poll ? '[poll]' : '')) + '</p></div></div>';
    }).join('');
  }
  function socServer(cid) {
    var c = Soc.community(cid); if (!c) { S.soc = null; return socList(); }
    var chans = socChans(cid); if (!chans.some(function (x) { return x.id === S.socCh; })) S.socCh = chans[0].id;
    var ch = chans.filter(function (x) { return x.id === S.socCh; })[0] || chans[0];
    return '<div class="soc" style="height:calc(100% - 4px)"><div class="socSide"><div class="sb" style="' + (c.banner ? 'background-image:url(' + esc(c.banner) + ')' : '') + '"><b>' + esc(c.name) + '</b></div><div class="chs"><div class="chh">TEXT CHANNELS</div>' +
      chans.map(function (x) { return '<a class="ch' + (x.id === S.socCh ? ' on' : '') + '" data-ch="' + esc(x.id) + '"># ' + esc(x.name || x.id) + '</a>'; }).join('') +
      (c.members ? '<div class="chh" style="margin-top:8px">MEMBERS - ' + c.members + '</div>' : '') + '</div><div class="back" data-act="soc-back">&larr; All communities</div></div>' +
      '<div class="socMain"><div class="sh"><b># ' + esc(ch.name || ch.id) + '</b><span>' + esc(ch.topic || c.desc || '') + '</span><button class="btn sm' + (c.joined ? '' : ' go') + '" id="socJoin" data-act="soc-join">' + (c.joined ? 'Leave' : 'Join') + '</button></div>' +
      '<div class="socPosts" id="socPosts">' + socPostsHtml(cid) + '</div>' +
      '<div class="socComp"><input class="inp" id="socMsg" maxlength="600" placeholder="Message #' + esc(ch.name || ch.id) + '"><button class="btn sm go" data-act="soc-send">Send</button></div></div></div>';
  }
  function socSend() {
    var i = $('#socMsg'), t = i ? i.value.trim() : ''; if (!t || !S.soc) return;
    var c = Soc.community(S.soc); if (!c) return;
    if (!nick()) { askName(socSend); return; }
    try { Soc.setName(nick()); } catch (e) {}
    if (!c.joined && c.ownerId !== Soc.id) Soc.joinCommunity(S.soc, true);
    Promise.resolve(Soc.post(S.soc, t, { ch: S.socCh })).then(function (ok) { if (ok) { i.value = ''; var b = $('#socPosts'); if (b) b.innerHTML = socPostsHtml(S.soc); } else toast('Could not send (slow mode or no permission)'); });
  }
  function saReady() { return !!OBB.gameAdded; }   // 1.4: only the GTA SA added to AML Multiplayer
  // 1.4: AML Multiplayer 32-bit. Android gives an app one CPU mode, so a GTA SA without arm64-v8a (2.00, 32-bit)
  // runs in the 32-bit edition of AML Multiplayer (same app, installed over this one; settings and files stay).
  function edName(e) { return e === '32' ? 'AML Multiplayer 32-bit' : 'AML Multiplayer (64-bit)'; }
  function editionAsk(text) {
    var need = OBB.editionNeed || '';
    if (need !== '32' && need !== '64') { if (need) dialog('This GTA SA can\'t run here', '<p>' + esc(OBB.editionText || text || '') + '</p>', [['OK', 'go']]); return; }
    dialog('Switch to ' + edName(need), '<p>' + esc(OBB.editionText || text || '') + '</p><p>AML Multiplayer downloads ' + edName(need) + ' and Android asks you to install it over this one. Tap Connect again after it opens.</p>', [['Later', ''], ['Switch', 'go']], function (i) { if (i === 1) { go('settings', 'storage'); job('edition', need); } });
  }
  function editionCard() {
    var j = S.job && S.job.kind === 'edition' ? S.job : null, need = OBB.editionNeed || '', ed = OBB.edition || '';
    var other = ed === '64' ? '32' : ed === '32' ? '64' : '';
    var st = '<span class="st-ok">' + (ed === '32' ? 'AML Multiplayer 32-bit (GTA SA 2.00, armeabi-v7a)' : ed === '32only' ? '32-bit phone (GTA SA 2.00, armeabi-v7a)' : 'AML Multiplayer 64-bit (GTA SA 2.10, arm64-v8a)') + '</span>';
    if (OBB.gameAbi) st += '<br>Added GTA SA: ' + esc(OBB.gameAbi);
    if (need) st += '<br><span class="st-bad">' + esc(OBB.editionText || '') + '</span>';
    var btn = other && (need === other || !need) ? '<button class="btn sm' + (need ? ' go' : '') + '" data-act="edition"' + (S.job ? ' disabled' : '') + '>' + ic('download') + 'Switch to ' + (other === '32' ? '32-bit' : '64-bit') + '</button>' : '';
    if (ed === '64' && OBB.has32 === false) btn = '';
    return '<div class="card"><h3>Edition</h3>' + opt('CPU edition', st + '<br>A GTA SA with no arm64-v8a (32-bit only) runs in AML Multiplayer 32-bit, the same app installed over this one.' +
      (j ? '<div class="prog"><i style="width:' + (j.total ? Math.round(j.done * 100 / j.total) : 0) + '%"></i></div><span id="jobMsg">' + esc(j.msg || '') + '</span>' : ''), btn) + '</div>';
  }
  function addGameCard() {
    var j = S.job && S.job.kind === 'game-add' ? S.job : null;
    var st = OBB.gameImported ? '<span class="st-ok">Added to AML Multiplayer' + (OBB.gameVersion ? ' v' + esc(OBB.gameVersion) : '') + (OBB.gameSize ? ' (' + gb(OBB.gameSize) + ')' : '') + '</span>' : '<span class="st-bad">Not added yet</span>';
    return '<div class="card"><h3>GTA SA game</h3>' + opt('GTA San Andreas', st + '<br>This phone needs ' + esc(OBB.gameNeed || 'GTA SA AML') + '. Pick your own GTA SA .apk once: AML Multiplayer keeps its own copy and always runs that one inside; it never opens or uses the GTA SA app. (Rockstar\'s game is not shipped with AML Multiplayer, you add the copy you own.)' +
      (j ? '<div class="prog"><i style="width:' + (j.total ? Math.round(j.done * 100 / j.total) : 0) + '%"></i></div><span id="jobMsg">' + esc(j.msg || '') + '</span>' : ''),
      '<button class="btn sm go" data-act="game-add"' + (S.job ? ' disabled' : '') + '>' + ic('plus') + (OBB.gameImported ? 'Replace' : 'Add GTA SA') + '</button>' + (OBB.gameImported ? '<button class="btn sm red" data-act="game-remove">Remove</button>' : '')) + '</div>';
  }
  function hasObb() { return OBB.ready || (OBB.main && (OBB.main.own > 0 || OBB.main.game > 0)); }
  function gameCard(g) {
    var sa = g === 'sa', inf = sa ? INFO.sa || {} : INFO.vc || {}, vl = INFO.vl || {};
    var ga = OBB.gameAbi || '', abi = sa ? (/32-bit only/.test(ga) ? 'armeabi-v7a' : /^64/.test(ga) ? 'arm64-v8a' : /\+/.test(ga) ? '32 + 64-bit' : (INFO.abi || 'armeabi-v7a')) : '';
    var ver = sa ? (OBB.gameAdded ? '<span class="st-ok">added' + (OBB.gameVersion ? ' v' + esc(OBB.gameVersion) : '') + '</span>' : '<span class="st-bad">not added</span>') : inf.installed ? 'v' + esc(inf.version || '?') : '<span class="st-bad">not installed</span>';
    var how = sa ? (ENG.problem ? '<span class="st-mid">' + esc(ENG.problem) + '</span>' : '<span class="st-ok">Runs inside AML Multiplayer</span>') : (vl.installed ? 'ViceLoader' : 'Opens Vice City');
    return '<div class="gcard ' + g + '" style="background:url(img/bg/bg_' + (sa ? 'default' : '7') + '.jpg)"><div class="gt">GRAND THEFT AUTO</div>' +
      '<h2>' + (sa ? 'San Andreas' : 'Vice City') + '</h2>' +
      '<div class="gi"><span>' + (sa ? 'AML' : 'VC') + ' ' + ver + '</span>' + (sa ? '<span class="chip mob">' + esc(abi) + '</span>' : '') + '</div>' +
      '<div class="gi">' + how + '</div>' +
      '<div class="ga"><button class="btn ' + (sa ? 'go' : 'vc') + '" data-play="' + g + '"' + ((sa ? saReady() : inf.installed) ? '' : ' disabled') + '>' + ic('play') + 'Play ' + (sa ? 'GTA SA' : 'Vice City') + '</button>' +
      '<button class="btn sm" data-act="quick-' + g + '">Quick connect</button></div></div>';
  }
  function obbRow(k) {
    var o = OBB[k] || {};
    var st = o.own > 0 ? '<span class="st-ok">Own copy ' + gb(o.own) + '</span>' : o.game > 0 ? '<span class="st-mid">GTA SA\'s ' + gb(o.game) + ' (read in place)</span>' : '<span class="st-bad">not found</span>';
    return opt(k + '.8 game file', st, '');
  }
  function obbCard() {
    var j = S.job;
    return '<div class="card"><h3>GTA SA game files</h3>' + obbRow('main') + obbRow('patch') +
      '<div class="opt"><div class="ol"><b>Get the game files</b><span>Copy them from the installed GTA SA, or pick the main.8 / patch.8 files. Android 11+ may need "Allow access" first.</span>' +
      (j ? '<div class="prog"><i style="width:' + (j.total ? Math.round(j.done * 100 / j.total) : 0) + '%"></i></div><span id="jobMsg">' + esc(j.msg || '') + '</span>' : '') + '</div>' +
      '<button class="btn sm go" data-act="obb-copy"' + (j ? ' disabled' : '') + '>' + ic('download') + 'Copy</button><button class="btn sm" data-act="obb-import"' + (j ? ' disabled' : '') + '>' + ic('folder') + 'Import</button>' + (OBB.canAsk ? '<button class="btn sm" data-act="obb-access">Allow access</button>' : '') + '</div></div>';
  }
  function engineCard() {
    return '<div class="card"><h3>MP files</h3>' + opt('MP data (files/TYFMC/MP)', ENG.mpFiles ? '<span class="st-ok">ready</span>' : '<span class="st-mid">not downloaded yet - MP downloads what it needs on first start, or get it all now</span>', '<button class="btn sm" data-act="mp-files">' + ic('download') + 'Download</button>') + '</div>';
  }
  function browser() {
    var rows = filtered(), net = S.page === 'lan' || S.page === 'favorites' ? '' : needNet();
    var tabs = [['internet', 'Internet'], ['featured', 'Featured'], ['favorites', 'Favorites'], ['lan', 'Lan']];
    var head = S.page === 'community' && S.comm ? '<div class="ttabs"><a class="on">' + esc(S.comm.name) + '</a><span class="cnt">' + rows.length + ' servers</span></div>' :
      '<div class="ttabs">' + tabs.map(function (t) { return '<a data-go="' + t[0] + '" class="' + (S.page === t[0] ? 'on' : '') + '">' + t[1] + '</a>'; }).join('') + (S.page === 'hosted' ? '<a class="on">open.mp</a>' : '<a data-go="hosted">open.mp</a>') + (S.page === 'coop' ? '<a class="on">Co-op</a>' : '<a data-go="coop">Co-op</a>') + '<span class="cnt">' + (S.page === 'lan' ? '<a data-act="lan-scan">' + (S.lanBusy ? 'Searching...' : 'Search again') + '</a>' : S.page === 'coop' ? '<a data-act="coop-add" title="Add server">+ Add</a>' : rows.length + ' servers') + '</span></div>';
    return '<div class="browser"><div class="tbl">' + head + (net ? net : table(rows)) + '</div>' + pane(rowBy(S.sel)) + '</div>';
  }
  function table(rows) {
    var sel = rows.filter(function (r) { return r.key === S.sel; })[0] || rows[0];
    S.sel = sel ? sel.key : null;
    function th(k, t) { return '<span class="' + (S.sort === k ? 'sorted' : '') + '" data-sort="' + k + '">' + t + (S.sort === k ? (S.sortDir < 0 ? ' &#9662;' : ' &#9652;') : '') + '</span>'; }
    var empty = S.page === 'favorites' ? 'No favourite servers yet. Select a server and tap the star.' : S.page === 'lan' ? (S.lanBusy ? 'Searching this Wi-Fi / hotspot for MP servers...' : 'No MP server on this Wi-Fi / hotspot. Start one in the game (Host) or tap Search again.') : S.page === 'coop' ? (S.coopBusy ? 'Looking for co-op servers...' : 'No CoopFusion32 co-op server online right now. Run the CoopFusion32 server, ask a friend for theirs, or tap Add server.') : 'No servers match.';
    var body = (!S.loaded && S.page !== 'lan' && S.page !== 'favorites' && S.page !== 'coop') || (S.page === 'coop' && S.coopBusy && !rows.length) || (S.page === 'lan' && S.lanBusy && !rows.length) ? skelRows(10) : rows.length ? rows.slice(0, 500).map(function (r) {
      return '<div class="trow' + (sel && r.key === sel.key ? ' sel' : '') + '" data-key="' + esc(r.key) + '"><i class="rbg" style="background-image:url(' + bgOf(r) + ')"></i><span class="lk' + (r.locked ? '' : ' n') + '">' + ic(r.locked ? 'lock' : 'unlock') + '</span>' +
        '<span class="nm"><span class="t">' + esc(r.name) + '</span></span><span class="md">' + esc(r.mode || '-') + '</span><span class="pl">' + r.players + '<span> / ' + r.max + '</span></span><span class="pg ' + pcls(r.kind === 'mp' ? 0 : r.ping) + '">' + ptxt(r) + '</span><span class="lg">' + esc(r.lang || '-') + '</span><span class="loc">' + flag(r.cc || ccFromLang(r.lang)) + '</span></div>';
    }).join('') : '<div class="empty">' + empty + '</div>';
    return '<div class="thead"><span>' + ic('lock') + '</span>' + th('name', 'HostName') + th('mode', 'Gamemode') + th('players', 'Players') + th('ping', 'Ping') + th('lang', 'Language') + '<span class="lc">Loc.</span></div><div class="tbody">' + body + '</div>';
  }
  function rulesOf(r) {
    if (r.kind === 'coop') return { mod: 'CoopFusion32 server' + (r.ver ? ' ' + r.ver : ''), mode: 'Co-op', game: 'GTA SA (AML), 32-bit and 64-bit', combat: 'Shots, melee, health and kills synced', crossplay: 'MP (libMP) players + CoopFusion32 players', address: r.host + ':' + r.port };
    if (r.kind === 'omp') { var x = S.rules[r.key]; if (x) return x; fetchRules(r); return null; }
    return { lagcomp: 'On', mapname: 'San Andreas', version: 'MP ' + MPVER, region: REGIONS[r.region] || 'Auto', gamemode: r.mode, weather: '10', worldtime: '12:00', weburl: 'mpsamp-open.github.io' };
  }
  function fetchRules(r) {
    if (r._rq) return; r._rq = 1;
    getJson(URLS.ompOne + r.host + ':' + r.port).then(function (j) { S.rules[r.key] = j.ru || {}; if (S.sel === r.key) refreshPane(); }).catch(function () { S.rules[r.key] = { version: r.ver || '-' }; if (S.sel === r.key) refreshPane(); });
  }
  function pane(r) {
    if (!r) return '<div class="pane"><div class="ph"><span>Player</span><span>Score</span></div><div class="pl"><div class="none">Select a server.</div></div><div class="ph"><span>Rule</span><span>Value</span></div><div class="ru"></div></div>';
    var ru = rulesOf(r), rr = '';
    if (ru) Object.keys(ru).filter(function (k) { return k !== 'artwork' && k !== 'allowed_clients'; }).forEach(function (k) { rr += '<div class="kvr"><span>' + esc(k) + '</span><span>' + esc(ru[k]) + '</span></div>'; });
    else rr = '<div class="none">Loading rules...</div>';
    var web = ru && ru.weburl ? ru.weburl : '';
    var who = r.players ? r.players + ' player' + (r.players === 1 ? '' : 's') + ' online. Names show in the game (Tab).' : 'Nobody is playing right now.';
    return '<div class="pane"><div class="sel"><i class="rbg" style="background-image:url(' + bgOf(r) + ')"></i><div style="min-width:0;flex:1"><b>' + esc(r.name) + '</b><span>' + esc(r.kind === 'mp' ? 'MP lobby, port ' + r.port : r.kind === 'coop' ? 'Co-op ' + (r.src === 'lan' ? 'on this Wi-Fi, ' : '') + r.host + ':' + r.port : r.host + ':' + r.port) + '</span><span class="pf">' + esc(r.mode || '') + ' &middot; ' + (r.cc === 'global' ? 'Global' : esc((r.cc || ccFromLang(r.lang) || '-').toUpperCase())) + '</span></div><a data-fav="' + esc(r.key) + '" title="Favorite" style="color:' + (S.favs[r.key] ? 'var(--mid)' : 'var(--dim)') + '">' + ic('star') + '</a></div>' +
      '<div class="ph"><span>Player</span><span>Score</span></div><div class="pl"><div class="none">' + esc(who) + '</div></div>' +
      '<div class="ph"><span>Rule</span><span>Value</span></div><div class="ru">' + rr + '</div>' + (web ? '<a class="web" data-open-url="' + esc(/^https?:/.test(web) ? web : 'http://' + web) + '">' + esc(web) + '</a>' : '') + '</div>';
  }
  function refreshPane() { var p = $('.pane'); if (!p) return; var t = D.createElement('div'); t.innerHTML = pane(rowBy(S.sel)); var n = t.firstChild; p.replaceWith(n); bindIn(n); statusLine(); }

  // ---------- settings pages ----------
  function opt(t, s, ctl) { return '<div class="opt"><div class="ol"><b>' + t + '</b>' + (s ? '<span>' + s + '</span>' : '') + '</div>' + (ctl || '') + '</div>'; }
  function sw(id, on) { return '<button class="sw' + (on ? ' on' : '') + '" data-sw="' + id + '"></button>'; }
  var SET = {
    account: function () {
      return '<div class="card"><h3>MP account</h3>' + (S.user ? opt((S.user.avatar ? '<img class="avBig" src="' + S.user.avatar + '" alt="">' : '') + 'Signed in as ' + esc(S.user.user), S.user.dname ? 'Discord: ' + esc(S.user.dname) + ' &middot; the same account as in the game, Moquil and the MP apps' : S.user.sess ? 'Getting your Discord picture...' : 'Sign out and in again to show your Discord picture', '<button class="btn sm red" data-act="logout">Sign out</button>')
        : opt('Guest', 'Sign in or create an MP account to keep your name, stats and friends', '<button class="btn sm go" data-act="auth">Sign in</button>')) +
        opt('Name in game', 'The Name: box. Sent to MP when the game starts', '<input class="inp" id="nickInp" style="width:170px" maxlength="23" value="' + esc(nick()) + '"' + (S.user ? ' disabled' : '') + '>') + '</div>';
    },
    game: function () {
      var sa = INFO.sa || {};
      return '<div class="card"><h3>GTA San Andreas (AML)</h3>' + opt('Game', OBB.gameAdded ? '<span class="st-ok">Added to AML Multiplayer' + (OBB.gameVersion ? ' v' + esc(OBB.gameVersion) : '') + (OBB.gameAbi ? ' (' + esc(OBB.gameAbi) + ')' : '') + '</span>' : '<span class="st-bad">not added yet</span>', '<button class="btn sm go" data-act="go-storage">' + (OBB.gameAdded ? 'Storage' : 'Add GTA SA') + '</button>') +
        opt('Run GTA SA', ENG.problem ? '<span class="st-mid">' + esc(ENG.problem) + '</span>' : 'Always inside AML Multiplayer, with the GTA SA added to it: its own game screen, game files and MP files. The GTA SA app is never opened or used.', '') +
        opt('CPU / MP build', 'arm64-v8a = GTA SA 2.10, armeabi-v7a = GTA SA 2.00. Inside AML Multiplayer always uses this phone\'s CPU (' + (ENG.proc64 ? '64-bit' : '32-bit') + ')',
          '<select class="dd" id="abiSel"><option value="auto">Auto (' + esc(INFO.abi || '') + ')</option><option value="arm64-v8a">arm64-v8a (2.10)</option><option value="armeabi-v7a">armeabi-v7a (2.00)</option></select>') +
        opt('Join from the title screen', 'MP joins the server you picked as soon as the title menu is up', sw('autoJoin', ls('autoJoin') !== false)) + '</div>' +
        '<div class="card"><h3>Vice City</h3>' + opt('GTA Vice City', (INFO.vc || {}).installed ? 'v' + esc(INFO.vc.version) + ' - opens the Vice City app (ViceLoader for MP)' : '<span class="st-bad">not installed</span>', '<button class="btn sm" data-act="open-vc">Open</button>') + '</div>';
    },
    storage: function () {
      return addGameCard() + editionCard() + obbCard() + engineCard() + '<div class="card"><h3>AML Multiplayer folder (date2)</h3>' + opt('date2', esc(INFO.date2 || ''), '<button class="btn sm" data-act="setup">Repair</button>') +
        opt('Boot video', 'Put your own startup.mp4 in date2/resources to play it at start-up', '') + '</div>';
    },
    network: function () {
      return '<div class="card"><h3>Network</h3>' + opt('Status', S.online ? '<span class="st-ok">Online</span>' : '<span class="st-bad">Offline - turn on Wi-Fi or mobile data</span>', '<button class="btn sm" data-act="retry">Retry</button>') +
        opt('Region', 'MP lobby servers of this region (Auto = all)', '<select class="dd" id="regSel">' + REGIONS.map(function (r, i) { return '<option value="' + i + '"' + (S.region === i ? ' selected' : '') + '>' + r + '</option>'; }).join('') + '</select>') +
        opt('Refresh the list every 60 s', '', sw('autoRefresh', S.autoRefresh)) + '</div>';
    },
    ui: function () {
      return '<div class="card"><h3>Interface</h3>' + opt('Loading screen', 'The MP loading screen at start-up', sw('bootAnim', ls('bootAnim') !== false)) +
        opt('Notifications', 'MP updates in the bell', sw('notify', ls('notify') !== false)) + '</div>';
    }
  };

  // ---------- binding ----------
  function bindIn(P) {
    $$('[data-go]', P).forEach(function (a) { a.onclick = function () { go(a.dataset.go); }; });
    $$('[data-fav]', P).forEach(function (f) { f.onclick = function (e) { e.stopPropagation(); toggleFav(f.dataset.fav); }; });
    $$('[data-open-url]', P).forEach(function (a) { a.onclick = function (e) { e.preventDefault(); X.open(a.dataset.openUrl); }; });
    $$('[data-act]', P).forEach(function (b) { b.onclick = function () { act(b.dataset.act); }; });
  }
  function bindPage() {
    var P = $('#page'); bindIn(P);
    $$('[data-sort]', P).forEach(function (a) { a.onclick = function () { if (S.sort === a.dataset.sort) S.sortDir = -S.sortDir; else { S.sort = a.dataset.sort; S.sortDir = -1; } ls('sort', S.sort); render(); }; });
    $$('.trow', P).forEach(function (r) {
      r.onclick = function () {
        var was = S.sel === r.dataset.key; S.sel = r.dataset.key;
        if (was && Date.now() - (r._t || 0) < 450) { join(rowBy(S.sel)); return; }
        $$('.trow', P).forEach(function (x) { x.classList.toggle('sel', x === r); }); r._t = Date.now(); refreshPane();
      };
    });
    $$('[data-play]', P).forEach(function (b) { b.onclick = function () { play(b.dataset.play, null); }; });
    $$('[data-comm]', P).forEach(function (c) { c.onclick = function () { S.comm = COMMS.filter(function (x) { return x.id === c.dataset.comm; })[0]; S.page = 'community'; S.sel = null; render(); pingVisible(); }; });
    $$('[data-sw]', P).forEach(function (b) { b.onclick = function () { var k = b.dataset.sw, on = !b.classList.contains('on'); b.classList.toggle('on', on); if (k === 'autoRefresh') S.autoRefresh = on; ls(k, on); }; });
    $$('[data-soc]', P).forEach(function (c) { c.onclick = function () { S.soc = c.dataset.soc; S.socCh = 'general'; try { Soc.viewCommunity(S.soc); } catch (e) {} render(); }; });
    $$('[data-ch]', P).forEach(function (c) { c.onclick = function () { S.socCh = c.dataset.ch; render(); }; });
    var sm = $('#socMsg', P); if (sm) sm.onkeydown = function (e) { if (e.key === 'Enter') socSend(); };
    var ex = $('#exSort', P); if (ex) { ex.value = S.exploreSort; ex.onchange = function () { S.exploreSort = ex.value; render(); }; }
    var ni = $('#nickInp', P); if (ni) ni.onchange = function () { setNick(ni.value); toast('Name saved'); };
    var sp = $('#saPkg', P); if (sp) sp.onchange = function () { X.set('sa_pkg', sp.value.trim() || 'com.rockstargames.gtasa'); readInfo(); toast('Game package saved'); };
    var ab = $('#abiSel', P); if (ab) { ab.value = INFO.abiMode || 'auto'; ab.onchange = function () { X.set('abi', ab.value); setTimeout(function () { readInfo(); render(); }, 400); }; }
    
    var rg = $('#regSel', P); if (rg) rg.onchange = function () { setRegion(rg.value | 0); };
  }
  function go(p, set) { S.page = p; if (p !== 'community') S.comm = null; if (set) S.setTab = set; render(); if (LISTS[p]) pingVisible(); }
  function setNick(v) { v = String(v || '').trim().replace(/[^A-Za-z0-9_\[\].$=@()]/g, '').slice(0, 23); S.nick = v; ls('nick', v); renderAvatar(); }
  function rowBy(k) { return k ? allRows().filter(function (r) { return r.key === k; })[0] : null; }
  function toggleFav(k) { if (S.favs[k]) delete S.favs[k]; else S.favs[k] = 1; ls('favs', S.favs); if (S.page === 'favorites') render(); else refreshPane(); }
  function job(kind, arg) {
    if (S.job) return; var id = 'j' + (++cbn); S.job = { kind: kind, done: 0, total: 0, msg: kind === 'mp-files' ? 'Downloading MP files...' : kind === 'edition' ? 'Downloading AML Multiplayer ' + (arg === '32' ? '32-bit' : '64-bit') + '...' : 'Starting...' }; render();
    PROG[id] = function (state, done, total, msg) {
      if (state === 'cancel') { S.job = null; render(); return; }
      if (state === 'done' || state === 'error') { S.job = null; readInfo(); render(); toast(msg || (state === 'done' ? 'Done' : 'Failed')); if (state === 'done' && kind === 'game-add') editionAsk(); return; }
      S.job.done = done; S.job.total = total; S.job.msg = (state === 'download' || kind === 'edition' ? 'Downloading ' : 'Copying ') + (msg || '') + ' ' + gb(done) + (total > 0 ? ' / ' + gb(total) : '');
      var b = $('.prog i'); if (b) b.style.width = (total ? Math.round(done * 100 / total) : 0) + '%'; var m = $('#jobMsg'); if (m) m.textContent = S.job.msg;
    };
    var fn = { 'obb-copy': 'obbCopy', 'obb-import': 'obbImport', 'mp-files': 'mpFiles', 'game-add': 'gameAdd', edition: 'editionSwitch' }[kind];
    if (!call(fn, id, arg)) { delete PROG[id]; S.job = null; render(); toast('Update AML Multiplayer for this'); }
  }
  function act(a) {
    if (a === 'retry') { S.err = ''; loadServers(); render(); }
    else if (a === 'offline-play') play('sa', null);
    else if (a === 'auth') showAuth('login');
    else if (a === 'lan-scan') scanLan();
    else if (a === 'obb-copy' || a === 'obb-import' || a === 'mp-files' || a === 'game-add') job(a);
    else if (a === 'go-storage') go('settings', 'storage');
    else if (a === 'edition') { var ot = OBB.edition === '32' ? '64' : '32'; if (OBB.editionNeed === ot) job('edition', ot); else confirmDlg('Switch edition', 'Install ' + edName(ot) + ' over this one? Your settings, game files and the added GTA SA stay. ' + (ot === '32' ? 'It runs 32-bit GTA SA 2.00 only.' : 'It runs 64-bit GTA SA 2.10.'), 'Switch', function () { job('edition', ot); }); }
    else if (a === 'game-remove') confirmDlg('Remove GTA SA', 'Remove the GTA SA copy that was added to AML Multiplayer? Your game files and saves stay.', 'Remove', function () { call('gameRemove'); readInfo(); render(); });
    else if (a === 'coop-scan') loadCoop();
    else if (a === 'soc-back') { S.soc = null; render(); }
    else if (a === 'soc-send') socSend();
    else if (a === 'soc-join') { var sc = Soc.community(S.soc); if (sc) { Soc.joinCommunity(S.soc, !sc.joined); render(); } }
    else if (a === 'soc-moquil') { if (!X.openApp('com.thirtyfox.moquil')) X.open('https://mpsamp-open.github.io/moquils-web'); }
    else if (a === 'coop-add') coopAdd();
    else if (a === 'obb-access') { call('obbAccess'); toast('Allow "Install unknown apps" / storage, then come back'); }
    else if (a === 'logout') confirmDlg('Sign out', 'Sign out of your MP account on this phone?', 'Sign out', function () { S.user = null; ls('session', null); renderAvatar(); render(); });
    else if (a === 'open-vc') { if (!X.openApp('com.rockstargames.gtavc')) toast('Vice City is not installed'); }
    else if (a === 'setup') { var r = {}; try { r = JSON.parse(X.setup()); } catch (e) {} readInfo(); toast(r.ok ? 'date2 is ready' : 'date2: ' + (r.err || 'failed')); render(); }
    else if (a === 'quick-sa' || a === 'quick-vc') {
      var g = a.slice(6), best = S.rows.filter(function (r) { return !r.locked && r.kind === 'mp' && (g === 'vc' ? r.game === 'vc' : r.game !== 'vc') && (!r.max || r.players < r.max); }).sort(function (x, y) { return y.players - x.players; })[0];
      if (best) join(best); else play(g, null);
    }
  }

  // ---------- play / join (the MP loading screen while it starts) ----------
  function inApp() { return !ENG.problem; }
  function play(game, row, pass) {
    readInfo();
    var inf = game === 'vc' ? INFO.vc : INFO.sa;
    if (game === 'sa' && !saReady()) {
      dialog('Add GTA San Andreas', '<p>Add your GTA SA once: pick its .apk (' + esc(OBB.gameNeed || 'GTA SA AML') + '). AML Multiplayer keeps a copy and runs it inside, no GTA SA app needed.</p>', [['Cancel', ''], ['Add GTA SA', 'go']], function (i) { if (i === 1) { go('settings', 'storage'); job('game-add'); } });
      return;
    }
    if (game === 'vc' && (!inf || !inf.installed) && !(INFO.vl && INFO.vl.installed)) {
      dialog('Vice City is not installed', 'Install GTA Vice City (and ViceLoader for MP) first.', [['OK', '']]);
      return;
    }
    if (game === 'sa' && (inApp() || OBB.gameImported) && !hasObb()) {
      dialog('GTA SA game files', '<p>GTA SA inside AML Multiplayer needs its game files (main.8 and patch.8). Copy them from the installed GTA SA or pick the files once.</p>', [['Cancel', ''], ['Import', ''], ['Copy', 'go']], function (i) {
        if (i === 1) { go('settings', 'storage'); job('obb-import'); } else if (i === 2) { go('settings', 'storage'); job('obb-copy'); }
      });
      return;
    }
    var spec = row ? joinSpec(row) : '';
    var L = D.createElement('div'); L.className = 'launching';
    L.innerHTML = '<div class="ldArt"><div class="ldBg"></div><div class="ldVig"></div><img class="ldTitle" src="img/title.png" alt=""><div class="ldSub">' + (row ? 'Connecting to ' + esc(row.name) : game === 'vc' ? 'Starting Vice City' : 'Starting GTA San Andreas') + '</div>' +
      '<div class="ldTip"><b><i>!</i>GAMEPLAY TIP</b><span>' + esc(TIPS[(Math.random() * TIPS.length) | 0]) + '</span></div><div class="ldPct">LOADING... <span class="lp">0</span>%</div><div class="ldBar"><i></i></div></div>';
    D.body.appendChild(L);
    var p = 0, t = setInterval(function () { p = Math.min(99, p + 4 + ((Math.random() * 6) | 0)); $('.lp', L).textContent = p; $('.ldBar i', L).style.width = p + '%'; }, 90);
    X.log(new Date().toISOString() + ' play ' + game + ' ' + spec);
    setTimeout(function () {
      var r = X.play(game, ls('autoJoin') === false ? '' : spec, nick(), pass || '');
      setTimeout(function () { clearInterval(t); L.remove(); }, 1800);
      if (String(r).indexOf('edition:') === 0) { clearInterval(t); L.remove(); readInfo(); editionAsk(String(r).replace(/^edition:[^:]*:/, '')); return; }
      if (String(r).indexOf('nocoop:') === 0) {
        clearInterval(t); L.remove();
        dialog('Co-op can\'t start here', '<p>' + esc(r.slice(7)) + '</p>', [['OK', 'go']]);
        return;
      }
      if (String(r).indexOf('noinapp:') === 0) {
        clearInterval(t); L.remove();
        dialog('GTA SA can\'t start yet', '<p>' + esc(r.slice(8)) + '.</p><p>AML Multiplayer only runs the GTA SA added to it. Add your GTA SA (AML) .apk in Settings > Storage, then tap Connect again.</p>', [['Later', ''], ['Add GTA SA', 'go']], function (i) {
          if (i === 1) { go('settings', 'storage'); job('game-add'); }
        });
        return;
      }
      if (r !== 'ok') toast(r === 'missing' ? 'The game is not installed' : 'Could not start the game: ' + r);
    }, 1100);
  }
  function join(row) {
    if (!row) return;
    if (!nick()) { askName(function () { join(row); }); return; }
    var game = row.game === 'vc' ? 'vc' : 'sa';
    if (row.locked && row.kind === 'omp') {
      dialog('Password', '<input class="inp" id="pwInp" type="password" placeholder="Server password">', [['Cancel', ''], ['Join', 'go']], function (i) { if (i === 1) play(game, row, $('#pwInp').value); });
      return;
    }
    play(game, row);
  }
  function askName(done) {
    dialog('Choose a name', '<p>Type your name in the Name: box, or sign in to your MP account.</p><input class="inp" id="nmInp" maxlength="23" placeholder="Name_Surname">', [['Sign in', ''], ['Play as guest', 'go']], function (i) {
      if (i === 0) { showAuth('login'); return; }
      var v = ($('#nmInp').value || '').trim().replace(/[^A-Za-z0-9_\[\].$=@()]/g, '').slice(0, 23); if (v.length < 3) { toast('3-23 letters, digits or _'); return false; }
      setNick(v); done();
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
  function pop(anchor, html, bind, left) {
    var P = $('#pop'); P.innerHTML = html; P.classList.remove('hide');
    var r = anchor.getBoundingClientRect(), w = P.offsetWidth;
    P.style.top = (r.bottom + 2) + 'px'; P.style.left = Math.max(4, Math.min(G.innerWidth - w - 4, left ? r.left : r.right - w)) + 'px';
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
  function checkRelease() {
    getJson(URLS.rel).then(function (j) { if (j && j.tag_name) { S.relTag = j.tag_name; note('rel-' + j.tag_name, 'MP ' + j.tag_name + ' is out', (j.name || 'New release') + ' - update AML Multiplayer for the new libMP'); } }).catch(function () {});
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
      '<div class="aWrap"><div class="aForm"><div class="aBrand"><img src="img/title.png" alt="AML Multiplayer" style="width:150px;height:auto;border-radius:0"></div><h1>' + title + '</h1><div class="sub">' + sub + '</div>' + inner + '<div id="status" class="status"></div><div class="aFoot">' + foot + '</div></div>' +
      '<div class="aSide"><h2>One MP account</h2><p>The same account in the game, Moquil, MP Voice and AML Multiplayer. Create it with the MP Discord bot: a code, /gen in #verification, then your name and password.</p><button class="btn sm blue" data-ourl="' + esc(DISCORD) + '">' + ic('discord') + 'MP Discord</button><button class="btn sm" id="aGuest">Continue as guest</button></div></div>';
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
      A.innerHTML = shell('Create an account', 'One MP account for the game, Moquil and AML Multiplayer',
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
        signup: function (a) { busy(false); if (a.ok === '1') loggedIn(a.user || $('#user').value.trim(), a.sess); else st(a.err || 'Sign up failed.', 'bad'); } });
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
    var t = 0, c3 = acct({ login: function (a) { clearTimeout(t); busy(false); if (a.ok === '1') loggedIn(a.user || $('#user').value.trim(), a.sess); else st(a.err || 'Login failed.', 'bad'); } });
    $('#lf').onsubmit = function (e) {
      e.preventDefault(); var u = $('#user').value.trim(), p = $('#pass').value; if (!u || !p) { st('Type your account name and password.', 'bad'); return; }
      busy(true); st('Logging in...'); c3.send({ op: 'login', user: u, pass: p, chal: MPAuth.Client.chal(), srv: 'christe' });
      t = setTimeout(function () { busy(false); st('No answer from the account server. Try again.', 'bad'); }, 20000);
    };
  }
  function loggedIn(user, sess) {
    S.user = { user: user, at: Date.now(), sess: sess || '' }; ls('session', S.user); ls('lastUser', user); S.nick = user; ls('nick', user);
    closeAuth(); renderAvatar(); render(); toast('Signed in as ' + user);
    setTimeout(fetchAvatar, 300);
  }
  // 1.2: the Discord profile picture of the MP account (the bot's "avatar" answer, the same one the game shows):
  // {ok, n, p0..p(n-1) = base64 PNG, dname}; wait=1 means the bot is getting it from Discord and pushes it after
  var AVC = null;
  function fetchAvatar() {
    if (!S.user || !S.user.sess || !G.MPAuth || AVC) return;
    var stopT, me = S.user.user;
    AVC = new MPAuth.Client(function (a) {
      if (a.op !== 'avatar' && !(a.op === undefined && a.rid === 'a')) return;
      if (a.ok === '1' && a.wait === '1') return;
      clearTimeout(stopT); var c = AVC; AVC = null; if (c) c.stop();
      if (!S.user || S.user.user !== me) return;
      if (a.ok === '1' && (a.n | 0) > 0) {
        var b = ''; for (var i = 0; i < (a.n | 0); i++) b += a['p' + i] || '';
        S.user.avatar = 'data:image/png;base64,' + b; S.user.dname = a.dname || ''; S.user.avAt = Date.now();
      } else { S.user.avAt = Date.now(); if (a.relogin === '1') S.user.sess = ''; }
      ls('session', S.user); renderAvatar(); if (S.page === 'settings') render();
    });
    AVC.start(); AVC.send({ op: 'avatar', sess: S.user.sess, rid: 'a' });
    stopT = setTimeout(function () { if (AVC) { AVC.stop(); AVC = null; } }, 45000);
  }

  function renderAvatar() {
    var n = nick(), im = $('#avatarImg');
    $('#avatarName').textContent = S.user ? S.user.user : (n ? n : 'Guest');
    if (S.user && S.user.avatar) { im.className = 'av pic'; im.style.cssText = ''; im.innerHTML = '<img src="' + S.user.avatar + '" alt="">'; }
    else if (S.user || n) { im.className = 'av'; im.style.cssText = logo(n); im.textContent = initials(n); } else { im.className = 'av guest'; im.style.cssText = ''; im.innerHTML = ic('user'); }
    var nb = $('#nameInp'); if (nb && D.activeElement !== nb) { nb.value = n; nb.disabled = !!S.user; }
  }

  // ---------- window chrome: menus, tool bar, sidebar, bottom tabs ----------
  var MENUS = {
    file: [['Connect', 'connect'], ['Exit', 'exit']],
    view: [['Internet', 'v-internet'], ['Featured', 'v-featured'], ['Favorites', 'v-favorites'], ['Lan', 'v-lan'], ['Hosted (open.mp)', 'v-hosted'], ['Communities', 'v-explore']],
    servers: [['Connect', 'connect'], ['Add server (host:port)', 'add'], ['Add to favorites', 'fav'], ['Refresh', 'refresh'], ['Search Lan', 'lan']],
    tools: [['Play GTA SA / VC', 'v-game'], ['Storage', 's-storage'], ['Game settings', 's-game'], ['Settings', 's-account']],
    help: [['About AML Multiplayer', 'v-about'], ['MP Discord', 'discord'], ['MP YouTube', 'youtube']]
  };
  function menuAct(k) {
    if (k === 'connect') join(rowBy(S.sel)); else if (k === 'exit') X.exit();
    else if (k.indexOf('v-') === 0) go(k.slice(2)); else if (k.indexOf('s-') === 0) go('settings', k.slice(2));
    else if (k === 'add') addServer(); else if (k === 'fav') { if (S.sel) toggleFav(S.sel); }
    else if (k === 'refresh') { readInfo(); loadServers(); if (S.page === 'lan') scanLan(); if (S.page === 'coop') loadCoop(); }
    else if (k === 'lan') go('lan'), scanLan();
    else if (k === 'discord') X.open(DISCORD); else if (k === 'youtube') X.open(YOUTUBE);
  }
  function addServer() {
    dialog('Add server', '<input class="inp" id="adInp" placeholder="host:port" style="width:100%">', [['Cancel', ''], ['Add', 'go']], function (i) {
      if (i !== 1) return; var v = ($('#adInp').value || '').trim(); if (!/^[\w.-]+:\d{2,5}$/.test(v)) { toast('Type host:port'); return false; } addressRow(v);
    });
  }
  function bindShell() {
    $$('#nav a, #btabs a').forEach(function (a) { a.onclick = function () { if (a.dataset.ourl) { X.open(a.dataset.ourl); return; } go(a.dataset.page, a.dataset.set); }; });
    $$('.menus a').forEach(function (m) { m.onclick = function () {
      pop(m, MENUS[m.dataset.menu].map(function (x) { return '<div class="pi" data-m="' + x[1] + '">' + esc(x[0]) + '</div>'; }).join(''), function (P) { $$('[data-m]', P).forEach(function (x) { x.onclick = function () { P.classList.add('hide'); menuAct(x.dataset.m); }; }); }, true);
    }; });
    var qt; $('#q').oninput = function () {
      clearTimeout(qt); var v = this.value; qt = setTimeout(function () {
        S.q = v; if (/^[\w.-]+:\d{2,5}$/.test(v.trim())) { addressRow(v.trim()); return; }
        if (S.q && !LISTS[S.page] && S.page !== 'explore') S.page = 'internet';
        render(); pingVisible(); $('#q').focus();
      }, 220);
    };
    $('#nameInp').onchange = function () { if (!S.user) { setNick(this.value); this.value = nick(); toast('Name saved'); } };
    $('#refreshBtn').onclick = function () { menuAct('refresh'); };
    $('#tConnect').onclick = $('#connectBtn').onclick = function () { var r = rowBy(S.sel); if (r) join(r); else toast('Select a server first'); };
    $('#tAdd').onclick = addServer;
    $('#tFav').onclick = function () { if (S.sel) { toggleFav(S.sel); toast(S.favs[S.sel] ? 'Added to favorites' : 'Removed from favorites'); } };
    $('#tInfo').onclick = function () { go('about'); };
    $('#exitBtn').onclick = function () { X.exit(); };
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
        '<div class="pi" data-q="engine">' + ic('play') + 'GTA SA: inside AML Multiplayer' + '</div>' +
        '<div class="pi" data-q="region">' + ic('pin') + 'Region: ' + REGIONS[S.region] + '</div>' +
        '<div class="pi' + (S.autoRefresh ? ' on' : '') + '" data-q="auto">' + ic('refresh') + 'Auto refresh</div>' +
        '<div class="pi' + (ls('autoJoin') !== false ? ' on' : '') + '" data-q="join">' + ic('play') + 'Join from the title</div>' +
        '<div class="pi" data-q="all">' + ic('gear') + 'All settings</div>', function (P) {
        $$('[data-q]', P).forEach(function (x) { x.onclick = function () {
          var k = x.dataset.q; P.classList.add('hide');
          if (k === 'engine') go('settings', 'game'); else if (k === 'region') go('settings', 'network');
          else if (k === 'auto') { S.autoRefresh = !S.autoRefresh; ls('autoRefresh', S.autoRefresh); toast('Auto refresh ' + (S.autoRefresh ? 'on' : 'off')); }
          else if (k === 'join') { var on = ls('autoJoin') === false; ls('autoJoin', on); toast('Join from the title ' + (on ? 'on' : 'off')); }
          else go('settings', 'account');
        }; });
      });
    };
    $('#avatarBtn').onclick = function () {
      if (!S.user) { showAuth('login'); return; }
      pop(this, '<div class="ph2">' + esc(S.user.user) + '</div><div class="pi" data-p="set">' + ic('gear') + 'Account settings</div><div class="pi" data-p="moq">' + ic('users') + 'Open Moquil</div><div class="pi" data-p="out">' + ic('x') + 'Sign out</div>', function (P) {
        $$('[data-p]', P).forEach(function (x) { x.onclick = function () { P.classList.add('hide'); if (x.dataset.p === 'set') go('settings', 'account'); else if (x.dataset.p === 'moq') { if (!X.openApp('com.thirtyfox.moquil')) X.open('https://mpsamp-open.github.io/moquils-web'); } else act('logout'); }; });
      });
    };
  }
  function addressRow(a) {
    var hp = a.split(':'), r = { kind: 'omp', name: a, host: hp[0], port: hp[1] | 0, players: 0, max: 0, region: 0, mode: 'Direct', lang: '', locked: false, ping: -1, game: 'sa' };
    r.key = rowKey(r); if (!rowBy(r.key)) S.rows.unshift(r); S.favs[r.key] = 1; ls('favs', S.favs); S.sel = r.key; go('favorites');
  }

  // ---------- network / resume / back ----------
  G.CX_net = function (on) {
    S.online = on; var b = $('#netBar');
    b.className = 'netBar ' + (on ? 'on' : 'off'); b.innerHTML = on ? ic('globe') + 'Back online' : ic('wifi-off') + 'Lost connection. Try turning on Wi-Fi or mobile data.';
    if (on) { setTimeout(function () { b.classList.add('hide'); }, 2500); if (S.err) { S.err = ''; loadServers(); } }
    else if (!S.rows.length) { S.err = 'offline'; render(); }
  };
  if (!NATIVE) { G.addEventListener('online', function () { G.CX_net(true); }); G.addEventListener('offline', function () { G.CX_net(false); }); }
  function crashCheck() {
    if (!ENG.crash) return;
    var c = ENG.crash; ENG.crash = ''; call('crashSeen');
    dialog('GTA SA stopped', '<p>GTA SA inside AML Multiplayer closed with an error. Send this to the MP Discord so it can be fixed.</p><pre style="max-height:120px;overflow:auto;font-size:10px;white-space:pre-wrap;background:#0b0d11;padding:6px">' + esc(c) + '</pre>', [['OK', 'go']]);
  }
  G.CX_resume = function () { readInfo(); if (!LISTS[S.page]) render(); crashCheck(); };
  G.CX_back = function () {
    if (!$('#auth').classList.contains('hide')) { closeAuth(); return true; }
    if (!$('#dlg').classList.contains('hide')) { $('#dlg').classList.add('hide'); return true; }
    if (!$('#pop').classList.contains('hide')) { $('#pop').classList.add('hide'); return true; }
    if (S.page !== 'internet') { go('internet'); return true; }
    X.exit(); return false;
  };

  // ---------- boot = the MP loading screen (or date2/resources/startup.mp4) ----------
  function boot() {
    readInfo(); try { X.setup(); } catch (e) {}
    $('#verLbl').textContent = INFO.version || VERSION;
    var hp = /[#&]p=([\w-]+)/.exec(location.hash); if (hp) { var pg = hp[1]; if (/^set-/.test(pg)) { S.page = 'settings'; S.setTab = pg.slice(4); } else if (PAGES[pg]) { S.page = pg; if (pg === 'community') S.comm = COMMS[0]; } }
    if (/auth/.test(location.hash)) setTimeout(function () { showAuth('login'); }, 10);
    $('#regionLbl').textContent = REGIONS[S.region] || 'Auto';
    renderAvatar(); bindShell(); render();
    if (S.user && S.user.sess && (!S.user.avatar || Date.now() - (S.user.avAt || 0) > 864e5)) setTimeout(fetchAvatar, 1500);
    loadServers(); checkRelease();
    var B = $('#boot'), tip = 0, pct = 0;
    $('#bootTip').textContent = TIPS[0];
    var tipT = setInterval(function () { tip = (tip + 1) % TIPS.length; $('#bootTip').textContent = TIPS[tip]; }, 2400);
    var pctT = setInterval(function () { var cap = S.loaded || S.err ? 100 : 90; pct = Math.min(cap, pct + 3); $('#bootPct').textContent = pct; $('#bootBar').style.width = pct + '%'; }, 70);
    var done = false;
    function handoff() { if (done) return; done = true; clearInterval(tipT); clearInterval(pctT); $('#app').classList.remove('hide'); B.classList.add('out'); setTimeout(function () { B.remove(); crashCheck(); }, 650); }
    var vid = X.startupVideo && X.startupVideo();
    if (vid) {
      var V = $('#bootVid'); V.classList.remove('hide'); $('#bootArt').classList.add('hide'); V.src = vid;
      V.onended = handoff; V.onerror = handoff; V.play().catch(handoff); setTimeout(handoff, 20000);
      return;
    }
    if (ls('bootAnim') === false || /noboot/.test(location.hash)) { handoff(); return; }
    (function wait() { if (pct >= 100 || pct >= 90 && Date.now() - t0 > 6000) handoff(); else setTimeout(wait, 120); })();
  }
  var t0 = Date.now();
  setInterval(function () { if (S.autoRefresh && S.online && !D.hidden && LISTS[S.page] && S.page !== 'lan') loadServers(); }, 60000);
  if (D.readyState === 'loading') D.addEventListener('DOMContentLoaded', boot); else boot();
})(window, document);
