// 5.00.05: keeps this app's MP login (user, saved login, device id, settings) in the app's own Android storage
// too, so it is not lost when the WebView storage is reset (WebView update, storage cleared by the system).
// Restored when localStorage lost it. "Delete local data" / Logout still delete it (they go through here too).
// On the website (no Android bridge) it only tells mpauth.js which app this is.
(function (G) {
  var B = G.Moquil || G.MPTDE;
  G.MPAUTH_APP = G.Moquil ? 'moquil' : G.MPTDE ? 'tde' : 'web';
  if (!B || typeof B.keepGet !== 'function' || typeof B.keepSet !== 'function') return;
  var KEYS = ['mptde_user', 'mptde_remember', 'mptde_last', 'mptde_sess', 'mp_auth_dev', 'moq_session', 'moq_last_user', 'moq_key', 'moq_settings'];
  var PRE = ['moq_state_'];
  function watched(k) { if (KEYS.indexOf(k) >= 0) return true; for (var i = 0; i < PRE.length; i++) if (k.indexOf(PRE[i]) === 0) return true; return false; }
  var S = G.localStorage, P = G.Storage && G.Storage.prototype;
  if (!S || !P) return;
  var get = P.getItem, set = P.setItem, rem = P.removeItem, clr = P.clear, key = P.key;
  try {   // restore what localStorage lost
    var o = JSON.parse(B.keepGet() || '{}') || {};
    Object.keys(o).forEach(function (k) { if (watched(k) && get.call(S, k) === null && typeof o[k] === 'string') set.call(S, k, o[k]); });
  } catch (e) {}
  var last = null, t = null;
  function snap() {
    t = null;
    var o = {};
    try { for (var i = 0; i < S.length; i++) { var k = key.call(S, i); if (k && watched(k)) o[k] = get.call(S, k); } } catch (e) { return; }
    var j = JSON.stringify(o);
    if (j !== last) { last = j; try { B.keepSet(j); } catch (e) {} }
  }
  function later() { if (!t) t = setTimeout(snap, 300); }
  P.setItem = function (k, v) { var r = set.call(this, k, v); if (this === S && watched(String(k))) later(); return r; };
  P.removeItem = function (k) { var r = rem.call(this, k); if (this === S && watched(String(k))) snap(); return r; };
  P.clear = function () { var r = clr.call(this); if (this === S) snap(); return r; };
  G.addEventListener('pagehide', snap);
  G.document.addEventListener('visibilitychange', function () { if (G.document.visibilityState === 'hidden') snap(); });
  snap();
})(window);
