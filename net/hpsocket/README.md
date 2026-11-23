# HP-Socket (reference)

[HP-Socket](https://github.com/JebetrexxMC/HP-Socket) (ldcsaa) is a C/C++ TCP, UDP and HTTP component for Windows, Linux and macOS, with bindings for .NET, Java and Python. Its UDP side is ARQ-reliable UDP, and its TCP "pack" mode puts a length header on each message.

It isn't built into NetWire:
- The game ports speak RakNet. A different reliable-UDP layer can't talk to SA-MP / open.mp servers or to the phone game, so it could only carry side channels (admin, lobby). For those, NetWire already has MQTT, the GameNetworkingSockets lane test (`net/gns`) and libp2p (`net/p2p`).
- Its Linux build needs its own scripts and prebuilt dependencies (jemalloc, OpenSSL, zlib, brotli), and most of the docs are in Chinese.

It's kept as a reference for a Windows server tool or for the launcher's backend, if it ever needs a native TCP / HTTP stack instead of Rust's.
