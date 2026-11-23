# Kuma: service mesh for the mpnet web side

[Kuma](https://github.com/JebetrexxMC/kuma) is an Envoy-based service mesh for Kubernetes and VMs. In MP it only fits the HTTP / WebSocket services: `mpnet-beast` (:8090) and `mpnet-ws` (:8091) from `net/server`, which feed the website.

`mpnet-mesh.yaml`:
- enables sidecar injection in the `mpnet` namespace;
- annotations that keep UDP 7777 (RakNet + SA-MP query) out of the sidecar. A proxy hop there would add latency, and Envoy doesn't speak RakNet;
- a `MeshTrafficPermission` (the fork's current `rules` / `spiffeID` schema) so only workloads in `mpnet` can call mpnet-beast.

```
kumactl install control-plane | kubectl apply -f -
kubectl apply -f net/kuma/mpnet-mesh.yaml
```
