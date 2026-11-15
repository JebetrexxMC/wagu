# Servers and hosting


## Kinds of servers

- **Default server:** tap Connect. The first player hosts "MP LAN Server" on port 1922, and everyone on the same Wi-Fi or hotspot joins it.
- **Custom server:** under "Host your own server", set a name and a port (1923 or higher), then turn on Host server. Other players tap Join in the Servers tab.
- **Online:** players on different networks, including mobile data, meet through the internet lobby (MQTT Broker).
- **If the host leaves:** another player takes over as host, and everyone reconnects by themselves.

## MP Launcher

The launcher has Internet, Favorites and LAN lists with up to 500 servers. Each server shows its ping, players, mode and language, and has an info panel.

## 24/7 server on a spare phone (Termux)

```
curl -fsSL https://mpsamp-open.github.io/install.sh | bash
```

Manage it with `mp-server status | log | config | restart | stop | update`. Keep the phone charging and on Wi-Fi, and set Termux's battery use to Unrestricted.

## 24/7 server on Debian 12 / Ubuntu

```
wget -qO- https://mpsamp-open.github.io/HiveMQ/vps-setup.sh | bash -s europe
```

The regions are `asia`, `europe`, `na` and `oceania`. Open UDP port 1922 on the firewall or router.
