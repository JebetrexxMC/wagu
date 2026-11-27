# CoopFusion32

AML co-op mod for **GTA San Andreas 2.00 (armeabi-v7a)**, by our collaborator, now built and shipped with MP.

- Mod: `net.coopfusion32.mobile`, needs SAUtils (`net.rusjj.gtasa.utils`) 1.6. Default port 6767, LAN discovery on port + 1.
- Build: `cd CoopFusion32 && ./build.sh` (NDK r27) or the **CoopFusion32** workflow (`.github/workflows/coopfusion.yml`, artifact `libCoopFusion32`). The release copy is `bin/libCoopFusion32.so`.
- Server: `node CoopFusion32/server/index.mjs` (env `PORT`, `NAME`, `MAX_PLAYERS`).
- Public servers: add them to `servers.json` (`{"servers":[{"name":"...","host":"...","port":6767}]}`); AML Multiplayer lists them in its Co-op tab.

## 1.1
- Combat sync: deaths carry the killer (last player who hurt you) and weapon; the server broadcasts `KILL|killerId|killerName|victimId|victimName|weapon`; kill feed top right.
- `INFO|name|players|max|1.1` answer for server lists, `COOP_DISCOVER` UDP on port + 1, the host's name.
- `AutoConnect=1` in `[CoopFusion]` joins `ServerHost:ServerPort` once the game is loaded (AML Multiplayer crossplay).
