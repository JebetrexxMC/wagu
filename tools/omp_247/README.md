# openmp_247.sh - open.mp / SA-MP server 24/7

    bash tools/omp_247/openmp_247.sh tools/omp_247/raknet_247.yml

- Restarts the server when it crashes, or when it stops answering the SA-MP query (`health_fails` checks in a row).
- Backs up `scriptfiles/` and the logs every `backup_every` seconds into `backup_dir`, keeping the newest `backup_keep`.
- Console: `echo "gmx" > omp_console` types a line into the server console (it runs under `script(1)` so omp-server reads it).
- MQTT (optional, HiveMQ or any broker, needs `mosquitto-clients`): status on `<mqtt_topic>/status`,
  console lines from `<mqtt_topic>/cmd`.
- Stop: Ctrl+C (or kill the script) - it stops the server too.

It keeps the server up; it does not change what a gamemode shows. Textdraws, dialogs and spawns come from the Pawn scripts.
