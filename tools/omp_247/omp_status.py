#!/usr/bin/env python3
"""5.02.18: HiveMQ status topic for an open.mp / SA-MP server run by openmp_247.sh.

Every `status_every` seconds (default 10) it asks the server (SA-MP query 'i') and publishes one retained JSON
message on <mqtt_topic>/status, e.g. thirtyfox/mp4/omp/<name>/status:

  {"name":"...","online":true,"host":"1.2.3.4","port":7777,"players":3,"max":50,"hostname":"...","mode":"...",
   "lang":"...","state":"running","pid":1234,"restarts_hour":0,"last_backup":"backup_20261121_093000.tar.gz",
   "up_s":5400,"t":1763700000}

If the machine, the script or the broker link dies, the broker publishes the last will {"online":false}.
Only Python's standard library (MQTT 3.1.1, QoS 0), like HiveMQ/tools/mp-status.py.

Login: mqtt_user / mqtt_pass in the yml, or mqtt_pass_env: KEY to read KEY from /etc/mp-hivemq/mp.env
(the MP HiveMQ kit: user "bridge", BRIDGE_PASS). No mqtt_host = nothing to do (exits 0).
"""
import json, os, socket, ssl, struct, sys, time

def cfg(path):
    d = {}
    try:
        for l in open(path):
            l = l.split("#", 1)[0].strip()
            if ":" in l:
                k, v = l.split(":", 1); d[k.strip()] = v.strip()
    except OSError:
        pass
    return d

def mpenv(key, path="/etc/mp-hivemq/mp.env"):
    try:
        for l in open(path):
            l = l.strip()
            if l.startswith(key + "="): return l.split("=", 1)[1].strip().strip('"')
    except OSError:
        pass
    return ""

def s(b): b = b.encode(); return struct.pack("!H", len(b)) + b
def rl(n):
    o = b""
    while True:
        x = n % 128; n //= 128
        o += bytes([x | (0x80 if n else 0)])
        if not n: return o

def connect(host, port, user, pw, will_topic, will, tls=False):
    k = socket.create_connection((host, port), 8)
    if tls: k = ssl.create_default_context().wrap_socket(k, server_hostname=host)   # 5.02.20: HiveMQ Cloud (8883)
    flags = 0x20 | 0x04 | 0x02   # will retain, will, clean session
    if user: flags |= 0x80
    if pw: flags |= 0x40
    body = s("MQTT") + bytes([4, flags]) + struct.pack("!H", 60) + s("omp247-%d" % os.getpid()) + s(will_topic) + s(will)
    if user: body += s(user)
    if pw: body += s(pw)
    k.sendall(b"\x10" + rl(len(body)) + body)
    a = k.recv(4)
    if len(a) < 4 or a[0] != 0x20 or a[3] != 0: raise OSError("connack %r" % a)
    return k

def publish(k, topic, msg, retain=True):
    body = s(topic) + msg.encode()
    k.sendall((b"\x31" if retain else b"\x30") + rl(len(body)) + body)

def query(host, port):
    """SA-MP query 'i': password, players, max, hostname, gamemode, language."""
    try:
        ip = socket.gethostbyname(host)
        q = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); q.settimeout(2)
        q.sendto(b"SAMP" + socket.inet_aton(ip) + struct.pack("<H", port) + b"i", (ip, port))
        d = q.recv(4096); q.close()
        if d[:4] != b"SAMP" or len(d) < 16: return None
        o = 11; pw = d[o]; o += 1
        pl, mx = struct.unpack_from("<HH", d, o); o += 4
        out = []
        for _ in range(3):
            n = struct.unpack_from("<I", d, o)[0]; o += 4
            out.append(d[o:o + n].decode("cp1252", "replace")); o += n
        return {"password": bool(pw), "players": pl, "max": mx, "hostname": out[0], "mode": out[1], "lang": out[2]}
    except Exception:
        return None

def state(path):
    try: return json.load(open(path))
    except Exception: return {}

def main():
    c = cfg(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "raknet_247.yml"))
    mh = c.get("mqtt_host", "")
    if not mh: print("omp_status: no mqtt_host - off"); return 0
    mp = int(c.get("mqtt_port", "1883") or 1883)
    user = c.get("mqtt_user", "")
    pw = c.get("mqtt_pass", "") or (mpenv(c["mqtt_pass_env"]) if c.get("mqtt_pass_env") else "")
    base = c.get("mqtt_topic", "wagu/openmp").rstrip("/")
    name = c.get("name", "") or base.rsplit("/", 1)[-1]
    host = c.get("host", "127.0.0.1"); port = int(c.get("port", "7777") or 7777)
    pub_host = c.get("public_host", "") or host
    every = max(3, int(c.get("status_every", "10") or 10))
    sfile = c.get("state_file", "./omp_247.state")
    topic = base + "/status"
    will = json.dumps({"name": name, "online": False, "state": "offline"}, separators=(",", ":"))
    k = None; last = ""; lastAt = 0
    while True:
        try:
            if k is None:
                k = connect(mh, mp, user, pw, topic, will, c.get("mqtt_tls", "0") in ("1", "true", "yes"))
                print("omp_status: connected to %s:%d, topic %s" % (mh, mp, topic), flush=True)
            q = query(host, port); st = state(sfile)
            msg = {"name": name, "online": q is not None, "host": pub_host, "port": port,
                   "state": st.get("state", "unknown"), "pid": st.get("pid"), "restarts_hour": st.get("restarts_hour", 0),
                   "last_backup": st.get("last_backup", ""), "up_s": int(time.time() - st["started"]) if st.get("started") else 0}
            if q: msg.update(q)
            body = json.dumps(msg, separators=(",", ":"))
            now = time.time()
            if body != last or now - lastAt >= 60:   # on a change, else once a minute (fresh "t")
                msg["t"] = int(now); publish(k, topic, json.dumps(msg, separators=(",", ":"))); last = body; lastAt = now
            k.sendall(b"\xc0\x00")   # PINGREQ keeps the link (keepalive 60 s)
            k.setblocking(False)
            try:
                while k.recv(4096): pass   # drop PINGRESPs so the socket buffer never fills
            except (BlockingIOError, ssl.SSLWantReadError): pass
            k.setblocking(True); k.settimeout(8)
        except Exception as e:
            print("omp_status: %s - again in 10 s" % e, flush=True)
            try: k and k.close()
            except Exception: pass
            k = None; time.sleep(10); continue
        time.sleep(every)

if __name__ == "__main__":
    sys.exit(main() or 0)
