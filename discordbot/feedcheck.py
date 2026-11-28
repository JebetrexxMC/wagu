"""5.02.81: live feed check (workflow "MP live feed check"): the bot's permissions in the feed channel,
its webhooks, a test post, MQTT traffic on the feed topic, and an end-to-end test (a test event on the brokers
-> the running bot must post it). Only reads / posts in FEED_CHANNEL_ID. Prints a report, writes feedcheck.json.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.request

import paho.mqtt.client as mqtt

TOKEN = os.environ.get("DISCORD_TOKEN", "").strip()
CH = os.environ.get("FEED_CHANNEL_ID", "1576192312126472323").strip()
API = "https://discord.com/api/v10"
TOPIC = "wagu/feed1/e"
STATUS = "wagu/railway/discordpy-bot/status"   # the bot's retained beacon (5.02.82+: carries the /status feed line)
BROKERS = [("broker.hivemq.com", 1883), ("test.mosquitto.org", 1883), ("broker.emqx.io", 1883)]
BITS = {"ADMINISTRATOR": 1 << 3, "VIEW_CHANNEL": 1 << 10, "SEND_MESSAGES": 1 << 11, "EMBED_LINKS": 1 << 14,
        "READ_MESSAGE_HISTORY": 1 << 16, "MANAGE_ROLES": 1 << 28, "MANAGE_WEBHOOKS": 1 << 29}
R = {"channel": CH, "ok": False}


def api(method, path, body=None):
    req = urllib.request.Request(API + path, method=method, data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Authorization": "Bot " + TOKEN, "Content-Type": "application/json",
                                          "User-Agent": "DiscordBot (https://github.com/JebetrexxMC/wagu, 5.02.81)"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            t = r.read()
            return r.status, (json.loads(t) if t else None)
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read() or b"null")
        except Exception:
            return e.code, None


def perms(guild, member, channel):
    gid = guild["id"]
    roles = {r["id"]: int(r["permissions"]) for r in guild["roles"]}
    if guild.get("owner_id") == member["user"]["id"]:
        return -1
    p = roles.get(gid, 0)
    for rid in member["roles"]:
        p |= roles.get(rid, 0)
    if p & BITS["ADMINISTRATOR"]:
        return -1
    ow = {o["id"]: o for o in channel.get("permission_overwrites", [])}
    if gid in ow:
        p = (p & ~int(ow[gid]["deny"])) | int(ow[gid]["allow"])
    a = d = 0
    for rid in member["roles"]:
        if rid in ow:
            a |= int(ow[rid]["allow"]); d |= int(ow[rid]["deny"])
    p = (p & ~d) | a
    uid = member["user"]["id"]
    if uid in ow:
        p = (p & ~int(ow[uid]["deny"])) | int(ow[uid]["allow"])
    return p


def main():
    if not TOKEN:
        print("no DISCORD_TOKEN"); return 1
    s, me = api("GET", "/users/@me")
    print("bot:", s, me and me.get("username"), me and me.get("id"))
    s, ch = api("GET", f"/channels/{CH}")
    if s != 200:
        print("channel", CH, "->", s, ch); R["error"] = f"channel {s}"; return 0
    R["channel_name"] = ch.get("name"); R["type"] = ch.get("type")
    print("channel:", ch.get("name"), "type", ch.get("type"), "guild", ch.get("guild_id"))
    s, g = api("GET", f"/guilds/{ch['guild_id']}")
    s2, m = api("GET", f"/guilds/{ch['guild_id']}/members/{me['id']}")
    if s == 200 and s2 == 200:
        p = perms(g, m, ch)
        R["guild"] = g.get("name")
        R["perms"] = {k: (p == -1 or bool(p & v)) for k, v in BITS.items()}
        print("guild:", g.get("name"), "| permissions in channel:", R["perms"])
    else:
        print("guild/member:", s, s2)
    s, hooks = api("GET", f"/channels/{CH}/webhooks")
    R["webhooks_status"] = s
    hook = None
    if s == 200:
        R["webhooks"] = [{"name": h.get("name"), "by_bot": (h.get("user") or {}).get("id") == me["id"]} for h in hooks]
        print("webhooks:", R["webhooks"])
        for h in hooks:
            if h.get("name") == "MP Live Feed" and h.get("token"):
                hook = h
    else:
        print("webhooks ->", s, hooks)
    # recent messages (what the running bot posted)
    s, msgs = api("GET", f"/channels/{CH}/messages?limit=15")
    if s == 200:
        R["recent"] = [{"by": (x.get("author") or {}).get("username"), "webhook": bool(x.get("webhook_id")),
                        "at": x.get("timestamp"), "text": (x.get("content") or "")[:80]} for x in msgs]
        for x in R["recent"]:
            print("  recent:", x)
    else:
        print("messages ->", s)

    # MQTT: listen on the feed topic, then publish one test event (the running bot should post it)
    got = []
    beacon = {}
    cs = []
    for i, (h, port) in enumerate(BROKERS):
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"mpfeedchk-{os.getpid()}-{i}")
        c.on_connect = lambda c, u, f, rc, p=None: (c.subscribe(TOPIC), c.subscribe(STATUS))
        c.on_message = lambda c, u, msg, i=i: (beacon.__setitem__("doc", msg.payload) if msg.topic == STATUS
                                               else got.append((BROKERS[i][0], msg.payload[:200])))
        try:
            c.connect_async(h, port, 30); c.loop_start(); cs.append(c)
        except Exception as e:
            print("broker", h, e)
    time.sleep(8)
    tid = "chk%x" % int(time.time())
    ev = {"v": 1, "id": tid, "t": "join", "name": "FeedCheck", "user": "", "disc": "MP feed check",
          "srv": "live feed test " + time.strftime("%H:%M:%S UTC", time.gmtime()) + " " + tid, "msg": "", "ver": "check", "src": "s"}
    for c in cs:
        if c.is_connected():
            c.publish(TOPIC, json.dumps(ev))
    t0 = time.time()
    seen = False
    while time.time() - t0 < 45 and not seen:
        time.sleep(5)
        s, msgs = api("GET", f"/channels/{CH}/messages?limit=10")
        if s == 200:
            for x in msgs:
                if tid in (x.get("content") or ""):   # 5.02.82: only this run's own test event (not an older one)
                    seen = True
                    R["e2e_by"] = (x.get("author") or {}).get("username"); R["e2e_webhook"] = bool(x.get("webhook_id"))
    R["e2e"] = seen
    print("end-to-end (test event -> running bot -> channel):", "POSTED" if seen else "not seen in 45 s", R.get("e2e_by", ""))
    time.sleep(20)
    R["mqtt_events"] = len(got)
    R["mqtt_real_events"] = len([g for g in got if tid.encode() not in g[1]])
    print("feed events seen on the brokers in ~75 s:", len(got), "(real players:", R["mqtt_real_events"], ")")
    for g in got[:10]:
        print("  ", g)
    time.sleep(15)   # the beacon is sent every 30 s: get one from after the test event
    try:
        d = json.loads(beacon.get("doc") or b"{}")
        if "s" in d:
            d = json.loads(d["s"])
        R["bot"] = {k: d.get(k) for k in ("online", "ver", "commit", "started", "ts", "brokers")}
        R["status_feed"] = d.get("feed")
        print("bot beacon:", R["bot"])
        print("/status feed line:", (d.get("feed") or {}).get("line", "(bot older than 5.02.82: no feed line in the beacon)"))
    except Exception as e:
        print("beacon:", e)
    R["ok"] = seen
    with open("feedcheck.json", "w") as f:
        json.dump(R, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
