#!/usr/bin/env python3
"""MP NetWire: wires.json (from `mpnetd probe`) -> status files for the netwire-status branch.

  render.py OUT_DIR [previous_history.json]
writes OUT_DIR/
  history.json      last 288 rounds per wire (24 h at 5 min): [unix, grade letter, rtt]
  netwire.svg       README card (groups, up / total, best ping)
  badge-*.json      shields.io endpoint badges: wires, uptime, best, region, lobby, vps, omp, crossplay, featured, default
  README.md
"""
import json, os, sys, time

GROUPS = ["default", "featured", "crossplay", "region", "vps", "lobby", "emqx", "oracle", "xdp", "relay", "zerotier", "custom", "omp", "web"]
LETTER = {"green": "g", "yellow": "y", "red": "r", "down": "d", "skip": "s", "standby": "b"}
COLOR = {"g": "#3fb950", "y": "#d29922", "r": "#f85149", "d": "#6e7681", "s": "#6e7681", "b": "#58a6ff"}
# 5.02.72: region brokers and region VPS game ports are optional. When they don't answer the game moves to the
# public lobby (Mosquitto / HiveMQ / EMQX) and every server and player is still there and joinable, so a region
# wire that is down while the public lobby is up is "standby" (blue), not "down" (it doesn't count against uptime).
STANDBY_GROUPS = ("region", "vps")
FALLBACK_GROUP = "lobby"


def apply_standby(wires):
    fallback = any(w["group"] == FALLBACK_GROUP and w["grade"] not in ("down", "skip") for w in wires)
    n = 0
    if fallback:
        for w in wires:
            if w["group"] in STANDBY_GROUPS and w["grade"] == "down":
                w["grade"] = "standby"; w["standby"] = True; n += 1
    return n
KEEP = 288


def badge(out, name, label, msg, color):
    with open(os.path.join(out, "badge-%s.json" % name), "w") as f:
        json.dump({"schemaVersion": 1, "label": label, "message": msg, "color": color, "cacheSeconds": 300}, f)


def col_for(up, n):
    return "lightgrey" if n == 0 else ("brightgreen" if up == n else ("yellow" if up * 2 >= n else ("orange" if up else "red")))


def main():
    out = sys.argv[1]
    prev = sys.argv[2] if len(sys.argv) > 2 else os.path.join(out, "history.json")
    cur = json.load(open(os.path.join(out, "wires.json")))
    try: hist = json.load(open(prev))
    except Exception: hist = {}
    now = int(cur.get("updated", time.time()))
    wires = cur["wires"]
    standby = apply_standby(wires)
    cur["standby"] = standby
    if standby:   # mpnetd counted them as down: the page and badges only count what the game really needs
        cur["probed"] = max(0, int(cur.get("probed", len(wires))) - standby)
    for w in wires:
        h = hist.get(w["id"], [])
        h.append([now, LETTER.get(w["grade"], "d"), w["rtt"]])
        hist[w["id"]] = h[-KEEP:]
    ids = {w["id"] for w in wires}
    hist = {k: v for k, v in hist.items() if k in ids or (v and now - v[-1][0] < 86400)}
    json.dump(hist, open(os.path.join(out, "history.json"), "w"), separators=(",", ":"))

    # uptime per wire (24 h), added to wires.json so the page needs one file
    for w in wires:
        h = [x for x in hist.get(w["id"], []) if x[1] not in ("s", "b")]
        w["uptime"] = round(100.0 * sum(1 for x in h if x[1] != "d") / len(h), 1) if h else None
        w["spark"] = "".join(x[1] for x in hist.get(w["id"], [])[-48:])
    json.dump(cur, open(os.path.join(out, "wires.json"), "w"), indent=1)

    probed = [w for w in wires if w["grade"] not in ("skip", "standby")]
    up = [w for w in probed if w["grade"] != "down"]
    badge(out, "wires", "netwire", "%d/%d up" % (len(up), len(probed)) + (" · %d standby" % standby if standby else ""), col_for(len(up), len(probed)))
    ups = [w["uptime"] for w in probed if w["uptime"] is not None]
    avg = sum(ups) / len(ups) if ups else 0
    badge(out, "uptime", "uptime 24h", "%.1f%%" % avg, "brightgreen" if avg >= 99 else ("yellow" if avg >= 90 else "red"))
    live = [w for w in up if w["rtt"] is not None and w["kind"] in ("mqtt", "mp", "samp", "udp") and w["group"] != "omp"]
    if live:
        b = min(live, key=lambda w: w["rtt"])
        badge(out, "best", "best wire", "%s %d ms" % (b["id"], round(b["rtt"])), "brightgreen" if b["grade"] == "green" else "yellow")
    else:
        badge(out, "best", "best wire", "none up", "red")
    # mplatency badges: a grey "waiting" placeholder until render_latency.py writes the real ones, so the README
    # never shows shields.io's red "custom badge / resource not found" when mplatency did not finish this round
    badge(out, "latency", "mplatency", "waiting", "lightgrey")
    badge(out, "latency-jitter", "jitter", "waiting", "lightgrey")
    rows = []
    for g in GROUPS:
        gw = [w for w in wires if w["group"] == g]
        if not gw: continue
        sb = [w for w in gw if w["grade"] == "standby"]
        gu = [w for w in gw if w["grade"] not in ("down", "standby")]
        rt = [w["rtt"] for w in gu if w["rtt"] is not None]
        best = min(rt) if rt else None
        if sb and not gu:
            msg, col = "standby · public lobby in use", "blue"
        else:
            msg = "%d/%d up" % (len(gu), len(gw) - len(sb)) + (" · %d ms" % round(best) if best is not None else "") + (" · %d standby" % len(sb) if sb else "")
            col = col_for(len(gu), len(gw) - len(sb))
        badge(out, g, g, msg, col)
        lob = all(w["kind"] == "lobby" for w in gw)
        rows.append((g, len(gu), len(gw), best, sum(w.get("players", 0) for w in gw) if lob else None, len(sb)))

    # SVG card
    W, rh = 520, 22
    H = 58 + rh * len(rows)
    s = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" font-family="Segoe UI,Helvetica,Arial,sans-serif">' % (W, H, W, H),
         '<rect width="%d" height="%d" rx="10" fill="#0d1117" stroke="#30363d"/>' % (W, H),
         '<text x="16" y="26" fill="#e6edf3" font-size="15" font-weight="700">MP NetWire</text>',
         '<text x="%d" y="26" fill="#8b949e" font-size="11" text-anchor="end">%d/%d wires up · %s UTC</text>' % (W - 16, len(up), len(probed), time.strftime("%Y-%m-%d %H:%M", time.gmtime(now)))]
    y = 50
    for g, u, n, best, lp, sb in rows:
        c = "#58a6ff" if sb and not u else ("#3fb950" if u == n - sb else ("#d29922" if u else "#f85149"))
        s.append('<circle cx="22" cy="%d" r="5" fill="%s"/>' % (y - 4, c))
        s.append('<text x="34" y="%d" fill="#e6edf3" font-size="12">%s</text>' % (y, g))
        s.append('<text x="150" y="%d" fill="#8b949e" font-size="12">%s</text>' % (y, "standby" if sb and not u else "%d/%d up" % (u, n - sb)))
        s.append('<text x="230" y="%d" fill="#8b949e" font-size="12">%s</text>' % (y, ("best %d ms" % round(best)) if best is not None else ("in MP lobby, %d players" % lp if lp is not None else ("public lobby in use" if sb else "-"))))
        # 24 h strip of the best wire of the group
        gw = [w for w in wires if w["group"] == g]
        strip = max((w["spark"] for w in gw), key=lambda sp: sp.count("g") + sp.count("y") + sp.count("b") * 0.5, default="")[-24:]
        for i, ch in enumerate(strip):
            s.append('<rect x="%d" y="%d" width="2" height="10" fill="%s"/>' % (436 + i * 3, y - 9, COLOR.get(ch, "#6e7681")))
        y += rh
    s.append("</svg>")
    open(os.path.join(out, "netwire.svg"), "w").write("\n".join(s))
    with open(os.path.join(out, "README.md"), "w") as f:
        f.write("MP NetWire live status, made every 5 minutes by `.github/workflows/netwire-247.yml` on main "
                "(probed from a GitHub runner: pings are from GitHub's data centre, not from your phone).\n\n"
                "Files: wires.json (every wire + grade + ping + 24 h uptime), history.json, netwire.svg, badge-*.json (shields.io endpoint badges), mplatency.json + mplatency.svg (go/mplatency: avg / jitter / loss / location).\n")


if __name__ == "__main__":
    main()
