# mpadmin - MP admin console for Termux

One console on the phone for the VPS, its firewall, the global VPS network and MP's logs. It talks to the VPS
through the HiveMQ broker and `omp_autobot` (no SSH needed), and uses the
[Termux add-ons](https://github.com/orgs/termux/repositories):

| Termux part | used for |
|---|---|
| [termux-app](https://github.com/termux/termux-app) + [termux-packages](https://github.com/termux/termux-packages) | python, curl, openssh |
| [termux-api](https://github.com/termux/termux-api) / [termux-api-package](https://github.com/termux/termux-api-package) | notifications (firewall installed, VPS switched, no VPS answers), battery, wifi in `status` |
| [termux-services](https://github.com/termux/termux-services) | the edge client as a background service `mp-edge` (`sv up/down`, log in `$PREFIX/var/log/sv/mp-edge`) |
| [termux-boot](https://github.com/termux/termux-boot) | starts `mp-edge` after a phone restart |
| [termux-widget](https://github.com/termux/termux-widget) | 6 home-screen buttons (Admin, Status, Firewall, Firewall install, Fastest VPS, Logs) |
| [proot-distro](https://github.com/termux/proot-distro) | `mpadmin lab`: global center + 2 VPS test in Debian on the phone |
| [termux-auth](https://github.com/termux/termux-auth) | `mpadmin lock`: a password for Termux |

## Install

```bash
curl -fsSL https://raw.githubusercontent.com/JebetrexxMC/wagu/main/tools/termux_admin/mpadmin.sh -o $PREFIX/bin/mpadmin && chmod +x $PREFIX/bin/mpadmin && mpadmin setup
```

`setup` asks once for: the global center URL (or VPS seeds), the VPS SSH login (optional), and the VPS broker
login for the bot (host `mpsamp.duckdns.org`, user `admin`, bot name = the VPS host name or `all`).

## Use

| command | what |
|---|---|
| `mpadmin` | menu |
| `mpadmin fw install` | xdp-firewall with the MP rules on the VPS (the bot builds it, a few minutes); `fw status`, `fw off` |
| `mpadmin bot botupdate` | newest omp_autobot on the VPS (needed once before `fw`, older bots answer "unknown command") |
| `mpadmin status` | phone battery / wifi, bot ping, firewall, fastest VPS |
| `mpadmin edge once` / `on` / `off` / `log` | fastest VPS now / in the background with notifications |
| `mpadmin log` | copies mp_log.txt and aml_crashlog.txt to Downloads/mp-logs-... to send |
| `mpadmin ssh key` | SSH key for the VPS (password once), then `mpadmin ssh run 'uptime'` or `mpadmin ssh login` |
| `mpadmin lab` | Debian (proot-distro) test of the global VPS network on the phone |

The VPS side needs `omp_autobot` running (installed by `install_247.sh` / the HiveMQ kit). On the VPS itself the
same firewall is in the HiveMQ web console menu `x` (install), `X` (status), `f` (off).
