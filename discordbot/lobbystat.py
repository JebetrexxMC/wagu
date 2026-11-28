"""5.02.80: /status - the MP lobby live, read by the bot itself.

The bot's MQTT links (main.py Bridge, the 3 public brokers) also listen to the lobby topic the game and the website
read (thirtyfox/mp5/l/<token>, "v1|port|players|max|region|...|name"). A server counts as online while it keeps
sending FRESH announcements (retained = the last saved one, not proof it is up), the same rule as
tools/lobby_status.py (the README card).
"""
import threading
import time

LOBBY_TOPIC = "thirtyfox/mp5/l/+"
FRESH_S = 150          # no new announcement for 2.5 min = gone
REGIONS = ["Asia", "Europe", "North America", "Oceania"]


def _num(x):
    try:
        return int(x)
    except (TypeError, ValueError):
        return 0


class LobbyStat:
    def __init__(self):
        self.lock = threading.Lock()
        self.live = {}        # token -> server dict (fresh announcements)
        self.started = time.time()
        self.peak = {"day": "", "players": 0, "servers": 0}

    # MQTT thread
    def on_message(self, topic, payload, retained, broker):
        tok = topic.rsplit("/", 1)[-1]
        p = payload.decode("utf-8", "replace") if payload else ""
        with self.lock:
            if not p:                      # host went offline (empty retained message)
                self.live.pop(tok, None)
                return
            if retained:
                return
            f = p.split("|")
            if f[0] != "v1" or len(f) < 7:
                return
            s = self.live.get(tok) or {"first": time.time(), "brokers": set()}
            s.update(name=("|".join(f[6:])[:40].strip() or "MP server"), port=_num(f[1]), players=max(0, _num(f[2])),
                     max=max(0, _num(f[3])), region=_num(f[4]), seen=time.time())
            s["brokers"].add(broker)
            self.live[tok] = s

    def snapshot(self):
        now = time.time()
        with self.lock:
            for t in [t for t, s in self.live.items() if now - s["seen"] > FRESH_S]:
                del self.live[t]
            servers = [dict(s, token=t) for t, s in self.live.items()]
        players = sum(s["players"] for s in servers)
        day = time.strftime("%Y-%m-%d", time.gmtime(now))
        if self.peak["day"] != day:
            self.peak = {"day": day, "players": 0, "servers": 0}
        self.peak["players"] = max(self.peak["players"], players)
        self.peak["servers"] = max(self.peak["servers"], len(servers))
        regions = {}
        for s in servers:
            r = REGIONS[s["region"]] if 0 <= s["region"] < len(REGIONS) else "Worldwide"
            c = regions.setdefault(r, [0, 0])
            c[0] += 1
            c[1] += s["players"]
        servers.sort(key=lambda s: (-s["players"], s["name"].lower()))
        return {
            "servers": servers, "players": players, "regions": regions,
            "slots": sum(s["max"] for s in servers), "busy": sum(1 for s in servers if s["players"] > 0),
            "peak": dict(self.peak), "warm": now - self.started < 60,
        }
