#!/usr/bin/env python3
"""Fake MP server for the relay test: answers PKT_QUERY (14) with PKT_INFO (15), echoes the rest.
fake_mp.py PORT DELAY_MS NAME"""
import socket, struct, sys, time, threading
port, delay, name = int(sys.argv[1]), int(sys.argv[2]) / 1000.0, sys.argv[3]
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", port))
def reply(d, a):
    time.sleep(delay)
    if len(d) == 5 and d[0] == 14:
        t = struct.unpack("<I", d[1:5])[0]
        s.sendto(struct.pack("<BIHBBB32s", 15, t, 4, 3, 100, 0, name.encode()[:32]), a)
    else:
        s.sendto(b"ECHO:" + name.encode() + b":" + d, a)
while True:
    d, a = s.recvfrom(65536)
    threading.Thread(target=reply, args=(d, a), daemon=True).start()
