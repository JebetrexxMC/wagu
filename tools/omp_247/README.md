# openmp_247.sh - open.mp / SA-MP server 24/7

## Install on a server (5.02.18, systemd)

    sudo bash tools/omp_247/install_247.sh /home/samp/Server myserver 7777

- Makes the service `omp-247-myserver` (starts on boot, restarts the runner if it dies) and `/etc/omp-247/myserver.yml`.
- Backups every 30 min into `/var/backups/omp-247/myserver` (newest 48 kept): `scriptfiles/`, logs, `config.json` / `server.cfg`.
- HiveMQ status: with the MP HiveMQ kit on the same server (`/etc/mp-hivemq/mp.env`), it publishes a retained JSON on
  `thirtyfox/mp4/omp/myserver/status` as user `bridge` (BRIDGE_PASS). Example:
  `{"online":true,"name":"myserver","host":"1.2.3.4","port":7777,"state":"running","players":3,"max":50,"hostname":"...","restarts_hour":0,"last_backup":"..."}`.
  When the server dies the broker sends the last will `{"name":"myserver","online":false,"state":"offline"}`.
  Watch it: `mosquitto_sub -h 127.0.0.1 -u bridge -P "$BRIDGE_PASS" -t 'thirtyfox/mp4/omp/#' -v`
- Logs: `journalctl -u omp-247-myserver -f`. Console: `echo gmx > /home/samp/Server/omp_console`.

## Run by hand

    bash tools/omp_247/openmp_247.sh tools/omp_247/raknet_247.yml

- Restarts the server when it crashes, or when it stops answering the SA-MP query (`health_fails` checks in a row).
- Backs up `scriptfiles/` and the logs every `backup_every` seconds into `backup_dir`, keeping the newest `backup_keep`.
- Console: `echo "gmx" > omp_console` types a line into the server console (it runs under `script(1)` so omp-server reads it).
- MQTT (optional, HiveMQ or any broker, needs `mosquitto-clients`): status on `<mqtt_topic>/status`,
  console lines from `<mqtt_topic>/cmd`.
- Stop: Ctrl+C (or kill the script) - it stops the server too.

It keeps the server up; it does not change what a gamemode shows. Textdraws, dialogs and spawns come from the Pawn scripts.
