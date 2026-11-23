"""MP global center: every continental VPS reports here; edge clients ask it which VPS to use.

  POST /global/sync           a continental VPS reports itself (every 5 s); the answer is the list of all VPS (peers)
  GET  /global/regions        every VPS, alive or not (alive = reported in the last STALE_S seconds)
  GET  /global/best           ?lat=&lon= or ?continent=ASIA -> alive VPS ranked by distance + load (edge clients
                              still measure the real RTT themselves; this is the first guess)
  GET  /global/wires.txt      the alive VPS as MP NetWire lines (net/wires.txt format) for mpnetd / the status page
  GET  /health

Env: PORT (8000), STALE_S (20), GLOBAL_TOKEN (optional: VPS must send X-MP-Token).
"""
import math
import os
import time
from typing import Dict, List, Optional

import uvicorn
from fastapi import FastAPI, Header, HTTPException, Query
from fastapi.responses import PlainTextResponse
from pydantic import BaseModel, Field

STALE_S = float(os.environ.get("STALE_S", "20"))
TOKEN = os.environ.get("GLOBAL_TOKEN", "")

# rough centre of each continent's players (used when a VPS doesn't send its own lat/lon)
CENTROIDS = {"NA": (39.0, -98.0), "SA": (-15.0, -55.0), "EU": (50.0, 10.0), "AF": (2.0, 20.0),
             "ASIA": (14.0, 110.0), "OC": (-28.0, 140.0), "ME": (25.0, 45.0)}


class GameEndpoint(BaseModel):
    kind: str = "mp"            # mp | samp | mqtt | tls | udp (net/wires.txt kinds)
    host: str
    port: int


class VpsReport(BaseModel):
    continent: str
    url: str                    # base URL edge clients reach this VPS on (http://host:port)
    lat: Optional[float] = None
    lon: Optional[float] = None
    game: List[GameEndpoint] = Field(default_factory=list)
    players: int = 0
    max_players: int = 100
    uplink: str = "fiber"       # fiber | leo | geo | cellular (how this VPS itself is connected)
    center_rtt_ms: Optional[float] = None
    version: str = ""


app = FastAPI(title="MP global center")
vps: Dict[str, dict] = {}       # key: continent + url


def _alive(r: dict) -> bool:
    return time.time() - r["seen"] < STALE_S


def _pos(r: dict):
    if r.get("lat") is not None and r.get("lon") is not None:
        return r["lat"], r["lon"]
    return CENTROIDS.get(r["continent"].upper(), (0.0, 0.0))


def _km(a, b) -> float:
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 6371.0 * 2 * math.asin(math.sqrt(h))


def _public(r: dict) -> dict:
    out = {k: v for k, v in r.items() if k != "seen"}
    out["age_s"] = round(time.time() - r["seen"], 1)
    out["alive"] = _alive(r)
    out["load"] = round(r["players"] / max(1, r["max_players"]), 3)
    return out


@app.post("/global/sync")
def sync(rep: VpsReport, x_mp_token: str = Header(default="")):
    if TOKEN and x_mp_token != TOKEN:
        raise HTTPException(401, "bad token")
    key = rep.continent.upper() + "|" + rep.url
    vps[key] = dict(rep.model_dump(), continent=rep.continent.upper(), seen=time.time())
    return {"ok": True, "stale_s": STALE_S, "peers": [_public(r) for r in vps.values()]}


@app.get("/global/regions")
def regions():
    return {"now": time.time(), "vps": sorted((_public(r) for r in vps.values()), key=lambda r: r["continent"])}


@app.get("/global/best")
def best(lat: Optional[float] = None, lon: Optional[float] = None, continent: Optional[str] = None,
         limit: int = Query(5, ge=1, le=50)):
    if lat is not None and lon is not None:
        me = (lat, lon)
    else:
        me = CENTROIDS.get((continent or "").upper())
    ranked = []
    for r in vps.values():
        if not _alive(r):
            continue
        km = _km(me, _pos(r)) if me else 0.0
        # ~1 ms per 100 km of fibre (round trip); full servers rank lower; a GEO-satellite uplink adds ~550 ms
        est = km / 100.0 + (550.0 if r.get("uplink") == "geo" else 40.0 if r.get("uplink") == "leo" else 30.0 if r.get("uplink") == "cellular" else 0.0)
        load = r["players"] / max(1, r["max_players"])
        ranked.append(dict(_public(r), est_rtt_ms=round(est, 1), score=round(est + 100.0 * load ** 2, 1)))
    ranked.sort(key=lambda r: r["score"])
    return {"from": me, "vps": ranked[:limit]}


@app.get("/global/wires.txt", response_class=PlainTextResponse)
def wires():
    lines = ["# MP global center: alive continental VPS (net/wires.txt format: id | group | kind | host:port | name | options)"]
    for r in sorted(vps.values(), key=lambda r: r["continent"]):
        if not _alive(r):
            continue
        for i, g in enumerate(r["game"]):
            wid = f"{r['continent'].lower()}-gvps-{g['kind']}{i}"
            lines.append(f"{wid} | vps | {g['kind']} | {g['host']}:{g['port']} | {r['continent']} VPS ({g['kind']}) | region={r['continent']}")
    return "\n".join(lines) + "\n"


@app.get("/health")
def health():
    alive = sum(1 for r in vps.values() if _alive(r))
    return {"ok": True, "vps": len(vps), "alive": alive}


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=int(os.environ.get("PORT", "8000")), log_level="warning")
