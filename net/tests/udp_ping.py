#!/usr/bin/env python3
# NetWire UDP ping check: sends "MPNETPNG" + 4-byte token + 4 zero bytes, expects the same token back.
import os, socket, sys, time
host, port = sys.argv[1], int(sys.argv[2])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(1.0)
ok = 0
for i in range(5):
    tok = os.urandom(4); t = time.time()
    s.sendto(b"MPNETPNG" + tok + b"\0" * 4, (host, port))
    try:
        d, _ = s.recvfrom(64)
        if d[:8] == b"MPNETPNG" and d[8:12] == tok:
            ok += 1; print("reply %.2f ms" % ((time.time() - t) * 1000))
    except socket.timeout:
        print("timeout")
print("%d/5 replies" % ok)
sys.exit(0 if ok >= 4 else 1)
