# OpenSnitch: an application firewall on the MP box

[OpenSnitch](https://github.com/JebetrexxMC/opensnitch) is an interactive application firewall for Linux: it asks per program before a connection goes out. That is useful on a relay or server box, where only a few programs should talk to the internet.

`mp-allow-game.json` is a rule in OpenSnitch's own JSON format. It allows `mpnetd` to send UDP (wire probes and the game ports) without a prompt every time:

```
sudo cp net/opensnitch/mp-allow-game.json /etc/opensnitchd/rules/
sudo systemctl restart opensnitch     # or wait: the daemon reloads changed rules
```

Copy it for the game server binary (change `process.path`). OpenSnitch filters outgoing connections. Incoming 7777 is still the job of the normal firewall / `net/query-shield`.
