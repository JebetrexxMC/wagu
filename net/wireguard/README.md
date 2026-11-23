# WireGuard lanes

This uses the [wireguard-docs](https://github.com/JebetrexxMC/wireguard-docs) guide (pirate/wireguard-docs) as the reference.

`wg-relay.sh` puts an `mpnetd relay` box near the players and a game server far away on one WireGuard tunnel. The relay then reaches the server over the tunnel (a fixed, encrypted path) instead of the open internet.

It helps when the route between the relay box and the server is better than what players get from their ISP. It also lets you close the game port on the server to everyone except the relay.

```
# on the game server (gets 10.77.0.1):
sudo bash net/wireguard/wg-relay.sh server
# on the relay box (gets 10.77.0.2), with the server's public key + address printed above:
sudo bash net/wireguard/wg-relay.sh relay <server-public-key> <server-ip>:51820
mpnetd relay -l 7777 -u 10.77.0.1:7777 --kind samp --http 8091
```

The MTU is 1380, so RakNet packets (max 1400) aren't split. WireGuard adds about 60 bytes on IPv4, and phone networks often have a smaller MTU than 1500. `PersistentKeepalive = 25` keeps NAT mappings open.

The scripts need root and the `wireguard-tools` package.
