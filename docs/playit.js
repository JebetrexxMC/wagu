// 5.02.50: MP Tunnel console - web Termux for the playit.gg PC tunnel of MP Crossplay 24/7.
// The playit agent (playit-linux-amd64 2.0.2, release v5.02.30) runs on the 24/7 host, not on your
// computer; this page shows it live (MQTT retained thirtyfox/mp5/playit, + playit.json on mp247-data)
// and the MP Bot types the checks by itself. Account link: the claim link it prints -> Accept on playit.gg.
(function () {
  'use strict';
  var BROKERS = ['wss://broker.hivemq.com:8884/mqtt', 'wss://broker.emqx.io:8084/mqtt', 'wss://test.mosquitto.org:8081/'];
  var TOPIC = 'thirtyfox/mp5/playit';
  var RAWJ = 'https://raw.githubusercontent.com/JebetrexxMC/wagu/mp247-data/playit.json';
  var REL = 'https://github.com/JebetrexxMC/wagu/releases/download/v5.02.30/';
  var MPREL = 'https://github.com/JebetrexxMC/wagu/releases/download/playit-mp/';
  var OMPJ = 'https://raw.githubusercontent.com/JebetrexxMC/wagu/mp247-data/omp247.json', omp = null;
  var BINS = ['playit-linux-amd64', 'playit-linux-aarch64', 'playit-linux-armv7', 'playit-linux-i686'];
  var $ = function (id) { return document.getElementById(id); };
  var out = $('out'), stick = window.MPAutoScroll(out);
  var st = null, conns = [], seenState = '', seenClaim = '', seenAddr = '', seenUpd = 0;

  function esc(x) { return String(x).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function print(html) { var d = document.createElement('div'); d.innerHTML = html; out.appendChild(d); while (out.childNodes.length > 400) out.removeChild(out.firstChild); stick(); }
  function p(cls, t) { print('<span class="' + cls + '">' + esc(t) + '</span>'); }
  function link(url, label) { return /^https:\/\/(playit\.gg|github\.com|mpsamp-open\.github\.io)\//.test(url) ? '<a href="' + esc(url) + '" target="_blank" rel="noopener">' + esc(label || url) + '</a>' : esc(url); }
  function ago(t) { var s = Math.max(0, Math.round(Date.now() / 1000 - t)); return s < 90 ? s + ' s' : s < 5400 ? Math.round(s / 60) + ' min' : Math.round(s / 3600) + ' h'; }
  function fresh() { return st && st.updated && Date.now() / 1000 - st.updated < 1800; }

  function take(o) { if (o && o.updated && (!st || o.updated >= st.updated)) { st = o; live(); } }
  function connect(url) {
    if (typeof mqtt === 'undefined') return;
    var c = mqtt.connect(url, { clientId: 'webplayit' + Math.random().toString(16).slice(2, 12), clean: true, connectTimeout: 8000, reconnectPeriod: 5000, keepalive: 30, protocolVersion: 4 });
    c.on('connect', function () { c.subscribe(TOPIC); live(); });
    c.on('close', live); c.on('error', function () {});
    c.on('message', function (t, pl) { try { take(JSON.parse(pl.toString())); } catch (e) {} });
    conns.push(c);
  }
  function pullOmp() { fetch(OMPJ + '?t=' + Date.now(), { cache: 'no-store' }).then(function (r) { return r.ok ? r.json() : null; }).then(function (o) { if (o) omp = o; }).catch(function () {}); }
  function pull() { pullOmp(); return fetch(RAWJ + '?t=' + Date.now(), { cache: 'no-store' }).then(function (r) { return r.ok ? r.json() : null; }).then(take).catch(function () {}); }
  function live() {
    var on = conns.filter(function (c) { return c.connected; }).length;
    var s = !st ? 'no data yet' : !fresh() ? 'host offline (last ' + ago(st.updated) + ' ago)' : st.state + (st.addr ? ' · ' + st.addr : '');
    $('live').className = 'live ' + (!st || !fresh() ? 'bad' : st.state === 'online' ? 'ok' : 'warn');
    $('live-t').textContent = (on ? on + ' broker' + (on === 1 ? '' : 's') : 'connecting…') + ' · tunnel: ' + s;
  }

  var CMDS = {
    'curl -fsSL https://packages.playit.gg/install.sh | bash': function () {
      p('m', 'playit installer for Debian / Ubuntu - already done on the 24/7 host:');
      p('g', '  agent: ' + (st && st.agent ? st.agent : 'MP build of playit 2.0.2 (release playit-mp), else release v5.02.30'));
      p('m', '  runs as server/playit_agent.py next to the open.mp crossplay server - nothing to download on a computer');
    },
    'playit status': function () {
      if (!st) { p('y', 'no tunnel status yet - it appears when the next 24/7 shift starts the agent'); return; }
      p(fresh() ? 'g' : 'r', 'agent   ' + st.agent + ' · ' + (fresh() ? st.state : 'host offline') + ' · updated ' + ago(st.updated) + ' ago');
      p('m', 'tunnel  UDP ' + (st.addr || '(no address yet)') + ' -> 127.0.0.1:' + st.port + ' (open.mp PC port)');
      if (st.note) p('c', 'note    ' + st.note);
      if (st.state === 'claim' && st.claim_url && (!st.expires || st.expires > Date.now() / 1000)) print('<span class="y">account </span>' + link(st.claim_url) + ' <span class="m">- log in / sign up at playit.gg, press Accept</span>');
    },
    'playit claim': function () {
      if (st && st.state === 'claim' && st.claim_url && fresh() && (!st.expires || st.expires > Date.now() / 1000)) {
        var left = st.expires ? Math.max(0, Math.round(st.expires - Date.now() / 1000)) : 0;
        p('y', 'Link your playit.gg account (one time)' + (left ? ' - this link ends in ' + Math.floor(left / 60) + ':' + ('0' + left % 60).slice(-2) : '') + ':');
        var code = st.claim_url.split('/').pop();
        print('  <span class="g">Claim Code: ' + esc(code) + '</span>  <span class="m">(playit.gg new agent wizard -> Third party / any option -> "Enter claim code")</span>');
        print('  1. open ' + link(st.claim_url) + ' <span class="m">or paste the claim code on playit.gg</span>');
        p('m', '  2. log in or make a free account at playit.gg');
        p('m', '  3. press Accept - the agent key is saved encrypted (AUTH_PRIVATE_KEY) for every next shift');
        p('m', '  4. the UDP tunnel to 127.0.0.1:7778 is made by itself; its address shows up here');
      } else if (st && st.state === 'online') p('g', 'account already linked - tunnel online: ' + st.addr);
      else p('m', 'no link on show (' + (st ? (fresh() ? st.state : 'host offline') : 'no data') + ') - press "Link playit account" for a ' + ((st && st.link_min) || 5) + ' min link');
    },
    'playit tunnels': function () {
      var ts = (st && st.tunnels) || [];
      if (!ts.length) { p('m', 'no tunnels listed yet'); return; }
      ts.forEach(function (t) { p(t.disabled ? 'r' : 'g', '  ' + (t.proto || '?') + '  ' + (t.addr || '-') + '  -> ' + t.local + '  ' + (t.name || '') + (t.disabled ? '  (disabled)' : '')); });
    },
    'tail -n 12 logs/playit.log': function () {
      var l = (st && st.log) || [];
      if (!l.length) { p('m', '(empty)'); return; }
      l.forEach(function (x) { p(/WARN|ERROR/.test(x) ? 'y' : 'm', x); });
    },
    'playit download': function () {
      p('m', 'our own playit agent, built from source (github.com/JebetrexxMC/playit-agent), release playit-mp:');
      ['mp-playitd-linux-amd64', 'mp-playitd-linux-aarch64', 'mp-playit-cli-linux-amd64', 'mp-playit-cli-linux-aarch64'].forEach(function (b) { print('  ' + link(MPREL + b, b)); });
      p('m', 'playit 2.0.2 builds (release v5.02.30), for your own Linux box / VPS / Termux:');
      BINS.forEach(function (b) { print('  ' + link(REL + b, b)); });
      p('m', '  or: curl -fsSL https://packages.playit.gg/install.sh | bash');
    },
    'join': function () {
      if (omp && omp.addr && omp.tunnel === 'frp') { p('g', 'PC players: samp://' + omp.addr + '  ·  omp://' + omp.addr); return; }
      if (st && st.addr) { p('g', 'PC players: samp://' + st.addr + '  ·  omp://' + st.addr); print('  ' + link('https://mpsamp-open.github.io/join.html?addr=' + encodeURIComponent(st.addr), 'one-tap join page')); }
      else p('m', 'no tunnel address yet');
    },
    'frp status': function () {   // 5.02.52: our own open-source tunnel (frp) on the HiveMQ VPS
      if (omp && omp.tunnel === 'frp') p('g', 'frp tunnel: UDP ' + omp.addr + ' -> 127.0.0.1:' + omp.port + ' (own server, ' + (omp.online ? 'online' : 'offline') + ')');
      else p('m', 'frp tunnel: not set up' + (omp ? ' (now: ' + (omp.tunnel || 'none') + ')' : ''));
      p('m', '  own server, on the HiveMQ VPS as root: curl -fsSL https://mpsamp-open.github.io/HiveMQ/frps-setup.sh | sudo bash');
      p('m', '  then repo secret FRP_TOKEN + variable FRP_SERVER=<vps>:7000 - frp is used first, playit only with PLAYIT=on');
    },
    'clear': function () { out.innerHTML = ''; }
  };
  function run(line) { print('<span class="g">~ $</span> ' + esc(line)); var f = CMDS[line]; if (!f) { p('r', line.split(' ')[0] + ': command not found'); return; } return f(); }

  var boot = ['curl -fsSL https://packages.playit.gg/install.sh | bash', 'playit download', 'playit status'];
  var rot = ['playit status', 'tail -n 12 logs/playit.log', 'frp status', 'playit tunnels', 'playit status', 'join'], ri = 0, n = 0;
  function next() {
    if (boot.length) return { cmd: boot.shift(), why: 'start-up check' };
    if (st) {
      if (st.claim_url && st.claim_url !== seenClaim && st.state === 'claim' && (!st.expires || st.expires > Date.now() / 1000)) { seenClaim = st.claim_url; return { cmd: 'playit claim', why: 'account link waiting' }; }
      if (st.addr && st.addr !== seenAddr) { seenAddr = st.addr; return { cmd: 'join', why: 'tunnel address ' + st.addr }; }
      if (st.state !== seenState) { seenState = st.state; return { cmd: 'playit status', why: 'tunnel: ' + st.state }; }
      if (st.updated !== seenUpd && n % 3 === 0) { seenUpd = st.updated; n++; return { cmd: 'tail -n 12 logs/playit.log', why: 'new agent log' }; }
    }
    if (++n % 12 === 0) return { cmd: 'clear', why: 'keeping the screen short', wait: 600 };
    return { cmd: rot[ri++ % rot.length], why: 'routine check' };
  }

  var asked = 0;
  $('linkbtn').addEventListener('click', function () {
    var on = conns.filter(function (c) { return c.connected; });
    if (!on.length) { p('r', 'not connected to a broker yet - try again in a few seconds'); return; }
    if (st && st.state === 'online') { p('g', 'account already linked - tunnel online: ' + st.addr); return; }
    var t = Math.floor(Date.now() / 1000);
    on.forEach(function (c) { c.publish(TOPIC + '/req', JSON.stringify({ t: t }), { retain: true, qos: 0 }); });
    asked = Date.now(); this.disabled = true; var b = this; setTimeout(function () { b.disabled = false; }, 60000);
    p('y', 'asked the 24/7 host for a playit.gg link (ends after ' + ((st && st.link_min) || 5) + ' min) - it shows here in about 10 s');
  });
  print('<span class="g">Welcome to MP Tunnel!</span>  <span class="m">(web Termux for the playit.gg PC tunnel)</span>');
  print('<span class="m">Agent:  </span>playit 2.0.2 runs on the 24/7 host - this page only watches it');
  print('<span class="m">Bot:    </span>MP Bot types and presses Enter by itself');
  print('');
  BROKERS.forEach(connect); pull(); live();
  setInterval(pull, 60000); setInterval(live, 3000);
  MPBot({ out: out, line: $('typed'), keyEnter: $('kenter'), status: $('botst'), run: run, next: next, startMs: 1200 });
})();
