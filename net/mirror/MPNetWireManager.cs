// MPNetWireManager: a Mirror NetworkManager (fork: github.com/JebetrexxMC/Mirror) for Unity games and tools that
// sit next to MP. Before StartClient() it reads the MP NetWire status (netwire-status/wires.json, the same file as
// the website and the PC launcher), picks the best "up" wire of a group (default: relay) and joins that address.
// Not compiled in CI (needs Unity + Mirror); drop it into Assets/ of a Mirror project.
using System;
using System.Collections;
using System.Collections.Generic;
using Mirror;
using UnityEngine;
using UnityEngine.Networking;

public class MPNetWireManager : NetworkManager
{
    [Header("MP NetWire")]
    public string statusUrl = "https://raw.githubusercontent.com/JebetrexxMC/wagu/netwire-status/wires.json";
    public string wireGroup = "relay";
    public float timeoutSeconds = 8f;

    [Serializable] class Wire { public string id, group, kind, host, name, grade; public int port; public float rtt = -1, loss = 1; }
    [Serializable] class Status { public int total, up; public List<Wire> wires; }

    /// Picks the best wire of wireGroup and starts the client on it (falls back to networkAddress).
    public void StartClientBestWire() => StartCoroutine(PickAndStart());

    IEnumerator PickAndStart()
    {
        using (var req = UnityWebRequest.Get(statusUrl))
        {
            req.timeout = Mathf.CeilToInt(timeoutSeconds);
            yield return req.SendWebRequest();
            if (req.result == UnityWebRequest.Result.Success)
            {
                var s = JsonUtility.FromJson<Status>(req.downloadHandler.text);
                Wire best = null;
                foreach (var w in s?.wires ?? new List<Wire>())
                {
                    if (w.group != wireGroup || w.grade == "down" || w.grade == "skip" || w.rtt < 0) continue;
                    float score = w.rtt + 500f * w.loss;
                    if (best == null || score < best.rtt + 500f * best.loss) best = w;
                }
                if (best != null)
                {
                    networkAddress = best.host;
                    if (transport is PortTransport pt) pt.Port = (ushort)best.port;
                    Debug.Log($"MP NetWire: {best.name} {best.host}:{best.port} ({best.rtt:0} ms, {best.grade})");
                }
                else Debug.LogWarning($"MP NetWire: no wire of group '{wireGroup}' is up - using {networkAddress}");
            }
            else Debug.LogWarning($"MP NetWire status: {req.error} - using {networkAddress}");
        }
        StartClient();
    }
}
