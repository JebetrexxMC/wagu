#!/usr/bin/env python3
"""MP NetWire dynamic wires -> stdout (wires.txt format).

Wires whose address changes or that come from a list, re-read every round:
  crossplay  MP Crossplay 24/7 (open.mp 1.6.7, playit tunnel)  mp247-data/omp247.json
  default    MP LAN Server (Broker MQQ) as seen in the MQTT lobby live-status/status.json
  featured   the 24/7 featured servers in the lobby          live-status/status.json
  custom     every other server in the MP lobby              live-status/status.json
  omp        the top N open.mp / SA-MP servers by players     api.open.mp/servers
Usage: sources.py [top_n]   (env OMP_TOP, default 20)
"""
import datetime, json, os, sys, urllib.request

RAW = "https://raw.githubusercontent.com/%s/%s/%s"
REPO = os.environ.get("REPO", "JebetrexxMC/wagu")
FEATURED = ["Brief Case War", "Call Of Duty: Modern Warfare 3", "Mega Vil Futuristic", "Project Day Z Legacy",
            "San Andreas Online League", "Rockstar", "MP Crossplay"]


def get(url, t=15):
    req = urllib.request.Request(url, headers={"User-Agent": "mpnet-sources/1"})
    with urllib.request.urlopen(req, timeout=t) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def clean(s, n=48):
    return "".join(c if c.isprintable() and c not in "|\n\r" else " " for c in str(s))[:n].strip() or "?"


def slug(s):
    o = "".join(c.lower() if c.isalnum() else "-" for c in s)
    while "--" in o: o = o.replace("--", "-")
    return o.strip("-")[:28] or "x"


def main():
    top = int(sys.argv[1]) if len(sys.argv) > 1 else int(os.environ.get("OMP_TOP", "20"))
    out = []
    try:
        x = get(RAW % (REPO, "mp247-data", "omp247.json"))
        if x.get("addr") and ":" in x["addr"]:
            out.append("crossplay | crossplay | samp | %s | %s | region=%s" % (x["addr"], clean(x.get("name", "MP Crossplay 24/7")), clean(x.get("region", "?"), 16)))
    except Exception as e:
        print("# crossplay: %s" % e)
    try:
        st = get(RAW % (REPO, "live-status", "status.json"))
        seen = set()
        # the lobby reading is made every 10 min by live-status.yml; older than 40 min = not trusted (offline)
        try: age = (datetime.datetime.now(datetime.timezone.utc) - datetime.datetime.fromisoformat(st["updated"])).total_seconds()
        except Exception: age = 0
        on = 1 if age < 2400 else 0
        for s in st.get("servers", []):
            name = clean(s.get("name", "?"))
            if name == "MP LAN Server": grp, wid = "default", "default"
            elif any(name.startswith(f) for f in FEATURED): grp, wid = "featured", "f-" + slug(name)
            else: grp, wid = "custom", "c-" + slug(name)
            # player-hosted (custom) servers come and go: list them only while the lobby reading is fresh,
            # a closed one is not a "down" wire (it made the custom group 0/N up, red)
            if grp == "custom" and not on: continue
            while wid in seen: wid += "x"
            seen.add(wid)
            disp = "Broker MQQ (MP LAN Server)" if grp == "default" else name
            out.append("%s | %s | lobby | lobby:%d | %s | online=%d players=%d max=%d" % (wid, grp, int(s.get("port", 0)), disp, on, int(s.get("players", 0)), int(s.get("max", 0))))
    except Exception as e:
        print("# lobby: %s" % e)
    try:
        lst = get("https://api.open.mp/servers", 20)
        lst.sort(key=lambda v: -int(v.get("pc", 0)))
        for i, v in enumerate(lst[:top]):
            if ":" not in v.get("ip", ""): continue
            out.append("omp-%02d | omp | samp | %s | %s | players=%s" % (i + 1, v["ip"], clean(v.get("hn", "?")), v.get("pc", 0)))
    except Exception as e:
        print("# open.mp list: %s" % e)
    print("\n".join(out))


if __name__ == "__main__":
    main()
