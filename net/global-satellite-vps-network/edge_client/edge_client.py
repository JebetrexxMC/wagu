"""MP edge client: picks the fastest continental VPS by real RTT and fails over when it stops answering.

Runs anywhere Python 3.8+ runs (PC, Termux on the phone, a Raspberry Pi) - standard library only.

  python3 edge_client.py --center http://center:8000                       # keep running, print changes
  python3 edge_client.py --center http://center:8000 --once                # one round, JSON
  python3 edge_client.py --seed http://vps-eu:8002 --seed http://vps-asia:8003   # without the center
  python3 edge_client.py --center ... --wires-out mp_wires.txt             # the chosen VPS as NetWire lines

How it picks (the same idea as linux-dual-wan-failover / FailOver-Routing, applied to VPS instead of WAN links):
  * the list comes from the global center (/global/regions), else from any known VPS (/peers), else --seed;
  * every VPS gets SAMPLES timed GET /ping on one kept-alive connection; the score is the median RTT
    plus a load penalty;
  * the active VPS changes only when another one is better by --margin ms (no flapping), or when the active one
    failed --fail rounds in a row (failover);
  * a median RTT above 480 ms to the best VPS = most likely a GEO satellite link: MP should then use the
    region with the lowest RTT and a longer interpolation (reported as link=geo-satellite).
"""
import argparse
import http.client
import json
import os
import socket
import statistics
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

CLIENT_ID = f"{socket.gethostname()}-{os.getpid()}"


def get_json(url, timeout=4.0, data=None):
    req = urllib.request.Request(url, data=json.dumps(data).encode() if data is not None else None,
                                 headers={"Content-Type": "application/json"} if data is not None else {})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def probe(url, samples=4, timeout=2.0):
    """median / min / jitter of GET /ping in ms on one kept-alive connection (the first sample warms it up)"""
    u = urllib.parse.urlparse(url)
    conn_cls = http.client.HTTPSConnection if u.scheme == "https" else http.client.HTTPConnection
    c = conn_cls(u.hostname, u.port or (443 if u.scheme == "https" else 80), timeout=timeout)
    rtts = []
    try:
        for i in range(samples + 1):
            t0 = time.perf_counter()
            c.request("GET", (u.path.rstrip("/") or "") + "/ping")
            c.getresponse().read()
            if i:
                rtts.append((time.perf_counter() - t0) * 1000.0)
    except (OSError, http.client.HTTPException):
        pass
    finally:
        c.close()
    if not rtts:
        return None
    return {"median": round(statistics.median(rtts), 1), "min": round(min(rtts), 1),
            "jitter": round(statistics.pstdev(rtts), 1) if len(rtts) > 1 else 0.0, "ok": len(rtts), "of": samples}


class Edge:
    def __init__(self, a):
        self.a = a
        self.known = {s.rstrip("/"): {"url": s.rstrip("/"), "continent": "?", "game": []} for s in a.seed}
        self.active = None
        self.fails = 0
        self.source = "seed"

    def refresh(self):
        lst = None
        if self.a.center:
            try:
                lst = get_json(self.a.center.rstrip("/") + "/global/regions")["vps"]
                self.source = "center"
            except Exception:
                lst = None
        if lst is None:   # center down: ask the VPS we know (they cache the peer list)
            for url in ([self.active] if self.active else []) + list(self.known):
                try:
                    lst = get_json(url + "/peers")["peers"]
                    self.source = "peers of " + url
                    break
                except Exception:
                    continue
        for r in lst or []:
            if r.get("alive", True):
                self.known[r["url"].rstrip("/")] = r

    def round(self):
        self.refresh()
        urls = list(self.known)
        with ThreadPoolExecutor(max_workers=max(1, min(16, len(urls)))) as ex:
            res = dict(zip(urls, ex.map(lambda u: probe(u, self.a.samples), urls)))
        rows = []
        for u, p in res.items():
            r = self.known[u]
            load = r.get("load", 0.0) or 0.0
            score = None if p is None else round(p["median"] + 100.0 * load ** 2 + (0.5 * p["jitter"]), 1)
            rows.append({"url": u, "continent": r.get("continent", "?"), "game": r.get("game", []), "rtt": p,
                         "load": load, "score": score})
        ok = sorted((r for r in rows if r["score"] is not None), key=lambda r: r["score"])
        cur = next((r for r in rows if r["url"] == self.active), None)
        event = None
        if cur is not None and cur["score"] is None:
            self.fails += 1
        else:
            self.fails = 0
        if ok:
            best = ok[0]
            if self.active is None:
                event = "pick"
            elif self.fails >= self.a.fail:
                event = f"failover ({self.fails} failed rounds)"
            elif cur is not None and cur["score"] is not None and best["url"] != self.active and \
                    best["score"] + self.a.margin < cur["score"]:
                event = f"better by {round(cur['score'] - best['score'], 1)} ms"
            if event:
                self.active, self.fails = best["url"], 0
        act = next((r for r in rows if r["url"] == self.active), None)
        if act and act["rtt"]:
            try:
                get_json(self.active + "/edge/hello", data={"client": CLIENT_ID, "rtt_ms": act["rtt"]["median"]})
            except Exception:
                pass
        link = "unknown"
        if ok:
            m = ok[0]["rtt"]["median"]
            link = "geo-satellite" if m > 480 else "high-latency (LEO satellite / cellular?)" if m > 120 else "terrestrial"
        return {"t": time.strftime("%H:%M:%S"), "source": self.source, "event": event, "active": act, "link": link,
                "ranked": [{"url": r["url"], "continent": r["continent"], "score": r["score"],
                            "rtt": r["rtt"]["median"] if r["rtt"] else None} for r in ok] +
                          [{"url": r["url"], "continent": r["continent"], "score": None, "rtt": None}
                           for r in rows if r["score"] is None]}

    def wires(self, out):
        act = next((r for r in self.known.values() if r["url"].rstrip("/") == self.active), None)
        lines = ["# MP edge client: the VPS this device uses now (net/wires.txt format)"]
        for i, g in enumerate((act or {}).get("game", [])):
            lines.append(f"edge-{g['kind']}{i} | vps | {g['kind']} | {g['host']}:{g['port']} | "
                         f"{act.get('continent', '?')} VPS (edge pick) | region={act.get('continent', '?')}")
        tmp = out + ".tmp"
        with open(tmp, "w") as f:
            f.write("\n".join(lines) + "\n")
        os.replace(tmp, out)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--center", help="global center base URL, e.g. http://center:8000")
    p.add_argument("--seed", action="append", default=[], help="a VPS base URL (repeat); used when the center is down")
    p.add_argument("--interval", type=float, default=10.0)
    p.add_argument("--samples", type=int, default=4)
    p.add_argument("--fail", type=int, default=2, help="failed rounds before failover")
    p.add_argument("--margin", type=float, default=20.0, help="ms another VPS must be better by to switch")
    p.add_argument("--once", action="store_true")
    p.add_argument("--wires-out")
    a = p.parse_args()
    if not a.center and not a.seed:
        p.error("give --center and/or --seed")
    e = Edge(a)
    while True:
        r = e.round()
        if a.wires_out and e.active:
            e.wires(a.wires_out)
        if a.once:
            print(json.dumps(r, indent=1))
            return 0 if r["active"] else 1
        if r["event"]:
            act = r["active"]
            print(f"{r['t']} {r['event']}: {act['continent']} {act['url']} rtt {act['rtt']['median']} ms "
                  f"game {[g['host'] + ':' + str(g['port']) for g in act['game']]} link {r['link']} (list: {r['source']})",
                  flush=True)
        elif not r["active"]:
            print(f"{r['t']} no VPS answers (list: {r['source']})", flush=True)
        time.sleep(a.interval)


if __name__ == "__main__":
    sys.exit(main())
