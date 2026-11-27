# CoopFusion32

`CoopFusion32/` is our collaborator's AML co-op mod for GTA San Andreas 2.00, kept exactly as they uploaded it (`research/CoopFusion32-main.zip`). We don't edit, build or ship it. Changes to the mod are up to its author.

## Crossplay with MP

MP (libMP) handles the crossplay on its side, so CoopFusion32 players and MP players can meet without touching the mod:

- `src/net/coopgate.cpp` connects to a stock CoopFusion32 server (`node CoopFusion32/coopfusion/server/index.mjs`, TCP port 6767) as a normal co-op player using the mod's own protocol (`JOIN`, `INPUT`, `PLAYER`, `VEHICLE`, `REMOVE`, ...).
- `src/mp_coopfusion.inc` runs a private MP server on 127.0.0.1 and joins it. Co-op players show up in it as "Name (Co-op)" players, cars and passengers included, and the phone shows up on the co-op server under its MP name.
- Combat sync uses the mod's own messages: MP hits become `PLAYER_HEALTH` (and `PLAYER_HIT` for melee), MP gunshots become `SHOTX` so the co-op side replays the bullets, and co-op `PLAYER_HIT`, `PLAYER_VITALS` and `SHOT` turn into MP damage with the right attacker and weapon. Kills of co-op players count for the MP player who hit them.
- Works on both 32-bit and 64-bit GTA SA, because it's part of libMP and doesn't need the mod loaded.

## Public servers

Add co-op servers to `servers.json` (`{"servers":[{"name":"...","host":"...","port":6767}]}`). AML Multiplayer lists them in its Co-op tab and joins them through libMP (`coop|host|port|name`).
