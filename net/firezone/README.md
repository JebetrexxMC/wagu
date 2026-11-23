# Firezone gateway (private MP admin lanes)

[Firezone](https://github.com/JebetrexxMC/firezone) is a zero-trust access layer on WireGuard. The gateway runs on the MP / open.mp server box; the people you allow connect with the Firezone client (Windows, macOS, Linux, Android, iOS) and reach only the ports you list as Resources.

Use it for what should not be public: rcon, the MQTT admin login, the mpnetd relay control port, the VPS panel, `mpnet-ws` (net/server) on a private port.

```sh
cd net/firezone
FIREZONE_TOKEN=<token from app.firezone.dev> docker compose up -d
docker compose logs -f
```

Then add a wire for the private path in `net/wires.txt` if a NetWire box sits inside the same Firezone site. Game traffic for normal players stays on the public port (Firezone is for admins and private lanes, not a game relay).
