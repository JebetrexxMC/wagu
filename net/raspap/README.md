# RaspAP: a Raspberry Pi LAN-party hotspot for MP

[RaspAP](https://github.com/JebetrexxMC/raspap-webgui) turns a Raspberry Pi (or any Debian box with Wi-Fi) into a router or hotspot. Phones join its Wi-Fi and play **MP LAN Server** on the Pi's network. With a relay on the Pi, they can also reach an internet server through its uplink (Ethernet or USB tethering).

## The MPNetWire plugin

`MPNetWire/` is a RaspAP plugin in the format of [JebetrexxMC/plugins](https://github.com/JebetrexxMC/plugins). It adds an "MP NetWire" page to the RaspAP sidebar:
- start, stop and restart buttons for `mpnet-watch` and `mpnet-relay`;
- the live wire table from the local `mpnetd watch` (`:8090/wires.json`).

## Setup

```
curl -sL https://install.raspap.com | bash                  # RaspAP itself
bash net/manage/mpctl.sh netwire                            # mpnetd watch :8090 + relay :7777 as services
sudo bash net/raspap/install-plugin.sh                      # copies MPNetWire + its sudoers lines
```

Then add the Pi to `net/wires.txt` and to the relay's upstreams as you like. `mpnet-handy` (net/handy) is a small ping reflector you can also run on the Pi, so phones see the hotspot's own ping.

The plugin is checked with `php -l` in CI. It hasn't been tested inside a running RaspAP yet.
