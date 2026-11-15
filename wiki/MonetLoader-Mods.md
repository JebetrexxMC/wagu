# MonetLoader mods

![Mods](../Screenshots/gifs/monetloader.gif)

`moonloader/mods_loader.lua` starts every `.lua` right inside `moonloader/mods/`. Old copies left in `moonloader/` are stopped and renamed to `.lua.old`.

| Mod | What it does |
|---|---|
| `mods/tvplayer.lua` | Plays video on the TV model in houses |
| `mods/hitmarker.lua` | Hit markers from MP's real hits (`MP_PopHit`) |
| `mods/hg.lua` | HG menu, opened with `/hg` |

## When HG runs

HG and the CLEO menu are cheat mods.

They run:
- offline (single player)
- on custom servers whose owner allows CheatMenu + MonetLoader cheats (see [Server Management](Server-Management.md))

They never run:
- on the default server or LAN server
- on featured servers
- with "Block menu cheats"

When the owner turns cheats off, HG stops at once. It starts again where it is allowed.
