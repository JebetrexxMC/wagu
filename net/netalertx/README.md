# NetAlertX (LAN watch for MP Wi-Fi / LAN play)

[NetAlertX](https://github.com/JebetrexxMC/NetAlertX) scans the local network and alerts on new, missing or changed devices. For MP it is useful when a phone or PC hosts an MP / open.mp server on home Wi-Fi or at a LAN party: you see which devices are on the network, when the host box drops, and you can send alerts to the same MQTT broker the MP lobby uses.

```sh
cd net/netalertx && docker compose up -d     # web UI on http://<host>:20211
```

In Settings: set the scan subnet (for example `192.168.1.0/24 --interface=wlan0`), turn on the MQTT publisher if you want alerts in Home Assistant / a Discord bridge, and give the MP server box a fixed name so "down" alerts are clear.
Linux host only (network_mode: host).
