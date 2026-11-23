# cjdns: MP over an encrypted IPv6 mesh

[cjdns](https://github.com/JebetrexxMC/cjdns) gives every box an IPv6 address in `fc00::/8` made from its public key. Traffic between two cjdns nodes is end-to-end encrypted and routed over the mesh (peers you add, or the public peering list), with no port forwarding.

What it's good for in MP:
- Admin lanes and MQTT between your own boxes when they sit behind NAT. This is the same job as ZeroTier, but without a controller.
- A LAN party mesh (see `net/raspap`).

What it isn't: a way for players to join. The SA-MP / open.mp client and the phone game only speak IPv4. A player would need cjdns installed, and the server would need to listen on its `fc..` address. To use it, run `mpnetd relay` on a cjdns node that also has a public IPv4 address, and give players that IPv4.

```
sudo bash net/cjdns/setup.sh                 # build + config + systemd (Debian / Ubuntu / Raspberry Pi OS)
sudo bash net/cjdns/setup.sh peer <ip:port> <password> <publicKey>   # add a peer
ip -6 addr show tun0                          # your fc.. address
```

Add a wire to `net/wires.txt` to watch a box over the mesh: `my-box-cjdns  custom  tcp  [fc12:...]  22  My box over cjdns`. The GitHub probe can't reach the mesh, so run `mpnetd watch` on a cjdns node for these.
