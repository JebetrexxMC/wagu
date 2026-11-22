#!/usr/bin/env python3
"""5.02.50: the playit.gg PC tunnel for MP Crossplay 24/7, run by omp_crossplay.sh on the 24/7 host.

No download on your computer: the playit agent (playit-linux-amd64 2.0.2, uploaded in release v5.02.30)
runs here on the 24/7 host, and the website console (mpsamp-open.github.io/playit.html, web Termux +
MP Bot) shows what it does live over MQTT (HiveMQ, retained topic thirtyfox/mp5/playit).

  1. agent key: PLAYIT_SECRET (repo secret) -> else playit.enc (saved, AES-256 with AUTH_PRIVATE_KEY,
     on the mp247-data branch) -> else account link: a playit.gg claim link is shown on the website
     console; open it, log in / sign up at playit.gg, press Accept -> the key is saved (encrypted).
  2. the agent runs (restarts by itself), log in logs/playit.log
  3. UDP tunnel -> 127.0.0.1:7778: made by itself on the linked account if there is none yet
  4. the tunnel address goes to run/playit.addr -> omp247.json "addr" -> pc.html / join.html links
"""
import json, os, re, secrets, socket, struct, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)
API = "https://api.playit.gg"
# 5.02.51: our own build first (playit-agent.yml, from github.com/JebetrexxMC/playit-agent), then the v5.02.30 upload
BIN_URLS = [u for u in [os.environ.get("PLAYIT_BIN_URL", ""),
                        "https://github.com/JebetrexxMC/wagu/releases/download/playit-mp/mp-playitd-linux-amd64",
                        "https://github.com/JebetrexxMC/wagu/releases/download/v5.02.30/playit-linux-amd64"] if u]
BIN = os.path.join(os.environ.get("OMP_DIR", os.path.join(HERE, "omp")), "playit-linux-amd64")
PORT = int(os.environ.get("OMP_PORT", "7778"))
ENC = os.path.join(HERE, "playit.enc")
STATE = os.path.join(HERE, "playit.json")
ADDR = os.path.join(HERE, "run", "playit.addr")
LOG = os.path.join(HERE, "logs", "playit.log")
TOPIC = "thirtyfox/mp5/playit"
BROKERS = [("broker.hivemq.com", 1883), ("broker.emqx.io", 1883), ("test.mosquitto.org", 1883)]
PASS = os.environ.get("PLAYIT_KEY_PASS", "")
os.makedirs(os.path.join(HERE, "run"), exist_ok=True)
os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)

st = {"state": "starting", "agent": "playit 2.0.2 (release v5.02.30, linux-amd64)", "port": PORT,
      "proto": "udp", "addr": "", "claim_url": "", "expires": 0, "link_min": int(os.environ.get("PLAYIT_LINK_MIN", "5")), "account": "", "tunnels": [], "log": [], "note": ""}
secret = ""


# ---------------------------------------------------------------- status: playit.json + MQTT
def mqtt_publish(payload):
    """MQTT 3.1.1: CONNECT + PUBLISH (QoS 0, retained) + DISCONNECT, no extra packages"""
    def s(b): return struct.pack(">H", len(b)) + b
    def rl(n):
        o = b""
        while True:
            d, n = n % 128, n // 128
            o += bytes([d | (0x80 if n else 0)])
            if not n: return o
    cid = ("mp247-playit-" + secrets.token_hex(3)).encode()
    var = s(b"MQTT") + bytes([4, 2]) + struct.pack(">H", 30) + s(cid)
    pub = s(TOPIC.encode()) + payload
    for host, port in BROKERS:
        try:
            c = socket.create_connection((host, port), timeout=6)
            c.sendall(b"\x10" + rl(len(var)) + var)
            if c.recv(4)[:1] != b"\x20": raise OSError("no CONNACK")
            c.sendall(b"\x31" + rl(len(pub)) + pub + b"\xe0\x00")
            c.close()
        except Exception:
            pass


def mqtt_read(topic, secs=3):
    """subscribe, return the retained payload of one topic (or None)"""
    def s(b): return struct.pack(">H", len(b)) + b
    cid = ("mp247-plreq-" + secrets.token_hex(3)).encode()
    var = s(b"MQTT") + bytes([4, 2]) + struct.pack(">H", 30) + s(cid)
    sub = struct.pack(">H", 1) + s(topic.encode()) + b"\x00"
    for host, port in BROKERS:
        try:
            c = socket.create_connection((host, port), timeout=6)
            c.sendall(b"\x10" + bytes([len(var)]) + var)
            if c.recv(4)[:1] != b"\x20": raise OSError("no CONNACK")
            c.sendall(b"\x82" + bytes([len(sub)]) + sub)
            c.settimeout(secs); buf = b""; end = time.time() + secs
            while time.time() < end:
                try: d = c.recv(4096)
                except socket.timeout: break
                if not d: break
                buf += d
                while len(buf) >= 2:
                    n, m, i = 0, 1, 1
                    while i < len(buf) and buf[i] & 0x80: n += (buf[i] & 0x7f) * m; m *= 128; i += 1
                    if i >= len(buf): break
                    n += buf[i] * m; i += 1
                    if len(buf) < i + n: break
                    h, body, buf = buf[0], buf[i:i + n], buf[i + n:]
                    if h >> 4 == 3:
                        tl = struct.unpack(">H", body[:2])[0]
                        if body[2:2 + tl].decode(errors="replace") == topic:
                            c.close(); return body[2 + tl + (2 if h & 6 else 0):]
            c.close(); return None
        except Exception:
            continue
    return None


def clean(t):
    return t.replace(secret, "<agent key>") if secret else t


def publish(**kw):
    st.update(kw)
    try:
        with open(LOG, errors="replace") as f:
            st["log"] = [clean(re.sub(r"^\S+Z\s+", "", l.rstrip()))[-160:] for l in f.readlines()[-12:]]
    except OSError:
        pass
    st["updated"] = int(time.time())
    data = json.dumps(st, indent=1)
    with open(STATE, "w") as f: f.write(data)
    if st["addr"]:
        with open(ADDR, "w") as f: f.write(st["addr"])
    mqtt_publish(json.dumps(st, separators=(",", ":")).encode())
    print("[playit] %s %s %s" % (st["state"], st["addr"] or "", st["claim_url"] or ""), flush=True)


# ---------------------------------------------------------------- playit.gg API
def api(path, body, key=None):
    h = {"Content-Type": "application/json", "User-Agent": "mp-crossplay-247/5.02.50"}
    if key: h["Authorization"] = "Agent-Key " + key
    r = urllib.request.Request(API + path, data=json.dumps(body).encode(), headers=h, method="POST")
    try:
        with urllib.request.urlopen(r, timeout=20) as x: return json.loads(x.read())
    except urllib.error.HTTPError as e:
        try: return json.loads(e.read())
        except Exception: return {"status": "error", "data": "HTTP %d" % e.code}
    except Exception as e:
        return {"status": "error", "data": str(e)}


def load_saved():
    if not (PASS and os.path.isfile(ENC)): return ""
    try:
        out = subprocess.run(["openssl", "enc", "-d", "-aes-256-cbc", "-pbkdf2", "-a", "-A", "-pass", "env:PLAYIT_KEY_PASS",
                              "-in", ENC], capture_output=True, timeout=20).stdout.decode().strip()
        return out if re.fullmatch(r"[0-9a-fA-F]{32,128}", out) else ""
    except Exception:
        return ""


def save_key(key):
    if not PASS: return False
    p = subprocess.run(["openssl", "enc", "-aes-256-cbc", "-pbkdf2", "-salt", "-a", "-A", "-pass", "env:PLAYIT_KEY_PASS",
                        "-out", ENC], input=key.encode(), capture_output=True, timeout=20)
    return p.returncode == 0


LINK_MIN = int(os.environ.get("PLAYIT_LINK_MIN", "5"))    # 5.02.53: a link lives 5 min, then it is gone
REQ = TOPIC + "/req"                                     # the MP Tunnel console asks for a new link here
handled = 0


def wait_request():
    """no link on show: wait until someone presses "Link playit account" on the MP Tunnel console"""
    global handled
    publish(state="waiting", claim_url="", expires=0,
            note="no link on show - press \"Link playit account\" on the MP Tunnel console for a %d min link" % LINK_MIN)
    tries = []
    while True:
        time.sleep(10)
        raw = mqtt_read(REQ)
        try: t = int(json.loads(raw or b"{}").get("t", 0))
        except Exception: t = 0
        if t > handled and abs(time.time() - t) < 180:
            handled = t
            tries = [x for x in tries if time.time() - x < 3600]
            if len(tries) >= 6:                              # at most 6 links an hour
                publish(note="link limit reached (6 an hour) - try again later"); continue
            tries.append(time.time())
            mqtt_publish_to(REQ, b"")                        # clear the request
            return


def mqtt_publish_to(topic, payload):
    global TOPIC
    old, TOPIC = TOPIC, topic
    try: mqtt_publish(payload)
    finally: TOPIC = old


def claim(first=True):
    """account link: a short-lived playit.gg/claim/<code> on the MP Tunnel console; Accept on playit.gg"""
    ver = "mp-crossplay-247 2.0.2"
    while True:
        if not first: wait_request()
        first = False
        code = secrets.token_hex(5)
        r = api("/claim/setup", {"code": code, "agent_type": "self-managed", "version": ver})
        if r.get("status") != "success":
            publish(state="error", note="playit.gg claim setup failed: %s" % r.get("data")); time.sleep(60); continue
        exp = int(time.time()) + LINK_MIN * 60
        publish(state="claim", claim_url="https://playit.gg/claim/" + code, expires=exp,
                note="open the link, log in or sign up at playit.gg, press Accept (link ends in %d min)" % LINK_MIN)
        while time.time() < exp:
            time.sleep(5)
            d = api("/claim/setup", {"code": code, "agent_type": "self-managed", "version": ver}).get("data")
            if d == "UserAccepted":
                k = api("/claim/exchange", {"code": code})
                key = k["data"].get("secret_key", "") if isinstance(k.get("data"), dict) else ""
                if key:
                    saved = save_key(key)
                    publish(state="linked", claim_url="", expires=0,
                            note="account linked" + (", key saved (encrypted)" if saved else ""))
                    return key
            elif d == "UserRejected":
                break
            elif d == "WaitingForUser" and st["note"] != "link opened - press Accept on playit.gg":
                publish(note="link opened - press Accept on playit.gg")
        # the link is over: this host stops asking for that code and never uses it again


# ---------------------------------------------------------------- the agent + the UDP tunnel
def get_bin():
    if os.access(BIN, os.X_OK): return True
    os.makedirs(os.path.dirname(BIN), exist_ok=True)
    err = ""
    for u in BIN_URLS:
        try:
            urllib.request.urlretrieve(u, BIN + ".part"); os.chmod(BIN + ".part", 0o755)
            if subprocess.run([BIN + ".part", "--help"], capture_output=True, timeout=20).returncode != 0: raise OSError("does not run")
            os.replace(BIN + ".part", BIN)
            st["agent"] = ("playit 2.0.2, MP build (JebetrexxMC/playit-agent)" if "playit-mp" in u
                           else "playit 2.0.2 (release v5.02.30, linux-amd64)")
            return True
        except Exception as e:
            err = "%s: %s" % (u.rsplit("/", 2)[-2], e)
    publish(state="error", note="agent download failed (%s)" % err); return False


def run_agent(key):
    lf = open(LOG, "ab")
    # 5.02.55: --secret and --secret-path can not be used together (the agent exited at once = "agent is offline")
    return subprocess.Popen([BIN, "--secret", key, "--socket-path", os.path.join(HERE, "run", "playit.sock")],
                            stdout=lf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)


def cfg(t, name):
    for f in ((t.get("agent_config") or {}).get("fields") or []):
        if f.get("name") == name: return str(f.get("value", ""))
    return ""


def ensure_tunnel(key):
    """5.02.55: the playit 2.0.2 API (/v1/agents/rundata + /v1/tunnels/create, as in JebetrexxMC/playit-agent)"""
    r = api("/v1/agents/rundata", {}, key)
    if r.get("status") != "success":
        return "", "agent rundata: %s" % (r.get("data"),)
    d = r["data"]
    ts = d.get("tunnels") or []
    perm = d.get("permissions") or {}
    st["account"] = str(perm.get("account_status", ""))
    st["tunnels"] = [{"name": t.get("name"), "proto": t.get("port_type"), "local": "%s:%s" % (cfg(t, "local_ip") or "127.0.0.1", cfg(t, "local_port")),
                      "addr": t.get("display_address", ""), "disabled": bool(t.get("disabled_reason"))} for t in ts]
    for t in ts:
        if cfg(t, "local_port") == str(PORT) and str(t.get("port_type", "")).lower() in ("udp", "both") and not t.get("disabled_reason"):
            return t.get("display_address", ""), ""
    for t in ts:   # a UDP tunnel with no local port set yet (made in the dashboard) -> use it, it points at 7778 below
        if str(t.get("port_type", "")).lower() in ("udp", "both") and not t.get("disabled_reason") and not cfg(t, "local_port"):
            api("/v1/tunnels/config", {"tunnel_id": t.get("id"), "new_agent_id": d.get("agent_id"), "new_config": {"fields": [{"name": "local_ip", "value": "127.0.0.1"},
                                                                                       {"name": "local_port", "value": str(PORT)}]}}, key)
            return t.get("display_address", ""), ""
    pend = d.get("pending") or []
    if pend:
        return "", "tunnel being made: %s" % (pend[0].get("status_msg") or "")
    if st.get("made"):
        return "", "waiting for the new tunnel"
    req = {"name": "MP Crossplay 24/7", "ports": {"type": "custom-udp", "details": 1},
           "origin": {"type": "agent", "data": {"agent_id": d.get("agent_id"),
                      "config": {"fields": [{"name": "local_ip", "value": "127.0.0.1"}, {"name": "local_port", "value": str(PORT)}]}}},
           "enabled": True, "alloc": None, "firewall_id": None}
    c = api("/v1/tunnels/create", req, key)
    if c.get("status") == "success":
        st["made"] = True
        return "", "UDP tunnel made (-> 127.0.0.1:%d), waiting for its address" % PORT
    why = c.get("data")
    if "RequiresVerifiedAccount" in str(why):
        return "", "playit.gg wants a verified account for a new tunnel: verify your email on playit.gg, or make a UDP tunnel there (local port %d)" % PORT
    return "", "make the tunnel in the playit.gg dashboard: Custom UDP, 1 port, local 127.0.0.1:%d (%s)" % (PORT, why)


def main():
    global secret
    publish(state="starting", note="getting the agent key")
    secret = os.environ.get("PLAYIT_SECRET", "").strip() or load_saved()
    if secret:
        st["note"] = "agent key from " + ("PLAYIT_SECRET" if os.environ.get("PLAYIT_SECRET") else "playit.enc (saved)")
    else:
        if not PASS:
            st["note"] = "no AUTH_PRIVATE_KEY: the linked key is only kept for this shift"
        secret = claim(first=False)      # 5.02.53: a link only when asked for on the console
    if not get_bin(): return 1
    agent, last = None, 0
    while True:
        if agent is None or agent.poll() is not None:
            agent = run_agent(secret)
        if time.time() - last > 60:
            last = time.time()
            addr, why = ensure_tunnel(secret)
            if addr:
                publish(state="online", addr=addr, note="PC players: " + addr)
            elif "invalidagentkey" in why.lower().replace(" ", "") or "AgentKeyNotFound" in why or "AgentNotFound" in why:
                if os.path.exists(ENC) and not os.environ.get("PLAYIT_SECRET"):
                    os.remove(ENC)                       # the saved key was removed on playit.gg -> link again
                agent.terminate(); agent = None
                publish(state="claim", addr="", note="agent key not accepted - link the account again")
                secret = claim(first=False)
            else:
                publish(state="linked", note=why)
        time.sleep(10)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
