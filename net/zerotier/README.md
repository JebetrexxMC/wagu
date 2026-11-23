# ZeroTier for MP

[ZeroTier](https://github.com/JebetrexxMC/ZeroTierOne) makes a private virtual LAN over the internet. Every device on the network gets a fixed IP (like `10.147.17.5`) and can reach the others directly, peer to peer when possible. For MP that means:

- **MP LAN Server works over the internet**: host it on a PC, VPS or Oracle box, and friends on the same ZeroTier network join it by ZeroTier IP, no port forwarding, no playit tunnel.
- **UDP works** (unlike bore), so the game ports 1922 / 1930 / 1931 and open.mp 7777 go through.
- Usually close to direct ping, because the traffic goes peer to peer. It only goes through ZeroTier's relays when both sides are behind strict NAT.

## Set up

1. Create a free network at [my.zerotier.com](https://my.zerotier.com) and copy the 16-character Network ID.
2. Server (VPS / Oracle / Linux PC): `sudo bash net/zerotier/zerotier-join.sh <NETWORK_ID>`, then approve it under **Members**.
3. Windows / macOS PC: install ZeroTier One from [zerotier.com/download](https://www.zerotier.com/download/) and join the ID.
4. Phone: install **ZeroTier One** from Google Play / App Store and join the same ID (it runs as a VPN, so Termux does not need root).
5. Add the server to `net/wires.txt` so it shows on the Network page:
   ```
   zt-home | zerotier | mp | 10.147.17.5:1922 | MP LAN over ZeroTier
   ```

The GitHub runners are not on your ZeroTier network, so on the Network page a `zerotier` wire is checked by a box that is (run `mpnetd watch` on any member). Latest ZeroTier One: 1.16.2.
