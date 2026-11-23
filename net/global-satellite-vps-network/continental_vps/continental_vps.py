"""MP continental VPS agent: one per region (NA, EU, ASIA, SA, OC, ...), next to the game server and the broker.

  GET  /ping          tiny answer for RTT measurements (edge clients time it)
  GET  /status        this VPS: continent, game endpoints, players, center reachable or not
  GET  /peers         the last list of all VPS from the global center - edge clients fail over with it even when
                      the global center is down
  POST /edge/hello    an edge client says it uses this VPS (counts as a player for 60 s -> load)

It reports to the global center every SYNC_S seconds. When the center can't be reached it keeps serving the
cached peers and retries with backoff (5 s -> 60 s), like a dual-WAN failover keeps the last good route.

Env: CONTINENT, PORT, GLOBAL_CENTER_URL (http://center:8000/global/sync), PUBLIC_URL (how clients reach this
agent; default http://vps-<continent>:<port>), GAME_HOST / GAME_PORT (MP server, kind mp), MQTT_HOST / MQTT_PORT
(region broker), LAT / LON, MAX_PLAYERS, UPLINK (fiber | leo | geo | cellular), GLOBAL_TOKEN, SYNC_S.
"""
import asyncio
from contextlib import asynccontextmanager
import os
import time
from typing import Dict

import httpx
import uvicorn
from fastapi import FastAPI
from pydantic import BaseModel

CONT = os.environ.get("CONTINENT", "NA").upper()
PORT = int(os.environ.get("PORT", "8001"))
CENTER = os.environ.get("GLOBAL_CENTER_URL", "http://global-center:8000/global/sync")
PUBLIC = os.environ.get("PUBLIC_URL", f"http://vps-{CONT.lower()}:{PORT}")
SYNC_S = float(os.environ.get("SYNC_S", "5"))
TOKEN = os.environ.get("GLOBAL_TOKEN", "")


def _game():
    out = []
    if os.environ.get("GAME_HOST"):
        out.append({"kind": "mp", "host": os.environ["GAME_HOST"], "port": int(os.environ.get("GAME_PORT", "1922"))})
    if os.environ.get("MQTT_HOST"):
        out.append({"kind": "mqtt", "host": os.environ["MQTT_HOST"], "port": int(os.environ.get("MQTT_PORT", "1883"))})
    return out


state = {"peers": [], "center_ok": False, "center_rtt_ms": None, "last_sync": 0.0, "fails": 0}
edges: Dict[str, float] = {}       # client id -> last hello
@asynccontextmanager
async def lifespan(_app):
    task = asyncio.create_task(sync_loop())
    yield
    task.cancel()


app = FastAPI(title=f"MP continental VPS {CONT}", lifespan=lifespan)


class Hello(BaseModel):
    client: str
    rtt_ms: float = 0.0


def players() -> int:
    now = time.time()
    for k in [k for k, t in edges.items() if now - t > 60]:
        del edges[k]
    return len(edges)


def report() -> dict:
    r = {"continent": CONT, "url": PUBLIC, "game": _game(), "players": players(),
         "max_players": int(os.environ.get("MAX_PLAYERS", "100")), "uplink": os.environ.get("UPLINK", "fiber"),
         "center_rtt_ms": state["center_rtt_ms"], "version": "mp-gvps 1.0"}
    if os.environ.get("LAT") and os.environ.get("LON"):
        r["lat"], r["lon"] = float(os.environ["LAT"]), float(os.environ["LON"])
    return r


async def sync_loop():
    headers = {"X-MP-Token": TOKEN} if TOKEN else {}
    async with httpx.AsyncClient(timeout=5.0) as cli:
        while True:
            t0 = time.perf_counter()
            try:
                r = await cli.post(CENTER, json=report(), headers=headers)
                r.raise_for_status()
                state.update(peers=r.json().get("peers", []), center_ok=True, last_sync=time.time(), fails=0,
                             center_rtt_ms=round((time.perf_counter() - t0) * 1000, 1))
                wait = SYNC_S
            except Exception as e:  # center down / network: keep the cached peers
                state["fails"] += 1
                if state["center_ok"] or state["fails"] == 1:
                    print(f"[{CONT}] global center unreachable ({type(e).__name__}) - serving cached peers", flush=True)
                state["center_ok"] = False
                wait = min(60.0, SYNC_S * 2 ** min(state["fails"], 4))
            await asyncio.sleep(wait)




@app.get("/ping")
def ping():
    return {"c": CONT, "t": time.time()}


@app.get("/status")
def status():
    return dict(report(), center_ok=state["center_ok"], last_sync_age_s=round(time.time() - state["last_sync"], 1)
                if state["last_sync"] else None)


@app.get("/peers")
def peers():
    return {"from": CONT, "center_ok": state["center_ok"], "peers": state["peers"] or [dict(report(), alive=True)]}


@app.post("/edge/hello")
def hello(h: Hello):
    edges[h.client[:64]] = time.time()
    return {"ok": True, "continent": CONT, "game": _game(), "players": players()}


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="warning")
