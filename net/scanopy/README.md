# Scanopy: a live map of the MP network

[Scanopy](https://github.com/JebetrexxMC/scanopy) runs a daemon on a box. The daemon scans the box's networks and Docker containers, and the server draws a network diagram that stays up to date.

Put it on the box that runs the MP servers or the relay. The diagram then shows which hosts and ports are there: the game ports, MQTT, mpnetd :8090 / :8091, the query shield and the WireGuard lanes. That is easier to read than a port list when something changes.

Use the fork's own compose file. It contains the daemon (host network, privileged, for scanning), PostgreSQL and the server on :60072.

```
curl -fsSLO https://raw.githubusercontent.com/JebetrexxMC/scanopy/main/docker-compose.yml
POSTGRES_PASSWORD=$(openssl rand -hex 16) docker compose up -d
# open http://<box>:60072, then start a discovery scan of the box
```

Notes:
- Scanopy is AGPL-3.0, with a separate commercial licence.
- The daemon needs `privileged` and the Docker socket. Only run it on boxes you own.
- Keep port 60072 closed to the internet, or put it behind `net/firezone` or OpenZiti (`net/ziti`).
