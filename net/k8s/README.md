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
