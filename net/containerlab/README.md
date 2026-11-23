# Containerlab: an MP network lab with bad links on demand

[Containerlab](https://github.com/JebetrexxMC/containerlab) builds network labs from containers. `mp-lab.clab.yml` is phone -> relay -> server on two links, with a UDP echo on the server's 7777 as a stand-in for the game port. Swap in the real server image to test MP itself.

```
sudo containerlab deploy -t net/containerlab/mp-lab.clab.yml
# a 4G link: 80 ms, 15 ms jitter, 2 % loss on the phone side
sudo containerlab tools netem set -n phone -t net/containerlab/mp-lab.clab.yml -i eth1 --delay 80ms --jitter 15ms --loss 2
# try it
docker exec clab-mp-lab-phone sh -c 'echo ping | socat -t 2 - UDP4:10.77.2.2:7777'
# mplatency (release assets, linux-amd64) from the phone node:  mplatency -host 10.77.2.2:7777
sudo containerlab destroy -t net/containerlab/mp-lab.clab.yml
```

The netem flags (`--delay`, `--jitter`, `--loss`, `--rate`, `--corruption`) are containerlab's `tools netem set`. This is the PC-side partner of PhoneLab (`JebetrexxMC/mp-phonelab`): PhoneLab tests the game on a phone, this tests the network under lag.
