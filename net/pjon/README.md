# PJON: MP LAN server beacon for boards and boxes

[PJON](https://github.com/JebetrexxMC/PJON) is a multi-master network protocol that runs on Arduino, ESP32 and Linux. Its `LocalUDP` strategy broadcasts on the subnet (UDP 7100).

`mp-pjon-beacon.cpp` announces an MP LAN server to every PJON device on the same Wi-Fi or hotspot once a second, with no broker:

```
make PJON=/path/to/PJON            # needs C++14 (PJON's Linux interface)
./mp-pjon-beacon send 7777 "MP LAN Server" 3 50
./mp-pjon-beacon listen            # device 77: MPLAN 7777 3/50 MP LAN Server
```

An ESP32 with an LCD on the same bus can show the servers at a LAN party. The game itself still uses RakNet on 7777. PJON is only the "who is up" side channel, next to mDNS in `net/p2p`.
Tested here: the listener got the beacon's packets on the same host.
