# XPipe for MP

[XPipe](https://github.com/JebetrexxMC/xpipe) is a desktop connection hub (Windows / macOS / Linux): one window for all your servers, with shells, a file browser, SSH tunnels and saved scripts. Latest: 24.13 ([download](https://xpipe.io/download)).

1. Copy the hosts from [`ssh_config`](ssh_config) into `~/.ssh/config` (set your user and key). XPipe picks them up by itself: **mp-asia, mp-eu, mp-na, mp-oce, mp-oracle**, the ZeroTier variant and a status tunnel.
2. In XPipe, add [`../manage/mpctl.sh`](../manage/mpctl.sh) as a script (Scripts -> New -> file) so every box gets the same buttons:

| command | does |
|---|---|
| `mpctl status` | MP services, ports, mpnetd health |
| `mpctl restart [unit]` | restart mp-server (or any mp-* unit) |
| `mpctl logs [unit]` | last 100 log lines |
| `mpctl update` | git pull wagu + rebuild mpnetd |
| `mpctl netwire` | install mpnetd watch (:8090) + relay (:8091) as services |
| `mpctl ports` | which MP ports listen locally, and from outside |
| `mpctl zerotier ID` | join a ZeroTier network |
| `mpctl tunnel PORT` | share a TCP port through bore |

Without XPipe the same works over plain SSH: `ssh mp-asia 'bash -s' < net/manage/mpctl.sh status`.
