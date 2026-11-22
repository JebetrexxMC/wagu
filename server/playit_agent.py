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
BIN_URL = os.environ.get("PLAYIT_BIN_URL",
                         "https://github.com/JebetrexxMC/wagu/releases/download/v5.02.30/playit-linux-amd64")
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
      "proto": "udp", "addr": "", "claim_url": "", "account": "", "tunnels": [], "log": [], "note": ""}
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


def claim():
    """account link: playit.gg/claim/<code> on the website console until someone presses Accept"""
    while True:
        code = secrets.token_hex(5)
        r = api("/claim/setup", {"code": code, "agent_type": "self-managed", "version": "mp-crossplay-247 2.0.2"})
        if r.get("status") != "success":
            publish(state="error", note="playit.gg claim setup failed: %s" % r.get("data")); time.sleep(60); continue
        url = "https://playit.gg/claim/" + code
        publish(state="claim", claim_url=url, note="open the link, log in or sign up at playit.gg, press Accept")
        t0 = time.time()
        while time.time() - t0 < 900:               # a fresh link every 15 min
            time.sleep(5)
            r = api("/claim/setup", {"code": code, "agent_type": "self-managed", "version": "mp-crossplay-247 2.0.2"})
            d = r.get("data")
            if d == "UserAccepted":
                k = api("/claim/exchange", {"code": code})
                key = (k.get("data") or {}).get("secret_key", "") if isinstance(k.get("data"), dict) else ""
                if key:
                    saved = save_key(key)
                    publish(state="linked", claim_url="", note="account linked" + (", key saved (encrypted)" if saved else ""))
                    return key
            elif d == "UserRejected":
                publish(state="claim", claim_url="", note="the link was rejected - a new one comes next"); break
            elif d == "WaitingForUser":
                if st["note"] != "link opened - press Accept on playit.gg": publish(note="link opened - press Accept on playit.gg")


# ---------------------------------------------------------------- the agent + the UDP tunnel
def get_bin():
    if os.access(BIN, os.X_OK): return True
    os.makedirs(os.path.dirname(BIN), exist_ok=True)
    try:
        urllib.request.urlretrieve(BIN_URL, BIN + ".part"); os.chmod(BIN + ".part", 0o755); os.replace(BIN + ".part", BIN)
        return True
    except Exception as e:
        publish(state="error", note="agent download failed: %s" % e); return False


def run_agent(key):
    lf = open(LOG, "ab")
    return subprocess.Popen([BIN, "--secret", key, "--secret-path", os.path.join(HERE, "run", "playit.toml"),
                             "--socket-path", os.path.join(HERE, "run", "playit.sock")],
                            stdout=lf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)


def tunnel_addr(t):
    host = t.get("custom_domain") or t.get("assigned_domain") or ""
    p = t.get("port") or {}
    port = p.get("from") if isinstance(p, dict) else p
    return "%s:%s" % (host, port) if host and port else ""


def ensure_tunnel(key):
    r = api("/agents/rundata", {}, key)
    if r.get("status") != "success":
        return "", "agent rundata: %s" % (r.get("data"),)
    d = r["data"]
    ts = d.get("tunnels") or []
    st["account"] = str(d.get("account_status", ""))
    st["tunnels"] = [{"name": t.get("name"), "proto": t.get("proto"), "local": "%s:%s" % (t.get("local_ip"), t.get("local_port")),
                      "addr": tunnel_addr(t), "disabled": bool(t.get("disabled"))} for t in ts]
    for t in ts:
        if int(t.get("local_port") or 0) == PORT and str(t.get("proto", "")).lower() in ("udp", "both") and not t.get("disabled"):
            return tunnel_addr(t), ""
    if d.get("pending"):
        return "", "tunnel being made"
    c = api("/tunnels/create", {"name": "MP Crossplay 24/7", "tunnel_type": None, "port_type": "udp", "port_count": 1,
                               "origin": {"type": "agent", "data": {"agent_id": d.get("agent_id"), "local_ip": "127.0.0.1",
                                                                     "local_port": PORT}},
                               "enabled": True, "alloc": None, "firewall_id": None, "proxy_protocol": None}, key)
    if c.get("status") == "success":
        return "", "UDP tunnel made (-> 127.0.0.1:%d), waiting for its address" % PORT
    return "", "make the tunnel in the playit.gg dashboard: UDP, local 127.0.0.1:%d (%s)" % (PORT, c.get("data"))


def main():
    global secret
    publish(state="starting", note="getting the agent key")
    secret = os.environ.get("PLAYIT_SECRET", "").strip() or load_saved()
    if secret:
        st["note"] = "agent key from " + ("PLAYIT_SECRET" if os.environ.get("PLAYIT_SECRET") else "playit.enc (saved)")
    else:
        if not PASS:
            st["note"] = "no AUTH_PRIVATE_KEY: the linked key is only kept for this shift"
        secret = claim()
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
            elif "invalid" in why.lower() or "unauthorized" in why.lower() or "AgentKeyNotFound" in why:
                if os.path.exists(ENC) and not os.environ.get("PLAYIT_SECRET"):
                    os.remove(ENC)                       # the saved key was removed on playit.gg -> link again
                agent.terminate(); agent = None
                publish(state="claim", addr="", note="agent key not accepted - link the account again")
                secret = claim()
            else:
                publish(state="linked", note=why)
        time.sleep(10)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
