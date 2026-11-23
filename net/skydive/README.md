# Skydive: live topology and flows of an MP box

[Skydive](https://github.com/JebetrexxMC/skydive) shows interfaces, bridges, Docker and netns as a live graph and captures flows. Use it on the box that runs the MP servers or the relay to see where UDP 7777 goes (which container, which tunnel) and how much is lost.

The fork ships an all-in-one compose file (Elasticsearch + analyzer on :8082 + a privileged host agent):

```
curl -fsSLO https://raw.githubusercontent.com/JebetrexxMC/skydive/master/contrib/docker/docker-compose.yml
docker compose up -d                 # UI: http://<box>:8082
# capture only the game traffic on eth0
skydive client capture create --name mp-game --gremlin "G.V().Has('Name', 'eth0')" --bpf "udp port 7777"
```

Notes:
- The agent is privileged and needs the Docker socket. Only run it on boxes you own, and keep 8082 closed (put it behind `net/firezone` or `net/ziti`).
- `net/scanopy` gives a simpler static map. Skydive is for live flows while you debug lag.
