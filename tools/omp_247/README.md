# openmp_247.sh - open.mp / SA-MP server 24/7

## Install on a server (5.02.18, systemd)

    curl -fsSLO https://github.com/JebetrexxMC/wagu/releases/latest/download/install_247.sh
    sudo bash install_247.sh /home/samp/Server myserver 7777

- Makes the service `omp-247-myserver` (starts on boot, restarts the runner if it dies) and `/etc/omp-247/myserver.yml`.
- Backups every 30 min into `/var/backups/omp-247/myserver` (newest 48 kept): `scriptfiles/`, logs, `config.json` / `server.cfg`.
- HiveMQ status: with the MP HiveMQ kit on the same server (`/etc/mp-hivemq/mp.env`), it publishes a retained JSON on
  `thirtyfox/mp4/omp/myserver/status` as user `bridge` (BRIDGE_PASS). Example:
  `{"online":true,"name":"myserver","host":"1.2.3.4","port":7777,"state":"running","players":3,"max":50,"hostname":"...","restarts_hour":0,"last_backup":"..."}`.
  When the server dies the broker sends the last will `{"name":"myserver","online":false,"state":"offline"}`.
  Watch it: `mosquitto_sub -h 127.0.0.1 -u bridge -P "$BRIDGE_PASS" -t 'thirtyfox/mp4/omp/#' -v`
- Logs: `journalctl -u omp-247-myserver -f`. Console: `echo gmx > /home/samp/Server/omp_console`.

## Autobot: control it from HiveMQ, Termux or any console (5.02.20)

The installer also starts **omp-autobot** (as root) when a broker is found. It listens on
`thirtyfox/mp4/omp/bot/<bot>/cmd` (and `thirtyfox/mp4/omp/bot/all/cmd`) and answers on `thirtyfox/mp4/omp/bot/<bot>/reply`.
`<bot>` = the server's host name (`BOT_NAME` in `/etc/omp-247/autobot.env`). Retained `.../<bot>/online` shows whether it is up.

| Command (plain text) | Does |
|---|---|
| `install myserver 7777 /home/samp/Server` | runs install_247.sh for that folder (the 24/7 service + backups + status) |
| `list` | every omp-247 service and its state |
| `status myserver` / `logs myserver 50` | state + the last log lines |
| `start` / `stop` / `restart myserver` | the service |
| `backup myserver` | a backup now (into /var/backups/omp-247/myserver) |
| `update` | the newest 24/7 files from GitHub, restarts the servers and the bot |
| `ping` / `help` | |

Only these commands run, never a shell line. Who may send them is decided by the broker login: on the MP HiveMQ kit only
`admin` and `bridge` may use `thirtyfox/mp4/omp/#` (run the kit update, console menu 7, to get this rule). `BOT_TOKEN=secret`
at install time adds a password word that every command must start with.

**First time on the server** (SSH, Termius, or the web console's shell, menu 9):

    curl -fsSLO https://github.com/JebetrexxMC/wagu/releases/latest/download/install_247.sh
    sudo bash install_247.sh /home/samp/Server myserver 7777      # server + autobot
    sudo bash install_247.sh --bot                                # or only the autobot, then "install ..." over MQTT

**HiveMQ WebSocket client in the browser** ([hivemq.com/demo/websocket-client](https://www.hivemq.com/demo/websocket-client/)):
host = your broker, port 8000 (or 8884 with SSL), user `admin` + its password. Subscribe to `thirtyfox/mp4/omp/bot/+/reply`
and `thirtyfox/mp4/omp/+/status`, then publish e.g. `install myserver 7777 /home/samp/Server` on `thirtyfox/mp4/omp/bot/all/cmd`.

**Termux (phone) / any Linux console** (open.mp servers can't run on Android; this controls the server):

    pkg install python                        # Termux (Debian/Ubuntu: python3 is there)
    curl -fsSLO https://raw.githubusercontent.com/JebetrexxMC/wagu/main/tools/omp_247/omp_autobot.py
    python3 omp_autobot.py send -H your.broker -u admin -P 'pass' install myserver 7777 /home/samp/Server
    python3 omp_autobot.py send -H your.broker -u admin -P 'pass' status myserver
    # HiveMQ Cloud / TLS: add --tls (port 8883)

**MP web console** (the browser console of the HiveMQ kit): menu **p) open.mp 24/7 servers + autobot**: install a server,
live log, restart, backup now, stop, install / update the autobot, update the files, live MQTT status.

Other broker (HiveMQ Cloud etc.) for the server and the bot:
`MQTT_HOST=xxx.s1.eu.hivemq.cloud MQTT_PORT=8883 MQTT_TLS=1 MQTT_USER=u MQTT_PASS=p sudo -E bash install_247.sh ...`

## Run by hand

    bash tools/omp_247/openmp_247.sh tools/omp_247/raknet_247.yml

- Restarts the server when it crashes, or when it stops answering the SA-MP query (`health_fails` checks in a row).
- Backs up `scriptfiles/` and the logs every `backup_every` seconds into `backup_dir`, keeping the newest `backup_keep`.
- Console: `echo "gmx" > omp_console` types a line into the server console (it runs under `script(1)` so omp-server reads it).
- MQTT (optional, HiveMQ or any broker, needs `mosquitto-clients`): status on `<mqtt_topic>/status`,
  console lines from `<mqtt_topic>/cmd`.
- Stop: Ctrl+C (or kill the script) - it stops the server too.

It keeps the server up; it does not change what a gamemode shows. Textdraws, dialogs and spawns come from the Pawn scripts.
