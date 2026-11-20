#!/usr/bin/env python3
# 5.02.10: join every server on the open.mp list once (remote role only) and flag spawn / class-selection
# loops, kicks and "no InitGame". Usage: spawn_sweep.py [max_servers] [parallel]  -> results/spawn-<date>.tsv
import json, os, re, subprocess, sys, time, urllib.request
from concurrent.futures import ThreadPoolExecutor
T = os.environ.get("MP_TEST_BIN", "/tmp/sampgate_test")
N = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
P = int(sys.argv[2]) if len(sys.argv) > 2 else 40
WAIT = os.environ.get("MP_GATE_WAIT", "20000")
lst = json.load(urllib.request.urlopen(urllib.request.Request("https://api.open.mp/servers", headers={"User-Agent": "curl/8"}), timeout=30))
lst = [s for s in lst if not s.get("pa")]          # password servers need the password
lst.sort(key=lambda s: -s.get("pc", 0)); lst = lst[:N]
rpc = re.compile(r"\[crossplay rpc\] (\d+) ")
def one(a):
    i, s = a
    host, port = s["ip"].rsplit(":", 1)
    env = dict(os.environ, MP_GATE_DEBUG="1", MP_GATE_WAIT=WAIT, MP_TEST_PORT=str(21000 + (i % P) * 4), MP_TEST_REMOTE_ONLY="1")
    try: out = subprocess.run([T, host, port], env=env, capture_output=True, timeout=90).stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e: out = (e.stdout or b"").decode("utf-8", "replace") + "\nTIMEOUT"
    c = {}
    for m in rpc.finditer(out): c[m.group(1)] = c.get(m.group(1), 0) + 1
    st = re.search(r"messages (\d+), dialogs (\d+), textdraws (\d+), samp ops (\d+), cg acts (\d+)", out)
    msgs, dlg, tds, ops, cg = map(int, st.groups()) if st else (0, 0, 0, 0, 0)
    joined = "ok   gate joined the PC server" in out
    kicked = "closed the connection" in out
    cap = "keeps you in class selection" in out
    loop = c.get("74", 0) > 3 or c.get("128", 0) > 4 or c.get("129", 0) > 6 or cg > 400 or tds > 800
    res = "loop" if loop else ("kicked" if kicked else ("stayed" if joined else "no_initgame"))
    last = ""
    for l in out.splitlines():
        if l.startswith("  msg ") and "[open.mp]" not in l and l.split(":", 1)[1].strip(): last = l.split(":", 1)[1].strip()[:90]
    if res in ("loop", "kicked", "no_initgame"):
        open("/tmp/wide/%s_%s.log" % (host, port), "w").write(out)
    top = ",".join("%s:%d" % kv for kv in sorted(c.items(), key=lambda kv: -kv[1])[:6])
    return "\t".join(map(str, [s["ip"], s.get("pc", 0), s.get("vn", ""), res, c.get("74", 0), c.get("128", 0), c.get("129", 0), c.get("68", 0), tds, cg, dlg, int(cap), top, last, s.get("hn", "")[:60].replace("\t", " ")]))
t0 = time.time()
rows = []
with ThreadPoolExecutor(P) as ex:
    for k, r in enumerate(ex.map(one, enumerate(lst))):
        rows.append(r)
        if k % 50 == 0: print(k, round(time.time() - t0), r.split("\t")[3], file=sys.stderr, flush=True)
hdr = "addr\tplayers\tversion\tresult\tforce_class\tclass_ans\tspawn_ans\tspawn_info\ttextdraws\tcg_acts\tdialogs\tcapped\ttop_rpcs\tlast_msg\tname"
outp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results", "spawn-%s.tsv" % time.strftime("%Y-%m-%d"))
open(outp, "w").write(hdr + "\n" + "\n".join(rows) + "\n")
from collections import Counter
print(outp, Counter(r.split("\t")[3] for r in rows))
