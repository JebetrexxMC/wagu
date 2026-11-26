#!/usr/bin/env python3
"""Render go/mplatency results for the README (netwire-status branch).

usage: render_latency.py mplatency.json OUT_DIR

Writes into OUT_DIR:
  mplatency.json             the raw result (every wire: status, avg / min / max / jitter / loss, location)
  mplatency.svg              README card: one line per wire, sorted by group then average ping
  badge-latency.json         shields.io endpoint badge: "<up>/<total> up, median <ms>"
  badge-latency-jitter.json  shields.io endpoint badge: median jitter of the online wires
"""
import html
import json
import os
import shutil
import statistics
import sys

COL = {"green": "#3fb950", "yellow": "#d29922", "red": "#f85149", "down": "#6e7681", "standby": "#58a6ff"}
# 5.02.72: region / vps wires that don't answer while the public lobby is up are STANDBY (mplatency ApplyStandby):
# the game carries on over the public lobby, so they are shown blue and left out of the up / total count.
STANDBY_GROUPS, FALLBACK_GROUP = ("region", "vps"), "lobby"


def apply_standby(rows):   # same rule as mplatency, for JSON written by an older mplatency
    if any(r.get("group") == FALLBACK_GROUP and r.get("status") == "ONLINE" for r in rows):
        for r in rows:
            if r.get("group") in STANDBY_GROUPS and r.get("status") == "OFFLINE":
                r["status"], r["grade"] = "STANDBY", "standby"


def badge(out, name, label, msg, color):
    with open(os.path.join(out, "badge-%s.json" % name), "w") as f:
        json.dump({"schemaVersion": 1, "label": label, "message": msg, "color": color}, f)


def ms(v):
    return "-" if v is None or v < 0 else ("%.0f" % v if v >= 10 else "%.1f" % v)


def cut(t, n):
    return t if len(t) <= n else t[:n - 1].rstrip() + "\u2026"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    doc = json.load(open(src))
    rows = doc.get("targets") or []
    if not rows:
        sys.exit("no targets in %s" % src)
    os.makedirs(out, exist_ok=True)
    if os.path.abspath(src) != os.path.abspath(os.path.join(out, "mplatency.json")):
        shutil.copyfile(src, os.path.join(out, "mplatency.json"))

    apply_standby(rows)
    sb = [r for r in rows if r.get("status") == "STANDBY"]
    up = [r for r in rows if r.get("status") == "ONLINE"]
    avgs = [r["stats"]["avg_ms"] for r in up if r["stats"].get("avg_ms", 0) > 0]
    jits = [r["stats"]["jitter_ms"] for r in up]
    med = statistics.median(avgs) if avgs else None
    need = max(1, len(rows) - len(sb))
    share = len(up) / need
    color = "brightgreen" if share >= 0.9 else "yellow" if share >= 0.6 else "red"
    badge(out, "latency", "mplatency", "%d/%d up, median %s ms" % (len(up), need, ms(med)) + (", %d standby" % len(sb) if sb else ""), color)
    badge(out, "latency-jitter", "jitter", "%s ms" % ms(statistics.median(jits) if jits else None),
          "brightgreen" if jits and statistics.median(jits) < 10 else "yellow")

    rows = sorted(rows, key=lambda r: (r.get("group", ""), r.get("status") != "ONLINE",
                                       r["stats"].get("avg_ms") or 1e9))
    W, top, lh = 860, 58, 20
    H = top + lh * len(rows) + 30
    x = [16, 34, 300, 370, 440, 510, 575, 640]
    s = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
         'font-family="Segoe UI,Helvetica,Arial,sans-serif">' % (W, H, W, H),
         '<rect width="100%" height="100%" rx="10" fill="#0d1117" stroke="#30363d"/>',
         '<text x="16" y="26" fill="#e6edf3" font-size="16" font-weight="600">MP NetWire latency (mplatency)</text>',
         '<text x="%d" y="26" fill="#8b949e" font-size="12" text-anchor="end">%d/%d up%s, %d samples each, %s UTC, from GitHub Actions</text>'
         % (W - 16, len(up), len(rows) - len(sb), (", %d standby" % len(sb)) if sb else "", max((r["stats"].get("samples", 0) for r in rows), default=0),
            html.escape(doc.get("time", "")[:16].replace("T", " ")))]
    hdr = ["", "wire", "avg ms", "min", "max", "jitter", "loss", "location"]
    for i, h in enumerate(hdr):
        s.append('<text x="%d" y="48" fill="#8b949e" font-size="11">%s</text>' % (x[i], h))
    y = top
    for r in rows:
        st = r["stats"]
        on = r.get("status") == "ONLINE"
        off = "standby" if r.get("status") == "STANDBY" else "down"
        fg = "#e6edf3" if on else "#8b949e"
        name = "%s / %s" % (r.get("group", ""), r.get("name") or r.get("id"))
        loc = (r.get("location") or "").split(" (")[0]  # city, country (ISP left out: no room)
        cells = [html.escape(cut(name, 40)),
                 ms(st.get("avg_ms")) if on else off, ms(st.get("min_ms")) if on else "-",
                 ms(st.get("max_ms")) if on else "-", ms(st.get("jitter_ms")) if on else "-",
                 "%.0f%%" % st.get("loss_pct", 0), html.escape(cut(loc, 30))]
        s.append('<circle cx="%d" cy="%d" r="5" fill="%s"/>' % (x[0] + 5, y + 9, COL.get(r.get("grade"), "#6e7681")))
        for i, c in enumerate(cells, 1):
            s.append('<text x="%d" y="%d" fill="%s" font-size="12">%s</text>' % (x[i], y + 13, fg, c))
        y += lh
    s.append('<text x="16" y="%d" fill="#8b949e" font-size="11">green &lt;120 ms, yellow &lt;250 ms, red slower or lossy, '
             'blue standby (public lobby in use), grey down. Pings are from US / EU runners, not from a phone.</text>' % (H - 10))
    s.append("</svg>")
    open(os.path.join(out, "mplatency.svg"), "w").write("\n".join(s))
    print("mplatency: %d/%d up, %d standby, median %s ms" % (len(up), len(rows) - len(sb), len(sb), ms(med)))


if __name__ == "__main__":
    main()
