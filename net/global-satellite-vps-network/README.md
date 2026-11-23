# MP global satellite VPS network

One **global center** knows every **continental VPS** (NA, EU, ASIA, SA, OC). Each VPS reports itself every
5 s and gets the list of the others back. An **edge client** (PC, Termux on the phone, Raspberry Pi) measures the
real round-trip time to every VPS, uses the fastest one and fails over to the next one when it stops answering -
the same idea as dual-WAN failover on a router, applied to VPS instead of internet links. It also works over
satellite internet (Starlink / LEO, GEO) by measuring instead of guessing from location.

```
                 global-center :8000   (/global/sync, /global/regions, /global/best, /global/wires.txt)
        sync every 5 s  ^    ^    ^    ^    ^   peers back
                 vps-na  vps-eu  vps-asia  vps-sa  vps-oc   :8001-8005  (/ping, /status, /peers, /edge/hello)
                        ^ timed GET /ping, pick + failover
                   edge_client.py  ->  mp_wires.txt (net/wires.txt format, group vps)
```

| path | what |
|---|---|
| `global_center/global_center.py` | FastAPI. `POST /global/sync` (VPS report, optional `X-MP-Token` = `GLOBAL_TOKEN`), `GET /global/regions`, `GET /global/best?lat=&lon=` or `?continent=` (distance ~1 ms / 100 km + uplink + load), `GET /global/wires.txt`, `/health`. A VPS that has not synced for `STALE_S` (20 s) is dropped. |
| `continental_vps/continental_vps.py` | FastAPI. `/ping`, `/status`, `/peers` (last list from the center, kept when the center is down), `POST /edge/hello`. Env: `CONTINENT`, `PORT`, `GLOBAL_CENTER_URL`, `PUBLIC_URL`, `GAME_HOST`/`GAME_PORT` (MP server, UDP 1922), `MQTT_HOST`/`MQTT_PORT`, `LAT`/`LON`, `MAX_PLAYERS`, `UPLINK` (fiber, leo, geo, cellular), `GLOBAL_TOKEN`, `SYNC_S`. Retries with backoff up to 60 s. |
| `edge_client/edge_client.py` | Python standard library only. `--center URL`, `--seed URL` (repeat, works without the center), `--interval 10`, `--samples 4`, `--fail 2`, `--margin 20`, `--once` (JSON), `--wires-out FILE`. |
| `docker-compose.yml` | the whole network on one machine for testing (center + 5 VPS, region brokers from net/wires.txt). |
| `pgdog/` | PgDog config: one account database for all regions, reads from the local replica. |

## Run

```bash
docker compose up -d
python3 edge_client/edge_client.py --center http://127.0.0.1:8000 --once      # JSON: active VPS, link type, ranking
curl -s http://127.0.0.1:8000/global/wires.txt                                # alive VPS as NetWire lines
curl -s "http://127.0.0.1:8000/global/best?lat=8.48&lon=124.65"              # nearest by location (Cagayan de Oro)
```

Without Docker: `pip install fastapi uvicorn httpx pydantic`, then
`PORT=8000 python3 global_center/global_center.py` and per VPS
`CONTINENT=ASIA PORT=8003 GLOBAL_CENTER_URL=http://CENTER:8000/global/sync PUBLIC_URL=http://THIS_VPS:8003 GAME_HOST=mpsamp.duckdns.org python3 continental_vps/continental_vps.py`.

On a real network run the center on one box and **one** `vps-*` per real VPS, with `PUBLIC_URL` set to the
address players can reach, the same `GLOBAL_TOKEN` everywhere, and xdp-firewall in front (`net/xdp-firewall`).

Tested (local, 3 VPS): ranking for `continent=ASIA` = ASIA 0 ms, EU 95 ms, NA 134 ms (estimate); the edge
picked ASIA, failed over to EU 4 s after ASIA was killed, kept working from cached `/peers` after the center was
killed, then failed over to NA when EU was killed.

## Satellite links

- The edge client reports `link`: `terrestrial`, `high-latency` (best VPS > 120 ms) or `geo-satellite` (> 480 ms -
  a GEO satellite hop is ~600 ms). On GEO pick the lowest-RTT region and expect longer interpolation.
- LEO (Starlink) RTT changes every 15 s with satellite handovers; `--samples` + median + `--margin` keep the edge
  from switching VPS on every jump.
- A VPS on a satellite or cellular uplink sets `UPLINK=leo|geo|cellular`; the center adds a penalty so
  `/global/best` prefers fiber VPS.

## Repos used

| repo | used for |
|---|---|
| [librespacefoundation/satnogs](https://gitlab.com/librespacefoundation/satnogs) | SatNOGS global ground station network: the model of one central server + many stations that report in. |
| [JebetrexxMC/ground-station](https://github.com/JebetrexxMC/ground-station) (sgoudelis) | browser ground station (tracking, SDR, telemetry). Reference for the status / pass views; nothing vendored. |
| [JebetrexxMC/ground-station-optimizer](https://github.com/JebetrexxMC/ground-station-optimizer) (duncaneddy, IEEE Aerospace 2024) | picking the best set of stations for coverage: the same question as where to place the 5 VPS. |
| [JebetrexxMC/awesome-satellite-network](https://github.com/JebetrexxMC/awesome-satellite-network) | papers on LEO / GEO latency and handovers behind the link classes above. |
| [JebetrexxMC/geo-loadbalanced-service](https://github.com/JebetrexxMC/geo-loadbalanced-service) | geo load balancing: `/global/best` (nearest + load). |
| [JebetrexxMC/pgdog](https://github.com/JebetrexxMC/pgdog) | `pgdog/`: one Postgres for accounts, primary + local replica per VPS. |
| [JebetrexxMC/linux-dual-wan-failover](https://github.com/JebetrexxMC/linux-dual-wan-failover) | failover rules (N failed probes, then switch) in the edge client. |
| [JebetrexxMC/FailOver-Routing](https://github.com/JebetrexxMC/FailOver-Routing) | multi-WAN failover on a Raspberry Pi: the edge client runs there the same way. |
