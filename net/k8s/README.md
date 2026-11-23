# MP NetWire on Kubernetes

| File | What |
|---|---|
| `Dockerfile` | `mpnetd` in a small Debian image (watch by default, relay with other args) |
| `mpnet.yaml` | namespace `mpnet`: the status watcher (Service, HTTP) and a UDP relay (LoadBalancer, `externalTrafficPolicy: Local`) |
| `networkpolicy.yaml` | deny-all + allow rules after [kubernetes-network-policy-recipes](https://github.com/JebetrexxMC/kubernetes-network-policy-recipes) |
| `calico-policy.yaml` | a [Calico](https://github.com/JebetrexxMC/calico) GlobalNetworkPolicy for the relay edge (Calico clusters only) |
| `10-mpnet.conflist` | a [CNI](https://github.com/JebetrexxMC/cni) bridge network for a single box (containerd / nerdctl / podman without Kubernetes) |

```sh
docker build -f net/k8s/Dockerfile -t ghcr.io/jebetrexxmc/mpnetd:latest .
kubectl apply -f net/k8s/mpnet.yaml -f net/k8s/networkpolicy.yaml
kubectl -n mpnet get svc mpnet-relay     # the public UDP address players join
```

CI builds the image and validates the YAML (`.github/workflows/netwire-247.yml`, job `deploy`). It is not deployed anywhere: this is for your own cluster or VPS.

## More tools

| tool | how |
|---|---|
| [Knative Serving](https://github.com/JebetrexxMC/serving) | `knative-service.yaml`: mpnet-beast (HTTP query bridge) scaled to zero. HTTP only, so the UDP relay is not on Knative. |
| [kubefwd](https://github.com/JebetrexxMC/kubefwd) | `sudo kubefwd svc -n mpnet` puts the services on your PC under their names: `curl http://mpnet-watch/wires.json`. TCP only (kubefwd forwards with port-forward), so test the relay through its LoadBalancer address. |
| [Hubble](https://github.com/JebetrexxMC/hubble) | on Cilium clusters: `hubble observe -n mpnet --protocol udp --port 7777 -f` shows every player flow to the relay, and `--verdict DROPPED` shows what the NetworkPolicy blocked. |
