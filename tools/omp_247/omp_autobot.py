#!/usr/bin/env python3
"""5.02.20: omp_autobot - control the open.mp 24/7 servers of a machine over MQTT (HiveMQ).

On the server (installed as the systemd service omp-autobot by install_247.sh):
    omp_autobot.py serve
  listens on  thirtyfox/mp4/omp/bot/<bot>/cmd   and  thirtyfox/mp4/omp/bot/all/cmd
  answers on  thirtyfox/mp4/omp/bot/<bot>/reply  (JSON {"id","cmd","ok","done","out"})
  retained    thirtyfox/mp4/omp/bot/<bot>/online (last will {"online":false})
  <bot> = BOT_NAME in /etc/omp-247/autobot.env (default: the machine's host name)

From anywhere (Termux, Termius, a PC, another server) - same file, Python 3 only:
    python3 omp_autobot.py send -H broker.example.com -u admin -P 'pass' install myserver 7777 /home/samp/Server
  or from the HiveMQ WebSocket client in a browser: publish the command text on .../<bot>/cmd and
  subscribe to .../<bot>/reply.

Commands (plain text; with BOT_TOKEN set, the first word must be the token):
    help | list | install <name> <port> <server_dir> | status <name> | start|stop|restart <name>
    backup <name> | logs <name> [lines] | update | ping
Only these run - never a shell line. Names: letters, digits, _ and -. The broker login decides who may
publish commands (the MP HiveMQ kit: only "admin" and "bridge" may use thirtyfox/mp4/omp/#).
"""
import json, os, re, socket, ssl, struct, subprocess, sys, threading, time

BASE = "thirtyfox/mp4/omp/bot"
LIB = "/opt/omp-247"
RAW = "https://raw.githubusercontent.com/JebetrexxMC/wagu/main/tools/omp_247/"
FILES = ("install_247.sh", "openmp_247.sh", "omp_status.py", "omp_autobot.py")
NAME_RE = re.compile(r"^[A-Za-z0-9_-]{1,32}$")

# ---------------------------------------------------------------- tiny MQTT 3.1.1 (QoS 0)
def _s(b):
    b = b.encode() if isinstance(b, str) else b
    return struct.pack("!H", len(b)) + b

def _rl(n):
    o = b""
    while True:
        x = n % 128; n //= 128
        o += bytes([x | (0x80 if n else 0)])
        if not n: return o

class Mqtt:
    def __init__(self, host, port, user="", pw="", cid="omp-bot", will=None, tls=False, keepalive=60):
        k = socket.create_connection((host, port), 10)
        if tls: k = ssl.create_default_context().wrap_socket(k, server_hostname=host)
        flags = 0x02
        body_will = b""
        if will:
            flags |= 0x04 | 0x20; body_will = _s(will[0]) + _s(will[1])
        if user: flags |= 0x80
        if pw: flags |= 0x40
        body = _s("MQTT") + bytes([4, flags]) + struct.pack("!H", keepalive) + _s(cid) + body_will
        if user: body += _s(user)
        if pw: body += _s(pw)
        self.k = k; self.lock = threading.Lock(); self.buf = b""
        self._send(b"\x10" + _rl(len(body)) + body)
        t, p = self.read(10)
        if t != 0x20 or len(p) < 2 or p[1] != 0:
            raise OSError("broker refused the login (connack %s) - check user / password" % (p[1] if len(p) > 1 else "?"))

    def _send(self, b):
        with self.lock: self.k.sendall(b)

    def publish(self, topic, msg, retain=False):
        body = _s(topic) + (msg.encode() if isinstance(msg, str) else msg)
        self._send((b"\x31" if retain else b"\x30") + _rl(len(body)) + body)

    def subscribe(self, *topics):
        body = struct.pack("!H", 1) + b"".join(_s(t) + b"\x00" for t in topics)
        self._send(b"\x82" + _rl(len(body)) + body)

    def ping(self): self._send(b"\xc0\x00")

    def _take(self):
        """A whole packet from the buffer -> (type, payload) or None if not complete yet."""
        if len(self.buf) < 2: return None
        n, m, i = 0, 1, 1
        while True:
            if i >= len(self.buf) or i > 4: return None
            c = self.buf[i]; n += (c & 0x7f) * m; m *= 128; i += 1
            if not c & 0x80: break
        if len(self.buf) < i + n: return None
        t = self.buf[0] & 0xf0; p = self.buf[i:i + n]; self.buf = self.buf[i + n:]
        return t, p

    def read(self, timeout):
        """One packet -> (type & 0xf0, payload) or (None, None) on timeout."""
        end = time.time() + timeout
        while True:
            r = self._take()
            if r: return r
            left = end - time.time()
            if left <= 0: return None, None
            self.k.settimeout(left)
            try: d = self.k.recv(65536)
            except (socket.timeout, ssl.SSLError): return None, None
            if not d: raise OSError("broker closed the link")
            self.buf += d

def parse_publish(p):
    n = struct.unpack_from("!H", p, 0)[0]
    return p[2:2 + n].decode("utf-8", "replace"), p[2 + n:].decode("utf-8", "replace")

# ---------------------------------------------------------------- config
def envfile(path):
    d = {}
    try:
        for l in open(path):
            l = l.strip()
            if l and not l.startswith("#") and "=" in l:
                k, v = l.split("=", 1); d[k.strip()] = v.strip().strip('"').strip("'")
    except OSError:
        pass
    return d

def bot_cfg():
    hv = envfile("/etc/mp-hivemq/mp.env"); c = envfile("/etc/omp-247/autobot.env")
    return {
        "host": c.get("MQTT_HOST", "127.0.0.1"), "port": int(c.get("MQTT_PORT", "1883") or 1883),
        "user": c.get("MQTT_USER", "bridge"), "pw": c.get("MQTT_PASS", "") or hv.get("BRIDGE_PASS", ""),
        "tls": c.get("MQTT_TLS", "0") == "1", "bot": c.get("BOT_NAME", "") or socket.gethostname().split(".")[0],
        "token": c.get("BOT_TOKEN", ""),
    }

# ---------------------------------------------------------------- commands (server side)
def run(argv, timeout=60):
    try:
        r = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        out = r.stdout.decode("utf-8", "replace")
        return r.returncode == 0, "\n".join(out.strip().splitlines()[-40:])
    except subprocess.TimeoutExpired:
        return False, "timed out after %d s" % timeout
    except OSError as e:
        return False, str(e)

def unit(name): return "omp-247-%s.service" % name

def fetch_latest():
    """The newest kit files from the wagu repo into /opt/omp-247 (install_247.sh / update)."""
    import urllib.request
    os.makedirs(LIB, exist_ok=True); got = []
    for f in FILES:
        try:
            data = urllib.request.urlopen(RAW + f, timeout=30).read()
            if len(data) < 200: raise OSError("short file")
            tmp = os.path.join(LIB, f + ".new"); open(tmp, "wb").write(data); os.chmod(tmp, 0o755)
            os.replace(tmp, os.path.join(LIB, f)); got.append(f)
        except Exception as e:
            return False, "download %s failed: %s" % (f, e)
    return True, "updated " + ", ".join(got)

HELP = ("commands: list | install <name> <port> <server_dir> | status <name> | start|stop|restart <name> | "
        "backup <name> | logs <name> [lines] | update | ping")

def do(words):
    if not words: return False, HELP
    c, a = words[0].lower(), words[1:]
    if c in ("help", "?"): return True, HELP
    if c == "ping": return True, "pong from %s" % socket.gethostname()
    if c == "list":
        ok, out = run(["systemctl", "list-units", "omp-247-*", "--all", "--no-legend", "--plain"])
        return ok, out or "no open.mp 24/7 servers on this machine yet (install <name> <port> <server_dir>)"
    if c == "update":
        ok, out = fetch_latest()
        if ok:
            run(["bash", "-c", "systemctl daemon-reload; for u in $(systemctl list-units 'omp-247-*' --no-legend --plain | awk '{print $1}'); do systemctl restart \"$u\"; done"])
            threading.Timer(3, lambda: os._exit(0)).start()   # systemd restarts the bot on the new file
            out += "; servers restarted, bot restarting"
        return ok, out
    if not a or not NAME_RE.match(a[0]) or a[0] == "bot": return False, "give a server name (letters, digits, _ and -)"
    n = a[0]
    if c == "install":
        if len(a) < 3: return False, "install <name> <port> <server_dir>   e.g. install myserver 7777 /home/samp/Server"
        if not a[1].isdigit() or not 0 < int(a[1]) < 65536: return False, "port must be 1-65535"
        d = os.path.realpath(a[2])
        if not a[2].startswith("/") or not os.path.isdir(d): return False, "server_dir must be an existing absolute folder: %s" % a[2]
        inst = os.path.join(LIB, "install_247.sh")
        if not os.path.exists(inst):
            ok, out = fetch_latest()
            if not ok: return False, out
        return run(["bash", inst, d, n, a[1]], timeout=900)
    if c == "status":
        ok, st = run(["systemctl", "is-active", unit(n)])
        _, lg = run(["journalctl", "-u", unit(n), "-n", "8", "--no-pager", "-o", "cat"])
        return ok, "%s: %s\n%s" % (n, st, lg)
    if c in ("start", "stop", "restart"):
        ok, out = run(["systemctl", c, unit(n)], timeout=60)
        _, st = run(["systemctl", "is-active", unit(n)])
        return ok, "%s %s: %s %s" % (c, n, st, out)
    if c == "backup":
        ok, out = run(["systemctl", "kill", "--kill-whom=main", "-s", "USR1", unit(n)])
        return ok, ("backup asked - it shows in: logs %s" % n) if ok else out
    if c == "logs":
        lines = a[1] if len(a) > 1 and a[1].isdigit() else "30"
        return run(["journalctl", "-u", unit(n), "-n", str(min(int(lines), 100)), "--no-pager", "-o", "cat"])
    return False, "unknown command '%s' - %s" % (c, HELP)

def serve():
    c = bot_cfg(); me = "%s/%s" % (BASE, c["bot"])
    print("omp_autobot: %s:%d as %s, commands on %s/cmd and %s/all/cmd" % (c["host"], c["port"], c["user"], me, BASE), flush=True)
    busy = threading.Lock()
    while True:
        try:
            m = Mqtt(c["host"], c["port"], c["user"], c["pw"], "omp-bot-%s" % c["bot"],
                     will=(me + "/online", json.dumps({"bot": c["bot"], "online": False})), tls=c["tls"])
            m.publish(me + "/online", json.dumps({"bot": c["bot"], "online": True, "t": int(time.time())}), retain=True)
            m.subscribe(me + "/cmd", BASE + "/all/cmd")
            print("omp_autobot: connected", flush=True)
            last_ping = time.time()
            while True:
                t, p = m.read(20)
                if time.time() - last_ping > 25: m.ping(); last_ping = time.time()
                if t != 0x30: continue
                topic, text = parse_publish(p)
                rid = ""; words = []
                try:
                    j = json.loads(text)
                    if isinstance(j, dict):
                        rid = str(j.get("id", "")); text = str(j.get("cmd", ""))
                except ValueError:
                    pass
                words = text.split()
                if c["token"]:
                    if not words or words[0] != c["token"]:
                        m.publish(me + "/reply", json.dumps({"id": rid, "ok": False, "done": True, "out": "wrong or missing token"})); continue
                    words = words[1:]
                if words and words[0].startswith("id="): rid = words[0][3:]; words = words[1:]
                shown = " ".join(words)[:120]
                print("omp_autobot: command '%s' (from %s)" % (shown, topic), flush=True)
                def job(words=words, rid=rid, shown=shown):
                    with busy:   # one command at a time (an install can take minutes)
                        m.publish(me + "/reply", json.dumps({"id": rid, "cmd": shown, "ok": True, "done": False, "out": "running..."}))
                        try: ok, out = do(words)
                        except Exception as e: ok, out = False, "error: %s" % e
                        try: m.publish(me + "/reply", json.dumps({"id": rid, "cmd": shown, "ok": ok, "done": True, "out": out}))
                        except OSError: pass
                threading.Thread(target=job, daemon=True).start()
        except Exception as e:
            print("omp_autobot: %s - again in 10 s" % e, flush=True); time.sleep(10)

# ---------------------------------------------------------------- send (any console)
def send(argv):
    import argparse
    ap = argparse.ArgumentParser(prog="omp_autobot.py send", add_help=False,
                                 description="Send a command to omp_autobot and print its answer.")
    ap.add_argument("-H", "--host", required=True); ap.add_argument("-p", "--port", type=int, default=0)
    ap.add_argument("-u", "--user", default=""); ap.add_argument("-P", "--password", default=os.environ.get("MQTT_PASS", ""))
    ap.add_argument("-b", "--bot", default="all", help="bot name (the server's host name), default: all")
    ap.add_argument("-t", "--token", default=os.environ.get("BOT_TOKEN", ""))
    ap.add_argument("--tls", action="store_true"); ap.add_argument("-w", "--wait", type=int, default=0)
    ap.add_argument("--help", action="help"); ap.add_argument("command", nargs="+")
    o = ap.parse_args(argv)
    port = o.port or (8883 if o.tls else 1883)
    rid = "%x" % int(time.time() * 1000)
    words = ([o.token] if o.token else []) + ["id=" + rid] + o.command
    wait = o.wait or (900 if o.command[0] == "install" else 90)
    try: m = Mqtt(o.host, port, o.user, o.password, "omp-send-%s" % rid, tls=o.tls)
    except OSError as e: print("cannot reach %s:%d - %s" % (o.host, port, e)); return 3
    m.subscribe("%s/+/reply" % BASE)
    time.sleep(0.5)
    m.publish("%s/%s/cmd" % (BASE, o.bot), " ".join(words))
    print("sent to %s: %s (waiting up to %d s)" % (o.bot, " ".join(o.command), wait), flush=True)
    end = time.time() + wait; done = 0; last_ping = time.time()
    while time.time() < end:
        t, p = m.read(5)
        if time.time() - last_ping > 25: m.ping(); last_ping = time.time()
        if t != 0x30: continue
        topic, text = parse_publish(p)
        try: j = json.loads(text)
        except ValueError: continue
        if j.get("id") != rid: continue
        who = topic.split("/")[-2]
        print("[%s] %s%s" % (who, "" if j.get("ok") else "FAILED: ", j.get("out", "")), flush=True)
        if j.get("done"):
            done += 1
            if o.bot != "all": return 0 if j.get("ok") else 1
    if not done: print("no answer - is omp-autobot running on the server, and the bot name right?"); return 2
    return 0

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "serve": serve()
    elif len(sys.argv) > 1 and sys.argv[1] == "send": sys.exit(send(sys.argv[2:]))
    elif len(sys.argv) > 1 and sys.argv[1] == "fetch": ok, out = fetch_latest(); print(out); sys.exit(0 if ok else 1)
    else: print(__doc__); sys.exit(1)
