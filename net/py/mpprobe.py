#!/usr/bin/env python3
"""MP NetWire probe in Python: every wire of wires.txt at the same time.

Event loop: --loop uvloop (JebetrexxMC/uvloop, fastest), asyncio (stdlib) or trio (JebetrexxMC/trio).
Kinds: mp (PKT_QUERY 14 -> PKT_INFO 15), samp (SA-MP / open.mp 'p' ping + 'i' info), udp (MPNETPNG ping),
mqtt (CONNECT -> CONNACK, PINGREQ -> PINGRESP), tcp / tls / http (connect time). lobby wires are skipped.

  python3 mpprobe.py -w ../wires.txt [--loop uvloop|asyncio|trio] [--samples 3] [--json]
Grades are the same as mpnetd: green <=120 ms / jitter <=30 / loss <=20 %, yellow <=250 ms / loss <=50 %.
"""
import argparse, json, os, random, socket, statistics, struct, sys, time

PKT_QUERY, PKT_INFO = 14, 15


# ---------------------------------------------------------------- wires.txt
def load_wires(paths):
    out = []
    for p in paths:
        for ln in open(p, encoding="utf-8", errors="replace"):
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            f = [x.strip() for x in ln.split("|")]
            if len(f) < 4:
                continue
            opt = {}
            for x in f[5:]:
                for kv in x.split():
                    if "=" in kv:
                        k, v = kv.split("=", 1); opt[k] = v
            host, _, port = f[3].rpartition(":")
            if not host:
                host, port = f[3], "0"
            out.append(dict(id=f[0], group=f[1], kind=f[2], host=host, port=int(port or 0),
                            name=f[4] if len(f) > 4 else f[0], opt=opt))
    return out


def grade(rtts, sent):
    if not rtts:
        return dict(grade="down", rtt=None, jitter=None, loss=1.0)
    med = statistics.median(rtts)
    jit = statistics.mean(abs(a - b) for a, b in zip(rtts, rtts[1:])) if len(rtts) > 1 else 0.0
    loss = 1 - len(rtts) / max(sent, 1)
    g = "green" if med <= 120 and jit <= 30 and loss <= 0.2 else "yellow" if med <= 250 and loss <= 0.5 else "red"
    return dict(grade=g, rtt=round(med, 1), jitter=round(jit, 1), loss=round(loss, 2))


# ---------------------------------------------------------------- packets
def mp_query():
    t = random.getrandbits(32)
    return struct.pack("<BI", PKT_QUERY, t), (lambda b: len(b) >= 42 and b[0] == PKT_INFO and b[1:5] == struct.pack("<I", t))


def mp_info(b):
    players, mx = b[7], b[8]
    name = b[10:42].split(b"\0")[0].decode("utf-8", "replace")
    return f"{name} {players}/{mx}"


def samp_query(ip, port, op):
    q = b"SAMP" + socket.inet_aton(ip) + struct.pack("<H", port) + op
    tok = os.urandom(4) if op == b"p" else b""
    return q + tok, (lambda b: len(b) >= 11 and b[:4] == b"SAMP" and b[10:11] == op and (op != b"p" or b[11:15] == tok))


def samp_info(b):
    if len(b) < 20:
        return ""
    pl, mx, hl = struct.unpack("<HHI", b[12:20])
    return f"{b[20:20 + hl].decode('latin-1')} {pl}/{mx}"


def udp_ping():
    t = os.urandom(4)
    return b"MPNETPNG" + t + b"\0" * 4, (lambda b: len(b) >= 12 and b[8:12] == t)


def mqtt_connect(opt):
    user, pw = opt.get("user"), opt.get("pass")
    flags = 0x02 | (0x80 if user else 0) | (0x40 if pw else 0)
    def s(x):
        x = x.encode(); return struct.pack(">H", len(x)) + x
    body = b"\0\x04MQTT\x04" + bytes([flags]) + b"\0\x0a" + s(f"mpnet-py-{random.randrange(10**6)}")
    if user: body += s(user)
    if pw: body += s(pw)
    return b"\x10" + bytes([len(body)]) + body


# ---------------------------------------------------------------- backends
class AsyncioNet:
    def __init__(self):
        import asyncio
        self.a = asyncio

    async def resolve(self, host, port):
        r = await self.a.get_running_loop().getaddrinfo(host, port, family=socket.AF_INET)
        return r[0][4][0]

    async def udp_ask(self, ip, port, payload, match, to):
        # datagram endpoint: works on asyncio and uvloop (uvloop has no loop.sock_sendto)
        loop = self.a.get_running_loop()
        fut = loop.create_future()
        class P(self.a.DatagramProtocol):
            def datagram_received(self, b, addr):
                if not fut.done() and match(b):
                    fut.set_result(b)
            def error_received(self, e):
                pass
        try:
            tr, _ = await loop.create_datagram_endpoint(P, remote_addr=(ip, port))
        except OSError:
            return None
        try:
            t0 = time.perf_counter()
            tr.sendto(payload)
            b = await self.a.wait_for(fut, to)
            return (time.perf_counter() - t0) * 1000, b
        except (OSError, self.a.TimeoutError):
            return None
        finally:
            tr.close()

    async def tcp(self, ip, port, to, send=None, want=0):
        t0 = time.perf_counter()
        try:
            r, w = await self.a.wait_for(self.a.open_connection(ip, port), to)
        except (OSError, self.a.TimeoutError):
            return None
        dt = (time.perf_counter() - t0) * 1000
        try:
            if send:
                w.write(send); await w.drain()
                t0 = time.perf_counter()
                b = await self.a.wait_for(r.readexactly(want), to)
                return (time.perf_counter() - t0) * 1000, b, (r, w)
            return dt, b"", None
        except (OSError, self.a.TimeoutError, self.a.IncompleteReadError):
            return None
        finally:
            if not send:
                w.close()

    async def ask_more(self, conn, data, want, to):
        r, w = conn
        t0 = time.perf_counter()
        try:
            w.write(data); await w.drain()
            await self.a.wait_for(r.readexactly(want), to)
            return (time.perf_counter() - t0) * 1000
        except (OSError, self.a.TimeoutError, self.a.IncompleteReadError):
            return None

    def close(self, conn):
        conn[1].close()

    async def all(self, coros):
        return await self.a.gather(*coros)


class TrioNet:
    def __init__(self):
        import trio
        self.t = trio
        self.err = (OSError, trio.BrokenResourceError, trio.ClosedResourceError)

    async def resolve(self, host, port):
        r = await self.t.socket.getaddrinfo(host, port, family=socket.AF_INET)
        return r[0][4][0]

    async def udp_ask(self, ip, port, payload, match, to):
        s = self.t.socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        with s:
            t0 = time.perf_counter()
            with self.t.move_on_after(to):
                try:
                    await s.sendto(payload, (ip, port))
                    while True:
                        b, _ = await s.recvfrom(65536)
                        if match(b):
                            return (time.perf_counter() - t0) * 1000, b
                except self.err:
                    return None
        return None

    async def _read(self, st, n):
        b = b""
        while len(b) < n:
            x = await st.receive_some(n - len(b))
            if not x:
                raise OSError("closed")
            b += x
        return b

    async def tcp(self, ip, port, to, send=None, want=0):
        t0 = time.perf_counter()
        st = None
        with self.t.move_on_after(to):
            try:
                st = await self.t.open_tcp_stream(ip, port)
            except self.err:
                return None
        if st is None:
            return None
        dt = (time.perf_counter() - t0) * 1000
        if not send:
            await st.aclose(); return dt, b"", None
        with self.t.move_on_after(to):
            try:
                await st.send_all(send); t0 = time.perf_counter()
                b = await self._read(st, want)
                return (time.perf_counter() - t0) * 1000, b, st
            except self.err:
                pass
        await st.aclose()
        return None

    async def ask_more(self, st, data, want, to):
        with self.t.move_on_after(to):
            try:
                t0 = time.perf_counter(); await st.send_all(data); await self._read(st, want)
                return (time.perf_counter() - t0) * 1000
            except self.err:
                return None
        return None

    def close(self, st):
        pass   # closed when the stream is collected; trio streams need await aclose (done by GC here)

    async def all(self, coros):
        res = [None] * len(coros)
        async def run(i, c):
            res[i] = await c
        async with self.t.open_nursery() as n:
            for i, c in enumerate(coros):
                n.start_soon(run, i, c)
        return res


# ---------------------------------------------------------------- probe
async def probe(net, w, samples, to):
    r = dict(id=w["id"], group=w["group"], kind=w["kind"], host=w["host"], port=w["port"], name=w["name"], info="")
    k = w["kind"]
    if k == "lobby" or w["host"] == "lobby":
        r.update(grade="skip", rtt=None, jitter=None, loss=0.0, info="lobby-listed"); return r
    try:
        ip = await net.resolve(w["host"], w["port"])
    except OSError as e:
        r.update(grade="down", rtt=None, jitter=None, loss=1.0, info=f"dns: {e}"); return r
    rtts = []
    if k == "mp":
        for _ in range(samples):
            q, m = mp_query(); a = await net.udp_ask(ip, w["port"], q, m, to)
            if a: rtts.append(a[0]); r["info"] = mp_info(a[1])
    elif k == "samp":
        for _ in range(samples):
            q, m = samp_query(ip, w["port"], b"p"); a = await net.udp_ask(ip, w["port"], q, m, to)
            if a: rtts.append(a[0])
        q, m = samp_query(ip, w["port"], b"i"); a = await net.udp_ask(ip, w["port"], q, m, to)
        if a: r["info"] = samp_info(a[1])
    elif k == "udp":
        for _ in range(samples):
            q, m = udp_ping(); a = await net.udp_ask(ip, w["port"], q, m, to)
            if a: rtts.append(a[0])
    elif k == "mqtt":
        a = await net.tcp(ip, w["port"], to, send=mqtt_connect(w["opt"]), want=4)
        if a and a[1][0] == 0x20 and a[1][3] == 0:
            rtts.append(a[0])
            for _ in range(samples - 1):
                d = await net.ask_more(a[2], b"\xc0\x00", 2, to)
                if d is not None: rtts.append(d)
            net.close(a[2])
        elif a:
            r["info"] = f"CONNACK rc={a[1][3]}"; net.close(a[2])
    else:   # tcp / tls / http: TCP connect time
        for _ in range(samples):
            a = await net.tcp(ip, w["port"], to)
            if a: rtts.append(a[0])
    r.update(grade(rtts, samples))
    return r


async def main_async(net, wires, samples, to):
    return await net.all([probe(net, w, samples, to) for w in wires])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-w", "--wires", action="append")
    ap.add_argument("--loop", choices=["uvloop", "asyncio", "trio"], default="uvloop")
    ap.add_argument("--samples", type=int, default=3)
    ap.add_argument("--timeout", type=float, default=1.5)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    wires = load_wires(a.wires or [os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "wires.txt")])
    loop = a.loop
    t0 = time.perf_counter()
    if loop == "trio":
        import trio
        res = trio.run(main_async, TrioNet(), wires, a.samples, a.timeout)
    else:
        import asyncio
        if loop == "uvloop":
            try:
                import uvloop
            except ImportError:
                print("uvloop not installed (pip install uvloop), using asyncio", file=sys.stderr); loop = "asyncio"
        runner = uvloop.run if loop == "uvloop" else asyncio.run
        res = runner(main_async(AsyncioNet(), wires, a.samples, a.timeout))
    took = time.perf_counter() - t0
    if a.json:
        print(json.dumps(dict(loop=loop, took=round(took, 2), wires=res), indent=1)); return
    for r in res:
        rt = "-" if r["rtt"] is None else f"{r['rtt']:.0f} ms"
        print(f"{r['grade']:7} {r['id']:24} {r['kind']:5} {rt:>8}  loss {int(r['loss'] * 100):3}%  {r['info']}")
    up = sum(r["grade"] not in ("down", "skip") for r in res)
    print(f"{up}/{sum(r['grade'] != 'skip' for r in res)} up  ({loop}, {took:.1f} s)")


if __name__ == "__main__":
    main()
